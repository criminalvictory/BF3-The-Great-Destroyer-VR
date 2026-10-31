#include "window.h"

#include <windows.h>
#include <MinHook.h>

#include <algorithm>
#include <atomic>

#include "config.h"
#include "log.h"

namespace {

std::atomic<HWND> g_window{nullptr};
WNDPROC g_originalProc = nullptr;
bool g_wide = false;

// The pointer is kept this far in from the edges of the screen and the window.
constexpr int kMargin = 16;

using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
SetCursorPosFn g_setCursorPos = nullptr;
ClipCursorFn g_clipCursor = nullptr;
bool g_pointerHooked = false;

// Windows normally refuses to make a window bigger than the screen; let BF3's grow.
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT result = CallWindowProcW(g_originalProc, hwnd, msg, wp, lp);
    if (msg == WM_GETMINMAXINFO) {
        auto info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMaxTrackSize = {8192, 8192};
        info->ptMaxSize = {8192, 8192};
    }
    return result;
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

bool GameInFront() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// Where the mouse pointer may sit: on BF3's window, on the screen and off the taskbar. The window
// can be bigger than the monitor (4320x2160 in stereo at size 2160), and the game centres the
// pointer on it, so it ended up in the screen's bottom-right corner: the taskbar's "Show desktop"
// button. Every fire click the mod sent then minimised everything, the game too (2026-10-30).
bool SafeRect(RECT& out) {
    HWND w = g_window.load(std::memory_order_relaxed);
    RECT client;
    POINT origin{0, 0};
    MONITORINFO monitor{sizeof(monitor)};
    if (!w || IsIconic(w) || !GetClientRect(w, &client) || !ClientToScreen(w, &origin) ||
        !GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &monitor))
        return false;
    OffsetRect(&client, origin.x, origin.y);
    if (!IntersectRect(&out, &client, &monitor.rcWork))
        return false;
    InflateRect(&out, -kMargin, -kMargin);
    return out.right > out.left && out.bottom > out.top;
}

POINT Clamp(POINT p, const RECT& r) {
    return {std::clamp(p.x, r.left, r.right - 1), std::clamp(p.y, r.top, r.bottom - 1)};
}

// The game's own pointer moves and clips, kept on the safe part of its window.
BOOL WINAPI KeptSetCursorPos(int x, int y) {
    RECT safe;
    if (SafeRect(safe)) {
        POINT p = Clamp({x, y}, safe);
        x = p.x, y = p.y;
    }
    return g_setCursorPos(x, y);
}

BOOL WINAPI KeptClipCursor(const RECT* rect) {
    RECT safe, kept;
    if (rect && SafeRect(safe) && IntersectRect(&kept, rect, &safe))
        return g_clipCursor(&kept);
    return g_clipCursor(rect);
}

void HookPointer() {
    g_pointerHooked = true;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    void* setCursorPos = user32 ? reinterpret_cast<void*>(GetProcAddress(user32, "SetCursorPos")) : nullptr;
    void* clipCursor = user32 ? reinterpret_cast<void*>(GetProcAddress(user32, "ClipCursor")) : nullptr;
    if (!setCursorPos || !clipCursor ||
        MH_CreateHook(setCursorPos, reinterpret_cast<void*>(&KeptSetCursorPos),
                      reinterpret_cast<void**>(&g_setCursorPos)) != MH_OK ||
        MH_CreateHook(clipCursor, reinterpret_cast<void*>(&KeptClipCursor), reinterpret_cast<void**>(&g_clipCursor)) !=
            MH_OK ||
        MH_EnableHook(setCursorPos) != MH_OK || MH_EnableHook(clipCursor) != MH_OK) {
        Log("Window: the game's pointer moves couldn't be hooked; the mod still moves it back each frame");
        return;
    }
    Log("Window: the game's pointer moves and clips are kept on its window, off the taskbar");
}

// Every frame with BF3 in front: a pointer left off the safe part (put there before the hooks, or
// carried by the mod's mouse turning) is moved back.
void KeepPointer() {
    RECT safe;
    POINT p;
    if (!GameInFront() || !SafeRect(safe) || !GetCursorPos(&p) || PtInRect(&safe, p))
        return;
    POINT kept = Clamp(p, safe);
    SetCursorPos(kept.x, kept.y);
    static int logged = 0;
    if (logged < 5) {
        ++logged;
        Log("Window: the mouse pointer was at (%ld, %ld), off the game's window on screen; moved to (%ld, %ld)", p.x,
            p.y, kept.x, kept.y);
    }
}

}

namespace window {

void Maintain() {
    KeepPointer();
    static ULONGLONG lastCheck = 0;
    static bool lastWide = false;
    ULONGLONG now = GetTickCount64();
    if (now - lastCheck < 2000 && g_wide == lastWide)
        return;
    lastCheck = now;
    lastWide = g_wide;

    HWND w = g_window.load(std::memory_order_relaxed);
    if (!w || !IsWindow(w)) {
        w = nullptr;
        g_originalProc = nullptr;
        EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&w));
        if (!w)
            return;
        g_originalProc =
            reinterpret_cast<WNDPROC>(SetWindowLongPtrW(w, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProc)));
        g_window.store(w, std::memory_order_relaxed);
    }
    if (!g_pointerHooked)
        HookPointer();

    int size = config::Get().windowSize;
    if (size <= 0)
        return;
    int width = g_wide ? 2 * size : size;

    // Only windowed mode: a fullscreen game has no caption and sizes itself to the display mode.
    LONG style = GetWindowLongW(w, GWL_STYLE);
    if (!(style & WS_CAPTION))
        return;
    RECT client;
    GetClientRect(w, &client);
    if (client.right - client.left == width && client.bottom - client.top == size)
        return;

    RECT frame{0, 0, width, size};
    AdjustWindowRectEx(&frame, style, FALSE, GetWindowLongW(w, GWL_EXSTYLE));
    // Asynchronous: this runs on BF3's render thread, and the window belongs to its main thread,
    // which may be waiting on the render thread. A synchronous resize could deadlock them.
    SetWindowPos(w, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    Log("Window: resizing to %dx%d", width, size);
}

void SetWide(bool wide) {
    g_wide = wide;
}

bool PointerOnGame() {
    HWND w = g_window.load(std::memory_order_relaxed);
    RECT safe;
    POINT p;
    if (!w || !SafeRect(safe) || !GetCursorPos(&p))
        return false;
    // Where it is (kept on the safe part), else the middle: another window may be on top.
    const POINT tries[2] = {Clamp(p, safe), {(safe.left + safe.right) / 2, (safe.top + safe.bottom) / 2}};
    for (POINT at : tries) {
        if (at.x != p.x || at.y != p.y) {
            SetCursorPos(at.x, at.y);
            GetCursorPos(&p);
        }
        HWND under = WindowFromPoint(p);
        if (under && GetAncestor(under, GA_ROOT) == w)
            return true;
    }
    return false;
}

}
