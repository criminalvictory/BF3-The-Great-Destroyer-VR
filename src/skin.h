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

// weapon.txt says "watch": research logs are wanted (bones, the gun's matrices, poses).
bool Watching();

// The first-person view-projection BF3 uploaded last (rows as uploaded, before the wide-view
// rescale): in the space the arms and gun are drawn in, and in the world. Both carry the eye's
// position and the view's axes; their difference is where that space sits in the world.
bool FirstPersonViews(float (&arms)[16], float (&world)[16]);

// Free hands (hands.cpp): where to draw the gun and the left hand. Matrices are three rows,
// rotation | translation, from the gun's model space / the gloves' bind space into the view's
// own space: the eye at the origin, x right, y up, z back, metres. Each draw places that in the
// arms' space with the eye and axes of the view-projection uploaded for it, so the hands stay put
// relative to the picture however the arms' space moves.
struct HandTargets {
    bool gun = false, left = false;  // which of these is tracked
    float gunPose[12] = {};
    float leftPose[12] = {};
    float rightWrist[3] = {}, leftWrist[3] = {};  // each hand's grip point
    float leftClose = 0.0f;  // 0: the left glove open (its bind pose), 1: closed as the game holds it
};
void SetHands(const HandTargets* targets);  // nullptr = off: the game's own arms

// Where the game's own right hand holds the gun, in the gun's model space (from the hand's and the
// gun's bones as the game animates them). False until both have been drawn.
bool GunGrip(float* point);

}
