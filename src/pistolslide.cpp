#include "pistolslide.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>

#include "pistolposes.h"

namespace pistolslide {
namespace {

constexpr float kPi = 3.14159265f;

// The hand in the arms' bind space: out of the palm and along the fingers (handpose.cpp kPalm and kAlong), and the bare
// left palm bone's vertex middle (hands.cpp kLeftPalmCentre). The grab goes by where the palm centre is, the grip by
// how the hand is turned; the hand drawn gliding from where it was drawn into its grip turns about it, so it goes
// straight.
constexpr float kPalmOut[3] = {-0.639f, -0.748f, -0.179f};
constexpr float kAlong[3] = {0.437f, -0.544f, 0.716f};
constexpr float kPalmCentre[3] = {0.5182f, 1.1075f, 0.1756f};

// The grips' palm placements (pistolposes.h kSlidePalms: the arms' bind space -> gun space with the slide closed),
// written to 5 decimals, made exactly rigid.
float g_palms[kGrips][12] = {};

void Compose(const float* a, const float* b, float* out) {
    float r[12];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j];
        r[i * 4 + 3] += a[i * 4 + 3];
    }
    std::memcpy(out, r, sizeof(r));
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

void Turn(const float* m, const float* v, float* out) {  // a direction (the rotation only)
    for (int i = 0; i < 3; ++i)
        out[i] = m[i * 4] * v[0] + m[i * 4 + 1] * v[1] + m[i * 4 + 2] * v[2];
}

float Dot(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float Distance(const float* a, const float* b) {
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return std::sqrt(Dot(d, d));
}

bool Finite(const float* m, int n) {
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(m[i]))
            return false;
    return true;
}

// How far b's turn is from a's (rotation | translation rows, the rotations compared: trace(A^T B) = 1 + 2 cos), in
// degrees.
float Turned(const float* a, const float* b) {
    float trace = 0.0f;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            trace += a[i * 4 + j] * b[i * 4 + j];
    return std::acos(std::clamp((trace - 1.0f) * 0.5f, -1.0f, 1.0f)) * 180.0f / kPi;
}

// A rotation | translation's rotation made exactly orthonormal.
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

// a and b (rotation | translation rows) blended about p, a point of their source space: p goes straight from where a
// puts it to where b does, the turn between them blends about it (s 0 is a, 1 is b).
void BlendAt(const float* a, const float* b, float s, const float* p, float* out) {
    float qa[4], qb[4];
    ToQuat(a, qa), ToQuat(b, qb);
    if (qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3] < 0.0f)
        for (float& v : qb)
            v = -v;
    float q[4];
    for (int i = 0; i < 4; ++i)
        q[i] = qa[i] + (qb[i] - qa[i]) * s;
    float r[12], pa[3], pb[3], turned[3];
    FromQuat(q, r);
    Transform(a, p, pa);
    Transform(b, p, pb);
    Turn(r, p, turned);
    for (int i = 0; i < 3; ++i)
        r[i * 4 + 3] = pa[i] + (pb[i] - pa[i]) * s - turned[i];
    std::memcpy(out, r, sizeof(r));
}

// Eased: 0 to 1 with no jump in speed at either end.
float Smooth(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

void Prepare() {
    static bool ready = false;
    if (ready)
        return;
    ready = true;
    for (int k = 0; k < kGrips; ++k) {
        std::memcpy(g_palms[k], pistolposes::kSlidePalms[k], sizeof(g_palms[k]));
        Tidy(g_palms[k]);
    }
}

}

Slide::Slide(const Profile& p) : m_p(p) {
    Prepare();
}

void Slide::Tune(float reach, float wait, float glide) {
    if (std::isfinite(reach))
        m_p.reach = std::clamp(reach, 0.03f, 0.25f);
    if (std::isfinite(wait))
        m_p.wait = std::clamp(wait, 0.0f, 1.0f);
    if (std::isfinite(glide))
        m_p.glide = std::clamp(glide, 0.05f, 0.5f);
}

