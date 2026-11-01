// The train pistol's magazine by the left hand (src/pistolmag.cpp, as hands.cpp runs it with build\pistol.txt "mag 1"),
// on scripted frames at 72 Hz, without the game: the design's reloadtest (screenshots\wf_reload\design) ported, its
// magazine's checks (the ammo and the slide come later), the review's fixes, and since the first headset test the
// magnetic catch, the glide, the animated snap and G2. The pistol is held still in the view (the head's space: x right,
// y up, z back), out front-right, a little down and canted (25/-12/8 degrees, yaw/pitch/roll), and the left hand is put
// where it holds a magazine at a place near the grip (pistolmag's GloveFor, the carry's hold unless said): its top s
// along the grip's line from home (0 seated, -11.08 cm at the grip's bottom, the well's mouth), off the line along the
// gun's x or to its front and turned about the gun's x. Once it's caught, the hand's push is read from where it was
// caught: Push puts the hand where the push has the top at s. The geometry here is the design's own (geom.exe), not
// read from the module. The grip's own magazine is out (X) unless a check says otherwise; a seat puts it back in. G2
// (the index along its front) is the carry unless said; the defaults are the module's: caught within 7.5 cm and 55
// degrees, each eased move 0.15 s, the snap 6.5 cm, the dwell 0.4 s.
//   1  empty, a magazine from the pelvis: carried in G2's place and fingers (nothing in the hand changes on the way),
//      caught as it comes within reach, glides in, the hand drawn going over to the flat base, pushed home
//   2  a squeeze at the pelvis that takes nothing: the magazine in (X first), the hand busy, no pistol
//   3  let go once it's caught and the hand dropped away: it stays (parked); the palm pushes it home, held as the
//      carry or as the flat base would hold it
//   4  the drops: let go while carried away from the grip it falls at the hand's speed; at the pelvis it goes back; let
//      go within reach of the grip, it's caught all the same
//   5  the magnetic catch from any direction: lined up from below, 6 cm to the side, 45 degrees tilted, the gun moved
//      down onto a still magazine, from the front, a top already in the grip (straightened there; found there); not: a
//      fast swipe past, a one-frame tracking jump, turned past the angle, plainly going away, out of reach; followed
//      through (the review): the hand carrying on in after the catch from the side, the front, below at 45 degrees and
//      down from above, then pushed on or past the line and back, kept and home; leaving it still leaves it; pulled
//      back out in the grip and pushed straight back up, caught again
//   6  the game's own reload with a magazine in the hand: it's gone
//   7  the carries: G2 and C2 both, the same path the same way; the magazine never turns in the hand, the hand drawn on
//      it does (G2 52 degrees, C2 none); the game's pinch as G2; G2's index waits at its reach while the hand goes over
//   8  the snap (the review: the controllers can't overlap): within it two frames running; a one-frame jump near home
//      moves and seats nothing; "seat 0.5", "seat 50"; animated: eased over the glide's time from the speed it had,
//      never past home, the hand going with it and easing back to the controller after
//   9  the dwell: held there gripped 0.4 s, home by itself; let go, or drawing back, not; "dwell 0" never
//  10  each try's nearest approach (to set the snap from): parked, back out, seated, gone; the hands apart then
//  11  the push home (B2): its share over the last 3 cm; the hand drawn under the magazine on B, then on B2
//  12  the hand or the pistol lost: kept a second in the hand; on the line a moment (then parked); going home by itself
//      (the snap), it goes on; the hand drawn easing back after the seat with the pistol out of hand; cleared by the
//      caller, nothing left easing back; gone by the game's reload, eased back from where it was
//  13  the holds and the helpers: C2 is the flat base's place, C1 53 degrees off, G2 52; PlaceOnLine is the design's
//  14  frame times and poses not numbers: nothing drawn that isn't a number, never past home
//  15  the glide: no frame's step beyond a small one, the hand drawn with it, about the glide's time; let go during
//      it, it goes on in; X during it leaves it be; pulled back out with the grip squeezed, it glides back to the hand
//  16  no flicker: the fingers and the hand counted turning over while hovering at the edge of reach, in and out of it,
//      trembling at its edge (no near miss told meanwhile), jittering on the line and under a parked one, a hand in
//      above a parked one and lowered, parked by a hand still within a push's reach; at most one line a change
//  17  the tunables kept to their ranges, and a smaller reach and a longer glide doing what they say
//  18  hands.cpp's loadtest script, both ways and with both carries: home; parked, then pushed home
// Usage: pistolmagtest [-v]   (-v prints every check and every event)
// The exit code is the number of failed checks.

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

#include "../src/pistolmag.h"
#include "../src/pistolposes.h"

namespace {

constexpr float kDt = 1.0f / 72.0f;
constexpr float kPi = 3.14159265f;
// The design's geometry (geom.exe on the recordings): the seat (the magazine's bind space -> gun space home), the
// grip's line (the seat's y), the magazine's top in its bind space, and s at the grip's bottom.
const float kSeat[12] = {1, 0, 0, 0, 0, 0.96137f, -0.27524f, 0.13766f, 0, 0.27524f, 0.96137f, -0.02470f};
const float kLine[3] = {0.0f, 0.96137f, 0.27524f};
const float kFront[3] = {0.0f, -0.27524f, 0.96137f};  // the seat's z: off the line towards the gun's front
const float kTopBind[3] = {-0.0001f, -0.0926f, 0.1001f};
constexpr float kMouth = -0.1108f;
const float kLeftPalmCentre[3] = {0.5182f, 1.1075f, 0.1756f};  // hands.cpp's: the bare left palm's vertex middle
const float kNan = std::numeric_limits<float>::quiet_NaN();

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
void Invert(const float* m, float* out) {
    float r[12];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i * 4 + j] = m[j * 4 + i];
    for (int i = 0; i < 3; ++i)
        r[i * 4 + 3] = -(r[i * 4] * m[3] + r[i * 4 + 1] * m[7] + r[i * 4 + 2] * m[11]);
    std::memcpy(out, r, sizeof(r));
}
void Rotation(float yawDeg, float pitchDeg, float rollDeg, float* m) {  // about y, then x, then z (degrees)
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
float Distance(const float* a, const float* b) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}
float Angle(const float* m) {  // how far a pose turns (degrees)
    return std::acos(std::fmin(std::fmax((m[0] + m[5] + m[10] - 1.0f) * 0.5f, -1.0f), 1.0f)) * 180.0f / kPi;
}
float AngleBetween(const float* a, const float* b) {  // how far b's turn is from a's (degrees)
    float inverse[12], d[12];
    Invert(a, inverse);
    Compose(inverse, b, d);
    return Angle(d);
}
float MostApart(const float* a, const float* b) {  // the largest difference of any number
    float most = 0.0f;
    for (int i = 0; i < 12; ++i)
        most = std::fmax(most, std::fabs(a[i] - b[i]));
    return most;
}
bool Finite(const float* m, int n = 12) {
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(m[i]))
            return false;
    return true;
}
// The magazine at s along the line, off it along the gun's x (lateral) and towards its front (front), turned about the
// gun's x through its top: gun space home -> gun space, from the design's geometry.
void OnLine(float s, float lateral, float turnDeg, float* inGun, float front = 0.0f) {
    float r[12];
    Rotation(0, turnDeg, 0, r);
    float top[3], turned[3];
    Transform(kSeat, kTopBind, top);
    for (int i = 0; i < 3; ++i)
        turned[i] = r[i * 4] * top[0] + r[i * 4 + 1] * top[1] + r[i * 4 + 2] * top[2];
    const float off[3] = {lateral, front * kFront[1], front * kFront[2]};
    std::memcpy(inGun, r, sizeof(r));
    for (int i = 0; i < 3; ++i)
        inGun[i * 4 + 3] = top[i] + s * kLine[i] + off[i] - turned[i];
}
// The magazine's top in gun space home, and where a magazine pose (gun space home -> view) has it.
void TopOf(const float* magPose, float* out) {
    float home[3];
    Transform(kSeat, kTopBind, home);
    Transform(magPose, home, out);
}

// How a Sim is set up: the carry (G2 unless said) and the tunables (the module's defaults unless said).
struct Setup {
    const float* carry = pistolposes::kOnPalmG2;
    float guide = pistolposes::kGuideG2;
    float snap = 0.065f, dwell = 0.4f, reach = 0.075f, angle = 55.0f, glide = 0.15f;
};
Setup C2() {
    Setup s;
    s.carry = nullptr;
    s.guide = 0.0f;
    return s;
}
Setup Game() {
    Setup s;
    s.carry = pistolposes::kOnPalmGamePinch;
    s.guide = 0.0f;
    return s;
}
Setup Tuned(Setup s, float snap, float dwell) {
    s.snap = snap;
    s.dwell = dwell;
    return s;
}

// The pistol, the left hand and the grip, and what the module said and drew.
struct Sim {
    float pistolPose[12] = {};
    float glove[12] = {};
    bool left = true, pistol = true, tracked = true, handFree = true, atPouch = false;
    bool wellEmpty = true;  // the pistol's own magazine out (X); a seat puts it back
    float squeeze = 0.0f, apart = 0.25f;
    pistolmag::Result r;
    std::vector<std::string> events;
    int frame = 0, seats = 0, tries = 0, catches = 0, misses = 0, caughtAt = -1;
    pistolmag::Result lastTry, lastCatch, lastMiss;
    bool sane = true;  // every drawn pose a number; a magazine on the line never past home
    // The hand in gun space as the try began (caught, or a parked one pushed on) and the magazine's s then: the push
    // goes by how far the palm goes up the line from there (Push moves the hand so), and where the hand had it in the
    // hand when it was caught (s).
    float tryGlove[12] = {};
    float tryS = 0.0f, handS = 0.0f, caughtS = 0.0f;
    // What was drawn: the magazine's top and the hand's palm (as hands.cpp blends it), the most either moved in a frame
    // since Watch(); the most a magazine drawn in the hand was off where the hand has it with the carry's hold; how
    // many times the fingers (the carry's or the flat base's) and the hand (the controller's or the one drawn on the
    // magazine) turned over.
    float top[3] = {}, palm[3] = {};
    bool topBefore = false, palmBefore = false;
    float topStep = 0.0f, palmStep = 0.0f, offInHand = 0.0f;
    int flatTurns = 0, handTurns = 0;
    bool wasFlat = false, wasDrawn = false;

