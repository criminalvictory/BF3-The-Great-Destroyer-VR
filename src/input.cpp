// Motion controllers through OpenXR actions. The right controller's aim pose steers the
// player's look angles (see aim.cpp); its buttons and sticks are injected into BF3 as the
// keyboard and mouse input the game already understands.

#include "input.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "ammo.h"
#include "config.h"
#include "log.h"
#include "skin.h"

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
XrPath g_handPaths[2] = {};
XrSpace g_handSpaces[2] = {};
XrPosef g_handPoses[2] = {};
bool g_handTracked[2] = {};

bool g_ready = false;
bool g_aimTracked = false;
XrPosef g_aimPose{};

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
// profile also gets both hands' grip poses.
bool Suggest(const char* profile, const std::vector<std::pair<XrAction, const char*>>& bindings) {
    std::vector<XrActionSuggestedBinding> suggested;
    for (const auto& b : bindings)
        suggested.push_back({b.first, Path(b.second)});
    suggested.push_back({g_hand, Path("/user/hand/left/input/grip/pose")});
    suggested.push_back({g_hand, Path("/user/hand/right/input/grip/pose")});
    XrInteractionProfileSuggestedBinding info{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    info.interactionProfile = Path(profile);
    info.countSuggestedBindings = static_cast<uint32_t>(suggested.size());
    info.suggestedBindings = suggested.data();
    if (XR_SUCCEEDED(xrSuggestInteractionProfileBindings(g_instance, &info)))
        return true;
    Log("Controllers: no bindings for %s on this runtime", profile);
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
    auto withGrip = [&](const char* grip) {
        auto b = frame;
        b.push_back({g_grenade, grip});
        return b;
    };
    if (!Suggest(kFrame, withGrip("/user/hand/right/input/squeeze/value")) &&
        !Suggest(kFrame, withGrip("/user/hand/right/input/grip/value")))
        Suggest(kFrame, frame);
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

void SendMouseButton(bool left, bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = left ? (down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP)
                         : (down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP);
    SendInput(1, &in, sizeof(in));
}

void SendMouseMove(int dx) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(in));
}

void Press(const Mapping& m, bool down) {
    if (m.output == Output::Key)
        SendKey(m.scan, down);
    else
        SendMouseButton(m.output == Output::LeftMouse, down);
}

// Injected input goes to whichever window has focus; never type into anything but BF3.
bool GameHasFocus() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
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
}

// The gun doesn't fire with its magazine out, nor its last round: run dry, the game would reload
// by itself. (Guns loaded a round at a time, like the SMAW, aren't stopped.)
bool FireBlocked() {
    if (!skin::RifleInHand())
        return false;
    if (skin::MagazineOut())
        return true;
    int loaded = 0, spare = 0;
    return ammo::MagazineSize() >= 5 && ammo::Get(loaded, spare) && loaded <= 1;
}

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
        if (down && m.action == &g_fire && FireBlocked())
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

    XrVector2f move = Stick(session, g_move);
    bool want[4] = {move.y > 0.5f, move.y < -0.5f, move.x < -0.5f, move.x > 0.5f};
    for (int i = 0; i < 4; ++i) {
        if (want[i] != g_moveKeys[i].down) {
            SendKey(g_moveKeys[i].scan, want[i]);
            g_moveKeys[i].down = want[i];
        }
    }

    // Smooth turning: the right stick moves the mouse sideways, like turning with the mouse.
    XrVector2f turn = Stick(session, g_turn);
    if (std::fabs(turn.x) > 0.2f) {
        float speed = config::Get().turnSpeed;
        float amount = (std::fabs(turn.x) - 0.2f) / 0.8f;
        int dx = static_cast<int>(std::copysign(amount * speed, turn.x));
        if (dx)
            SendMouseMove(dx);
    }
}

bool AimPose(XrPosef& out) {
    if (!g_aimTracked)
        return false;
    out = g_aimPose;
    return true;
}

bool HandPose(int hand, XrPosef& out) {
    if (hand < 0 || hand > 1 || !g_handTracked[hand])
        return false;
    out = g_handPoses[hand];
    return true;
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
    } else if (skin::EjectMagazine()) {
        ammo::Eject();
    }
}

}
