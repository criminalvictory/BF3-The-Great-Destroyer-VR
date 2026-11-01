#include "aim.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "camera.h"
#include "config.h"
#include "god.h"
#include "log.h"

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kRadToDeg = 57.2957795f;

// The player's look angles: a float yaw and, 12 bytes on, the pitch (radians), somewhere within
// 0x200 bytes after an object the client soldier reaches by its fields. Neither the object's kind
// nor the way there stays the same from one load to the next (seen 2026-10-30: two pointers away,
// then three; the block bf3vr.ini's old F3 pointer led to went elsewhere at a checkpoint, the pitch
// was written over a pointer and the player walked backwards). What does: they're exactly the
// angles the game builds the weapon camera from, before the mod turns it (camera.cpp), and they
// keep a nudge and turn that camera with it, where the copies the game rebuilds each frame don't.
// So: the soldier's pointers three deep, the pairs just after each that match the camera, each
// nudged; nothing is written until one passes. Kept while it goes on matching the camera.
constexpr float kNudge = 0.03f;           // radians, undone straight after
constexpr ULONGLONG kSearchAgain = 3000;  // ms between searches while none is found
constexpr uint32_t kWindow = 0x200;       // bytes looked through after each pointer

// The look yaw is set outright each frame: where the game's aim was when the angles were found
// (less the hand's yaw then), plus the hand's yaw and the stick's turns since. Adding each frame's
// change instead let anything the game did to the angle pile up, and the picture, which takes the
// hand's yaw back out of it, turned with the gun again (user, 2026-10-30).
bool g_havePrevious = false;
float g_base = 0.0f;     // the look yaw with the hand's yaw at 0 and no stick turn
float g_turned = 0.0f;   // the stick's turns since then (radians, the hand's sense)
float g_target = 0.0f;   // the look yaw written this frame
bool g_haveTarget = false;
// g_target is mouse aim's, a yaw of the game's camera itself: no vehicle's turn (g_yawOffset) to add in LookYaw.
// The search thread sets g_yawOffset as it finds the angles, a moment before Targets hands them over: added to
// mouse aim's target then, the picture's body took a vehicle's turn for a frame and kept it from then on.
bool g_targetIsMouse = false;
// Reset() (a recenter, a mode or source switch): the next start takes the game's own yaw, not the picture's
// body (a recenter turns the hand and the head together, which the game's yaw kept then follows unseen).
bool g_reanchor = false;
// Update's last answer, the look going on: a new soldier then is a load or a respawn, whose facing is taken.
bool g_lookedLast = false;
int g_writes = 0;  // head moves written since the last Probe log
int g_gameMoves = 0;      // frames the game had moved the look yaw away from the last one written
float g_gameMoveMost = 0.0f;

// A scripted moment (a cutscene, a QTE, a vehicle) turns the look yaw itself, by up to 178 degrees a
// frame, and the head's yaw written over it each frame fought it: the view turned the wrong way (the
// user, The Great Destroyer, 2026-10-31: "When Kovac is walking into the room, it's inverted"; the car
// ride at the start of Rock and a Hard Place, the same). Moved by more than kScriptedMove, nothing is
// written until the game has left the yaw alone for kScriptedQuiet: the game's camera is then the
// body and the head turns the picture about it (xr_runtime.cpp keeps the hand's yaw as it was), the
// horizon level. weapon.txt "scripted 0" writes through it as before.
// 10 degrees a frame: the moving train's curves turned it about 2 a frame, which the 2 it was took for a
// scripted moment, stopping the aim 1.5 s at a time (the user's knife fight, 16:21); the scripted turns
// seen are 15-170 degrees.
constexpr float kScriptedMove = 10.0f / kRadToDeg;
constexpr ULONGLONG kScriptedQuiet = 1500;
std::atomic<bool> g_scriptedOn{true};
bool g_scripted = false;
float g_scriptedSeen = 0.0f;   // the look yaw as last read while scripted
ULONGLONG g_scriptedMovedAt = 0;
// How far up and down the game lets the look pitch go, as learned (radians), and the last written.
constexpr float kSteep = 0.9f;  // about 52 degrees: only writes past this teach the limits
float g_pitchMax = 1.5f, g_pitchMin = -1.5f, g_pitchWritten = 0.0f;
bool g_havePitch = false;

std::atomic<uintptr_t> g_yawAddress{0};  // the angles found, or 0 (the pitch is 0xC on)
std::atomic<uintptr_t> g_foundFor{0};    // the client soldier they were looked for from
std::atomic<bool> g_searching{false};
ULONGLONG g_searchedAt = 0;              // render thread
ULONGLONG g_mismatchSince = 0;           // render thread: when they stopped matching the camera

