#pragma once

namespace reload {

// The rifle's bolt and its two manual releases (the user's wish, 2026-10-30). Run dry, the bolt
// locks back and the gun won't fire. With a magazine in, it goes forward and chambers a round when
// the charging handle (the lever at the back of the receiver's top) is pulled back with the left
// hand, grip closed, and let go, or when the bolt release on the left side of the receiver, just
// behind the magazine well, is slapped (the flat-screen game's empty reload).
//
// Once a frame on the render thread, with free hands (hands.cpp): the gun's pose (rows: rotation |
// translation, its model space to the view's) and the left palm in the view's space (nullptr when
// not tracked), and how far the left grip is squeezed (0-1).
void Update(const float* gunPose, const float* leftPalm, float squeeze);

// The bolt is back: the gun mustn't fire.
bool BoltBack();

// The game's own reload (X held), or a new life: a round in the chamber.
void ReleaseBolt();

// How far back the charging handle and the bolt are drawn, in metres along the barrel.
float HandleBack();
float BoltOffset();

}
