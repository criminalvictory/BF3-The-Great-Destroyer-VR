// The train pistol's slide worked by the left hand (src/pistolslide.cpp, as hands.cpp runs it), on scripted frames at
// 72 Hz, without the game. The pistol is held still in the view (the head's space: x right, y up, z back), turned off
// its axes (three poses: square, 25/-12/8 degrees and 170/30/-40, yaw/pitch/roll), and the left hand as its controller
// holds it is put in the pistol's gun space (its frame bone's bind space: x the gun's left, y up, z to the muzzle),
// turned as one of the slide's grips has it (S1 overhand unless said: pistolposes.h kSlidePalms) with its palm centre
// at a point there, and carried into the view by that pose, unless a check holds it still in the view instead. The
// hand as drawn is the controller's blended with the one the module draws on the slide by its share, about the palm
// centre (the palm goes straight), as hands.cpp's OnTheSlide does it. pistolslide's numbers (its Profile): the three
// grips' holds on the slide's rear (pistolposes.h kSlideGrips), grabbed within 13 cm by a fresh press of the left
// trigger (past 0.6 after it was last below 0.4), or by one made up to 0.3 s before the hand gets there with the
// trigger held, in the grip the hand is turned for (waiting for its hold while the hand comes on to it); the hand drawn
// gliding on and back off over 0.12 s (longer for a turn past 90 degrees); a hand still coming in as it takes hold not
// pulling it until it's there or stops (2 mm in 0.1 s); 4.57 cm of travel; let go past 4.0 cm it racks; the slide stop
// at 4.1 cm, a tug of 4 mm off it; the hand sliding forward on it at most 3 mm a frame; home in 70 ms from fully back;
// the trigger's rack back in 30 ms; held through 0.2 s of a lost palm.
//   1  the grips: each palm placement is rigid and puts the bare palm centre (hands.cpp kLeftPalmCentre) on its hold
//      within 0.5 mm, its hand's directions are kSlideGrips' within half a degree; S1's hold is the one there was
//   2  the reach (13 cm; it was 7 from S1's hold): a press on each grip's hold grabs it (0 mm); 12.9 cm off grabs,
//      13.1 doesn't (pressed, its distance told), two ways out of each grip's hold that keep the others out of reach,
//      in three poses
//   3  fresh presses: a trigger held past the press's wait as the hand comes to the slide, or as the pistol comes into
//      hand, grabs nothing; let go and pressed again, it grabs; a trigger wavering 0.45-0.75 makes one press, and
//      holding on through it doesn't let go; a press with no palm or a broken one says so and grabs nothing; with no
//      pistol nothing's told
//   4  the ride: the slide follows the hand back along the frame's -z exactly, stops at 4.57 cm with the hand going on,
//      doesn't move as the hand goes across or up, nor forward of home; the pistol and the hand moving together don't
//      move it; the pistol pushed forward under a hand still in the view pulls it as far
//   5  let go: fully back it racks and is home (the game's own drawn) 5 frames after the let go's (83 ms in all), never
//      going back on the way; 3.95 cm doesn't rack, 4.05 does; a 2 cm pull is home 2 frames after
//   6  caught springing home: pressed again a frame after the let go, it's held where it was
//   7  the slide stop: drawn there; grabbed there; never forward of it while held; pushed 3 cm forward and tugged 3 mm
//      back from there, back onto it; a 4.5 mm tug racks and, the stop dropped, it springs home from it (63 ms), not in
//      one jump
//   8  the trigger's rack (Release): from home back to 4.57 cm and home again within 10 frames; from the stop, home
//   9  lost: the hand lost a frame while held keeps it where it was, and found again it rides on with the hand; lost
//      for good it lets go 0.2 s on (racked if it was back), at once if the trigger's let go; the pistol out of hand
//      lets go at once (nothing drawn, not racked); neither grabs again until the trigger's let go and pressed; the
//      pistol out of hand while the hand eases back off (or glides on), a frame or 10 s: back in hand the hand is off
//      the slide and stays off; 400 random sessions with the pistol out a frame now and then: no ease off rising
//  10  frame times of 0, 1 s, -1 and not a number, and hands not numbers: nothing drawn past its travel or not a number
//  11  the first headset test's misses (71-99 mm from S1's hold, out of its 7 cm then): every one grabbed now, 14 ways
//      round the hold in three poses, overhand with the hand turned for it and in some grip turned the other two ways
//      (in reach only of another grip, once the settle's 0.1 s shows the hand isn't coming on to its own); the log's
//      101-125 mm presses too; its 131-142 once the hand comes 2 cm nearer while the press waits
//  12  each grip from its own hand: on its hold, at the three holds' middle, turned 15 degrees off any way; drawn
//      exactly in it once glided on; the grip it had last kept for a hand turned between two, a little
//  13  no flip mid-pull: the hand turned from S1's to S2's and on to S3's as it pulls: S1 throughout, and as it eases
//      off
//  14  the glide: from 12.5 cm off onto the hold in the glide's time with no step beyond the eased move's, then riding
//      the slide back with no jump; let go, back to the controller the same way while the slide springs home as before;
//      grabbed again a frame later, on from where it's drawn; the hand turned the other way round: the glide longer in
//      proportion, the wrist no faster than a quarter turn in 0.12 s, and the ease back off the same; the hand moving
//  15  a press before the hand gets there: it waits 0.3 s, the trigger held; the hand coming in grabs it as it comes
//      within reach, once; too late, the trigger let go, the pistol out of hand: missed, told with the nearest it came;
//      "wait 0": missed at the press; a press with no palm doesn't wait
//  16  the hand still coming in as it takes hold, no pull meant: in from behind, the slide waits at home and comes back
//      as soon as the hand does; the review's two scratch runs, from the side, behind, in front and the diagonals,
//      pressed in reach or 20 cm out, at home and on the stop, in all three grips: taken in its own grip, never racked
//      nor off the stop, 2 mm back at the most; then a pull meant after all: after a stop on the hold or 3 cm in front
//      of it, swept on through, straight back from behind: the slide with the hand, racked; an overshoot settling back:
//      no pull
//  17  the tunables kept to their ranges; a 7 cm reach and a 0.3 s glide doing what they say
//  18  tracking jumps: held at home or on the stop, the palm a frame 2-10 cm forward and back: 3 mm back at the most,
//      never racked; gone forward and stayed, it slides on all the same
//  19  why a press missed: the palm not known for most of the wait, told so with how long; pressed while the hand eases
//      back off another grip, told so and from the hold it could take; let go while it waited for the grip the hand is
//      turned for, told so, not "too far"; the pistol out of hand: no pistol to work
// Usage: pistolslidetest [-v]   (-v prints every check)
// The exit code is the number of failed checks.

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

#include "../src/pistolposes.h"
#include "../src/pistolslide.h"

namespace {

constexpr float kDt = 1.0f / 72.0f;
constexpr float kPi = 3.14159265f;
constexpr float kLeftPalmCentre[3] = {0.5182f, 1.1075f, 0.1756f};  // hands.cpp's: the bare left palm's vertex middle
// handpose.cpp's: out of the palm and along the fingers in the arms' bind space.
constexpr float kPalmOut[3] = {-0.639f, -0.748f, -0.179f};
constexpr float kAlong[3] = {0.437f, -0.544f, 0.716f};
const pistolslide::Profile kProfile;
const char* const kNames[3] = {"S1 overhand", "S2 the fingers to the left", "S3 the pinch from behind"};
const float* GripHold(int grip) {
    return pistolposes::kSlideGrips[grip].hold;
}

bool g_verbose = false;
int g_checks = 0, g_failures = 0;

void Check(bool ok, const char* fmt, ...) {
    ++g_checks;
    if (ok && !g_verbose)
        return;
    char text[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    if (!ok)
        ++g_failures;
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", text);
}

// ---- Rigid poses (3x4, rows: rotation | translation) ----
void Compose(const float* a, const float* b, float* out) {
    float r[12];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j];
        r[i * 4 + 3] += a[i * 4 + 3];
    }
    std::memcpy(out, r, sizeof(r));
}
void Transform(const float* m, const float* p, float* out) {
    float r[3];
    for (int i = 0; i < 3; ++i)
        r[i] = m[i * 4] * p[0] + m[i * 4 + 1] * p[1] + m[i * 4 + 2] * p[2] + m[i * 4 + 3];
    std::memcpy(out, r, sizeof(r));
}
void Turn(const float* m, const float* v, float* out) {  // a direction (the rotation only)
    float r[3];
    for (int i = 0; i < 3; ++i)
        r[i] = m[i * 4] * v[0] + m[i * 4 + 1] * v[1] + m[i * 4 + 2] * v[2];
    std::memcpy(out, r, sizeof(r));
}
void ToLocal(const float* m, const float* p, float* out) {  // the inverse of a rigid m on a point
    const float d[3] = {p[0] - m[3], p[1] - m[7], p[2] - m[11]};
    for (int i = 0; i < 3; ++i)
        out[i] = m[i] * d[0] + m[4 + i] * d[1] + m[8 + i] * d[2];
}
void Invert(const float* m, float* out) {
    float r[12];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i * 4 + j] = m[j * 4 + i];
    for (int i = 0; i < 3; ++i)
        r[i * 4 + 3] = -(r[i * 4] * m[3] + r[i * 4 + 1] * m[7] + r[i * 4 + 2] * m[11]);
    std::memcpy(out, r, sizeof(r));
}
void Rotation(float yawDeg, float pitchDeg, float rollDeg, float* m) {  // about y, then x, then z
    const float k = kPi / 180.0f, cy = std::cos(yawDeg * k), sy = std::sin(yawDeg * k), cp = std::cos(pitchDeg * k),
                sp = std::sin(pitchDeg * k), cr = std::cos(rollDeg * k), sr = std::sin(rollDeg * k);
    const float Y[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy}, X[9] = {1, 0, 0, 0, cp, -sp, 0, sp, cp},
                Z[9] = {cr, -sr, 0, sr, cr, 0, 0, 0, 1};
    float t[9], r[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            t[i * 3 + j] = Y[i * 3] * X[j] + Y[i * 3 + 1] * X[3 + j] + Y[i * 3 + 2] * X[6 + j];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i * 3 + j] = t[i * 3] * Z[j] + t[i * 3 + 1] * Z[3 + j] + t[i * 3 + 2] * Z[6 + j];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            m[i * 4 + j] = r[i * 3 + j];
        m[i * 4 + 3] = 0.0f;
    }
}
float Dot(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
float Distance(const float* a, const float* b) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}
float Degrees(float radians) {
    return radians * 180.0f / kPi;
}
float AngleBetween(const float* a, const float* b) {  // how far b's turn is from a's (degrees)
    float trace = 0.0f;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            trace += a[i * 4 + j] * b[i * 4 + j];
    return Degrees(std::acos(std::fmin(std::fmax((trace - 1.0f) * 0.5f, -1.0f), 1.0f)));
}
bool Finite(const float* m, int n = 12) {
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(m[i]))
            return false;
    return true;
}
// A rotation | translation's rotation made exactly orthonormal (pistolposes.h's are written to 5 decimals).
void Tidy(float* m) {
    float* r0 = m;
    float* r1 = m + 4;
    float* r2 = m + 8;
    auto normalize = [](float* v) {
        const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        v[0] /= n, v[1] /= n, v[2] /= n;
    };
    normalize(r0);
    const float d = Dot(r1, r0);
    for (int i = 0; i < 3; ++i)
        r1[i] -= d * r0[i];
    normalize(r1);
    r2[0] = r0[1] * r1[2] - r0[2] * r1[1];
    r2[1] = r0[2] * r1[0] - r0[0] * r1[2];
    r2[2] = r0[0] * r1[1] - r0[1] * r1[0];
}
void ToQuat(const float* m, float* q) {
    const float trace = m[0] + m[5] + m[10];
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q[0] = (m[9] - m[6]) / s, q[1] = (m[2] - m[8]) / s, q[2] = (m[4] - m[1]) / s, q[3] = 0.25f * s;
    } else if (m[0] > m[5] && m[0] > m[10]) {
        const float s = std::sqrt(1.0f + m[0] - m[5] - m[10]) * 2.0f;
        q[0] = 0.25f * s, q[1] = (m[1] + m[4]) / s, q[2] = (m[2] + m[8]) / s, q[3] = (m[9] - m[6]) / s;
    } else if (m[5] > m[10]) {
        const float s = std::sqrt(1.0f + m[5] - m[0] - m[10]) * 2.0f;
        q[0] = (m[1] + m[4]) / s, q[1] = 0.25f * s, q[2] = (m[6] + m[9]) / s, q[3] = (m[2] - m[8]) / s;
    } else {
        const float s = std::sqrt(1.0f + m[10] - m[0] - m[5]) * 2.0f;
        q[0] = (m[2] + m[8]) / s, q[1] = (m[6] + m[9]) / s, q[2] = 0.25f * s, q[3] = (m[4] - m[1]) / s;
    }
}
void FromQuat(const float* q, float* m) {
    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    const float x = q[0] / n, y = q[1] / n, z = q[2] / n, w = q[3] / n;
    const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                        2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                        2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            m[i * 4 + j] = r[i * 3 + j];
}
// hands.cpp's BlendAbout for rigid poses: p (a point of the source space) goes straight from where a puts it to where b
// does, the turn between them blends (the quaternions' straight blend, made unit).
void BlendAbout(const float* a, const float* b, float s, const float* p, float* out) {
    float qa[4], qb[4], q[4], r[12], pa[3], pb[3], turned[3];
    ToQuat(a, qa), ToQuat(b, qb);
    if (Dot(qa, qb) + qa[3] * qb[3] < 0.0f)
        for (float& v : qb)
            v = -v;
    for (int i = 0; i < 4; ++i)
        q[i] = qa[i] + (qb[i] - qa[i]) * s;
    FromQuat(q, r);
    Transform(a, p, pa);
    Transform(b, p, pb);
    Turn(r, p, turned);
    for (int i = 0; i < 3; ++i)
        r[i * 4 + 3] = pa[i] + (pb[i] - pa[i]) * s - turned[i];
    std::memcpy(out, r, sizeof(r));
}
// A turn part way from a's to b's (the rotations only; slerp), into out's rotation.
void TurnBetween(const float* a, const float* b, float s, float* out) {
    float qa[4], qb[4], q[4];
    ToQuat(a, qa), ToQuat(b, qb);
    float d = Dot(qa, qb) + qa[3] * qb[3];
    if (d < 0.0f) {
        for (float& v : qb)
            v = -v;
        d = -d;
    }
    const float angle = std::acos(std::fmin(d, 1.0f));
    const float wa = angle > 1e-5f ? std::sin((1.0f - s) * angle) / std::sin(angle) : 1.0f - s;
    const float wb = angle > 1e-5f ? std::sin(s * angle) / std::sin(angle) : s;
    for (int i = 0; i < 4; ++i)
        q[i] = qa[i] * wa + qb[i] * wb;
    FromQuat(q, out);
}
// Each grip's palm placement (the arms' bind space -> gun space with the slide closed), made rigid as the module does.
float g_palms[3][12];
void PreparePalms() {
    for (int g = 0; g < 3; ++g) {
        std::memcpy(g_palms[g], pistolposes::kSlidePalms[g], sizeof(g_palms[g]));
        Tidy(g_palms[g]);
    }
}

