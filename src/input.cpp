// Motion controllers through OpenXR actions. The right controller's aim pose steers the
// player's look angles (see aim.cpp); its buttons and sticks are injected into BF3 as the
// keyboard and mouse input the game already understands.

#include "input.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "ammo.h"
#include "config.h"
#include "hands.h"
#include "log.h"
#include "pad.h"
#include "reload.h"
#include "skin.h"
#include "sounds.h"
#include "window.h"

namespace {

XrInstance g_instance = XR_NULL_HANDLE;
XrActionSet g_set = XR_NULL_HANDLE;
XrAction g_aim = XR_NULL_HANDLE;
XrAction g_fire = XR_NULL_HANDLE;
XrAction g_ads = XR_NULL_HANDLE;
XrAction g_grenade = XR_NULL_HANDLE;
XrAction g_move = XR_NULL_HANDLE;
XrAction g_turn = XR_NULL_HANDLE;
XrAction g_jump = XR_NULL_HANDLE;
XrAction g_crouch = XR_NULL_HANDLE;
XrAction g_reload = XR_NULL_HANDLE;
XrAction g_use = XR_NULL_HANDLE;
XrAction g_sprint = XR_NULL_HANDLE;
XrSpace g_aimSpace = XR_NULL_HANDLE;
// Both hands' grip poses (left 0, right 1), for drawing the hands where the controllers are.
XrAction g_hand = XR_NULL_HANDLE;
// The left grip, squeezed: the left glove's middle, ring and little fingers curl (its trigger, aim
// down sights, curls the index finger). The Steam Frame's left View button: lines the left glove
// up with the hand.
XrAction g_squeeze = XR_NULL_HANDLE;
XrAction g_calibrate = XR_NULL_HANDLE;
// The left thumb resting on the stick: the glove's thumb folds in.
XrAction g_thumbTouch = XR_NULL_HANDLE;
float g_leftSqueeze = 0.0f, g_leftTrigger = 0.0f;
bool g_leftThumb = false;
bool g_calibrateDown = false, g_calibratePressed = false;
XrPath g_handPaths[2] = {};
XrSpace g_handSpaces[2] = {};
XrPosef g_handPoses[2] = {};
bool g_handTracked[2] = {};

bool g_ready = false;
bool g_aimTracked = false;
XrPosef g_aimPose{};

// The stand-in hand (weapon.txt "standin 1"): while the right controller isn't tracked (nobody in
// the headset, the controllers asleep), a right hand holds the rifle out in front, aiming where the
// head looks, so the drawn gun, its effects and the aim can be tested without the user. Its grip
// pose sits from its aim pose as the real controller's do: learned whenever the right controller
// is tracked and kept in build\standin.txt for the next run.
std::atomic<bool> g_standIn{false};
bool g_standingIn = false;  // holding the gun this frame
XrPosef g_aimToGrip{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};  // the grip in the aim pose's frame
bool g_haveAimToGrip = false;
ULONGLONG g_aimToGripSavedAt = 0;
// Where it aims from in the head's frame (x right, y up, z back; metres): a rifle shouldered.
constexpr XrVector3f kStandInAim{0.08f, -0.10f, -0.30f};

XrQuaternionf Mul(const XrQuaternionf& a, const XrQuaternionf& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

XrQuaternionf Conjugate(const XrQuaternionf& q) {
    return {-q.x, -q.y, -q.z, q.w};
}

XrVector3f Rotate(const XrQuaternionf& q, const XrVector3f& v) {
    const XrQuaternionf r = Mul(Mul(q, {v.x, v.y, v.z, 0.0f}), Conjugate(q));
    return {r.x, r.y, r.z};
}

std::wstring StandInPath() {
    wchar_t path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&StandInPath), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\'))
        wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"standin.txt");
    return path;
}

// The real controller's grip seen from its aim pose, each frame both are tracked; saved once a
// minute (and the first time).
void LearnAimToGrip() {
    if (!g_aimTracked || !g_handTracked[1])
        return;
    const XrQuaternionf inverse = Conjugate(g_aimPose.orientation);
    const XrPosef& grip = g_handPoses[1];
    g_aimToGrip.orientation = Mul(inverse, grip.orientation);
    g_aimToGrip.position = Rotate(inverse, {grip.position.x - g_aimPose.position.x,
                                            grip.position.y - g_aimPose.position.y,
                                            grip.position.z - g_aimPose.position.z});
    g_haveAimToGrip = true;
    const ULONGLONG now = GetTickCount64();
    if (g_aimToGripSavedAt && now - g_aimToGripSavedAt < 60000)
        return;
    g_aimToGripSavedAt = now;
    FILE* f = nullptr;
    if (_wfopen_s(&f, StandInPath().c_str(), L"w") == 0 && f) {
        const XrPosef& p = g_aimToGrip;
        fprintf(f, "%f %f %f %f %f %f %f\n", p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w,
                p.position.x, p.position.y, p.position.z);
        fclose(f);
    }
}

