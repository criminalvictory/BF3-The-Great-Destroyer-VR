#pragma once

namespace aim {

// Head or controller tracking by way of the player's look angles: each stereo frame, the change
// in yaw since the last frame is added (the mouse turns the body too), and pitch is set outright.
// BF3 then turns, culls, renders and aims from them itself.
// headYaw: radians, turning left is positive. headPitch: radians, looking up is positive.
// turn: the right stick's smooth turn for this frame (radians, left positive), added with it.
// False while the angles aren't known: they're looked for from the player's soldier (each new
// life or checkpoint) in the background, and nothing is written until then.
bool Update(float headYaw, float headPitch, float turn = 0.0f);

// Forget the last head angles (mode switch, recenter), so the next Update doesn't jump.
void Reset();

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
