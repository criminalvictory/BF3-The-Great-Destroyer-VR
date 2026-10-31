#pragma once
// The game's pixel shaders told apart from one start of the game to the next: their pointers change
// every time, their bytecode doesn't, so each one's bytecode checksum is kept as the game creates it
// (d3d11_hook.cpp hooks ID3D11Device::CreatePixelShader before the game makes its device).

#include <cstdint>

struct ID3D11PixelShader;

namespace shaders {

// The hook on the device's CreatePixelShader (MinHook; enabled with the others).
bool InstallHook(void* createPixelShader);

// The checksum of a shader's bytecode, 0 if its creation wasn't seen.
uint64_t ChecksumOf(ID3D11PixelShader* ps);

// The sun's lens flare ghost: an aperture-shaped ring near the middle of the view, one of the quads the
// game draws for the flare after the first-person pass, each with a pixel shader of its own (found
// 2026-10-31 with the sun in view, by hiding them in turn). In a headset it floats in front of the eyes
// like a crosshair (the user's "crosshair when I'm hit": it came with the sun while being shot at). The
// dirty lens's soft blobs from the same flare stay (the user likes them). Taken off unless weapon.txt
// says "lensflare 1"; "flareps <checksum> ..." adds checksums to the built-in one (as "tail" logs them).
bool IsLensFlare(ID3D11PixelShader* ps);
void SetLensFlare(bool keep, const uint64_t* extra, int extraCount);

}  // namespace shaders
