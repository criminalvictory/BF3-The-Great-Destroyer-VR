#pragma once

#include <d3d11.h>

#include <cstdint>
#include <functional>

namespace effects {

// The gun's effects, the muzzle flash and the spent casings, which BF3 draws where its own gun is
// rather than where free hands draw it.

// The draw calls skin.cpp doesn't hook: Draw, DrawInstanced, DrawIndexedInstanced and the two
// indirect ones, of the immediate and the deferred contexts. Call after MH_Initialize;
// MH_EnableHook(MH_ALL_HOOKS) turns them on.
struct DrawFunctions {
    void* draw[2];
    void* drawInstanced[2];
    void* drawIndexedInstanced[2];
    void* drawIndexedIndirect[2];
    void* drawIndirect[2];
};
bool InstallHooks(const DrawFunctions& functions);

// Every DrawIndexed (skin.cpp's hook), for the census and the probe: kHide skips the draw, kMove
// draws it with DrawWithGun (the muzzle flash's mesh after a shot).
enum DrawChoice { kNormal, kHide, kMove };
int NoteDrawIndexed(ID3D11DeviceContext* ctx, UINT count, UINT start, INT base);
// Calls draw with the view constants changed so what it draws moves with the gun in the right hand
// (free hands). False when it can't be moved (then it hasn't drawn). which: the immediate (0) or
// deferred (1) context's functions.
bool DrawWithGun(int which, ID3D11DeviceContext* ctx, const std::function<void()>& draw);

// weapon.txt "hidefx 36 234": indexed draws with these index counts are skipped (up to four), to
// tell in the headset which draws are the muzzle flash and the casings.
void SetHidden(const UINT* counts, int n);
// weapon.txt "hideps 1B77A09C": draws with these pixel shaders (up to four, as the census logs
// them for this run of the game) are skipped.
void SetHiddenShaders(const uintptr_t* shaders, int n);
// weapon.txt "flashmats 6": which view constants a moved draw gets changed (1 the view, 2 the
// view-projection, 4 the one taking points less the eye, 8 the projection; a sum).
void SetMoveMatrices(int which);
// weapon.txt "flashlift 1": what's moved with the gun is lifted that many metres more, to see
// plainly whether a way of moving it works.
void SetLift(float metres);

// Milliseconds since the last shot (the rounds in the gun going down by one), for animations.
ULONGLONG MsSinceShot();
// weapon.txt "movefx 36": indexed instanced draws with these index counts (up to four) are drawn
// where the gun in the right hand is, not at the game's own gun (free hands).
void SetMoved(const UINT* counts, int n);
// weapon.txt "dumpfx 234": the next three indexed instanced draws with that index count log the
// start of the 4096-byte block they're drawn with.
void DumpNext(UINT count);

// F18 (tools/drawhunt.ps1): a census of every draw for 6 s. The first 2 s are the baseline; fire
// during the rest, and the shaders drawn then that weren't in the baseline, or in more of the
// frames, are logged with what they were drawn with (stencil, blending, the views bound).
void StartCensus();
// Once a frame (render thread).
void Update();

// F19 (tools/drawhunt.ps1): for 10 s, every draw with something placed within 1 m of the game's
// own gun is logged once by its kind, with where (fire, with weapon.txt "watch").
void StartFlashProbe();

}
