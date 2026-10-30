#pragma once
#include <cstdint>

namespace finder {

// Starts the camera finder on its own thread (F10). Does nothing if it's already running.
void Start();

// F5 during the F6 cull hunt: saves a pointer to the copy being tried as the cull camera.
void SaveCull();

// F3: finds the player's look angles (yaw, pitch) and saves pointers to them in bf3vr.ini.
void StartAngles();

// F12: research only. Finds rows of bone transforms that move like the first-person arms while
// the player reloads and aims, logs them with pointers, and writes their bones to skeleton.csv.
void StartSkeleton();

// Home: one step of the FOV finder. Press with BF3's FOV slider at 90, then 60, then 90; the
// third press saves pointers to the FOV setting in bf3vr.ini.
void FovStep();
// True between the first and third press (the FOV override holds off meanwhile).
bool FovFinderActive();

// Home: one step of the health finder. Press at full health, after taking a little damage, and
// once health has refilled; the third press saves pointers to the player's health.
void HealthStep();

// F16 (sent by a script): the three health steps on their own, timed by watching for the moment
// several health copies drop together and refill; the copy the others follow is saved first.
void HealthAuto();
// True between the first and third press (god mode holds off meanwhile).
bool HealthFinderActive();

}
