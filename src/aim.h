#pragma once

namespace aim {

// Head or controller tracking by way of the player's look angles: each stereo frame, the change
// in yaw since the last frame is added (the mouse turns the body too), and pitch is set outright.
// BF3 then turns, culls, renders and aims from them itself.
// headYaw: radians, turning left is positive. headPitch: radians, looking up is positive.
// turn: the right stick's smooth turn for this frame (radians, left positive), added with it.
// False while the angles aren't known: they're looked for from the player's soldier (each new
// life or checkpoint) in the background, and nothing is written until then.
// body: the picture's body yaw, the game yaw the room's straight ahead shows (radians, the look yaw's sense;
// xr_runtime.cpp), or nullptr. Wherever the look yaw starts again (the angles found, a scripted moment over,
// mouse aim after a pause or a cutscene) it starts from there, so the view goes on and the game's camera turns
// to the gun.
bool Update(float headYaw, float headPitch, float turn = 0.0f, const float* body = nullptr);

// Forget the last head angles (mode switch, recenter), so the next Update doesn't jump: it starts
// again from the game's own yaw (a recenter turns the hand and the head together).
void Reset();

// Drops the look angles found, so they're looked for again: after the game drew from its own camera (a
// cutscene, the jump onto the train in The Great Destroyer; unturned, or a scripted camera turned to the
// head), the place they were found can hold something else, and writing there crashed the game 2 s after
// the jump (2026-10-31).
void Forget();

// weapon.txt "mouseaim 0/1": the view steered with the mouse while the look angles aren't found (on by default).
void SetMouseAim(bool on);

// True while this frame's mouse aim held its corrections for the search's nudge of a candidate: the game's
// facing then keeps the stick's turn but not the hand's (xr_runtime.cpp turns the walk from where it was).
bool Nudging();

// Whether the game is turning the look yaw itself (a scripted moment): nothing written meanwhile.
bool Scripted();

// weapon.txt "scripted 0/1": whether a scripted moment (the game turning the look yaw itself) stops the
// writes until it's over (on by default; aim.cpp).
void SetScriptedFollow(bool on);

// weapon.txt "aimtest 20": for 2 s from 1 s after, TestTurn() is that many degrees (radians), added
// to the hand's yaw, to check the view's turn without moving the controllers.
void StartTest(float degrees);
float TestTurn();

// The player's look yaw as BF3 stores it (radians, atan2(x, -z) of the camera's backwards row),
// and which way it moves for a controller turn to the left (+1 or -1). False if unreadable.
bool LookYaw(float& radians, float& sign);

// Logs about once a second where the yaw/pitch pointers lead and how many head moves were written.
void Probe();

// F4 / F2: flip which way a head turn / tilt moves the view, saved to bf3vr.ini.
void FlipYaw();
void FlipPitch();

}
