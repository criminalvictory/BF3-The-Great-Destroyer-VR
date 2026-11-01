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
// The funnel below the mouth (a beveled magwell, made forgiving for hands in the air): at depth h (0-5 cm) the top
// may be r(h) = 5 mm + 25 mm h/5 cm off the line and the magazine turned 3 + 22 h/5 cm degrees from home. Caught only
// there, top first from below. Above the mouth the last allowance fades over 1 cm.
constexpr float kFunnelDepth = 0.05f;
float FunnelRadius(float h) {
    return 0.005f + 0.025f * std::clamp(h, 0.0f, kFunnelDepth) / kFunnelDepth;
}
float FunnelAngle(float h) {
    return (3.0f + 22.0f * std::clamp(h, 0.0f, kFunnelDepth) / kFunnelDepth) * kDegree;
}
constexpr float kMouthFade = 0.01f;
// On the line: at most 3 cm a frame, never past home. Below the mouth it rests on the palm and goes with it either way;
// 1 cm in (kInWell) the well holds it, and the palm only pushes it up. Below the mouth it comes off the line, back to
// the hand, when the hand goes 3 cm wider or 20 degrees more turned than the funnel allows, or 6 cm below the mouth; in
// the well the hand leaving its floorplate (2.5 cm below it, 5 cm aside or 50 degrees turned) leaves it there: parked.
// Off only after three frames (a tracking jump is one or two).
constexpr float kMaxStep = 0.03f, kInWell = 0.01f;
constexpr int kHeldFrames = 2, kOffFrames = 3;
constexpr float kOffWider = 0.03f, kOffTurned = 20.0f * kDegree, kOffBelow = 0.06f;
constexpr float kLeaveBelow = 0.025f, kLeaveAside = 0.05f, kLeaveTurned = 50.0f * kDegree;
// Home (the review: the controllers can't overlap as the hands would under the grip, so seating by the palm alone is
// probably out of reach). Once the palm's push (its top where the flat base would hold it) has been within the snap
// distance of home for two frames running (a tracking jump is one: the well holding it up, a jump's 3 cm would stay),
// it goes on home by itself, 3 cm a frame whatever the hand does, and is seated after two frames there. The snap is
// 2 cm until the first headset test says otherwise (SetSnap: pistol.txt "seat"), never under 5 mm nor more than 10 cm
// (a centimetre into the grip).
constexpr float kHome = 0.005f, kSnapMost = 0.10f;
constexpr int kHomeFrames = 2;
float g_snap = 0.02f;
// A parked magazine is pushed again by a palm under its floorplate: the hand's push hold puts its top within 1 cm
// below where it's parked (or above), within 3 cm of the line and 40 degrees.
constexpr float kPushReach = 0.01f, kPushAside = 0.03f, kPushTurned = 40.0f * kDegree;
// The flat base: on when the magazine's top comes within 12 cm below the mouth (to 2 cm into the grip), within 4 cm
// of the line and 35 degrees of home; off beyond 16 cm, 6 cm or 45 degrees. Eased over 0.15 s.
constexpr float kFlatOn = 0.12f, kFlatOff = 0.16f;
constexpr float kFlatAside = 0.04f, kFlatAsideOff = 0.06f;
constexpr float kFlatTurned = 35.0f * kDegree, kFlatTurnedOff = 45.0f * kDegree;
constexpr float kFlatRate = 1.0f / 0.15f;
// The game's own push home (2.421 s) comes in over the magazine's last 3 cm.
constexpr float kPushFrom = 0.03f;
constexpr float kLost = 1.0f;          // s a lost hand keeps its magazine (and the pistol, out of hand)
constexpr float kArm = 0.4f;           // the grip below this: let go
constexpr float kMaxDropSpeed = 3.0f;  // m/s

// ---- The holds (pistolposes.h: gun space with the magazine home -> the arms' bind space) ----
// The flat base (B) and the push home (B2) are the game's own hands; the carry is the flat base's place with the
// fingers closed (C2) unless SetCarry gives it a place of its own (the game's pinch).
float g_push[12] = {};
float g_seated[12] = {};
float g_carry[12] = {};
bool g_carryOwn = false;

