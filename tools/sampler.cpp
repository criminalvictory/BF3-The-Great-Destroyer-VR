// Where BF3's threads spend their time, seen from outside the game. The busiest threads are
// paused about a thousand times a second for a few seconds; each pause notes where the thread
// was: running code (where), or waiting in a system call (which one, and the calls that led
// there).
//
//   sampler [seconds=5] [threads=10]
//
// Waits read as system calls: NtWaitForSingleObject and its friends wait for another thread or
// for the graphics card (the driver waits for it with the same calls), NtDelayExecution is a
// Sleep, NtYieldExecution a thread giving its turn away. bf3.exe isn't relocated, so its
// addresses are printed as they are, ready for `dumpbin /disasm` of build\bf3_mem.exe.

#include <windows.h>
#include <intrin.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

HANDLE g_process = nullptr;

bool Read(uint32_t address, void* out, size_t size) {
    SIZE_T got = 0;
    return ReadProcessMemory(g_process, reinterpret_cast<LPCVOID>(static_cast<uintptr_t>(address)), out, size,
                             &got) &&
           got == size;
}

struct Module {
    std::string name;  // lower case, without the extension
    uint32_t base = 0, size = 0;
    std::vector<std::pair<uint32_t, std::string>> exports;  // by address
    bool system = false;  // Windows and the C runtime: what a wait passes through, not who waits
};
std::vector<Module> g_modules;  // by base

const Module* ModuleOf(uint32_t address) {
    auto it = std::upper_bound(g_modules.begin(), g_modules.end(), address,
                               [](uint32_t a, const Module& m) { return a < m.base; });
    if (it == g_modules.begin())
        return nullptr;
    --it;
    return address - it->base < it->size ? &*it : nullptr;
}

// The module's exported functions, from its export table in the game's memory.
void LoadExports(Module& m) {
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS32 nt;
    if (!Read(m.base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        !Read(m.base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE)
        return;
    const IMAGE_DATA_DIRECTORY dir = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress || dir.Size < sizeof(IMAGE_EXPORT_DIRECTORY))
        return;
    std::vector<uint8_t> blob(dir.Size);
    if (!Read(m.base + dir.VirtualAddress, blob.data(), blob.size()))
        return;
    auto at = [&](uint32_t rva, uint64_t bytes) -> const uint8_t* {
        return rva >= dir.VirtualAddress && rva - dir.VirtualAddress + bytes <= blob.size()
                   ? blob.data() + (rva - dir.VirtualAddress)
                   : nullptr;
    };
    const auto* ex = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(blob.data());
    const auto* functions = reinterpret_cast<const uint32_t*>(at(ex->AddressOfFunctions, ex->NumberOfFunctions * 4ull));
    const auto* names = reinterpret_cast<const uint32_t*>(at(ex->AddressOfNames, ex->NumberOfNames * 4ull));
    const auto* ordinals = reinterpret_cast<const uint16_t*>(at(ex->AddressOfNameOrdinals, ex->NumberOfNames * 2ull));
    if (!functions || !names || !ordinals)
        return;
    for (DWORD i = 0; i < ex->NumberOfNames; ++i) {
        if (ordinals[i] >= ex->NumberOfFunctions)
            continue;
        uint32_t rva = functions[ordinals[i]];
        if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size)
            continue;  // forwarded to another module
        const char* name = reinterpret_cast<const char*>(at(names[i], 1));
        if (!name)
            continue;
        size_t room = blob.size() - (names[i] - dir.VirtualAddress);
        m.exports.emplace_back(m.base + rva, std::string(name, strnlen(name, room)));
    }
    std::sort(m.exports.begin(), m.exports.end());
}

void LoadModules(DWORD pid) {
    static const char* kSystem[] = {"ntdll",    "kernelbase", "kernel32",     "win32u",   "gdi32",
                                    "gdi32full", "user32",    "ucrtbase",     "msvcp140", "vcruntime140",
                                    "msvcr100", "msvcp100",   "combase",      "rpcrt4",   "sechost",
                                    "advapi32", "msvcrt",     "kernel.appcore"};
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE)
        return;
    MODULEENTRY32W e{sizeof(e)};
    for (BOOL ok = Module32FirstW(snap, &e); ok; ok = Module32NextW(snap, &e)) {
        Module m;
        char name[MAX_PATH];
        WideCharToMultiByte(CP_ACP, 0, e.szModule, -1, name, sizeof(name), nullptr, nullptr);
        m.name = name;
        std::transform(m.name.begin(), m.name.end(), m.name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (size_t dot = m.name.rfind('.'); dot != std::string::npos)
            m.name.resize(dot);
        m.base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(e.modBaseAddr));
        m.size = e.modBaseSize;
        for (const char* s : kSystem)
            m.system |= m.name == s;
        LoadExports(m);
        g_modules.push_back(std::move(m));
    }
    CloseHandle(snap);
    std::sort(g_modules.begin(), g_modules.end(), [](const Module& a, const Module& b) { return a.base < b.base; });
}

