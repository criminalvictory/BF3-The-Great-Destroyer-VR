#pragma once

namespace finder {

// Starts the camera finder on its own thread (F10). Does nothing if it's already running.
void Start();

// F5 during the F6 cull hunt: saves a pointer to the copy being tried as the cull camera.
void SaveCull();

// F3: finds the player's look angles (yaw, pitch) and saves pointers to them in bf3vr.ini.
void StartAngles();

// Home: one step of the FOV finder. Press with BF3's FOV slider at 90, then 60, then 90; the
// third press saves pointers to the FOV setting in bf3vr.ini.
void FovStep();
// True between the first and third press (the FOV override holds off meanwhile).
bool FovFinderActive();

}
