#include "pouch.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pouch {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDegree = kPi / 180.0f;

// ---- The M416's magazine well, in its model space (x left, y up, z to the muzzle; from the mesh
// dump, build\gun_*.bin) ----
// The seated magazine's top middle; its top 10 cm are straight along the gun's +y, the well's line.
// The well's mouth (the receiver's underside) is 8.7 cm below home: s, the top's place along the line
// from home, is 0 seated and -0.087 at the mouth; h, the depth below the mouth, is -0.087 - s.
constexpr float kTop[3] = {0.0f, 0.0413f, 0.415f};
constexpr float kMouth = -0.087f;
// The funnel below the mouth (a flared well does this for real hands): at depth h (0-6 cm) the top
// may be r(h) = 6 mm + 34 mm h/6 cm off the line and the magazine turned theta(h) = 3 + 27 h/6 cm
// degrees from seated (one angle for tilt, twist, backwards and upside down). Caught only there, top
// first from below: a side entry above the mouth never is. Pushed up, it's straightened as it goes.
constexpr float kFunnelDepth = 0.06f;
float FunnelRadius(float h) {
    return 0.006f + 0.034f * std::clamp(h, 0.0f, kFunnelDepth) / kFunnelDepth;
}
float FunnelAngle(float h) {
    return (3.0f + 27.0f * std::clamp(h, 0.0f, kFunnelDepth) / kFunnelDepth) * kDegree;
}
// On the line it moves at most 3 cm a frame (a tracking jump can't throw it home) and never past
// home; within 5 mm of home for two frames it's seated. It comes off the line, back to the hand, when
// the hand goes 3 cm wider or 20 degrees more turned than the funnel allows, or 8 cm below the mouth;
// inside the well, when the hand is 12 cm from where the glove is drawn or turned 60 degrees.
constexpr float kMaxStep = 0.03f, kHome = 0.005f;
constexpr int kHomeFrames = 2, kHeldFrames = 2;
constexpr float kOffWider = 0.03f, kOffTurned = 20.0f * kDegree, kOffBelow = 0.08f;
constexpr float kOffInWell = 0.12f, kOffInWellTurned = 60.0f * kDegree;
// Off the line only after this many frames running (a tracking jump is a frame or two).
constexpr int kOffFrames = 3;
// The hand's middle in the gloves' bind space (the judge's fit: (0.045 -0.140 0.425) in the gun's
// space with the magazine seated): inside the well the hand is measured there from the drawn glove,
// not at the bind space's origin 1.24 m away (a 6 degree turn of the wrist moved that 12 cm).
constexpr float kHandInGlove[3] = {0.5363f, 1.0976f, 0.2092f};
// Above the mouth the funnel's last allowance (6 mm, 3 degrees) fades out over this much of the well.
constexpr float kMouthFade = 0.01f;
constexpr float kLost = 1.0f;  // s a lost hand keeps its magazine

// ---- The pouch: in front of the lower belly, below the belly button and above the groin (the
// user's picture). Nothing tracks the body, so it's placed from the headset: a neck point 7.5 cm below
// and 8 cm behind the eyes (looking down doesn't move it), and the body's heading. Its middle 60 cm
// straight below the neck and 19 cm in front of it; an ellipsoid 16 cm across, 13 cm up and down and
// 15 cm deep each way from it (for a 1.75 m player standing, 0.83-1.09 m off the floor). ----
float g_zone[5] = {0.60f, 0.19f, 0.16f, 0.13f, 0.15f};  // down, forward, half across, half up, half deep
bool g_zoneOn = true;
constexpr float kNeck[3] = {0.0f, -0.075f, 0.08f};  // from the eyes in the head's axes (x right, y up, z back)
constexpr float kPutBack = 1.2f;   // let go of within this of the pouch's size: back in it
constexpr float kLogNear = 0.4f;   // squeezes this near its middle are logged
// The body's heading: the head's turn about the vertical (its twist, defined looking straight down
// too). It follows the head only when they're more than 35 degrees apart, at up to 4 rad/s, and within
// that settles towards it at 5 degrees/s while the head turns slower than 20 degrees/s; it holds still
// while the left hand is within 25 cm of the pouch (reaching), and jumps with the head when that turns
// more than 45 degrees in one frame (recentred).
constexpr float kBodyLag = 35.0f * kDegree, kBodyTurn = 4.0f, kBodySettle = 5.0f * kDegree,
                kBodySteady = 20.0f * kDegree, kBodyReach = 0.25f, kBodyJump = 45.0f * kDegree;