void LoadAimToGrip() {
    FILE* f = nullptr;
    if (_wfopen_s(&f, StandInPath().c_str(), L"r") != 0 || !f)
        return;
    XrPosef p{};
    if (fscanf_s(f, "%f %f %f %f %f %f %f", &p.orientation.x, &p.orientation.y, &p.orientation.z, &p.orientation.w,
                 &p.position.x, &p.position.y, &p.position.z) == 7) {
        g_aimToGrip = p;
        g_haveAimToGrip = true;
        Log("Stand-in hand: the controller's grip from its aim as last seen (build\\standin.txt)");
    }
    fclose(f);
}

// The smooth turn. Through the mouse it stuttered: BF3 reads the mouse on a rhythm of its own, and
// one move a frame landed on both sides of a read about 6 times a second, so a frame barely turned
// and the next turned double (logged 2026-10-30). In stereo the stick's turn now goes into the look
// yaw the mod writes each frame (aim.cpp), which the picture follows, so it's drawn in step with
// the frames. Otherwise (theater mode, no [aim] yaw) a thread moves the mouse a little every
// millisecond. The stick (or F17 test), the mouse thread's speed (counts a second), the direct
// turn's stick and when it was last taken, and when the turn started and was last asked for (ms,
// QueryPerformanceCounter).
float g_stickTurn = 0.0f;
float g_moveTurn = 0.0f;  // the head's angle from the game's facing (SetMoveTurn)
int g_turnTestFrames = 0;
std::atomic<float> g_turnRate{0.0f};
bool g_turnThread = false;
float g_directTurn = 0.0f;
double g_lastTake = 0.0;
std::atomic<double> g_turnSince{0.0}, g_turnLast{0.0};

// DirectInput scan codes, which BF3 reads.
constexpr WORD kScanW = 0x11, kScanA = 0x1E, kScanS = 0x1F, kScanD = 0x20;
constexpr WORD kScanSpace = 0x39, kScanCtrl = 0x1D, kScanR = 0x13, kScanE = 0x12;
constexpr WORD kScanShift = 0x2A, kScanG = 0x22;

enum class Output { Key, LeftMouse, RightMouse };

// One controller input mapped to one key or mouse button, sent only when it changes.
struct Mapping {
    XrAction* action;
    bool analog;  // float action pressed past a threshold, rather than a button
    Output output;
    WORD scan;
    bool down;
};

Mapping g_buttons[] = {
    {&g_fire, true, Output::LeftMouse, 0, false},  {&g_ads, true, Output::RightMouse, 0, false},
    {&g_grenade, true, Output::Key, kScanG, false}, {&g_jump, false, Output::Key, kScanSpace, false},
    {&g_crouch, false, Output::Key, kScanCtrl, false}, {&g_use, false, Output::Key, kScanE, false},
    {&g_sprint, false, Output::Key, kScanShift, false},
};

// X (g_reload): a tap ejects the magazine, which drops to the floor, or with it out puts a new
// one in. Held for 0.6 s it's the game's own reload (R), as is a tap with a gun the mod doesn't
// know the magazine of.
constexpr ULONGLONG kHoldForReload = 600;
bool g_xDown = false, g_xHeld = false;
ULONGLONG g_xSince = 0;
bool g_rDown = false;
ULONGLONG g_rUntil = 0;  // a tapped R is let go then

// Left stick to W/A/S/D, each pressed past half deflection.
struct StickKey {
    WORD scan;
    bool down;
};
StickKey g_moveKeys[4] = {{kScanW, false}, {kScanS, false}, {kScanA, false}, {kScanD, false}};

bool Check(XrResult result, const char* what) {
    if (XR_SUCCEEDED(result))
        return true;
    char name[XR_MAX_RESULT_STRING_SIZE] = "?";
    xrResultToString(g_instance, result, name);
    Log("Controllers: %s failed: %s", what, name);
    return false;
}

XrPath Path(const char* text) {
    XrPath path = XR_NULL_PATH;
    xrStringToPath(g_instance, text, &path);
    return path;
}

bool MakeAction(XrActionType type, const char* name, const char* label, XrAction& out) {
    XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
    ci.actionType = type;
    strcpy_s(ci.actionName, name);
    strcpy_s(ci.localizedActionName, label);
    return Check(xrCreateAction(g_set, &ci, &out), name);
}

