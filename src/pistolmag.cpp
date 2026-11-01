#include "pistolmag.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "pistolposes.h"

namespace pistolmag {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDegree = kPi / 180.0f;

// ---- The train pistol's well, in gun space (screenshots\wf_reload\design\geom.exe on the recordings) ----
// The seat: the magazine's bind space -> gun space with it home (the tactical recording at 0.5 s; SetSeat puts the
// game's own here once skin.cpp has it). Its y, the magazine's own length, is the way in: 16.0 degrees back from the
// gun's y (every moving frame of the game's reload within 0.17 mm of that line, never turned).
float g_seat[12] = {1, 0, 0, 0, 0, 0.96137f, -0.27524f, 0.13766f, 0, 0.27524f, 0.96137f, -0.02470f};
float g_line[3] = {0.0f, 0.96137f, 0.27524f};
// The magazine's top middle (its feed lips) in its bind space; home in gun space it's at (0, 0.0211, 0.0460). It is
// 11.15 cm long, and home its floorplate is flush with the grip's bottom (0.7 mm below it), so its top reaches the
// grip's bottom, the well's mouth, 11.08 cm below home: s (the top's place along the line from home) -0.1108.
constexpr float kTopBind[3] = {-0.0001f, -0.0926f, 0.1001f};
float g_top[3] = {};  // home, in gun space (from the seat)
constexpr float kMouth = -0.1108f;
// Its floorplate's middle (the box's base, as kTopBind its top) in its bind space: home in gun space at
// (0, -0.0852, 0.0229). The hand drawn going over from a carry's own place to the flat base turns about it: about gun
// space's origin, 8.8 cm off, G2's index went 1.5 mm into the frame and the thumb 7.1 mm into the magazine on the way;
// about this, the fingers ahead, the thumb 2.7 mm at most (screenshots\wf_reload\g2\C-index-then-palm\geo_report.txt).
constexpr float kBaseBind[3] = {-0.0001f, -0.2011f, 0.1071f};
float g_base[3] = {};  // home, in gun space (from the seat)

// ---- The magnetic catch ----
// The first headset test (2026-11-01): the magazine slipped past the grip's bottom uncaught (a funnel 5 mm and 3
// degrees wide at the mouth, and nothing caught that had gone above it), "it takes a couple of attempts". The user:
// magnetic, from any direction, the hand to the gun or the gun onto the hand, "at least some animation to it". So a
// magazine in the hand is caught once its top is within the reach of the grip's mouth (pistol.txt "catch": 7.5 cm) or
// already in the grip (within 3 cm of its line between the mouth and home), its length within the angle of the grip's
// line and turned about it no more than that (55 degrees: one facing backwards isn't one to catch), however it got
// there. Not while it's plainly going away or passing by fast: where its top will be 0.08 s on, at the speed it's going
// relative to the pistol, must be in reach too (or up the grip, where one going in is going), so a swipe past at 2 m/s
// isn't caught; nor faster than 4 m/s (not a hand: a tracking jump), nor the frame it's seen again (its speed not
// known). Pulled back out of the grip, it isn't caught again until it's been out of reach.
float g_reach = 0.075f, g_angle = 55.0f * kDegree;
constexpr float kReachLeast = 0.01f, kReachMost = 0.15f, kAngleLeast = 5.0f, kAngleMost = 80.0f;
constexpr float kInGrip = 0.03f, kLead = 0.08f, kFastest = 4.0f;
// Caught, it glides onto the line: an eased move (pistol.txt "glide": 0.15 s) from where the hand has it to the line,
// its top 1.5 cm into the grip (where the well holds it) or as far in as it already was; the hand drawn holding it goes
// with it. Every eased move takes the glide's time: the snap home, the hand drawn going over from the carry to the flat
// base (its fingers ahead of it, open two thirds of the way), and its easing back to the controller after.
float g_glide = 0.15f;
constexpr float kGlideLeast = 0.05f, kGlideMost = 0.5f, kGlideIn = 0.015f;

// ---- On the line ----
// The hand goes on as it was at the catch: the magazine goes as far up the line as the palm goes on up it from where it
// was then (the bare left palm's middle: hands.cpp's kLeftPalmCentre, pistolslide's grip), the lesser of its last two
// frames (a one-frame tracking jump moves nothing), at most 2 cm a frame, never past home and never back down (the well
// holds it). How the hand is turned doesn't come into it (read through the hand's whole pose, a wrist straightening the
// magazine after the catch swung its top centimetres off). The palm leaving it, three frames running (a tracking jump
// is one or two), leaves it there (parked): 2.5 cm back down from where it pushes it or 5 cm off the line; still
// gliding in with the grip squeezed, 3 cm down or 4 cm off takes it back out into the hand (it glides back). A hand
// lost a moment (0.25 s: the catch can leave the hand some centimetres under the magazine, out of a parked push's
// reach) keeps it on the line; longer, it's parked.
constexpr float kPalmBind[3] = {0.5182f, 1.1075f, 0.1756f};
constexpr float kMaxStep = 0.02f;
constexpr int kHeldFrames = 2, kOffFrames = 3;
constexpr float kOffBelow = 0.03f, kOffAside = 0.04f;
constexpr float kLeaveBelow = 0.025f, kLeaveAside = 0.05f;
constexpr float kLostOnLine = 0.25f;
// Home (the review: the controllers can't overlap as the hands would under the grip, so seating by the palm alone is
// probably out of reach). Once the push has been within the snap of home for two frames running (a tracking jump is
// one), or the hand has held it on the line the dwell's time with the grip squeezed (pistol.txt "dwell": 0.4 s; drawing
// back 1 cm starts it again from there; the first headset test: every caught magazine was pushed home anyway, and the
// last centimetres are where the controllers meet), it goes home by itself over the glide's time whatever the hand does
// (seen or not): eased on from the speed it had to a stop at home, and seated as it gets there. The snap is 6.5 cm (the
// first test's tries came 2.1-3.7 cm short of home with 4; pistol.txt "seat"), never under 5 mm nor more than 10 cm (a
// centimetre into the grip). A carry whose fingers reach up the grip (G2's index along its front: SetCarry's guide)
// isn't pushed past their reach, nor snaps, until the hand drawn on it is over on the flat base.
constexpr float kHome = 0.005f, kSnapMost = 0.10f;
float g_snap = 0.065f;
float g_dwell = 0.4f;
constexpr float kDwellLeast = 0.1f, kDwellMost = 3.0f, kDwellSlack = 0.01f;
// A parked magazine is pushed on by a palm under its floorplate: the hand holding it as the carry would or as the flat
// base would (an open palm) puts its top within 1 cm below where it's parked (or above), within 4 cm of the line and 45
// degrees (leaving it takes 2.5 cm: nothing goes back and forth between). The hand drawn under it then is the flat
// base's, eased in.
constexpr float kPushReach = 0.01f, kPushAside = 0.04f, kPushTurned = 45.0f * kDegree;
// The game's own push home (2.421 s) comes in over the magazine's last 3 cm.
constexpr float kPushFrom = 0.03f;
constexpr float kLost = 1.0f;          // s a lost hand keeps its magazine (and the pistol, out of hand)
constexpr float kArm = 0.4f;           // the grip below this: let go
constexpr float kMaxDropSpeed = 3.0f;  // m/s

// ---- The holds (pistolposes.h: gun space with the magazine home -> the arms' bind space) ----
// The flat base (B) and the push home (B2) are the game's own hands; the carry is the flat base's place with the
// fingers closed (C2) unless SetCarry gives it a place of its own (G2, the game's pinch).
float g_push[12] = {};
float g_seated[12] = {};
float g_carry[12] = {};
bool g_carryOwn = false;
float g_guide = 0.0f;  // a carry's fingers' reach up the grip past the mouth (m; 0: never in the way)

enum class State { kNone, kHand, kLine, kParked };
State g_state = State::kNone;
int g_heldFrames = 0, g_offFor = 0;
float g_along = 0.0f;  // the magazine's s on the line (kLine, kParked; gliding back into the hand: where it came off)
float g_handS = 0.0f;  // in the hand: its s as the hand has it
float g_lostFor = 0.0f, g_gunLostFor = 0.0f, g_lineLostFor = 0.0f;
bool g_wasEmpty = false;
// The magazine as drawn: where the hand has it (g_handInGun: gun space home -> gun space, as last seen) blended onto
// its place on the line at g_along by how far it has glided (0 in the hand, 1 on the line; eased).
float g_handInGun[12] = {};
float g_glided = 0.0f;
// The hand drawn holding it: how far it has gone over from the carry to the flat base (0-1, eased; the fingers ahead),
// and on the line how far it has come onto the magazine (1 from a catch, where it's the controller's own hand; a
// parked push eases it in).
float g_regrip = 0.0f;
float g_handIn = 0.0f;
// This try on the line: where the palm was in gun space as it began (caught, or pushed on from parked) and the
// magazine's s then, the push the frame before, the nearest it has held the top to home for two frames running with the
// hands apart then; the magazine's speed up the line (m/s); the snap (from s at a speed, how long it's been going,
// whether the dwell began it); how long the hand has held it there, and the furthest up the push has been since.
bool g_trying = false, g_snapping = false, g_dwelt = false;
float g_tryPalm[3] = {};
float g_tryFrom = 0.0f;
float g_lastPush = 0.0f, g_closest = 0.0f, g_closestApart = 0.0f;
float g_speed = 0.0f;
float g_snapFrom = 0.0f, g_snapSpeed = 0.0f, g_snapFor = 0.0f;
float g_dwellFor = 0.0f, g_dwellTop = 0.0f;
// The hand drawn easing back to the controller after the magazine has gone from it (seated, parked, let go, gone): how
// far off the controller it was (controller^-1 x drawn), its share then, how long it has been easing (below 0: not),
// and the fingers on the magazine's poses then.
float g_away[12] = {};
float g_awayShare = 0.0f, g_awayFor = -1.0f, g_awayFingers = 0.0f, g_awayFlat = 0.0f, g_awayPush = 0.0f;
// The controller's hand as last seen; the hand and the fingers as last drawn holding it.
float g_glove[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
float g_drawn[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
float g_drawnShare = 0.0f, g_shownFingers = 0.0f, g_shownFlat = 0.0f, g_shownPush = 0.0f;
// The catch: armed (not just pulled back out of the grip); the top as last seen in the hand (its speed); a near miss
// so far (the nearest it came to the mouth in reach and uncaught, its turn then and why: below 0, none).
bool g_armed = true, g_haveTop = false;
float g_lastTop[3] = {};
float g_missFrom = -1.0f, g_missTurned = 0.0f;
const char* g_missWhy = "";
XrVector3f g_gripAt[4] = {};
float g_gripDt[4] = {};
int g_gripNext = 0, g_gripCount = 0;

void Compose(const float* a, const float* b, float* out) {
    float r[12];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j];
        r[i * 4 + 3] += a[i * 4 + 3];
    }
    std::memcpy(out, r, sizeof(r));
}

// A rotation | translation's rotation made exactly orthonormal (the holds are written to 5 decimals).
void Tidy(float* m) {
    float* r0 = m;
    float* r1 = m + 4;
    float* r2 = m + 8;
    auto normalize = [](float* v) {
        const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (n > 0.0f)
            v[0] /= n, v[1] /= n, v[2] /= n;
    };
    normalize(r0);
    const float d = r1[0] * r0[0] + r1[1] * r0[1] + r1[2] * r0[2];
    for (int i = 0; i < 3; ++i)
        r1[i] -= d * r0[i];
    normalize(r1);
    r2[0] = r0[1] * r1[2] - r0[2] * r1[1];
    r2[1] = r0[2] * r1[0] - r0[0] * r1[2];
    r2[2] = r0[0] * r1[1] - r0[1] * r1[0];
}

// A rotation | translation inverted (the rotation's transpose).
void InvertRigid(const float* m, float* out) {
    float r[12];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i * 4 + j] = m[j * 4 + i];
    for (int i = 0; i < 3; ++i)
        r[i * 4 + 3] = -(r[i * 4] * m[3] + r[i * 4 + 1] * m[7] + r[i * 4 + 2] * m[11]);
    std::memcpy(out, r, sizeof(r));
}

