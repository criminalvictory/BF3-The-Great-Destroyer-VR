#include "window.h"

#include <windows.h>
#include <windowsx.h>
#include <intrin.h>
#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "config.h"
#include "log.h"

#pragma intrinsic(_ReturnAddress)

namespace {

std::atomic<HWND> g_window{nullptr};
WNDPROC g_originalProc = nullptr;
std::atomic<bool> g_wide{false};

// The pointer is kept this far in from the edges of the screen and the window.
constexpr int kMargin = 16;

using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
SetCursorPosFn g_setCursorPos = nullptr;
ClipCursorFn g_clipCursor = nullptr;
bool g_pointerHooked = false;

// Fitted ([window] fit, weapon.txt "windowfit 0/1"): the window fills the monitor, borderless, in the shape of the
// picture it shows (g_shape), and the game is told its window is still the size it draws at (g_told): BF3 draws its
// picture at its window's size, and a window the monitor's size would have it draw the headset's eyes at half theirs.
// Windows scales the bigger picture into the window (DXGI stretches a back buffer to its window). The user,
// 2026-11-01: the desktop "way too zoomed in ... way too cropped", and Discord's stream of the game was half an old
// picture: of a window off the edges of the screen, Windows keeps only the part on it up to date (the desktop showed
// the top left of a 4320x2160 window on a 1920x1080 monitor; Discord's capture, and PrintWindow, that part moving and
// the rest still).
std::once_flag g_fitLoaded;
std::atomic<bool> g_fit{true};
std::atomic<float> g_shape{0.0f};
// Sizes packed width << 16 | height, so they're read whole: what the game is told its window is (0: the truth),
// and what its last WM_SIZE said.
std::atomic<uint32_t> g_told{0};
std::atomic<uint32_t> g_sent{0};
// The window's own styles while the mod has its frame off (on the window's own thread, Apply).
LONG g_ownStyle = 0;
LONG g_ownExStyle = 0;
std::atomic<bool> g_restyled{false};
// Posted to the window, done on its own thread (WindowProc): restyling and resizing it there, the messages they
// send it go straight to it. From the render thread, where Maintain runs, they would wait on the game's main thread,
// which may itself be waiting on the render thread.
std::atomic<UINT> g_applyMessage{0};
std::atomic<ULONGLONG> g_fittedAt{0};
// The game in its own fullscreen (its swap chain's state, from Present): a borderless window then is its own, not
// the mod's (the review: once fitted, Fit took it for its own and resized it, and "windowfit 0" put a frame on it).
std::atomic<bool> g_gameFullscreen{false};

// What the game asks of its window while fitted (GetClientRect, ScreenToClient, ClientToScreen): answered for the
// size it's told, to its own code only. Windows, DXGI (which scales its picture into the window), the overlays and
// the mod see the window as it is.
using GetClientRectFn = BOOL(WINAPI*)(HWND, LPRECT);
using PointFn = BOOL(WINAPI*)(HWND, LPPOINT);
GetClientRectFn g_getClientRect = nullptr;
PointFn g_screenToClient = nullptr;
PointFn g_clientToScreen = nullptr;
bool g_sizeHooked = false;
std::atomic<bool> g_canTell{false};  // the three hooked: without them the window is never fitted
std::atomic<int> g_askedByGame{0};
std::atomic<int> g_askedByOthers{0};
std::atomic<int> g_pointsMoved{0};
std::atomic<uintptr_t> g_otherCallers[4] = {};  // where the first calls from elsewhere came from (the log names them)

uint32_t Pack(int width, int height) {
    return (static_cast<uint32_t>(width & 0xFFFF) << 16) | static_cast<uint32_t>(height & 0xFFFF);
}

void LoadFit() {
    std::call_once(g_fitLoaded, [] { g_fit = config::Get().windowFit; });
}

// The size the game is told its window w is: true while it's fitted.
bool Told(HWND w, int& width, int& height) {
    const uint32_t told = g_told.load(std::memory_order_relaxed);
    width = static_cast<int>(told >> 16);
    height = static_cast<int>(told & 0xFFFF);
    return width > 0 && height > 0 && w && w == g_window.load(std::memory_order_relaxed);
}

BOOL RealClientRect(HWND w, RECT* r) {
    return g_getClientRect ? g_getClientRect(w, r) : GetClientRect(w, r);
}

// A call from the game's own code (bf3.exe's image), not from Windows, DXGI, an overlay or the mod.
bool FromGame(const void* returnAddress) {
    static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    static const uintptr_t end = [] {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        return base + nt->OptionalHeader.SizeOfImage;
    }();
    const uintptr_t at = reinterpret_cast<uintptr_t>(returnAddress);
    return at >= base && at < end;
}

void NoteOtherCaller(const void* returnAddress) {
    const uintptr_t at = reinterpret_cast<uintptr_t>(returnAddress);
    for (auto& slot : g_otherCallers) {
        uintptr_t seen = slot.load(std::memory_order_relaxed);
        if (seen == at)
            return;
        if (!seen && slot.compare_exchange_strong(seen, at))
            return;
    }
}

BOOL WINAPI ToldGetClientRect(HWND w, LPRECT r) {
    const void* caller = _ReturnAddress();
    const BOOL ok = g_getClientRect(w, r);
    int width, height;
    if (ok && r && Told(w, width, height)) {
        if (FromGame(caller)) {
            *r = RECT{0, 0, width, height};
            g_askedByGame.fetch_add(1, std::memory_order_relaxed);
        } else {
            g_askedByOthers.fetch_add(1, std::memory_order_relaxed);
            NoteOtherCaller(caller);
        }
    }
    return ok;
}

BOOL WINAPI ToldScreenToClient(HWND w, LPPOINT p) {
    const void* caller = _ReturnAddress();
    const BOOL ok = g_screenToClient(w, p);
    int width, height;
    RECT real;
    if (ok && p && Told(w, width, height) && FromGame(caller) && g_getClientRect(w, &real) && real.right > 0 &&
        real.bottom > 0) {
        p->x = MulDiv(p->x, width, real.right);
        p->y = MulDiv(p->y, height, real.bottom);
        g_pointsMoved.fetch_add(1, std::memory_order_relaxed);
    }
    return ok;
}

BOOL WINAPI ToldClientToScreen(HWND w, LPPOINT p) {
    const void* caller = _ReturnAddress();
    int width, height;
    RECT real;
    if (p && Told(w, width, height) && FromGame(caller) && g_getClientRect(w, &real) && real.right > 0 &&
        real.bottom > 0) {
        p->x = MulDiv(p->x, real.right, width);
        p->y = MulDiv(p->y, real.bottom, height);
        g_pointsMoved.fetch_add(1, std::memory_order_relaxed);
    }
    return g_clientToScreen(w, p);
}

void HookSize() {
    g_sizeHooked = true;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    void* getClientRect = user32 ? reinterpret_cast<void*>(GetProcAddress(user32, "GetClientRect")) : nullptr;
    void* screenToClient = user32 ? reinterpret_cast<void*>(GetProcAddress(user32, "ScreenToClient")) : nullptr;
    void* clientToScreen = user32 ? reinterpret_cast<void*>(GetProcAddress(user32, "ClientToScreen")) : nullptr;
    if (!getClientRect || !screenToClient || !clientToScreen ||
        MH_CreateHook(getClientRect, reinterpret_cast<void*>(&ToldGetClientRect),
                      reinterpret_cast<void**>(&g_getClientRect)) != MH_OK ||
        MH_CreateHook(screenToClient, reinterpret_cast<void*>(&ToldScreenToClient),
                      reinterpret_cast<void**>(&g_screenToClient)) != MH_OK ||
        MH_CreateHook(clientToScreen, reinterpret_cast<void*>(&ToldClientToScreen),
                      reinterpret_cast<void**>(&g_clientToScreen)) != MH_OK ||
        MH_EnableHook(getClientRect) != MH_OK || MH_EnableHook(screenToClient) != MH_OK ||
        MH_EnableHook(clientToScreen) != MH_OK) {
        // Not fitted without all three: the game would see its window's real size and draw at it. Any that took are
        // switched off again (their trampolines stay valid: the real functions).
        for (void* target : {getClientRect, screenToClient, clientToScreen})
            if (target)
                MH_DisableHook(target);
        Log("Window: the window's size couldn't be told to the game; it stays as big as the game draws");
        return;
    }
    g_canTell = true;
    Log("Window: the game can be told its window's size (fitting the window to the monitor)");
}

bool ClientMouse(UINT msg) {
    return (msg >= WM_MOUSEMOVE && msg <= WM_MBUTTONDBLCLK) || (msg >= WM_XBUTTONDOWN && msg <= WM_XBUTTONDBLCLK) ||
           msg == WM_MOUSEHOVER;
}

// The mouse's place in the window, in the size the game's told: its menus are laid out in that.
LPARAM ToGame(HWND w, LPARAM lp, int width, int height) {
    RECT real;
    if (!RealClientRect(w, &real) || real.right <= 0 || real.bottom <= 0)
        return lp;
    const int x = MulDiv(GET_X_LPARAM(lp), width, real.right);
    const int y = MulDiv(GET_Y_LPARAM(lp), height, real.bottom);
    return MAKELPARAM(static_cast<WORD>(static_cast<short>(x)), static_cast<WORD>(static_cast<short>(y)));
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

// On the window's own thread: borderless, as big as the monitor takes in the picture's shape, in its middle; the game
// told the size it draws at first, so the WM_SIZE the resize brings says that.
void Fit(HWND w, int width, int height) {
    const LONG style = GetWindowLongW(w, GWL_STYLE);
    const bool framed = (style & (WS_CAPTION | WS_THICKFRAME)) != 0;
    if (!framed && !g_restyled.load())
        return;  // the game's own fullscreen: it sizes itself to the display mode
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &monitor))
        return;
    const RECT& m = monitor.rcMonitor;
    const int across = m.right - m.left, down = m.bottom - m.top;
    if (across <= 0 || down <= 0)
        return;
    const float shape = g_shape.load() > 0.0f ? g_shape.load() : static_cast<float>(width) / static_cast<float>(height);
    int fitWidth = across;
    int fitHeight = static_cast<int>(std::lround(across / shape));
    if (fitHeight > down) {
        fitHeight = down;
        fitWidth = std::min(across, static_cast<int>(std::lround(down * shape)));
    }
    const RECT want{m.left + (across - fitWidth) / 2, m.top + (down - fitHeight) / 2,
                    m.left + (across - fitWidth) / 2 + fitWidth, m.top + (down - fitHeight) / 2 + fitHeight};

