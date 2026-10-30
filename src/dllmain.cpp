#include <windows.h>

#include "camera.h"
#include "config.h"
#include "d3d11_hook.h"
#include "log.h"

namespace {

HMODULE g_module = nullptr;

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