bool SafeWrite(uintptr_t address, float value) {
    __try {
        *reinterpret_cast<float*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A float that could be a look angle in radians: finite, the pitch within a quarter turn, the yaw
// within a thousand (it may carry whole turns), and not a pointer's bits (those read as huge, or
// as tiny numbers no angle has).
bool LooksLikeAngle(float v, bool pitch) {
    if (!std::isfinite(v))
        return false;
    const float size = std::fabs(v);
    return (size == 0.0f || size >= 1e-6f) && size <= (pitch ? 1.6f : 1000.0f);
}

float YawApart(float a, float b) {
    float d = std::fmod(a - b, 2.0f * kPi);
    if (d > kPi)
        d -= 2.0f * kPi;
    if (d < -kPi)
        d += 2.0f * kPi;
    return std::fabs(d);
}

// How a pair was reached from the soldier: the field followed at each step (its index among the
// node's 0x100), then the pair's place in the window after the last pointer.
struct Route {
    int steps = 0;
    uint16_t field[3] = {};
    uint16_t offset = 0;
};

// Every yaw-and-pitch pair within kWindow after a pointer the soldier's fields lead to, three
// deep, that matches the camera's angles (routes: how each was reached, alike in order).
std::vector<uintptr_t> Pairs(uintptr_t soldier, float cameraYaw, float cameraPitch, std::vector<Route>* routes = nullptr,
                            bool anyYaw = false) {
    std::vector<uintptr_t> pairs;
    struct Node {
        uintptr_t at;
        Route route;
    };
    std::vector<Node> level{{soldier, {}}}, next;
    std::unordered_set<uintptr_t> seen{soldier};
    uint32_t window[kWindow / 4 + 3];
    for (int depth = 0; depth < 3; ++depth) {
        for (const Node& node : level) {
            uint32_t fields[0x100];
            if (!camera::SafeRead(node.at, fields, sizeof(fields)))
                continue;
            for (uint16_t f = 0; f < 0x100; ++f) {
                const uint32_t v = fields[f];
                if (v < 0x10000 || v >= 0xFFFF0000u || (v & 3) || !seen.insert(v).second)
                    continue;
                Route route = node.route;
                route.field[route.steps++] = f;
                if (depth < 2 && seen.size() < 200000)
                    next.push_back({v, route});
                if (!camera::SafeRead(v, window, sizeof(window)))
                    continue;
                for (uint32_t i = 0; i < kWindow / 4; ++i) {
                    float yaw, pitch;
                    std::memcpy(&yaw, &window[i], 4);
                    std::memcpy(&pitch, &window[i + 3], 4);
                    // Any yaw, but not a zero (a null pointer as often as an angle: Proves).
                    if (LooksLikeAngle(yaw, false) &&
                        (anyYaw ? std::fabs(yaw) >= 0.02f && std::fabs(yaw) < 7.0f : YawApart(yaw, cameraYaw) < 0.01f) &&
                        std::fabs(pitch - cameraPitch) < 0.01f &&
                        std::find(pairs.begin(), pairs.end(), v + i * 4) == pairs.end()) {
                        pairs.push_back(v + i * 4);
                        if (routes) {
                            route.offset = static_cast<uint16_t>(i * 4);
                            routes->push_back(route);
                        }
                    }
                }
            }
        }
        level.swap(next);
        next.clear();
    }
    return pairs;
}

// The route the look angles were last found by (Search): a new soldier (each load, checkpoint or life)
// is tried by it first, one nudge instead of a whole search (the user, 2026-10-31: the free arms
// waited 5-10 s for the aim after every load, the game's own arms shown meanwhile).
Route g_lastRoute;
bool g_haveLastRoute = false;
// The camera's yaw less the look yaw found (radians): none on foot, a moving vehicle's turn on The Great
// Destroyer's train. Followed slowly as the vehicle turns (Targets).
float g_yawOffset = 0.0f;

// The pairs matching only the camera's pitch (a vehicle's look yaw is its own, not the camera's).
std::vector<uintptr_t> PitchPairs(uintptr_t soldier, float cameraPitch, std::vector<Route>* routes) {
    return Pairs(soldier, 0.0f, cameraPitch, routes, true);
}

// Kept in aimroute.txt next to the mod's DLL, so the next launch starts with it too.
std::wstring RoutePath() {
    HMODULE self = nullptr;
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&RoutePath), &self) ||
        !GetModuleFileNameW(self, path, MAX_PATH))
        return {};
    std::wstring p = path;
    return p.substr(0, p.find_last_of(L'\\') + 1) + L"aimroute.txt";
}

void SaveRoute() {
    FILE* f = nullptr;
    if (_wfopen_s(&f, RoutePath().c_str(), L"w") || !f)
        return;
    fprintf(f, "%d %u %u %u %u\n", g_lastRoute.steps, g_lastRoute.field[0], g_lastRoute.field[1], g_lastRoute.field[2],
            g_lastRoute.offset);
    fclose(f);
}

void LoadRoute() {
    static bool s_loaded = false;
    if (s_loaded)
        return;
    s_loaded = true;
    FILE* f = nullptr;
    if (_wfopen_s(&f, RoutePath().c_str(), L"r") || !f)
        return;
    unsigned a = 0, b = 0, c = 0, o = 0;
    int steps = 0;
    if (fscanf_s(f, "%d %u %u %u %u", &steps, &a, &b, &c, &o) == 5 && steps >= 1 && steps <= 3 && a < 0x100 && b < 0x100 &&
        c < 0x100 && o < kWindow) {
        g_lastRoute.steps = steps;
        g_lastRoute.field[0] = static_cast<uint16_t>(a), g_lastRoute.field[1] = static_cast<uint16_t>(b);
        g_lastRoute.field[2] = static_cast<uint16_t>(c), g_lastRoute.offset = static_cast<uint16_t>(o);
        g_haveLastRoute = true;
        Log("Aim: the last route to the look angles read from aimroute.txt (%d steps)", steps);
    }
    fclose(f);
}

