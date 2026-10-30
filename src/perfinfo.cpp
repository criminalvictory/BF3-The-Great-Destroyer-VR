#include "perfinfo.h"

#include <windows.h>
#include <psapi.h>

#include <MinHook.h>

#include <atomic>

#include "log.h"

namespace {

using GetPerformanceInfoFn = BOOL(WINAPI*)(PPERFORMANCE_INFORMATION, DWORD);
GetPerformanceInfoFn g_original = nullptr;

SRWLOCK g_lock = SRWLOCK_INIT;
PERFORMANCE_INFORMATION g_copy{};
bool g_haveCopy = false;

std::atomic<uint32_t> g_answered{0}, g_passedOn{0};
std::atomic<float> g_refreshMs{0.0f};
ULONGLONG g_lastLog = 0;

BOOL WINAPI Hooked(PPERFORMANCE_INFORMATION info, DWORD size) {
    if (info && size == sizeof(PERFORMANCE_INFORMATION)) {
        AcquireSRWLockShared(&g_lock);
        bool have = g_haveCopy;
        if (have)
            *info = g_copy;
        ReleaseSRWLockShared(&g_lock);
        if (have) {
            g_answered.fetch_add(1, std::memory_order_relaxed);
            return TRUE;
        }
    }
    g_passedOn.fetch_add(1, std::memory_order_relaxed);
    return g_original(info, size);
}

DWORD WINAPI Refresh(LPVOID) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    for (;;) {
        PERFORMANCE_INFORMATION fresh{};
        LARGE_INTEGER a, b;
        QueryPerformanceCounter(&a);
        BOOL ok = g_original(&fresh, sizeof(fresh));
        QueryPerformanceCounter(&b);
        g_refreshMs.store(static_cast<float>(1000.0 * (b.QuadPart - a.QuadPart) / frequency.QuadPart));
        if (ok) {
            AcquireSRWLockExclusive(&g_lock);
            g_copy = fresh;
            g_haveCopy = true;
            ReleaseSRWLockExclusive(&g_lock);
        }
        Sleep(1000);
    }
}

}

namespace perfinfo {

bool Install() {
    // psapi's GetPerformanceInfo, which the game loads by name, jumps straight to kernelbase's
    // (kernel32 has a copy of its own that the game never reaches).
    HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll");
    void* target =
        kernelbase ? reinterpret_cast<void*>(GetProcAddress(kernelbase, "K32GetPerformanceInfo")) : nullptr;
    if (!target ||
        MH_CreateHook(target, reinterpret_cast<void*>(&Hooked), reinterpret_cast<void**>(&g_original)) != MH_OK) {
        Log("Memory stats: K32GetPerformanceInfo not hooked; the game counts the PC's processes every frame");
        return false;
    }
    if (HANDLE t = CreateThread(nullptr, 0, Refresh, nullptr, 0, nullptr))
        CloseHandle(t);
    return true;
}

void LogStats() {
    ULONGLONG now = GetTickCount64();
    if (now - g_lastLog < 10000)
        return;
    g_lastLog = now;
    Log("Memory stats: the game asked %u times in 10 s, answered from the copy (%u passed on); a refresh "
        "takes %.1f ms",
        g_answered.exchange(0), g_passedOn.exchange(0), g_refreshMs.load());
}

}
