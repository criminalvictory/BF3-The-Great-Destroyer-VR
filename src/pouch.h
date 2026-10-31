#pragma once

#include <openxr/openxr.h>

namespace pouch {

// Magazines taken from a pouch at the front of the pelvis and pushed into the gun by hand (the M416,
// with the left hand; the user, 2026-10-31; the design: three designs and a judge, the same night).
// With the gun "hot" (its magazine out, or its bolt locked back on an empty one), a fresh squeeze of
// the left grip in front of the lower belly puts a magazine in the left glove: the gun's own magazine,
// drawn on its own (skin.cpp). Brought top first up under the magazine well, it's caught in a funnel
// below the well's mouth that straightens it as it's pushed, slides only along the well and seats when
// pushed home: only then is the game told (hands.cpp: the rounds, the gun's magazine, its sound).
// Let go of before that, it falls, or goes back in the pouch if let go there. Pure: no game calls and
// no logs (Result::note says why a squeeze took nothing), so magtest.exe runs it on scripted paths.

struct Frame {
    float dt = 0.0f;  // seconds since the last frame
    // In the room (the recentred space, y up, metres): the head, and the left controller's grip point
    // (the middle of the fist).
    XrPosef head{};
    bool left = false;      // a left hand this frame: the controller, or a test's
    XrVector3f leftGrip{};
    float squeeze = 0.0f;   // the left grip, 0-1
    // In the view's space (hands.cpp's): the gun's body (its model space into the view's) and the left
    // glove as its controller holds it (the gloves' bind space into the view's).
    bool gun = false;  // the gun the pouch knows (the M416), its own firing state, its magazine's seat known
    float gunPose[12] = {};
    float glove[12] = {};
    float guardDistance = 1.0f;  // the left palm from the handguard's line (metres)
    bool handFree = true;        // not on the handguard or the charging handle, not lining the glove up
    bool wellEmpty = false;      // the gun's magazine is out
    bool dry = false;            // its bolt is locked back
    bool spare = false;          // rounds left for another magazine
    bool forceInside = false;    // tests: the pouch counted as reached wherever the hand is
};

struct Result {
    bool mag = false;      // a magazine to draw: magPose, where the gun's body would be with it seated
    float magPose[12] = {};
    float glove[12] = {};  // the glove where it holds the magazine on the well's line ...
    float gloveBlend = 0;  // ... blended in by this much from where the hand is
    bool holding = false;  // a magazine in the hand (the hand is busy: no handguard, no bolt)
    // Events this frame.
    bool reached = false;  // a fresh squeeze reached the pouch (another gun: its own reload)
    bool took = false;     // a magazine taken: this squeeze is spent (not the handguard's)
    bool caught = false;   // caught in the funnel under the well
    bool offLine = false;  // pulled off the well's line: back in the hand as it's held
    bool seated = false;   // pushed home: the game is told
    bool letGo = false;    // let go of: it falls from where it was drawn last
    bool putBack = false;  // let go of at the pouch: gone back in it
    bool cancelled = false;  // gone without a fall (another magazine went in, the hand lost a second)
    // A fresh squeeze near the pouch (within 40 cm), for the log: where the grip point was in the
    // pouch's axes from its middle (right, up, forward; metres), how far out (1 its edge), and why
    // nothing was taken if it wasn't.
    bool squeezed = false;
    float fromMiddle[3] = {};
    float size = 0.0f;
    const char* note = "";
};

Result Update(const Frame& f);
// A magazine is in the left hand, or on its way into the well.
bool Holding();
// Gone without a fall (free hands off).
void Clear();

// build\pouch.txt (hands.cpp reads it; grip.txt is rewritten whole when the glove is lined up):
// "zone down forward across up depth": the pouch's middle that far below the neck and in front of it,
// and its half sizes (metres); "pouch off" turns it off.
void SetZone(const float* zone, bool on);
// "hold" and 12 numbers: the magazine's place in the left glove (rows, rotation | translation, from
// the gun's model space with the magazine seated into the gloves' bind space).
void SetHold(const float* hold);
// Where the glove is to hold a magazine at magPose (tests).
void GloveFor(const float* magPose, float* glove);

}
