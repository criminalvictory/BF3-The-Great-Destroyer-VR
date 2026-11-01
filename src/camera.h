#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "config.h"

namespace camera {

// Frostbite LinearTransform: four rows of float4 (right, up, forward, translation).
struct Transform {
    float right[4];
    float up[4];
    float forward[4];
    float trans[4];
};

// memcpy that returns false instead of crashing on unmapped or protected memory.
bool SafeRead(uintptr_t address, void* out, size_t size);

// Follows a pointer chain to the address it ends at.
bool Resolve(const config::CameraPointer& pointer, uintptr_t& address);

// Follows the first pointer chain from bf3vr.ini. Returns false if it isn't set or doesn't resolve right now.
bool Read(Transform& out);

// Logs the camera transform and the write watch about once a second.
void Probe();

// Puts hardware write breakpoints on up to four camera copies in every game thread. Each time
// the game writes one, the current eye offset is added straight after, before anything renders it.
void StartWatch();

// Sideways shift along the camera's right vector, in metres (negative = left).
void SetEyeOffset(float metres);

// With a controller aiming, the game camera points where the hand points. This turns the render
// cameras so the picture shows where the head looks instead. Both in the recentered OpenXR space.
struct ViewTurn {
    float head[4];        // head orientation quaternion x, y, z, w
    // Without a look yaw: the yaw taken out of each camera as it is (radians, turning left positive), so the
    // picture follows the game's camera (xr_runtime.cpp holds it at the yaw the last picture took out).
    float controllerYaw;
    // Without lag guessing: the controller yaw and BF3's look yaw written this frame, and which
    // way the look yaw moves for a controller turn left. Each camera then reads the look yaw it
    // was built from out of its own matrix, so the hand's turn it includes is known exactly: the
    // picture shows lookYaw + currentYaw - the head (lookSign -1), whatever the game's camera did.
    bool haveLook;
    float currentYaw;
    float lookYaw;
    float lookSign;
    // Mouse aim holding its corrections for the search's nudge this frame (aim::Nudging): weapon.txt "viewlog"
    // marks those frames.
    bool nudging;
};
// nullptr = off (render cameras keep the game's own direction).
void SetViewTurn(const ViewTurn* turn);

// While the view is turned, the weapon camera ([aim] game_camera, which the gun model is placed
// from) is turned the opposite way so the gun stays with the hand. If the gun swings away from
// the hand instead, the direction is the other way round: flip = true.
void SetGunFlip(bool flip);

// Research (weapon.txt "wcam x y z"): the weapon camera moved by offset along its own right, up and
// back axes (metres) each time the game writes it, watched in every mode; nullptr = off.
void SetWeaponTest(const float* offset);

// The field of view to make the game render with, in radians (0 = the game's own). Written into
// the render and cull views and their source each time a camera breakpoint fires.
void SetRenderFov(float radians);
float WantedFov();

// Per-eye views (views.cpp) turn and shift the render and cull cameras where the game builds them:
// the breakpoints then leave those alone (still watching them, for RenderedFov) and only turn the
// weapon camera.
void SetViewsMode(bool on);
// A camera transform turned to the head, as the breakpoints turn the render cameras. False (and
// unchanged) while no view turn is set, or if it isn't a camera. A scripted camera (a cutscene's or a
// ride's, its rows' w lanes filled: viewturn.h) is turned too, made level about its yaw with the head
// put on it, its w lanes left as they are; weapon.txt "cutsceneturn 0" refuses it as before. body: the
// view's first-person camera (+0x930) when transform is the view's main camera (views.cpp), or nullptr.
// With a scripted camera and no look angles written, it's turned by the same turn about its own place
// (the turn as the main camera's own frame has it, put on it in its own: viewturn.h TurnWith); with the
// player's kind it's left as it is (the mod places the gun and arms itself), and with a refused camera too.
// The body isn't drawn from it: the main camera's turn goes to skin.cpp as well (BodyTurns), which turns
// the views the body is drawn with as they're uploaded.
bool TurnToHead(float* transform, float* body = nullptr);
// A view build's turn of its main camera, for the body lock (skin.cpp): build, BeginViews' count of view
// builds; at, when (GetTickCount64); turn, the main camera's turn as its own frame has it (viewturn.h
// OwnFrameTurn: turn[3 * row + k], the turned row's part along the game's row k); before and after, its
// rows (right, up, back) as the game made them and as turned; place, where the game put it (the world);
// firstPersonMade and firstPerson, the view's first-person camera's rows as the game made them and as
// turned with it (zeros if it isn't a camera).
struct BodyTurn {
    uint32_t build = 0;
    uint64_t at = 0;
    float turn[9] = {};
    float before[9] = {}, after[9] = {};
    float place[3] = {};
    float firstPersonMade[9] = {}, firstPerson[9] = {};
};
// The last view builds' turns, the newest first, up to most of them: only while the newest build turned a
// scripted main camera (a cutscene's or a ride's) with no look angles written, within the last quarter of
// a second, and then as many before it as did too, in a row. 0 in a normal picture, an unturned one, with
// "cutsceneturn 0", or with the look angles written (the mod places the gun and arms itself then). Any
// thread.
int BodyTurns(BodyTurn* out, int most);
// Whether BodyTurns has any: two atomics read, for every upload to ask cheaply. Any thread.
bool BodyTurnWanted();
// views.cpp, once a frame before its TurnToHead calls (on the same thread): the turn published now is the one
// they all use, so a turn published in between can't give the two eyes and their cull cameras different ones.
void BeginViews();
// The latest frame's first camera turned (the left eye's): its yaw as the game built it and the yaw the turn
// took out of it (radians; shown = camera + taken - head, in the look yaw's sense); for a scripted camera (a
// cutscene's or a ride's), its yaw as made level and nothing taken out. False before any, and when none was
// turned for a quarter of a second (a cutscene drawn unturned, the view turn off, theater mode): it's stale then.
// A scripted camera's picture, turned to the head, keeps it fresh.
bool ShownTurn(float& cameraYaw, float& takenYaw);
// How many cameras TurnToHead turned since the last call (xr_runtime.cpp, once a picture: none means
// the game drew it facing its own way, as in the cutscenes of The Great Destroyer). scripted: how many
// of them were scripted cameras (a cutscene's or a ride's), turned about their own yaw.
int TakeTurned(int* scripted = nullptr);
// weapon.txt "viewlog N": for N seconds, log each frame's view turn (hand, written look, game
// camera's yaw and place, head, shown).
void LogTurns(int seconds);

// weapon.txt "mirrorfix 0/1": a camera whose rows are mirrored from the player's own (a cutscene's)
// gets the head's turn mirrored to match (on by default).
void SetMirrorFix(bool on);

// weapon.txt "cutsceneturn 0/1": a scripted camera (a cutscene's or a ride's, its rows' w lanes filled)
// turned to the head about its own yaw (on by default), or refused, the picture held still in the room as
// before. Logged each time it's read.
void SetCutsceneTurn(bool on);

// The game's own field of view this frame (radians; 0 if unreadable), from bf3.exe+1F8B4F8.
float SourceFov();

// The FOV (radians) the game really drew the last frame with: the wider one while the render view
// is watched and widened, the game's own while it isn't (BF3 moves the render camera in memory,
// and after the opening ride the saved pointer chain stopped finding it). 0 if unknown.
float RenderedFov();

// The look angles the weapon camera ([aim] game_camera) was last written with by the game, before the
// mod turns it, as the player's look angles store them: yaw atan2 of its back row's x and -z, pitch
// -asin of its y (radians). False if it hasn't been written in the last 200 ms. Any thread.
bool GameWeaponAngles(float& yaw, float& pitch);

// How many times the game built its main render camera since the last call.
uint32_t TakeCameraBuilds();

// From the render view's projection matrix: depth = depthScale - nearPlane / distance (so
// distance = nearPlane / (depthScale - depth)). False, leaving them as they are, if unreadable.
bool ProjectionDepth(float& nearPlane, float& depthScale);
// Cull hunt: BF3 keeps dozens of identical camera copies, and decides what to draw from one that
// isn't turned with the head, so scenery goes missing when the hand aims away. The finder (F10)
// hands over every copy it saw; F6 then turns them with the head one at a time, a few seconds
// each, and F5 saves the one that makes the missing scenery come back.
void SetTestCopies(const std::vector<uintptr_t>& copies);
// F6: start the hunt, or stop it without saving.
void ToggleHunt();
// Every frame: moves the hunt on to the next copy when its time is up.
void UpdateHunt();
// The copy being tried right now; 0 when not hunting.
uintptr_t HuntCandidate();
void StopHunt();

}