// The pistol held in the view, the left hand, the slide, and what it said and drew.
struct Rig {
    pistolslide::Slide slide;
    float pose[12] = {};
    float turn[12] = {};      // the hand's turn in gun space (its rotation: the arms' bind space -> gun space)
    float handPose[12] = {};  // the left hand as its controller holds it (the arms' bind space -> the view)
    bool hand = true, pistol = true, locked = false;
    float trigger = 0.0f;
    pistolslide::Output o;
    int presses = 0, grabs = 0, letGos = 0, racks = 0, misses = 0;
    float palmFrom = 0.0f, pulled = 0.0f;
    pistolslide::Output lastGrab, lastMiss;
    bool sane = true;  // the slide never drawn past its travel, nor as not a number; the hand drawn a number
    // The hand as drawn (hands.cpp's OnTheSlide): the controller's blended with the one on the slide by its share.
    // Since Watch(): the most its palm centre moved in a frame and its turn turned, and how often the grip changed
    // while the hand was drawn on the slide.
    float drawn[12] = {};
    bool drawnBefore = false;
    float palmStep = 0.0f, turnStep = 0.0f, firstStep = -1.0f;
    int flips = 0;

    explicit Rig(int which = 1, int grip = 0) {
        const float turns[3][3] = {{0, 0, 0}, {25, -12, 8}, {170, 30, -40}};
        Rotation(turns[which][0], turns[which][1], turns[which][2], pose);
        pose[3] = 0.16f, pose[7] = -0.22f, pose[11] = -0.38f;
        TurnAs(grip);
        HoldAt(grip);
        Steps(3);
    }
    // The hand turned as a grip has it, or part way from one grip's turn to another's.
    void TurnAs(int grip) {
        std::memcpy(turn, g_palms[grip], sizeof(turn));
    }
    void TurnBetweenGrips(int a, int b, float s) {
        TurnBetween(g_palms[a], g_palms[b], s, turn);
    }
    // The palm centre at a point of the gun's space, the hand turned as turn has it.
    void PalmAt(const float* gun) {
        float inGun[12], centre[3];
        std::memcpy(inGun, turn, sizeof(inGun));
        Turn(turn, kLeftPalmCentre, centre);
        for (int i = 0; i < 3; ++i)
            inGun[i * 4 + 3] = gun[i] - centre[i];
        Compose(pose, inGun, handPose);
    }
    // ... on a grip's hold on the slide's rear, that far back along the frame's -z and off it (the gun's x, y, z).
    void HoldAt(int grip, float back = 0.0f, float x = 0.0f, float y = 0.0f, float z = 0.0f) {
        const float* h = GripHold(grip);
        const float at[3] = {h[0] + x, h[1] + y, h[2] - back + z};
        PalmAt(at);
    }
    // ... on S1's (the overhand hold, the one there was).
    void Hold(float back = 0.0f, float x = 0.0f, float y = 0.0f) {
        HoldAt(0, back, x, y);
    }
    // The palm centre in gun space, as the hand is now.
    void PalmInGun(float* out) const {
        float palm[3];
        Transform(handPose, kLeftPalmCentre, palm);
        ToLocal(pose, palm, out);
    }
    void Watch() {
        palmStep = turnStep = 0.0f;
        firstStep = -1.0f;
        flips = 0;
    }
    void Step(float dt = kDt) {
        pistolslide::Input in;
        in.dt = dt;
        in.pistolPose = pistol ? pose : nullptr;
        in.hand = hand ? handPose : nullptr;
        in.trigger = trigger;
        in.locked = locked;
        const int gripBefore = o.grip;
        const float shareBefore = o.share;
        o = slide.Update(in);
        presses += o.pressed, grabs += o.grabbed, letGos += o.letGo, racks += o.racked, misses += o.missed;
        if (o.pressed)
            palmFrom = o.palmFrom;
        if (o.grabbed)
            lastGrab = o;
        if (o.missed)
            lastMiss = o;
        if (o.letGo)
            pulled = o.pulled;
        if (!(o.back <= kProfile.travel + 1e-6f) || !(o.share >= 0.0f && o.share <= 1.0f) ||
            (o.share > 0.0f && !Finite(o.hand)) || o.grip < 0 || o.grip > 2)  // (false for not a number too)
            sane = false;
        if (shareBefore > 0.0f && o.share > 0.0f && o.grip != gripBefore)
            ++flips;
        // The hand as drawn, as hands.cpp blends it (only with the left hand tracked: otherwise none is drawn).
        if (hand && Finite(handPose)) {
            float now[12], palmNow[3], palmThen[3];
            if (o.share > 0.0f)
                BlendAbout(handPose, o.hand, o.share, kLeftPalmCentre, now);
            else
                std::memcpy(now, handPose, sizeof(now));
            if (drawnBefore) {
                Transform(now, kLeftPalmCentre, palmNow);
                Transform(drawn, kLeftPalmCentre, palmThen);
                const float step = Distance(palmNow, palmThen);
                palmStep = std::fmax(palmStep, step);
                turnStep = std::fmax(turnStep, AngleBetween(drawn, now));
                if (firstStep < 0.0f)
                    firstStep = step;
            }
            std::memcpy(drawn, now, sizeof(drawn));
            drawnBefore = true;
        } else {
            drawnBefore = false;
        }
        if (g_verbose)
            std::printf("      back %+.4f held %d share %.3f grip %d%s%s%s%s\n", o.back, o.held, o.share, o.grip,
                        o.pressed ? " pressed" : "", o.grabbed ? " grabbed" : "", o.missed ? " missed" : "",
                        o.letGo ? (o.racked ? " let go, racked" : " let go") : "");
    }
    void Steps(int n) {
        for (int i = 0; i < n; ++i)
            Step();
    }
    void Press() {
        trigger = 1.0f;
        Step();
    }
    void Release() {
        trigger = 0.0f;
        Step();
    }
    // Held: the hand drawn back from where it is to back (from the hold it took) over n frames.
    void PullTo(float back, int n = 12, int grip = 0) {
        float from[3];
        PalmInGun(from);
        for (int i = 1; i <= n; ++i) {
            const float at[3] = {from[0], from[1], from[2] + (GripHold(grip)[2] - back - from[2]) * i / n};
            PalmAt(at);
            Step();
        }
    }
    // Frames until the slide is home (the game's own drawn), at most n; it never goes back on the way.
    int FramesHome(int n, bool& monotonic) {
        monotonic = true;
        float last = o.back;
        for (int i = 1; i <= n; ++i) {
            if (o.back < 0.0f)
                return i - 1;
            Step();
            if (o.back >= 0.0f && o.back > last + 1e-6f)
                monotonic = false;
            last = o.back;
        }
        return o.back < 0.0f ? n : n + 1;
    }
    // The drawn hand's palm centre in gun space.
    void DrawnPalm(float* out) const {
        float palm[3];
        Transform(drawn, kLeftPalmCentre, palm);
        ToLocal(pose, palm, out);
    }
    // Frames until the hand is all on the slide (share 1), at most n.
    int FramesOn(int n) {
        for (int i = 1; i <= n; ++i) {
            Step();
            if (o.share >= 1.0f)
                return i;
        }
        return n + 1;
    }
};

