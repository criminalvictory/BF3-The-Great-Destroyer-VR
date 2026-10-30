#pragma once

// Per-eye views. BF3 keeps a developer setting, GameRenderSettings.SplitScreenTestViewCount, that
// draws a second complete view beside the first every frame, from the same game state (found
// 2026-10-30: the frame's views are built by one function, 0x01018E70 in bf3.exe 1.6, which names
// them "Root" and "SecondaryRoot" and gives each half of the picture; the second one's camera is
// the first turned round by a test angle). Hooked here, the two become the eyes: the left eye in
// the left half, the right eye in the right half, each its own camera, as FrostbiteVR renders
// Bad Company 2 (no picture made up from depth, so no halo).
namespace views {

// Every frame, from the render thread. want: draw the two eye views (the per-eye stereo mode) or
// the game's single view. Hooks the view builder once bf3.exe's code is in place (it's decrypted
// at start-up) and sets the view count.
void Update(bool want);

// Whether the last frame was built as two eye views.
bool Active();

// Each eye's offset along the head's right, in metres (left negative), for the next frames.
void SetEyeOffsets(float left, float right);

// The offset (metres along the head's right) of eye 0 or 1, as last set.
float EyeOffset(int eye);

// The vertical field of view (radians) the eyes were last built with; 0 when not active.
float RenderedFov();

}
