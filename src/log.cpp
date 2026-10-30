#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <share.h>
#include <string>

namespace {
FILE* g_file = nullptr;
std::mutex g_mutex;
}

namespace logx {

void Open(HMODULE self) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring p(path);
    p = p.substr(0, p.find_last_of(L"\\/") + 1) + L"bf3vr.log";
    // Shared so the log can be read while the game is still running.
    g_file = _wfsopen(p.c_str(), L"w", _SH_DENYNO);
}

void Write(const char* fmt, ...) {
    std::lock_guard lock(g_mutex);
    if (!g_file) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_file, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    vfprintf(g_file, fmt, args);
    va_end(args);
    fputc('\n', g_file);
    fflush(g_file);
}

}
