#pragma once

#include <openxr/openxr.h>

namespace pistolmag {

// The train pistol's magazine (The Great Destroyer's M9) from the pouch at the pelvis into the grip, by the left hand
// (docs\research\pistol-manual-reload.md; hands.cpp, build\pistol.txt "mag 1"). Its own, not the M416's pouch.cpp,
// which stays as it is: this only takes pouch.cpp's events (a fresh squeeze reaching the pelvis, the hand at the
// pouch), so the zone, the squeeze's rules and the body's heading are the same for both guns:
//   - the well is raked: the magazine goes in along its own y, 16 degrees back from the gun's vertical, and the whole
//     magazine goes in (its floorplate ends flush with the grip's bottom: the mouth is 11.08 cm below home);
//   - the hand holds it on one place of the palm (the game's 2.390 s, its floorplate on the palm) in two poses: the
//     carry (the fingers closed round it) and the flat base (the fingers open, the magazine standing on the palm),
//     switched by "flat" as the magazine nears the grip, so nothing moves in the hand at the switch. A carry with a
//     place of its own (the game's pinch, 2.218 s: SetCarry) turns the magazine onto the flat base over 0.15 s as it
//     nears the grip, and it isn't caught until it has;
//   - on the flat base the grip may be let go of (the hand opens with the picture): the magazine stands on the palm
//     while it's under the grip; let go away from it, it falls;
//   - once its top is in the grip, it stays there (held by the well) if the hand drops away or lets go ("parked");
//     the palm under its floorplate pushes it the rest of the way; X drops it out (hands.cpp);
//   - the hands can't close up under the grip as the game's do (the controllers can't overlap: the review), so once
//     the palm has held its top within the snap distance of home for two frames running it goes home by itself, the
//     hand seen or not (SetSnap); each try on the grip's line says how near the palm brought it, to set that from;
//   - over its last 3 cm the hand goes over to the game's own push home (2.421 s: the palm centred under the
//     floorplate, its fingers too: Result::push).
// Gun space: the pistol frame bone's (3) bind space (x the gun's left, y up, z to the muzzle). View space: the head's
// (hands.cpp's). The arms' bind space: the bare arms' (and the gloves') rest pose. Pure: no game calls and no logs
// (Result::note says why a squeeze took nothing), so tools\pistolmagtest.cpp runs it on scripted paths.

struct Frame {
    float dt = 0.0f;
    // The pistol in the right hand with its magazine worked by hand (pistol.txt "mag 1"); the right controller seen
    // this frame; the pistol frame bone's bind space -> the view's.
    bool pistol = false;
    bool pistolTracked = true;
    float pistolPose[12] = {};
    // The left hand as its controller holds it (the arms' bind space -> the view's: hands.cpp's t.leftPose before any
    // blend), its grip point in the room (the drop's speed), the grip squeezed (0-1), and how far the two hands' grip
    // points are apart (m: the log of how near the palm came).
    bool left = false;
    float glove[12] = {};
    XrVector3f leftGrip{};
    float squeeze = 0.0f;
    float apart = 0.0f;
    // pouch.cpp's events this frame: a fresh squeeze reached the pelvis zone; the hand within the size a let go puts
    // a magazine back at.
    bool reached = false;
    bool atPouch = false;
    bool handFree = true;     // not holding the pistol in both hands, not on the slide, not lining the glove up
    bool wellEmpty = false;   // the pistol's magazine is out
    bool slideLocked = false; // its slide is held open on an empty magazine (a take is allowed then, as the M416's dry)
    bool spare = false;       // rounds left for a magazine
};

struct Result {
    bool mag = false;       // a magazine to draw: where the frame bone would be with it seated (draw: magPose x seat)
    float magPose[12] = {};
    float glove[12] = {};   // the hand drawn under the magazine on the line ...
    float gloveBlend = 0;   // ... blended in by this much from where it is
    float flat = 0;         // the fingers: 0 the carry's, 1 the flat base's ...
    float push = 0;         // ... and over the flat base's, the push home's (0-1)
    bool holding = false;   // a magazine in the hand or on the line under it (the hand is busy)
    float s = 0;            // the magazine's top along the line from home (m; the mouth at -0.1108)
    // Events this frame.
    bool took = false, flatOn = false, flatOff = false, caught = false, offLine = false, parked = false, pushed = false,
         seated = false, letGo = false, putBack = false, cancelled = false;
    float dropVelocity[3] = {};  // let go of: the hand's speed (room, m/s, at most 3)
    // A try on the grip's line over (seated, parked, pulled off it, or gone): the nearest the palm held its top to home
    // for two frames running (m along the line: 0 home, below 0 short of it; the snap goes by the same), and how far
    // apart the hands' grip points were then.
    bool tried = false;
    float closest = 0.0f, apartThen = 0.0f;
    const char* note = "";  // a squeeze at the pouch that took nothing: why
};

// Once a frame (hands.cpp, after pouch::Update).
Result Update(const Frame& f);
// A magazine in the hand, on its way in, or parked in the well.
bool Holding();
bool Parked();
// Gone without a fall (free hands off, the pistol dropped for good, X with it parked: the caller drops it).
void Clear();

// The carry, the magazine in the hand away from the grip: where it sits on the palm (pistolposes.h's convention: gun
// space with it home -> the arms' bind space), or nullptr for the flat base's own place (C2, the closed carry: the
// default). pistol.txt "carry" (hands.cpp, which has the fingers).
void SetCarry(const float* hold);
// pistol.txt "seat 2": the snap, in metres (at least 5 mm; at most 10 cm, a centimetre into the grip).
void SetSnap(float metres);
// The hold used to draw a magazine at magPose (tests): the glove that holds it so, with the carry or the flat base.
void GloveFor(const float* magPose, bool flat, float* glove);
// A magazine with its top s along the grip's line from home, turned that many degrees about the gun's x through its top
// (gun space home -> gun space; tests: the scripted hands hold it there with GloveFor).
void PlaceOnLine(float s, float turnDegrees, float* inGun);
// The seat (the magazine's bind space -> gun space): skin.cpp takes it from the game's own draw (the recording's
// until then: skin::PistolMagazineSeat).
void SetSeat(const float* seat);

}
