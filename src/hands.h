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

}
