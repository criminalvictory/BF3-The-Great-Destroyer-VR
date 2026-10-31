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

// The stand-in hand (weapon.txt "standin 1"): each frame after Sync, with the head's pose. While
// it's on and the right controller isn't tracked, AimPose and HandPose(1) give a right hand holding
// the rifle out in front, aimed where the head looks, to test the gun with nobody in the headset.
void SetStandIn(bool on);
void StandIn(const XrPosef& head);
bool StandingIn();
// The stand-in is switched on (weapon.txt "standin 1"), whether or not it holds the gun now.
bool StandInOn();

// The left glove's fingers: how far the grip is squeezed (0-1, the middle, ring and little
// fingers), how far the trigger is pulled (0-1, the index finger), and the thumb resting on the
// stick. All three: a fist.
float LeftSqueeze();
float LeftTrigger();
bool LeftThumbOnStick();
// The left View button (Steam Frame) was pressed since the last call: line up the left glove.
bool TakeCalibratePress();

// X tapped (F16 from tools/drawhunt.ps1 does the same): the magazine out, dropping to the floor,
// or with it out a new one in. With a gun the mod doesn't know the magazine of, the game's reload.
void MagazineTap();

// F17 (from tools/drawhunt.ps1): for 3 s, turns the way the right stick held all the way does,
// to measure how even the turn is without the controller.
void StartTurnTest();
// Once per frame, after Sync: the smooth turn from the right stick (or the F17 test), only while
// BF3 has focus. direct: the frame's turn is taken with TakeTurn and written into the look yaw
// with the head's (aim.cpp), which the picture follows; otherwise a thread moves the mouse.
void UpdateTurn(bool direct);
// The direct turn since the last call: radians, left positive ([controllers] turn_degrees).
float TakeTurn();
// Each frame: how far the head is turned from the way the game faces (radians, left positive).
// The left stick walks where the head faces, so it's turned by this into the game's frame.
void SetMoveTurn(float radians);
// The turn's evenness from frame to frame, logged once a second while the stick turns: which 0 =
// the yaw of the game's own camera as built, 1 = the yaw shown. Radians.
void NoteTurnStep(int which, float yaw);

}