    explicit Sim(const Setup& s = Setup()) {
        pistolmag::Clear();
        pistolmag::SetCarry(s.carry, s.guide);
        pistolmag::SetSnap(s.snap);
        pistolmag::SetDwell(s.dwell);
        pistolmag::SetCatch(s.reach, s.angle);
        pistolmag::SetGlide(s.glide);
        pistolmag::SetSeat(kSeat);
        Rotation(25, -12, 8, pistolPose);  // out front-right, a little down and canted
        pistolPose[3] = 0.16f, pistolPose[7] = -0.22f, pistolPose[11] = -0.38f;
        MagAt(-0.45f);
        // The module sees the grip empty before anything's taken (an earlier Sim's hand eases off meanwhile).
        for (int i = 0; i < 40; ++i)
            Step();
        events.clear();
        Watch();
        flatTurns = handTurns = 0;
    }
    void Note(const char* e) {
        events.push_back(e);
        if (g_verbose)
            std::printf("    [%4d] %s (s %+.3f)\n", frame, e, r.s);
    }
    bool Has(const char* e) const {
        for (const std::string& x : events)
            if (x.find(e) != std::string::npos)
                return true;
        return false;
    }
    int Count(const char* e) const {
        int n = 0;
        for (const std::string& x : events)
            n += x.find(e) != std::string::npos;
        return n;
    }
    // From now on: the most the drawn magazine and hand move in a frame, and the most a magazine in the hand is off it.
    void Watch() {
        topStep = palmStep = offInHand = 0.0f;
        topBefore = palmBefore = false;
    }
    void Step(bool reached = false, float dt = kDt) {
        ++frame;
        pistolmag::Frame f;
        f.dt = dt;
        f.pistol = pistol;
        f.pistolTracked = tracked;
        std::memcpy(f.pistolPose, pistolPose, sizeof(pistolPose));
        f.left = left;
        std::memcpy(f.glove, glove, sizeof(glove));
        f.leftGrip = {glove[3], glove[7], glove[11]};
        f.squeeze = squeeze;
        f.apart = apart;
        f.reached = reached;
        f.atPouch = atPouch;
        f.handFree = handFree;
        f.wellEmpty = wellEmpty;
        f.spare = true;
        const float before = pistolmag::Holding() && !pistolmag::Parked() ? 1.0f : 0.0f;
        r = pistolmag::Update(f);
        if (r.took)
            Note("took");
        if (reached && !r.took && *r.note)
            Note((std::string("no take: ") + r.note).c_str());
        if (r.missed) {
            char text[160];
            std::snprintf(text, sizeof(text), "missed: %.1f cm from the mouth, %.0f degrees (%s)", r.missFrom * 100,
                          r.missTurned, r.missWhy);
            Note(text);
            lastMiss = r;
            ++misses;
        }
        if (r.caught) {
            char text[96];
            std::snprintf(text, sizeof(text), "caught %.1f cm from the mouth, %.0f degrees", r.caughtFrom * 100,
                          r.caughtTurned);
            Note(text);
            lastCatch = r;
            ++catches;
            caughtAt = frame;
            caughtS = handS;
        }
        if (r.caught || r.pushed) {  // the try's start: the hand as it is, the magazine on the line at r.s
            float inverse[12];
            Invert(pistolPose, inverse);
            Compose(inverse, glove, tryGlove);
            tryS = r.s;
        }
        if (r.offLine)
            Note("off line");
        if (r.parked)
            Note("parked");
        if (r.pushed)
            Note("pushed");
        if (r.letGo)
            Note("let go");
        if (r.putBack)
            Note("put back");
        if (r.cancelled)
            Note("cancelled");
        if (r.seated) {
            Note(r.dwelt ? "seated (dwell)" : "seated");
            wellEmpty = false;
            ++seats;
        }
        if (r.tried) {
            char text[96];
            std::snprintf(text, sizeof(text), "tried: nearest %+.1f cm, %.0f cm apart", r.closest * 100,
                          r.apartThen * 100);
            Note(text);
            lastTry = r;
            ++tries;
        }
        if ((r.mag && !Finite(r.magPose)) || (r.gloveBlend > 0.0f && !Finite(r.glove)) || !std::isfinite(r.s) ||
            !std::isfinite(r.flat) || !std::isfinite(r.push) || !std::isfinite(r.fingers) ||
            !std::isfinite(r.gloveBlend))
            sane = false;
        if (r.mag && pistolmag::Holding() && r.s > 1e-6f && !(r.holding && !before))  // on the line: past home
            sane = false;
        // What's drawn: the magazine's top, and the palm as hands.cpp blends the controller's hand with the drawn one.
        if (r.mag && Finite(r.magPose)) {
            float now[3];
            TopOf(r.magPose, now);
            if (topBefore)
                topStep = std::fmax(topStep, Distance(now, top));
            std::memcpy(top, now, sizeof(top));
            topBefore = true;
        } else {
            topBefore = false;
        }
        if (left && Finite(glove)) {
            float own[3], drawn[3], now[3];
            Transform(glove, kLeftPalmCentre, own);
            if (r.gloveBlend > 0.0f && Finite(r.glove))
                Transform(r.glove, kLeftPalmCentre, drawn);
            else
                std::memcpy(drawn, own, sizeof(drawn));
            for (int i = 0; i < 3; ++i)
                now[i] = own[i] + (drawn[i] - own[i]) * r.gloveBlend;
            if (palmBefore)
                palmStep = std::fmax(palmStep, Distance(now, palm));
            std::memcpy(palm, now, sizeof(palm));
            palmBefore = true;
        } else {
            palmBefore = false;
        }
        // In the hand, the magazine is where the hand has it (the carry's hold) unless it's gliding back out.
        if (r.mag && r.holding && caughtAt < 0 && Finite(r.magPose)) {
            float holds[12];
            pistolmag::GloveFor(r.magPose, false, holds);
            offInHand = std::fmax(offInHand, MostApart(holds, glove));
        }
        const bool flat = r.flat > 0.5f, drawnOn = r.gloveBlend > 0.5f;
        flatTurns += flat != wasFlat;
        handTurns += drawnOn != wasDrawn;
        wasFlat = flat, wasDrawn = drawnOn;
    }
    void Steps(int n) {
        for (int i = 0; i < n; ++i)
            Step();
    }
    // The left hand holding the magazine at s along the line (lateral: off it along the gun's x; front: towards the
    // gun's front; turn: degrees about the gun's x), with the carry's hold or the flat base's (an open palm under it).
    void MagAt(float s, float lateral = 0.0f, float turnDeg = 0.0f, bool flat = false, float front = 0.0f) {
        float inGun[12], magPose[12];
        OnLine(s, lateral, turnDeg, inGun, front);
        Compose(pistolPose, inGun, magPose);
        pistolmag::GloveFor(magPose, flat, glove);
        handS = s;
    }
    // The magazine carried along the line from s0 to s1 at speed (m/s).
    void Carry(float s0, float s1, float speed, float lateral = 0.0f, float turn = 0.0f, bool flat = false) {
        const int n = std::max(1, static_cast<int>(std::fabs(s1 - s0) / speed / kDt));
        for (int i = 1; i <= n; ++i) {
            MagAt(s0 + (s1 - s0) * i / n, lateral, turn, flat);
            Step();
        }
    }
    // Once it's caught (or pushed on from parked): the hand where its push has the magazine's top at s (as it was then,
    // moved that much up the line).
    void PushTo(float s) {
        const float d = s - tryS;
        const float up[12] = {1, 0, 0, kLine[0] * d, 0, 1, 0, kLine[1] * d, 0, 0, 1, kLine[2] * d};
        float inGun[12];
        Compose(up, tryGlove, inGun);
        Compose(pistolPose, inGun, glove);
    }
    // The magazine brought along a path in the hand (s, lateral, turn and front each going from a to b at speed m/s
    // along the line or the most any of them moves) until it's caught: true if it was.
    bool UntilCaught(float s0, float s1, float speed, float lateral0 = 0.0f, float lateral1 = 0.0f, float turn0 = 0.0f,
                     float turn1 = 0.0f, float front0 = 0.0f, float front1 = 0.0f) {
        const float far =
            std::fmax(std::fmax(std::fabs(s1 - s0), std::fabs(lateral1 - lateral0)), std::fabs(front1 - front0));
        const int n = std::max(1, static_cast<int>(far / speed / kDt));
        for (int i = 1; i <= n; ++i) {
            const float k = static_cast<float>(i) / n;
            MagAt(s0 + (s1 - s0) * k, lateral0 + (lateral1 - lateral0) * k, turn0 + (turn1 - turn0) * k, false,
                  front0 + (front1 - front0) * k);
            Step();
            if (r.caught)
                return true;
        }
        return false;
    }
    void Push(float s0, float s1, float speed) {
        const int n = std::max(1, static_cast<int>(std::fabs(s1 - s0) / speed / kDt));
        for (int i = 1; i <= n; ++i) {
            PushTo(s0 + (s1 - s0) * i / n);
            Step();
        }
    }
    // The magazine brought in the hand from a to b (s, lateral and front as MagAt has them) over that many seconds,
    // eased as a hand moves: from rest to rest, its top fastest half way (1.5 times the average), caught or not.
    void Ease(float s0, float s1, float lateral0, float lateral1, float front0, float front1, float seconds) {
        const int n = std::max(1, static_cast<int>(std::lround(seconds / kDt)));
        for (int i = 1; i <= n; ++i) {
            const float u = static_cast<float>(i) / n, k = u * u * (3.0f - 2.0f * u);
            MagAt(s0 + (s1 - s0) * k, lateral0 + (lateral1 - lateral0) * k, 0.0f, false, front0 + (front1 - front0) * k);
            Step();
        }
    }
    // The push brought to s and held there until the snap takes the magazine on past it (true; at most 10 frames).
    bool UntilSnap(float s) {
        for (int i = 0; i < 10; ++i) {
            PushTo(s);
            Step();
            if (r.s > s + 0.0005f)
                return true;
        }
        return false;
    }
    // A magazine taken at the pelvis and brought up the line, caught as it comes within 7.5 cm of the grip's bottom
    // (the hand stopping 0.6 cm past where it's caught), glided in and the hand drawn over on the flat base: its top
    // about 9 cm short of home.
    void TakeAndCatch() {
        MagAt(-0.45f);
        squeeze = 1.0f;
        Step(true);
        Carry(-0.45f, -0.20f, 0.6f);
        Carry(-0.20f, -0.18f, 0.2f);
        Steps(12);
    }
    void Take() {
        MagAt(-0.45f);
        squeeze = 1.0f;
        Step(true);
    }
    // Where the hand drawn under the magazine puts the bare palm's middle, in gun space.
    void DrawnPalm(float* out) const {
        float inverse[12], inGun[12];
        Invert(pistolPose, inverse);
        Compose(inverse, r.glove, inGun);
        Transform(inGun, kLeftPalmCentre, out);
    }
    // How far the drawn magazine is off its place on the line at r.s (m: the largest difference of any number).
    float OffLine() const {
        float inGun[12], at[12];
        pistolmag::PlaceOnLine(r.s, 0.0f, inGun);
        Compose(pistolPose, inGun, at);
        return MostApart(at, r.magPose);
    }
};

