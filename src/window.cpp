#include "window.h"

#include <windows.h>

#include "config.h"
#include "log.h"

namespace {

HWND g_window = nullptr;
WNDPROC g_originalProc = nullptr;

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

}

namespace window {

void Maintain() {
    int size = config::Get().windowSize;
    if (size <= 0)
        return;
    static ULONGLONG lastCheck = 0;
    ULONGLONG now = GetTickCount64();
    if (now - lastCheck < 2000)
        return;
    lastCheck = now;

    if (!g_window || !IsWindow(g_window)) {
        g_window = nullptr;
        g_originalProc = nullptr;
        EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&g_window));
        if (!g_window)
            return;
        g_originalProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProc)));
    }

    // Only windowed mode: a fullscreen game has no caption and sizes itself to the display mode.
    LONG style = GetWindowLongW(g_window, GWL_STYLE);
    if (!(style & WS_CAPTION))
        return;
    RECT client;
    GetClientRect(g_window, &client);
    if (client.right - client.left == size && client.bottom - client.top == size)
        return;

    RECT frame{0, 0, size, size};
    AdjustWindowRectEx(&frame, style, FALSE, GetWindowLongW(g_window, GWL_EXSTYLE));
    // Asynchronous: this runs on BF3's render thread, and the window belongs to its main thread,
    // which may be waiting on the render thread. A synchronous resize could deadlock them.
    SetWindowPos(g_window, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    Log("Window: resizing to %dx%d", size, size);
}

}
