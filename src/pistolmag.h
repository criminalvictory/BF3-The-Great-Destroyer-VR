#pragma once

#include <openxr/openxr.h>

namespace pistolmag {

// The train pistol's magazine (The Great Destroyer's M9) from the pouch at the pelvis into the grip, by the left hand
// (docs\research\pistol-manual-reload.md; hands.cpp, build\pistol.txt "mag 1"). Its own, not the M416's pouch.cpp,
// which stays as it is: this only takes pouch.cpp's events (a fresh squeeze reaching the pelvis, the hand at the
// pouch), so the zone, the squeeze's rules and the body's heading are the same for both guns:
//   - the well is raked: the magazine goes in along its own y, 16 degrees back from the gun's vertical, and the whole
//     magazine goes in (its floorplate ends flush with the grip's bottom: the mouth is 11.08 cm below home);
//   - the hand carries it in the carry's own place and fingers (G2, the index along its front: the default; C2, closed
//     round its base, the flat base's own place; the game's pinch), never the flat base before it's caught, so
//     nothing in the hand changes on the way to the grip;
//   - it's caught magnetically (the first headset test: "it takes a couple of attempts"; the user: from any direction,
//     the hand to the gun or the gun onto the hand): its top within the reach of the grip's mouth or already in the
//     grip, its length within the angle of the grip's line (SetCatch), unless it's plainly going away or passing fast;
//   - caught, it glides onto the line (an eased move over the glide's time: SetGlide), its top 1.5 cm into the grip
//     where the well holds it, and the hand drawn holding it goes with it, going over from the carry onto the flat base
//     under its floorplate (the game's 2.390 s) as it does, the fingers first, the place turning about the floorplate
//     (G2's 52 degrees: the hand drawn turns round the magazine, never the magazine in the hand);
//   - on the line the hand goes on as it was at the catch: its push takes the magazine up the line from there (only
//     up: the well holds it); the hand leaving it leaves it there ("parked": the palm pushes it on later, X drops it,
//     hands.cpp); let go of after the catch, it goes on in all the same; let go of in the hand, it falls;
//   - the hands can't close up under the grip as the game's do (the controllers can't overlap: the review), so once the
//     push has held its top within the snap distance of home for two frames running (SetSnap), or the hand has held it
//     there gripped a moment (SetDwell), it goes home by itself, eased over the glide's time, the hand seen or not;
//     each try on the line says how near the push brought it, to set that from;
//   - over its last 3 cm the hand goes over to the game's own push home (2.421 s: the palm centred under the
//     floorplate, its fingers too: Result::push), and after the seat (or a park, a fall) it eases back to the
//     controller over the glide's time.
// Gun space: the pistol frame bone's (3) bind space (x the gun's left, y up, z to the muzzle). View space: the head's
// (hands.cpp's). The arms' bind space: the bare arms' (and the gloves') rest pose. Pure: no game calls and no logs
// (Result::note and missWhy say why nothing happened), so tools\pistolmagtest.cpp runs it on scripted paths.

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
    float glove[12] = {};   // the hand drawn holding the magazine (gliding in, on the line, gliding back out), or
    float gloveBlend = 0;   // easing back to the controller after it's gone: blended in by this much from where it is
    float fingers = 0;      // the fingers on the magazine's poses this much (1 holding it, easing in and out with the
    float flat = 0;         // hand): the carry's (0) or the flat base's (1) ...
    float push = 0;         // ... and over the flat base's, the push home's (0-1)
    bool holding = false;   // a magazine in the hand or on the line under it (the hand is busy)
    float s = 0;            // the magazine's top along the line from home (m; the mouth at -0.1108)
    // Events this frame.
    bool took = false, caught = false, offLine = false, parked = false, pushed = false, seated = false, letGo = false,
         putBack = false, cancelled = false;
    // Caught: how far its top was from the grip's mouth (m) and its length turned from the grip's line (degrees).
    float caughtFrom = 0.0f, caughtTurned = 0.0f;
    // Seated: it went home by itself after the hand held it there (the dwell), not pushed within the snap.
    bool dwelt = false;
    // A near miss: its top in reach without being caught, told once it's out of reach again (or out of the hand): the
    // nearest it came to the grip's mouth (m), its length's turn from the line then (degrees) and why it wasn't caught.
    bool missed = false;
    float missFrom = 0.0f, missTurned = 0.0f;
    const char* missWhy = "";
    float dropVelocity[3] = {};  // let go of: the hand's speed (room, m/s, at most 3)
    // A try on the grip's line over (seated, parked, pulled off it, or gone): the nearest the push held its top to home
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
// Gone without a fall (free hands off, the pistol dropped for good, X with it parked: the caller drops it). A hand
// drawn holding it eases back to the controller from where it was last drawn.
void Clear();

// The carry, the magazine in the hand away from the grip: where it sits on the palm (pistolposes.h's convention: gun
// space with it home -> the arms' bind space), or nullptr for the flat base's own place (C2, the closed carry). guide:
// a carry with a place of its own whose fingers reach up the grip's sides (G2's index along its front:
// pistolposes::kGuideG2): this far past the mouth (m) the magazine waits while the hand drawn on it is still going over
// to the flat base (0: never in the way). pistol.txt "carry" (hands.cpp, which has the fingers).
void SetCarry(const float* hold, float guide = 0.0f);
// pistol.txt "catch 7.5 55": the catch's reach from the grip's mouth (m: 1-15 cm) and its angle from the grip's line
// (degrees: 5-80).
void SetCatch(float metres, float degrees);
// pistol.txt "glide 0.15": how long each eased move takes (s: 0.05-0.5): the glide onto the line, the snap home, the
// hand drawn going over to the flat base and easing back to the controller.
void SetGlide(float seconds);
// pistol.txt "seat 6.5": the snap, in metres (at least 5 mm; at most 10 cm, a centimetre into the grip).
void SetSnap(float metres);
// pistol.txt "dwell 0.4": held on the line this long with the grip squeezed and the hand not drawing back, it goes home
// by itself (s: 0.1-3; 0 never).
void SetDwell(float seconds);
// The tunables as in use (as the Set functions keep them: hands.cpp logs these).
struct Tuning {
    float reach = 0.0f, angle = 0.0f;  // the catch: m, degrees
    float glide = 0.0f;                // s
    float snap = 0.0f;                 // m
    float dwell = 0.0f;                // s (0: never by itself)
};
Tuning Settings();
// The hold used to draw a magazine at magPose (tests, loadtest): the glove that holds it so, with the carry's hold or
// the flat base's (an open palm under it).
void GloveFor(const float* magPose, bool flat, float* glove);
// A magazine with its top s along the grip's line from home, turned that many degrees about the gun's x through its top
// (gun space home -> gun space; tests: the scripted hands hold it there with GloveFor).
void PlaceOnLine(float s, float turnDegrees, float* inGun);
// The seat (the magazine's bind space -> gun space): skin.cpp takes it from the game's own draw (the recording's
// until then: skin::PistolMagazineSeat).
void SetSeat(const float* seat);

}
