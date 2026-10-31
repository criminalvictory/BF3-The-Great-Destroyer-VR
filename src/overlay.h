#pragma once

#include <d3d11.h>

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
bool Draw(ID3D11DeviceContext* ctx, UINT count, const std::function<void()>& draw);

// From the Present hook: a new frame's pictures follow.
void NewFrame();

// weapon.txt "tail": log the next frame's choices. "hudfix N": a HUD experiment (0 none).
void LogNextFrame();
void SetHudFix(int mode);

}