// A runtime may not know every profile; that profile is skipped and the others still apply. Every
// profile also gets both hands' grip poses, and all but the simple one the left grip's squeeze.
bool Suggest(const char* profile, const std::vector<std::pair<XrAction, const char*>>& bindings,
             bool leftSqueeze = true) {
    std::vector<XrActionSuggestedBinding> suggested;
    for (const auto& b : bindings)
        suggested.push_back({b.first, Path(b.second)});
    suggested.push_back({g_hand, Path("/user/hand/left/input/grip/pose")});
    suggested.push_back({g_hand, Path("/user/hand/right/input/grip/pose")});
    if (leftSqueeze) {
        suggested.push_back({g_squeeze, Path("/user/hand/left/input/squeeze/value")});
        suggested.push_back({g_thumbTouch, Path("/user/hand/left/input/thumbstick/touch")});
    }
    XrInteractionProfileSuggestedBinding info{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    info.interactionProfile = Path(profile);
    info.countSuggestedBindings = static_cast<uint32_t>(suggested.size());
    info.suggestedBindings = suggested.data();
    if (XR_SUCCEEDED(xrSuggestInteractionProfileBindings(g_instance, &info))) {
        Log("Controllers: bindings for %s accepted (%zu)", profile, suggested.size());
        return true;
    }
    Log("Controllers: no bindings for %s on this runtime (%zu tried)", profile, suggested.size());
    return false;
}

void SuggestBindings() {
    // Valve Index: A/B on both hands.
    Suggest("/interaction_profiles/valve/index_controller",
            {{g_aim, "/user/hand/right/input/aim/pose"},
             {g_fire, "/user/hand/right/input/trigger/value"},
             {g_ads, "/user/hand/left/input/trigger/value"},
             {g_grenade, "/user/hand/right/input/squeeze/value"},
             {g_move, "/user/hand/left/input/thumbstick"},
             {g_turn, "/user/hand/right/input/thumbstick"},
             {g_jump, "/user/hand/right/input/a/click"},
             {g_crouch, "/user/hand/right/input/b/click"},
             {g_reload, "/user/hand/left/input/a/click"},
             {g_use, "/user/hand/left/input/b/click"},
             {g_sprint, "/user/hand/left/input/thumbstick/click"}});
    // Quest / Touch style: A/B right, X/Y left. SteamVR also uses this for many other controllers.
    Suggest("/interaction_profiles/oculus/touch_controller",
            {{g_aim, "/user/hand/right/input/aim/pose"},
             {g_fire, "/user/hand/right/input/trigger/value"},
             {g_ads, "/user/hand/left/input/trigger/value"},
             {g_grenade, "/user/hand/right/input/squeeze/value"},
             {g_move, "/user/hand/left/input/thumbstick"},
             {g_turn, "/user/hand/right/input/thumbstick"},
             {g_jump, "/user/hand/right/input/a/click"},
             {g_crouch, "/user/hand/right/input/b/click"},
             {g_reload, "/user/hand/left/input/x/click"},
             {g_use, "/user/hand/left/input/y/click"},
             {g_sprint, "/user/hand/left/input/thumbstick/click"}});
    // Steam Frame (when the runtime offers its extension): A/B/X/Y all on the right hand, a D-pad
    // on the left. X is the magazine. Its grip's name isn't documented: squeeze, else grip.
    std::vector<std::pair<XrAction, const char*>> frame = {
        {g_aim, "/user/hand/right/input/aim/pose"},
        {g_fire, "/user/hand/right/input/trigger/value"},
        {g_ads, "/user/hand/left/input/trigger/value"},
        {g_move, "/user/hand/left/input/thumbstick"},
        {g_turn, "/user/hand/right/input/thumbstick"},
        {g_jump, "/user/hand/right/input/a/click"},
        {g_crouch, "/user/hand/right/input/b/click"},
        {g_reload, "/user/hand/right/input/x/click"},
        {g_use, "/user/hand/right/input/y/click"},
        {g_sprint, "/user/hand/left/input/thumbstick/click"}};
    const char* kFrame = "/interaction_profiles/valve/frame_controller";
    // Its grips are squeeze or grip; the View button may not be offered: each try leaves out more.
    auto with = [&](bool squeeze, bool grip, bool view) {
        auto b = frame;
        if (squeeze) {
            b.push_back({g_grenade, "/user/hand/right/input/squeeze/value"});
            b.push_back({g_squeeze, "/user/hand/left/input/squeeze/value"});
        } else if (grip) {
            b.push_back({g_grenade, "/user/hand/right/input/grip/value"});
            b.push_back({g_squeeze, "/user/hand/left/input/grip/value"});
        }
        if (view) {
            b.push_back({g_calibrate, "/user/hand/left/input/view/click"});
            b.push_back({g_thumbTouch, "/user/hand/left/input/thumbstick/touch"});
        }
        return b;
    };
    if (!Suggest(kFrame, with(true, false, true), false) && !Suggest(kFrame, with(false, true, true), false) &&
        !Suggest(kFrame, with(true, false, false), false) && !Suggest(kFrame, with(false, true, false), false))
        Suggest(kFrame, with(false, false, false), false);
    // Anything else: at least aim and fire.
    Suggest("/interaction_profiles/khr/simple_controller",
            {{g_aim, "/user/hand/right/input/aim/pose"}, {g_fire, "/user/hand/right/input/select/click"}});
}

void SendKey(WORD scan, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = scan;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &in, sizeof(in));
}

