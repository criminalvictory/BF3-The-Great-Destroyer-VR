#pragma once
// The left hand steering the gun while it holds the handguard (hands.cpp Support), worked out on its
// own so tools/steertest.cpp can run it without the game.
//
// Holding, the gun points from the right hand towards the left, by the smallest turn from where the
// right hand alone points it. It did that whatever the hands did, and in one jump: the user's video
// (2026-10-31 08:21) has the rifle lying across the body, swinging round at the face and dropping out of
// view for the 38 s the left hand held on (the grip squeezed throughout), and steady in the right
// hand while it didn't. The hold itself still lasts until the grip is let go (the user, 2026-10-30:
// "locked no matter what until I let go"); what changes is how much the left hand points the gun:
//   - fully while the line between the hands is within the full angle (35 degrees) of where the right
//     hand alone points the gun's line to the hold, not at all past the none angle (65: a hand beside,
//     below or behind the right isn't on a rifle's handguard), and the hands far enough apart for that
//     line to be steady (from together to together + kTogetherBlend: it had switched at 10 cm in one
//     frame); weapon.txt "steer 35 65" tries others;
//   - eased in and out (kEase), letting go too: the gun goes back to the right hand's aim over a
//     tenth of a second, turned by the last steer the hands gave it, not in one jump;
//   - the steer itself changes at most kMostTurn a second: a tracking glitch of the left controller
//     (inside-out tracking, the hands close together) can't throw the gun across the view in a frame.
//     The same limit delays cancelling a turn of the right controller faster than that (a 12-degree
//     wrist twitch in one frame shows 5 degrees for that frame: the review, 2026-10-31);
//   - with the left controller lost for a moment (coast), everything stays as it was.
// hands.cpp gives both lines in the gun's own model space as the right hand alone holds it, so a
// swing of both hands, a recentre or the head turning leave the steer as it is (kept in the room, a
// recentre swung the held gun off the hands' line until the limit caught up: the review, 2026-10-31).

#include <cmath>

