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
    float controllerYaw;  // radians, turning left positive; view_lag frames old (the fallback)
    // Without lag guessing: the controller yaw and BF3's look yaw written this frame, and which
    // way the look yaw moves for a controller turn left. Each camera then reads the look yaw it
    // was built from out of its own matrix, so the hand's turn it includes is known exactly.
    bool haveLook;
    float currentYaw;
    float lookYaw;
    float lookSign;
};
// nullptr = off (render cameras keep the game's own direction).
void SetViewTurn(const ViewTurn* turn);

// While the view is turned, the weapon camera ([aim] game_camera, which the gun model is placed
// from) is turned the opposite way so the gun stays with the hand. If the gun swings away from
// the hand instead, the direction is the other way round: flip = true.
void SetGunFlip(bool flip);

// The field of view to make the game render with, in radians (0 = the game's own). Written into
// the render and cull views and their source each time a camera breakpoint fires.
void SetRenderFov(float radians);

// The game's own field of view this frame (radians; 0 if unreadable), from bf3.exe+1F8B4F8.
float SourceFov();

// The FOV (radians) the game really drew the last frame with: the wider one while the render view
// is watched and widened, the game's own while it isn't (BF3 moves the render camera in memory,
// and after the opening ride the saved pointer chain stopped finding it). 0 if unknown.
float RenderedFov();

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
