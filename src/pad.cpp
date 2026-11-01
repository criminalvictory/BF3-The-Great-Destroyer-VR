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
// When the game last asked for controller 0 with the mod's stick on: it does every frame while it reads it. Not asked
// for this long, the stick goes nowhere and walking goes on W/A/S/D (input.cpp) until it asks again.
std::atomic<ULONGLONG> g_readAt{0};
constexpr ULONGLONG kReadWithin = 500;
std::atomic<int> g_asked{0};  // the game's XInputGetState calls, any controller, the stick on or not (the log's)
// Not asked for this long, the game is told the devices changed (WM_DEVICECHANGE, as Windows tells it when a
// controller is plugged in), at most kKicks times a stall: its controllers looked for again.
constexpr ULONGLONG kKickAfter = 2000;
constexpr int kKicks = 3;

bool Active() {
    return GetTickCount64() - g_setAt.load(std::memory_order_relaxed) < 250;
}

BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM out) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) {
        *reinterpret_cast<HWND*>(out) = hwnd;
        return FALSE;
    }
    return TRUE;
}

DWORD WINAPI GetState(DWORD index, XINPUT_STATE* state) {
    const DWORD result = g_getState(index, state);
    g_asked.fetch_add(1, std::memory_order_relaxed);
    if (index != 0 || !state || !Active())
        return result;
    g_readAt.store(GetTickCount64(), std::memory_order_relaxed);
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
    const ULONGLONG now = GetTickCount64();
    static ULONGLONG s_since = 0;  // since when the mod's stick has been on without a break
    if (now - g_setAt.load() >= 250)
        s_since = now;
    g_setAt = now;

    // Walking through the stick only while the game reads it (each frame when it does).
    const ULONGLONG readAt = g_readAt.load(std::memory_order_relaxed);
    const bool read = readAt && now - readAt < kReadWithin;
    static bool s_read = true;
    static int s_kicks = 0;
    static ULONGLONG s_kickedAt = 0;
    if (read != s_read && (read || now - s_since >= kReadWithin)) {
        s_read = read;
        if (read) {
            Log("Pad: the game reads controller 0 again%s; walking goes through its left stick",
                s_kicks ? " (after being told the devices changed)" : "");
            s_kicks = 0;
        } else {
            Log("Pad: the game isn't reading controller 0; walking on W/A/S/D until it does (it asked for any "
                "controller %d times since the hook)",
                g_asked.load());
        }
    }
    // Not read for a while (the stick on all along): the game told the devices changed, so it looks for its
    // controllers again; a few times a stall, not to keep it re-scanning.
    const ULONGLONG quiet = now - std::max(readAt, s_since);
    if (!read && quiet >= kKickAfter && s_kicks < kKicks && now - s_kickedAt >= kKickAfter) {
        HWND game = nullptr;
        EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&game));
        if (game) {
            ++s_kicks;
            s_kickedAt = now;
            PostMessageW(game, WM_DEVICECHANGE, 0x0007 /* DBT_DEVNODES_CHANGED */, 0);
            Log("Pad: controller 0 not read for %.1f s; the game told the devices changed (%d of %d)", quiet / 1000.0,
                s_kicks, kKicks);
        }
    }
    return read;
}

}