namespace steer {

constexpr float kDegree = 3.14159265f / 180.0f;
constexpr float kFullAngle = 35.0f * kDegree;
constexpr float kNoneAngle = 65.0f * kDegree;
constexpr float kTogetherBlend = 0.05f;  // metres beyond the hands-together distance to steer fully
constexpr float kEase = 0.04f;           // seconds: the weight's time constant, in and out
constexpr float kMostTurn = 500.0f * kDegree;  // radians a second the steer may change by

struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

struct Input {
    bool holding = false;     // the left hand holds the handguard and is tracked this frame
    bool lineOk = false;      // the hold point far enough from the grip to make a line
    bool coast = false;       // holding, the left controller lost for a moment: all kept as it was
    float from[3] = {};       // unit: the gun's line to the hold point as the right hand alone points it
    float to[3] = {};         // unit: the right hand's grip to the left hand
    float apart = 0.0f;       // metres between the hands
    float together = 0.10f;   // closer than this, the right hand alone points the gun
    float fullAngle = kFullAngle, noneAngle = kNoneAngle;  // radians: steered fully within, not at all past
    float dt = 0.0f;          // seconds since the last frame
};

struct State {
    float weight = 0.0f;  // how much of the steer is applied, eased
    Quat steer;           // the steer at full weight: towards the hands' line, rate-limited
};

struct Output {
    Quat turn;             // onto the right hand's own aim of the gun
    float weight = 0.0f;   // as applied
    float wanted = 0.0f;   // this frame's, before easing
    float angle = -1.0f;   // radians between from and to (-1: not holding with a line)
};

inline float Smooth(float t) {
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

inline Quat Normalized(Quat q) {
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(n > 0.0f))
        return {};
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

inline Quat Mul(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

inline Quat Conjugate(const Quat& q) {
    return {-q.x, -q.y, -q.z, q.w};
}

// The angle a turn turns by (radians, 0 to pi).
inline float AngleOf(const Quat& q) {
    const float w = std::fabs(q.w) > 1.0f ? 1.0f : std::fabs(q.w);
    return 2.0f * std::acos(w);
}

// The smallest turn taking unit a onto unit b; none when they're (nearly) opposite, which never gets
// steered by (past the none angle).
inline Quat Arc(const float* a, const float* b) {
    const float c = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    if (c < -0.999f)
        return {};
    return Normalized({a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0], 1.0f + c});
}

// From a towards b, s of the way (0 a, 1 b), the short way round.
inline Quat Slerp(const Quat& a, Quat b, float s) {
    float c = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (c < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
        c = -c;
    }
    if (c > 0.9995f)
        return Normalized({a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s, a.z + (b.z - a.z) * s, a.w + (b.w - a.w) * s});
    const float half = std::acos(c), sine = std::sin(half);
    const float ka = std::sin((1.0f - s) * half) / sine, kb = std::sin(s * half) / sine;
    return {a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb, a.w * ka + b.w * kb};
}

// This frame's turn of the gun from the right hand's own aim of it.
inline Output Update(const Input& in, State& state) {
    Output out;
    if (in.coast && in.holding) {  // the left controller lost for a moment: the gun stays as it was
        out.wanted = out.weight = state.weight;
        out.turn = out.weight > 0.0f ? Slerp(Quat{}, state.steer, out.weight) : Quat{};
        return out;
    }
    const bool line = in.holding && in.lineOk;
    const float dot = in.from[0] * in.to[0] + in.from[1] * in.to[1] + in.from[2] * in.to[2];
    if (line && std::isfinite(dot)) {  // a pose lost to NaN gives no line
        const float c = dot > 1.0f ? 1.0f : dot < -1.0f ? -1.0f : dot;
        const float full = in.fullAngle, none = in.noneAngle > in.fullAngle ? in.noneAngle : in.fullAngle + kDegree;
        out.angle = std::acos(c);
        out.wanted = Smooth((in.apart - in.together) / kTogetherBlend) * (1.0f - Smooth((out.angle - full) / (none - full)));
    }
    if (!std::isfinite(out.wanted))
        out.wanted = 0.0f;
    const float dt = in.dt > 0.0f && in.dt < 0.25f ? in.dt : 0.0f;
    if (out.wanted > 0.0f) {
        const Quat target = Arc(in.from, in.to);
        if (state.weight < 0.01f) {
            state.steer = target;  // a fresh start: nothing of an old steer shows
        } else {
            // Towards the hands' line, at most kMostTurn a second.
            float apart = AngleOf(Mul(target, Conjugate(state.steer)));
            const float most = kMostTurn * dt;
            if (apart > most && out.wanted > state.weight) {
                // The steer still has to turn onto the hands' line while its weight would grow (the line
                // back after a let-go or after the hands came together, the old steer still easing out):
                // growing the weight first swung the gun towards the old steer (13 degrees and back in
                // 80 ms, steertest 7a). The gun held where it is instead, its weight as wanted (the steer
                // shortened to match), then turned on at the rate limit.
                state.steer = Slerp(Quat{}, state.steer, state.weight / out.wanted);
                state.weight = out.wanted;
                apart = AngleOf(Mul(target, Conjugate(state.steer)));
            }
            state.steer = apart <= most || !(apart > 0.0f) ? target : Slerp(state.steer, target, most / apart);
        }
    }
    // Without a line to steer by (let go, out of the cone, the left hand lost) the last steer stays and
    // only its weight eases out: the gun turns back to the right hand's aim from where it was.
    const float ease = dt > 0.0f ? 1.0f - std::exp(-dt / kEase) : 1.0f;
    state.weight += (out.wanted - state.weight) * ease;
    if (std::fabs(out.wanted - state.weight) < 0.002f)
        state.weight = out.wanted;
    if (!std::isfinite(state.weight))
        state.weight = 0.0f;
    out.weight = state.weight;
    out.turn = out.weight > 0.0f ? Slerp(Quat{}, state.steer, out.weight) : Quat{};
    return out;
}

}  // namespace steer