uintptr_t Follow(uintptr_t soldier, const Route& route) {
    uintptr_t at = soldier;
    for (int s = 0; s < route.steps; ++s) {
        uint32_t v = 0;
        if (!camera::SafeRead(at + route.field[s] * 4, &v, sizeof(v)) || v < 0x10000 || v >= 0xFFFF0000u || (v & 3))
            return 0;
        at = v;
    }
    return at + route.offset;
}

// A load leaves the pitch exactly level until it's written, and level, nothing is nudged (below):
// the game's view is tilted a few degrees by the mouse, the game's own input, so the next search
// can go ahead. Only with the game's window in front (the mouse would move the desktop's otherwise).
void TiltView(bool turn) {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId())
        return;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dy = 24;            // about 6 degrees down at the test setup's mouse speed
    in.mi.dx = turn ? 24 : 0;  // and as much to the side, for a yaw that's a zero too
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(in));
}

// More pairs than this matching the camera means its angles are ones memory is full of (a yaw or
// pitch near zero): 163 pairs at yaw 0.0085 once, and nudging them crashed the game (2026-10-31).
// Nothing is nudged then; the view is turned a little and searched again.
constexpr size_t kMostPairs = 16;

// Whether a yaw/pitch pair is the player's look: with the camera still (not turning, not firing),
// its yaw nudged one way and its pitch the other, the pair keeps them and the camera's yaw and pitch
// both follow, each its own way; put back, the camera comes back. A yaw nudge alone, checked only
// for the camera moving about as much, took two wrong pairs in one evening: recoil and the smooth
// turn moved the camera during the test, and the mod then wrote where the game wasn't looking (the
// user's shots at the floor went elsewhere, 2026-10-30).
enum class Proof { kNo, kKept, kYes, kBusy };

// How far the camera may drift in the 120 ms before a nudge (radians, 0.35 degrees): on The Great
// Destroyer's moving train its sway always passed the 0.002 it was, and the aim was never found there
// (the user's shots missed, 16:04). The nudge it then has to follow, and come back from, still proves it.
constexpr float kStillMost = 0.006f;

// While a candidate is being nudged (Proves): mouse aim holds its corrections, or they would undo the nudge
// (MouseAim). How long it held them in the current search, and for how many candidates (the search's own
// thread; Search logs them).
std::atomic<bool> g_proving{false};
ULONGLONG g_heldMs = 0;
int g_nudged = 0;
struct Proving {
    const ULONGLONG since = GetTickCount64();
    Proving() { g_proving = true; }
    ~Proving() {
        g_proving = false;
        g_heldMs += GetTickCount64() - since;
        ++g_nudged;
    }
};