void Grips() {
    std::printf("1 the grips\n");
    // The one hold there was (pistolslide.h's Profile::grip before the three grips).
    const float overhand[3] = {0.0123f, 0.0959f, 0.0551f};
    Check(Distance(GripHold(0), overhand) < 1e-4f, "S1's hold is the overhand hold there was, (%.4f %.4f %.4f)",
          GripHold(0)[0], GripHold(0)[1], GripHold(0)[2]);
    for (int g = 0; g < 3; ++g) {
        const float* m = pistolposes::kSlidePalms[g];
        float most = 0.0f;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                const float dot = m[i * 4] * m[j * 4] + m[i * 4 + 1] * m[j * 4 + 1] + m[i * 4 + 2] * m[j * 4 + 2];
                most = std::fmax(most, std::fabs(dot - (i == j ? 1.0f : 0.0f)));
            }
        const float det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
                          m[2] * (m[4] * m[9] - m[5] * m[8]);
        Check(most < 1e-3f && std::fabs(det - 1.0f) < 1e-3f,
              "%s: its palm placement is rigid (rows %.5f off orthonormal, det %.5f)", kNames[g], most, det);
        float palm[3], out[3], along[3];
        Transform(g_palms[g], kLeftPalmCentre, palm);
        Turn(g_palms[g], kPalmOut, out);
        Turn(g_palms[g], kAlong, along);
        for (float* v : {out, along}) {  // (handpose.cpp's are 0.9998 long, kSlideGrips' unit)
            const float n = std::sqrt(Dot(v, v));
            v[0] /= n, v[1] /= n, v[2] /= n;
        }
        const pistolposes::SlideGrip& grip = pistolposes::kSlideGrips[g];
        const float off = Distance(palm, grip.hold);
        const float outOff = Degrees(std::acos(std::fmin(Dot(out, grip.palmOut), 1.0f)));
        const float alongOff = Degrees(std::acos(std::fmin(Dot(along, grip.fingers), 1.0f)));
        Check(off < 0.0005f && outOff < 0.5f && alongOff < 0.5f,
              "%s: puts the bare palm centre %.2f mm from its hold; out of the palm %.2f and along the fingers %.2f "
              "degrees from kSlideGrips'", kNames[g], off * 1000.0f, outOff, alongOff);
    }
}