void Empty() {
    std::printf("1 empty: a magazine from the pelvis into the grip\n");
    Sim m;
    m.MagAt(-0.45f, 0.02f, 20.0f);  // the hand at the pelvis
    m.squeeze = 1.0f;
    m.Step(true);
    Check(m.Has("took") && m.r.mag && m.r.holding && m.r.fingers == 1.0f, "the squeeze at the pelvis takes a magazine");
    Check(m.r.flat == 0.0f && m.r.gloveBlend == 0.0f, "carried in G2's fingers, the hand the controller's own");
    m.Watch();
    m.Carry(-0.45f, -0.17f, 0.8f);
    Check(m.Has("caught") && m.catches == 1 && m.lastCatch.caughtFrom <= 0.0751f && m.lastCatch.caughtFrom > 0.06f,
          "caught as it came within reach of the grip's bottom (%.1f cm from it)", m.lastCatch.caughtFrom * 100);
    Check(m.offInHand < 1e-5f, "until then it stayed where the hand has it, in G2's place (%.1e off)", m.offInHand);
    m.Steps(12);
    Check(m.r.flat == 1.0f && m.r.push == 0.0f && m.OffLine() < 1e-4f && m.r.s > kMouth + 0.0149f,
          "0.17 s on: glided onto the line %.1f cm into the grip, the fingers open on the flat base",
          (m.r.s - kMouth) * 100);
    float flatBase[12];
    pistolmag::GloveFor(m.r.magPose, true, flatBase);
    Check(m.r.gloveBlend == 1.0f && MostApart(flatBase, m.r.glove) < 1e-4f,
          "the hand drawn holding it on the flat base (%.1e off B), %.0f degrees off the controller's",
          MostApart(flatBase, m.r.glove), AngleBetween(m.glove, m.r.glove));
    m.Push(m.r.s, -0.02f, 0.3f);
    m.Steps(16);
    Check(m.Has("seated") && !m.wellEmpty && m.Count("seated") == 1, "pushed on: home");
    Check(!pistolmag::Holding() && !m.r.mag && !m.r.holding, "nothing in the hand after: the pistol's own is drawn");
    Check(m.tries == 1 && m.lastTry.seated && m.lastTry.closest >= -0.065f,
          "one try at the grip, seated, the push %.1f cm from home at the nearest", -m.lastTry.closest * 100);
    m.Steps(12);
    Check(m.r.gloveBlend == 0.0f && m.r.fingers == 0.0f,
          "the hand back where the controller has it the glide's time after");
    Check(m.topStep < 0.02f && m.palmStep < 0.025f,
          "nothing drawn jumped: the magazine %.1f cm a frame at most, the palm %.1f", m.topStep * 100,
          m.palmStep * 100);
    Check(m.sane, "everything drawn a number, nothing past home");
}

void Refused() {
    std::printf("2 a squeeze at the pelvis that takes nothing\n");
    Sim m;
    m.wellEmpty = false;
    m.Step();
    m.MagAt(-0.45f);
    m.squeeze = 1.0f;
    m.Step(true);
    Check(m.Has("X first") && !pistolmag::Holding(), "the magazine in: nothing taken (X first)");
    Sim b;
    b.handFree = false;
    b.MagAt(-0.45f);
    b.squeeze = 1.0f;
    b.Step(true);
    Check(b.Has("busy") && !pistolmag::Holding(), "the hand on the pistol or its slide: nothing taken (busy)");
    Sim p;
    p.pistol = false;
    p.MagAt(-0.45f);
    p.squeeze = 1.0f;
    p.Step(true);
    Check(p.Has("not in hand") && !pistolmag::Holding(), "no pistol in hand: nothing taken");
    Sim n;
    n.MagAt(-0.45f);
    n.squeeze = 1.0f;
    n.Step();
    Check(!pistolmag::Holding() && n.events.empty(), "a squeeze that didn't reach the pelvis: nothing, nothing said");
}

void ParkAndPush() {
    std::printf("3 parked in the grip, pushed home\n");
    for (bool open : {false, true}) {
        Sim m;
        m.TakeAndCatch();
        m.Push(m.r.s, -0.075f, 0.2f);
        m.Steps(2);
        Check(std::fabs(m.r.s + 0.075f) < 0.001f, "caught and pushed 3.6 cm into the grip (s %.3f)", m.r.s);
        m.squeeze = 0.0f;  // the hand opens ...
        m.Steps(3);
        Check(pistolmag::Holding() && !m.Has("let go"), "let go of in the grip: it stays (no fall)");
        for (int i = 1; i <= 20; ++i) {  // ... and drops 10 cm
            m.PushTo(-0.075f - 0.005f * i);
            m.Step();
        }
        Check(pistolmag::Parked() && m.Has("parked") && std::fabs(m.r.s + 0.075f) < 0.001f,
              "parked where it was (s %.3f)", m.r.s);
        Check(!m.r.holding && m.r.mag && m.r.gloveBlend == 0.0f && m.r.fingers == 0.0f,
              "the hand free while it's parked, back on the controller; the magazine drawn in the grip");
        m.Carry(-0.20f, 0.01f, 1.0f, 0.0f, 0.0f, open);  // a palm comes up under it and pushes it home
        m.Steps(14);
        Check(m.Has("pushed") && m.Has("seated") && !m.wellEmpty, "pushed home by the palm %s",
              open ? "open under it (as the flat base would hold it)" : "holding it as the carry would");
        Check(!m.Has("let go") && m.sane, "never fell; everything drawn a number, nothing past home");
    }
}

void Drops() {
    std::printf("4 the drops\n");
    Sim t;
    t.Take();
    for (int i = 0; i < 6; ++i) {
        t.MagAt(-0.45f, 0.01f * i);
        t.Step();
    }
    t.squeeze = 0.0f;
    t.Step();
    const float* v = t.r.dropVelocity;
    const float speed = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    Check(t.Has("let go") && speed > 0.3f && !pistolmag::Holding(),
          "let go while carried: it falls at the hand's speed (%.2f m/s)", speed);
    Sim a;
    a.Take();
    a.Steps(3);
    a.atPouch = true;
    a.squeeze = 0.0f;
    a.Step();
    Check(a.Has("put back") && !a.Has("let go") && !pistolmag::Holding(), "let go at the pelvis: put back, no fall");
    Sim n;
    n.Take();
    n.Carry(-0.45f, -0.20f, 0.6f);  // 8.9 cm under the grip: out of reach
    n.squeeze = 0.0f;
    n.Step();
    Check(n.Has("let go") && !n.Has("caught") && n.wellEmpty,
          "let go 8.9 cm under the grip: it falls, nothing goes in");
    Sim w;
    w.Take();
    w.Carry(-0.45f, -0.20f, 0.6f);
    w.MagAt(-0.18f);  // in one frame into reach, the grip let go of as it gets there
    w.squeeze = 0.0f;
    w.Step();
    w.Steps(12);
    Check(w.Has("caught") && !w.Has("let go") && pistolmag::Holding() && w.r.s > kMouth + 0.0149f,
          "let go as it comes within reach: caught all the same, it glides in (%.1f cm into the grip)",
          (w.r.s - kMouth) * 100);
    w.Carry(-0.18f, -0.30f, 0.6f);
    Check(w.Has("parked") && !w.Has("let go"), "the open hand dropped away: it's left in the grip (parked), no fall");
}

