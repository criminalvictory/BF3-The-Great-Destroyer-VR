#pragma once

namespace reload {

// The rifle's bolt and its two manual releases (the user's wish, 2026-10-30). Run dry, the bolt
// locks back and the gun won't fire. With a magazine in, it goes forward and chambers a round when
// the charging handle (the lever at the back of the receiver's top) is pulled back with the left
// hand, left trigger held, and let go, or when the bolt release on the left side of the receiver, just
// behind the magazine well, is slapped (the flat-screen game's empty reload).
//
// Once a frame on the render thread, with free hands (hands.cpp): the gun's pose (rows: rotation |
// translation, its model space to the view's) and the left palm in the view's space (nullptr when
// not tracked), how far the left trigger is pulled (0-1), and where the left glove touches the
// gun's surface, in its model space (nullptr when it doesn't: gunshape.cpp).
void Update(const float* gunPose, const float* leftPalm, float trigger, const float* touch);

// The bolt is back: the gun mustn't fire.
bool BoltBack();

// The left hand has hold of the charging handle (the glove is drawn on it, hooking it).
bool HandleHeld();

// The game's own reload (X held), or a new life: a round in the chamber.
void ReleaseBolt();

// How far back the charging handle is drawn, in metres along the barrel.
float HandleBack();

// How far the ejection port's cover has swung open on its hinge, in radians (0 = shut).
float CoverAngle();
// weapon.txt "pull 30": the charging handle held drawn this far (metres; below 0 no test), to see
// the cover open with it without anyone in the headset.
void TestPull(float metres);

}
