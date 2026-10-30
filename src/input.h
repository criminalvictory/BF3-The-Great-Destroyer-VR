#pragma once
#include <openxr/openxr.h>

namespace input {

// Creates the controller actions and suggests bindings for the common controller profiles
// (SteamVR maps them onto any controller, Steam Frame included). Call once after the session is
// created. Returns false if controllers can't be used; the mod still works without them.
bool Init(XrInstance instance, XrSession session);

// Once per frame: reads the controllers, locates the right-hand aim pose in space, and turns
// buttons and sticks into keyboard/mouse input for BF3 (only while its window has focus).
void Sync(XrSession session, XrSpace space, XrTime time);

// The right controller's aim pose from the last Sync, if it's being tracked.
bool AimPose(XrPosef& out);

}