void Catches() {
    std::printf("5 the magnetic catch from any direction\n");
    for (int c = 0; c < 2; ++c) {
        const Setup carry = c ? C2() : Setup();
        const char* name = c ? "C2" : "G2";
        // Each brought in until it's caught, then held where it was: 14 frames on, it's glided onto the line.
        Sim b(carry);
        b.Take();
        b.Carry(-0.45f, -0.25f, 0.6f);
        const bool below = b.UntilCaught(-0.25f, -0.13f, 0.4f);
        b.Steps(14);
        Check(below && b.lastCatch.caughtFrom <= 0.0751f && b.OffLine() < 1e-4f,
              "%s lined up from below: caught %.1f cm under the grip, glided onto the line", name,
              b.lastCatch.caughtFrom * 100);
        Sim s(carry);
        s.Take();
        const bool side = s.UntilCaught(-0.45f, kMouth, 0.4f, 0.06f, 0.06f);  // up 6 cm to the side of the grip
        s.Steps(14);
        Check(side && s.lastCatch.caughtFrom <= 0.0751f && s.OffLine() < 1e-4f,
              "%s 6 cm to the side: caught as it came up beside the grip (%.1f cm from its bottom), glided across onto "
              "the line",
              name, s.lastCatch.caughtFrom * 100);
        Sim t(carry);
        t.Take();
        t.Carry(-0.45f, -0.30f, 0.6f, 0.0f, 45.0f);
        const bool tilted = t.UntilCaught(-0.30f, -0.13f, 0.4f, 0.0f, 0.0f, 45.0f, 45.0f);
        t.Steps(14);
        Check(tilted && std::fabs(t.lastCatch.caughtTurned - 45.0f) < 0.5f && t.OffLine() < 1e-4f,
              "%s 45 degrees tilted: caught (%.0f), straightened onto the line", name, t.lastCatch.caughtTurned);
        Sim f(carry);
        f.Take();
        f.MagAt(kMouth, 0.0f, 0.0f, false, 0.15f);
        f.Steps(3);
        const bool front = f.UntilCaught(kMouth, kMouth, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.15f, 0.03f);
        f.Steps(14);
        Check(front && f.lastCatch.caughtFrom > 0.065f && f.OffLine() < 1e-4f,
              "%s from the front: caught %.1f cm in front of the grip's bottom, glided back onto the line", name,
              f.lastCatch.caughtFrom * 100);
    }
    // The gun brought down onto a still hand: in gun space the magazine goes up the line; then held there, the dwell
    // takes it home.
    Sim g;
    g.Take();
    g.MagAt(-0.30f);
    g.Steps(2);
    float lineView[3];  // the grip's line in the view
    for (int i = 0; i < 3; ++i)
        lineView[i] =
            g.pistolPose[i * 4] * kLine[0] + g.pistolPose[i * 4 + 1] * kLine[1] + g.pistolPose[i * 4 + 2] * kLine[2];
    for (int i = 0; i < 36 && !g.Has("caught"); ++i) {  // down at 0.5 m/s, the hand where it is
        for (int k = 0; k < 3; ++k)
            g.pistolPose[k * 4 + 3] -= lineView[k] * 0.5f * kDt;
        g.Step();
    }
    Check(g.Has("caught") && g.lastCatch.caughtFrom <= 0.0751f, "the gun brought down onto a still magazine: caught");
    g.Steps(50);
    Check(g.Has("seated (dwell)"), "held there with the grip squeezed: home by itself (the dwell)");
    // A top already in the grip: jammed in crooked, then straightened there (and held once it's caught).
    Sim j;
    j.Take();
    j.Carry(-0.45f, -0.25f, 0.6f, 0.02f, 70.0f);
    j.Carry(-0.25f, -0.06f, 0.5f, 0.02f, 70.0f);  // up beside it, tilted 70: not caught
    const bool notYet = !j.Has("caught");
    const bool straightened = j.UntilCaught(-0.06f, -0.06f, 0.04f, 0.02f, 0.0f, 70.0f, 0.0f);
    Check(notYet && straightened && j.lastCatch.caughtTurned <= 55.01f,
          "a top already in the grip, tilted 70 degrees: not caught; straightened there, caught at %.0f",
          j.lastCatch.caughtTurned);
    j.Steps(26);
    Check(j.Has("seated") && !j.Has("dwell"), "then home: it was within the snap already");
    // ... or found there: the pistol not seen while the hand put it there.
    Sim l;
    l.Take();
    l.tracked = false;
    l.Carry(-0.45f, -0.07f, 1.0f);
    l.tracked = true;
    l.Step();
    const bool waited = !l.Has("caught");
    l.Step();
    Check(waited && l.Has("caught") && std::fabs(l.r.s + 0.07f) < 0.002f,
          "found with its top in the grip after the pistol wasn't seen: caught there the frame after (s %.3f)", l.r.s);
    // Not: a fast swipe past, 4 cm under the grip's bottom at 2.5 m/s.
    Sim w;
    w.Take();
    w.MagAt(kMouth - 0.04f, -0.30f);
    w.Steps(3);
    for (int i = 1; i <= 17; ++i) {
        w.MagAt(kMouth - 0.04f, -0.30f + 0.60f * i / 17);
        w.Step();
    }
    Check(!w.Has("caught") && w.misses == 1 && std::strstr(w.lastMiss.missWhy, "fast"),
          "a swipe past at 2.5 m/s, 4 cm under the grip: not caught, its near miss told once (%s)", w.lastMiss.missWhy);
    // Not: a one-frame tracking jump into reach.
    Sim k;
    k.Take();
    k.Carry(-0.45f, -0.30f, 0.6f);
    k.MagAt(-0.12f);
    k.Step();
    k.MagAt(-0.30f);
    k.Steps(5);
    Check(!k.Has("caught"), "a one-frame tracking jump into reach: not caught");
    // Not: turned past the angle.
    Sim a;
    a.Take();
    a.Carry(-0.45f, -0.14f, 0.5f, 0.0f, 60.0f);
    a.Steps(20);
    a.Carry(-0.14f, -0.30f, 0.5f, 0.0f, 60.0f);
    Check(!a.Has("caught") && a.misses == 1 && std::fabs(a.lastMiss.missTurned - 60.0f) < 0.5f &&
              std::strstr(a.lastMiss.missWhy, "turned too far from"),
          "tilted 60 degrees (past 55): not caught, told once as it went (%s)", a.lastMiss.missWhy);
    // Not: plainly going away (held in reach tilted, then straightened as it's pulled away at 1.2 m/s).
    Sim p;
    p.Take();
    p.Carry(-0.45f, -0.14f, 0.5f, 0.0f, 70.0f);
    p.Steps(5);
    for (int i = 1; i <= 10; ++i) {
        p.MagAt(-0.14f - 0.0167f * i, 0.0f, std::fmax(70.0f - 20.0f * i, 0.0f));
        p.Step();
    }
    Check(!p.Has("caught"), "straightened as it's pulled away at 1.2 m/s: not caught");
    // Not: out of reach.
    Sim o;
    o.Take();
    o.Carry(-0.45f, -0.197f, 0.5f);
    o.Steps(72);
    Check(!o.Has("caught") && pistolmag::Holding() && o.misses == 0,
          "held 8.6 cm under the grip: out of reach, nothing");

    // Followed through: caught 6-7.5 cm off, a real hand carries on towards the grip (the review: every catch from the
    // side, the front or a diagonal with the hand still moving was pulled back out, or parked out of the palm's reach;
    // the checks above hold the hand still from the catch on). Each from 15 cm off to 1 cm under the grip's bottom,
    // eased from rest to rest, then held there gripped: caught once, never pulled out or parked, the drawn hand on it
    // and the fingers flat once each (and off again after), home (by the dwell, or pushed by a diagonal's way up).
    struct Way {
        const char* name;
        float s, lateral, front, seconds;
    };
    const Way ways[] = {
        {"from the side at 1.5 m/s", kMouth - 0.01f, 0.15f, 0.0f, 0.15f},
        {"from the side at 0.9 m/s", kMouth - 0.01f, 0.15f, 0.0f, 0.25f},
        {"from the side at 0.4 m/s", kMouth - 0.01f, 0.15f, 0.0f, 0.6f},
        {"from the side at 0.2 m/s", kMouth - 0.01f, 0.15f, 0.0f, 1.0f},
        {"from the front at 0.9 m/s", kMouth - 0.01f, 0.0f, 0.15f, 0.25f},
        {"from the front at 0.4 m/s", kMouth - 0.01f, 0.0f, 0.15f, 0.6f},
        {"from below and the side (45 degrees) at 1.1 m/s", kMouth - 0.155f, 0.15f, 0.0f, 0.3f},
        {"from below and the front (45 degrees) at 1.1 m/s", kMouth - 0.155f, 0.0f, 0.15f, 0.3f},
        {"down and in from 6 cm above the mouth, the side", kMouth + 0.06f, 0.15f, 0.0f, 0.3f},
        {"down and in from 6 cm above the mouth, the front", kMouth + 0.06f, 0.0f, 0.15f, 0.6f},
    };
    for (int c = 0; c < 2; ++c) {
        const Setup carry = c ? C2() : Setup();
        const char* name = c ? "C2" : "G2";
        for (const Way& way : ways) {
            Sim m(carry);
            m.Take();
            m.MagAt(-0.45f, way.lateral, 0.0f, false, way.front);  // to the start well away from the grip
            m.Steps(3);
            m.Ease(-0.45f, way.s, way.lateral, way.lateral, way.front, way.front, 0.6f);
            m.Steps(3);
            const bool before = m.catches == 0;
            m.flatTurns = m.handTurns = 0;
            m.Ease(way.s, kMouth - 0.01f, way.lateral, 0.0f, way.front, 0.0f, way.seconds);
            m.Steps(72);
            Check(before && m.catches == 1 && !m.Has("off line") && !m.Has("parked") && m.seats == 1 &&
                      m.handTurns == 2 && m.flatTurns == 2,
                  "%s %s, carrying on in after the catch (%.1f cm off): never pulled out nor parked, home%s", name,
                  way.name, m.lastCatch.caughtFrom * 100, m.Has("dwell") ? " (the dwell)" : " (pushed)");
        }
        // ... and pushed on: from the side, then on up 5 cm into the grip (no dwell: the push takes it home).
        Sim up(Tuned(carry, 0.065f, 0.0f));
        up.Take();
        up.MagAt(-0.45f, 0.15f);
        up.Steps(3);
        up.Ease(-0.45f, kMouth - 0.01f, 0.15f, 0.15f, 0.0f, 0.0f, 0.6f);
        up.Steps(3);
        up.Ease(kMouth - 0.01f, kMouth - 0.01f, 0.15f, 0.0f, 0.0f, 0.0f, 0.25f);
        up.Ease(kMouth - 0.01f, kMouth + 0.04f, 0.0f, 0.0f, 0.0f, 0.0f, 0.3f);
        up.Steps(20);
        Check(up.catches == 1 && !up.Has("off line") && !up.Has("parked") && up.Has("seated") && !up.Has("dwell"),
              "%s from the side, then on up 5 cm into the grip: pushed home", name);
        // ... and past the line 5 cm before coming back onto it.
        Sim past(carry);
        past.Take();
        past.MagAt(-0.45f, 0.15f);
        past.Steps(3);
        past.Ease(-0.45f, kMouth - 0.01f, 0.15f, 0.15f, 0.0f, 0.0f, 0.6f);
        past.Steps(3);
        past.Ease(kMouth - 0.01f, kMouth - 0.01f, 0.15f, -0.05f, 0.0f, 0.0f, 0.25f);
        past.Ease(kMouth - 0.01f, kMouth - 0.01f, -0.05f, 0.0f, 0.0f, 0.0f, 0.2f);
        past.Steps(72);
        Check(past.catches == 1 && !past.Has("off line") && !past.Has("parked") && past.Has("seated (dwell)"),
              "%s from the side, 5 cm past the line and back onto it: kept, home", name);
    }
    // Leaving it still leaves it: caught from the side and held till it's glided in (no dwell), then the hand taken
    // 10 cm back out across, gripped or let go, or dropped 10 cm: parked each time; going back out across as it glides
    // in, gripped: back in the hand.
    for (int leave = 0; leave < 4; ++leave) {
        Sim m(Tuned(Setup(), 0.065f, 0.0f));
        m.Take();
        m.UntilCaught(-0.45f, kMouth, 0.4f, 0.06f, 0.06f);
        const float at = m.handS;
        if (leave >= 2)
            m.squeeze = 0.0f;
        m.Steps(14);
        if (leave % 2 == 0)
            m.Ease(at, at, 0.06f, 0.16f, 0.0f, 0.0f, 0.3f);
        else
            m.Ease(at, at - 0.10f, 0.06f, 0.06f, 0.0f, 0.0f, 0.3f);
        m.Steps(5);
        Check(m.Has("parked") && !m.Has("off line") && !m.Has("seated") && pistolmag::Parked(),
              "caught from the side, then %s %s: parked in the grip", leave >= 2 ? "let go and" : "gripped,",
              leave % 2 == 0 ? "the hand 10 cm back out across" : "the hand dropped 10 cm");
    }
    // A pass under the grip, 1 cm below its bottom, carried on 15 cm past it at 0.5 m/s: caught, and left part way in.
    Sim pass;
    pass.Take();
    pass.MagAt(kMouth - 0.01f, -0.15f);
    pass.Steps(3);
    for (int i = 1; i <= 43; ++i) {
        pass.MagAt(kMouth - 0.01f, -0.15f + 0.30f * i / 43);
        pass.Step();
    }
    pass.Steps(5);
    Check(pass.catches == 1 && pass.Has("parked") && !pass.Has("off line") && !pass.Has("let go") &&
              pistolmag::Parked(),
          "a pass under the grip carried on 15 cm past it at 0.5 m/s: caught, left part way in (parked)");
    Sim back;
    back.Take();
    back.UntilCaught(-0.45f, kMouth, 0.4f, 0.06f, 0.06f);
    const float from = back.handS;
    back.Ease(from, from, 0.06f, 0.13f, 0.0f, 0.0f, 0.1f);
    back.Steps(3);
    Check(back.Has("off line") && !back.Has("parked") && pistolmag::Holding() && !pistolmag::Parked(),
          "caught from the side, then 7 cm back out across as it glides in, gripped: back in the hand");
    // Pulled back out with its top still in the grip, then pushed straight back up it: caught again as it heads in
    // (the review: not until it had been out of reach, 7.5 cm and more, so pushing on up caught nothing).
    Sim in;
    in.Take();
    in.tracked = false;  // the pistol not seen as the hand puts its top 4 cm into the grip
    in.Carry(-0.45f, -0.07f, 1.0f);
    in.tracked = true;
    in.Steps(2);
    const bool caught = in.Has("caught");
    in.MagAt(-0.11f);  // 4 cm back down at once as it glides in, gripped: its top still in the grip
    in.Steps(4);
    const bool out = in.Has("off line") && in.catches == 1;
    in.Ease(-0.11f, -0.075f, 0.0f, 0.0f, 0.0f, 0.0f, 0.2f);  // pushed straight back up 3.5 cm, then held (the dwell)
    in.Steps(50);
    Check(caught && out && in.catches == 2 && in.Has("seated"),
          "pulled back out with its top still in the grip, pushed straight back up it: caught again (%d), home",
          in.catches);
}

void GameReload() {
    std::printf("6 the game's own reload\n");
    Sim s;
    s.Take();
    s.Carry(-0.45f, -0.3f, 0.6f);
    s.wellEmpty = false;  // X held: the game's reload puts its own in
    s.Step();
    Check(s.Has("cancelled") && !pistolmag::Holding() && !s.Has("let go"), "the hand's magazine gone, no fall");
}