float g_bodyYaw = 0.0f, g_headYaw = 0.0f;
bool g_haveBody = false;
float g_neck[3] = {};  // the neck point as of the last Update (room), for BodyFrame

// ---- The squeeze: armed below 0.4, crossing 0.6 from there; it reaches the pouch if the hand is in
// it within 0.15 s of the crossing, still squeezed (a squeeze held while sweeping through takes
// nothing). ----
constexpr float kArm = 0.4f, kTake = 0.6f, kReachWindow = 0.15f;
bool g_armed = true;
float g_time = 0.0f, g_crossedAt = -1.0f;
float g_crossedWhere[3] = {}, g_crossedSize = 0.0f;  // the open squeeze's crossing, for its one log line

// The magazine in the left glove, from the gun's model space with it seated into the gloves' bind
// space: its thin side against the palm, its top out above the thumb, its front along the fingers.
// Refitted 2026-10-31 on the meshes with the glove posed as the game poses it (palm width 1.15, the
// fingers and thumb as hands.cpp sets them on a magazine): the thumb and the ball of the thumb along
// its left side, the index, middle and ring round its front edge, the little finger under the base
// plate; no glove point more than 0.8 mm into it (the judge's earlier fit had the thumb 11 mm in);
// the glove 17 mm or more clear of the gun all the way up the well; the hand's middle at
// (0.047 -0.139 0.430) in the gun's space with it seated.
float g_hold[12] = {0.48852f, -0.73548f, 0.46948f, 0.20951f,  //
                    0.84399f, 0.26178f,  -0.46813f, 1.29586f,  //
                    0.22140f, 0.62493f,  0.74862f,  -0.03828f};

enum class State { kNone, kHand, kLine };
State g_state = State::kNone;
int g_heldFrames = 0, g_homeFor = 0;
int g_offFor = 0;          // frames running the hand has been off the line
bool g_fromAbove = false;  // the held magazine's top went above the mouth in the gun's way
float g_along = 0.0f;       // the drawn magazine's s on the line
float g_line[12] = {};      // its place in the gun's space there (as last drawn)
float g_lostFor = 0.0f;
bool g_wasEmpty = false;
float g_gloveBlend = 0.0f;  // the glove drawn on the magazine on the line: 0-1
float g_gloveInGun[12] = {};  // ... there, in the gun's space (it eases back to the hand from it)
// The grip point over the last four frames held (room) and their lengths: the hand's speed when it
// lets go, which the magazine falls with.
XrVector3f g_gripAt[4] = {};
float g_gripDt[4] = {};
int g_gripNext = 0, g_gripCount = 0;
constexpr float kMaxDropSpeed = 3.0f;  // m/s

void Compose(const float* a, const float* b, float* out) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j)
            out[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j];
        out[i * 4 + 3] += a[i * 4 + 3];
    }
}

// A rotation | translation's rotation made exactly orthonormal (the hold's is written to 3 decimals:
// its transpose was off its inverse by up to 0.3 mm at the magazine).
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
const bool g_holdTidy = (Tidy(g_hold), true);

// A rotation | translation inverted (the rotation's transpose).
void InvertRigid(const float* m, float* out) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out[i * 4 + j] = m[j * 4 + i];
    for (int i = 0; i < 3; ++i)
        out[i * 4 + 3] = -(out[i * 4] * m[3] + out[i * 4 + 1] * m[7] + out[i * 4 + 2] * m[11]);
}

void Transform(const float* m, const float* p, float* out) {
    for (int i = 0; i < 3; ++i)
        out[i] = m[i * 4] * p[0] + m[i * 4 + 1] * p[1] + m[i * 4 + 2] * p[2] + m[i * 4 + 3];
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
    FromQuat(q, out);
    for (int i = 0; i < 3; ++i)
        out[i * 4 + 3] = a[i * 4 + 3] + (b[i * 4 + 3] - a[i * 4 + 3]) * s;
}

