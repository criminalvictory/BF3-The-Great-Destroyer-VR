#pragma once

namespace aim {

// Head or controller tracking by way of the player's look angles: each stereo frame, the change
// in yaw since the last frame is added (the mouse and stick turn the body too), and pitch is set
// outright. BF3 then turns, culls, renders and aims from them itself.
// headYaw: radians, turning left is positive. headPitch: radians, looking up is positive.
void Update(float headYaw, float headPitch);

// Forget the last head angles (mode switch, recenter), so the next Update doesn't jump.
void Reset();

// Logs about once a second where the yaw/pitch pointers lead and how many head moves were written.
void Probe();

// F4 / F2: flip which way a head turn / tilt moves the view, saved to bf3vr.ini.
void FlipYaw();
void FlipPitch();

}