// The nearest exported function at or below the address, if it's close enough to be the one the
// address is in.
const std::string* ExportOf(const Module& m, uint32_t address, uint32_t& offset) {
    auto it = std::upper_bound(m.exports.begin(), m.exports.end(), address,
                               [](uint32_t a, const std::pair<uint32_t, std::string>& e) { return a < e.first; });
    if (it == m.exports.begin())
        return nullptr;
    --it;
    offset = address - it->first;
    return offset < 0x4000 ? &it->second : nullptr;
}

// coarse: running code is grouped by the 256 bytes (bf3) or 4 KB (elsewhere) it's in.
std::string Text(uint32_t address, bool coarse = false) {
    char text[256];
    const Module* m = ModuleOf(address);
    if (!m) {
        snprintf(text, sizeof(text), "%08X", address);
    } else if (m->name == "bf3") {
        snprintf(text, sizeof(text), "bf3!%08X", coarse ? address & ~0xFFu : address);
    } else if (uint32_t offset = 0; const std::string* name = ExportOf(*m, address, offset)) {
        if (coarse)
            snprintf(text, sizeof(text), "%s!%s", m->name.c_str(), name->c_str());
        else
            snprintf(text, sizeof(text), "%s!%s+%X", m->name.c_str(), name->c_str(), offset);
    } else {
        uint32_t rva = address - m->base;
        snprintf(text, sizeof(text), "%s+%X", m->name.c_str(), coarse ? rva & ~0xFFFu : rva);
    }
    return text;
}

// A stack value is taken for a return address when it points into a module just after a call
// instruction: E8 rel32, or FF /2 with any addressing form.
std::unordered_map<uint32_t, bool> g_afterCall;

bool AfterCall(uint32_t address) {
    const Module* m = ModuleOf(address);
    if (!m || address - m->base < 0x1000)
        return false;
    auto [it, fresh] = g_afterCall.try_emplace(address, false);
    if (!fresh)
        return it->second;
    uint8_t b[8];
    if (!Read(address - 8, b, sizeof(b)))
        return false;
    bool call = b[3] == 0xE8;
    for (int k = 2; k <= 7 && !call; ++k) {
        const uint8_t* op = b + 8 - k;
        if (op[0] != 0xFF || ((op[1] >> 3) & 7) != 2)
            continue;
        int mod = op[1] >> 6, rm = op[1] & 7, length = 2;
        if (mod != 3 && rm == 4) {
            if (k < 3)
                continue;
            ++length;
            if (mod == 0 && (op[2] & 7) == 5)
                length += 4;
        }
        if (mod == 0 && rm == 5)
            length += 4;
        else if (mod == 1)
            length += 1;
        else if (mod == 2)
            length += 4;
        call = length == k;
    }
    it->second = call;
    return call;
}

bool IsSyscall(uint32_t address, std::string& name) {
    const Module* m = ModuleOf(address);
    if (!m || (m->name != "ntdll" && m->name != "win32u"))
        return false;
    uint32_t offset = 0;
    const std::string* e = ExportOf(*m, address, offset);
    if (!e || offset > 0x20 || !(e->rfind("Nt", 0) == 0 || e->rfind("Zw", 0) == 0))
        return false;
    name = *e;
    return true;
}

struct Thread {
    DWORD id = 0;
    HANDLE handle = nullptr;
    uint32_t start = 0, stackTop = 0;
    ULONGLONG cpu = 0;
    std::wstring description;
};

// CPU cycles the thread has used. (GetThreadTimes is only charged at clock ticks, and a thread
// that runs in short bursts between them can look idle.)
ULONGLONG Cpu(HANDLE thread) {
    ULONG64 cycles = 0;
    QueryThreadCycleTime(thread, &cycles);
    return cycles;
}