// How far a 3x4's rotation turns (radians, 0-pi).
float TurnAngle(const float* m) {
    return std::acos(std::clamp((m[0] + m[5] + m[10] - 1.0f) * 0.5f, -1.0f, 1.0f));
}

XrVector3f Rotate(const XrQuaternionf& q, const XrVector3f& v) {
    const XrVector3f u{q.x, q.y, q.z};
    const XrVector3f t{2 * (u.y * v.z - u.z * v.y), 2 * (u.z * v.x - u.x * v.z), 2 * (u.x * v.y - u.y * v.x)};
    return {v.x + q.w * t.x + (u.y * t.z - u.z * t.y), v.y + q.w * t.y + (u.z * t.x - u.x * t.z),
            v.z + q.w * t.z + (u.x * t.y - u.y * t.x)};
}

float Wrap(float a) {
    return std::remainder(a, 2.0f * kPi);
}

// The grip point in the pouch's axes from its middle (right, up, forward), the body's heading
// followed first.
void FromPouch(const Frame& f, float* out) {
    const XrQuaternionf& q = f.head.orientation;
    const float head = 2.0f * std::atan2(q.y, q.w);  // the head's twist about the vertical
    const float jump = std::fabs(Wrap(head - g_headYaw));
    const float headTurn = f.dt > 0.0f ? jump / f.dt : 0.0f;
    g_headYaw = head;
    const XrVector3f neckOffset = Rotate(q, {kNeck[0], kNeck[1], kNeck[2]});
    const float neck[3] = {f.head.position.x + neckOffset.x, f.head.position.y + neckOffset.y,
                           f.head.position.z + neckOffset.z};
    std::memcpy(g_neck, neck, sizeof(g_neck));
    auto middleFor = [&](float yaw, float* m) {
        m[0] = neck[0] - std::sin(yaw) * g_zone[1];
        m[1] = neck[1] - g_zone[0];
        m[2] = neck[2] - std::cos(yaw) * g_zone[1];
    };
    if (!g_haveBody || jump > kBodyJump) {
        g_bodyYaw = head;
        g_haveBody = true;
    } else {
        float middle[3];
        middleFor(g_bodyYaw, middle);
        const float near = std::sqrt((f.leftGrip.x - middle[0]) * (f.leftGrip.x - middle[0]) +
                                     (f.leftGrip.y - middle[1]) * (f.leftGrip.y - middle[1]) +
                                     (f.leftGrip.z - middle[2]) * (f.leftGrip.z - middle[2]));
        if (!(f.left && near < kBodyReach)) {
            const float apart = Wrap(head - g_bodyYaw);
            float turn = 0.0f;
            if (std::fabs(apart) > kBodyLag)
                turn = std::min(std::fabs(apart) - kBodyLag, kBodyTurn * f.dt);
            else if (headTurn < kBodySteady)
                turn = std::min(std::fabs(apart), kBodySettle * f.dt);
            g_bodyYaw = Wrap(g_bodyYaw + (apart > 0.0f ? turn : -turn));
        }
    }
    float middle[3];
    middleFor(g_bodyYaw, middle);
    const float forward[3] = {-std::sin(g_bodyYaw), 0.0f, -std::cos(g_bodyYaw)};
    const float right[3] = {std::cos(g_bodyYaw), 0.0f, -std::sin(g_bodyYaw)};
    const float d[3] = {f.leftGrip.x - middle[0], f.leftGrip.y - middle[1], f.leftGrip.z - middle[2]};
    out[0] = d[0] * right[0] + d[2] * right[2];
    out[1] = d[1];
    out[2] = d[0] * forward[0] + d[2] * forward[2];
}

// The magazine as the hand holds it (in the view's space), and in the gun's space (inGun) with its
// top there.
void AsHeld(const Frame& f, float* mag, float* inGun, float* top) {
    Compose(f.glove, g_hold, mag);
    float gunInverse[12];
    InvertRigid(f.gunPose, gunInverse);
    Compose(gunInverse, mag, inGun);
    Transform(inGun, kTop, top);
}

