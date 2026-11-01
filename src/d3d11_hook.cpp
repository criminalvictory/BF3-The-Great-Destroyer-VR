#include "d3d11_hook.h"
#include "crash.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>

#include "config.h"
#include "depth3d.h"
#include "effects.h"
#include "log.h"
#include "perfinfo.h"
#include "shaders.h"
#include "skin.h"
#include "xr_runtime.h"

namespace {

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

// IDXGISwapChain vtable slots.
constexpr int kPresentSlot = 8;
constexpr int kResizeBuffersSlot = 13;

PresentFn g_origPresent = nullptr;
ResizeBuffersFn g_origResizeBuffers = nullptr;
void* g_presentFunction = nullptr;  // DXGI's Present itself, as every swap chain's table holds it

// The mod's work on each frame, then on to next (the rest of the way to the game's own Present).
HRESULT PresentWork(IDXGISwapChain* swap, UINT syncInterval, UINT flags, PresentFn next) {
    // A test Present (the game asking whether its window can be seen, minimised or covered) shows nothing and
    // follows no new picture: the back buffer still holds the last frame as the mod left it, the stream view's
    // (streamview.cpp), which would go to the headset as both eyes and be cropped again. Passed straight on.
    if (flags & DXGI_PRESENT_TEST)
        return next(swap, syncInterval, flags);
    crash::KeepOnTop();
    vr::OnPresent(swap);
    // How long the game's own Present takes once ours is done: handing frames to the headset
    // seemed to stall the game (about 18 ms a frame against 8 ms without), so time where.
    static LARGE_INTEGER frequency{};
    static double total = 0.0;
    static int calls = 0;
    if (!frequency.QuadPart)
        QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    // The game waits for the monitor's refresh (sync interval 1); a frame a little over one refresh
    // then waits for the next, capping it near 48 fps. The headset paces the game in VR, so the
    // desktop window needn't wait ([window] vsync=0).
    HRESULT hr = next(swap, config::Get().desktopVsync ? syncInterval : 0, flags);
    QueryPerformanceCounter(&t1);
    total += 1000.0 * static_cast<double>(t1.QuadPart - t0.QuadPart) / static_cast<double>(frequency.QuadPart);
    if (++calls == 60) {
        Log("Game's own Present: %.2f ms a frame (sync interval %u)", total / calls, syncInterval);
        total = 0.0;
        calls = 0;
    }
    return hr;
}

// OBS's game capture (graphics-hook32.dll) and overlays hook the same Present function after the mod, so their
// code runs before ours: OBS took each frame before the mod's work on it (the orange markers in the user's
// videos, 2026-10-31), and would record both eyes side by side where the window shows the stream view
// (streamview.cpp). The game calls Present through its swap chain's table of functions, so that table's
// Present is pointed at ours as well, at the swap chain's first Present: ours runs before any hook in the
// function, and the call goes on through them (OBS records the finished frame) to the real Present. Coming
// back round to the hook in the function, the call passes straight on.
struct TableHook {
    void** table = nullptr;
    PresentFn next = nullptr;  // what the table held: the function itself, or another program's hook
};
TableHook g_tables[4];  // a swap chain of another kind would have a table of its own
thread_local bool t_inTable = false;

HRESULT STDMETHODCALLTYPE TablePresent(IDXGISwapChain* swap, UINT syncInterval, UINT flags) {
    // Back round inside the chain (another program's table hook that put itself in front of ours again and calls
    // this as its original): the work is done; on through the function's own hooks to the real Present.
    if (t_inTable)
        return reinterpret_cast<PresentFn>(g_presentFunction)(swap, syncInterval, flags);
    void** table = *reinterpret_cast<void***>(swap);
    PresentFn next = reinterpret_cast<PresentFn>(g_presentFunction);  // through the function's hooks (OBS)
    for (const TableHook& t : g_tables)
        if (t.table == table && t.next)
            next = t.next;
    t_inTable = true;
    HRESULT hr = PresentWork(swap, syncInterval, flags, next);
    t_inTable = false;
    return hr;
}

void HookTable(IDXGISwapChain* swap) {
    void** table = *reinterpret_cast<void***>(swap);
    TableHook* slot = nullptr;
    for (TableHook& t : g_tables) {
        if (t.table == table)
            return;  // done (or tried) already
        if (!t.table && !slot)
            slot = &t;
    }
    if (!slot)
        return;
    slot->table = table;
    void* held = table[kPresentSlot];
    // Executable too: 32-bit dxgi.dll keeps its tables in its code section, and the page may hold code.
    DWORD protect = 0;
    if (!VirtualProtect(&table[kPresentSlot], sizeof(void*), PAGE_EXECUTE_READWRITE, &protect)) {
        Log("Present: the swap chain's table can't be written (%lu): OBS still records each frame before the "
            "mod's work on it", GetLastError());
        return;
    }
    slot->next = reinterpret_cast<PresentFn>(held);
    InterlockedExchangePointer(&table[kPresentSlot], reinterpret_cast<void*>(&TablePresent));
    VirtualProtect(&table[kPresentSlot], sizeof(void*), protect, &protect);
    Log("Present: hooked in the swap chain's table too, ahead of other programs' hooks (OBS records the mod's "
        "finished frame)%s",
        held == g_presentFunction ? "" : "; the table held another program's hook, called after ours");
}

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swap, UINT syncInterval, UINT flags) {
    if (t_inTable)  // through TablePresent, which did the work: on to the real Present
        return g_origPresent(swap, syncInterval, flags);
    HookTable(swap);
    return PresentWork(swap, syncInterval, flags, g_origPresent);
}

