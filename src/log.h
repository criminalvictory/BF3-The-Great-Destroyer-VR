#pragma once
#include <windows.h>

namespace logx {
// Opens bf3vr.log next to the mod DLL.
void Open(HMODULE self);
void Write(const char* fmt, ...);
}

#define Log(...) logx::Write(__VA_ARGS__)