// A click reaches the game and also the window under the pointer, so a press only goes with the
// pointer on BF3's window (window.cpp): on the taskbar's "Show desktop" corner, every shot
// minimised the game (2026-10-30). A release always goes, so a button never sticks down.
bool g_mouseDown[2] = {};  // left, right: as the game was last sent them

void SendMouseButton(bool left, bool down) {
    bool& sent = g_mouseDown[left ? 0 : 1];
    if (down == sent)
        return;
    if (!window::PointerOnGame() && down) {
        static ULONGLONG lastLog = 0;
        if (GetTickCount64() - lastLog > 1000) {
            lastLog = GetTickCount64();
            Log("Input: a %s click held back; the mouse pointer couldn't be put on the game's window",
                left ? "fire" : "right");
        }
        return;
    }
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = left ? (down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP)
                         : (down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP);
    SendInput(1, &in, sizeof(in));
    sent = down;
}

void SendMouseMove(int dx) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(in));
}

double NowMs() {
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return 1000.0 * static_cast<double>(now.QuadPart) / static_cast<double>(frequency.QuadPart);
}

// Injected input goes to whichever window has focus; never type into anything but BF3.
bool GameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// Moves the mouse at g_turnRate, carrying the fractions over. It stops on its own when the frames
// stop asking (a loading screen, a stalled game) or BF3 loses focus.
DWORD WINAPI TurnThread(LPVOID) {
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    double carry = 0.0, last = NowMs();
    for (;;) {
        LARGE_INTEGER due;
        due.QuadPart = -10000;  // 1 ms
        if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
            WaitForSingleObject(timer, INFINITE);
        else
            Sleep(1);
        const double now = NowMs(), elapsed = std::min(now - last, 20.0);
        last = now;
        const float rate = g_turnRate.load();
        if (rate == 0.0f || now - g_turnLast.load() > 50.0 || !GameHasFocus()) {
            carry = 0.0;
            continue;
        }
        carry += rate * elapsed / 1000.0;
        if (int dx = static_cast<int>(carry)) {  // toward zero, the rest carried
            carry -= dx;
            SendMouseMove(dx);
        }
    }
}

// How far the stick at x (-1 to 1) is past its dead zone (0 to 1).
float TurnAmount(float x) {
    return std::fabs(x) > 0.2f ? (std::fabs(x) - 0.2f) / 0.8f : 0.0f;
}

void NoteTurning() {
    const double now = NowMs();
    if (now - g_turnLast.load() > 40.0)
        g_turnSince.store(now);
    g_turnLast.store(now);
}