    const uint32_t told = Pack(width, height);
    const bool newTold = g_told.exchange(told) != told;
    if (framed) {
        g_ownStyle = style;
        g_ownExStyle = GetWindowLongW(w, GWL_EXSTYLE);
        g_restyled = true;
        SetWindowLongW(w, GWL_EXSTYLE,
                       g_ownExStyle & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
        SetWindowLongW(w, GWL_STYLE, (style & ~(WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX)) | WS_POPUP);
    }
    RECT now{};
    GetWindowRect(w, &now);
    const bool moved = framed || !EqualRect(&now, &want);
    if (moved)
        SetWindowPos(w, nullptr, want.left, want.top, fitWidth, fitHeight,
                     SWP_NOZORDER | SWP_NOACTIVATE | (framed ? SWP_FRAMECHANGED : 0));
    // The window's own size stayed (only the size the game draws at changed): told by a WM_SIZE of its own.
    if (g_sent.load() != told)
        WindowProc(w, WM_SIZE, SIZE_RESTORED, 0);
    if (moved || newTold) {
        g_fittedAt = GetTickCount64();
        Log("Window: fitted to the monitor, borderless, %dx%d at (%ld, %ld) (the picture %.2f:1); the game still draws "
            "%dx%d for the headset, scaled into it",
            fitWidth, fitHeight, want.left, want.top, shape, width, height);
    }
}

// On the window's own thread: its frame back, as big as the game draws (the game told its window's real size).
void Unfit(HWND w, int width, int height) {
    g_told = 0;
    if (!g_restyled.exchange(false))
        return;
    SetWindowLongW(w, GWL_EXSTYLE, g_ownExStyle);
    SetWindowLongW(w, GWL_STYLE, g_ownStyle);
    RECT frame{0, 0, width, height};
    AdjustWindowRectEx(&frame, g_ownStyle, FALSE, g_ownExStyle);
    SetWindowPos(w, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    Log("Window: as big as the game draws again, %dx%d (bigger than most monitors: the desktop shows its top left)",
        width, height);
}

void Apply(HWND w) {
    // In the game's own fullscreen, its window is left alone; the game is still told the size it draws at (the
    // headset's), as it was.
    if (w != g_window.load() || IsIconic(w) || g_gameFullscreen.load())
        return;
    const int size = config::Get().windowSize;
    if (size <= 0)
        return;
    const int width = g_wide.load() ? 2 * size : size;
    if (g_fit.load() && g_canTell.load())
        Fit(w, width, size);
    else
        Unfit(w, width, size);
}

// Windows normally refuses to make a window bigger than the screen; let BF3's grow. Fitted, the game is told its
// window is the size it draws at, and the mouse's place in it in that size.
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    const UINT apply = g_applyMessage.load(std::memory_order_relaxed);
    if (apply && msg == apply) {
        Apply(hwnd);
        return 0;
    }
    int width, height;
    if (Told(hwnd, width, height)) {
        if (msg == WM_SIZE && wp != SIZE_MINIMIZED)
            lp = MAKELPARAM(width, height);
        else if (ClientMouse(msg))
            lp = ToGame(hwnd, lp, width, height);
    }
    if (msg == WM_SIZE && wp != SIZE_MINIMIZED)
        g_sent = Pack(LOWORD(lp), HIWORD(lp));
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

// 10 s and a minute after the window's fitted: what the game asked of it, and who else asked its size.
void LogFitStats(ULONGLONG now) {
    static int logged = 0;
    const ULONGLONG at = g_fittedAt.load();
    if (!at || logged >= 2 || now - at < (logged ? 60000u : 10000u))
        return;
    ++logged;
    char others[200] = "";
    for (auto& slot : g_otherCallers) {
        const uintptr_t caller = slot.load();
        HMODULE module = nullptr;
        char path[MAX_PATH];
        if (!caller ||
            !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCSTR>(caller), &module) ||
            !GetModuleFileNameA(module, path, MAX_PATH))
            continue;
        const char* name = std::strrchr(path, '\\');
        name = name ? name + 1 : path;
        if (!std::strstr(others, name) && std::strlen(others) + std::strlen(name) + 3 < sizeof(others)) {
            if (others[0])
                strcat_s(others, ", ");
            strcat_s(others, name);
        }
    }
    Log("Window: fitted, the game asked its window's size %d times (other code %d%s%s%s), the mouse's place in it "
        "turned to its size %d times",
        g_askedByGame.load(), g_askedByOthers.load(), others[0] ? ": " : "", others,
        g_askedByGame.load() || !g_askedByOthers.load() ? "" : "; none from the game's own code", g_pointsMoved.load());
}

}