float Lateral(const float* top) {
    return std::sqrt(top[0] * top[0] + (top[2] - kTop[2]) * (top[2] - kTop[2]));
}

// The drawn magazine on the line in the gun's space: at s along it; below the mouth the hand's turn
// and offset, clamped to the funnel there (straightened as it rises); at the mouth and above, on the
// line exactly.
void OnLine(const float* held, const float* heldTop, float s, float* out) {
    const float h = kMouth - s;
    const float fade = h >= 0.0f ? 1.0f : std::max(1.0f + h / kMouthFade, 0.0f);
    if (fade <= 0.0f) {
        const float onLine[12] = {1, 0, 0, 0, 0, 1, 0, s, 0, 0, 1, 0};
        std::memcpy(out, onLine, sizeof(onLine));
        return;
    }
    // The turn: the hand's, at most the funnel's angle (the same axis, less far round).
    const float identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    const float angle = TurnAngle(held), most = FunnelAngle(h) * fade;
    Blend(identity, held, angle > most && angle > 0.0f ? most / angle : 1.0f, out);
    // The top off the line by the hand's offset, at most the funnel's radius.
    float off[2] = {heldTop[0], heldTop[2] - kTop[2]};
    const float lateral = std::sqrt(off[0] * off[0] + off[1] * off[1]), radius = FunnelRadius(h) * fade;
    if (lateral > radius)
        off[0] *= radius / lateral, off[1] *= radius / lateral;
    const float top[3] = {off[0], kTop[1] + s, kTop[2] + off[1]};
    float turned[3];
    for (int i = 0; i < 3; ++i)
        turned[i] = out[i * 4] * kTop[0] + out[i * 4 + 1] * kTop[1] + out[i * 4 + 2] * kTop[2];
    for (int i = 0; i < 3; ++i)
        out[i * 4 + 3] = top[i] - turned[i];
}

void GloveOn(const float* magInGun, float* gloveInGun) {
    float holdInverse[12];
    InvertRigid(g_hold, holdInverse);
    Compose(magInGun, holdInverse, gloveInGun);
}

}

