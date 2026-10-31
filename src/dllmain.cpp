#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cwchar>

#include "camera.h"
#include "crash.h"
#include "config.h"
#include "d3d11_hook.h"
#include "log.h"

namespace {

HMODULE g_module = nullptr;

// Where an address is: "module+offset", or the bare address outside any module.
void Where(uintptr_t address, char* out, size_t size) {
    HMODULE owner = nullptr;
    wchar_t path[MAX_PATH] = {};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &owner) &&
        GetModuleFileNameW(owner, path, MAX_PATH)) {
        const wchar_t* name = wcsrchr(path, L'\\');
        sprintf_s(out, size, "%ls+%X", name ? name + 1 : path, static_cast<unsigned>(address - reinterpret_cast<uintptr_t>(owner)));
    } else {
        sprintf_s(out, size, "%08X", static_cast<unsigned>(address));
    }
}

// The game crashed four times jumping from the bridge onto the train in The Great Destroyer
// (2026-10-31), each time at a different place in bf3.exe and with nothing in the log. A crash the
// game doesn't recover from is logged: where, what it touched, the registers, and the mod's and the
// game's return addresses on the stack, so the next one says whether a hook of the mod's was on the
// way. Only unhandled ones: watching every access violation (a vectored handler) caught the mod's
// own guarded reads by the dozen at each launch and filled its places before the train.
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;

LONG WINAPI OnCrash(EXCEPTION_POINTERS* info) {
    static LONG s_logged = 0;
    const EXCEPTION_RECORD* e = info->ExceptionRecord;
    const auto at = reinterpret_cast<uintptr_t>(e->ExceptionAddress);
    if (InterlockedIncrement(&s_logged) > 3)
        return g_previousFilter ? g_previousFilter(info) : EXCEPTION_CONTINUE_SEARCH;
    char where[160], what[160];
    Where(at, where, sizeof(where));
    const ULONG_PTR touched = e->NumberParameters >= 2 ? e->ExceptionInformation[1] : 0;
    Where(touched, what, sizeof(what));
    const CONTEXT* c = info->ContextRecord;
    Log("CRASH? %08X at %s, %s %s (thread %u); eax %08X ebx %08X ecx %08X edx %08X esi %08X edi %08X ebp %08X esp %08X",
        static_cast<unsigned>(e->ExceptionCode), where,
        e->NumberParameters >= 1 ? (e->ExceptionInformation[0] == 1 ? "writing" : e->ExceptionInformation[0] == 8 ? "running" : "reading") : "?",
        what, GetCurrentThreadId(), c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    // Return addresses on the stack: values pointing into code (the game's or the mod's).
    char line[1024] = "CRASH? stack:";
    size_t used = strlen(line);
    const auto* stack = reinterpret_cast<const uintptr_t*>(c->Esp);
    int found = 0;
    for (int i = 0; i < 512 && found < 24; ++i) {
        uintptr_t v = 0;
        __try {
            v = stack[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        MEMORY_BASIC_INFORMATION mbi;
        if (v < 0x10000 || !VirtualQuery(reinterpret_cast<LPCVOID>(v), &mbi, sizeof(mbi)) ||
            !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
            continue;
        char one[160];
        Where(v, one, sizeof(one));
        used += sprintf_s(line + used, sizeof(line) - used, " %s", one);
        ++found;
        if (used > sizeof(line) - 170)
            break;
    }
    Log("%s", line);
    // The code round the crash as it runs (bf3.exe's is encrypted on disk: dumpbin of the file shows
    // noise), 64 bytes before and 48 after, and the first stack words: to disassemble afterwards and
    // tell what was null (bf3.exe+134E5F0, three times on 2026-10-31: the train jump, a checkpoint load,
    // the interrogation's QTE).
    auto bytes = [](const char* name, uintptr_t from, int count) {
        char text[1024];
        int used = sprintf_s(text, "CRASH? %s %08X:", name, static_cast<unsigned>(from));
        for (int i = 0; i < count && used < static_cast<int>(sizeof(text)) - 4; ++i) {
            unsigned char b = 0;
            __try {
                b = reinterpret_cast<const unsigned char*>(from)[i];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                used += sprintf_s(text + used, sizeof(text) - used, " ??");
                break;
            }
            used += sprintf_s(text + used, sizeof(text) - used, " %02X", b);
        }
        Log("%s", text);
    };
    bytes("code", at - 64, 112);
    char words[1024] = "CRASH? stack words:";
    size_t wordsUsed = strlen(words);
    for (int i = 0; i < 32; ++i) {
        uintptr_t v = 0;
        __try {
            v = stack[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        wordsUsed += sprintf_s(words + wordsUsed, sizeof(words) - wordsUsed, " %08X", static_cast<unsigned>(v));
    }
    Log("%s", words);
    return g_previousFilter ? g_previousFilter(info) : EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

namespace crash {

// The game may set a crash filter of its own after the mod loads: put back on top each time (from
// Present, every few seconds), handing on to whatever was there.
void KeepOnTop() {
    static ULONGLONG s_last = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - s_last < 5000)
        return;
    s_last = now;
    LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter(OnCrash);
    if (previous != OnCrash) {
        g_previousFilter = previous;
        Log("Crash log: on (the game's own crash filter %s)", previous ? "handed on to" : "none");
    }
}

}  // namespace crash

namespace {

DWORD WINAPI Start(LPVOID) {
    Log("bf3vr loaded (%s build)", sizeof(void*) == 8 ? "x64" : "x86");
    config::Load(g_module);
    if (!hooks::Install())
        Log("Hook install failed; mod inactive");
    camera::StartWatch();
    return 0;
}

}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        g_module = module;
        logx::Open(module);
        // Never do real work under the loader lock.
        if (HANDLE t = CreateThread(nullptr, 0, Start, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    // No teardown on detach: the game is exiting and the OpenXR runtime cleans up after a dead process.
    return TRUE;
}