// The mouse thread's speed for the stick at x (inside the dead zone: none). turn_speed is the
// mouse counts of a 72nd of a second at full deflection.
void SetMouseTurn(float x) {
    const float amount = TurnAmount(x);
    if (amount == 0.0f) {
        g_turnRate.store(0.0f);
        return;
    }
    if (!g_turnThread) {
        g_turnThread = true;
        if (HANDLE t = CreateThread(nullptr, 0, TurnThread, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    NoteTurning();
    g_turnRate.store(std::copysign(amount * config::Get().turnSpeed * 72.0f, x));
}

// The stick turn seen from frame to frame while it turns steadily: 0 = the game's own camera as
// built (views.cpp), 1 = what is shown (the look yaw written this frame against the controller's,
// xr_runtime.cpp). Even steps are a smooth turn; a frame without a step and then a double one is a
// stutter. Logged once a second while turning.
struct TurnSteps {
    const char* name;
    float lastYaw = 0.0f;
    bool haveLast = false;
    std::vector<float> steps;  // degrees
    ULONGLONG lastLog = 0;
};
TurnSteps g_turnSteps[2] = {{"the game's camera"}, {"shown"}};

bool TurningSteadily() {
    const double now = NowMs();
    return now - g_turnLast.load() < 40.0 && now - g_turnSince.load() > 100.0;
}

void Press(const Mapping& m, bool down) {
    if (m.output == Output::Key)
        SendKey(m.scan, down);
    else
        SendMouseButton(m.output == Output::LeftMouse, down);
}

void PressR(bool down) {
    if (down != g_rDown) {
        SendKey(kScanR, down);
        g_rDown = down;
    }
}

void ReleaseAll() {
    for (Mapping& m : g_buttons)
        if (m.down)
            Press(m, false), m.down = false;
    for (StickKey& k : g_moveKeys)
        if (k.down)
            SendKey(k.scan, false), k.down = false;
    PressR(false);
    g_xDown = g_xHeld = false;
    g_stickTurn = 0.0f;
}

// The gun doesn't fire with its magazine out, nor its last round: run dry, the game would reload
// by itself. (Guns loaded a round at a time, like the SMAW, aren't stopped.) Says why, or null.
const char* FireBlocked() {
    if (!skin::RifleInHand())
        return nullptr;
    if (skin::MagazineOut())
        return "the magazine is out";
    if (reload::BoltBack())  // charge it first
        return "the bolt is locked back";
    int loaded = 0, spare = 0;
    if (ammo::MagazineSize() >= 5 && ammo::Get(loaded, spare) && loaded <= 1)
        return "the magazine is empty";
    return nullptr;
}
bool g_fireToldWhy = false;  // logged once a pull

XrVector2f Stick(XrSession session, XrAction action) {
    XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
    get.action = action;
    XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
    if (XR_FAILED(xrGetActionStateVector2f(session, &get, &state)) || !state.isActive)
        return {0.0f, 0.0f};
    return state.currentState;
}

}

namespace input {

bool Init(XrInstance instance, XrSession session) {
    g_instance = instance;
    XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy_s(si.actionSetName, "gameplay");
    strcpy_s(si.localizedActionSetName, "Gameplay");
    if (!Check(xrCreateActionSet(instance, &si, &g_set), "xrCreateActionSet"))
        return false;

    g_handPaths[0] = Path("/user/hand/left");
    g_handPaths[1] = Path("/user/hand/right");
    XrActionCreateInfo hand{XR_TYPE_ACTION_CREATE_INFO};
    hand.actionType = XR_ACTION_TYPE_POSE_INPUT;
    strcpy_s(hand.actionName, "hand");
    strcpy_s(hand.localizedActionName, "Hands");
    hand.countSubactionPaths = 2;
    hand.subactionPaths = g_handPaths;
    if (!Check(xrCreateAction(g_set, &hand, &g_hand), "hand"))
        return false;
    if (!MakeAction(XR_ACTION_TYPE_FLOAT_INPUT, "left_squeeze", "Close the left hand", g_squeeze) ||
        !MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "calibrate_left", "Line up the left hand", g_calibrate) ||
        !MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "left_thumb_touch", "Left thumb on the stick", g_thumbTouch))
        return false;
    bool ok = MakeAction(XR_ACTION_TYPE_POSE_INPUT, "aim", "Aim (right hand)", g_aim) &&
              MakeAction(XR_ACTION_TYPE_FLOAT_INPUT, "fire", "Fire", g_fire) &&
              MakeAction(XR_ACTION_TYPE_FLOAT_INPUT, "aim_down_sights", "Aim down sights", g_ads) &&
              MakeAction(XR_ACTION_TYPE_FLOAT_INPUT, "grenade", "Grenade", g_grenade) &&
              MakeAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "move", "Move", g_move) &&
              MakeAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "turn", "Turn", g_turn) &&
              MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "jump", "Jump", g_jump) &&
              MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "crouch", "Crouch", g_crouch) &&
              MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "reload", "Magazine (tap: eject / insert, hold: reload)",
                         g_reload) &&
              MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "use", "Use", g_use) &&
              MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "sprint", "Sprint", g_sprint);
    if (!ok)
        return false;
    SuggestBindings();

    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &g_set;
    if (!Check(xrAttachSessionActionSets(session, &attach), "xrAttachSessionActionSets"))
        return false;

    XrActionSpaceCreateInfo space{XR_TYPE_ACTION_SPACE_CREATE_INFO};
    space.action = g_aim;
    space.poseInActionSpace.orientation.w = 1.0f;
    if (!Check(xrCreateActionSpace(session, &space, &g_aimSpace), "xrCreateActionSpace"))
        return false;
    for (int i = 0; i < 2; ++i) {
        XrActionSpaceCreateInfo hs{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        hs.action = g_hand;
        hs.subactionPath = g_handPaths[i];
        hs.poseInActionSpace.orientation.w = 1.0f;
        if (!Check(xrCreateActionSpace(session, &hs, &g_handSpaces[i]), "xrCreateActionSpace (hand)"))
            return false;
    }

    g_ready = true;
    Log("Controllers: ready");
    return true;
}