// Puts value at address only if it still holds expected, bit for bit: false if it doesn't (or can't be
// written).
bool SwapIfUnchanged(uintptr_t address, float expected, float value) {
    LONG was = 0, now = 0;
    std::memcpy(&was, &expected, sizeof(was));
    std::memcpy(&now, &value, sizeof(now));
    __try {
        return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(address), now, was) == was;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A nudge undone where it's still there: by the nudge alone, so a turn the game added meanwhile stays; a
// value the game replaced is left as the game made it.
void Undo(uintptr_t address, float nudged, float nudge) {
    float now = 0.0f;
    if (camera::SafeRead(address, &now, sizeof(now)) && std::fabs(now - nudged) < kNudge * 0.3f)
        SwapIfUnchanged(address, now, now - nudge);
}

Proof Proves(uintptr_t at, bool anyYaw = false) {
    const Proving proving;
    float yaw = 0.0f, pitch = 0.0f, still[2][2] = {};
    if (!camera::SafeRead(at, &yaw, sizeof(yaw)) || !camera::SafeRead(at + 0xC, &pitch, sizeof(pitch)) ||
        !camera::GameWeaponAngles(still[0][0], still[0][1]) || (!anyYaw && YawApart(yaw, still[0][0]) > 0.02f))
        return Proof::kNo;
    // Only real angles are nudged, never a zero: zeros near the soldier are null pointers and empty lists as
    // often as level views, and the pitch-alone pass (any yaw) let them through. Nudged, the game read the
    // 0.03 as a pointer and crashed (bf3.exe+179AE2 reading 3CF5C28F, 2026-10-31 19:54); the crashes just
    // after the interrogation (lists and nodes that looked freed) were likely the same.
    if (std::fabs(yaw) < 0.02f || std::fabs(pitch) < 0.02f || !LooksLikeAngle(yaw, false) || !LooksLikeAngle(pitch, true))
        return Proof::kNo;
    Sleep(120);
    if (!camera::GameWeaponAngles(still[1][0], still[1][1]) || YawApart(still[1][0], still[0][0]) > kStillMost ||
        std::fabs(still[1][1] - still[0][1]) > kStillMost)
        return Proof::kBusy;
    // The pitch towards level, so the nudge never goes past the game's limits. Each nudge goes only over the
    // value read, and is undone only where it's still there (Undo): put back regardless, as it was, a stale
    // value could land over whatever the game had put there meanwhile.
    const float pitchNudge = pitch > 0.0f ? -kNudge : kNudge;
    if (!SwapIfUnchanged(at, yaw, yaw + kNudge))
        return Proof::kNo;
    if (!SwapIfUnchanged(at + 0xC, pitch, pitch + pitchNudge)) {
        Undo(at, yaw + kNudge, kNudge);
        return Proof::kNo;
    }
    Sleep(150);
    float keptYaw = 0.0f, keptPitch = 0.0f, nudged[2] = {};
    const bool kept = camera::SafeRead(at, &keptYaw, sizeof(keptYaw)) &&
                      camera::SafeRead(at + 0xC, &keptPitch, sizeof(keptPitch)) &&
                      std::fabs(keptYaw - (yaw + kNudge)) < kNudge * 0.3f &&
                      std::fabs(keptPitch - (pitch + pitchNudge)) < kNudge * 0.3f;
    // Signed: the camera's yaw up by the nudge, its pitch the pitch nudge's way.
    bool followed = camera::GameWeaponAngles(nudged[0], nudged[1]);
    if (followed) {
        float yawMoved = nudged[0] - still[1][0];
        yawMoved -= 2 * kPi * std::round(yawMoved / (2 * kPi));
        followed = std::fabs(yawMoved - kNudge) < kNudge * 0.3f &&
                   std::fabs((nudged[1] - still[1][1]) - pitchNudge) < kNudge * 0.3f;
    }
    // Undone first, kept or not: one of the two kept while the other was rebuilt stayed nudged before.
    Undo(at, yaw + kNudge, kNudge);
    Undo(at + 0xC, pitch + pitchNudge, pitchNudge);
    if (!kept)
        return Proof::kNo;
    if (!followed)
        return Proof::kKept;
    Sleep(150);
    float back[2] = {};
    const bool returned = camera::GameWeaponAngles(back[0], back[1]) && YawApart(back[0], still[1][0]) < kNudge * 0.3f &&
                          std::fabs(back[1] - still[1][1]) < kNudge * 0.3f;
    return returned ? Proof::kYes : Proof::kKept;
}

// Background thread: the pairs tested in turn; the first that proves to be the player's look
// (Proves) is used.
void Search(uintptr_t soldier) {
    const ULONGLONG searchStart = GetTickCount64();
    g_heldMs = 0, g_nudged = 0;
    int byPitch = 0;  // candidates nudged by the pitch alone (the "no look angles yet" line doesn't count them)
    float cameraYaw = 0.0f, cameraPitch = 0.0f;
    for (int wait = 0; wait < 20 && !camera::GameWeaponAngles(cameraYaw, cameraPitch); ++wait)  // just after a load
        Sleep(50);
    uintptr_t found = 0;
    size_t candidates = 0;
    int kept = 0;
    // Looking level, the pitch is a zero, which matches every zero after a yaw-like float: nothing
    // is nudged then (nudging stray matches crashed the game once, from outside, 2026-10-30).
    int busy = 0;  // pairs not tried: the camera was moving (turning, firing)
    bool crowded = false;
    LoadRoute();
    // The last route first: its pair matching the camera and proving itself, nothing else to search.
    if (g_haveLastRoute && camera::GameWeaponAngles(cameraYaw, cameraPitch)) {
        const uintptr_t at = Follow(soldier, g_lastRoute);
        float pair[4] = {};
        // Its yaw needn't match the camera's: on a moving vehicle (The Great Destroyer's train) the look
        // yaw is the player's own, the camera's the vehicle's turn added (g_yawOffset).
        if (at && camera::SafeRead(at, pair, sizeof(pair)) && LooksLikeAngle(pair[0], false) &&
            std::fabs(pair[3] - cameraPitch) < 0.01f && Proves(at, true) == Proof::kYes) {
            found = at, kept = 1, candidates = 1;
            g_yawOffset = std::remainder(cameraYaw - pair[0], 2.0f * kPi);
            Log("Aim: the look angles found again by the last route, at once (%.0f degrees off the camera's yaw)",
                g_yawOffset * kRadToDeg);
        }
    }
    if (!found && camera::GameWeaponAngles(cameraYaw, cameraPitch) && std::fabs(cameraPitch) > 0.02f &&
        std::fabs(cameraYaw) > 0.02f) {
        std::vector<Route> routes;
        const std::vector<uintptr_t> pairs = Pairs(soldier, cameraYaw, cameraPitch, &routes);
        candidates = pairs.size();
        crowded = candidates > kMostPairs;
        for (size_t k = 0; k < pairs.size() && !crowded && !found; ++k) {
            switch (Proves(pairs[k])) {
            case Proof::kYes:
                found = pairs[k], ++kept;
                if (k < routes.size())
                    g_lastRoute = routes[k], g_haveLastRoute = true, SaveRoute();
                break;
            case Proof::kKept:
                ++kept;
                break;
            case Proof::kBusy:
                ++busy;
                break;
            default:
                break;
            }
        }
        if (found)
            g_yawOffset = 0.0f;
    }
    // Then by the pitch alone (the yaw any): on the moving train the look yaw is the player's own, not the
    // camera's, and no pair matching the camera's ever kept a nudge (the user's shots missed, 16:10). Only
    // with the view clearly up or down (more stray floats match a small pitch; nudging strays crashed the
    // game once) and at most kMostPairs of them.
    if (!found && !crowded && camera::GameWeaponAngles(cameraYaw, cameraPitch) && std::fabs(cameraPitch) > 0.1f) {
        std::vector<Route> routes;
        const std::vector<uintptr_t> pairs = PitchPairs(soldier, cameraPitch, &routes);
        for (size_t k = 0; k < pairs.size() && k < kMostPairs && !found; ++k) {
            float yaw = 0.0f;
            ++byPitch;
            if (Proves(pairs[k], true) == Proof::kYes && camera::SafeRead(pairs[k], &yaw, sizeof(yaw))) {
                found = pairs[k], ++kept;
                g_yawOffset = std::remainder(cameraYaw - yaw, 2.0f * kPi);
                if (k < routes.size())
                    g_lastRoute = routes[k], g_haveLastRoute = true, SaveRoute();
                Log("Aim: the look angles found by the pitch alone, %.0f degrees off the camera's yaw (a vehicle's turn)",
                    g_yawOffset * kRadToDeg);
            }
        }
    }
    const bool level = !found && camera::GameWeaponAngles(cameraYaw, cameraPitch) &&
                       (std::fabs(cameraPitch) <= 0.02f || std::fabs(cameraYaw) <= 0.02f);
    if (level || crowded)
        TiltView(true);
    static uintptr_t s_logged = 1;
    if (found && found != s_logged)
        Log("Aim: the look angles found at %08X (%zu pairs near the soldier matching the camera, %d kept a nudge)",
            static_cast<unsigned>(found), candidates, kept);
    else if (level)
        Log("Aim: the game's view is exactly level or straight ahead, which can't be told apart; turned a little to "
            "look again");
    else if (crowded)
        Log("Aim: %zu pairs near the soldier match the camera, too many to nudge safely; turned a little to look again",
            candidates);
    else if (!found)
        Log("Aim: no look angles yet (%zu pairs near the soldier matching the camera, %d kept a nudge, none turned "
            "it, %d not tried with the camera moving); the view follows the head meanwhile",
            candidates, kept, busy);
    s_logged = found;
    // How long the search took, and how long mouse aim held its corrections for the nudges: most of each search
    // in the interrogation room (2026-10-31: 3.5-4.4 s searches back to back, the view tilted, so the pitch-alone
    // pass nudged up to 16 candidates each time).
    Log("Aim: the search took %llu ms; mouse aim held its corrections %llu ms for %d candidates (%d by the pitch "
        "alone)",
        GetTickCount64() - searchStart, g_heldMs, g_nudged, byPitch);
    g_yawAddress = found;
    g_foundFor = soldier;
    g_searching = false;
}

// Where to write the angles, if they're known for the player's current soldier and still match the
// camera (a vehicle, a cutscene or a load can leave them: after a second of not matching, they're
// looked for again); otherwise a search starts and nothing is written meanwhile.
bool Targets(uintptr_t& yawAddress, uintptr_t& pitchAddress) {
    const uintptr_t soldier = god::PlayerClientSoldier();
    const uintptr_t found = g_yawAddress.load();
    const ULONGLONG now = GetTickCount64();
    float yaw = 0.0f, pitch = 0.0f, cameraYaw = 0.0f, cameraPitch = 0.0f;
    if (soldier && soldier == g_foundFor.load() && found && camera::SafeRead(found, &yaw, sizeof(yaw)) &&
        camera::SafeRead(found + 0xC, &pitch, sizeof(pitch)) && LooksLikeAngle(yaw, false) &&
        LooksLikeAngle(pitch, true)) {
        // Yaw and pitch both (a wrong pair's yaw stayed near the camera's for 16 s by chance while
        // its pitch never did, 2026-10-30).
        // The yaw with the vehicle's turn added (g_yawOffset: none on foot), which is followed slowly so
        // the train's curves keep it matching.
        const bool haveCamera = camera::GameWeaponAngles(cameraYaw, cameraPitch);
        const bool matching = !haveCamera || (YawApart(yaw + g_yawOffset, cameraYaw) < 0.2f && std::fabs(pitch - cameraPitch) < 0.12f);
        if (haveCamera && matching && g_yawOffset != 0.0f)
            g_yawOffset += 0.05f * std::remainder(cameraYaw - yaw - g_yawOffset, 2.0f * kPi);
        if (matching)
            g_mismatchSince = 0;
        else if (!g_mismatchSince)
            g_mismatchSince = now;
        if (!g_mismatchSince || now - g_mismatchSince < 1000) {
            yawAddress = found;
            pitchAddress = found + 0xC;
            return true;
        }
        Log("Aim: the look angles at %08X stopped matching the weapon camera; looking again",
            static_cast<unsigned>(found));
        g_yawAddress = 0;
        g_mismatchSince = 0;
    }
    if (soldier && !g_searching.load() && (soldier != g_foundFor.load() || now - g_searchedAt > kSearchAgain)) {
        g_searchedAt = now;
        g_searching = true;
        g_yawAddress = 0;
        std::thread(Search, soldier).detach();
    }
    return false;
}

void SetTo(uintptr_t address, float value) {
    if (SafeWrite(address, value))
        ++g_writes;
}


}

