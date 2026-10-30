#include "aim.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
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

// The look angles' own object (found 2026-10-30): vtable bf3.exe+0x1C793A8, the yaw at +0x10C and
// the pitch at +0x118, in radians. The player's soldier reaches it within three pointers. Another
// object of the kind holds a copy the game rebuilds from it every frame, and the pointers in
// bf3vr.ini ([aim] yaw and pitch, from F3) can lead to either, or elsewhere after a load: at a new
// checkpoint they led into a block of pointers, and the pitch was being written over one of them.
// So nothing is written but to an object of the kind, and of those only to the one that keeps a
// small nudge (a copy loses it within a frame).
constexpr uintptr_t kAnglesType = 0x1C793A8;
constexpr uintptr_t kYawField = 0x10C;
constexpr uintptr_t kPitchField = 0x118;
constexpr float kNudge = 0.03f;            // radians, undone straight after
constexpr ULONGLONG kSearchAgain = 5000;   // ms between searches while none is found

bool g_havePrevious = false;
float g_previousYaw = 0.0f;
float g_previousPitch = 0.0f;
int g_writes = 0;  // head moves written since the last Probe log

std::atomic<uintptr_t> g_yawAddress{0};  // the real yaw, or 0 (the pitch is 0xC on)
std::atomic<uintptr_t> g_foundFor{0};    // the client soldier it was looked for from
std::atomic<bool> g_searching{false};
ULONGLONG g_searchedAt = 0;              // render thread

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

bool OfTheKind(uintptr_t object) {
    uint32_t type = 0;
    return camera::SafeRead(object, &type, sizeof(type)) &&
           type == reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + kAnglesType;
}

// The yaw the game camera ([aim] game_camera) is built from, as these angles store it (atan2 of its
// forward row's x and -z); false if it can't be read. In stereo the mod turns that camera by the
// hand's offset from the head, so it only roughly matches.
bool CameraYaw(float& yaw) {
    uintptr_t address = 0;
    camera::Transform t;
    if (!camera::Resolve(config::GetAim().gameCamera, address) || !camera::SafeRead(address, &t, sizeof(t)))
        return false;
    yaw = std::atan2(t.forward[0], -t.forward[2]);
    return std::isfinite(yaw);
}

float YawApart(float a, float b) {
    float d = std::fmod(a - b, 2.0f * kPi);
    if (d > kPi)
        d -= 2.0f * kPi;
    if (d < -kPi)
        d += 2.0f * kPi;
    return std::fabs(d);
}

// Background thread: the objects of the kind the soldier reaches (and the one bf3vr.ini's pointer
// leads to), each nudged in turn until one keeps it. One facing well away from the game camera is
// someone else's.
void Search(uintptr_t soldier, uintptr_t chainYaw) {
    std::vector<uintptr_t> objects;
    auto consider = [&](uintptr_t v) {
        for (uintptr_t start : {v, v - 0x40})  // the object, or its part the old pointers led to
            if (OfTheKind(start) && std::find(objects.begin(), objects.end(), start) == objects.end())
                objects.push_back(start);
    };
    if (chainYaw)
        consider(chainYaw - kYawField);
    std::vector<uintptr_t> level{soldier}, next;
    std::unordered_set<uintptr_t> seen{soldier};
    for (int depth = 0; depth < 3 && objects.size() < 16; ++depth) {
        for (uintptr_t node : level) {
            uint32_t fields[0x100];
            if (!camera::SafeRead(node, fields, sizeof(fields)))
                continue;
            for (uint32_t v : fields) {
                if (v < 0x10000 || v >= 0xFFFF0000u || (v & 3) || !seen.insert(v).second)
                    continue;
                consider(v);
                if (depth < 2 && seen.size() < 200000)
                    next.push_back(v);
            }
        }
        level.swap(next);
        next.clear();
    }
    uintptr_t found = 0;
    float cameraYaw = 0.0f;
    const bool haveCamera = CameraYaw(cameraYaw);
    for (uintptr_t o : objects) {
        const uintptr_t at = o + kYawField;
        float yaw = 0.0f, pitch = 0.0f, after = 0.0f;
        if (!camera::SafeRead(at, &yaw, sizeof(yaw)) || !camera::SafeRead(o + kPitchField, &pitch, sizeof(pitch)) ||
            !LooksLikeAngle(yaw, false) || !LooksLikeAngle(pitch, true) ||
            (haveCamera && YawApart(yaw, cameraYaw) > 1.0f) || !SafeWrite(at, yaw + kNudge))
            continue;
        Sleep(150);
        if (camera::SafeRead(at, &after, sizeof(after)) && std::fabs(after - (yaw + kNudge)) < kNudge * 0.3f) {
            SafeWrite(at, after - kNudge);
            found = at;
            break;
        }
    }
    static uintptr_t s_logged = 1;
    if (found && found != s_logged)
        Log("Aim: the look angles found at %08X (%zu objects of the kind from the soldier; it kept the nudge)",
            static_cast<unsigned>(found), objects.size());
    else if (!found && s_logged)
        Log("Aim: no look angles found (%zu objects of the kind from the soldier, none kept a nudge); nothing is "
            "written until they are",
            objects.size());
    s_logged = found;
    g_yawAddress = found;
    g_foundFor = soldier;
    g_searching = false;
}