namespace window {

void Maintain() {
    KeepPointer();
    LoadFit();
    static ULONGLONG lastCheck = 0;
    static bool lastWide = false, lastFit = true;
    static float lastShape = 0.0f;
    const bool wide = g_wide.load(), fit = g_fit.load();
    const float shape = g_shape.load();
    ULONGLONG now = GetTickCount64();
    LogFitStats(now);
    if (now - lastCheck < 2000 && wide == lastWide && fit == lastFit && shape == lastShape)
        return;
    lastCheck = now;
    lastWide = wide;
    lastFit = fit;
    lastShape = shape;

    HWND w = g_window.load(std::memory_order_relaxed);
    if (!w || !IsWindow(w)) {
        w = nullptr;
        g_originalProc = nullptr;
        EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&w));
        if (!w)
            return;
        // A window of its own: nothing told, nothing restyled yet.
        g_told = 0;
        g_sent = 0;
        g_restyled = false;
        if (!g_applyMessage.load())
            g_applyMessage = RegisterWindowMessageW(L"bf3vr_window_fit");
        g_originalProc =
            reinterpret_cast<WNDPROC>(SetWindowLongPtrW(w, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProc)));
        g_window.store(w, std::memory_order_relaxed);
    }
    if (!g_pointerHooked)
        HookPointer();
    if (!g_sizeHooked)
        HookSize();

    int size = config::Get().windowSize;
    if (size <= 0)
        return;
    int width = wide ? 2 * size : size;

    // Fitted, or its frame to be put back: done on the window's own thread (Apply), which sees what's needed.
    if ((g_fit.load() && g_canTell.load()) || g_restyled.load()) {
        if (g_originalProc && g_applyMessage.load())
            PostMessageW(w, g_applyMessage.load(), 0, 0);
        return;
    }

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

void SetFit(bool on) {
    LoadFit();
    if (g_fit.exchange(on) != on)
        Log("Window: %s", on ? "to be fitted to the monitor" : "to be as big as the game draws");
}

void SetGameFullscreen(bool on) {
    if (g_gameFullscreen.exchange(on) != on)
        Log("Window: the game %s its own fullscreen%s", on ? "is in" : "left", on ? "; its window is left alone" : "");
}

void SetShape(float aspect) {
    g_shape = aspect > 0.0f ? std::clamp(aspect, 0.25f, 4.0f) : 0.0f;
}

bool GameSize(HWND__* w, int& width, int& height) {
    return Told(w, width, height);
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
