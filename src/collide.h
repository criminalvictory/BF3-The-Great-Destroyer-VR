#pragma once
// The free left glove kept out of the gun (the user: the hand went through the gun when it slapped
// the bolt release, 2026-10-30). Moved out of hands.cpp so tools/collidetest.cpp can run it on
// scripted hand paths without the game: hands.cpp poses the glove and places it, this keeps it out.

namespace collide {

// What it keeps from frame to frame (hands.cpp holds one for the left glove).
struct State {
    // Where the glove is from the tracked hand, in the gun's model space (the user: the hand
    // jittered in and out of the gun, 2026-10-30, when each frame pushed it out afresh from wherever
    // the hand was, by whichever sphere was deepest).
    float shift[3] = {};
    // Where the glove was drawn last frame (its placement's origin in the gun's model space), to
    // sweep from.
    float lastGlove[3] = {};
    bool haveLastGlove = false;
};

// The glove not free (on the handguard, on the charging handle, lining up, untracked): it follows
// the hand again from wherever it is next free.
void Reset(State& state);

// Keeps the glove out of the gun: it stays where it was against the gun, pushed out only as far as
// the gun moved into it, and comes back to the hand as far as the gun lets it, so it rests on the
// surface instead of jumping. bones: the glove's bones as posed (handpose::Pose, the gloves' bind
// space); toGun: the glove's bind space -> the gun's model space, as the tracked hand places it
// (rows: rotation | translation). shift gets the move to draw the glove with (the gun's model
// space), touch where it touches the gun's surface, if it does. True when it touches.
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
