#pragma once
// The free left glove kept out of the gun (the user: the hand went through the gun when it slapped
// the bolt release, 2026-10-30; then it still jerked about when it slapped softly). Moved out of
// hands.cpp so tools/collidetest.cpp can run it on scripted hand paths without the game: hands.cpp
// poses the glove and places it, this keeps it out.

namespace collide {

// What it keeps from frame to frame (hands.cpp holds one for the left glove).
struct State {
    // Where the glove's middle was drawn last frame, and where the tracked hand's middle was, in
    // the gun's model space (the user: the hand jittered in and out of the gun, 2026-10-30, when
    // each frame pushed it out afresh from wherever the hand was).
    float middle[3] = {};
    float handMiddle[3] = {};
    bool have = false;      // false: the glove starts at the hand
    bool touching = false;  // it touched last frame (it lets go a little further off than it touches)
};

// The glove not free (on the handguard, on the charging handle, lining up, untracked): it follows
// the hand again from wherever it is next free.
void Reset(State& state);

// Keeps the glove out of the gun: it rests on the gun where the hand goes into it, slides along it
// with the hand, is pushed out only as far as the gun (or the hand's turn) moves into it, and comes
// back to the hand no faster than the hand moves plus a little, so it doesn't jump. It can't pass
// through the gun however fast the hand moves. bones: the glove's bones as posed (handpose::Pose,
// the gloves' bind space); toGun: the glove's bind space -> the gun's model space, as the tracked
// hand places it (rows: rotation | translation). shift gets the move to draw the glove with (the
// gun's model space), touch where it touches the gun's surface, if it does: the gun's surface under
// its spheres within a centimetre of it, the nearest most (reload.cpp's bolt release slap goes by
// it). True when it touches: a sphere within 4 mm of the gun, and once touching, until 6 mm off.
bool KeepOut(const float (*bones)[12], const float* toGun, State& state, float* shift, float* touch);

// For tools/collidetest.cpp's measures: the glove's collision spheres (tools/gunshape.cpp measured
// them from the gloves' mesh dump): bone, finger (-1 the palm, 0 the thumb ... 4 the little
// finger), centre in the gloves' bind space, radius (metres).
struct Sphere {
    int bone;
    int finger;
    float centre[3];
    float radius;
};
const Sphere* Spheres(int& count);

}