// The rate the cycle counter runs at, to turn cycles into time.
double CyclesPerSecond() {
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    unsigned long long t0 = __rdtsc();
    Sleep(200);
    QueryPerformanceCounter(&b);
    unsigned long long t1 = __rdtsc();
    return static_cast<double>(t1 - t0) * f.QuadPart / static_cast<double>(b.QuadPart - a.QuadPart);
}

constexpr int kFrames = 24;
struct Sample {
    uint16_t thread = 0;
    uint8_t count = 0;
    uint32_t eip = 0;
    uint32_t frames[kFrames];
};

struct Tally {
    std::string key;
    int count = 0;
};

void Add(std::unordered_map<std::string, int>& tally, const std::string& key) {
    ++tally[key];
}

void Print(const std::unordered_map<std::string, int>& tally, int total, int limit, double minimum) {
    std::vector<Tally> list;
    for (const auto& [key, count] : tally)
        list.push_back({key, count});
    std::sort(list.begin(), list.end(), [](const Tally& a, const Tally& b) { return a.count > b.count; });
    for (int i = 0; i < static_cast<int>(list.size()) && i < limit; ++i) {
        double share = 100.0 * list[i].count / total;
        if (share < minimum)
            break;
        printf("    %5.1f%%  %s\n", share, list[i].key.c_str());
    }
}

}

