#include "d3d11_hook.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>

#include "depth3d.h"
#include "log.h"
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

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swap, UINT syncInterval, UINT flags) {
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
    HRESULT hr = g_origPresent(swap, syncInterval, flags);
    QueryPerformanceCounter(&t1);
    total += 1000.0 * static_cast<double>(t1.QuadPart - t0.QuadPart) / static_cast<double>(frequency.QuadPart);
    if (++calls == 60) {
        Log("Game's own Present: %.2f ms a frame (sync interval %u)", total / calls, syncInterval);
        total = 0.0;
        calls = 0;
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookedResizeBuffers(IDXGISwapChain* swap, UINT count, UINT width, UINT height,
                                              DXGI_FORMAT format, UINT flags) {
    vr::OnResize();
    return g_origResizeBuffers(swap, count, width, height, format, flags);
}

// ID3D11DeviceContext vtable slots.
constexpr int kDrawIndexedSlot = 12;
constexpr int kMapSlot = 14;
constexpr int kUnmapSlot = 15;
constexpr int kClearDepthStencilViewSlot = 53;

// Functions of the immediate context and of deferred contexts (which BF3 records draws on from
// several threads); they may be different functions.
struct ContextFunctions {
    void* map[2];
    void* unmap[2];
    void* drawIndexed[2];
    void* clearDepth[2];
};

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
        ID3D11DeviceContext* deferred = nullptr;
        if (SUCCEEDED(device->CreateDeferredContext(0, &deferred))) {
            void** table = *reinterpret_cast<void***>(deferred);
            contexts.map[1] = table[kMapSlot];
            contexts.unmap[1] = table[kUnmapSlot];
            contexts.drawIndexed[1] = table[kDrawIndexedSlot];
            contexts.clearDepth[1] = table[kClearDepthStencilViewSlot];
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
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        Log("MH_EnableHook failed");
        return false;
    }
    Log("Present/ResizeBuffers hooked%s%s", skinHooked ? ", Map/Unmap/DrawIndexed hooked" : "",
        depthHooked ? ", ClearDepthStencilView hooked" : "");
    return true;
}

}