Result Update(const Frame& f) {
    Result r;
    const float dt = std::clamp(f.dt, 0.0f, 0.1f);
    g_time += dt;
    float where[3];
    FromPouch(f, where);
    const float size = std::sqrt((where[0] / g_zone[2]) * (where[0] / g_zone[2]) + (where[1] / g_zone[3]) * (where[1] / g_zone[3]) +
                                 (where[2] / g_zone[4]) * (where[2] / g_zone[4]));
    const bool inside = g_zoneOn && f.left && (f.forceInside || size <= 1.0f);
    const bool atPouch = g_zoneOn && f.left && (f.forceInside || size <= kPutBack);
    // The squeeze: armed, crossed, and reaching the pouch within 0.15 s of crossing. Logged once, when
    // it's settled: reached (where the hand came in), or not (where it crossed).
    if (f.squeeze < kArm)
        g_armed = true;
    if (f.left && g_armed && f.squeeze > kTake) {
        g_armed = false;
        g_crossedAt = g_time;
        std::memcpy(g_crossedWhere, where, sizeof(where));
        g_crossedSize = size;
    }
    bool missed = false;
    if (g_crossedAt >= 0.0f && (g_time - g_crossedAt > kReachWindow || f.squeeze <= kTake)) {
        g_crossedAt = -1.0f;
        missed = true;
    }
    if (g_crossedAt >= 0.0f && inside) {
        r.reached = true;
        g_crossedAt = -1.0f;
    }
    if (missed || r.reached) {
        const float* at = r.reached ? where : g_crossedWhere;
        const float distance = std::sqrt(at[0] * at[0] + at[1] * at[1] + at[2] * at[2]);
        if (distance < kLogNear || r.reached) {
            r.squeezed = true;
            std::memcpy(r.fromMiddle, at, sizeof(r.fromMiddle));
            r.size = f.forceInside ? 0.0f : r.reached ? size : g_crossedSize;
        }
    }
    const bool wentIn = g_wasEmpty && !f.wellEmpty;
    g_wasEmpty = f.wellEmpty;

    // The glove eases back to the hand from the line (after a seat, or let go of on it).
    if (g_state != State::kLine && g_gloveBlend > 0.0f) {
        g_gloveBlend = std::max(g_gloveBlend - 0.35f, 0.0f);
        if (f.gun && f.gunTracked) {
            Compose(f.gunPose, g_gloveInGun, r.glove);
            r.gloveBlend = g_gloveBlend;
        }
    }

    if (g_state != State::kNone) {
        // Gone without a fall: the gun went, or got a magazine some other way (X, the game's reload,
        // a new life).
        if (!f.gun || wentIn) {
            Clear();
            r.cancelled = true;
            return r;
        }
    } else {
        if (!r.reached)
            return r;
        // A squeeze at the pouch: a magazine if the hand and the gun can take one.
        r.note = !f.handFree               ? "the hand is busy"
                 : !f.gun                  ? "no gun the pouch knows (its own reload)"
                 : !f.gunTracked           ? "the gun not tracked this frame"
                 : f.guardDistance < 0.10f ? "too near the handguard"
                 : !f.wellEmpty && !f.dry  ? "the magazine is in and has rounds: X first"
                 : !f.spare                ? "no rounds left for it"
                                           : "";
        if (*r.note)
            return r;
        g_state = State::kHand;
        g_heldFrames = 0;
        g_lostFor = 0.0f;
        g_fromAbove = false;
        g_gripCount = 0;
        r.took = true;
        r.note = "took a magazine";
    }

    // The hand lost, or the gun (the right controller): the magazine kept (not drawn in the hand; on the
    // line where it was, with the gun seen) for a second.
    if (!f.left || !f.gunTracked) {
        g_lostFor += dt;
        g_gripCount = 0;
        if (g_lostFor > kLost || f.squeeze < kArm) {  // lost a second, or let go of while lost: gone
            Clear();
            r.cancelled = true;
            return r;
        }
        r.holding = true;
        if (g_state == State::kLine && f.gunTracked) {
            Compose(f.gunPose, g_line, r.magPose);
            r.mag = true;
        }
        return r;
    }
    g_lostFor = 0.0f;
    ++g_heldFrames;
    g_gripAt[g_gripNext] = f.leftGrip, g_gripDt[g_gripNext] = dt;
    g_gripNext = (g_gripNext + 1) % 4, g_gripCount = std::min(g_gripCount + 1, 4);

    float held[12], inGun[12], top[3];
    AsHeld(f, held, inGun, top);
    const float s = top[1] - kTop[1], h = kMouth - s, turned = TurnAngle(inGun), lateral = Lateral(top);
    if (g_state == State::kHand) {
        // Caught in the funnel: below the mouth, near enough the line, turned little enough.
        // Top first from below or the side, not down through the receiver: once above the mouth in
        // the gun's way, not caught until it's been below the funnel or well to the side of it.
        if (h > kFunnelDepth || lateral > FunnelRadius(kFunnelDepth) + kOffWider)
            g_fromAbove = false;
        else if (h < 0.0f)
            g_fromAbove = true;
        if (!g_fromAbove && f.wellEmpty && g_heldFrames >= kHeldFrames && h >= 0.0f && h <= kFunnelDepth &&
            lateral <= FunnelRadius(h) && turned <= FunnelAngle(h)) {
            g_state = State::kLine;
            g_along = std::min(s, 0.0f);
            g_homeFor = 0;
            g_offFor = 0;
            r.caught = true;
        }
    } else {
        // Pulled off the line: below the mouth, past the funnel's allowance; inside the well, the hand
        // far from the drawn glove or turned round.
        float gloveInGun[12];
        GloveOn(g_line, gloveInGun);
        float gloveHere[12], gunInverse[12];
        InvertRigid(f.gunPose, gunInverse);
        Compose(gunInverse, f.glove, gloveHere);
        float here[3], drawn[3];
        Transform(gloveHere, kHandInGlove, here);
        Transform(gloveInGun, kHandInGlove, drawn);
        const float apart = std::sqrt((here[0] - drawn[0]) * (here[0] - drawn[0]) + (here[1] - drawn[1]) * (here[1] - drawn[1]) +
                                      (here[2] - drawn[2]) * (here[2] - drawn[2]));
        const bool off = kMouth - g_along > 0.0f
                             ? lateral > FunnelRadius(h) + kOffWider || turned > FunnelAngle(h) + kOffTurned || h > kOffBelow
                             : apart > kOffInWell || turned > kOffInWellTurned;
        g_offFor = off ? g_offFor + 1 : 0;
        if (g_offFor >= kOffFrames) {
            g_state = State::kHand;
            r.offLine = true;
            g_gloveBlend = 0.0f;  // back in the hand as it's held: the glove with its magazine, no ease to the line
        }
    }
    if (g_state == State::kLine) {
        // Along the line as far as the hand pushes it, at most 3 cm a frame, never past home.
        // ... and held where it is while the hand is off the line (a tracking jump moves and seats nothing).
        if (g_offFor == 0) {
            g_along = std::clamp(std::min(s, 0.0f), g_along - kMaxStep, g_along + kMaxStep);
            OnLine(inGun, top, g_along, g_line);
            g_homeFor = g_along >= -kHome ? g_homeFor + 1 : 0;
        }
        Compose(f.gunPose, g_line, r.magPose);
        r.mag = true;
        GloveOn(g_line, g_gloveInGun);
        g_gloveBlend = std::min(g_gloveBlend + 0.35f, 1.0f);
        Compose(f.gunPose, g_gloveInGun, r.glove);
        r.gloveBlend = g_gloveBlend;
        // Home for two frames: seated (before any letting go this frame).
        if (g_homeFor >= kHomeFrames) {
            g_state = State::kNone;
            r.seated = true;
            r.mag = false;  // the gun's own is drawn from now on
            return r;
        }
    } else {
        std::memcpy(r.magPose, held, sizeof(held));
        r.mag = true;
    }
    r.holding = true;
    if (f.squeeze < kArm) {
        // Let go of: back in the pouch if let go at it, otherwise it falls from where it's drawn, at the
        // hand's speed over its last three frames (at most 3 m/s).
        g_state = State::kNone;
        r.holding = false;
        r.mag = false;
        r.putBack = atPouch;
        r.letGo = !atPouch;
        if (r.letGo && g_gripCount == 4) {
            const XrVector3f &now = g_gripAt[(g_gripNext + 3) % 4], &then = g_gripAt[g_gripNext];
            const float span = g_gripDt[(g_gripNext + 1) % 4] + g_gripDt[(g_gripNext + 2) % 4] + g_gripDt[(g_gripNext + 3) % 4];
            if (span > 0.0f) {
                const float v[3] = {(now.x - then.x) / span, (now.y - then.y) / span, (now.z - then.z) / span};
                const float speed = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                const float k = speed > kMaxDropSpeed ? kMaxDropSpeed / speed : 1.0f;
                for (int i = 0; i < 3; ++i)
                    r.dropVelocity[i] = v[i] * k;
            }
        }
    }
    return r;
}

bool Holding() {
    return g_state != State::kNone;
}

bool BodyFrame(float neck[3], float& yaw) {
    if (!g_haveBody)
        return false;
    std::memcpy(neck, g_neck, sizeof(g_neck));
    yaw = g_bodyYaw;
    return true;
}

void Clear() {
    g_state = State::kNone;
    g_gloveBlend = 0.0f;  // gone: the glove doesn't ease back towards where it was
    g_homeFor = 0;
    g_lostFor = 0.0f;
}

void SetZone(const float* zone, bool on) {
    if (zone)
        std::memcpy(g_zone, zone, sizeof(g_zone));
    g_zoneOn = on;
}

void SetHold(const float* hold) {
    std::memcpy(g_hold, hold, sizeof(g_hold));
    Tidy(g_hold);
}

void GloveFor(const float* magPose, float* glove) {
    float holdInverse[12];
    InvertRigid(g_hold, holdInverse);
    Compose(magPose, holdInverse, glove);
}

}
