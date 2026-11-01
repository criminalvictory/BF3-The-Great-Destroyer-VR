#pragma once

#include <d3d11.h>
#include <openxr/openxr.h>

namespace streamview {

// The desktop window, and with it OBS's game capture, in per-eye stereo: one view instead of the two eyes side
// by side (the user, 2026-11-01: "if we could just have it to be one display, and preferably the left eye ...
// so I could start showing people gameplay"). Once the headset has the frame, the back buffer is drawn over with
// the left eye's picture, cropped to fill it ([stereo] stream_fov degrees across), seen from a steadied camera
// with the horizon level: the head's small shakes and its tilt are taken out ([stereo] stream_steady).

// image: the orientation the left eye's picture was drawn for (in the headset layers' space), or null when the
// headset had no frame (idle): the middle of the eye's picture then. fov: what the left eye's picture covers.
// Nothing happens unless the back buffer holds two square eyes side by side.
void Draw(ID3D11Device* device, ID3D11Texture2D* back, const XrQuaternionf* image, const XrFovf& fov);

// On or off (Home in game, [stereo] stream_view, weapon.txt "streamview 0/1").
void SetOn(bool on);
bool On();
// Degrees across (weapon.txt "streamfov 85").
void SetFov(float degrees);
// The shape it's drawn for, width over height, squeezed into the window ([stereo] stream_aspect, weapon.txt
// "streamaspect 1.7778"): 16:9 by default, which OBS's "stretch to screen" on a 16:9 canvas shows in proportion;
// 0 = the window's own shape (2:1 in per-eye stereo).
void SetAspect(float aspect);
// That shape, 0 for the window's own (window.cpp fits the window to the monitor in it).
float Aspect();
// The whole eye's picture fitted into that shape, black at its sides, instead of the view filled with its middle
// ([stereo] stream_fit, weapon.txt "streamfit 1"): as the headset shows it, neither turned nor steadied.
void SetFit(bool on);
// Steadied with the horizon level, or the eye's picture as it is (weapon.txt "streamsteady 0/1"); still: the
// steadying's cut-off with the head held still (Hz, lower is steadier), speedUp: how much faster it follows per
// radian a second of turning ("streamsteady 1 0.5 3").
void SetSteady(bool on, float still = -1.0f, float speedUp = -1.0f);
// Looking this many degrees below where the head looks, to show more of the gun (weapon.txt "streamtilt 5").
void SetTilt(float degrees);

// The head's view jumped (a recenter, a mode switch): the steadying starts again from where it looks.
void Reset();

}
