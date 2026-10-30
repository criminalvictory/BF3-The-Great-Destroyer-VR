#pragma once

#include <d3d11.h>

namespace depth3d {

// 3D from one picture and its depth. The alternate-eye stereo gives each eye a new picture only
// every second frame, from a different moment than the other eye's, which doubles the picture on
// head turns (a mono test, every frame to both eyes, had none). Here every frame becomes both
// eyes: each pixel is shifted sideways by the eye's offset over its distance, read from BF3's own
// depth buffer, the way the eye would see it from half the eye distance to the side.

// Hooks ID3D11DeviceContext::ClearDepthStencilView (immediate and deferred) to find the depth
// buffer the size of the picture. Call between MH_Initialize and MH_EnableHook.
bool InstallHooks(void* const (&clearDepthStencilView)[2]);

// The size of the game's picture, to tell its depth buffer from others.
void SetPictureSize(UINT width, UINT height);

// Which eye the desktop window should show (0 left, 1 right; "show 1" in depth3d.txt).
int MirrorEye();

struct Eye {
    ID3D11Texture2D* target;  // the headset image to draw into
    float shift;              // sideways shift in picture widths at 1 m: +left eye, -right eye
};

// Depth as BF3 stores it: distance = nearPlane / (depthScale - depth).
struct DepthModel {
    float nearPlane = 0.06f;
    float depthScale = 1.00001f;
    float minDistance = 0.25f;  // nearer than this counts as this (the gun's own depth range)
    // How the shift is shared between the eyes, overriding depth3d.txt's "bias" when in -1..1:
    // -1 the left eye gets the picture as it is and the right eye twice its shift, 1 the reverse.
    float bias = 2.0f;
};

// depth3d.txt's "strength": the share of the real eye distance the 3D uses.
float Strength();

// Draws both eyes from the back buffer and the depth buffer found this frame. False if no depth
// buffer has been found yet or a resource can't be made; then nothing was drawn.
bool Render(ID3D11Device* device, ID3D11Texture2D* back, DXGI_FORMAT targetFormat, const Eye (&eyes)[2],
            const DepthModel& model);

// Once a second: which depth buffers were cleared, and which one is used.
void Probe();

// The headset images went away (resize): forget the views made on them.
void Reset();

// depth3d.txt "save N" for a picture made another way (per-eye views): when N has changed, writes
// it to name next to the DLL. True if written.
bool SaveIfAsked(ID3D11Device* device, ID3D11Texture2D* picture, const wchar_t* name);

}