void Transform(const float* m, const float* p, float* out) {
    float r[3];
    for (int i = 0; i < 3; ++i)
        r[i] = m[i * 4] * p[0] + m[i * 4 + 1] * p[1] + m[i * 4 + 2] * p[2] + m[i * 4 + 3];
    std::memcpy(out, r, sizeof(r));
}

void ToQuat(const float* m, float* q) {  // m: a 3x4's rotation; q: x y z w
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

void FromQuat(const float* q, float* m) {  // into a 3x4's rotation (its translation untouched)
    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    const float x = q[0] / n, y = q[1] / n, z = q[2] / n, w = q[3] / n;
    const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                        2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                        2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            m[i * 4 + j] = r[i * 3 + j];
}

// a and b (rotation | translation rows) blended: s 0 is a, 1 is b.
void Blend(const float* a, const float* b, float s, float* out) {
    float qa[4], qb[4];
    ToQuat(a, qa), ToQuat(b, qb);
    if (qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3] < 0.0f)
        for (float& v : qb)
            v = -v;
    float q[4];
    for (int i = 0; i < 4; ++i)
        q[i] = qa[i] + (qb[i] - qa[i]) * s;
    float r[12];
    FromQuat(q, r);
    for (int i = 0; i < 3; ++i)
        r[i * 4 + 3] = a[i * 4 + 3] + (b[i * 4 + 3] - a[i * 4 + 3]) * s;
    std::memcpy(out, r, sizeof(r));
}

