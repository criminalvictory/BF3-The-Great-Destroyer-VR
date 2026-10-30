#pragma once

namespace hooks {
// Hooks IDXGISwapChain::Present and ResizeBuffers for every swap chain in the process.
bool Install();
}