enum class State { kNone, kHand, kLine, kParked };
State g_state = State::kNone;
int g_heldFrames = 0, g_homeFor = 0, g_offFor = 0;
bool g_fromAbove = false;
float g_along = 0.0f;      // the magazine's s on the line (kLine, kParked)
float g_onLine[12] = {};   // its place in gun space there (as last drawn)
float g_lostFor = 0.0f, g_gunLostFor = 0.0f;
bool g_wasEmpty = false;
float g_flat = 0.0f;       // 0 carry, 1 the flat base
bool g_flatWanted = false;
float g_gloveBlend = 0.0f;
float g_gloveInGun[12] = {};
// This try on the line: going home by itself, the palm's push the frame before, and the nearest the palm has held the
// top to home for two frames running, with the hands apart then.
bool g_trying = false, g_snapping = false;
float g_lastPush = 0.0f, g_closest = 0.0f, g_closestApart = 0.0f;
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
}

// The hold in use in the hand: the carry, the flat base, or between (a carry with its own place; C2 is the flat
// base's).
void Hold(float flat, float* hold) {
    if (!g_carryOwn) {
        std::memcpy(hold, g_push, sizeof(g_push));
        return;
    }
    Blend(g_carry, g_push, flat, hold);
}

// The push home's share on the line at s: none until its last 3 cm, all of it home.
float PushShare(float s) {
    return std::clamp(1.0f + s / kPushFrom, 0.0f, 1.0f);
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

// The magazine on the line at s (gun space home -> gun space): below the mouth with the hand's turn and offset as
// far as the funnel allows there (straightened as it rises); in the well, on the line exactly.
void OnLine(const Held& held, float s, float* out) {
    const float h = kMouth - s;
    const float fade = h >= 0.0f ? 1.0f : std::max(1.0f + h / kMouthFade, 0.0f);
    const float identity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    float off[3] = {};
    if (fade > 0.0f) {
        const float most = FunnelAngle(h) * fade;
        Blend(identity, held.inGun, held.turned > most && held.turned > 0.0f ? most / held.turned : 1.0f, out);
        const float d[3] = {held.top[0] - g_top[0], held.top[1] - g_top[1], held.top[2] - g_top[2]};
        const float along = Dot(d, g_line);
        for (int i = 0; i < 3; ++i)
            off[i] = d[i] - along * g_line[i];
        const float lateral = std::sqrt(Dot(off, off)), radius = FunnelRadius(h) * fade;
        if (lateral > radius)
            for (float& v : off)
                v *= radius / lateral;
    } else {
        std::memcpy(out, identity, sizeof(identity));
    }
    // The top there: home's moved s along the line and off it by off; the rest turns about it.
    float top[3], turned[3];
    for (int i = 0; i < 3; ++i)
        top[i] = g_top[i] + s * g_line[i] + off[i];
    for (int i = 0; i < 3; ++i)
        turned[i] = out[i * 4] * g_top[0] + out[i * 4 + 1] * g_top[1] + out[i * 4 + 2] * g_top[2];
    for (int i = 0; i < 3; ++i)
        out[i * 4 + 3] = top[i] - turned[i];
}

// The hand holding a magazine at magInGun with hold, in gun space.
void GloveOn(const float* magInGun, const float* hold, float* gloveInGun) {
    float inverse[12];
    InvertRigid(hold, inverse);
    Compose(magInGun, inverse, gloveInGun);
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

// Let go of: back in the pouch if let go at it, otherwise it falls from where it was drawn last.
void LetGo(Result& r, const Frame& f) {
    r.putBack = f.atPouch;
    r.letGo = !f.atPouch;
    if (r.letGo)
        DropSpeed(r);
    g_state = State::kNone;
    r.holding = r.mag = false;
}

// A try on the line from s (caught, or pushed on from parked): its first frame has no frame before it to count with.
void StartTry(float s, float apart) {
    g_trying = true;
    g_snapping = false;
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

void Cancel(Result& r) {
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

    // The glove eases back to the hand from under the magazine (after a seat, a park, or off the line).
    if (g_state != State::kLine && g_gloveBlend > 0.0f) {
        g_gloveBlend = std::max(g_gloveBlend - 0.35f, 0.0f);
        if (f.pistol && pistolTracked) {
            Compose(f.pistolPose, g_gloveInGun, r.glove);
            r.gloveBlend = g_gloveBlend;
        }
    }

    if (g_state == State::kNone) {
        g_flat = 0.0f;
        g_flatWanted = false;
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
        g_fromAbove = false;
        g_gripCount = 0;
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

    // The hand or the pistol not seen: in the hand, kept a second (not drawn); on the line in the well it stays there
    // (parked), below the mouth it waits on the line for the hand; parked, it stays.
    if (!left || !f.pistol || !pistolTracked) {
        g_gripCount = 0;
        if (g_state == State::kHand) {
            g_lostFor += dt;
            if (g_lostFor > kLost || (left && f.squeeze < kArm && g_flat < 0.5f)) {
                Cancel(r);
                return r;
            }
        } else if (g_state == State::kLine && g_along > kMouth + kInWell) {
            g_state = State::kParked;
            r.parked = true;
            EndTry(r);
        }
        r.holding = g_state != State::kParked;
        if (g_state != State::kHand && f.pistol && pistolTracked) {
            Compose(f.pistolPose, g_onLine, r.magPose);
            r.mag = true;
        }
        r.s = g_along;
        return r;
    }
    g_lostFor = 0.0f;
    g_gripAt[g_gripNext] = f.leftGrip, g_gripDt[g_gripNext] = dt;
    g_gripNext = (g_gripNext + 1) % 4, g_gripCount = std::min(g_gripCount + 1, 4);

    // ---- In the hand: the carry, the flat base near the grip, caught in the funnel ----
    if (g_state == State::kHand) {
        ++g_heldFrames;
        float hold[12];
        Hold(g_flat, hold);
        Held held = AsHeld(f, hold);
        const float h = kMouth - held.s;
        // The flat base wanted: near the grip's bottom, lined up.
        const bool on = h >= -0.02f && h <= kFlatOn && held.lateral <= kFlatAside && held.turned <= kFlatTurned;
        const bool off = h > kFlatOff || held.lateral > kFlatAsideOff || held.turned > kFlatTurnedOff || h < -0.04f;
        if (!g_flatWanted && on)
            g_flatWanted = r.flatOn = true;
        else if (g_flatWanted && off)
            g_flatWanted = false, r.flatOff = true;
        g_flat = std::clamp(g_flat + (g_flatWanted ? 1.0f : -1.0f) * kFlatRate * dt, 0.0f, 1.0f);
        // Let go of: on the flat base the magazine stands on the open palm (the hand opens with the picture: the user
        // may let go too); anywhere else, or moved away from the grip with nothing holding it, it falls.
        if (f.squeeze < kArm && !g_flatWanted) {
            Hold(g_flat, hold);
            held = AsHeld(f, hold);
            std::memcpy(r.magPose, held.pose, sizeof(r.magPose));
            LetGo(r, f);
            return r;
        }
        // Top first from below: once above the mouth in the gun's way, not caught until it's been below the funnel or
        // well to the side of it.
        if (h > kFunnelDepth || held.lateral > FunnelRadius(kFunnelDepth) + kOffWider)
            g_fromAbove = false;
        else if (h < 0.0f)
            g_fromAbove = true;
        // Caught: in the funnel, lined up, on the flat base (a carry with its own place: once it's turned onto the
        // palm).
        if (!g_fromAbove && f.wellEmpty && g_heldFrames >= kHeldFrames && h >= 0.0f && h <= kFunnelDepth &&
            held.lateral <= FunnelRadius(h) && held.turned <= FunnelAngle(h) && (!g_carryOwn || g_flat >= 0.95f)) {
            g_state = State::kLine;
            g_along = std::min(held.s, 0.0f);
            g_homeFor = g_offFor = 0;
            StartTry(held.s, apart);
            r.caught = true;
        } else {
            std::memcpy(r.magPose, held.pose, sizeof(r.magPose));
            r.mag = r.holding = true;
            r.flat = g_flat;
            r.s = held.s;
            return r;
        }
    }

    // ---- Parked in the well: a palm under its floorplate pushes it on ----
    if (g_state == State::kParked) {
        const Held held = AsHeld(f, g_push);
        if (held.s >= g_along - kPushReach && held.lateral <= kPushAside && held.turned <= kPushTurned) {
            g_state = State::kLine;
            g_homeFor = g_offFor = 0;
            StartTry(held.s, apart);
            r.pushed = true;
        } else {
            Compose(f.pistolPose, g_onLine, r.magPose);
            r.mag = true;
            r.s = g_along;
            return r;
        }
    }

    // ---- On the line ----
    g_flat = 1.0f;
    const Held held = AsHeld(f, g_push);
    const float h = kMouth - held.s;
    const bool inWell = g_along > kMouth + kInWell;
    // How near the palm brings its top to home, two frames running (as the snap goes by: a tracking jump is one).
    const float twoFrames = std::min(held.s, g_lastPush);
    g_lastPush = held.s;
    if (twoFrames > g_closest)
        g_closest = twoFrames, g_closestApart = apart;
    if (!g_snapping) {
        bool off;
        if (!inWell)
            off = held.lateral > FunnelRadius(h) + kOffWider || held.turned > FunnelAngle(h) + kOffTurned ||
                  h > kOffBelow;
        else
            off = held.s < g_along - kLeaveBelow || held.lateral > kLeaveAside || held.turned > kLeaveTurned;
        g_offFor = off ? g_offFor + 1 : 0;
        if (g_offFor >= kOffFrames) {
            g_offFor = 0;
            EndTry(r);
            if (inWell) {
                // Left in the well: it stays (parked); the hand goes back to where it is.
                g_state = State::kParked;
                r.parked = true;
                Compose(f.pistolPose, g_onLine, r.magPose);
                r.mag = true;
                r.s = g_along;
                return r;
            }
            // Below the mouth: off the line, back in the hand as it's held (or, let go of, it falls off the palm).
            g_state = State::kHand;
            g_flatWanted = false;
            g_gloveBlend = 0.0f;
            r.offLine = true;
            float hold[12];
            Hold(g_flat, hold);
            const Held back = AsHeld(f, hold);
            std::memcpy(r.magPose, back.pose, sizeof(r.magPose));
            if (f.squeeze < kArm) {
                LetGo(r, f);
                return r;
            }
            r.mag = r.holding = true;
            r.flat = g_flat;
            r.s = back.s;
            return r;
        }
        // Within the snap for two frames running, on the line: home by itself from here.
        if (g_offFor == 0 && twoFrames >= -g_snap)
            g_snapping = true;
    }
    if (g_snapping) {
        g_along = std::min(g_along + kMaxStep, 0.0f);
        OnLine(held, g_along, g_onLine);
    } else if (g_offFor == 0) {
        // As far as the palm pushes it, 3 cm a frame at most, never past home; in the well only up (it holds it).
        float want = std::min(held.s, 0.0f);
        if (inWell)
            want = std::max(want, g_along);
        g_along = std::clamp(want, g_along - kMaxStep, g_along + kMaxStep);
        OnLine(held, g_along, g_onLine);
    }
    g_homeFor = g_snapping && g_along >= -kHome ? g_homeFor + 1 : 0;
    Compose(f.pistolPose, g_onLine, r.magPose);
    r.mag = r.holding = true;
    r.flat = 1.0f;
    r.push = PushShare(g_along);
    r.s = g_along;
    // The hand under it: the flat base, going over to the push home near the top.
    float hold[12];
    Blend(g_push, g_seated, r.push, hold);
    GloveOn(g_onLine, hold, g_gloveInGun);
    g_gloveBlend = std::min(g_gloveBlend + 0.35f, 1.0f);
    Compose(f.pistolPose, g_gloveInGun, r.glove);
    r.gloveBlend = g_gloveBlend;
    if (g_homeFor >= kHomeFrames) {
        g_state = State::kNone;
        r.seated = true;
        EndTry(r);
        r.mag = false;  // the pistol's own is drawn from now on
        r.holding = false;
    }
    return r;
}

bool Holding() {
    return g_state != State::kNone;
}

bool Parked() {
    return g_state == State::kParked;
}

void Clear() {
    g_state = State::kNone;
    g_gloveBlend = 0.0f;  // gone: the glove doesn't ease back towards where it was
    g_homeFor = g_offFor = 0;
    g_lostFor = g_gunLostFor = 0.0f;
    g_flat = 0.0f;
    g_flatWanted = false;
    g_trying = g_snapping = false;
}

void SetCarry(const float* hold) {
    Prepare();
    g_carryOwn = hold != nullptr;
    std::memcpy(g_carry, hold ? hold : g_push, sizeof(g_carry));
    Tidy(g_carry);
}

void SetSnap(float metres) {
    g_snap = metres > 0.0f ? std::clamp(metres, kHome, kSnapMost) : kHome;  // (not a number: the least)
}

void GloveFor(const float* magPose, bool flat, float* glove) {
    Prepare();
    float hold[12], inverse[12];
    Hold(flat ? 1.0f : 0.0f, hold);
    InvertRigid(hold, inverse);
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
}

}
