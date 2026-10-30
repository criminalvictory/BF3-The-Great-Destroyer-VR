#pragma once

namespace skin {

// Research into BF3's skinning: the bone matrices it uploads to the graphics card to bend
// meshes such as the first-person arms. Hooks ID3D11DeviceContext::Map/Unmap/DrawIndexed
// (immediate and deferred: index 0 and 1, which may be the same function). Call between
// MH_Initialize and MH_EnableHook.
bool InstallHooks(void* const (&map)[2], void* const (&unmap)[2], void* const (&drawIndexed)[2]);

// Every frame: where the camera is in the world and its axes (right, up, true forward), so the
// log can show where blocks sit relative to the eyes.
void SetCamera(const float* position, const float* right, const float* up, const float* forward);

// Pause: research hooks on/off (bone searches in every upload, ring-buffer comparisons, draw
// counting). Off by default: they cost frame rate. The hunt and the lift turn them on.
void ToggleResearch();

// Lift the camera-relative pieces (within a metre of the origin) by 15 cm, or stop.
// Logs what's been seen since the last press.
void ToggleLift();

// Insert: draw the gloves (the draw the hunt found) 20 cm higher, or normally again. The first
// test of moving the hands.
void ToggleHandShift();

// Every frame: the game's own FOV and the wider one the mod renders with (radians; either 0 =
// off). Matrices uploaded with the game's FOV, such as the first-person gun and arms', are
// rescaled to the wide one.
void SetProjectionFix(float gameFov, float wideFov);

// Draw hunt, driven by tools/drawhunt.ps1 through keys no real keyboard has: F15 starts it
// (hiding half of all skinned draws seen so far), then F13 = "the arms vanished" or F14 = "still
// there" halves the candidates again, until the arms' draw is found. Its bone data is logged.
void HuntStart();
void HuntAnswer(bool vanished);

// Every frame: logs the found draw's bone data about once a second.
void Probe();

// The magazine of the gun the mod knows (the campaign's M16-type rifle): out of the gun, falling
// to the floor and lying there a while (false if the gun hasn't been drawn yet, or it's already
// out), or back in. Render thread.
bool EjectMagazine();
void InsertMagazine();
bool MagazineOut();
// Whether that gun is the one in hand (drawn in the last half second).
bool RifleInHand();

// Every frame on the render thread: moves the dropped magazine.
void Update();

}
