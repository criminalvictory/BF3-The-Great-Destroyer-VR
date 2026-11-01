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
// The thread Present runs on (the render thread's): a crash's log says whether it was that one.
volatile DWORD g_presentThread = 0;

// bf3.exe+134E5B0 (and its twin at +134E670) is a step the renderer takes for each view: it walks a list
// of pointers (a mesh's parts, it seems) and, if one's +98h is zero, adds its owner to a set for that view
// under a lock. Its caller skips it for the right eye of BF3's own stereo 3D, not for the split-screen's
// second view. The game died
// at the walk's first load four times on 2026-10-31, each at a scripted moment (the train jump, a checkpoint
// load, twice after the interrogation): the list was torn, its begin null and its end not, as if freed
// under it. The walk is skipped then the way it ends when no part matches: its own way out. The code from
// the walk's first load to that way out, as the running game has it (build\bf3_mem.exe):
//   mov edx,[eax] / cmp dword ptr [edx+98h],0 / je +0Fh / add eax,4 / cmp eax,ecx / jne -12h /
//   pop esi / pop edi / add esp,8 / ret 8
constexpr unsigned char kPartsWalk[] = {0x8B, 0x10, 0x83, 0xBA, 0x98, 0x00, 0x00, 0x00, 0x00, 0x74, 0x0F, 0x83, 0xC0,
                                        0x04, 0x3B, 0xC1, 0x75, 0xEE, 0x5E, 0x5F, 0x83, 0xC4, 0x08, 0xC2, 0x08, 0x00};
constexpr uintptr_t kPartsWalks[] = {0x134E5F0, 0x134E6A0};
constexpr uintptr_t kPartsWalkOut = 0x12;