int main(int argc, char** argv) {
    const double seconds = argc > 1 ? atof(argv[1]) : 5.0;
    const int top = argc > 2 ? atoi(argv[2]) : 10;

    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{sizeof(pe)};
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !pid; ok = Process32NextW(snap, &pe))
        if (_wcsicmp(pe.szExeFile, L"bf3.exe") == 0)
            pid = pe.th32ProcessID;
    CloseHandle(snap);
    if (!pid) {
        printf("bf3.exe isn't running\n");
        return 1;
    }
    g_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!g_process) {
        printf("Can't open bf3.exe (%lu)\n", GetLastError());
        return 1;
    }
    LoadModules(pid);

    using QueryThread = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    auto query = reinterpret_cast<QueryThread>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
    using Describe = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    auto describe = reinterpret_cast<Describe>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));

    std::vector<Thread> threads;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{sizeof(te)};
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid)
            continue;
        Thread t;
        t.id = te.th32ThreadID;
        t.handle = OpenThread(THREAD_QUERY_INFORMATION | THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, t.id);
        if (!t.handle)
            continue;
        if (query) {
            ULONG start = 0;
            query(t.handle, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof(start), nullptr);
            t.start = start;
        }
        if (PWSTR text = nullptr; describe && SUCCEEDED(describe(t.handle, &text)) && text) {
            t.description = text;
            LocalFree(text);
        }
        t.cpu = Cpu(t.handle);
        threads.push_back(std::move(t));
    }
    CloseHandle(snap);

    // The busiest threads over a second.
    const double hz = CyclesPerSecond();
    Sleep(800);
    for (Thread& t : threads)
        t.cpu = Cpu(t.handle) - t.cpu;
    std::sort(threads.begin(), threads.end(), [](const Thread& a, const Thread& b) { return a.cpu > b.cpu; });
    printf("bf3.exe: %zu threads; the busiest over a second (%% of one core):", threads.size());
    for (size_t i = 0; i < threads.size() && i < 20; ++i)
        printf(" %lu:%.0f", threads[i].id, threads[i].cpu / hz * 100.0);
    printf("\n");
    for (size_t i = top; i < threads.size(); ++i)
        CloseHandle(threads[i].handle);
    if (threads.size() > static_cast<size_t>(top))
        threads.resize(top);

    // Sampling: each thread is paused only to copy its registers and the top of its stack.
    std::vector<Sample> samples;
    samples.reserve(static_cast<size_t>(seconds * 1000 * threads.size()) + 16);
    std::vector<uint32_t> stack(2048);
    LARGE_INTEGER frequency, begin, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);
    const LONGLONG interval = frequency.QuadPart / 1000;
    LONGLONG next = begin.QuadPart;
    for (Thread& t : threads)
        t.cpu = Cpu(t.handle);
    for (;;) {
        QueryPerformanceCounter(&now);
        if (now.QuadPart - begin.QuadPart >= static_cast<LONGLONG>(seconds * frequency.QuadPart))
            break;
        if (now.QuadPart < next) {
            YieldProcessor();
            continue;
        }
        next = std::max(next + interval, now.QuadPart - interval);
        for (size_t i = 0; i < threads.size(); ++i) {
            Thread& t = threads[i];
            if (SuspendThread(t.handle) == static_cast<DWORD>(-1))
                continue;
            CONTEXT c{};
            c.ContextFlags = CONTEXT_CONTROL;
            bool ok = GetThreadContext(t.handle, &c) != FALSE;
            SIZE_T got = 0;
            if (ok) {
                if (!t.stackTop || c.Esp >= t.stackTop) {
                    MEMORY_BASIC_INFORMATION mbi{};
                    if (VirtualQueryEx(g_process, reinterpret_cast<LPCVOID>(static_cast<uintptr_t>(c.Esp)), &mbi,
                                       sizeof(mbi)))
                        t.stackTop = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mbi.BaseAddress) +
                                                           mbi.RegionSize);
                }
                size_t bytes = t.stackTop > c.Esp ? std::min<size_t>(stack.size() * 4, t.stackTop - c.Esp) : 0;
                ReadProcessMemory(g_process, reinterpret_cast<LPCVOID>(static_cast<uintptr_t>(c.Esp)), stack.data(),
                                  bytes, &got);
            }
            ResumeThread(t.handle);
            if (!ok)
                continue;
            Sample s;
            s.thread = static_cast<uint16_t>(i);
            s.eip = c.Eip;
            for (size_t w = 0; w < got / 4 && s.count < kFrames; ++w)
                if (AfterCall(stack[w]))
                    s.frames[s.count++] = stack[w];
            samples.push_back(s);
        }
    }
    QueryPerformanceCounter(&now);
    const double elapsed = static_cast<double>(now.QuadPart - begin.QuadPart) / frequency.QuadPart;

    for (size_t i = 0; i < threads.size(); ++i) {
        Thread& t = threads[i];
        double busy = (Cpu(t.handle) - t.cpu) / hz / elapsed * 100.0;
        int total = 0, waiting = 0;
        std::unordered_map<std::string, int> calls, detail, callers;
        for (const Sample& s : samples) {
            if (s.thread != i)
                continue;
            ++total;
            // The system call the thread is in: the leaf, or (in WOW64's own code) the first frame.
            std::string call;
            bool inCall = IsSyscall(s.eip, call) || (!ModuleOf(s.eip) && s.count && IsSyscall(s.frames[0], call));
            // Who's behind it: the first frames outside Windows itself.
            std::string path, firstBf3;
            int shown = 0;
            for (int f = 0; f < s.count; ++f) {
                const Module* m = ModuleOf(s.frames[f]);
                if (!m || m->system)
                    continue;
                if (shown < 3) {
                    path += (shown ? " < " : "") + Text(s.frames[f]);
                    ++shown;
                }
                if (firstBf3.empty() && m->name == "bf3")
                    firstBf3 = Text(s.frames[f]);
            }
            if (inCall) {
                ++waiting;
                Add(calls, call);
                char at[16];
                snprintf(at, sizeof(at), "@%08X", s.eip);
                Add(detail, "wait " + call + at + "  " + path);
                Add(callers, "wait " + call + "  from " + (firstBf3.empty() ? "(no bf3 frame)" : firstBf3));
            } else {
                std::string leaf = Text(s.eip, true);
                Add(calls, "(running)");
                Add(detail, "run  " + leaf + "  " + path);
                const Module* m = ModuleOf(s.eip);
                Add(callers, std::string("run  in ") + (m ? m->name : "?") + "  from " +
                                 (firstBf3.empty() ? "(no bf3 frame)" : firstBf3));
            }
        }
        if (!total)
            continue;
        printf("\nThread %lu (start %s%s%ls): %.0f%% of a core, %d samples, waiting in system calls %.0f%%\n", t.id,
               Text(t.start).c_str(), t.description.empty() ? "" : ", ", t.description.c_str(), busy, total,
               100.0 * waiting / total);
        printf("  by call:\n");
        Print(calls, total, 8, 0.5);
        printf("  by where (first frames outside Windows):\n");
        Print(detail, total, 14, 0.8);
        printf("  by the game's code behind it:\n");
        Print(callers, total, 10, 0.8);
    }
    printf("\n%zu samples in %.1f s\n", samples.size(), elapsed);
    for (Thread& t : threads)
        CloseHandle(t.handle);
    CloseHandle(g_process);
    return 0;
}
