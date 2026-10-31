#include "pad.h"

#include <windows.h>
#include <MinHook.h>
#include <xinput.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "log.h"

namespace {

using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using GetCapabilitiesFn = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
GetStateFn g_getState = nullptr;
GetCapabilitiesFn g_getCapabilities = nullptr;
bool g_tried = false, g_hooked = false;

std::atomic<SHORT> g_x{0}, g_y{0};
std::atomic<ULONGLONG> g_setAt{0};  // when the mod last set the stick (it lets go if that stops)
std::atomic<DWORD> g_packet{1};
std::atomic<bool> g_polled{false};

bool Active() {
    return GetTickCount64() - g_setAt.load(std::memory_order_relaxed) < 250;
}

DWORD WINAPI GetState(DWORD index, XINPUT_STATE* state) {
    const DWORD result = g_getState(index, state);
    if (index != 0 || !state || !Active())
        return result;
    if (!g_polled.exchange(true))
        Log("Pad: the game reads controller 0; walking goes through its left stick");
    if (result != ERROR_SUCCESS)
        std::memset(state, 0, sizeof(*state));  // no controller there: only the mod's stick
    state->dwPacketNumber = g_packet.load(std::memory_order_relaxed);
    state->Gamepad.sThumbLX = g_x.load(std::memory_order_relaxed);
    state->Gamepad.sThumbLY = g_y.load(std::memory_order_relaxed);
    return ERROR_SUCCESS;
}

DWORD WINAPI GetCapabilities(DWORD index, DWORD flags, XINPUT_CAPABILITIES* caps) {
    const DWORD result = g_getCapabilities(index, flags, caps);
    if (index != 0 || !caps || result == ERROR_SUCCESS || !Active())
        return result;
    std::memset(caps, 0, sizeof(*caps));
    caps->Type = XINPUT_DEVTYPE_GAMEPAD;
    caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
    caps->Gamepad.sThumbLX = caps->Gamepad.sThumbLY = static_cast<SHORT>(0xFFC0);  // full range
    return ERROR_SUCCESS;
}

bool Hook() {
    g_tried = true;
    HMODULE xinput = GetModuleHandleW(L"XINPUT9_1_0.dll");
    if (!xinput)
        xinput = GetModuleHandleW(L"XInput1_3.dll");
    void* getState = xinput ? reinterpret_cast<void*>(GetProcAddress(xinput, "XInputGetState")) : nullptr;
    void* getCapabilities = xinput ? reinterpret_cast<void*>(GetProcAddress(xinput, "XInputGetCapabilities")) : nullptr;
    if (!getState || !getCapabilities ||
        MH_CreateHook(getState, reinterpret_cast<void*>(&GetState), reinterpret_cast<void**>(&g_getState)) != MH_OK ||
        MH_CreateHook(getCapabilities, reinterpret_cast<void*>(&GetCapabilities),
                      reinterpret_cast<void**>(&g_getCapabilities)) != MH_OK ||
        MH_EnableHook(getState) != MH_OK || MH_EnableHook(getCapabilities) != MH_OK) {
        Log("Pad: the game's XInput couldn't be hooked; walking stays on W/A/S/D");
        return false;
    }
    Log("Pad: XInput hooked");
    return true;
}

}

namespace pad {

bool SetMove(float x, float y) {
    if (!g_tried)
        g_hooked = Hook();
    if (!g_hooked)
        return false;
    // Past the game's own dead zone (XInput's usual 24% of the range) as soon as the stick moves
    // at all, then on to full: a small push walks slowly rather than not at all.
    constexpr float kGameDeadZone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32767.0f;
    const float length = std::sqrt(x * x + y * y);
    SHORT sx = 0, sy = 0;
    if (length > 0.001f) {
        const float scale = (kGameDeadZone + (1.0f - kGameDeadZone) * std::min(length, 1.0f)) / length * 32767.0f;
        sx = static_cast<SHORT>(std::clamp(x * scale, -32767.0f, 32767.0f));
        sy = static_cast<SHORT>(std::clamp(y * scale, -32767.0f, 32767.0f));
    }
    if (sx != g_x.load() || sy != g_y.load()) {
        g_x = sx;
        g_y = sy;
        ++g_packet;
    }
    g_setAt = GetTickCount64();
    return true;
}

}
