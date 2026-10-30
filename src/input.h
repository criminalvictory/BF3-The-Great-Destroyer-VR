#pragma once
#include <openxr/openxr.h>

namespace input {

// SteamVR's extension for the Steam Frame's controllers; enabled when the runtime offers it.
constexpr const char* kFrameControllerExtension = "XR_VALVE_frame_controller_interaction";

// Creates the controller actions and suggests bindings for the common controller profiles
// (SteamVR maps them onto any controller, Steam Frame included). Call once after the session is
// created. Returns false if controllers can't be used; the mod still works without them.
bool Init(XrInstance instance, XrSession session);

// Once per frame: reads the controllers, locates the right-hand aim pose in space, and turns
// buttons and sticks into keyboard/mouse input for BF3 (only while its window has focus).
void Sync(XrSession session, XrSpace space, XrTime time);

// The right controller's aim pose from the last Sync, if it's being tracked.
bool AimPose(XrPosef& out);

// A hand's grip pose (0 left, 1 right) from the last Sync, in the same space, if it's tracked.
bool HandPose(int hand, XrPosef& out);

// How far the left grip is squeezed (0-1): the left glove closes.
float LeftSqueeze();
// The left View button (Steam Frame) was pressed since the last call: line up the left glove.
bool TakeCalibratePress();

// X tapped (F16 from tools/drawhunt.ps1 does the same): the magazine out, dropping to the floor,
// or with it out a new one in. With a gun the mod doesn't know the magazine of, the game's reload.
void MagazineTap();

}