namespace aim {

// Mouse aim, while the look angles aren't known (after a load, on The Great Destroyer's moving train,
// where the search failed for minutes and a knife-rushing enemy killed the user before the gun could
// aim, 16:17): the game's view steered with the mouse towards where the hand points, closing half the
// gap each frame from the weapon camera's angles as the game made them. Nothing in memory is searched
// or written, so it works at once. The picture takes the hand's turn out by the yaw aimed for
// (g_target, LookYaw) as it does with the angles written. Off without the game's window in front and
// with weapon.txt "mouseaim 0". While the search nudges a candidate (g_proving) only its corrections
// wait (they would undo the nudge): the aim and the picture go on, and the stick's turn is sent as it is.
std::atomic<bool> g_mouseAimOn{true};
bool g_mouseHave = false;
float g_mouseBase = 0.0f;   // the game yaw with the hand's yaw at 0 and no stick turn
float g_mouseTurned = 0.0f;
constexpr float kMousePixelsPerRadian = 230.0f;  // about 4 a degree (TiltView's 24 for some 6)
float g_mouseCarry = 0.0f;  // during a nudge: the stick's turn not yet sent (a fraction of a count)
bool g_mouseHeld = false;   // this frame's corrections held for a nudge (Nudging)
bool g_mousePaused = false;  // it waited since it last steered: centred again when it steers next

bool MouseAim(float headYaw, float headPitch, float turn, float sign, const float* body, bool lookedLast) {
    g_mouseHeld = false;
    float cameraYaw = 0.0f, cameraPitch = 0.0f;
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (!g_mouseAimOn.load(std::memory_order_relaxed) || pid != GetCurrentProcessId() ||
        !camera::GameWeaponAngles(cameraYaw, cameraPitch)) {
        g_mousePaused = true;
        return false;
    }
    const bool nudging = g_proving.load();
    // Centred afresh for a new soldier (a respawn, a load), after the look angles were written, and after a pause.
    static uintptr_t s_soldier = 0;
    const uintptr_t soldier = god::PlayerClientSoldier();
    bool newSoldier = false;
    if (soldier != s_soldier) {
        s_soldier = soldier;
        g_mouseHave = false;
        newSoldier = true;
    }
    if (!g_mouseHave || g_mousePaused) {
        // From the picture's body (Update's body): the yaw aimed for takes the hand's turn out of it, so the view
        // goes on where it was and the game's camera turns to the gun. Its centring kept through a pause instead,
        // whatever turned the game's camera meanwhile (the stick, through the mouse thread) was undone and the view
        // jumped with it (The Great Destroyer, 18:31:16 and 18:32:27: 112 and 136 degrees); centred on the game's
        // camera, the hand's turn meanwhile jumped it (16:21). The game's camera only after a recenter, or for a
        // new soldier while the look went on (a load: its facing taken), and never a nudged one.
        const bool fromBody = body && !g_reanchor && !(newSoldier && lookedLast);
        if (nudging && !fromBody)
            return false;  // never centred on a nudged camera: paused, as it always was through a nudge
        g_mouseBase = fromBody ? *body - (1.0f + sign) * headYaw : cameraYaw - sign * headYaw;
        g_mouseTurned = 0.0f;
        g_yawOffset = 0.0f;  // the camera's own yaw here, no vehicle's turn to add
        g_reanchor = false;
        if (!g_mouseHave)
            Log("Aim: the view steered with the mouse until the look angles are found%s",
                fromBody ? " (from the picture's turn)" : "");
        g_mouseHave = true;
        g_mousePaused = false;
    }
    g_mouseTurned += turn;
    const float yaw = g_mouseBase + sign * (headYaw + g_mouseTurned);
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    if (nudging) {
        // A candidate being nudged (Proves): no correction, which would undo the nudge, but the aim and the
        // picture (LookYaw) go on, and the stick's own turn is sent as input.cpp's mouse thread sent it while
        // mouse aim paused. Paused outright (returning false), the picture switched to the game's camera for
        // each nudge and back after, undoing the stick's turn made meanwhile in one frame: the interrogation
        // room's "teleporting" (2026-10-31 18:07, 75-86% of the stick-turning frames during nudges; 112
        // degrees in one frame at 18:31:16, 162 and 180 on the train at 18:42).
        g_mouseCarry += sign * turn * kMousePixelsPerRadian;
        in.mi.dx = static_cast<LONG>(g_mouseCarry);
        g_mouseCarry -= static_cast<float>(in.mi.dx);
        g_mouseHeld = true;
    } else {
        g_mouseCarry = 0.0f;
        const float pitch = std::clamp(headPitch, -1.4f, 1.4f);
        const float yawError = std::remainder(yaw - cameraYaw, 2.0f * kPi), pitchError = pitch - cameraPitch;
        in.mi.dx = static_cast<LONG>(std::clamp(yawError * kMousePixelsPerRadian * 0.5f, -150.0f, 150.0f));
        in.mi.dy = static_cast<LONG>(std::clamp(-pitchError * kMousePixelsPerRadian * 0.5f, -150.0f, 150.0f));
    }
    if (in.mi.dx || in.mi.dy)
        SendInput(1, &in, sizeof(in));
    g_target = yaw - 2 * kPi * std::floor(yaw / (2 * kPi));
    g_haveTarget = true;
    g_targetIsMouse = true;
    return true;
}

void SetMouseAim(bool on) {
    if (g_mouseAimOn.exchange(on) != on)
        Log("Aim: mouse aim %s", on ? "on (until the look angles are found)" : "off");
}

bool Nudging() {
    return g_mouseHeld && g_mouseHave;
}

bool Update(float headYaw, float headPitch, float turn, const float* body) {
    const bool lookedLast = g_lookedLast;
    g_lookedLast = false;
    config::Aim a = config::GetAim();
    uintptr_t yawAddress = 0, pitchAddress = 0;
    if (!Targets(yawAddress, pitchAddress)) {
        g_havePrevious = false;  // found again: start from there, no jump
        g_haveTarget = false;
        g_havePitch = false;
        // A scripted moment can only be seen ending through the look angles: without them it stayed on and
        // kept the free hands off for 1.5 minutes after the dumpster (the user, 18:33). Over once the quiet has
        // lasted as long as it would have to with them.
        if (g_scripted && GetTickCount64() - g_scriptedMovedAt >= kScriptedQuiet) {
            g_scripted = false;
            Log("Aim: the scripted moment is over (%.1f s without seeing the game turn the look; its angles not found)",
                kScriptedQuiet / 1000.0f);
        }
        g_lookedLast = MouseAim(headYaw, headPitch, turn, a.invertYaw ? 1.0f : -1.0f, body, lookedLast);
        return g_lookedLast;
    }
    g_mouseHave = false;
    // The hand's yaw is OpenXR's (growing as it turns left); the game's look yaw grows turning
    // right: it goes in negated. Measured 2026-10-30 (weapon.txt "viewlog", the user sweeping the
    // gun with the head still): taken as it came, the game aimed the other way from the gun and
    // the picture, taking the hand's turn back out, swung twice the gun's turn ("the camera moves
    // with the gun"). The stick's turn (input::TakeTurn) is OpenXR's way too, left positive: it
    // goes in negated with it (left as it came, the smooth turn ran backwards: user, the same
    // evening). ([aim] invert_yaw undoes both.)
    const float sign = a.invertYaw ? 1.0f : -1.0f;
    float now = 0.0f;
    if (!camera::SafeRead(yawAddress, &now, sizeof(now))) {
        g_haveTarget = false;
        return false;
    }
    const ULONGLONG tick = GetTickCount64();
    if (g_scripted) {
        float moved = now - g_scriptedSeen;
        moved -= 2 * kPi * std::round(moved / (2 * kPi));
        g_scriptedSeen = now;
        if (std::fabs(moved) > 0.0005f) {
            g_scriptedMovedAt = tick;
            ++g_gameMoves;
            g_gameMoveMost = std::max(g_gameMoveMost, std::fabs(moved));
        }
        if (tick - g_scriptedMovedAt < kScriptedQuiet && g_scriptedOn.load(std::memory_order_relaxed))
            return false;
        g_scripted = false;
        g_havePrevious = false;  // from where the game left it, no jump
        Log("Aim: the game left the look yaw alone for %.1f s: the head aims again", kScriptedQuiet / 1000.0f);
    }
    if (!g_havePrevious) {
        // From the picture's body where there is one (the angles found while mouse aim steered, a scripted moment
        // over, after a cutscene): the yaw written takes the hand's turn out of it, so the view goes on and the
        // game's camera turns to the gun. From the game's own yaw, the view jumped by the hand's turn since the
        // look stopped (18-34 degrees in The Great Destroyer: 18:31:38, 18:32:12, 18:33:27, 18:33:44, 18:34:51).
        // After a recenter, from the game's (Reset).
        g_base = body && !g_reanchor ? *body - g_yawOffset - (1.0f + sign) * headYaw : now - sign * headYaw;
        g_reanchor = false;
        g_turned = 0.0f;
        g_havePrevious = true;
    } else if (g_haveTarget) {
        // Whatever the game did to it since the last write (it should do nothing).
        float moved = now - g_target;
        moved -= 2 * kPi * std::round(moved / (2 * kPi));
        if (std::fabs(moved) > 0.0005f) {
            ++g_gameMoves;
            g_gameMoveMost = std::max(g_gameMoveMost, std::fabs(moved));
        }
        if (std::fabs(moved) > kScriptedMove && g_scriptedOn.load(std::memory_order_relaxed)) {
            g_scripted = true;
            g_scriptedSeen = now;
            g_scriptedMovedAt = tick;
            g_haveTarget = false;
            g_havePitch = false;
            Log("Aim: the game turned the look yaw itself by %.1f degrees (a scripted moment): the game's camera "
                "leads, the head looks about it",
                std::fabs(moved) * kRadToDeg);
            return false;
        }
    }
    // Yaw: set outright, the stick's turn added to the hand's. The picture follows the yaw written
    // here (camera.cpp: LookYaw gives this, not what the game made of it), so the turn is drawn in
    // step with the frame, whenever the game itself reads it, and the gun can't turn the view.
    g_turned += turn;
    float yaw = g_base + sign * (headYaw + g_turned);
    yaw -= 2 * kPi * std::floor(yaw / (2 * kPi));  // the game keeps it within 0 to 2 pi
    g_target = yaw;
    g_haveTarget = true;
    g_targetIsMouse = false;
    SetTo(yawAddress, yaw);
    // Pitch: set it outright. Adding changes drifted: BF3 clamps how far the player can look
    // up or down, so whatever went past the clamp was lost and the game ended up aiming low.
    // The stored pitch is the true view pitch (positive = up), so it takes the angle as is. Past
    // that clamp, the game pulled each frame's write back and the gun shook aiming up (user,
    // 2026-10-30): where it pulls a steep write back to is learned as the limit and kept to.
    // (Only steep ones: the game's recoil moves the pitch too.)
    float pitchNow = 0.0f;
    if (g_havePitch && camera::SafeRead(pitchAddress, &pitchNow, sizeof(pitchNow)) &&
        std::fabs(pitchNow - g_pitchWritten) > 0.002f) {
        if (g_pitchWritten > kSteep && pitchNow < g_pitchWritten && pitchNow < g_pitchMax) {
            g_pitchMax = pitchNow;
            Log("Aim: the game keeps the look pitch below %.1f degrees", pitchNow * 57.29578f);
        } else if (g_pitchWritten < -kSteep && pitchNow > g_pitchWritten && pitchNow > g_pitchMin) {
            g_pitchMin = pitchNow;
            Log("Aim: the game keeps the look pitch above %.1f degrees", pitchNow * 57.29578f);
        }
    }
    const float pitch = std::clamp(a.invertPitch ? -headPitch : headPitch, g_pitchMin, g_pitchMax);
    SetTo(pitchAddress, pitch);
    g_pitchWritten = pitch;
    g_havePitch = true;
    g_lookedLast = true;
    return true;
}

void Reset() {
    g_havePrevious = false;
    g_haveTarget = false;
    g_mouseHave = false;  // mouse aim centred again too, on the game's camera (g_reanchor)
    g_reanchor = true;
}

bool Scripted() {
    return g_scripted;
}

void Forget() {
    if (g_yawAddress.exchange(0))
        Log("Aim: the look angles forgotten (a cutscene: the game draws from its own camera, unturned or scripted; "
            "they're looked for again after)");
    g_foundFor = 0;
    g_havePrevious = false;
    g_haveTarget = false;
    g_havePitch = false;
    // Mouse aim centred again where the game left the view: kept, it steered from its centring before the
    // load's opening sequence and swung the soldier back 75-80 degrees after it (the 16:27 log).
    g_mouseHave = false;
    // A cutscene ends a scripted moment: left on, it held the free hands off long after (Update's not-found path).
    g_scripted = false;
    g_lookedLast = false;  // the look stopped: a new soldier after the cutscene goes on from the picture
}

void SetScriptedFollow(bool on) {
    if (g_scriptedOn.exchange(on) != on)
        Log("Aim: scripted moments %s", on ? "lead the view (the head looks about them)" : "written over as before");
}

// weapon.txt "aimtest 20": the hand's yaw taken as turned that many degrees more, from 1 s to 3 s
// after, so the view's turn can be checked with the controllers lying still (weapon.txt "viewlog":
// the game's camera turns by it, the picture mustn't).
std::atomic<ULONGLONG> g_testAt{0};
std::atomic<float> g_testAngle{0.0f};

void StartTest(float degrees) {
    g_testAngle.store(degrees * kPi / 180.0f, std::memory_order_relaxed);
    g_testAt.store(GetTickCount64(), std::memory_order_relaxed);
    Log("Aim: test: the hand taken as turned %.0f degrees more from 1 s to 3 s from now", degrees);
}

float TestTurn() {
    const ULONGLONG since = GetTickCount64() - g_testAt.load(std::memory_order_relaxed);
    return since >= 1000 && since < 3000 ? g_testAngle.load(std::memory_order_relaxed) : 0.0f;
}

bool LookYaw(float& radians, float& sign) {
    if (!g_haveTarget)
        return false;
    // As written, whatever the game has done to it since; with a vehicle's turn (g_yawOffset) added, as
    // the camera has it (the picture takes the hand's turn out by the difference). Mouse aim's is the camera's own.
    radians = g_target + (g_targetIsMouse ? 0.0f : g_yawOffset);
    sign = config::GetAim().invertYaw ? 1.0f : -1.0f;  // the hand's yaw goes in negated (Update)
    return true;
}

void Probe() {
    static ULONGLONG lastLog = 0;
    ULONGLONG now = GetTickCount64();
    // weapon.txt "aimtest": the weapon camera (where the game fires) against the yaw written, ten
    // times a second while the test turns the hand (tools/selftest.ps1: it must aim where the gun points).
    static ULONGLONG lastTest = 0;
    if (TestTurn() != 0.0f && now - lastTest >= 100) {
        lastTest = now;
        float written[4] = {}, cameraYaw = 0.0f, cameraPitch = 0.0f;
        const uintptr_t at = g_yawAddress.load();
        if (at && camera::SafeRead(at, written, sizeof(written)) && camera::GameWeaponAngles(cameraYaw, cameraPitch))
            Log("Aim test: written %.4f, weapon camera %.4f", written[0], cameraYaw);
    }
    if (now - lastLog < 1000)
        return;
    lastLog = now;
    const uintptr_t yaw = g_yawAddress.load();
    float angles[4] = {}, cameraYaw = 0.0f, cameraPitch = 0.0f;
    if (yaw && camera::SafeRead(yaw, angles, sizeof(angles)))
        Log("Aim: yaw at %08X = %.4f, pitch %.4f", static_cast<unsigned>(yaw), angles[0], angles[3]);
    if (camera::GameWeaponAngles(cameraYaw, cameraPitch))
        Log("Aim: the weapon camera as the game wrote it: yaw %.4f, pitch %.4f", cameraYaw, cameraPitch);
    if (g_writes)
        Log("Aim: %d head moves written in the last second", g_writes);
    if (g_gameMoves)
        Log("Aim: the game moved the look yaw itself in %d frames (by up to %.2f degrees)", g_gameMoves,
            g_gameMoveMost * 57.29578f);
    g_writes = 0;
    g_gameMoves = 0;
    g_gameMoveMost = 0.0f;
}

void FlipYaw() {
    config::Aim a = config::GetAim();
    a.invertYaw = !a.invertYaw;
    config::SetAim(a);
    Log("Head yaw direction %s", a.invertYaw ? "inverted" : "normal");
}

void FlipPitch() {
    config::Aim a = config::GetAim();
    a.invertPitch = !a.invertPitch;
    config::SetAim(a);
    Log("Head pitch direction %s", a.invertPitch ? "inverted" : "normal");
}

}
