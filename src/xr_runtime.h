#pragma once
#include <dxgi.h>

namespace vr {
// Called from the game's render thread, just before the real Present.
// First call brings up OpenXR on the game's own D3D11 device.
void OnPresent(IDXGISwapChain* swap);
// The game is about to resize its back buffers; rebuild our swapchain next frame.
void OnResize();
}