void Reach() {
    std::printf("2 the reach\n");
    const float reach = kProfile.reach;
    // Out of each grip's hold, two ways that keep the other grips' holds out of reach: along the gun's x away from the
    // slide (S1 to its left, S2 to its right), behind it (S3), and up.
    const float outward[3][2][3] = {{{1, 0, 0}, {0, 1, 0}}, {{-1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {0, 1, 0}}};
    for (int which = 0; which < 3; ++which) {
        for (int g = 0; g < 3; ++g) {
            Rig r(which, g);
            r.Press();
            Check(r.o.grabbed && r.o.held && r.o.grip == g && r.palmFrom < 1e-4f && r.o.back == 0.0f,
                  "pose %d, %s: a press on its hold grabs it (%.1f mm), held at home", which, kNames[g],
                  r.palmFrom * 1000.0f);
            for (const auto& d : outward[g]) {
                for (float at : {reach - 0.001f, reach + 0.001f}) {
                    Rig s(which, g);
                    const float* h = GripHold(g);
                    const float p[3] = {h[0] + d[0] * at, h[1] + d[1] * at, h[2] + d[2] * at};
                    float others = 1.0f;  // the other grips' holds from there: out of reach
                    for (int k = 0; k < 3; ++k)
                        if (k != g)
                            others = std::fmin(others, Distance(p, GripHold(k)));
                    s.PalmAt(p);
                    s.Step();
                    s.Press();
                    const bool want = at < reach;
                    Check(others > reach + 0.005f && s.presses == 1 && s.grabs == (want ? 1 : 0) &&
                              (!want || s.o.grip == g) && std::fabs(s.palmFrom - at) < 1e-4f,
                          "pose %d, %s, %.1f cm off along (%.0f %.0f %.0f): pressed %d, %s (told %.1f mm; the others' "
                          "holds %.1f cm off)",
                          which, kNames[g], at * 100, d[0], d[1], d[2], s.presses, s.grabs ? "grabbed" : "not grabbed",
                          s.palmFrom * 1000.0f, others * 100.0f);
                }
            }
        }
    }
}

void FreshPresses() {
    std::printf("3 fresh presses\n");
    {
        Rig r;
        r.Hold(0.0f, 0.15f);
        r.Press();     // 15 cm off: pressed, not grabbed; it waits 0.3 s for the hand
        r.Steps(36);   // held 0.5 s: past the wait
        r.Hold();
        r.Steps(5);  // brought to the hold with it held
        Check(r.presses == 1 && r.grabs == 0 && !r.o.held && r.misses == 1,
              "a trigger held 0.5 s as the hand comes to the hold (past the press's wait): nothing grabbed, the press "
              "missed once");
        r.Release();
        r.Press();
        Check(r.grabs == 1 && r.o.held, "let go and pressed again on the hold: grabbed");
    }
    {
        Rig r;
        r.pistol = false;
        r.Press();
        Check(r.presses == 0 && r.grabs == 0, "a press with no pistol in hand: nothing told, nothing grabbed");
        r.pistol = true;
        r.Steps(3);
        Check(r.grabs == 0 && !r.o.held, "the pistol into hand with the trigger held: nothing grabbed");
        r.Release();
        r.Press();
        Check(r.grabs == 1, "then let go and pressed: grabbed");
    }
    {
        Rig r;
        r.Hold(0.0f, 0.15f);
        r.Press();
        for (int i = 0; i < 144; ++i) {  // 2 s wavering about 0.6
            r.trigger = 0.6f + 0.15f * std::sin(i * 0.7f);
            r.Step();
        }
        Check(r.presses == 1, "a trigger wavering 0.45-0.75 after a press: %d press%s", r.presses,
              r.presses == 1 ? "" : "es");
        Rig h;
        h.Press();
        for (int i = 0; i < 144; ++i) {
            h.trigger = 0.6f + 0.15f * std::sin(i * 0.7f);
            h.Step();
        }
        Check(h.presses == 1 && h.letGos == 0 && h.o.held, "holding the slide through it: still held, not pressed again");
    }
    {
        Rig r;
        r.hand = false;
        r.Press();
        Check(r.presses == 1 && r.grabs == 0 && r.palmFrom < 0.0f, "a press with no palm: told (no palm), not grabbed");
        Rig n;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        n.handPose[3] = nan;
        n.Press();
        Check(n.presses == 1 && n.grabs == 0 && n.palmFrom < 0.0f, "a press with a palm not a number: told, not grabbed");
    }
}

void Ride() {
    std::printf("4 the ride\n");
    Rig r;
    r.Press();
    float worst = 0.0f;
    for (int i = 1; i <= 60; ++i) {  // 1 mm a frame back to 6 cm
        r.Hold(0.001f * i);
        r.Step();
        worst = std::fmax(worst, std::fabs(r.o.back - std::fmin(0.001f * i, kProfile.travel)));
    }
    Check(r.o.held && worst < 1e-5f && std::fabs(r.o.back - kProfile.travel) < 1e-6f,
          "the slide follows the hand back (%.4f mm off at most) and stops at %.2f cm with the hand at 6", worst * 1000.0f,
          r.o.back * 100.0f);
    Rig a;
    a.Press();
    a.PullTo(0.02f);
    for (int i = 1; i <= 30; ++i) {
        a.Hold(0.02f, 0.001f * i, 0.0015f * i);
        a.Step();
    }
    Check(std::fabs(a.o.back - 0.02f) < 1e-5f, "the hand 3 cm across and 4.5 cm up at 2 cm back: still 2 cm back (%.2f)",
          a.o.back * 100.0f);
    a.PullTo(-0.03f);
    Check(a.o.back == 0.0f && a.o.held, "pushed 3 cm forward of home: at home (%.2f), held", a.o.back * 100.0f);
    // The pistol and the hand moving together (a swing, both hands): the slide doesn't move.
    Rig t;
    t.Press();
    float moved = 0.0f;
    for (int i = 1; i <= 72; ++i) {
        Rotation(25.0f + 0.5f * i, -12.0f + 0.3f * i, 8.0f, t.pose);
        t.pose[3] = 0.16f + 0.003f * i, t.pose[7] = -0.22f + 0.002f * i, t.pose[11] = -0.38f;
        t.Hold();
        t.Step();
        moved = std::fmax(moved, std::fabs(t.o.back));
    }
    Check(moved < 1e-5f, "the pistol and the hand swung together 36 degrees and 25 cm: the slide still home (%.4f mm)",
          moved * 1000.0f);
    // The pistol pushed forward under the hand, the hand still in the view.
    Rig f;
    f.Press();
    for (int i = 1; i <= 30; ++i) {
        for (int k = 0; k < 3; ++k)
            f.pose[k * 4 + 3] += f.pose[k * 4 + 2] * 0.001f;  // 1 mm a frame along the gun's own z (to the muzzle)
        f.Step();
    }
    Check(std::fabs(f.o.back - 0.03f) < 1e-5f, "the pistol pushed 3 cm forward under the hand: 3 cm back (%.3f)",
          f.o.back * 100.0f);
}

void LetGo() {
    std::printf("5 let go\n");
    Rig r;
    r.Press();
    r.PullTo(0.06f);
    r.Release();
    bool monotonic = true;
    const int frames = r.FramesHome(20, monotonic);
    Check(r.letGos == 1 && r.racks == 1 && std::fabs(r.pulled - kProfile.travel) < 1e-6f,
          "let go fully back: racked (pulled %.2f cm)", r.pulled * 100.0f);
    Check(frames <= 5 && monotonic, "home %d frames after the let go's (%.0f ms in all), never going back", frames,
          (frames + 1) * kDt * 1000.0f);
    for (float to : {0.0395f, 0.0405f}) {
        Rig s;
        s.Press();
        s.PullTo(to);
        s.Release();
        Check(s.racks == (to >= kProfile.rackAt ? 1 : 0), "let go %.2f cm back: %s", to * 100.0f,
              s.racks ? "racked" : "not racked");
    }
    Rig s;
    s.Press();
    s.PullTo(0.02f);
    s.Release();
    const int shortFrames = s.FramesHome(20, monotonic);
    Check(s.racks == 0 && shortFrames <= 2 && monotonic, "a 2 cm pull: not racked, home %d frames after", shortFrames);
}

void Caught() {
    std::printf("6 caught springing home\n");
    Rig r;
    r.Press();
    r.PullTo(kProfile.travel);
    r.Release();
    const float at = r.o.back;
    r.Hold(at);  // the hand on its rear where it's drawn
    r.Press();
    Check(r.grabs == 2 && r.o.held && std::fabs(r.o.back - at) < 1e-5f && r.palmFrom < 1e-4f,
          "pressed a frame after the let go on its rear: held where it was (%.2f cm)", r.o.back * 100.0f);
}

void SlideStop() {
    std::printf("7 the slide stop\n");
    Rig r;
    r.locked = true;
    r.Step();
    Check(std::fabs(r.o.back - kProfile.lockAt) < 1e-6f, "locked: drawn on the stop (%.2f cm)", r.o.back * 100.0f);
    r.Hold(kProfile.lockAt);
    r.Press();
    Check(r.grabs == 1 && r.palmFrom < 1e-4f && std::fabs(r.o.back - kProfile.lockAt) < 1e-6f,
          "grabbed on its rear there (%.1f mm from the hold)", r.palmFrom * 1000.0f);
    r.PullTo(kProfile.lockAt - 0.03f);
    Check(std::fabs(r.o.back - kProfile.lockAt) < 1e-6f, "pushed 3 cm forward: still on the stop (%.2f)",
          r.o.back * 100.0f);
    // The hand gone on forward of the stop slides forward on it: a tug is read from where it is now.
    r.PullTo(kProfile.lockAt - 0.03f + 0.003f);
    const float tugged = r.o.back;
    r.Release();
    r.Steps(5);
    Check(r.racks == 0 && std::fabs(tugged - kProfile.lockAt - 0.003f) < 1e-5f &&
              std::fabs(r.o.back - kProfile.lockAt) < 1e-6f,
          "then a 3 mm tug back from there (%.2f mm past the stop) and let go: back on the stop",
          (tugged - kProfile.lockAt) * 1000.0f);
    r.Hold(kProfile.lockAt);
    r.Press();
    r.PullTo(kProfile.lockAt + 0.0045f);
    r.Release();
    Check(r.racks == 1, "a 4.5 mm tug and let go: racked");
    r.locked = false;  // the mechanism drops the stop
    bool monotonic = true;
    const int frames = r.FramesHome(20, monotonic);
    Check(frames >= 3 && frames <= 5 && monotonic, "the stop dropped: sprung home from it in %d frames, not in one jump",
          frames);
}

void TriggerRack() {
    std::printf("8 the trigger's rack\n");
    Rig r;
    r.slide.Release();
    float most = 0.0f;
    int frames = 0;
    for (; frames < 12; ++frames) {
        r.Step();
        most = std::fmax(most, r.o.back);
        if (r.o.back < 0.0f)
            break;
    }
    Check(std::fabs(most - kProfile.travel) < 1e-6f && frames <= 10,
          "from home: back to %.2f cm and home in %d frames", most * 100.0f, frames + 1);
    Rig s;
    s.locked = true;
    s.Step();
    s.slide.Release();
    s.locked = false;
    s.Step();
    bool monotonic = true;
    const int home = s.FramesHome(20, monotonic);
    Check(home <= 5 && monotonic, "from the stop, unlocked: home %d frames later", home + 1);
}

void Lost() {
    std::printf("9 lost\n");
    Rig r;
    r.Press();
    r.PullTo(0.045f);
    r.hand = false;
    r.Step();
    Check(r.letGos == 0 && r.racks == 0 && r.o.held && std::fabs(r.o.back - 0.045f) < 1e-5f,
          "the hand lost a frame 4.5 cm back: still held there (%.2f cm), not racked", r.o.back * 100.0f);
    r.hand = true;
    r.PullTo(0.02f);
    Check(r.letGos == 0 && r.o.held && std::fabs(r.o.back - 0.02f) < 1e-5f,
          "found again: it rides on with the hand (%.2f cm)", r.o.back * 100.0f);
    r.PullTo(0.045f);
    r.hand = false;
    int frames = 0;
    for (; frames < 72 && r.letGos == 0; ++frames)
        r.Step();
    Check(r.letGos == 1 && r.racks == 1 && frames * kDt >= kProfile.coast && (frames - 1) * kDt < kProfile.coast,
          "lost for good 4.5 cm back: let go %d frames on (%.0f ms), racked", frames, frames * kDt * 1000.0f);
    r.hand = true;
    r.Hold();
    r.Steps(10);
    Check(r.grabs == 1, "found again on the hold with the trigger still held: not grabbed again");
    Rig q;
    q.Press();
    q.PullTo(0.045f);
    q.hand = false;
    q.Step();
    q.Release();
    Check(q.letGos == 1 && q.racks == 1 && !q.o.held,
          "the trigger let go a frame after the hand's lost: let go then, racked");
    Rig p;
    p.Press();
    p.PullTo(0.045f);
    p.pistol = false;
    p.Step();
    Check(p.letGos == 1 && p.racks == 0 && p.o.back < 0.0f && p.o.share == 0.0f,
          "the pistol out of hand: let go, not racked, nothing drawn (the hand neither)");
    p.pistol = true;
    p.Hold();
    p.Steps(5);
    Check(p.grabs == 1 && !p.o.held && p.o.back < 0.0f, "back in hand with the trigger held: not grabbed, at home");
    p.Release();
    p.Press();
    Check(p.grabs == 2, "let go and pressed: grabbed");
    // The pistol out of hand while the hand eases back off the slide, or glides on: that ends there, and back in hand
    // the hand is where the controller has it, not back on the slide easing off again (the review: from 2 frames into
    // the ease off it came back 0.72 of the way on, after a frame out of hand or 10 s; gliding on at 0.28, a frame out
    // brought it back at 0.56, the last let go's ease).
    int cases = 0, off = 0;
    for (int after : {2, 5})
        for (int out : {1, 72, 720}) {
            Rig e;
            e.Press();
            e.Steps(36);
            e.Release();
            e.Steps(after - 1);
            const bool easing = !e.o.held && e.o.share > 0.0f && e.o.share < 1.0f;
            e.pistol = false;
            e.Steps(out);
            e.pistol = true;
            bool stays = true;
            for (int i = 0; i < 30; ++i) {
                e.Step();
                stays = stays && e.o.share == 0.0f;
            }
            ++cases;
            off += easing && stays;
        }
    Check(off == cases,
          "let go, 2 or 5 frames into the ease back off, the pistol out of hand 1, 72 or 720 frames: back in hand the "
          "hand is off the slide and stays off (%d of %d)",
          off, cases);
    Rig g;
    g.Press();
    g.Steps(36);
    g.Release();
    g.Steps(20);  // all back off: this let go's ease from all on
    g.HoldAt(0, 0.0f, 0.0f, 0.0f, -0.08f);
    g.Press();
    g.Steps(2);
    const float gliding = g.o.share;
    g.pistol = false;
    g.Step();
    g.pistol = true;
    bool stays = true;
    for (int i = 0; i < 30; ++i) {
        g.Step();
        stays = stays && g.o.share == 0.0f;
    }
    Check(gliding > 0.0f && gliding < 1.0f && stays,
          "gliding on (%.2f of the way), the pistol out of hand a frame: back in hand the hand is off the slide and "
          "stays off",
          gliding);
    // The review's random sessions: hands drifting in to each hold turned for it (or for another, three times in ten),
    // the trigger pressed and let go, the palm lost two frames in a hundred, the pistol out of hand one in a hundred.
    unsigned seed = 12345;
    auto random = [&seed] {
        seed = seed * 1103515245u + 12345u;
        return static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
    };
    int rises = 0, flips = 0, played = 0;
    for (int s = 0; s < 400; ++s) {
        Rig w(s % 3, 0);
        float at[3] = {};
        int grip = 0;
        float shareBefore = w.o.share;
        bool heldBefore = false;
        w.Watch();
        for (int f = 0; f < 72 * 6; ++f, ++played) {
            if (f % 60 == 0) {
                grip = static_cast<int>(random() * 3) % 3;
                for (float& v : at)
                    v = (random() - 0.5f) * 0.3f;
                w.TurnAs(random() > 0.7f ? (grip + 1) % 3 : grip);
            }
            for (float& v : at)
                v *= 0.97f;  // coming in
            w.HoldAt(grip, 0.0f, at[0], at[1], at[2]);
            const int phase = f % 60;
            w.trigger = phase < 5 ? 0.0f : phase < 45 ? 1.0f : 0.0f;
            if (random() < 0.01f)
                w.trigger = 0.45f;  // wavering
            w.hand = random() > 0.02f;
            w.pistol = random() >= 0.01f;
            w.Step();
            rises += !w.o.held && !w.o.grabbed && !heldBefore && w.o.share > shareBefore + 1e-4f;
            shareBefore = w.o.share;
            heldBefore = w.o.held;
        }
        flips += w.flips;
    }
    Check(rises == 0 && flips == 0,
          "400 random sessions (%d frames): %d frames of an ease back off rising (the review's had 295, each just "
          "after the pistol was back in hand), %d grip flips while drawn",
          played, rises, flips);
}

void Degenerate() {
    std::printf("10 frame times and broken hands\n");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Rig r;
    r.Press();
    r.PullTo(0.03f);
    r.Release();
    for (float dt : {0.0f, 1.0f, -1.0f, nan})
        r.Step(dt);
    r.Steps(10);
    r.Hold();
    r.Press();
    r.handPose[7] = nan;  // the palm not a number
    r.Step();
    r.Hold();
    r.Steps(3);
    r.handPose[1] = nan;  // the hand's turn not a number
    r.Step();
    r.Hold();
    r.Steps(3);
    r.Release();
    r.slide.Release();
    for (float dt : {nan, 1.0f, -1.0f, 0.0f})
        r.Step(dt);
    r.Steps(20);
    // A press waiting for the hand through broken frames.
    r.Hold(0.0f, 0.2f);
    r.Press();
    for (float dt : {nan, 0.0f, -1.0f})
        r.Step(dt);
    r.handPose[5] = nan;
    r.Steps(3);
    r.Hold();
    r.Steps(30);
    r.Release();
    r.Steps(20);
    Check(r.sane && r.o.back < 0.0f && r.o.share == 0.0f,
          "never drawn past its travel or not a number, the hand drawn a number; home and the hand off it at the end");
}

// The distinct distances of the first headset test's presses at the slide that were too far (7 cm then), 2026-11-01
// (screenshots\logs\bf3vr_0029_firsttest.log).
constexpr int kMissed[] = {71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84,
                           85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 98, 99};
constexpr int kFurther[] = {101, 102, 103, 105, 106, 107, 111, 112, 115, 118, 120, 123, 125};
constexpr int kFurthest[] = {131, 135, 136, 138, 142};

// 14 ways round a point: the gun's axes both ways and the 8 diagonals.
void Ways(float (*out)[3]) {
    int n = 0;
    for (int axis = 0; axis < 3; ++axis)
        for (float s : {1.0f, -1.0f}) {
            out[n][0] = out[n][1] = out[n][2] = 0.0f;
            out[n++][axis] = s;
        }
    const float k = 1.0f / std::sqrt(3.0f);
    for (float x : {k, -k})
        for (float y : {k, -k})
            for (float z : {k, -k})
                out[n][0] = x, out[n][1] = y, out[n++][2] = z;
}

void FirstTest() {
    std::printf("11 the first headset test's misses\n");
    float ways[14][3];
    Ways(ways);
    const float* h = GripHold(0);
    for (int which = 0; which < 3; ++which) {
        int presses = 0, grabbed = 0, overhand = 0, otherTurns = 0, otherGrabbed = 0, otherLater = 0;
        for (int mm : kMissed)
            for (const auto& d : ways) {
                const float at = mm / 1000.0f, p[3] = {h[0] + d[0] * at, h[1] + d[1] * at, h[2] + d[2] * at};
                Rig r(which, 0);
                r.PalmAt(p);
                r.Step();
                r.Press();
                ++presses;
                grabbed += r.o.grabbed;
                overhand += r.o.grabbed && r.o.grip == 0;
                for (int g = 1; g < 3; ++g) {  // the hand turned the other two ways
                    Rig s(which, g);
                    s.PalmAt(p);
                    s.Step();
                    s.Press();
                    const bool atOnce = s.o.grabbed;
                    // In reach only of a grip it isn't turned for, it waits the settle (0.1 s) to see whether the hand
                    // comes on to its own grip's hold: held still, it takes the one in reach then (this check was "at
                    // once" before the review's fixes).
                    for (int i = 0; i < 12 && s.grabs == 0; ++i)
                        s.Step();
                    ++otherTurns;
                    otherGrabbed += s.grabs == 1 && (atOnce || s.lastGrab.waited <= kProfile.settle + 1.5f * kDt);
                    otherLater += !atOnce;
                }
            }
        Check(grabbed == presses && overhand == presses,
              "pose %d: all %d presses 71-99 mm from S1's hold, 14 ways round it, grabbed at once, %d overhand (the "
              "hand turned for it)",
              which, presses, overhand);
        Check(otherGrabbed == otherTurns && otherLater > 0,
              "pose %d: turned as S2 or S3 there, %d of %d grabbed (in a grip in reach; %d of them, only another's in "
              "reach, once the settle's %.0f ms showed the hand wasn't coming on to its own)",
              which, otherGrabbed, otherTurns, otherLater, kProfile.settle * 1000.0f);
    }
    int presses = 0, grabbed = 0;
    for (int mm : kFurther)
        for (const auto& d : ways) {
            const float at = mm / 1000.0f, p[3] = {h[0] + d[0] * at, h[1] + d[1] * at, h[2] + d[2] * at};
            Rig r(1, 0);
            r.PalmAt(p);
            r.Step();
            r.Press();
            ++presses;
            grabbed += r.o.grabbed;
        }
    Check(grabbed == presses, "the log's 101-125 mm presses, 14 ways round S1's hold: %d of %d grabbed", grabbed,
          presses);
    // 131-142 mm: out of reach at the press only on the far side from the other holds; the hand coming 2 cm nearer over
    // the next 0.1 s while it waits takes it.
    int far = 0, waited = 0, then = 0;
    for (int mm : kFurthest)
        for (const auto& d : ways) {
            const float at = mm / 1000.0f;
            const float p[3] = {h[0] + d[0] * at, h[1] + d[1] * at, h[2] + d[2] * at};
            Rig r(1, 0);
            r.PalmAt(p);
            r.Step();
            r.Press();
            if (r.o.grabbed)
                continue;
            ++far;
            for (int i = 1; i <= 7; ++i) {
                const float nearer = at - 0.02f * i / 7.0f;
                const float q[3] = {h[0] + d[0] * nearer, h[1] + d[1] * nearer, h[2] + d[2] * nearer};
                r.PalmAt(q);
                r.Step();
            }
            waited += r.grabs == 1;
            then += r.grabs == 1 && r.lastGrab.waited > 0.0f && r.lastGrab.waited < kProfile.wait;
        }
    Check(far > 0 && waited == far && then == far,
          "the log's 131-142 mm presses out of every hold's reach (%d): each grabbed as the hand came 2 cm nearer "
          "while the press waited",
          far);
}

// The hand turned as turn has it, its palm centre at p: the grip a fresh press takes there (-1: none), after a grab
// with the grip before (-1: none before).
int PickedAt(const float* turn, const float* p, int before, int which = 1) {
    Rig r(which, 0);
    if (before >= 0) {  // a grab with that grip first, let go, the hand all back off it
        r.TurnAs(before);
        r.HoldAt(before);
        r.Step();
        r.Press();
        r.Release();
        r.Steps(20);
    }
    std::memcpy(r.turn, turn, sizeof(r.turn));
    r.PalmAt(p);
    r.Step();
    r.Press();
    return r.o.grabbed ? r.o.grip : -1;
}

void OwnHands() {
    std::printf("12 each grip from its own hand\n");
    float middle[3] = {};  // the three holds' middle: within reach of all of them
    for (int g = 0; g < 3; ++g)
        for (int i = 0; i < 3; ++i)
            middle[i] += GripHold(g)[i] / 3.0f;
    float most = 0.0f;
    for (int g = 0; g < 3; ++g)
        most = std::fmax(most, Distance(middle, GripHold(g)));
    for (int which = 0; which < 3; ++which)
        for (int g = 0; g < 3; ++g) {
            Rig r(which, g);
            r.Press();
            const bool onHold = r.o.grabbed && r.o.grip == g;
            Rig m(which, g);
            m.PalmAt(middle);
            m.Step();
            m.Press();
            const bool atMiddle = m.o.grabbed && m.o.grip == g;
            int off = 0;
            for (int axis = 0; axis < 3; ++axis)
                for (float deg : {15.0f, -15.0f}) {
                    float about[12], turned[12];
                    Rotation(axis == 1 ? deg : 0, axis == 0 ? deg : 0, axis == 2 ? deg : 0, about);
                    Compose(about, g_palms[g], turned);
                    Rig t(which, g);
                    std::memcpy(t.turn, turned, sizeof(t.turn));
                    t.PalmAt(middle);
                    t.Step();
                    t.Press();
                    off += t.o.grabbed && t.o.grip == g;
                }
            Check(onHold && atMiddle && off == 6,
                  "pose %d, %s: its own hand takes it on its hold, at the holds' middle (%.1f cm from the furthest) "
                  "and turned 15 degrees off about each of the gun's axes (%d of 6)",
                  which, kNames[g], most * 100.0f, off);
        }
    // Once glided on, drawn exactly in its grip.
    for (int g = 0; g < 3; ++g) {
        Rig r(1, g);
        r.HoldAt(g, 0.0f, 0.03f, 0.04f);
        r.Step();
        r.Press();
        r.Steps(12);
        float want[12];
        Compose(r.pose, g_palms[g], want);  // (the slide home: not moved back)
        float worst = 0.0f;
        for (int i = 0; i < 12; ++i)
            worst = std::fmax(worst, std::fabs(want[i] - r.o.hand[i]));
        Check(r.o.share == 1.0f && r.o.grip == g && r.o.back == 0.0f && worst < 1e-4f,
              "%s: drawn in its grip once glided on (the pistol x its palm placement, %.5f off at most)", kNames[g],
              worst);
    }
    // The grip it had last, a little ahead: a hand turned half way between S2's and S3's keeps whichever it had, and
    // where a fresh hand would go over to the other, the one before keeps it a few degrees further.
    float halfway[12];
    TurnBetween(g_palms[1], g_palms[2], 0.5f, halfway);
    Check(PickedAt(halfway, middle, 1) == 1 && PickedAt(halfway, middle, 2) == 2,
          "turned half way between S2's and S3's: S2 after S2, S3 after S3");
    const float apart = AngleBetween(g_palms[1], g_palms[2]);
    // Where it goes over to S3 (degrees from S2's turn): with no grip before, and after S2.
    float fresh = -1.0f, sticky = -1.0f;
    for (int i = 0; i <= 200 && (fresh < 0.0f || sticky < 0.0f); ++i) {
        float turn[12];
        TurnBetween(g_palms[1], g_palms[2], i / 200.0f, turn);
        if (fresh < 0.0f && PickedAt(turn, middle, -1) == 2)
            fresh = apart * i / 200.0f;
        if (sticky < 0.0f && PickedAt(turn, middle, 1) == 2)
            sticky = apart * i / 200.0f;
    }
    Check(fresh > 0.0f && sticky - fresh > 3.0f && sticky - fresh < 10.0f,
          "turned from S2's towards S3's (%.0f degrees apart): a fresh hand goes over to S3 at %.1f degrees, after S2 "
          "at %.1f (kept %.1f degrees further: a little)",
          apart, fresh, sticky, sticky - fresh);
}

void NoFlip() {
    std::printf("13 no flip mid-pull\n");
    Rig r(1, 0);
    r.Press();
    r.Watch();
    int wrong = 0;
    for (int i = 1; i <= 40; ++i) {  // pulled back to 4 cm, the hand turning to S2's and on to S3's on the way
        if (i <= 20)
            r.TurnBetweenGrips(0, 1, i / 20.0f);
        else
            r.TurnBetweenGrips(1, 2, (i - 20) / 20.0f);
        r.Hold(0.04f * i / 40.0f);
        r.Step();
        wrong += r.o.grip != 0;
    }
    Check(r.o.held && wrong == 0 && r.flips == 0,
          "pulled 4 cm with the hand turning from S1's to S2's and on to S3's: S1 every frame (%d not), no flip",
          wrong);
    r.Release();
    int easing = 0;
    for (int i = 0; i < 20; ++i) {
        r.Step();
        easing += r.o.share > 0.0f;
        wrong += r.o.grip != 0 && r.o.share > 0.0f;
    }
    Check(wrong == 0 && r.flips == 0 && easing > 0 && r.o.share == 0.0f,
          "let go: S1 while it eases back off (%d frames), then the hand off it", easing);
    r.TurnAs(2);
    r.HoldAt(2);
    r.Step();
    r.Press();
    Check(r.o.grabbed && r.o.grip == 2, "then a press with S3's hand on its hold: S3");
}

void Glide() {
    std::printf("14 the glide\n");
    const float glideFrames = kProfile.glide / kDt;
    // From 12.5 cm above S1's hold, the hand still: onto the hold over the glide's time.
    Rig r(1, 0);
    r.Hold(0.0f, 0.0f, 0.125f);
    r.Step();
    float palmThen[3];
    r.PalmInGun(palmThen);
    r.Watch();
    r.Press();
    float lastShare = r.o.share;
    bool rising = true;
    int frames = 1;
    for (; frames < 30 && r.o.share < 1.0f; ++frames) {
        r.Step();
        rising = rising && r.o.share >= lastShare;
        lastShare = r.o.share;
    }
    float drawnAt[3];
    r.DrawnPalm(drawnAt);
    const float way = Distance(palmThen, GripHold(0)), most = 1.6f * way / glideFrames;
    Check(r.o.held && rising && frames >= glideFrames - 1.0f && frames <= glideFrames + 1.0f,
          "pressed 12.5 cm off: the hand's share rose 0 to 1 over %d frames (%.0f ms; the glide %.0f ms)", frames,
          frames * kDt * 1000.0f, kProfile.glide * 1000.0f);
    Check(r.palmStep <= most && r.firstStep <= 0.1f * way && Distance(drawnAt, GripHold(0)) < 0.0005f,
          "drawn onto the hold with no jump: %.1f cm a frame at most (the eased move's %.1f), the first %.2f cm, %.2f "
          "mm off the hold then",
          r.palmStep * 100.0f, most * 100.0f, r.firstStep * 100.0f, Distance(drawnAt, GripHold(0)) * 1000.0f);
    // Riding it back.
    r.Watch();
    r.PullTo(0.03f);
    r.DrawnPalm(drawnAt);
    const float onSlide[3] = {GripHold(0)[0], GripHold(0)[1], GripHold(0)[2] - r.o.back};
    Check(std::fabs(r.o.back - 0.03f) < 1e-5f && r.palmStep < 0.003f && Distance(drawnAt, onSlide) < 0.0005f,
          "pulled 3 cm: the drawn hand rides the slide back (%.1f mm a frame at most, on its hold)",
          r.palmStep * 1000.0f);
    // Let go: back to the controller over the glide's time, the slide springing home as before.
    r.PullTo(0.05f);
    r.Watch();
    float letGoAt[3];
    r.DrawnPalm(letGoAt);
    r.Release();
    bool monotonic = true;
    const int home = r.FramesHome(20, monotonic);
    int off = 0;
    for (; off < 30 && r.o.share > 0.0f; ++off)
        r.Step();
    float palmNow[3];
    r.PalmInGun(palmNow);
    const float back = Distance(letGoAt, palmNow), mostBack = 1.6f * back / glideFrames;
    Check(home <= 5 && monotonic && r.racks == 1, "let go fully back: racked, home %d frames after as before", home);
    Check(r.palmStep <= mostBack && home + off + 1 >= glideFrames - 1.0f && home + off + 1 <= glideFrames + 2.0f,
          "the hand eased back to the controller over %d frames with no jump (%.1f cm a frame at most, the eased "
          "move's %.1f)",
          home + off + 1, r.palmStep * 100.0f, mostBack * 100.0f);
    // Grabbed again a frame after the let go, the hand still 12.5 cm above: on from where it's drawn, the same grip.
    Rig a(1, 0);
    a.Hold(0.0f, 0.0f, 0.125f);
    a.Step();
    a.Press();
    a.Steps(12);
    a.PullTo(0.05f);
    a.Hold(kProfile.travel, 0.0f, 0.125f);
    a.Step();
    a.Release();
    a.Watch();
    a.Press();
    const float regrabStep = a.firstStep;
    a.Steps(12);
    Check(a.grabs == 2 && a.o.grip == 0 && a.flips == 0 && regrabStep < 0.005f && a.palmStep < 0.02f,
          "pressed again a frame after the let go: grabbed, S1, glides on from where it was drawn (%.1f mm that frame, "
          "%.1f cm a frame at most)", regrabStep * 1000.0f, a.palmStep * 100.0f);
    // The hand turned the other way round (as S2, out on the gun's left where only S1's hold is in reach), held still:
    // once the press has waited the settle for it to come on to S2's hold, S1. The palm still goes straight onto the
    // hold, the hand turning over about it, over a glide made longer for the turn (the review: in the glide's own
    // 0.12 s the wrist whipped round 38 degrees a frame), so it turns no faster than through 90 degrees in 0.12 s does
    // (the quaternions' straight blend turns a half turn's middle a fifth faster than a quarter turn's).
    Rig s(1, 1);
    s.Hold(0.0f, 0.12f);
    s.Step();
    float from[3];
    s.PalmInGun(from);
    s.Watch();
    s.Press();
    for (int i = 0; i < 12 && s.grabs == 0; ++i)
        s.Step();
    const float turnedOff = s.lastGrab.turnedOff, spinFor = s.lastGrab.glideFor;
    const float spinFrames = spinFor / kDt, wantFor = kProfile.glide * turnedOff / kProfile.turnAt;
    const int spun = s.FramesOn(60) + 1;  // (the grab's frame the first of the glide)
    const float spinMost = 1.6f * Distance(from, GripHold(0)) / spinFrames;
    const float turnMost = 1.25f * 1.6f * kProfile.turnAt / glideFrames;
    Check(s.grabs == 1 && s.o.grip == 0 && turnedOff > 150.0f && std::fabs(spinFor - wantFor) < 1e-4f &&
              spun >= spinFrames - 1.0f && spun <= spinFrames + 1.0f,
          "turned as S2 12 cm out on the gun's left: S1 (%.0f degrees off its turn) %.0f ms after the press, the glide "
          "%.0f ms (%.0f x %.0f/%.0f), on in %d frames",
          turnedOff, s.lastGrab.waited * 1000.0f, spinFor * 1000.0f, kProfile.glide * 1000.0f, turnedOff,
          kProfile.turnAt, spun);
    Check(s.palmStep <= spinMost && s.turnStep < turnMost,
          "... the palm %.1f cm a frame at most (the eased move's %.1f), the hand turning %.0f degrees a frame at most "
          "(a quarter turn in the glide's time: %.0f; it was 38)",
          s.palmStep * 100.0f, spinMost * 100.0f, s.turnStep, turnMost / 1.25f);
    // Let go with the controller turned the other way round from the grip it's drawn in (the hand turned as S2's while
    // it held S1): the ease back off lengthened the same way.
    Rig e(1, 0);
    e.Press();
    e.Steps(12);
    e.TurnAs(1);
    e.Hold();
    e.Step();
    const float offTurn = AngleBetween(g_palms[0], g_palms[1]);
    e.Watch();
    e.Release();
    int easing = 1;
    for (; easing < 60 && e.o.share > 0.0f; ++easing)
        e.Step();
    const float offFrames = kProfile.glide * std::fmax(1.0f, offTurn / kProfile.turnAt) / kDt;
    Check(offTurn > 150.0f && easing >= offFrames - 1.0f && easing <= offFrames + 1.0f && e.turnStep < turnMost,
          "let go with the controller turned %.0f degrees from the grip it's drawn in: eased back off over %d frames "
          "(%.0f), %.0f degrees a frame at most",
          offTurn, easing, offFrames, e.turnStep);
    // The hand still coming in at 1 m/s as it takes hold, from 12.5 cm above: no step beyond its own and the glide's.
    Rig m(1, 0);
    m.Hold(0.0f, 0.0f, 0.125f);
    m.Step();
    m.Watch();
    m.Press();
    for (int i = 1; i <= 9; ++i) {
        m.Hold(0.0f, 0.0f, 0.125f - i * kDt);
        m.Step();
    }
    m.Steps(6);
    Check(m.grabs == 1 && m.palmStep <= kDt + most, "the hand coming on in at 1 m/s through the glide: %.1f cm a frame "
          "at most (its own %.1f and the glide's %.1f)", m.palmStep * 100.0f, kDt * 100.0f, most * 100.0f);
}

void Waits() {
    std::printf("15 a press before the hand gets there\n");
    // Pressed 20 cm above S1's hold (out of every hold's reach), the hand coming down at 1 m/s.
    Rig r(1, 0);
    r.Hold(0.0f, 0.0f, 0.20f);
    r.Step();
    r.Press();
    const bool waiting = r.presses == 1 && r.grabs == 0 && r.misses == 0 && std::fabs(r.palmFrom - 0.20f) < 0.03f;
    int frame = 0, grabbedAt = -1;
    float within = 0.0f;
    for (frame = 1; frame <= 30; ++frame) {
        r.Hold(0.0f, 0.0f, std::fmax(0.20f - frame * kDt, 0.0f));
        r.Step();
        if (r.o.grabbed && grabbedAt < 0) {
            grabbedAt = frame;
            within = r.lastGrab.palmFrom;
        }
    }
    Check(waiting && r.grabs == 1 && grabbedAt > 0 && within < kProfile.reach && within > kProfile.reach - 0.015f &&
              std::fabs(r.lastGrab.waited - grabbedAt * kDt) < 1e-4f && r.lastGrab.pressedFrom > kProfile.reach,
          "pressed 20 cm off, the hand coming in at 1 m/s: waiting, then grabbed once, %.0f ms on, as it came within "
          "%.1f cm (pressed %.1f cm off)",
          r.lastGrab.waited * 1000.0f, within * 100.0f, r.lastGrab.pressedFrom * 100.0f);
    // Too late: the hand there 0.4 s after the press.
    Rig l(1, 0);
    l.Hold(0.0f, 0.0f, 0.20f);
    l.Step();
    l.Press();
    l.Steps(28);  // 0.39 s, still
    l.Hold();
    l.Steps(5);
    Check(l.grabs == 0 && l.misses == 1 && std::fabs(l.lastMiss.nearest - 0.20f) < 0.03f &&
              l.lastMiss.pressedFrom > kProfile.reach && *l.lastMiss.missWhy,
          "the hand there 0.4 s after the press: missed at 0.3 s (told: %s, the nearest %.1f cm), not grabbed when it "
          "came",
          l.lastMiss.missWhy, l.lastMiss.nearest * 100.0f);
    // The trigger let go before the hand got there.
    Rig g(1, 0);
    g.Hold(0.0f, 0.0f, 0.20f);
    g.Step();
    g.Press();
    g.Steps(3);
    g.trigger = 0.3f;
    g.Step();
    g.Hold();
    g.Steps(5);
    Check(g.grabs == 0 && g.misses == 1 && std::strstr(g.lastMiss.missWhy, "let go"),
          "the trigger let go before the hand got there: missed (%s), not grabbed", g.lastMiss.missWhy);
    // The pistol out of hand while it waits.
    Rig p(1, 0);
    p.Hold(0.0f, 0.0f, 0.20f);
    p.Step();
    p.Press();
    p.pistol = false;
    p.Step();
    p.pistol = true;
    p.Hold();
    p.Steps(5);
    Check(p.grabs == 0 && p.misses == 1, "the pistol out of hand while it waits: missed (%s), not grabbed",
          p.lastMiss.missWhy);
    // A press with no palm doesn't wait: the hand found on the hold then grabs nothing.
    Rig n(1, 0);
    n.hand = false;
    n.Press();
    n.hand = true;
    n.Steps(5);
    Check(n.presses == 1 && n.grabs == 0 && n.misses == 0, "a press with no palm: told at once, doesn't wait");
    // "wait 0": missed at the press.
    Rig z(1, 0);
    z.slide.Tune(kProfile.reach, 0.0f, kProfile.glide);
    z.Hold(0.0f, 0.0f, 0.20f);
    z.Step();
    z.Press();
    const bool atOnce = z.misses == 1 && z.o.pressed && z.o.missed;
    z.Hold();
    z.Steps(5);
    Check(atOnce && z.grabs == 0, "wait 0: missed at the press, not grabbed when the hand gets there");
    // The hand losing its place for a few frames as it comes in: still grabbed once found within reach.
    Rig q(1, 0);
    q.Hold(0.0f, 0.0f, 0.18f);
    q.Step();
    q.Press();
    q.hand = false;
    q.Steps(4);
    q.hand = true;
    q.Hold(0.0f, 0.0f, 0.05f);
    q.Step();
    Check(q.grabs == 1 && q.o.held, "the hand lost 4 frames while it waits, then found 5 cm off: grabbed");
}

// The hand turned for a grip coming in to a stop on its hold, along u (a unit direction in gun space, from the hold out
// to where the hand starts): the trigger pressed where it starts (start out, with the slide stopBack back), then either
// slowing evenly from v to the stop or at v until its last 3 cm and eased to the stop from there (the review's two
// scratch runs); held still 0.3 s once stopped, then let go. No pull meant at all.
struct Approach {
    int grabs = 0, grip = -1;
    float pulled = 0.0f;  // the slide's pull once the hand had stopped (m, from home or the stop)
    float most = 0.0f;    // the most it was pulled on the way (m, from home or the stop)
    bool racked = false;  // the let go racked it (or dropped the stop)
};
Approach ComeIn(int grip, const float* u, float start, float v, bool evenly, bool locked, float stopBack) {
    Rig r(1, grip);
    r.locked = locked;
    const float base = locked ? kProfile.lockAt : 0.0f;
    float dist = start, speed = v;
    auto at = [&] { r.HoldAt(grip, stopBack, u[0] * dist, u[1] * dist, u[2] * dist); };
    at();
    r.Step();
    r.Press();
    Approach a;
    auto note = [&] {
        if (r.o.back >= 0.0f)
            a.most = std::fmax(a.most, r.o.back - base);
    };
    note();
    const float slowing = v * v / (2.0f * start);
    for (int f = 0; f < 400 && dist > 0.0f; ++f) {
        if (evenly)
            speed = std::fmax(speed - slowing * kDt, 0.02f);
        else
            speed = dist < 0.03f ? std::fmax(v * dist / 0.03f, 0.05f) : v;
        dist = std::fmax(dist - speed * kDt, 0.0f);
        at();
        r.Step();
        note();
    }
    r.Steps(22);
    note();
    a.pulled = r.o.back >= 0.0f ? r.o.back - base : 0.0f;
    a.grabs = r.grabs;
    a.grip = r.lastGrab.grip;
    r.Release();
    a.racked = r.racks > 0;
    return a;
}

void ComingIn() {
    std::printf("16 the hand still coming in as it takes hold\n");
    // In from behind as S3: pressed 15 cm behind its hold, coming forward at 0.6 m/s, grabbed as it comes within reach
    // and going on to 2 cm short of the hold before it stops; then pulled back 3 cm from there.
    Rig r(1, 2);
    r.HoldAt(2, 0.0f, 0.0f, 0.0f, -0.15f);
    r.Step();
    r.Press();
    float most = 0.0f, z = -0.15f, grabbedAt = 0.0f;
    for (; z + 0.6f * kDt <= -0.02f;) {
        z += 0.6f * kDt;
        r.HoldAt(2, 0.0f, 0.0f, 0.0f, z);
        r.Step();
        if (r.o.grabbed)
            grabbedAt = z;
        most = std::fmax(most, r.o.back);
    }
    r.Steps(5);
    const bool atHome = r.grabs == 1 && r.o.held && most == 0.0f;
    for (int i = 1; i <= 12; ++i) {
        r.HoldAt(2, 0.0f, 0.0f, 0.0f, z - 0.03f * i / 12.0f);
        r.Step();
    }
    Check(atHome && std::fabs(r.o.back - 0.03f) < 1e-5f,
          "S3 in from behind, grabbed %.1f cm out, the hand going on %.1f cm: the slide at home all the way; pulled 3 "
          "cm back from there: 3 cm back (%.2f)",
          -grabbedAt * 100.0f, (z - grabbedAt) * 100.0f, r.o.back * 100.0f);

    // The review's first scratch run: pressed in reach with the hand still coming in, slowing evenly to a stop on the
    // hold, in the gun's level plane from the side, some of it from in front (positive angles) or from behind: S1 from
    // its left, S2 from its right, S3 from behind; 6, 10 or 13 cm out at 0.3 or 0.6 m/s; at home, and on the slide
    // stop with the hand stopping on the hold there or where the hold is with the slide closed (4.1 cm in front of it).
    // At 30 degrees towards the front, 10-13 cm out, the approach itself had drawn the slide fully back and racked it.
    const char* const from[3] = {"S1 from its left", "S2 from its right", "S3 from behind"};
    const float side[3] = {1.0f, -1.0f, 1.0f};
    for (int g = 0; g < 3; ++g) {
        int runs = 0, own = 0, racked = 0;
        float worst = 0.0f, worstStop = 0.0f;
        for (int stop = 0; stop < 3; ++stop)  // home; on the stop, to its hold there; to where it is closed
            for (float v : {0.3f, 0.6f})
                for (float d : {0.06f, 0.10f, 0.13f})
                    for (float angle : {-45.0f, -20.0f, 0.0f, 15.0f, 30.0f, 45.0f, 60.0f}) {
                        const float a = (g == 2 ? angle - 90.0f : angle) * kPi / 180.0f;
                        const float u[3] = {side[g] * std::cos(a), 0.0f, std::sin(a)};
                        const Approach run =
                            ComeIn(g, u, d - 0.001f, v, true, stop > 0, stop == 1 ? kProfile.lockAt : 0.0f);
                        ++runs;
                        own += run.grabs == 1 && run.grip == g;
                        racked += run.racked;
                        (stop ? worstStop : worst) = std::fmax(stop ? worstStop : worst, run.most);
                    }
        Check(own == runs && racked == 0 && worst <= kProfile.settleStep + 1e-5f &&
                  worstStop < kProfile.tug - 0.001f,
              "%s, 45 degrees from behind to 60 from in front, pressed 6-13 cm out coming in at 0.3-0.6 m/s to a stop: "
              "%d of %d taken in its grip, %d racked or off the stop, the slide %.1f mm back at the most (%.1f on the "
              "stop; its tug %.0f)",
              from[g], own, runs, racked, worst * 1000.0f, worstStop * 1000.0f, kProfile.tug * 1000.0f);
    }
    // The review's second: pressed 20 cm out (waiting), the hand coming in at 0.5 or 1 m/s from each of the 14 ways
    // round its own grip's hold, easing to a stop over its last 3 cm. Before, all five ways with some of it from in
    // front (in front, and the four diagonals there) racked it by themselves, in all three grips; turned for S2 or S3
    // from in front, the press took S1 14-19 cm out of the hand's own grip's hold, and its coming on in pulled it.
    float ways[14][3];
    Ways(ways);
    for (int g = 0; g < 3; ++g) {
        int runs = 0, own = 0, racked = 0, frontRuns = 0, frontOwn = 0;
        float worst = 0.0f;
        for (float v : {0.5f, 1.0f})
            for (const auto& u : ways) {
                const Approach run = ComeIn(g, u, 0.20f, v, false, false, 0.0f);
                ++runs;
                own += run.grabs == 1 && run.grip == g;
                racked += run.racked;
                worst = std::fmax(worst, run.most);
                if (u[2] > 0.1f)
                    ++frontRuns, frontOwn += run.grabs == 1 && run.grip == g && !run.racked && run.most < 0.001f;
            }
        Check(own == runs && racked == 0 && worst <= kProfile.settleStep + 1e-5f && frontOwn == frontRuns,
              "%s's hand pressed 20 cm out, coming in from the 14 ways round its hold to a stop: %d of %d taken in its "
              "grip, %d racked, the slide %.1f mm back at the most (from in front and its diagonals: %d of %d)",
              kNames[g], own, runs, racked, worst * 1000.0f, frontOwn, frontRuns);
    }

    // A pull meant after all. Coming in from in front (S1 from its left, 30 degrees towards the front, pressed 13 cm
    // out at 0.6 m/s) and stopped on the hold, still for 0.15 s, then pulled 5 cm: the slide comes back with the hand
    // from there, and racks.
    const float a30 = 30.0f * kPi / 180.0f;
    const float front30[3] = {std::cos(a30), 0.0f, std::sin(a30)};
    {
        Rig p(1, 0);
        float dist = 0.129f, speed = 0.6f;
        const float slowing = speed * speed / (2.0f * dist);
        p.HoldAt(0, 0.0f, front30[0] * dist, 0.0f, front30[2] * dist);
        p.Step();
        p.Press();
        while (dist > 0.0f) {
            speed = std::fmax(speed - slowing * kDt, 0.02f);
            dist = std::fmax(dist - speed * kDt, 0.0f);
            p.HoldAt(0, 0.0f, front30[0] * dist, 0.0f, front30[2] * dist);
            p.Step();
        }
        p.Steps(11);
        const float still = p.o.back;
        float worst = 0.0f;
        for (int i = 1; i <= 12; ++i) {
            p.Hold(0.05f * i / 12.0f);
            p.Step();
            worst = std::fmax(worst, std::fabs(p.o.back - std::fmin(0.05f * i / 12.0f, kProfile.travel)));
        }
        p.Release();
        Check(still < 1e-5f && worst < 1e-5f && p.racks == 1,
              "S1 in from in front at 30 degrees, stopped on the hold, still 0.15 s, pulled 5 cm: the slide with the "
              "hand from there (%.4f mm off at most), racked",
              worst * 1000.0f);
    }
    // Stopped 3 cm in front of the hold (S1 straight in from in front, pressed 12 cm out at 0.5 m/s), still 0.15 s,
    // then pulled 4.6 cm back from there: it comes back with the hand at once, and racks (no part of the pull lost).
    {
        Rig p(1, 0);
        float dist = 0.12f;
        p.HoldAt(0, 0.0f, 0.0f, 0.0f, dist);
        p.Step();
        p.Press();
        while (dist > 0.03f) {
            dist = std::fmax(dist - 0.5f * kDt * std::fmin(1.0f, (dist - 0.03f) / 0.03f + 0.1f), 0.03f);
            p.HoldAt(0, 0.0f, 0.0f, 0.0f, dist);
            p.Step();
        }
        p.Steps(11);
        const float still = p.o.back;
        float first = -1.0f;
        for (int i = 1; i <= 12; ++i) {
            p.HoldAt(0, 0.0f, 0.0f, 0.0f, 0.03f - 0.046f * i / 12.0f);
            p.Step();
            if (first < 0.0f)
                first = p.o.back;
        }
        p.Release();
        Check(still < 1e-5f && std::fabs(first - 0.046f / 12.0f) < 1e-5f && p.racks == 1,
              "S1 stopped 3 cm in front of its hold, still 0.15 s, pulled back 4.6 cm: %.1f mm back the first frame of "
              "it (the hand's %.1f), racked",
              first * 1000.0f, 0.046f / 12.0f * 1000.0f);
    }
    // The sweep: straight in from in front at 0.5 m/s (S1, pressed 12 cm out) through the hold and on 6 cm without
    // stopping: what goes on past the hold pulls it, and it racks.
    {
        Rig p(1, 0);
        float along = 0.12f;
        p.HoldAt(0, 0.0f, 0.0f, 0.0f, along);
        p.Step();
        p.Press();
        while (along > -0.06f) {
            along = std::fmax(along - 0.5f * kDt, -0.06f);
            p.HoldAt(0, 0.0f, 0.0f, 0.0f, along);
            p.Step();
        }
        const float back = p.o.back;
        p.Release();
        Check(std::fabs(back - kProfile.travel) < 1e-5f && p.racks == 1,
              "S1 swept in from in front through its hold and on 6 cm: %.2f cm back, racked", back * 100.0f);
    }
    // From behind and straight back without stopping (S3 pressed 12 cm behind its hold, in at 0.6 m/s to 2 cm short,
    // then at once back 5 cm at 0.6 m/s): it comes back from the first frame back, and racks.
    {
        Rig p(1, 2);
        float along = -0.12f;
        p.HoldAt(2, 0.0f, 0.0f, 0.0f, along);
        p.Step();
        p.Press();
        while (along < -0.02f) {
            along = std::fmin(along + 0.6f * kDt, -0.02f);
            p.HoldAt(2, 0.0f, 0.0f, 0.0f, along);
            p.Step();
        }
        const float turned = p.o.back;
        p.HoldAt(2, 0.0f, 0.0f, 0.0f, along - 0.6f * kDt);
        p.Step();
        const float first = p.o.back;
        for (float to = along - 0.6f * kDt; to > along - 0.05f;) {
            to = std::fmax(to - 0.6f * kDt, along - 0.05f);
            p.HoldAt(2, 0.0f, 0.0f, 0.0f, to);
            p.Step();
        }
        p.Release();
        Check(turned < 1e-5f && std::fabs(first - 0.6f * kDt) < 1e-5f && p.racks == 1,
              "S3 in from behind and straight back without a stop: home as it came, %.1f mm back the first frame back "
              "(the hand's %.1f), racked",
              first * 1000.0f, 0.6f * kDt * 1000.0f);
    }
    // Overshooting the hold and settling back (S3 in from behind at 0.6 m/s, 1 cm past its hold, back onto it over
    // 0.1 s), at home and on the slide stop: no pull, nor the stop dropped.
    for (bool locked : {false, true}) {
        Rig p(1, 2);
        p.locked = locked;
        const float base = locked ? kProfile.lockAt : 0.0f;
        float along = -0.12f, peak = 0.0f;
        p.HoldAt(2, base, 0.0f, 0.0f, along);
        p.Step();
        p.Press();
        while (along < 0.01f) {
            along = std::fmin(along + 0.6f * kDt * std::fmin(1.0f, (0.01f - along) / 0.02f + 0.15f), 0.01f);
            p.HoldAt(2, base, 0.0f, 0.0f, along);
            p.Step();
            peak = std::fmax(peak, p.o.back - base);
        }
        for (int i = 1; i <= 7; ++i) {
            p.HoldAt(2, base, 0.0f, 0.0f, 0.01f * (1.0f - i / 7.0f));
            p.Step();
            peak = std::fmax(peak, p.o.back - base);
        }
        p.Steps(22);
        p.Release();
        Check(p.grabs == 1 && peak < 0.001f && p.racks == 0,
              "S3 in from behind 1 cm past its hold and settling back onto it%s: %.1f mm back at the most, %s",
              locked ? ", on the slide stop" : "", peak * 1000.0f, p.racks ? "racked" : "not racked");
    }
}

void Tunables() {
    std::printf("17 the tunables\n");
    pistolslide::Slide s;
    s.Tune(0.01f, -1.0f, 0.0f);
    const pistolslide::Profile low = s.Settings();
    s.Tune(1.0f, 5.0f, 2.0f);
    const pistolslide::Profile high = s.Settings();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    s.Tune(nan, nan, nan);
    const pistolslide::Profile kept = s.Settings();
    Check(low.reach == 0.03f && low.wait == 0.0f && low.glide == 0.05f && high.reach == 0.25f && high.wait == 1.0f &&
              high.glide == 0.5f && kept.reach == 0.25f && kept.wait == 1.0f && kept.glide == 0.5f,
          "grab kept to 3-25 cm, 0-1 s and 0.05-0.5 s; not a number leaves it as it was");
    // A 7 cm reach (the one there was): 6.9 cm off grabs, 7.1 doesn't.
    for (float at : {0.069f, 0.071f}) {
        Rig r(1, 0);
        r.slide.Tune(0.07f, 0.0f, kProfile.glide);
        r.Hold(0.0f, at);
        r.Step();
        r.Press();
        Check(r.grabs == (at < 0.07f ? 1 : 0), "grab 7 0: %.1f cm off along the gun's x: %s", at * 100.0f,
              r.grabs ? "grabbed" : "not grabbed");
    }
    // A 0.3 s glide.
    Rig g(1, 0);
    g.slide.Tune(kProfile.reach, kProfile.wait, 0.3f);
    g.Hold(0.0f, 0.0f, 0.1f);
    g.Step();
    g.Press();
    const int frames = g.FramesOn(60);
    Check(std::fabs(frames * kDt - 0.3f) <= kDt, "grab 13 0.3 0.3: the hand on the slide %.0f ms after the press",
          frames * kDt * 1000.0f);
}

void Jumps() {
    std::printf("18 tracking jumps\n");
    // Held at home or on the slide stop, the palm jumping forward a frame and straight back (the left controller's
    // tracking): the hand slides forward on the slide at most 3 mm a frame (slip), so it's drawn that far back at the
    // most after, and never racked or off the stop (the review: a 2 cm jump left it 2 cm back, 5 cm racked it).
    for (bool locked : {false, true}) {
        const float base = locked ? kProfile.lockAt : 0.0f;
        float most = 0.0f;
        int racks = 0;
        for (float jump : {0.02f, 0.05f, 0.10f}) {
            Rig r;
            r.locked = locked;
            r.Hold(base);
            r.Press();
            r.Steps(12);
            r.HoldAt(0, base, 0.0f, 0.0f, jump);
            r.Step();
            r.Hold(base);
            r.Steps(30);
            most = std::fmax(most, r.o.back - base);
            r.Release();
            racks += r.racks;
        }
        Check(most <= kProfile.slip + 1e-6f && racks == 0,
              "%s, the palm jumping 2, 5 or 10 cm forward a frame and straight back: %.1f mm back at the most after "
              "(the slip's %.0f a frame), %s",
              locked ? "on the slide stop" : "held at home", most * 1000.0f, kProfile.slip * 1000.0f,
              racks ? "racked" : "never racked");
    }
    // Gone on forward and stayed there, it slides forward on it all the same, 3 mm a frame: pushed 3 cm forward of home
    // in a frame and still 0.2 s, then pulled 3 cm back from there: 3 cm back.
    Rig f;
    f.Press();
    f.Steps(12);
    f.HoldAt(0, 0.0f, 0.0f, 0.0f, 0.03f);
    f.Steps(15);
    for (int i = 1; i <= 12; ++i) {
        f.HoldAt(0, 0.0f, 0.0f, 0.0f, 0.03f - 0.03f * i / 12.0f);
        f.Step();
    }
    Check(std::fabs(f.o.back - 0.03f) < 1e-5f && f.o.held,
          "pushed 3 cm forward of home in a frame, still 0.2 s, pulled 3 cm back from there: %.2f cm back",
          f.o.back * 100.0f);
}

void Misses() {
    std::printf("19 why a press missed\n");
    // Waiting through frames without the palm (busy: the right trigger pulled, a magazine in the hand; or the left
    // controller lost), the palm on the hold while it isn't known, the wait over: told it wasn't seen within reach, and
    // for how long it wasn't known (the review: told "didn't come within reach" with the palm well within it).
    Rig b(1, 0);
    b.Hold(0.0f, 0.0f, 0.20f);
    b.Step();
    b.Press();
    b.Steps(3);
    b.hand = false;
    b.Hold();
    b.Steps(24);
    b.hand = true;
    b.Steps(3);
    Check(b.misses == 1 && b.grabs == 0 && std::strstr(b.lastMiss.missWhy, "wasn't seen") &&
              std::fabs(b.lastMiss.unseen - (kProfile.wait - 3 * kDt)) < 1.5f * kDt,
          "the palm not known for most of the wait, on the hold then: missed (%s), %.0f ms of it not known",
          b.lastMiss.missWhy, b.lastMiss.unseen * 1000.0f);
    // Pressed while the hand still eases back off S1, the palm 6 cm from S2's hold (13.8 cm from S1's): with "wait 0",
    // missed at the press, told why, and from the nearest hold it could take (S1's, out of reach; it had been told as
    // S2's, 6 cm off, "too far" all the same); with the wait, S2 once the ease is done.
    float away[3];
    for (int i = 0; i < 3; ++i)
        away[i] = GripHold(1)[i] - GripHold(0)[i];
    const float apart = std::sqrt(Dot(away, away));
    const float p[3] = {GripHold(1)[0] + away[0] / apart * 0.06f, GripHold(1)[1] + away[1] / apart * 0.06f,
                        GripHold(1)[2] + away[2] / apart * 0.06f};
    for (float wait : {0.0f, kProfile.wait}) {
        Rig e(1, 0);
        e.slide.Tune(kProfile.reach, wait, kProfile.glide);
        e.Press();
        e.Steps(12);
        e.Release();
        e.TurnAs(1);
        e.PalmAt(p);
        e.Press();
        const bool easing = e.o.share > 0.0f && !e.o.grabbed;
        e.Steps(20);
        if (wait == 0.0f)
            Check(easing && e.misses == 1 && e.grabs == 1 && std::strstr(e.lastMiss.missWhy, "eased back off") &&
                      e.lastMiss.pressedFrom >= kProfile.reach,
                  "pressed 6 cm from S2's hold as the hand eases back off S1, wait 0: missed (%s), %.0f mm from the "
                  "nearest hold it could take",
                  e.lastMiss.missWhy, e.lastMiss.pressedFrom * 1000.0f);
        else
            Check(easing && e.misses == 0 && e.grabs == 2 && e.lastGrab.grip == 1,
                  "... with the wait: S2 %.0f ms after the press, once the ease is done", e.lastGrab.waited * 1000.0f);
    }
    // Turned for S3 and pressed 11 cm in front of S1's hold (in reach of S1's only), coming on towards S3's, the
    // trigger let go before that one came within reach: missed, told it waited for the grip the hand is turned for,
    // and the press in reach (so not told "too far").
    Rig t(1, 2);
    const float* s1 = GripHold(0);
    const float start[3] = {s1[0], s1[1], s1[2] + 0.11f};
    t.PalmAt(start);
    t.Step();
    t.Press();
    float toward[3];
    for (int i = 0; i < 3; ++i)
        toward[i] = GripHold(2)[i] - start[i];
    const float length = std::sqrt(Dot(toward, toward));
    for (int k = 1; k <= 3; ++k) {
        float q[3];
        for (int i = 0; i < 3; ++i)
            q[i] = start[i] + toward[i] / length * 0.007f * k;
        t.PalmAt(q);
        t.Step();
    }
    t.Release();
    Check(t.misses == 1 && t.grabs == 0 && t.lastMiss.pressedFrom < kProfile.reach &&
              std::strstr(t.lastMiss.missWhy, "turned for"),
          "turned for S3, pressed in reach of S1's hold only and coming on, the trigger let go: missed (%s), pressed "
          "%.0f mm from the nearest hold",
          t.lastMiss.missWhy, t.lastMiss.pressedFrom * 1000.0f);
    // The pistol out of hand while it waits: told there was no pistol to work (hands.cpp says which: out of hand, or
    // pistol.txt no longer working the slide; the review found pistol.txt turned off told as the pistol leaving).
    Rig n(1, 0);
    n.Hold(0.0f, 0.0f, 0.20f);
    n.Step();
    n.Press();
    n.pistol = false;
    n.Step();
    Check(n.misses == 1 && std::strstr(n.lastMiss.missWhy, "no pistol"), "the pistol out of hand while it waits: %s",
          n.lastMiss.missWhy);
}

}

int main(int argc, char** argv) {
    g_verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;
    PreparePalms();
    std::printf("pistolslidetest: src/pistolslide.cpp at 72 Hz. Three grips' holds (S1 %.4f %.4f %.4f), within %.0f "
                "cm or a press waiting %.1f s for the hand, gliding on and off over %.2f s; %.2f cm of travel, racked "
                "past %.1f, the stop at %.1f (a %.0f mm tug), home in %.0f ms, held %.0f ms with the palm lost.\n",
                GripHold(0)[0], GripHold(0)[1], GripHold(0)[2], kProfile.reach * 100, kProfile.wait, kProfile.glide,
                kProfile.travel * 100, kProfile.rackAt * 100, kProfile.lockAt * 100, kProfile.tug * 1000,
                kProfile.springHome * 1000, kProfile.coast * 1000);
    Grips();
    Reach();
    FreshPresses();
    Ride();
    LetGo();
    Caught();
    SlideStop();
    TriggerRack();
    Lost();
    Degenerate();
    FirstTest();
    OwnHands();
    NoFlip();
    Glide();
    Waits();
    ComingIn();
    Tunables();
    Jumps();
    Misses();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
