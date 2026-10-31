#include "aim.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
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
constexpr float kScriptedMove = 2.0f / kRadToDeg;
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

// Every yaw-and-pitch pair within kWindow after a pointer the soldier's fields lead to, three
// deep, that matches the camera's angles.
std::vector<uintptr_t> Pairs(uintptr_t soldier, float cameraYaw, float cameraPitch) {
    std::vector<uintptr_t> pairs;
    std::vector<uintptr_t> level{soldier}, next;
    std::unordered_set<uintptr_t> seen{soldier};
    uint32_t window[kWindow / 4 + 3];
    for (int depth = 0; depth < 3; ++depth) {
        for (uintptr_t node : level) {
            uint32_t fields[0x100];
            if (!camera::SafeRead(node, fields, sizeof(fields)))
                continue;
            for (uint32_t v : fields) {
                if (v < 0x10000 || v >= 0xFFFF0000u || (v & 3) || !seen.insert(v).second)
                    continue;
                if (depth < 2 && seen.size() < 200000)
                    next.push_back(v);
                if (!camera::SafeRead(v, window, sizeof(window)))
                    continue;
                for (uint32_t i = 0; i < kWindow / 4; ++i) {
                    float yaw, pitch;
                    std::memcpy(&yaw, &window[i], 4);
                    std::memcpy(&pitch, &window[i + 3], 4);
                    if (LooksLikeAngle(yaw, false) && YawApart(yaw, cameraYaw) < 0.01f &&
                        std::fabs(pitch - cameraPitch) < 0.01f &&
                        std::find(pairs.begin(), pairs.end(), v + i * 4) == pairs.end())
                        pairs.push_back(v + i * 4);
                }
            }
        }
        level.swap(next);
        next.clear();
    }
    return pairs;
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

Proof Proves(uintptr_t at) {
    float yaw = 0.0f, pitch = 0.0f, still[2][2] = {};
    if (!camera::SafeRead(at, &yaw, sizeof(yaw)) || !camera::SafeRead(at + 0xC, &pitch, sizeof(pitch)) ||
        !camera::GameWeaponAngles(still[0][0], still[0][1]) || YawApart(yaw, still[0][0]) > 0.02f)
        return Proof::kNo;
    Sleep(120);
    if (!camera::GameWeaponAngles(still[1][0], still[1][1]) || YawApart(still[1][0], still[0][0]) > 0.002f ||
        std::fabs(still[1][1] - still[0][1]) > 0.002f)
        return Proof::kBusy;
    // The pitch towards level, so the nudge never goes past the game's limits.
    const float pitchNudge = pitch > 0.0f ? -kNudge : kNudge;
    if (!SafeWrite(at, yaw + kNudge) || !SafeWrite(at + 0xC, pitch + pitchNudge))
        return Proof::kNo;
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
    if (!kept)
        return Proof::kNo;
    SafeWrite(at, yaw);
    SafeWrite(at + 0xC, pitch);
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
    if (camera::GameWeaponAngles(cameraYaw, cameraPitch) && std::fabs(cameraPitch) > 0.02f &&
        std::fabs(cameraYaw) > 0.02f) {
        const std::vector<uintptr_t> pairs = Pairs(soldier, cameraYaw, cameraPitch);
        candidates = pairs.size();
        crowded = candidates > kMostPairs;
        for (size_t k = 0; k < pairs.size() && !crowded && !found; ++k) {
            switch (Proves(pairs[k])) {
            case Proof::kYes:
                found = pairs[k], ++kept;
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
        const bool matching = !camera::GameWeaponAngles(cameraYaw, cameraPitch) ||
                              (YawApart(yaw, cameraYaw) < 0.2f && std::fabs(pitch - cameraPitch) < 0.12f);
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

bool Update(float headYaw, float headPitch, float turn) {
    config::Aim a = config::GetAim();
    uintptr_t yawAddress = 0, pitchAddress = 0;
    if (!Targets(yawAddress, pitchAddress)) {
        g_havePrevious = false;  // found again: start from there, no jump
        g_haveTarget = false;
        g_havePitch = false;
        return false;
    }
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
        g_base = now - sign * headYaw;
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
    return true;
}

void Reset() {
    g_havePrevious = false;
    g_haveTarget = false;
}

void Forget() {
    if (g_yawAddress.exchange(0))
        Log("Aim: the look angles forgotten (the game drew without the head's turn: they're looked for again after)");
    g_foundFor = 0;
    g_havePrevious = false;
    g_haveTarget = false;
    g_havePitch = false;
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
    radians = g_target;  // as written, whatever the game has done to it since
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