// The hand in gun space: where its palm centre is, which way out of the palm and along the fingers. A pose with a
// number that isn't one (a broken pose) is no hand.
Slide::Hand Slide::Read(const Input& in) const {
    Hand h;
    if (!in.hand || !in.pistolPose)
        return h;
    float inverse[12];
    InvertRigid(in.pistolPose, inverse);
    Compose(inverse, in.hand, h.turn);
    Transform(h.turn, kPalmCentre, h.palm);
    Turn(h.turn, kPalmOut, h.out);
    Turn(h.turn, kAlong, h.along);
    h.have = Finite(h.turn, 12) && Finite(h.palm, 3);
    for (float* v : {h.out, h.along}) {  // (handpose.cpp's are 0.9998 long; kSlideGrips' are unit)
        const float n = std::sqrt(Dot(v, v));
        h.have = h.have && n > 0.5f;
        for (int i = 0; i < 3 && n > 0.0f; ++i)
            v[i] /= n;
    }
    return h;
}

// The grip the hand is turned for: out of the palm and along the fingers, the grip it had last a little ahead
// (Profile::stick); and of the grips whose hold (moved back with the slide) the palm is within reach of, the one it's
// turned for most. While the hand is still drawn on the slide, easing back off, only the grip it's in: its fingers are
// still on it (another's hold in reach then is refused, and isn't the nearest: the review found a miss told "too far"
// with the palm on another grip's hold).
Slide::Picked Slide::Pick(const Hand& h, float back) const {
    Picked p;
    float bestFit = 0.0f, turnedFit = 0.0f;
    for (int k = 0; k < kGrips; ++k) {
        const pistolposes::SlideGrip& g = pistolposes::kSlideGrips[k];
        const float at[3] = {g.hold[0], g.hold[1], g.hold[2] - back};
        const float d = Distance(h.palm, at);
        if (m_share > 0.0f && k != m_grip) {
            p.refused = p.refused || d < m_p.reach;
            continue;
        }
        p.nearest = std::min(p.nearest, d);
        const float fit = Dot(h.out, g.palmOut) + Dot(h.along, g.fingers) + (k == m_lastGrip ? m_p.stick : 0.0f);
        if (p.turnedFor < 0 || fit > turnedFit)
            p.turnedFor = k, turnedFit = fit, p.turnedFrom = d;
        if (d < m_p.reach && (p.grip < 0 || fit > bestFit))
            p.grip = k, bestFit = fit, p.from = d;
    }
    return p;
}

void Slide::Grab(const Hand& h, int grip, float now, bool locked, Output& o) {
    m_held = true;
    m_grabZ = h.palm[2];
    m_start = m_pull = m_maxPull = now;
    m_startLocked = locked;
    m_springing = m_cycleBack = false;
    m_waited = -1.0f;
    // The hand coming on in from where it was at the press isn't a pull (Update): from how far off the hold.
    const float* hold = pistolposes::kSlideGrips[grip].hold;
    const float at[3] = {hold[0], hold[1], hold[2] - now};
    m_coming = true;
    m_comingFrom = Distance(h.palm, at);
    m_comingStill = 0.0f;
    // The hand drawn glides on into the grip from where it's drawn: from the controller's hand (none of it on the slide
    // yet), or from where it is as it eases back off (a grab again before it's done), over the glide's time, longer
    // for a hand turned far from the grip.
    float drawn[12];
    if (m_share > 0.0f)
        BlendAt(h.turn, m_inGun, m_share, kPalmCentre, drawn);
    else
        std::memcpy(drawn, h.turn, sizeof(drawn));
    m_grip = m_lastGrip = grip;
    m_handOn = true;
    m_handFor = 0.0f;
    m_glideFor = GlideTime(drawn, g_palms[grip]);
    m_shareFrom = m_share;
    std::memcpy(m_from, m_inGun, sizeof(m_from));
    o.grabbed = true;
    o.turnedOff = Turned(h.turn, g_palms[grip]);  // the controller's hand from the grip's
    o.glideFor = m_glideFor;
}

// The glide's time, and for a turn further than turnAt that much longer in proportion: the hand turns no faster than
// through turnAt in the glide's time.
float Slide::GlideTime(const float* from, const float* to) const {
    return m_p.glide * std::max(1.0f, Turned(from, to) / m_p.turnAt);
}

