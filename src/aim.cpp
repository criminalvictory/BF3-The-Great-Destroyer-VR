#include "aim.h"

#include <windows.h>

#include <cmath>

#include "camera.h"
#include "config.h"
#include "log.h"

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kRadToDeg = 57.2957795f;

bool g_havePrevious = false;
float g_previousYaw = 0.0f;
float g_previousPitch = 0.0f;
int g_writes = 0;  // head moves written since the last Probe log

bool SafeWrite(uintptr_t address, float value) {
    __try {
        *reinterpret_cast<float*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void SetTo(const config::CameraPointer& pointer, float value) {
    uintptr_t address = 0;
    float current = 0.0f;
    if (!pointer.base || !camera::Resolve(pointer, address) || !camera::SafeRead(address, &current, sizeof(current)) ||
        !std::isfinite(current))
        return;
    if (SafeWrite(address, value))
        ++g_writes;
}

// False when the angle can't be reached at all.
bool AddTo(const config::CameraPointer& pointer, float delta) {
    uintptr_t address = 0;
    float value = 0.0f;
    if (!pointer.base || !camera::Resolve(pointer, address) || !camera::SafeRead(address, &value, sizeof(value)) ||
        !std::isfinite(value))
        return false;
    if (delta != 0.0f && SafeWrite(address, value + delta))
        ++g_writes;
    return true;
}

void LogAngle(const char* name, const config::CameraPointer& pointer) {
    uintptr_t address = 0;
    float value = 0.0f;
    if (!pointer.base)
        Log("Aim: %s not set (run F3)", name);
    else if (!camera::Resolve(pointer, address) || !camera::SafeRead(address, &value, sizeof(value)))
        Log("Aim: %s pointer did not resolve", name);
    else
        Log("Aim: %s at %08X = %.4f", name, static_cast<unsigned>(address), value);
}

}

namespace aim {

bool Update(float headYaw, float headPitch, float turn) {
    config::Aim a = config::GetAim();
    if (!g_havePrevious) {
        g_previousYaw = headYaw;
        g_previousPitch = headPitch;
        g_havePrevious = true;
        return AddTo(a.yaw, 0.0f);
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
    const bool reached = AddTo(a.yaw, a.yawDegrees ? yaw * kRadToDeg : yaw);
    // Pitch: set it outright. Adding changes drifted: BF3 clamps how far the player can look
    // up or down, so whatever went past the clamp was lost and the game ended up aiming low.
    // The stored pitch is the true view pitch (positive = up), so it takes the angle as is.
    float pitch = a.invertPitch ? -headPitch : headPitch;
    SetTo(a.pitch, a.pitchDegrees ? pitch * kRadToDeg : pitch);
    return reached;
}

void Reset() {
    g_havePrevious = false;
}

bool LookYaw(float& radians, float& sign) {
    config::Aim a = config::GetAim();
    uintptr_t address = 0;
    float value = 0.0f;
    if (!a.yaw.base || !camera::Resolve(a.yaw, address) || !camera::SafeRead(address, &value, sizeof(value)) ||
        !std::isfinite(value))
        return false;
    radians = a.yawDegrees ? value / kRadToDeg : value;
    sign = a.invertYaw ? -1.0f : 1.0f;
    return true;
}

void Probe() {
    static ULONGLONG lastLog = 0;
    ULONGLONG now = GetTickCount64();
    if (now - lastLog < 1000)
        return;
    lastLog = now;
    config::Aim a = config::GetAim();
    LogAngle("yaw", a.yaw);
    LogAngle("pitch", a.pitch);
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