void Carries() {
    std::printf("7 the carries\n");
    // The same path with each carry (one Sim at a time: the module is the one hand's).
    struct Run {
        bool caught = false, seated = false, sane = false;
        int caughtAfter = 0;
        float offInHand = 0.0f, turnAtCatch = 0.0f, handTurn = 0.0f, magTurn = 0.0f, flat = 0.0f, topStep = 0.0f,
              palmStep = 0.0f;
    };
    Run run[2];
    for (int k = 0; k < 2; ++k) {
        Sim m(k ? C2() : Setup());
        Run& out = run[k];
        const int from = m.frame;
        m.Take();
        m.Watch();
        m.Carry(-0.45f, -0.20f, 0.6f);
        out.caught = m.UntilCaught(-0.20f, -0.17f, 0.2f);
        out.caughtAfter = m.caughtAt - from;
        out.offInHand = m.offInHand;
        out.turnAtCatch = AngleBetween(m.glove, m.r.glove);
        for (int i = 0; i < 14; ++i) {
            m.Step();
            float inverse[12], inGun[12];
            Invert(m.pistolPose, inverse);
            Compose(inverse, m.r.magPose, inGun);
            out.magTurn = std::fmax(out.magTurn, Angle(inGun));  // turned from home's: the line's own turn
            out.handTurn = std::fmax(out.handTurn, AngleBetween(m.glove, m.r.glove));
        }
        out.flat = m.r.flat;
        m.Push(m.r.s, -0.02f, 0.3f);
        m.Steps(16);
        out.seated = m.Has("seated");
        out.sane = m.sane;
        out.topStep = m.topStep, out.palmStep = m.palmStep;
    }
    Check(run[0].caught && run[1].caught && run[0].caughtAfter == run[1].caughtAfter,
          "G2 and C2 brought up the same way are caught in the same frame");
    Check(run[0].offInHand < 1e-5f && run[1].offInHand < 1e-5f,
          "and until then each stays where its hand has it (%.1e, %.1e)", run[0].offInHand, run[1].offInHand);
    Check(run[0].magTurn < 0.1f && run[1].magTurn < 0.1f,
          "lined up, the magazine never turns as it glides in (%.2f, %.2f degrees)", run[0].magTurn, run[1].magTurn);
    Check(run[0].turnAtCatch < 0.1f && std::fabs(run[0].handTurn - 51.8f) < 0.5f && run[0].flat == 1.0f,
          "G2: the hand drawn on it starts as the controller's (%.2f), turns %.1f degrees round it onto the flat base",
          run[0].turnAtCatch, run[0].handTurn);
    Check(run[1].turnAtCatch < 0.1f && run[1].handTurn < 0.1f && run[1].flat == 1.0f,
          "C2: its place is the flat base's, only the fingers open (the hand turns %.2f degrees)", run[1].handTurn);
    Check(run[0].seated && run[1].seated && run[0].sane && run[1].sane, "both pushed home");
    Check(run[0].topStep < 0.02f && run[1].topStep < 0.02f && run[0].palmStep < 0.025f && run[1].palmStep < 0.025f,
          "nothing jumped: the magazine %.1f / %.1f cm a frame at most, the palm %.1f / %.1f", run[0].topStep * 100,
          run[1].topStep * 100, run[0].palmStep * 100, run[1].palmStep * 100);
    Sim p(Game());
    p.Take();
    p.Carry(-0.45f, -0.20f, 0.6f);
    const bool pinch = p.UntilCaught(-0.20f, -0.17f, 0.2f);
    p.Steps(14);
    Check(pinch && std::fabs(AngleBetween(p.glove, p.r.glove) - 53.4f) < 0.5f && p.r.flat == 1.0f,
          "the game's pinch: caught as it's held, the hand drawn on it turned %.1f degrees onto the flat base",
          AngleBetween(p.glove, p.r.glove));
    // G2's index along its front reaches the grip's frame 3.25 cm past its bottom: pushed fast right after the catch,
    // the magazine waits 3.1 cm in until the hand drawn on it is over on the flat base; C2 goes straight on.
    float most2[2] = {-1.0f, -1.0f};
    bool waited = true, home[2] = {};
    for (int k = 0; k < 2; ++k) {
        Sim q(k ? C2() : Setup());
        q.Take();
        q.Carry(-0.45f, -0.20f, 0.6f);
        q.UntilCaught(-0.20f, -0.17f, 0.2f);
        const int at = q.caughtAt;
        for (int i = 1; i <= 30; ++i) {
            q.PushTo(std::fmin(q.lastCatch.s + 0.01f * i, -0.03f));  // 0.7 m/s up the line
            q.Step();
            if (q.frame - at < 10) {
                most2[k] = std::fmax(most2[k], q.r.s);
                if (k == 0)
                    waited = waited && q.r.s <= kMouth + pistolposes::kGuideG2 + 1e-4f;
            }
        }
        home[k] = q.Has("seated");
    }
    Check(waited && home[0],
          "G2 pushed fast at once: it waits %.1f cm in (at most %.1f) while the hand goes over, then goes home",
          pistolposes::kGuideG2 * 100, (most2[0] - kMouth) * 100);
    Check(most2[1] > kMouth + pistolposes::kGuideG2 + 0.005f && home[1],
          "C2 has no fingers up the grip: it goes straight on (%.1f cm in), home", (most2[1] - kMouth) * 100);
}

void Snap() {
    std::printf("8 the snap\n");
    Sim a(Tuned(Setup(), 0.02f, 0.0f));
    a.TakeAndCatch();
    Check(a.Has("caught") && !a.Has("seated"), "caught, glided in, not home (the snap at 2, no dwell)");
    a.Push(a.r.s, -0.025f, 0.4f);
    a.Steps(30);
    Check(!a.Has("seated") && pistolmag::Holding() && std::fabs(a.r.s + 0.025f) < 0.002f,
          "held 2.5 cm short with the snap at 2: it stays there on the line (s %.3f)", a.r.s);
    a.PushTo(-0.015f);
    a.Step();
    const int at = a.frame;
    while (!a.Has("seated") && a.frame < at + 20)
        a.Step();
    Check(a.Has("seated") && a.frame - at >= 8 && a.frame - at <= 13,
          "brought to 1.5 cm short: home by itself over the glide's time, seated %d frames on", a.frame - at);
    Sim one(Tuned(Setup(), 0.02f, 0.0f));
    one.TakeAndCatch();
    one.Push(one.r.s, -0.05f, 0.4f);
    one.Steps(3);
    one.PushTo(-0.01f);  // a single frame within the snap (the push), then back
    one.Step();
    one.PushTo(-0.05f);
    one.Steps(10);
    Check(!one.Has("seated"), "within it for one frame only: not home (s %.3f)", one.r.s);
    Sim j(Tuned(Setup(), 0.02f, 0.0f));
    j.TakeAndCatch();
    j.Push(j.r.s, -0.04f, 0.4f);
    j.Steps(3);
    const float before = j.r.s;
    j.PushTo(0.2f);  // a tracking jump: one frame 24 cm up
    j.Step();
    j.PushTo(-0.04f);
    j.Steps(30);
    Check(!j.Has("seated") && pistolmag::Holding() && std::fabs(j.r.s - before) < 0.001f,
          "a one-frame jump from 4 cm short moves nothing (two frames running: %.1f mm), not home",
          (j.r.s - before) * 1000);
    j.Push(-0.04f, -0.015f, 0.3f);
    j.Steps(14);
    Check(j.Has("seated"), "then pushed within the snap: home");
    Sim f(Tuned(Setup(), 0.005f, 0.0f));
    f.TakeAndCatch();
    f.Push(f.r.s, -0.015f, 0.4f);
    f.Steps(30);
    Check(!f.Has("seated") && std::fabs(f.r.s + 0.015f) < 0.002f, "seat 0.5: 1.5 cm short isn't near enough");
    f.Push(-0.015f, -0.003f, 0.2f);
    f.Steps(14);
    Check(f.Has("seated"), "seat 0.5: 3 mm short is");
    Sim w(Tuned(Setup(), 0.5f, 0.0f));  // a snap wider than the grip: kept to 10 cm
    w.Take();
    w.Carry(-0.45f, -0.20f, 0.6f);
    w.UntilCaught(-0.20f, -0.17f, 0.2f);
    w.Steps(30);
    Check(pistolmag::Settings().snap == 0.10f && w.Has("seated") && w.lastTry.closest < -0.09f,
          "seat 50 is kept to 10 cm: caught and glided in (%.1f cm short), it snaps at once", -w.lastTry.closest * 100);
    // Animated: eased from the speed it had to a stop at home, the hand drawn going with it, then back.
    Sim e;
    e.TakeAndCatch();
    std::vector<float> path;
    const float start = e.r.s;
    for (int i = 1; i <= 40 && !e.Has("seated"); ++i) {
        e.PushTo(std::fmin(start + 0.005f * i, -0.01f));  // 0.36 m/s
        e.Step();
        path.push_back(e.r.s);
    }
    float biggest = 0.0f, smallest = 1.0f;
    bool onward = true;
    for (size_t i = 1; i < path.size(); ++i) {
        biggest = std::fmax(biggest, path[i] - path[i - 1]);
        onward = onward && path[i] >= path[i - 1] - 1e-6f;
        if (path[i - 1] > -0.07f && path[i - 1] < -0.03f)  // the push into the snap and the snap's first half
            smallest = std::fmin(smallest, path[i] - path[i - 1]);
    }
    Check(e.Has("seated") && onward && biggest < 0.012f && path.back() == 0.0f,
          "pushed into the snap at 0.36 m/s: on up to home, never back, %.1f mm a frame at most", biggest * 1000);
    Check(smallest > 0.003f, "no hitch as it goes over to going by itself: at least %.1f mm a frame there",
          smallest * 1000);
    float palmHome[3], b2[3], inverse[12];
    e.DrawnPalm(palmHome);
    Invert(pistolposes::kOnPalmSeated, inverse);
    Transform(inverse, kLeftPalmCentre, b2);
    Check(e.r.seated && Distance(palmHome, b2) < 0.001f,
          "the hand drawn went with it: on B2 under it at home (%.2f mm)", Distance(palmHome, b2) * 1000);
    std::vector<float> blends;
    for (int i = 0; i < 14; ++i) {
        e.Step();
        blends.push_back(e.r.gloveBlend);
    }
    float drop = 0.0f;
    for (size_t i = 1; i < blends.size(); ++i)
        drop = std::fmax(drop, blends[i - 1] - blends[i]);
    Check(blends[0] > 0.9f && blends[7] > 0.0f && blends.back() == 0.0f && drop < 0.16f,
          "then back to the controller over the glide's time (%.0f%% at most in a frame)", drop * 100);
    Check(a.sane && j.sane && f.sane && w.sane && e.sane, "everything drawn a number, nothing past home");
}

void Dwell() {
    std::printf("9 the dwell\n");
    Sim d;
    d.TakeAndCatch();
    const int at = d.caughtAt;
    while (!d.Has("seated") && d.frame < at + 100)
        d.Step();
    const float seconds = (d.frame - at) * kDt;
    Check(d.Has("seated (dwell)") && seconds > 0.5f && seconds < 0.62f,
          "caught and held there gripped: home by itself %.2f s after the catch (0.4 held, 0.15 going)", seconds);
    Sim l;
    l.TakeAndCatch();
    l.squeeze = 0.0f;  // let go, the hand still under it
    l.Steps(72);
    Check(!l.Has("seated") && pistolmag::Holding() && !pistolmag::Parked(),
          "let go of, the hand still under it: it waits");
    l.Push(l.r.s, -0.05f, 0.3f);
    l.Steps(14);
    Check(l.Has("seated") && !l.Has("dwell"), "pushed on with the open hand: home");
    Sim b;
    b.Take();
    b.Carry(-0.45f, -0.20f, 0.6f);
    b.UntilCaught(-0.20f, -0.17f, 0.2f);
    const float from = b.r.s;
    for (int i = 1; i <= 30; ++i) {  // from the catch, the hand drifting back down 2 cm in 0.4 s, then held
        b.PushTo(from - 0.02f * i / 30);
        b.Step();
    }
    const bool none = !b.Has("seated");
    b.Steps(50);
    Check(none && b.Has("seated (dwell)") && !b.Has("parked"),
          "drawing back 2 cm: not by itself meanwhile; held still again, it is");
    Sim n(Tuned(Setup(), 0.065f, 0.0f));
    n.TakeAndCatch();
    n.Steps(144);
    Check(!n.Has("seated") && pistolmag::Holding(), "dwell 0: held there 2 s, never by itself");
}

