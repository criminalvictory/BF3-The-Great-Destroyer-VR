#pragma once
#include <openxr/openxr.h>

namespace hands {

// Free hands: the gun (with the right hand on it) is drawn at the right controller and the left
// glove at the left controller; the rest of the first-person arms is folded away. Works out the
// target poses every frame, in the space BF3 draws the first-person arms in, and hands them to
// skin.cpp, which redraws the arms and gun with moved bones.
//
// head: the eyes' centre and orientation in the recentered space, the controllers' in the same.
// Render thread, after input::Sync, in stereo mode.
void Update(const XrPosef& head);

// Insert: free hands on / off (on by default).
void Toggle();

// Free hands are on (the left trigger then works the charging handle instead of zooming).
bool On();

// True while the left hand steers the gun (steer.h: holding the handguard, eased in and out, letting
// go too): the right controller's aim orientation turned as the gun is drawn, for the game to aim
// and fire along, in the recentered space; otherwise false (the gun goes by the right hand alone).
// After Update, same thread.
bool AimOrientation(XrQuaternionf& out);

// weapon.txt "steer 35 65": the left hand steers the gun fully while the hands' line is within the
// first angle of where the right hand points it, not at all past the second (degrees; steer.h).
void SetSteerCone(float fullDegrees, float noneDegrees);

// The train pistol's slide held back by the left hand (build\pistol.txt "reload on" and "slide 1", the
// pistol in the right hand): it's out of battery, so the right trigger mustn't fire (input.cpp). False
// otherwise, and once Update hasn't seen it held for 0.2 s (free hands off, the headset not rendering).
// Render thread.
bool PistolSlideHeld();

// The train pistol's magazine by hand (build\pistol.txt "reload on" and "mag 1", the pistol in the right hand; a
// rehearsal: no rounds change). X tapped (input.cpp MagazineTap): its magazine release. One part way into the grip
// drops out; otherwise the pistol's own drops out of the grip; with the grip empty nothing (fromScript, F16: one goes
// in). False when it isn't the pistol's: X as before (the game's reload). Render thread.
bool PistolMagazineTap(bool fromScript);
// X held (the game's own reload, R), alike: the pistol's magazine is in again (one part way in drops out first, one in
// the left hand is gone). Nothing otherwise. Render thread.
void PistolGameReload();
// Its magazine out of the grip: the right trigger mustn't fire (input.cpp). False otherwise, and once Update hasn't
// seen it so with the pistol in hand for 0.2 s. Render thread.
bool PistolMagazineOut();

}
