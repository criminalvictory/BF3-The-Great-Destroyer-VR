#pragma once

#include <d3d11.h>

#include <cstdint>
#include <functional>

namespace overlay {

// The eyes side by side on the window, each drawn as the game draws a split screen's first view.
// BF3 draws each eye's finished picture onto the window, then that eye's own screen effects (the
// screen's blood, the damage tint), then the HUD once, for the second view. The right eye's view is
// placed at the window's left edge like the left's (views.cpp: placed at the right, its screen
// effects read the scene past its edge while suppressed and smeared the whole eye into streaks,
// 2026-10-30), so all of that comes out at the left edge; here it goes where it belongs: the right
// eye's picture and its own effects into the window's right half, the HUD into both halves.
// From the Draw and DrawIndexed hooks with the vertex or index count; true when it has drawn it.
// (start and base: a DrawIndexed's, for weapon.txt "tail" to read its vertices.)
bool Draw(ID3D11DeviceContext* ctx, UINT count, const std::function<void()>& draw, bool indexed = false,
          UINT start = 0, INT base = 0);

// From the Present hook, before NewFrame: the frame's HUD is done; the enemies' orange markers are
// taken off it (unless SetKeepOrange).
void FinishFrame(IDXGISwapChain* swap);
// weapon.txt "orangemarkers 1": the enemies' orange markers left on.
void SetKeepOrange(bool keep);
// weapon.txt "crosshair 1": the game's own crosshair (four ticks round the view's middle) left on.
void SetKeepCrosshair(bool keep);

// From the Present hook: a new frame's pictures follow.
void NewFrame();

// weapon.txt "tail": log the next frame's choices. "hudfix N": a HUD experiment (0 none).
void LogNextFrame();
void SetHudFix(int mode);
// weapon.txt "hudskip 10-19 40": the HUD's draws with those numbers in each frame (0-255, in the
// order the game draws them) left out, to tell which is which.
void SetHudSkip(const uint64_t (&bits)[4]);
// weapon.txt "hudskiptex 64x32": the HUD's draws whose first texture is that size (width << 16 |
// height, up to four; the first 0: none) left out.
void SetHudSkipTextures(const uint32_t (&sizes)[4]);

}