// The same about p, a point of their source space: p goes straight from where a puts it to where b does, the rest
// turns about it (Blend turns about the source's origin: for the holds, 8.8 cm from the floorplate).
void BlendAt(const float* a, const float* b, float s, const float* p, float* out) {
    const float to[12] = {1, 0, 0, p[0], 0, 1, 0, p[1], 0, 0, 1, p[2]};
    const float back[12] = {1, 0, 0, -p[0], 0, 1, 0, -p[1], 0, 0, 1, -p[2]};
    float pa[12], pb[12], blended[12];
    Compose(a, to, pa), Compose(b, to, pb);
    Blend(pa, pb, s, blended);
    Compose(blended, back, out);
}

// Eased: 0 to 1 with no jump in speed at either end.
float Smooth(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

// How far along the eased move y is (Smooth's inverse).
float Unsmooth(float y) {
    return 0.5f - std::sin(std::asin(std::clamp(1.0f - 2.0f * y, -1.0f, 1.0f)) / 3.0f);
}

// How far a 3x4's rotation turns (radians, 0-pi).
float TurnAngle(const float* m) {
    return std::acos(std::clamp((m[0] + m[5] + m[10] - 1.0f) * 0.5f, -1.0f, 1.0f));
}

float Dot(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// A pose with no number that isn't one (a broken pose counts as not seen).
bool Finite(const float* m) {
    for (int i = 0; i < 12; ++i)
        if (!std::isfinite(m[i]))
            return false;
    return true;
}

void Prepare() {
    static bool ready = false;
    if (ready)
        return;
    ready = true;
    std::memcpy(g_push, pistolposes::kOnPalmFlat, sizeof(g_push));
    std::memcpy(g_seated, pistolposes::kOnPalmSeated, sizeof(g_seated));
    Tidy(g_push);
    Tidy(g_seated);
    if (!g_carryOwn)
        std::memcpy(g_carry, g_push, sizeof(g_carry));
    Transform(g_seat, kTopBind, g_top);
    Transform(g_seat, kBaseBind, g_base);
}

// The push home's share on the line at s: none until its last 3 cm, all of it home.
float PushShare(float s) {
    return std::clamp(1.0f + s / kPushFrom, 0.0f, 1.0f);
}

// The fingers going over from the carry's to the flat base's: ahead of the hand drawn on it, open two thirds of the way
// (G2's thumb went 6 mm into the magazine with them together, 2.7 mm so: screenshots\wf_reload\g2\C-index-then-palm).
float FingersFlat() {
    return Smooth(std::min(1.5f * g_regrip, 1.0f));
}

// The magazine as the hand holds it with hold: its pose (where the frame bone would be with it home, view space),
// in gun space (inGun: gun space home -> gun space now) and its top there; s, how far it is along the line from
// home; lateral, how far its top is off the line; turned, how far it's turned from home.
struct Held {
    float pose[12], inGun[12], top[3];
    float s, lateral, turned;
};
Held AsHeld(const Frame& f, const float* hold) {
    Held h;
    Compose(f.glove, hold, h.pose);
    float gunInverse[12];
    InvertRigid(f.pistolPose, gunInverse);
    Compose(gunInverse, h.pose, h.inGun);
    Transform(h.inGun, g_top, h.top);
    const float d[3] = {h.top[0] - g_top[0], h.top[1] - g_top[1], h.top[2] - g_top[2]};
    h.s = Dot(d, g_line);
    const float off[3] = {d[0] - h.s * g_line[0], d[1] - h.s * g_line[1], d[2] - h.s * g_line[2]};
    h.lateral = std::sqrt(Dot(off, off));
    h.turned = TurnAngle(h.inGun);
    return h;
}

// How far a magazine at inGun has its length turned from the grip's line (tilt), and how far it's turned about its own
// length (twist): radians.
void Turns(const float* inGun, float& tilt, float& twist) {
    float axis[3];
    for (int i = 0; i < 3; ++i)
        axis[i] = inGun[i * 4] * g_line[0] + inGun[i * 4 + 1] * g_line[1] + inGun[i * 4 + 2] * g_line[2];
    tilt = std::acos(std::clamp(Dot(axis, g_line), -1.0f, 1.0f));
    float q[4];
    ToQuat(inGun, q);
    twist = 2.0f * std::atan2(std::fabs(q[0] * g_line[0] + q[1] * g_line[1] + q[2] * g_line[2]), std::fabs(q[3]));
}

// A top at p (gun space) in the catch's reach: within the reach of the grip's mouth, or in the grip (within 3 cm of its
// line from the mouth up to home; heading: past home too, where one going up the grip is going). fromMouth: how far it
// is from the mouth.
bool InReach(const float* p, bool heading, float* fromMouth) {
    const float d[3] = {p[0] - g_top[0], p[1] - g_top[1], p[2] - g_top[2]};
    const float s = Dot(d, g_line);
    const float m[3] = {d[0] - kMouth * g_line[0], d[1] - kMouth * g_line[1], d[2] - kMouth * g_line[2]};
    const float mouth = std::sqrt(Dot(m, m));
    if (fromMouth)
        *fromMouth = mouth;
    const float off[3] = {d[0] - s * g_line[0], d[1] - s * g_line[1], d[2] - s * g_line[2]};
    return mouth <= g_reach || (s >= kMouth && (heading || s <= 0.0f) && std::sqrt(Dot(off, off)) <= kInGrip);
}

// The magazine as drawn (gun space home -> gun space): where the hand has it, glided onto the line by g_glided.
void DrawnMag(float* inGun) {
    float line[12];
    PlaceOnLine(g_along, 0.0f, line);
    Blend(g_handInGun, line, Smooth(g_glided), inGun);
}

// A carry whose fingers reach up the grip still in the way: the hand drawn on it not over on the flat base yet.
bool Waiting() {
    return g_carryOwn && g_guide > 0.0f && g_regrip < 1.0f;
}

// The snap t s after it began: from where it was at the speed it had (no hitch) to a stop at home, over the glide's
// time (a cubic: never past home, never back, with the speed kept within twice the average).
float SnapAt(float t) {
    const float u = std::clamp(t / g_glide, 0.0f, 1.0f), u2 = u * u, u3 = u2 * u;
    return (2.0f * u3 - 3.0f * u2 + 1.0f) * g_snapFrom + (u3 - 2.0f * u2 + u) * g_glide * g_snapSpeed;
}

// What's drawn this frame, by the state: the magazine (in the hand, gliding onto the line or back out, on the line,
// parked) and the hand drawn holding it with its fingers (in the hand it's the controller's own unless gliding back;
// parked or gone, the hand is EaseBack's). drawable: the pistol's pose known (and in the hand, the hand's).
void Show(const Frame& f, bool drawable, Result& r) {
    const bool line = g_state == State::kLine;
    r.holding = g_state == State::kHand || line;
    r.s = g_state == State::kHand ? g_handS : g_along;
    if (g_state == State::kParked && drawable) {
        float inGun[12];
        DrawnMag(inGun);
        Compose(f.pistolPose, inGun, r.magPose);
        r.mag = true;
    }
    if (!r.holding)
        return;
    const float regrip = Smooth(g_regrip), flat = FingersFlat(), push = PushShare(g_along);
    r.fingers = line ? Smooth(g_handIn) : 1.0f;
    r.flat = flat;
    r.push = push * flat;
    g_shownFingers = r.fingers, g_shownFlat = r.flat, g_shownPush = r.push;
    if (!drawable)
        return;
    float inGun[12], base[12], hold[12], inverse[12];
    DrawnMag(inGun);
    Compose(f.pistolPose, inGun, r.magPose);
    r.mag = true;
    // The hand on it: the carry's place going over to the flat base's about the floorplate, then the push home's.
    BlendAt(g_carry, g_push, regrip, g_base, base);
    Blend(base, g_seated, push * regrip, hold);
    InvertRigid(hold, inverse);
    Compose(r.magPose, inverse, r.glove);
    r.gloveBlend = line ? Smooth(g_handIn) : g_glided > 0.0f || g_regrip > 0.0f ? 1.0f : 0.0f;
    std::memcpy(g_drawn, r.glove, sizeof(g_drawn));
    g_drawnShare = r.gloveBlend;
}

// The magazine has gone from the hand (seated, parked, let go, gone): the hand drawn holding it eases back to the
// controller from where it was last drawn, kept off the controller by that much, shrinking, so it goes with the hand
// rather than lagging behind it; the fingers come off the magazine with it.
void LeaveHand() {
    float inverse[12];
    InvertRigid(g_glove, inverse);
    Compose(inverse, g_drawn, g_away);
    g_awayShare = g_drawnShare;
    g_awayFingers = g_shownFingers, g_awayFlat = g_shownFlat, g_awayPush = g_shownPush;
    g_awayFor = 0.0f;
}

// Easing back, once a frame (before anything else: a state holding the magazine again draws over it).
void EaseBack(float dt, Result& r) {
    if (g_awayFor < 0.0f)
        return;
    g_awayFor += dt;
    const float k = 1.0f - Smooth(g_awayFor / g_glide);
    if (k <= 0.0f) {
        g_awayFor = -1.0f;
        return;
    }
    Compose(g_glove, g_away, r.glove);
    r.gloveBlend = g_awayShare * k;
    r.fingers = g_awayFingers * k;
    r.flat = g_awayFlat, r.push = g_awayPush;
}

// The share the hand drawn easing back has now (for one that comes back onto the magazine from there).
float AwayShare() {
    return g_awayFor >= 0.0f ? g_awayShare * (1.0f - Smooth(g_awayFor / g_glide)) : 0.0f;
}

// The hand's speed over its last three frames as it lets go (at most 3 m/s), for the fall.
void DropSpeed(Result& r) {
    if (g_gripCount < 4)
        return;
    const XrVector3f &now = g_gripAt[(g_gripNext + 3) % 4], &then = g_gripAt[g_gripNext];
    const float span = g_gripDt[(g_gripNext + 1) % 4] + g_gripDt[(g_gripNext + 2) % 4] + g_gripDt[(g_gripNext + 3) % 4];
    if (span <= 0.0f)
        return;
    const float v[3] = {(now.x - then.x) / span, (now.y - then.y) / span, (now.z - then.z) / span};
    const float speed = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    const float k = speed > kMaxDropSpeed ? kMaxDropSpeed / speed : 1.0f;
    for (int i = 0; i < 3; ++i)
        r.dropVelocity[i] = v[i] * k;
}

// A near miss told (once it's out of reach again, or out of the hand), and forgotten.
void Missed(Result& r) {
    if (g_missFrom < 0.0f)
        return;
    r.missed = true;
    r.missFrom = g_missFrom;
    r.missTurned = g_missTurned / kDegree;
    r.missWhy = g_missWhy;
    g_missFrom = -1.0f;
}

// Let go of: back in the pouch if let go at it, otherwise it falls from where it was drawn last.
void LetGo(Result& r, const Frame& f) {
    Missed(r);
    LeaveHand();
    r.putBack = f.atPouch;
    r.letGo = !f.atPouch;
    if (r.letGo)
        DropSpeed(r);
    g_state = State::kNone;
    r.holding = r.mag = false;
}

// The bare left palm's middle in gun space.
void PalmInGun(const Frame& f, float* out) {
    float palm[3], inverse[12];
    Transform(f.glove, kPalmBind, palm);
    InvertRigid(f.pistolPose, inverse);
    Transform(inverse, palm, out);
}

// The hand's push on the line: how far up it the palm has the magazine's top from where it was as the try began (s),
// and how far the palm has gone off the line since (aside).
struct Push {
    float s, aside;
};
Push PushNow(const Frame& f) {
    float palm[3];
    PalmInGun(f, palm);
    const float d[3] = {palm[0] - g_tryPalm[0], palm[1] - g_tryPalm[1], palm[2] - g_tryPalm[2]};
    const float along = Dot(d, g_line);
    const float off[3] = {d[0] - along * g_line[0], d[1] - along * g_line[1], d[2] - along * g_line[2]};
    return {g_tryFrom + along, std::sqrt(Dot(off, off))};
}

// A try on the line from s (caught, or pushed on from parked), the hand's push from where the palm is now: its first
// frame has no frame before it to count with.
void StartTry(const Frame& f, float s, float apart) {
    PalmInGun(f, g_tryPalm);
    g_tryFrom = s;
    g_state = State::kLine;
    g_along = s;
    g_offFor = 0;
    g_dwellFor = g_speed = 0.0f;
    g_dwellTop = s;
    g_trying = true;
    g_snapping = g_dwelt = false;
    g_closest = s, g_closestApart = apart;
    g_lastPush = -1.0f;
}

void EndTry(Result& r) {
    if (g_trying) {
        r.tried = true;
        r.closest = g_closest;
        r.apartThen = g_closestApart;
    }
    g_trying = g_snapping = false;
}

// Home: the pistol's own is drawn from now on; the hand drawn under it eases back to the controller from there.
void Seat(Result& r) {
    g_state = State::kNone;
    r.seated = true;
    r.dwelt = g_dwelt;
    EndTry(r);
    LeaveHand();
    r.mag = r.holding = false;
}

void Cancel(Result& r) {
    Missed(r);
    EndTry(r);
    Clear();
    r.cancelled = true;
}

}

Result Update(const Frame& f) {
    Prepare();
    Result r;
    const float dt = f.dt > 0.0f ? std::min(f.dt, 0.1f) : 0.0f;  // (not a number: none)
    const bool wentIn = g_wasEmpty && !f.wellEmpty;
    g_wasEmpty = f.wellEmpty;
    // A pose that isn't one (a broken pose) is the hand or the pistol not seen this frame.
    const bool left = f.left && Finite(f.glove), pistolTracked = f.pistolTracked && Finite(f.pistolPose);
    const float apart = std::isfinite(f.apart) ? f.apart : 0.0f;
    if (left)
        std::memcpy(g_glove, f.glove, sizeof(g_glove));

    // The hand drawn easing back to the controller after the magazine has gone from it, the fingers with it.
    EaseBack(dt, r);

    if (g_state == State::kNone) {
        if (!f.reached)
            return r;
        r.note = !f.handFree                      ? "the hand is busy"
                 : !f.pistol                      ? "the pistol not in hand"
                 : !pistolTracked                 ? "the pistol not tracked this frame"
                 : !f.wellEmpty && !f.slideLocked ? "the magazine is in: X first"
                 : !f.spare                       ? "no rounds left for it"
                                                  : "";
        if (*r.note)
            return r;
        g_state = State::kHand;
        g_heldFrames = 0;
        g_lostFor = g_gunLostFor = 0.0f;
        g_gripCount = 0;
        g_along = kMouth;
        g_glided = g_regrip = 0.0f;
        g_armed = true, g_haveTop = false;
        g_missFrom = -1.0f;
        g_awayFor = -1.0f;  // the fingers on it the frame it's taken
        r.gloveBlend = 0.0f;
        r.took = true;
        r.note = "took a magazine";
    }

    // Gone without a fall: a magazine went in some other way (the game's own reload, X held), or the pistol has been
    // out of hand a second (the pistol's "in hand" drops for a moment now and then: kept through that).
    if (wentIn) {
        Cancel(r);
        return r;
    }
    g_gunLostFor = f.pistol ? 0.0f : g_gunLostFor + dt;
    if (g_gunLostFor > kLost) {
        Cancel(r);
        return r;
    }

    // The hand or the pistol not seen: in the hand, kept a second (not drawn; let go of meanwhile, gone); on the line
    // it waits a moment for the hand (then it's parked) unless it's going home by itself (the snap needs nothing of the
    // hand: it goes on); gliding, it goes on gliding; parked, it stays.
    if (!left || !f.pistol || !pistolTracked) {
        g_gripCount = 0;
        g_haveTop = false;
        const float step = dt / g_glide;
        if (g_state == State::kHand) {
            g_lostFor += dt;
            if (g_lostFor > kLost || (left && f.squeeze < kArm)) {
                Cancel(r);
                return r;
            }
            g_glided = std::max(g_glided - step, 0.0f);
            g_regrip = std::max(g_regrip - step, 0.0f);
        } else if (g_state == State::kLine) {
            g_glided = std::min(g_glided + step, 1.0f);
            g_regrip = std::min(g_regrip + step, 1.0f);
            g_handIn = std::min(g_handIn + step, 1.0f);
            if (g_snapping) {
                // Going home by itself, it goes on as it would with the hand (the review: parked, a frame lost on the
                // way left it at home or near it, out of the palm's reach), the hand's place under it with it.
                g_snapFor += dt;
                g_along = SnapAt(g_snapFor);
            } else {
                g_lineLostFor += dt;
                if (g_lineLostFor > kLostOnLine) {
                    Show(f, f.pistol && pistolTracked, r);
                    g_state = State::kParked;
                    r.parked = true;
                    r.holding = false;
                    EndTry(r);
                    LeaveHand();
                    return r;
                }
            }
        } else if (g_state == State::kParked) {
            g_glided = std::min(g_glided + step, 1.0f);
        }
        Show(f, f.pistol && pistolTracked && (g_state != State::kHand || left), r);
        if (g_state == State::kLine && g_snapping && g_snapFor >= g_glide)
            Seat(r);
        return r;
    }
    g_lostFor = g_lineLostFor = 0.0f;
    g_gripAt[g_gripNext] = f.leftGrip, g_gripDt[g_gripNext] = dt;
    g_gripNext = (g_gripNext + 1) % 4, g_gripCount = std::min(g_gripCount + 1, 4);
    const float step = dt / g_glide;

    // ---- In the hand: carried as the carry holds it, caught magnetically ----
    bool caughtNow = false;
    if (g_state == State::kHand) {
        ++g_heldFrames;
        const Held held = AsHeld(f, g_carry);
        std::memcpy(g_handInGun, held.inGun, sizeof(g_handInGun));
        g_handS = held.s;
        // Pulled back out of the grip: it glides back into the hand, the hand drawn on it back onto the carry.
        g_glided = std::max(g_glided - step, 0.0f);
        g_regrip = std::max(g_regrip - step, 0.0f);
        // Its top's speed relative to the pistol (not known the frame it's seen again).
        float v[3] = {};
        const bool known = g_haveTop && dt > 0.0f;
        if (known)
            for (int i = 0; i < 3; ++i)
                v[i] = (held.top[i] - g_lastTop[i]) / dt;
        std::memcpy(g_lastTop, held.top, sizeof(g_lastTop));
        g_haveTop = true;
        float fromMouth = 0.0f, tilt = 0.0f, twist = 0.0f;
        const bool near = InReach(held.top, false, &fromMouth);
        Turns(held.inGun, tilt, twist);
        const char* why = "";
        if (near) {
            float lead[3];
            for (int i = 0; i < 3; ++i)
                lead[i] = held.top[i] + v[i] * kLead;
            why = !f.wellEmpty                    ? "the grip has its own magazine in"
                  : g_heldFrames < kHeldFrames    ? "only just taken"
                  : !g_armed                      ? "just pulled back out of the grip (out of reach first)"
                  : tilt > g_angle                ? "its length turned too far from the grip's line"
                  : twist > g_angle               ? "turned too far about its own length"
                  : !known                        ? "only just seen again (its speed not known)"
                  : Dot(v, v) > kFastest * kFastest ? "faster than a hand (a tracking jump)"
                  : !InReach(lead, true, nullptr) ? "going away from the grip, or passing it fast"
                                                  : "";
        } else {
            g_armed = true;
            Missed(r);
        }
        if (near && !*why) {
            // Caught: onto the line 1.5 cm into the grip (or as far in as its top already was), the hand's push read
            // from here on by how far the palm goes on up the line from where it is now.
            StartTry(f, std::clamp(held.s, kMouth + kGlideIn, 0.0f), apart);
            g_handIn = 1.0f;  // the hand drawn holding it is the controller's own as it's caught
            g_missFrom = -1.0f;
            g_haveTop = false;
            r.caught = true;
            r.caughtFrom = fromMouth;
            r.caughtTurned = tilt / kDegree;
            caughtNow = true;
        } else {
            if (near && (g_missFrom < 0.0f || fromMouth < g_missFrom))
                g_missFrom = fromMouth, g_missTurned = tilt, g_missWhy = why;
            Show(f, true, r);
            // Let go of away from the grip (out of its reach, or not to be caught): it falls from where it's drawn.
            if (f.squeeze < kArm)
                LetGo(r, f);
            return r;
        }
    }

    // ---- Parked in the well: a palm under its floorplate pushes it on ----
    if (g_state == State::kParked) {
        g_glided = std::min(g_glided + step, 1.0f);
        auto under = [](const Held& h) {
            return h.s >= g_along - kPushReach && h.lateral <= kPushAside && h.turned <= kPushTurned;
        };
        if (under(AsHeld(f, g_carry)) || under(AsHeld(f, g_push))) {
            // The hand drawn under it on the flat base, eased in from where it's drawn now.
            const float share = AwayShare();
            StartTry(f, g_along, apart);
            g_regrip = 1.0f;
            g_handIn = Unsmooth(share);
            g_awayFor = -1.0f;
            r.pushed = true;
        } else {
            Show(f, true, r);
            return r;
        }
    }

    // ---- On the line ----
    if (!caughtNow) {
        g_glided = std::min(g_glided + step, 1.0f);
        g_regrip = std::min(g_regrip + step, 1.0f);
        g_handIn = std::min(g_handIn + step, 1.0f);
    }
    // Where the hand has it as it carries it: the glide's start (gliding back, its end).
    std::memcpy(g_handInGun, AsHeld(f, g_carry).inGun, sizeof(g_handInGun));
    const Push push = PushNow(f);
    // How near the push brings its top to home, two frames running (as the snap goes by: a tracking jump is one).
    const float twoFrames = std::min(push.s, g_lastPush);
    g_lastPush = push.s;
    if (twoFrames > g_closest)
        g_closest = twoFrames, g_closestApart = apart;
    const float before = g_along;
    if (!g_snapping) {
        // The hand leaving it: still gliding in with the grip squeezed, it goes back out into the hand; otherwise it
        // stays where it is in the grip (parked).
        const bool back = g_glided < 1.0f && f.squeeze >= kArm;
        const bool off = back ? push.s < g_along - kOffBelow || push.aside > kOffAside
                              : push.s < g_along - kLeaveBelow || push.aside > kLeaveAside;
        g_offFor = off ? g_offFor + 1 : 0;
        if (g_offFor >= kOffFrames) {
            g_offFor = 0;
            EndTry(r);
            if (back) {
                g_state = State::kHand;
                g_handS = AsHeld(f, g_carry).s;
                g_armed = false;
                g_haveTop = false;
                r.offLine = true;
                Show(f, true, r);
                return r;
            }
            Show(f, true, r);  // the hand drawn under it this frame still
            g_state = State::kParked;
            r.parked = true;
            r.holding = false;
            LeaveHand();
            return r;
        }
        // Held there gripped: the dwell (drawing back 1 cm from the furthest it's been pushed starts it again).
        if (f.squeeze >= kArm && g_offFor == 0 && push.s >= g_dwellTop - kDwellSlack) {
            g_dwellTop = std::max(g_dwellTop, push.s);
            g_dwellFor += dt;
        } else {
            g_dwellTop = push.s;
            g_dwellFor = 0.0f;
        }
        // Within the snap two frames running, or held there the dwell: home by itself from here, on from where it is
        // at the speed it has (a carry whose fingers reach up the grip: once the hand drawn on it is over).
        const bool dwelt = g_dwell > 0.0f && g_dwellFor >= g_dwell;
        if (g_offFor == 0 && !Waiting() && (twoFrames >= -g_snap || dwelt)) {
            g_snapping = true;
            g_dwelt = twoFrames < -g_snap;
            g_snapFrom = g_along;
            g_snapSpeed = std::clamp(g_speed, 0.0f, 2.0f * -g_along / g_glide);
            g_snapFor = 0.0f;
        } else if (g_offFor == 0) {
            // As far as the hand goes on, 2 cm a frame at most, only up, never past home (nor a carry's fingers' reach
            // before the hand drawn on it is over).
            const float furthest = Waiting() ? std::max(kMouth + g_guide, g_along) : 0.0f;
            g_along = std::max(g_along, std::min({twoFrames, furthest, g_along + kMaxStep}));
        }
    }
    if (g_snapping) {
        g_snapFor += dt;
        g_along = SnapAt(g_snapFor);
    }
    g_speed = dt > 0.0f ? (g_along - before) / dt : 0.0f;
    Show(f, true, r);
    if (g_snapping && g_snapFor >= g_glide)
        Seat(r);
    return r;
}

bool Holding() {
    return g_state != State::kNone;
}

bool Parked() {
    return g_state == State::kParked;
}

void Clear() {
    if (g_state == State::kHand || g_state == State::kLine)
        LeaveHand();  // the hand drawn on it eases back from where it was drawn
    g_state = State::kNone;
    g_offFor = 0;
    g_lostFor = g_gunLostFor = g_lineLostFor = 0.0f;
    g_glided = g_regrip = 0.0f;
    g_trying = g_snapping = g_dwelt = false;
    g_dwellFor = 0.0f;
    g_missFrom = -1.0f;
}

void SetCarry(const float* hold, float guide) {
    Prepare();
    g_carryOwn = hold != nullptr;
    std::memcpy(g_carry, hold ? hold : g_push, sizeof(g_carry));
    Tidy(g_carry);
    g_guide = hold && guide > 0.0f ? std::min(guide, -kMouth) : 0.0f;  // (not a number: none)
}

void SetCatch(float metres, float degrees) {
    g_reach = metres > 0.0f ? std::clamp(metres, kReachLeast, kReachMost) : kReachLeast;  // (not a number: the least)
    g_angle = (degrees > 0.0f ? std::clamp(degrees, kAngleLeast, kAngleMost) : kAngleLeast) * kDegree;
}

void SetGlide(float seconds) {
    g_glide = seconds > 0.0f ? std::clamp(seconds, kGlideLeast, kGlideMost) : kGlideLeast;
}

void SetSnap(float metres) {
    g_snap = metres > 0.0f ? std::clamp(metres, kHome, kSnapMost) : kHome;  // (not a number: the least)
}

void SetDwell(float seconds) {
    g_dwell = seconds > 0.0f ? std::clamp(seconds, kDwellLeast, kDwellMost) : 0.0f;  // (0 or not a number: never)
}

Tuning Settings() {
    Tuning t;
    t.reach = g_reach;
    t.angle = g_angle / kDegree;
    t.glide = g_glide;
    t.snap = g_snap;
    t.dwell = g_dwell;
    return t;
}

void GloveFor(const float* magPose, bool flat, float* glove) {
    Prepare();
    float inverse[12];
    InvertRigid(flat ? g_push : g_carry, inverse);
    Compose(magPose, inverse, glove);
}

void PlaceOnLine(float s, float turnDegrees, float* inGun) {
    Prepare();
    const float a = turnDegrees * kDegree, c = std::cos(a), n = std::sin(a);
    const float turn[12] = {1, 0, 0, 0, 0, c, -n, 0, 0, n, c, 0};
    float turned[3];
    Transform(turn, g_top, turned);
    std::memcpy(inGun, turn, sizeof(turn));
    for (int i = 0; i < 3; ++i)
        inGun[i * 4 + 3] = g_top[i] + s * g_line[i] - turned[i];
}

void SetSeat(const float* seat) {
    Prepare();
    float line[3] = {seat[1], seat[5], seat[9]};
    const float n = std::sqrt(Dot(line, line));
    if (!(n > 0.0f))
        return;  // not a seat
    std::memcpy(g_seat, seat, sizeof(g_seat));
    for (int i = 0; i < 3; ++i)
        g_line[i] = line[i] / n;
    Transform(g_seat, kTopBind, g_top);
    Transform(g_seat, kBaseBind, g_base);
}

}