void Tries() {
    std::printf("10 each try's nearest approach\n");
    Sim p(Tuned(Setup(), 0.02f, 0.0f));
    p.TakeAndCatch();
    const float from = p.r.s;
    for (int i = 1; i <= 20; ++i) {  // up to 6 cm short, the hands coming together as it goes
        const float s = from + (-0.06f - from) * i / 20;
        p.apart = 0.30f + s;
        p.PushTo(s);
        p.Step();
    }
    p.Steps(5);
    for (int i = 0; i < 20; ++i) {  // the hand drops away: parked
        p.PushTo(-0.06f - 0.005f * i);
        p.Step();
    }
    Check(p.Has("parked") && p.tries == 1 && std::fabs(p.lastTry.closest + 0.06f) < 0.001f &&
              std::fabs(p.lastTry.apartThen - 0.24f) < 0.001f,
          "parked: the try's nearest %.1f cm short (6), the hands %.1f cm apart then (24)", -p.lastTry.closest * 100,
          p.lastTry.apartThen * 100);
    Check(p.lastTry.parked, "told in the same frame as the park");
    Sim o;
    o.Take();
    o.Carry(-0.45f, -0.20f, 0.6f);
    o.UntilCaught(-0.20f, -0.17f, 0.2f);
    const float caughtAt = o.handS;
    for (int i = 1; i <= 6; ++i) {  // still gliding, the gripped hand pulled 9 cm aside
        o.MagAt(caughtAt, 0.015f * i);
        o.Step();
    }
    Check(o.Has("off line") && o.tries == 1 && o.lastTry.offLine && o.lastTry.closest < -0.09f,
          "pulled back out as it glided in: a try told, nearest %.1f cm short", -o.lastTry.closest * 100);
    Sim s(Tuned(Setup(), 0.02f, 0.0f));
    s.TakeAndCatch();
    s.Push(s.r.s, -0.012f, 0.4f);
    s.Steps(14);
    Check(s.tries == 1 && s.lastTry.seated && s.lastTry.closest > -0.0125f && s.lastTry.closest < -0.0115f,
          "seated by the snap: its try told, nearest %.1f cm short (1.2)", -s.lastTry.closest * 100);
    Sim g;
    g.TakeAndCatch();
    g.wellEmpty = false;
    g.Step();
    Check(g.Has("cancelled") && g.tries == 1, "gone (the game's reload): its try told too");
}

void PushHome() {
    std::printf("11 the push home (B2)\n");
    float onB[3], onB2[3], inverse[12];
    Invert(pistolposes::kOnPalmFlat, inverse);
    Transform(inverse, kLeftPalmCentre, onB);  // the palm's middle with the magazine home, on B
    Invert(pistolposes::kOnPalmSeated, inverse);
    Transform(inverse, kLeftPalmCentre, onB2);
    for (int c = 0; c < 2; ++c) {
        Sim m(Tuned(c ? C2() : Setup(), 0.005f, 0.0f));
        m.TakeAndCatch();
        m.Push(m.r.s, -0.05f, 0.4f);
        m.Steps(2);
        float palm[3], want[3];
        m.DrawnPalm(palm);
        for (int i = 0; i < 3; ++i)
            want[i] = onB[i] + m.r.s * kLine[i];
        Check(m.r.push == 0.0f && m.r.flat == 1.0f && m.r.gloveBlend == 1.0f && Distance(palm, want) < 0.001f,
              "%s 5 cm short: the flat base (push %.2f), the hand drawn on B (the palm %.2f mm off)", c ? "C2" : "G2",
              m.r.push, Distance(palm, want) * 1000);
        m.Push(-0.05f, -0.015f, 0.2f);
        m.Steps(2);
        Check(std::fabs(m.r.push - 0.5f) < 0.02f, "1.5 cm short: half the push home (%.2f)", m.r.push);
        m.Push(-0.015f, 0.0f, 0.2f);
        while (!m.Has("seated") && m.frame < 4000)
            m.Step();
        m.DrawnPalm(palm);  // the seat's frame: the hand still drawn under it at home
        Check(m.Has("seated") && m.r.seated && Distance(palm, onB2) < 0.001f,
              "home: the hand drawn on B2, the palm under the floorplate (%.2f mm off; (%.4f %.4f %.4f))",
              Distance(palm, onB2) * 1000, palm[0], palm[1], palm[2]);
        m.Steps(1);
        Check(m.r.gloveBlend > 0.0f && m.r.gloveBlend < 1.0f, "after the seat the hand eases back (%.2f)",
              m.r.gloveBlend);
    }
    const float geocheck[3] = {0.0093f, -0.1107f, 0.0038f};  // review\geocheck.txt: B2 as recorded
    Check(Distance(onB2, geocheck) < 0.0005f, "B2's palm as the review measured it (%.2f mm off)",
          Distance(onB2, geocheck) * 1000);
}

void Lost() {
    std::printf("12 the hand or the pistol lost\n");
    Sim h;
    h.Take();
    h.Steps(3);
    h.left = false;
    for (int i = 0; i < 36; ++i)  // half a second
        h.Step();
    Check(pistolmag::Holding() && !h.r.mag && !h.Has("cancelled"), "the hand lost half a second: kept, not drawn");
    h.left = true;
    h.Step();
    Check(h.r.mag && h.r.holding, "found again: in the hand");
    h.left = false;
    for (int i = 0; i < 80; ++i)
        h.Step();
    Check(h.Has("cancelled") && !pistolmag::Holding() && !h.Has("let go"), "lost over a second: gone, no fall");
    // On the line: the catch can leave the hand centimetres under the magazine, out of a parked push's reach, so a
    // moment lost keeps it on the line (it used to be parked at once).
    Sim w(Tuned(Setup(), 0.02f, 0.0f));
    w.TakeAndCatch();
    w.Push(w.r.s, -0.06f, 0.4f);
    w.left = false;
    w.Steps(3);
    Check(!w.Has("parked") && pistolmag::Holding() && !pistolmag::Parked() && w.r.mag,
          "lost on the line in the grip a few frames: it waits on the line, drawn");
    w.left = true;
    w.Push(-0.06f, -0.01f, 0.4f);
    w.Steps(14);
    Check(w.Has("seated") && !w.Has("parked"), "found again: the push goes on, home");
    Sim q(Tuned(Setup(), 0.02f, 0.0f));
    q.TakeAndCatch();
    q.left = false;
    q.Steps(19);
    Check(q.Has("parked") && pistolmag::Parked() && q.r.mag, "lost over a quarter of a second: parked there");
    Sim g;
    g.Take();
    g.Carry(-0.45f, -0.20f, 0.6f);
    g.UntilCaught(-0.20f, -0.17f, 0.2f);
    g.left = false;  // lost as it starts gliding in
    g.Steps(12);
    Check(!g.Has("parked") && g.OffLine() < 1e-4f && g.r.mag, "lost as it glides in: it goes on onto the line, drawn");
    g.left = true;
    g.Steps(50);
    Check(g.Has("seated (dwell)"), "found again, held there: home");
    Sim p;
    p.Take();
    p.pistol = false;
    for (int i = 0; i < 36; ++i)
        p.Step();
    Check(pistolmag::Holding() && !p.Has("cancelled"), "the pistol out of hand half a second: kept");
    p.pistol = true;
    p.Step();
    p.pistol = false;
    for (int i = 0; i < 80; ++i)
        p.Step();
    Check(p.Has("cancelled") && !pistolmag::Holding(), "out of hand over a second: gone");
    Sim t;
    t.Take();
    t.tracked = false;
    t.squeeze = 0.0f;
    t.Step();
    Check(t.Has("cancelled") && !t.Has("let go"), "let go while the pistol isn't tracked: gone, no fall");
    // Going home by itself (the snap) it needs nothing of the hand: a frame without the hand, the pistol's "in hand" or
    // its tracking as it goes doesn't leave it parked at home, out of the palm's reach (the review).
    const char* what[3] = {"the hand lost", "the pistol out of hand", "the pistol not tracked"};
    for (int i = 0; i < 3; ++i) {
        Sim n(Tuned(Setup(), 0.02f, 0.0f));
        n.TakeAndCatch();
        n.Push(n.r.s, -0.03f, 0.4f);
        n.Steps(3);
        const bool snapped = n.UntilSnap(-0.018f);
        bool* lost[3] = {&n.left, &n.pistol, &n.tracked};
        *lost[i] = false;  // a frame
        n.Step();
        *lost[i] = true;
        n.Steps(14);
        Check(snapped && n.Has("seated") && !n.Has("parked") && !n.wellEmpty && !pistolmag::Holding(),
              "%s a frame as it snaps from 1.8 cm short: home all the same, never parked", what[i]);
    }
    // (The snap is eased now, from the speed the push had: it goes on up a little each frame, not 3 cm at once.)
    Sim five(Tuned(Setup(), 0.05f, 0.0f));
    five.TakeAndCatch();
    five.Push(five.r.s, -0.06f, 0.4f);
    five.Steps(3);
    const bool snapped = five.UntilSnap(-0.045f);
    const float from = five.r.s;
    five.left = false;
    five.Step();
    Check(snapped && five.r.s > from + 0.001f && five.r.mag && five.r.holding && !pistolmag::Parked(),
          "seat 5, the push 4.5 cm short, the hand lost as it snaps: it goes on up (s %+.3f to %+.3f), drawn", from,
          five.r.s);
    five.left = true;
    five.Steps(14);
    Check(five.Has("seated") && !five.Has("parked") && five.tries == 1 && five.lastTry.seated,
          "then home, its try told with the seat (the push %.1f cm short at the nearest)", -five.lastTry.closest * 100);
    Sim ten(Tuned(Setup(), 0.10f, 0.0f));
    ten.Take();
    ten.Carry(-0.45f, -0.20f, 0.6f);
    ten.UntilCaught(-0.20f, -0.17f, 0.2f);
    ten.Steps(11);  // the hand drawn on it over on the flat base: it snaps from here
    const bool going = ten.r.s > ten.lastCatch.s;
    ten.left = false;
    ten.Steps(16);
    Check(going && ten.Has("seated") && !ten.Has("parked") && !ten.wellEmpty,
          "seat 10 from 9.6 cm short, the hand lost as the snap begins: home with no hand");
    // After the seat the hand drawn under it eases back to the controller over the glide's time, the pistol in hand or
    // not: hands.cpp runs Update on while it does (EasingBack; the review: stopped once the pistol left the hand, it was
    // drawn off the controller again as the pistol came back).
    Sim e;
    e.TakeAndCatch();
    while (!e.Has("seated") && e.frame < 400)
        e.Step();
    const bool easing = pistolmag::EasingBack();
    e.pistol = false;  // out of hand as it eases back
    std::vector<float> share;
    for (int i = 0; i < 14; ++i) {
        e.Step();
        share.push_back(e.r.gloveBlend);
    }
    float drop = 0.0f;
    for (size_t i = 1; i < share.size(); ++i)
        drop = std::fmax(drop, share[i - 1] - share[i]);
    Check(easing && share[0] > 0.9f && share.back() == 0.0f && drop < 0.16f && !pistolmag::EasingBack(),
          "after the seat, the pistol out of hand: the hand drawn under it eases back all the same (%.0f%% at most a "
          "frame), then it's done",
          drop * 100);
    // Gone by the caller with it on the line (the magazine by hand off, free hands off: Update isn't run after): the hand
    // drawn on it is back on the controller at once, nothing left easing back (the review: on the next Update, minutes
    // later, it was drawn 9 cm off the controller with the flat base's fingers).
    Sim c;
    c.TakeAndCatch();
    const bool drawnOn = c.r.gloveBlend == 1.0f && c.r.flat == 1.0f;
    pistolmag::Clear();
    const bool cleared = !pistolmag::EasingBack() && !pistolmag::Holding();
    c.MagAt(-0.45f, 0.05f, 30.0f);  // ... much later, the hand at the pelvis
    c.Step();
    Check(drawnOn && cleared && c.r.gloveBlend == 0.0f && c.r.fingers == 0.0f && !c.r.mag,
          "cleared on the line by the caller: nothing left easing back, the next Update draws the hand where it is");
    // Gone from here (the game's own reload) with it on the line: Update goes on, so the hand eases back from where it
    // was drawn, that frame too.
    Sim g2;
    g2.TakeAndCatch();
    const float onLine = g2.r.gloveBlend;
    g2.wellEmpty = false;
    g2.Step();
    const float atGone = g2.r.gloveBlend;
    g2.Steps(14);
    Check(g2.Has("cancelled") && onLine == 1.0f && atGone > 0.99f && g2.r.gloveBlend == 0.0f,
          "gone by the game's own reload on the line: the hand drawn on it eases back from there (%.2f the frame it "
          "goes), then it's on the controller",
          atGone);
}