// Where to write the angles, if they're known for the player's current soldier; otherwise a
// search starts (a new life, a checkpoint, a load) and nothing is written meanwhile.
bool Targets(const config::Aim& a, uintptr_t& yawAddress, uintptr_t& pitchAddress) {
    const uintptr_t soldier = god::PlayerClientSoldier();
    const uintptr_t found = g_yawAddress.load();
    float yaw = 0.0f, pitch = 0.0f;
    if (soldier && soldier == g_foundFor.load() && found && OfTheKind(found - kYawField) &&
        camera::SafeRead(found, &yaw, sizeof(yaw)) && camera::SafeRead(found + 0xC, &pitch, sizeof(pitch)) &&
        LooksLikeAngle(yaw, false) && LooksLikeAngle(pitch, true)) {
        yawAddress = found;
        pitchAddress = found + (kPitchField - kYawField);
        return true;
    }
    const ULONGLONG now = GetTickCount64();
    if (soldier && !g_searching.load() && (soldier != g_foundFor.load() || now - g_searchedAt > kSearchAgain)) {
        g_searchedAt = now;
        g_searching = true;
        g_yawAddress = 0;
        uintptr_t chain = 0;
        if (a.yaw.base && !camera::Resolve(a.yaw, chain))
            chain = 0;
        std::thread(Search, soldier, chain).detach();
    }
    return false;
}

void SetTo(uintptr_t address, float value) {
    if (SafeWrite(address, value))
        ++g_writes;
}

void AddTo(uintptr_t address, float delta) {
    float value = 0.0f;
    if (delta != 0.0f && camera::SafeRead(address, &value, sizeof(value)) && SafeWrite(address, value + delta))
        ++g_writes;
}

}

namespace aim {

bool Update(float headYaw, float headPitch, float turn) {
    config::Aim a = config::GetAim();
    uintptr_t yawAddress = 0, pitchAddress = 0;
    if (!Targets(a, yawAddress, pitchAddress)) {
        g_havePrevious = false;  // found again: start from there, no jump
        return false;
    }
    if (!g_havePrevious) {
        g_previousYaw = headYaw;
        g_previousPitch = headPitch;
        g_havePrevious = true;
        return true;
    }
    float yaw = headYaw - g_previousYaw;
    if (yaw > kPi)
        yaw -= 2 * kPi;
    if (yaw < -kPi)
        yaw += 2 * kPi;
    g_previousYaw = headYaw;
    g_previousPitch = headPitch;

    // Yaw: add the change, since the mouse also turns the body, and the stick's turn with it.
    // The picture follows the yaw written here (camera.cpp), so the turn is drawn in step with
    // the frame, whenever the game itself reads it.
    yaw += turn;
    if (a.invertYaw)
        yaw = -yaw;
    AddTo(yawAddress, yaw);
    // Pitch: set it outright. Adding changes drifted: BF3 clamps how far the player can look
    // up or down, so whatever went past the clamp was lost and the game ended up aiming low.
    // The stored pitch is the true view pitch (positive = up), so it takes the angle as is.
    SetTo(pitchAddress, a.invertPitch ? -headPitch : headPitch);
    return true;
}

void Reset() {
    g_havePrevious = false;
}

bool LookYaw(float& radians, float& sign) {
    config::Aim a = config::GetAim();
    uintptr_t yawAddress = 0, pitchAddress = 0;
    if (!Targets(a, yawAddress, pitchAddress) || !camera::SafeRead(yawAddress, &radians, sizeof(radians)))
        return false;
    sign = a.invertYaw ? -1.0f : 1.0f;
    return true;
}

void Probe() {
    static ULONGLONG lastLog = 0;
    ULONGLONG now = GetTickCount64();
    if (now - lastLog < 1000)
        return;
    lastLog = now;
    const uintptr_t yaw = g_yawAddress.load();
    float angles[4] = {};
    if (yaw && camera::SafeRead(yaw, angles, sizeof(angles)))
        Log("Aim: yaw at %08X = %.4f, pitch %.4f", static_cast<unsigned>(yaw), angles[0], angles[3]);
    if (g_writes)
        Log("Aim: %d head moves written in the last second", g_writes);
    g_writes = 0;
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