void Sync(XrSession session, XrSpace space, XrTime time) {
    g_aimTracked = false;
    if (!g_ready)
        return;
    XrActiveActionSet active{g_set, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (xrSyncActions(session, &sync) != XR_SUCCESS) {  // also XR_SESSION_NOT_FOCUSED
        ReleaseAll();
        return;
    }
    // Which controller profile the runtime picked (it decides what each button does).
    static XrPath rightHand = Path("/user/hand/right"), profile = XR_NULL_PATH;
    XrInteractionProfileState state{XR_TYPE_INTERACTION_PROFILE_STATE};
    if (XR_SUCCEEDED(xrGetCurrentInteractionProfile(session, rightHand, &state)) &&
        state.interactionProfile != profile) {
        profile = state.interactionProfile;
        char name[XR_MAX_PATH_LENGTH] = "(none)";
        uint32_t length = 0;
        if (profile)
            xrPathToString(g_instance, profile, sizeof(name), &length, name);
        Log("Controllers: the right hand's profile is %s", name);
    }

    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    if (XR_SUCCEEDED(xrLocateSpace(g_aimSpace, space, time, &location)) &&
        (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT)) {
        g_aimPose = location.pose;
        g_aimTracked = true;
    }
    for (int i = 0; i < 2; ++i) {
        XrSpaceLocation hand{XR_TYPE_SPACE_LOCATION};
        g_handTracked[i] = XR_SUCCEEDED(xrLocateSpace(g_handSpaces[i], space, time, &hand)) &&
                           (hand.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) &&
                           (hand.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
        if (g_handTracked[i])
            g_handPoses[i] = hand.pose;
    }
    LearnAimToGrip();
    g_standingIn = false;
    {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = g_squeeze;
        XrActionStateFloat squeeze{XR_TYPE_ACTION_STATE_FLOAT};
        g_leftSqueeze = XR_SUCCEEDED(xrGetActionStateFloat(session, &get, &squeeze)) && squeeze.isActive
                            ? squeeze.currentState
                            : 0.0f;
        get.action = g_calibrate;
        XrActionStateBoolean view{XR_TYPE_ACTION_STATE_BOOLEAN};
        bool down = XR_SUCCEEDED(xrGetActionStateBoolean(session, &get, &view)) && view.isActive && view.currentState;
        if (down && !g_calibrateDown)
            g_calibratePressed = true;
        g_calibrateDown = down;
        get.action = g_thumbTouch;
        XrActionStateBoolean thumb{XR_TYPE_ACTION_STATE_BOOLEAN};
        g_leftThumb = XR_SUCCEEDED(xrGetActionStateBoolean(session, &get, &thumb)) && thumb.isActive && thumb.currentState;
        get.action = g_ads;  // the left trigger
        XrActionStateFloat trigger{XR_TYPE_ACTION_STATE_FLOAT};
        g_leftTrigger = XR_SUCCEEDED(xrGetActionStateFloat(session, &get, &trigger)) && trigger.isActive
                            ? trigger.currentState
                            : 0.0f;
    }

    if (!GameHasFocus()) {
        ReleaseAll();
        return;
    }

    for (Mapping& m : g_buttons) {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = *m.action;
        bool down = false;
        if (m.analog) {
            // A little hysteresis so a trigger resting near the threshold doesn't chatter.
            XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
            if (XR_SUCCEEDED(xrGetActionStateFloat(session, &get, &state)) && state.isActive)
                down = state.currentState > (m.down ? 0.4f : 0.6f);
        } else {
            XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
            down = XR_SUCCEEDED(xrGetActionStateBoolean(session, &get, &state)) && state.isActive &&
                   state.currentState;
        }
        if (m.action == &g_fire) {
            const char* why = down ? FireBlocked() : nullptr;
            if (why && !g_fireToldWhy) {
                int loaded = 0, spare = 0;
                ammo::Get(loaded, spare);
                Log("Input: the trigger didn't fire: %s (%d rounds loaded, %d spare)", why, loaded, spare);
            }
            g_fireToldWhy = why != nullptr;
            if (why)
                down = false;
        }
        // The left trigger is aim down sights, unless the grip is held with it: a fist. With free
        // hands the scope is looked through by holding the gun up to the eye, and the trigger
        // works the charging handle (reload.cpp) and curls the glove's index finger (user,
        // 2026-10-30).
        if (down && m.action == &g_ads && (hands::On() || g_leftSqueeze > 0.5f))
            down = false;
        if (down != m.down) {
            Press(m, down);
            m.down = down;
        }
    }

    XrActionStateGetInfo getX{XR_TYPE_ACTION_STATE_GET_INFO};
    getX.action = g_reload;
    XrActionStateBoolean xState{XR_TYPE_ACTION_STATE_BOOLEAN};
    bool x = XR_SUCCEEDED(xrGetActionStateBoolean(session, &getX, &xState)) && xState.isActive &&
             xState.currentState;
    ULONGLONG now = GetTickCount64();
    if (x && !g_xDown) {
        g_xSince = now;
        g_xHeld = false;
    }
    if (x && !g_xHeld && now - g_xSince >= kHoldForReload) {
        g_xHeld = true;
        PressR(true);            // until X is let go
        skin::InsertMagazine();  // the game's reload puts one in
        reload::ReleaseBolt();   // and chambers a round
    }
    if (!x && g_xDown) {
        if (g_xHeld)
            PressR(false);
        else
            input::MagazineTap();
    }
    g_xDown = x;
    if (g_rDown && !g_xHeld && now >= g_rUntil)
        PressR(false);

    // The left stick walks where the head faces (user, 2026-10-30: not where the gun points). The
    // game walks along its own facing, which follows the gun, so the stick is turned by the head's
    // angle from it; then it goes in as a controller's left stick (pad.cpp), or as W/A/S/D.
    XrVector2f move = Stick(session, g_move);
    const float c = std::cos(g_moveTurn), s = std::sin(g_moveTurn);
    float mx = move.x * c - move.y * s, my = move.x * s + move.y * c;
    const float length = std::sqrt(mx * mx + my * my);
    constexpr float kDeadZone = 0.15f;
    const float scale = length > kDeadZone ? (length - kDeadZone) / (1.0f - kDeadZone) / length : 0.0f;
    const bool analog = config::Get().analogMove && GameHasFocus() && pad::SetMove(mx * scale, my * scale);
    bool want[4] = {!analog && my > 0.5f, !analog && my < -0.5f, !analog && mx < -0.5f, !analog && mx > 0.5f};
    for (int i = 0; i < 4; ++i) {
        if (want[i] != g_moveKeys[i].down) {
            SendKey(g_moveKeys[i].scan, want[i]);
            g_moveKeys[i].down = want[i];
        }
    }

    g_stickTurn = Stick(session, g_turn).x;  // turned in UpdateTurn
}

void StartTurnTest() {
    g_turnTestFrames = 216;  // 3 s at 72 Hz
    Log("Turn test: 3 s of the stick held right");
}

void UpdateTurn(bool direct) {
    float x = GameHasFocus() ? g_stickTurn : 0.0f;
    if (g_turnTestFrames > 0 && GameHasFocus()) {
        --g_turnTestFrames;
        x = 1.0f;
    }
    SetMouseTurn(direct ? 0.0f : x);
    g_directTurn = direct ? x : 0.0f;
    if (direct && TurnAmount(x) > 0.0f)
        NoteTurning();
}

void SetMoveTurn(float radians) {
    g_moveTurn = radians;
}

float TakeTurn() {
    const double now = NowMs();
    double elapsed = now - g_lastTake;
    g_lastTake = now;
    if (elapsed < 0.0 || elapsed > 50.0)
        elapsed = 1000.0 / 72.0;  // after a pause, one frame's worth
    const float amount = TurnAmount(g_directTurn);
    if (amount == 0.0f)
        return 0.0f;
    // Left is positive; the stick to the right turns right.
    const float radians = amount * config::Get().turnDegrees * 0.01745329f * static_cast<float>(elapsed / 1000.0);
    return g_directTurn > 0.0f ? -radians : radians;
}

void NoteTurnStep(int which, float yaw) {
    constexpr float kPi = 3.14159265f;
    if (which < 0 || which > 1)
        return;
    TurnSteps& t = g_turnSteps[which];
    if (t.haveLast && TurningSteadily()) {
        float step = yaw - t.lastYaw;
        if (step > kPi)
            step -= 2.0f * kPi;
        else if (step < -kPi)
            step += 2.0f * kPi;
        t.steps.push_back(std::fabs(step) * 57.29578f);
    }
    t.lastYaw = yaw;
    t.haveLast = true;
    ULONGLONG now = GetTickCount64();
    if (now - t.lastLog < 1000)
        return;
    t.lastLog = now;
    if (t.steps.empty())
        return;
    std::vector<float> sorted = t.steps;
    std::sort(sorted.begin(), sorted.end());
    const float median = sorted[sorted.size() / 2];
    int none = 0, doubled = 0;
    for (float s : t.steps) {
        none += s < 0.25f * median;
        doubled += s > 1.75f * median;
    }
    Log("Turn (%s): %zu frames turning; the yaw moved %.3f degrees a frame (median), %.3f to %.3f; %d frames "
        "without a step, %d with a double one",
        t.name, t.steps.size(), median, sorted.front(), sorted.back(), none, doubled);
    if (none || doubled || sorted.back() > 1.25f * median || sorted.front() < 0.75f * median) {
        char text[512];
        int used = 0;
        for (size_t i = 0; i < t.steps.size() && i < 48 && used < 480; ++i)
            used += snprintf(text + used, sizeof(text) - used, " %.2f", t.steps[i]);
        Log("Turn (%s): steps%s", t.name, text);
    }
    t.steps.clear();
}

bool AimPose(XrPosef& out) {
    if (!g_aimTracked)
        return false;
    out = g_aimPose;
    return true;
}

float LeftSqueeze() {
    return g_leftSqueeze;
}

float LeftTrigger() {
    return g_leftTrigger;
}

bool LeftThumbOnStick() {
    return g_leftThumb;
}

bool TakeCalibratePress() {
    bool pressed = g_calibratePressed;
    g_calibratePressed = false;
    return pressed;
}

bool HandPose(int hand, XrPosef& out) {
    if (hand < 0 || hand > 1 || !g_handTracked[hand])
        return false;
    out = g_handPoses[hand];
    return true;
}

void SetStandIn(bool on) {
    if (g_standIn.exchange(on) != on)
        Log("Stand-in hand: %s", on ? "on (holds the rifle while the right controller isn't tracked)" : "off");
}

void StandIn(const XrPosef& head) {
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        LoadAimToGrip();
    }
    if (!g_standIn.load(std::memory_order_relaxed) || g_aimTracked || g_handTracked[1]) {
        g_standingIn = false;
        return;
    }
    // Aiming where the head looks, level (no roll), from the shouldered rifle's place.
    float yaw = 0.0f, pitch = 0.0f;
    {
        const XrQuaternionf& q = head.orientation;
        const float fx = -2.0f * (q.x * q.z + q.w * q.y);
        const float fy = -2.0f * (q.y * q.z - q.w * q.x);
        const float fz = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));
        yaw = std::atan2(-fx, -fz);
        pitch = std::asin(std::clamp(fy, -1.0f, 1.0f));
    }
    const XrQuaternionf turn{0.0f, std::sin(yaw / 2), 0.0f, std::cos(yaw / 2)};
    const XrQuaternionf tilt{std::sin(pitch / 2), 0.0f, 0.0f, std::cos(pitch / 2)};
    const XrQuaternionf level = Mul(turn, tilt);
    const XrVector3f at = Rotate(level, kStandInAim);
    g_aimPose.orientation = level;
    g_aimPose.position = {head.position.x + at.x, head.position.y + at.y, head.position.z + at.z};
    g_aimTracked = true;
    const XrVector3f grip = Rotate(level, g_aimToGrip.position);
    g_handPoses[1].orientation = Mul(level, g_aimToGrip.orientation);
    g_handPoses[1].position = {g_aimPose.position.x + grip.x, g_aimPose.position.y + grip.y,
                               g_aimPose.position.z + grip.z};
    g_handTracked[1] = true;
    if (!g_standingIn) {
        static ULONGLONG told = 0;
        if (GetTickCount64() - told > 10000) {
            told = GetTickCount64();
            Log("Stand-in hand: holding the rifle (the right controller isn't tracked)%s",
                g_haveAimToGrip ? "" : "; its grip is placed at its aim, never having seen the controller");
        }
    }
    g_standingIn = true;
}

bool StandingIn() {
    return g_standingIn;
}

void MagazineTap() {
    if (!skin::RifleInHand()) {
        PressR(true);
        g_rUntil = GetTickCount64() + 150;  // the game reads the keys once a frame
        return;
    }
    if (skin::MagazineOut()) {
        ammo::Insert();
        skin::InsertMagazine();
        sounds::Play("mag_in");
    } else if (skin::EjectMagazine()) {
        ammo::Eject();
        sounds::Play("mag_out");
    }
}

}