HRESULT STDMETHODCALLTYPE HookedResizeBuffers(IDXGISwapChain* swap, UINT count, UINT width, UINT height,
                                              DXGI_FORMAT format, UINT flags) {
    vr::OnResize();
    return g_origResizeBuffers(swap, count, width, height, format, flags);
}

// ID3D11DeviceContext vtable slots.
constexpr int kDrawIndexedSlot = 12;
constexpr int kDrawSlot = 13;
constexpr int kMapSlot = 14;
constexpr int kUnmapSlot = 15;
constexpr int kDrawIndexedInstancedSlot = 20;
constexpr int kDrawInstancedSlot = 21;
constexpr int kDrawIndexedInstancedIndirectSlot = 39;
constexpr int kDrawInstancedIndirectSlot = 40;
constexpr int kClearDepthStencilViewSlot = 53;

// Functions of the immediate context and of deferred contexts (which BF3 records draws on from
// several threads); they may be different functions.
struct ContextFunctions {
    void* map[2];
    void* unmap[2];
    void* drawIndexed[2];
    void* clearDepth[2];
    effects::DrawFunctions draws;
    void* createPixelShader;  // the device's (shaders.cpp)
};

// ID3D11Device vtable slot.
constexpr int kCreatePixelShaderSlot = 15;

void TakeDraws(void** table, int which, effects::DrawFunctions& d) {
    d.draw[which] = table[kDrawSlot];
    d.drawInstanced[which] = table[kDrawInstancedSlot];
    d.drawIndexedInstanced[which] = table[kDrawIndexedInstancedSlot];
    d.drawIndexedIndirect[which] = table[kDrawIndexedInstancedIndirectSlot];
    d.drawIndirect[which] = table[kDrawInstancedIndirectSlot];
}

// Swap chain and device context methods are shared by every object of their kind from
// d3d11.dll and dxgi.dll, so a throwaway device on a hidden window is enough to find them.
bool FindSwapChainVtable(void**& vtable, ContextFunctions& contexts) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"bf3vr_dummy";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
                                nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 64;
    sd.BufferDesc.Height = 64;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swap = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                               D3D11_SDK_VERSION, &sd, &swap, &device, nullptr, &context);
    bool ok = SUCCEEDED(hr);
    if (ok) {
        vtable = *reinterpret_cast<void***>(swap);
        void** immediate = *reinterpret_cast<void***>(context);
        contexts.map[0] = contexts.map[1] = immediate[kMapSlot];
        contexts.unmap[0] = contexts.unmap[1] = immediate[kUnmapSlot];
        contexts.drawIndexed[0] = contexts.drawIndexed[1] = immediate[kDrawIndexedSlot];
        contexts.clearDepth[0] = contexts.clearDepth[1] = immediate[kClearDepthStencilViewSlot];
        TakeDraws(immediate, 0, contexts.draws);
        TakeDraws(immediate, 1, contexts.draws);
        contexts.createPixelShader = (*reinterpret_cast<void***>(device))[kCreatePixelShaderSlot];
        ID3D11DeviceContext* deferred = nullptr;
        if (SUCCEEDED(device->CreateDeferredContext(0, &deferred))) {
            void** table = *reinterpret_cast<void***>(deferred);
            contexts.map[1] = table[kMapSlot];
            contexts.unmap[1] = table[kUnmapSlot];
            contexts.drawIndexed[1] = table[kDrawIndexedSlot];
            contexts.clearDepth[1] = table[kClearDepthStencilViewSlot];
            TakeDraws(table, 1, contexts.draws);
            deferred->Release();
        }
        swap->Release();
        context->Release();
        device->Release();
    } else {
        Log("Dummy D3D11 device creation failed: 0x%08X", static_cast<unsigned>(hr));
    }

    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

}

namespace hooks {

bool Install() {
    void** vtable = nullptr;
    ContextFunctions contexts{};
    if (!FindSwapChainVtable(vtable, contexts))
        return false;

    if (MH_Initialize() != MH_OK) {
        Log("MH_Initialize failed");
        return false;
    }
    g_presentFunction = vtable[kPresentSlot];
    if (MH_CreateHook(vtable[kPresentSlot], &HookedPresent, reinterpret_cast<void**>(&g_origPresent)) != MH_OK ||
        MH_CreateHook(vtable[kResizeBuffersSlot], &HookedResizeBuffers,
                      reinterpret_cast<void**>(&g_origResizeBuffers)) != MH_OK) {
        Log("MH_CreateHook failed");
        return false;
    }
    // Constant buffer uploads, for the skinning research (skin.cpp). Not fatal if it fails.
    bool skinHooked = skin::InstallHooks(contexts.map, contexts.unmap, contexts.drawIndexed);
    // Depth clears, to find the depth buffer for depth-based 3D (depth3d.cpp).
    bool depthHooked = depth3d::InstallHooks(contexts.clearDepth);
    // The game's memory statistics, from a copy (perfinfo.cpp).
    bool statsHooked = perfinfo::Install();
    // The other draw calls, for the gun's effects (effects.cpp). Not fatal if it fails.
    effects::InstallHooks(contexts.draws);
    // The game's pixel shaders by their bytecode, to know the lens flare's from run to run (shaders.cpp).
    shaders::InstallHook(contexts.createPixelShader);
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        Log("MH_EnableHook failed");
        return false;
    }
    Log("Present/ResizeBuffers hooked%s%s%s", skinHooked ? ", Map/Unmap/DrawIndexed hooked" : "",
        depthHooked ? ", ClearDepthStencilView hooked" : "", statsHooked ? ", GetPerformanceInfo hooked" : "");
    return true;
}

}