void Holds() {
    std::printf("13 the holds and the helpers\n");
    float a[12], b[12], d[12];
    const float home[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    pistolmag::SetCarry(nullptr);
    pistolmag::GloveFor(home, false, a);
    pistolmag::GloveFor(home, true, b);
    Check(MostApart(a, b) < 1e-6f, "C2: the carry holds it where the flat base does (%.1e)", MostApart(a, b));
    pistolmag::SetCarry(pistolposes::kOnPalmGamePinch);
    pistolmag::GloveFor(home, false, a);
    Check(std::fabs(AngleBetween(a, b) - 53.4f) < 0.5f, "C1: 53 degrees off the flat base (%.1f)", AngleBetween(a, b));
    pistolmag::SetCarry(pistolposes::kOnPalmG2, pistolposes::kGuideG2);
    pistolmag::GloveFor(home, false, a);
    float inverse[12], floorplate[3], onA[3], onB[3];
    const float bind[3] = {-0.0001f, -0.2011f, 0.1071f};  // the floorplate's middle in the magazine's bind space
    Transform(kSeat, bind, floorplate);
    Invert(a, inverse);
    Compose(inverse, b, d);
    Transform(pistolposes::kOnPalmG2, floorplate, onA);
    Transform(pistolposes::kOnPalmFlat, floorplate, onB);
    Check(std::fabs(Angle(d) - 51.8f) < 0.3f && std::fabs(Distance(onA, onB) - 0.0172f) < 0.0005f,
          "G2: 51.8 degrees off the flat base (%.1f), the floorplate 1.7 cm from B's on the palm (%.2f)", Angle(d),
          Distance(onA, onB) * 100);
    pistolmag::SetCarry(nullptr);
    for (const float* hold : {pistolposes::kOnPalmFlat, pistolposes::kOnPalmSeated, pistolposes::kOnPalmGamePinch,
                              pistolposes::kOnPalmG2}) {
        float worst = 0.0f;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                const float dot =
                    hold[i * 4] * hold[j * 4] + hold[i * 4 + 1] * hold[j * 4 + 1] + hold[i * 4 + 2] * hold[j * 4 + 2];
                worst = std::fmax(worst, std::fabs(dot - (i == j ? 1.0f : 0.0f)));
            }
        Check(worst < 1e-4f, "a hold on the palm is rigid (rows %.1e off orthonormal)", worst);
    }
    float mine[12], theirs[12];
    for (float s : {0.0f, -0.05f, -0.30f})
        for (float turn : {0.0f, 35.0f}) {
            OnLine(s, 0.0f, turn, mine);
            pistolmag::PlaceOnLine(s, turn, theirs);
            Check(MostApart(mine, theirs) < 1e-5f, "PlaceOnLine(%.2f, %.0f) is the design's place (%.1e)", s, turn,
                  MostApart(mine, theirs));
        }
}

void Degenerate() {
    std::printf("14 frame times and poses not numbers\n");
    Sim m;
    m.Take();
    for (float dt : {0.0f, 1.0f, -1.0f, kNan})
        m.Step(false, dt);
    m.Carry(-0.45f, -0.20f, 0.6f);
    m.Carry(-0.20f, -0.18f, 0.2f);
    m.glove[5] = kNan;  // a broken pose: as the hand not seen
    m.Step();
    m.apart = kNan;
    m.Steps(12);
    m.MagAt(-0.18f);
    m.Steps(2);
    m.pistolPose[7] = kNan;  // the pistol's pose broken a frame
    m.Step();
    m.pistolPose[7] = -0.22f;
    m.Push(m.r.s, -0.04f, 0.4f);
    for (float dt : {kNan, 1.0f, -1.0f, 0.0f})
        m.Step(false, dt);
    m.Steps(16);
    Check(m.sane, "nothing drawn that isn't a number, never past home");
    Check(m.Has("seated"), "and it went home in the end");
}

void Glide() {
    std::printf("15 the glide\n");
    // From 6 cm aside and 45 degrees tilted, the hand still: the magazine and the hand drawn move a little a frame.
    for (int c = 0; c < 2; ++c) {
        Sim m(c ? C2() : Setup());
        m.Take();
        m.Carry(-0.45f, -0.20f, 0.6f, 0.06f, 45.0f);
        m.Watch();
        const bool caught = m.UntilCaught(-0.20f, -0.14f, 0.3f, 0.06f, 0.06f, 45.0f, 45.0f);
        const int at = m.caughtAt;
        float settled = -1.0f;
        for (int i = 0; i < 20; ++i) {
            m.Step();
            if (settled < 0.0f && m.OffLine() < 1e-4f)
                settled = (m.frame - at) * kDt;
        }
        Check(caught && settled > 0.12f && settled < 0.17f,
              "%s caught 6 cm aside and tilted 45: on the line %.2f s later (the glide 0.15)", c ? "C2" : "G2",
              settled);
        Check(m.topStep < 0.02f && m.palmStep < 0.025f,
              "no jump on the way: the magazine %.1f cm a frame at most, the hand drawn with it %.1f", m.topStep * 100,
              m.palmStep * 100);
    }
    // Let go during it: it goes on in; the hand dropped away, it's left there.
    Sim l;
    l.Take();
    l.Carry(-0.45f, -0.20f, 0.6f);
    l.UntilCaught(-0.20f, -0.17f, 0.2f);
    l.squeeze = 0.0f;
    l.Steps(12);
    Check(l.OffLine() < 1e-4f && !l.Has("let go") && !l.Has("off line") && pistolmag::Holding(),
          "let go of as it glides in: it goes on onto the line");
    l.Carry(l.handS, -0.30f, 0.6f);
    Check(l.Has("parked") && !l.Has("let go"), "the hand dropped away: parked, no fall");
    // X during it: hands.cpp drops only a parked one (otherwise "nothing in the grip to drop"): it goes on.
    Sim x;
    x.Take();
    x.Carry(-0.45f, -0.20f, 0.6f);
    x.UntilCaught(-0.20f, -0.17f, 0.2f);
    x.Steps(3);
    const bool parked = pistolmag::Parked(), holding = pistolmag::Holding();
    if (parked)
        pistolmag::Clear();  // as hands.cpp's X does with one parked
    x.Steps(40);
    Check(!parked && holding && x.Has("seated"), "X as it glides in: not a parked one, so left be; it goes on home");
    // Pulled back out with the grip squeezed: back into the hand, gliding, not caught again until it's out of reach.
    Sim b;
    b.Take();
    b.Carry(-0.45f, -0.20f, 0.6f);
    b.UntilCaught(-0.20f, -0.17f, 0.2f);
    const float from = b.handS;
    b.Watch();
    for (int i = 1; i <= 6; ++i) {  // 6 cm down in 6 frames
        b.MagAt(from - 0.01f * i);
        b.Step();
    }
    const bool out = b.Has("off line") && pistolmag::Holding() && !pistolmag::Parked();
    b.Steps(14);
    float holds[12];
    pistolmag::GloveFor(b.r.magPose, false, holds);
    Check(out && MostApart(holds, b.glove) < 1e-4f && b.r.gloveBlend == 0.0f && b.r.flat == 0.0f,
          "pulled back out while gliding in: it glides back into the hand, the hand back on its carry");
    Check(b.topStep < 0.03f, "no jump going back: %.1f cm a frame at most", b.topStep * 100);
    b.MagAt(-0.20f);
    b.Steps(10);
    Check(b.catches == 1, "held there out of reach and back in: caught again only when it's come back in");
    b.Carry(-0.20f, -0.30f, 0.6f);
    b.Carry(-0.30f, -0.18f, 0.3f);
    Check(b.catches == 2, "out of reach and back: caught again");
}

