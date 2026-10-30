// Waits for the game process and loads bf3vr.dll (from this exe's folder) into it.
// Usage: injector.exe [process name]   (default: bf3.exe)

#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <string>

namespace {

DWORD FindProcess(const std::wstring& name) {
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32W entry{sizeof(entry)};
    for (BOOL ok = Process32FirstW(snap, &entry); ok; ok = Process32NextW(snap, &entry)) {
        if (_wcsicmp(entry.szExeFile, name.c_str()) == 0) {
            pid = entry.th32ProcessID;
            break;
        }
    }
    CloseHandle(snap);
    return pid;
}

std::wstring DllPath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    return p.substr(0, p.find_last_of(L"\\/") + 1) + L"bf3vr.dll";
}

}

int wmain(int argc, wchar_t** argv) {
    std::wstring target = argc > 1 ? argv[1] : L"bf3.exe";
    std::wstring dll = DllPath();
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wprintf(L"Can't find %ls\n", dll.c_str());
        return 1;
    }

    wprintf(L"Waiting for %ls... start the game now.\n", target.c_str());
    DWORD pid = 0;
    while (!(pid = FindProcess(target)))
        Sleep(500);
    wprintf(L"Found %ls (pid %lu)\n", target.c_str(), pid);
    // Give the game a moment to finish starting before loading into it.
    Sleep(3000);

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                     PROCESS_VM_WRITE | PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) {
        wprintf(L"OpenProcess failed (%lu). Try running as administrator.\n", GetLastError());
        return 1;
    }

    BOOL selfWow = FALSE, targetWow = FALSE;
    IsWow64Process(GetCurrentProcess(), &selfWow);
    IsWow64Process(process, &targetWow);
    if (selfWow != targetWow) {
        wprintf(L"Bitness mismatch: the game is %ls but this injector is %ls. Use the matching build.\n",
                targetWow ? L"32-bit" : L"64-bit", selfWow ? L"32-bit" : L"64-bit");
        CloseHandle(process);
        return 1;
    }

    size_t bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(process, remote, dll.c_str(), bytes, nullptr)) {
        wprintf(L"Writing into the game failed (%lu)\n", GetLastError());
        CloseHandle(process);
        return 1;
    }

    // kernel32 is mapped at the same address in every process of the same bitness.
    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr);
    if (!thread) {
        wprintf(L"CreateRemoteThread failed (%lu)\n", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }
    WaitForSingleObject(thread, INFINITE);
    DWORD result = 0;
    GetExitCodeThread(thread, &result);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);

    if (!result) {
        wprintf(L"LoadLibrary failed inside the game\n");
        return 1;
    }
    wprintf(L"Loaded. See bf3vr.log next to the DLL.\n");
    return 0;
}