bool SkipTornWalk(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* e = info->ExceptionRecord;
    if (e->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || e->NumberParameters < 2 || e->ExceptionInformation[0] != 0)
        return false;
    const auto game = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto at = reinterpret_cast<uintptr_t>(e->ExceptionAddress);
    for (uintptr_t walk : kPartsWalks) {
        const uintptr_t start = game + walk;
        if (at != start && at != start + 2)  // the list's element, or the part's field
            continue;
        if (std::memcmp(reinterpret_cast<const void*>(start), kPartsWalk, sizeof(kPartsWalk)) != 0)
            return false;
        CONTEXT* c = info->ContextRecord;
        static LONG s_skips = 0;
        const LONG skips = InterlockedIncrement(&s_skips);
        if (skips <= 5 || (skips & (skips - 1)) == 0) {
            // The list as it is now: still torn (freed under the walk), or whole again (caught mid-change).
            DWORD now[2] = {};
            __try {
                now[0] = reinterpret_cast<const DWORD*>(c->Esi)[0];
                now[1] = reinterpret_cast<const DWORD*>(c->Esi)[1];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            Log("Crash guard: bf3.exe+%X read %08X walking a mesh's parts (the list at %08X, at %08X of its end %08X; "
                "now %08X %08X) on thread %u%s: skipped as if no part matched (%ld so far)",
                static_cast<unsigned>(at - game), static_cast<unsigned>(e->ExceptionInformation[1]), c->Esi, c->Eax,
                c->Ecx, now[0], now[1], GetCurrentThreadId(),
                GetCurrentThreadId() == g_presentThread ? ", the render thread" : "", skips);
        }
        c->Eip = static_cast<DWORD>(start + kPartsWalkOut);
        return true;
    }
    return false;
}

// bf3.exe+ADEFA0 (thiscall, ret 0Ch) steps something through a chain of refcounted nodes ([this+1D0h], the
// count at +8), asking the node through its vtable (+4) for values by name hash. 2026-10-31 19:48:57, at the
// same moment after the interrogation as the walk's crashes and on a thread other than the render thread's,
// the node had been freed under it: its vtable's +4 was zero and the game called address 0. The step is
// taken then the way the function takes it with no node: its own path at +ADEFC4, with the stack as the
// function's body has it (the call's return address and its one argument, the name hash, taken off). The
// code of both calls (+ADF011 to +ADF037) and of that path (+ADEFC4 to +ADEFF4), as the running game has it:
constexpr unsigned char kNodeCalls[] = {0x8B, 0x8E, 0xD0, 0x01, 0x00, 0x00, 0x8B, 0x11, 0x8B, 0x42, 0x04, 0x68, 0x10,
                                        0x12, 0x1A, 0x30, 0xFF, 0xD0, 0x8B, 0x8E, 0xD0, 0x01, 0x00, 0x00, 0x8B, 0x11,
                                        0x8B, 0xF8, 0x8B, 0x42, 0x04, 0x68, 0xF6, 0xDE, 0x8B, 0x23, 0xFF, 0xD0};
constexpr unsigned char kNoNodePath[] = {0x8B, 0x54, 0x24, 0x28, 0x8B, 0x02, 0x6A, 0x00, 0x83, 0xC6, 0x2C, 0x56,
                                         0x50, 0x8D, 0x44, 0x24, 0x34, 0x50, 0xE8, 0xF5, 0xF5, 0xFB, 0xFF, 0x8B,
                                         0x08, 0x8B, 0x44, 0x24, 0x30, 0x83, 0xC4, 0x10, 0x89, 0x4C, 0x24, 0x10,
                                         0x89, 0x08, 0x5F, 0x5E, 0x5D, 0x5B, 0x83, 0xC4, 0x0C, 0xC2, 0x0C, 0x00};
constexpr uintptr_t kNodeCallsAt = 0xADF011, kNodeReturns[] = {0xADF023, 0xADF037}, kNoNodeAt = 0xADEFC4;

bool SkipFreedNode(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* e = info->ExceptionRecord;
    CONTEXT* c = info->ContextRecord;
    if (e->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || c->Eip != 0 || e->NumberParameters < 2 ||
        e->ExceptionInformation[0] != 8)
        return false;  // only a call to address 0
    const auto game = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    DWORD returnTo = 0;
    __try {
        returnTo = *reinterpret_cast<const DWORD*>(c->Esp);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (returnTo != game + kNodeReturns[0] && returnTo != game + kNodeReturns[1])
        return false;
    if (std::memcmp(reinterpret_cast<const void*>(game + kNodeCallsAt), kNodeCalls, sizeof(kNodeCalls)) != 0 ||
        std::memcmp(reinterpret_cast<const void*>(game + kNoNodeAt), kNoNodePath, sizeof(kNoNodePath)) != 0)
        return false;
    static LONG s_skips = 0;
    const LONG skips = InterlockedIncrement(&s_skips);
    if (skips <= 5 || (skips & (skips - 1)) == 0)
        Log("Crash guard: bf3.exe+%X called address 0 through a freed node (this %08X, the node %08X) on thread %u%s: "
            "taken as if it had none (%ld so far)",
            static_cast<unsigned>(returnTo - game - 2), c->Esi, c->Ecx, GetCurrentThreadId(),
            GetCurrentThreadId() == g_presentThread ? ", the render thread" : "", skips);
    c->Esp += 8;
    c->Eip = static_cast<DWORD>(game + kNoNodeAt);
    return true;
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS* info) {
    if (SkipTornWalk(info) || SkipFreedNode(info))
        return EXCEPTION_CONTINUE_EXECUTION;
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
    Log("CRASH? %08X at %s, %s %s (thread %u%s); eax %08X ebx %08X ecx %08X edx %08X esi %08X edi %08X ebp %08X esp %08X",
        static_cast<unsigned>(e->ExceptionCode), where,
        e->NumberParameters >= 1 ? (e->ExceptionInformation[0] == 1 ? "writing" : e->ExceptionInformation[0] == 8 ? "running" : "reading") : "?",
        what, GetCurrentThreadId(), GetCurrentThreadId() == g_presentThread ? ", the render thread" : "", c->Eax, c->Ebx,
        c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
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
    g_presentThread = GetCurrentThreadId();
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