void Flicker() {
    std::printf("16 no flicker\n");
    // Hovering at the edge of reach for 3 s, jittering a centimetre either way.
    Sim e;
    e.Take();
    e.Carry(-0.45f, -0.20f, 0.6f, 0.0f, 0.0f);
    for (int i = 0; i < 216; ++i) {
        e.MagAt(-0.186f - 0.01f * std::sin(i * 0.9f), 0.004f * std::cos(i * 1.7f));
        e.Step();
    }
    Check(e.catches == 1 && e.flatTurns == 1 && e.handTurns == 1 && !e.Has("off line"),
          "hovering at the edge of reach 3 s: caught once, the fingers opening once (%d), the hand drawn on once (%d)",
          e.flatTurns, e.handTurns);
    // In and out of reach over and over, too tilted to catch: told once each time it's out of reach again, 1.5 cm past
    // it (the swing goes from 1.1 cm inside the reach to 2.9 cm outside: it went 1.6 either way before the slack).
    Sim t;
    t.Take();
    t.Carry(-0.45f, -0.20f, 0.6f, 0.0f, 60.0f);
    for (int i = 0; i < 216; ++i) {
        t.MagAt(-0.195f + 0.02f * std::sin(i * 0.12f), 0.0f, 60.0f);
        t.Step();
    }
    const int crossings = t.misses;
    Check(t.catches == 0 && t.flatTurns == 0 && t.handTurns == 0 && crossings >= 3 && crossings <= 5,
          "in and out of reach tilted 60, 3 s: nothing caught, no pose changing, %d near misses told (one each)",
          crossings);
    // Trembling 1 mm at the edge of reach, too tilted to catch: no near miss told while it's there (the review: in and
    // out of reach every frame or two, 72 told in 3 s, each a line in the log), one as it goes.
    Sim q;
    q.Take();
    q.Carry(-0.45f, kMouth - 0.09f, 0.6f, 0.0f, 60.0f);
    for (int i = 0; i < 216; ++i) {
        q.MagAt(kMouth - 0.075f + 0.001f * std::sin(i * 2.1f), 0.0f, 60.0f);
        q.Step();
    }
    const int there = q.misses;
    q.Carry(kMouth - 0.075f, kMouth - 0.12f, 0.3f, 0.0f, 60.0f);
    Check(there == 0 && q.misses == 1,
          "trembling 1 mm at the edge of reach tilted 60, 3 s: no near miss told meanwhile (%d), one as it goes (%d)",
          there, q.misses);
    // On the line, the hand jittering 1.5 cm up and down (leaving it takes 2.5 below, three frames running).
    Sim j(Tuned(Setup(), 0.02f, 0.0f));
    j.TakeAndCatch();
    const float at = j.r.s;
    const int flats = j.flatTurns, hands = j.handTurns;
    for (int i = 0; i < 216; ++i) {
        j.PushTo(at - 0.0075f - 0.0075f * std::sin(i * 1.3f));
        j.Step();
    }
    Check(!j.Has("parked") && !j.Has("pushed") && j.flatTurns == flats && j.handTurns == hands,
          "on the line, the hand jittering 1.5 cm: never parked, nothing turned over");
    // Under a parked one, hovering between a push's reach (1 cm) and leaving it (2.5 cm).
    Sim p(Tuned(Setup(), 0.02f, 0.0f));
    p.TakeAndCatch();
    const float parkedAt = p.r.s;
    p.squeeze = 0.0f;
    p.Push(parkedAt, parkedAt - 0.10f, 0.6f);
    const int park = p.Count("parked");
    for (int i = 0; i < 216; ++i) {
        p.MagAt(parkedAt - 0.0175f - 0.0055f * std::sin(i * 0.8f), 0.0f, 0.0f, true);
        p.Step();
    }
    Check(park == 1 && p.Count("parked") == 1 && !p.Has("pushed"),
          "under a parked one, the palm hovering 1.2-2.3 cm below it 3 s: no push, no going back and forth");
    p.MagAt(parkedAt - 0.005f, 0.0f, 0.0f, true);
    p.Step();
    Check(p.Has("pushed") && p.Count("pushed") == 1, "brought within 1 cm: pushed on, once");
    // A hand in from the side 2 cm above a parked one, then lowered 6 cm (the review: pushed and parked twice, two
    // tries told, the drawn hand on and off three times): pushed once, left at most once.
    Sim f(Tuned(Setup(), 0.02f, 0.0f));
    f.TakeAndCatch();
    const float at2 = f.r.s;
    f.squeeze = 0.0f;
    f.Push(at2, at2 - 0.10f, 0.6f);
    const int parks = f.Count("parked"), turns = f.handTurns;
    for (int i = 1; i <= 20; ++i) {  // out to the side at its height + 2 cm
        f.MagAt(at2 - 0.10f + 0.12f * i / 20, 0.10f * i / 20, 0.0f, true);
        f.Step();
    }
    for (int i = 1; i <= 20; ++i) {  // in from the side
        f.MagAt(at2 + 0.02f, 0.10f - 0.08f * i / 20, 0.0f, true);
        f.Step();
    }
    for (int i = 1; i <= 40; ++i) {  // down 6 cm
        f.MagAt(at2 + 0.02f - 0.06f * i / 40, 0.02f, 0.0f, true);
        f.Step();
    }
    Check(f.Count("pushed") == 1 && f.Count("parked") - parks <= 1 && f.handTurns - turns <= 2,
          "a hand in from the side above a parked one, then lowered 6 cm: pushed once, left at most once (%d), the "
          "drawn hand on and off at most once (%d)",
          f.Count("parked") - parks, f.handTurns - turns);
    // Parked by the hand leaving it with its top still within a push's reach (turned 27 degrees about it, the palm
    // swung 6 cm across): not pushed on again until the hand has been out of that reach.
    Sim u(Tuned(Setup(), 0.02f, 0.0f));
    u.TakeAndCatch();
    const float left = u.r.s;
    u.squeeze = 0.0f;
    u.Push(left, left - 0.10f, 0.6f);  // dropped away: parked
    u.MagAt(left + 0.01f);             // back holding it as the carry would, its top 1 cm into the parked one's
    u.Step();
    const bool pushedOn = u.Count("pushed") == 1;
    float turned = 0.0f;
    for (int i = 1; i <= 15 && u.Count("parked") < 2; ++i) {  // turned about its top: the palm swings across
        turned = 40.0f * i / 15;
        u.MagAt(left + 0.01f, 0.0f, turned);
        u.Step();
    }
    const bool leftAgain = u.Count("parked") == 2 && pistolmag::Parked();
    for (int i = 1; i <= 20; ++i) {  // straightened again, its top within a push's reach of the parked one's
        u.MagAt(left + 0.01f, 0.0f, turned - turned * i / 20);
        u.Step();
    }
    const bool waited = u.Count("pushed") == 1;
    const float parkedThere = u.r.s;
    u.MagAt(left - 0.03f);  // out of the reach
    u.Steps(3);
    u.MagAt(parkedThere - 0.005f);  // and back within it
    u.Step();
    Check(pushedOn && leftAgain && waited && u.Count("pushed") == 2,
          "parked by the hand leaving it within a push's reach: not pushed on again until the hand's been out of it");
    // Every line hands.cpp logs is one of the events: a whole reload is a handful of them.
    Sim w;
    w.Take();
    w.Carry(-0.45f, -0.17f, 0.6f);
    w.Steps(60);
    Check(w.events.size() == 4, "a whole reload: %d events (taken, caught, seated, its try)",
          static_cast<int>(w.events.size()));
}

void Tunables() {
    std::printf("17 the tunables\n");
    pistolmag::SetCatch(0.5f, 120.0f);
    pistolmag::Tuning t = pistolmag::Settings();
    Check(std::fabs(t.reach - 0.15f) < 1e-6f && std::fabs(t.angle - 80.0f) < 1e-3f,
          "catch 50 120: kept to 15 cm, 80 degrees");
    pistolmag::SetCatch(kNan, kNan);
    t = pistolmag::Settings();
    Check(std::fabs(t.reach - 0.01f) < 1e-6f && std::fabs(t.angle - 5.0f) < 1e-3f,
          "catch not numbers: the least (1 cm, 5)");
    pistolmag::SetGlide(0.0f);
    Check(std::fabs(pistolmag::Settings().glide - 0.05f) < 1e-6f, "glide 0: the least, 0.05 s");
    pistolmag::SetGlide(9.0f);
    Check(std::fabs(pistolmag::Settings().glide - 0.5f) < 1e-6f, "glide 9: kept to 0.5 s");
    pistolmag::SetDwell(0.0f);
    Check(pistolmag::Settings().dwell == 0.0f, "dwell 0: never by itself");
    pistolmag::SetDwell(9.0f);
    Check(std::fabs(pistolmag::Settings().dwell - 3.0f) < 1e-6f, "dwell 9: kept to 3 s");
    pistolmag::SetDwell(kNan);
    Check(pistolmag::Settings().dwell == 0.0f, "dwell not a number: never");
    pistolmag::SetSnap(0.0f);
    Check(std::fabs(pistolmag::Settings().snap - 0.005f) < 1e-6f, "seat 0: the least, 5 mm");
    Setup small;
    small.reach = 0.05f;
    Sim s(small);
    s.Take();
    const bool side = s.UntilCaught(-0.45f, kMouth, 0.4f, 0.06f, 0.06f);
    s.Steps(20);
    Check(!side && !s.Has("caught"), "catch 5: up 6 cm to the side isn't caught");
    Setup slow;
    slow.glide = 0.3f;
    Sim g(slow);
    g.Take();
    g.Carry(-0.45f, -0.20f, 0.6f);
    g.UntilCaught(-0.20f, -0.17f, 0.2f);
    const int at = g.caughtAt;
    float settled = -1.0f;
    for (int i = 0; i < 30; ++i) {
        g.Step();
        if (settled < 0.0f && g.OffLine() < 1e-4f)
            settled = (g.frame - at) * kDt;
    }
    Check(settled > 0.27f && settled < 0.32f, "glide 0.3: on the line %.2f s after the catch", settled);
}

// hands.cpp's LoadTest, its script as there: every 6 s, X; taken at the pelvis 40 cm down the line turned 35 degrees
// (0.6 s); up towards the grip straightening to 6 cm under it (2.0 s); then pushed on 4 cm (2.6 s), held, let go and
// dropped (3.0-4.0 s); or ("park") let go and dropped 10 cm (2.0-2.6 s), back up to 6 cm into the grip (4.2 s) and
// dropped away (5.2 s). The hand holds it as the carry does all the way.
void LoadTestScript() {
    std::printf("18 hands.cpp's loadtest script\n");
    auto ease = [](float x) {
        x = std::fmin(std::fmax(x, 0.0f), 1.0f);
        return x * x * (3.0f - 2.0f * x);
    };
    for (int c = 0; c < 2; ++c)
        for (bool park : {false, true}) {
            Sim m(c ? C2() : Setup());
            bool took = false;
            for (int k = 0; k < 432; ++k) {  // 6 s
                const float u = k * kDt;
                auto between = [&](float from, float to, float a, float b) {
                    return a + (b - a) * ease((u - from) / (to - from));
                };
                float s = -0.40f, turn = 0.0f, squeeze = u >= 0.6f ? 1.0f : 0.0f;
                if (u < 0.8f) {
                    turn = 35.0f;
                } else if (u < 2.0f) {
                    s = between(0.8f, 2.0f, -0.40f, -0.17f);
                    turn = between(0.8f, 2.0f, 35.0f, 0.0f);
                } else if (!park) {
                    s = u < 2.6f   ? between(2.0f, 2.6f, -0.17f, -0.13f)
                        : u < 3.0f ? -0.13f
                                   : between(3.0f, 4.0f, -0.13f, -0.28f);
                    squeeze = u < 3.0f ? 1.0f : 0.0f;
                } else {
                    s = u < 2.6f   ? between(2.0f, 2.6f, -0.17f, -0.27f)
                        : u < 4.2f ? between(2.6f, 4.2f, -0.27f, -0.05f)
                                   : between(4.2f, 5.2f, -0.05f, -0.25f);
                    squeeze = 0.0f;
                }
                m.MagAt(s, 0.0f, turn);
                m.squeeze = squeeze;
                const bool reached = u >= 0.6f && !took;
                took = took || reached;
                m.apart = 0.0f;
                m.Step(reached);
            }
            const char* name = c ? "C2" : "G2";
            if (!park)
                Check(m.Has("caught") && m.Has("seated") && !m.Has("parked") && !m.Has("let go") && m.sane,
                      "%s loadtest: caught, pushed home", name);
            else
                Check(m.Has("caught") && m.Has("parked") && m.Has("pushed") && m.Has("seated") && !m.Has("let go") &&
                          m.sane,
                      "%s loadtest park: caught, left part way in, pushed on, home", name);
        }
}

}

int main(int argc, char** argv) {
    g_verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;
    std::printf("pistolmagtest: src/pistolmag.cpp at 72 Hz. The grip's line 16 degrees back, its bottom %.2f cm below "
                "home; G2, caught within 7.5 cm and 55 degrees, the glide 0.15 s, the snap 6.5 cm, the dwell 0.4 s "
                "unless said.\n",
                -kMouth * 100);
    Empty();
    Refused();
    ParkAndPush();
    Drops();
    Catches();
    GameReload();
    Carries();
    Snap();
    Dwell();
    Tries();
    PushHome();
    Lost();
    Holds();
    Degenerate();
    Glide();
    Flicker();
    Tunables();
    LoadTestScript();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