// The hand drawn on the slide, once a frame: gliding on into its grip (eased over the glide's time from where it was
// drawn as it began), then on it, back with the slide (handBack: as the slide's drawn while held); let go, easing back
// off over the glide's time from where it was let go of, less and less of it, while the slide springs home out of it.
// (The glide's time: GlideTime's, as the grab or the let go found it.)
void Slide::DrawHand(const float* pistolPose, float handBack, float dt, Output& o) {
    m_handFor += dt;
    const float k = m_glideFor > 0.0f ? Smooth(m_handFor / m_glideFor) : 1.0f;
    if (m_handOn) {
        const float back[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -handBack};
        float on[12];
        Compose(back, g_palms[m_grip], on);
        if (m_shareFrom > 0.0f && k < 1.0f)
            BlendAt(m_from, on, k, kPalmCentre, m_inGun);
        else
            std::memcpy(m_inGun, on, sizeof(on));
        m_share = m_shareFrom + (1.0f - m_shareFrom) * k;
    } else {
        m_share = m_shareOff * (1.0f - k);
    }
    o.grip = m_grip;
    if (m_share > 0.0f && Finite(pistolPose, 12)) {
        Compose(pistolPose, m_inGun, o.hand);
        o.share = m_share;
    }
}

Output Slide::Update(const Input& in) {
    Output o;
    const float dt = in.dt > 0.0f ? std::min(in.dt, 0.1f) : 0.0f;  // (not a number: none)
    // A fresh press, followed with the pistol in hand or not: a trigger already held as it comes into hand isn't one.
    bool press = false;
    if (in.trigger < 0.4f)
        m_armed = true;
    else if (m_armed && in.trigger > 0.6f)
        press = true, m_armed = false;
    if (!in.pistolPose) {
        // No pistol: let go of, nothing drawn (the hand on it neither: nothing to draw it on), a press waiting gone.
        // The hand's glide on or ease back off ends here too: back in hand, the hand is where the controller has it,
        // not back on the slide easing off again from where it was (the review: from 2 frames into an ease off, after
        // 10 s out of hand it came back at 0.72 of the way on).
        if (m_held) {
            o.letGo = true;
            o.pulled = m_maxPull;
        }
        if (m_waited >= 0.0f) {
            o.missed = true;
            o.missWhy = "no pistol to work (out of hand, or the slide no longer worked)";
            o.pressedFrom = m_pressFrom, o.nearest = m_nearest, o.unseen = m_unseen;
        }
        m_held = m_springing = m_cycleBack = m_coming = false;
        m_drawn = -1.0f;
        m_waited = -1.0f;
        m_share = m_shareOff = 0.0f;
        m_handOn = false;
        o.grip = m_grip;
        return o;
    }
    const Hand h = Read(in);
    // Where it is now: held or springing as the mod draws it, on the stop, or home (the game's own then).
    const float now = m_drawn >= 0.0f ? m_drawn : in.locked ? m_p.lockAt : 0.0f;

    // The press within reach of a grip's hold on its rear, where the slide is now; out of reach, it waits for the hand
    // (the trigger held) until the wait's time has gone by. A press with no palm is told so at once, and doesn't wait.
    // In reach only of grips the hand isn't turned for, it waits for the one it is turned for too, while the hand comes
    // on towards that one's hold (settleStep nearer within settle; settle at first, to see), at most a second: the
    // review's hand turned for S2 or S3, coming in from in front, had S1 grabbed 14-19 cm out of its own grip's hold,
    // and its own coming on in pulled the slide back. Stopped short, or the wait over, the grip it's turned for most of
    // those in reach. A miss says why: the hold in reach another grip's while the hand still eased back off its last
    // one, the palm not known for some of the wait (busy or not tracked: then it wasn't seen within reach, it may have
    // been), or not.
    constexpr const char* kRefused =
        "the hold in reach was another grip's while the hand still eased back off its last one (only that one could be "
        "taken then)";
    constexpr float kLongest = 1.0f;  // s: the most a press waits for the hand coming on to the grip it's turned for
    if (!m_held && press) {
        o.pressed = true;
        o.palmFrom = -1.0f;
        if (h.have) {
            const Picked p = Pick(h, now);
            const bool forTheTurn = p.grip >= 0 && p.grip != p.turnedFor && m_p.wait > 0.0f;
            if (p.grip >= 0 && !forTheTurn) {
                o.palmFrom = p.from;
                o.turnedFor = p.turnedFor;
                Grab(h, p.grip, now, in.locked, o);
            } else {
                o.palmFrom = p.nearest;
                m_waited = m_unseen = 0.0f;
                m_refused = p.refused;
                m_passing = forTheTurn;
                m_waitFor = p.turnedFor, m_waitNear = p.turnedFrom, m_waitStill = 0.0f;
                m_pressFrom = m_nearest = p.nearest;
                if (m_p.wait <= 0.0f) {
                    o.missed = true;
                    o.missWhy = p.refused ? kRefused : "a press doesn't wait for the hand (grab's wait 0)";
                    o.pressedFrom = o.nearest = p.nearest;
                    m_waited = -1.0f;
                }
            }
        }
    } else if (!m_held && m_waited >= 0.0f) {
        m_waited += dt;
        const Picked p = h.have ? Pick(h, now) : Picked();
        if (h.have) {
            m_nearest = std::min(m_nearest, p.nearest);
            // Coming on towards the hold of the grip it's turned for (turned for another now: from there).
            if (p.turnedFor != m_waitFor || p.turnedFrom < m_waitNear - m_p.settleStep) {
                m_waitFor = p.turnedFor, m_waitNear = p.turnedFrom, m_waitStill = 0.0f;
            } else {
                if (p.turnedFrom > m_waitNear + m_p.settleStep)
                    m_waitNear = p.turnedFrom;
                m_waitStill += dt;
            }
        } else {
            m_unseen += dt;
        }
        m_refused = m_refused || p.refused;
        const bool onItsWay =
            p.grip >= 0 && p.grip != p.turnedFor && m_waitStill < m_p.settle && m_waited <= kLongest + 1e-4f;
        m_passing = m_passing || onItsWay;
        const bool inTime = m_waited <= m_p.wait + 1e-4f || onItsWay;
        if (in.trigger >= 0.4f && p.grip >= 0 && !onItsWay) {
            o.palmFrom = p.from;
            o.pressedFrom = m_pressFrom;
            o.waited = m_waited;
            o.turnedFor = p.turnedFor;
            Grab(h, p.grip, now, in.locked, o);
        } else if (in.trigger < 0.4f || !inTime) {
            o.missed = true;
            o.missWhy = in.trigger < 0.4f
                            ? (m_passing ? "the trigger was let go while it waited for the hand to come on to the hold "
                                           "of the grip it's turned for"
                                         : "the trigger was let go first")
                        : m_refused ? kRefused
                        : m_passing ? "the palm went out of reach again while it waited for the hand to come on to "
                                      "the hold of the grip it's turned for"
                        : m_unseen > 0.0f ? "the palm wasn't seen within reach while the press waited"
                                          : "the palm didn't come within reach while the press waited";
            o.pressedFrom = m_pressFrom, o.nearest = m_nearest, o.unseen = m_unseen;
            m_waited = -1.0f;
        }
    }
    if (m_held) {
        m_lost = h.have ? 0.0f : m_lost + dt;
        if (h.have && in.trigger >= 0.4f) {
            // As far back as the hand has come since the grab (the frame's -z), on the stop's side of it while locked.
            const float low = m_startLocked ? m_p.lockAt : 0.0f;
            const float* hold = pistolposes::kSlideGrips[m_grip].hold;
            if (m_coming) {
                // Still coming to the grip's hold (grabbed on its way in): its move along the slide towards the hold's
                // place, where the slide is drawn, is taken up, not pulled, from in front or from behind; on past it,
                // the rest pulls. kept: how far in front of the hold the pull keeps the palm (behind: below 0), ahead:
                // how far in front it is now.
                const float kept = m_grabZ - (hold[2] - m_start);
                const float ahead = h.palm[2] - (hold[2] - m_pull);
                const float keep = kept > 0.0f ? std::clamp(ahead, 0.0f, kept) : std::clamp(ahead, kept, 0.0f);
                m_grabZ += keep - kept;
            }
            // The hand going on forward of home (or the stop) slides forward on it: the pull is read from where it is
            // then, but at most slip further forward a frame (a tracking jump forward and straight back isn't a pull).
            float pull = m_start + (m_grabZ - h.palm[2]);
            if (pull < low) {
                m_grabZ = std::min(h.palm[2] + low - m_start, m_grabZ + m_p.slip);
                pull = low;
            }
            m_pull = std::min(pull, m_p.travel);
            m_maxPull = std::max(m_maxPull, m_pull);
            m_drawn = m_pull;
            if (m_coming) {
                // Still coming while it comes settleStep nearer the hold (where the slide is drawn now) within settle
                // of its last step nearer; gone further off first, nearer from there.
                const float at[3] = {hold[0], hold[1], hold[2] - m_pull};
                const float d = Distance(h.palm, at);
                if (d < m_comingFrom - m_p.settleStep) {
                    m_comingFrom = d;
                    m_comingStill = 0.0f;
                } else {
                    if (d > m_comingFrom + m_p.settleStep)
                        m_comingFrom = d;
                    m_comingStill += dt;
                    m_coming = m_comingStill < m_p.settle;
                }
            }
        }
        // The palm lost a moment (the left controller's tracking, a broken pose) with the trigger still pressed: held
        // where it was for up to coast, not let go (a lost frame far back would rack it).
        if (in.trigger >= 0.4f && (h.have || m_lost < m_p.coast)) {
            o.held = true;
            o.back = m_drawn;
            DrawHand(in.pistolPose, m_drawn, dt, o);
            return o;
        }
        // Let go (or the hand lost for longer): far enough back, it feeds; from the stop, a tug past it drops the stop
        // (the same as a rack then: the chamber is empty on the stop). Either way it springs forward from where it was
        // let go, and the hand eases back off from where it's drawn to the controller's, over the glide's time (longer
        // for a controller turned far from the grip; the hand not known, the glide's own).
        m_held = m_coming = false;
        o.letGo = true;
        o.pulled = m_maxPull;
        o.racked = m_startLocked ? m_maxPull >= m_p.lockAt + m_p.tug : m_maxPull >= m_p.rackAt;
        m_springing = true;
        m_springFrom = m_pull;
        m_springT = 0.0f;
        m_handOn = false;
        m_handFor = 0.0f;
        m_shareOff = m_share;
        m_glideFor = m_p.glide;
        if (h.have && m_share > 0.0f) {
            float drawn[12];
            BlendAt(h.turn, m_inGun, m_share, kPalmCentre, drawn);
            m_glideFor = GlideTime(drawn, h.turn);
        }
    } else if (!m_springing && m_drawn >= 0.0f && !in.locked) {
        // On the stop, and the stop dropped: home from there, not in one jump. (A tug off the stop springs it back onto
        // the stop as it's let go, before the caller has told the mechanism, which drops the stop for the next frame.)
        m_springing = true;
        m_springFrom = m_drawn;
        m_springT = 0.0f;
    }
    if (m_springing && m_cycleBack) {
        // The trigger's rack from home: back first (30 ms), as a shot's cycle.
        constexpr float kBack = 0.03f;
        m_springT += dt;
        m_drawn = m_p.travel * std::min(m_springT / kBack, 1.0f);
        if (m_springT >= kBack)
            m_cycleBack = false, m_springT = 0.0f;
    } else if (m_springing) {
        // Home, or onto the stop while the mechanism holds it (the caller has told it about a rack by the next frame).
        const float target = in.locked ? m_p.lockAt : 0.0f;
        const float span = std::max(m_springFrom - target, 0.0f);
        const float duration = m_p.springHome * span / m_p.travel;
        m_springT += dt;
        if (span <= 0.0f || m_springT >= duration) {
            m_springing = false;
            m_drawn = in.locked ? m_p.lockAt : -1.0f;
        } else {
            m_drawn = m_springFrom - span * (m_springT / duration);
        }
    } else {
        m_drawn = in.locked ? m_p.lockAt : -1.0f;
    }
    o.back = m_drawn;
    DrawHand(in.pistolPose, m_drawn, dt, o);
    return o;
}

}
