#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "handpose.h"

struct ID3D11Resource;
struct ID3D11DeviceContext;

namespace skin {

// The bare arms of The Great Destroyer follow the controllers only while this allows it
// (xr_runtime.cpp: no cutscene picture or scripted turn for a few seconds, the aim found).
void SetBareHandsAllowed(bool allowed);
// weapon.txt "barewrist 0/1": the bare forearms solved from their hands (hands.cpp; on by default).
bool BareWrist();

// weapon.txt "pslog": the instanced draws (effects.cpp) logged like the indexed ones.
void LogShaderDraw(ID3D11DeviceContext* ctx, unsigned count, unsigned start, int base, unsigned instances);

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
// A magazine held in the left hand (HandTargets::mag) let go of: it falls from where it was last
// drawn in the hand (this last tenth of a second), starting at velocityView (m/s in the view's space;
// none: from rest). False if it hasn't been drawn there lately.
bool DropHeldMagazine(const float* velocityView = nullptr);
// Whether the magazine's seat in the gun (each of its bones' offset from the gun's body, as the game
// draws it in the well) is known: until then a magazine can't be drawn in the hand.
bool MagazineSeatKnown();
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
// Whether a constant buffer is one those view-projections went into: 1 in the arms' space, 2 in
// the world, 0 neither.
int ViewBufferKind(ID3D11Resource* buffer);

// This thread's latest 4096-byte upload (BF3's per-draw block: bones, or instances' placements)
// and its buffer; whole only while something needs it (weapon.txt "watch", free hands).
bool LatestBlock(ID3D11Resource*& resource, std::vector<uint8_t>& bytes);
// This thread's latest small upload into that buffer, if it's among the last eight (weapon.txt
// "watch" keeps them), and the latest 4096-byte block if it went into that buffer: valid until this
// thread's next upload. serial: UploadSerial() when it was made.
const uint8_t* RecentUploadOf(ID3D11Resource* resource, unsigned int& size, uint32_t& serial);
const uint8_t* LatestBlockOf(ID3D11Resource* resource, unsigned int& size, uint32_t& serial);
// Small fresh uploads made on this thread so far: data a draw uses is the draw's own only if it
// was uploaded after the draw before it.
uint32_t UploadSerial();
// Where the game's own gun body was last drawn, in the world and in the arms' space.
bool GameGunPlaces(float (&world)[3], float (&arms)[3]);
// The gun's move from where the game holds it to the right hand, to where the gun really is (no
// per-eye shift), in the world: a 4x4 (rows; column vectors). False while the gun isn't moved.
bool GunDeltaWorld(float (&out)[16]);
// The same move in the arms' space: rows rotation | translation.
bool GunDeltaArms(float (&out)[12]);
// This thread's latest per-view constants (352 bytes: the view-projection at +176) as they went to
// the graphics card, and their buffer.
const uint8_t* LatestViewConstants(ID3D11Resource*& resource);
// Draws (draw) with bytes bound in place of the constant buffer original, in the vertex shader
// slot it's in, then puts BF3's back. which: the immediate (0) or deferred (1) context's functions.
bool DrawWithCopy(int which, ID3D11DeviceContext* ctx, ID3D11Resource* original, const uint8_t* bytes,
                  unsigned int size, const std::function<void()>& draw);
// The game's own gun body as last drawn (bone 2 of its passes), in the world: rows rotation |
// translation.
bool GameGunBody(float (&out)[12]);

// Free hands (hands.cpp): where to draw the gun and the left hand. Matrices are three rows,
// rotation | translation, from the gun's model space / the gloves' bind space into the view's
// own space: the eye at the origin, x right, y up, z back, metres. Each draw places that in the
// arms' space with the eye and axes of the view-projection uploaded for it, so the hands stay put
// relative to the picture however the arms' space moves.
struct HandTargets {
    bool gun = false, left = false;  // which of these is tracked
    float gunPose[12] = {};
    // Set as the targets go into the arms' space for a draw: the gun where it really is (from the
    // middle of the head; gunPose then carries the shift that fakes each eye's view of it).
    float gunPoseTrue[12] = {};
    float leftPose[12] = {};
    // The right hand's as the left glove's is, mirrored (its controller with the left glove's line-up
    // mirrored across x): The Great Destroyer's bare right arm is placed with it (skin.cpp).
    bool right = false;
    float rightPose[12] = {};
    float rightWrist[3] = {}, leftWrist[3] = {};  // each hand's grip point
    // Each of the left glove's bones bent as its fingers are held, in the gloves' bind space
    // (handpose.cpp): the glove is drawn with leftPose composed with these.
    float leftFingers[handpose::kBones][12] = {};
    // A magazine in the left hand or on its way into the well (pouch.cpp): where the gun's body
    // would be with that magazine seated in it, so its bones are drawn at magPose times their seat.
    bool mag = false;
    float magPose[12] = {};
    // Each forearm solved from its hand and a shoulder (armik.h; hands.cpp, build\arms.txt "on"), left
    // then right: its elbow end and twist bones A and B, from the gloves' bind space into the view's,
    // and where its folded arm goes inside it. Without (arm false) the forearm moves whole with its
    // hand, as before.
    bool arm[2] = {};
    float forearm[2][3][12] = {};
    float fold[2][3] = {};
    // The bare arms' (The Great Destroyer) forearms solved from each hand the same way, left then right
    // (hands.cpp PlaceBareForearms): the hand bends at the wrist, the forearm stays on the arm's line.
    bool bareArm[2] = {};
    float bareForearm[2][3][12] = {};
};
void SetHands(const HandTargets* targets);  // nullptr = off: the game's own arms

// Where the game's own right hand holds the gun, in the gun's model space (from the hand's and the
// gun's bones as the game animates them). False until both have been drawn.
bool GunGrip(float* point);

// The game's own left glove as its animation last held the gun: each of its bones (handpose.cpp's
// 0-22) from the gloves' bind space into the gun's model space. Made by the game's animators for
// this gun, launcher and all, so it doesn't clip; hands.cpp puts it on the free gun while the left
// hand holds the handguard. False until learned once.
bool GameLeftGrip(float (*bones)[12]);

}
