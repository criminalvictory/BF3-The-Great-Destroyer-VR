#pragma once
// Wrist movement (the user: the forearm swung rigidly with the hand; the design: three designs and a
// judge, 2026-10-31): one arm's forearm placed from its hand, a shoulder estimated from the headset
// and an elbow pole in the body's axes, so the glove bends at the wrist while the forearm keeps
// pointing at the elbow. The forearm is three bones of the game's rig: E, its elbow end (no twist),
// and the twist bones A and B, which take a third and two thirds of the hand's twist about it (the
// right arm's own split in the dumps, 0.319/0.639; the mesh's weights, 0.32/0.68). The hand itself
// stays where it's drawn. Pure: no game calls and no logs, so armtest.exe runs it without the game.
// Every matrix is three rows, rotation | translation, from the gloves' bind space into one space
// (hands.cpp: the view's); points and directions are in that space.
// Grafted onto the first design's solver: the elbow poles fitted per side to the game's own rifle
// hold (the left's within 0.7 degrees, the right's 0.1), the second design's rising hand, a turn of
// the elbow's way limited to 12 degrees a frame (FrostbiteVR, the third design), the third design's
// anatomical wrist range (an ellipse, not a 75 degree cone: a wrong elbow guess can't show a 75
// degree radial bend), the hand's turn without its widening.

#include <algorithm>
#include <cmath>
#include <cstring>

namespace armik {

// The left arm's joints in the gloves' bind space: where the dumps' fixed rotation lines cross
// (within 0.1 mm; the wrist from the right arm's lines, 73.7 degrees apart, mirrored: the left's are
// only 6.2 apart). The right arm's: x negated. The elbow is bent 36.4 degrees in the bind pose.
constexpr float kShoulder[3] = {0.1641f, 1.4745f, -0.0396f};
constexpr float kElbow[3] = {0.3728f, 1.2601f, -0.0125f};
constexpr float kWrist[3] = {0.5089f, 1.1207f, 0.1609f};
constexpr float kUpper = 0.3004f, kFore = 0.2608f;  // |E-S| 0.30042, |W-E| 0.26081
constexpr float kFoldAt[3] = {0.440f, 1.215f, 0.061f};  // skin.cpp's kForearmMiddle: inside the forearm
// The hand's axes in the bind space (handpose.cpp): across (thumb -> little finger), out of the palm.
constexpr float kAcross[3] = {0.633f, -0.380f, -0.675f};
constexpr float kPalm[3] = {-0.639f, -0.748f, -0.179f};
constexpr float kPi = 3.14159265f, kDeg = kPi / 180.0f;

struct Params {
    float share[3] = {0.0f, 1.0f / 3.0f, 2.0f / 3.0f};  // of the hand's twist: the elbow end, twist bones A, B
    // The pole in the body's axes (out to this arm's side, down, back; the left's, fitted to the game's
    // hold: the right's is 0.9 1 0.3), and how far a rising hand takes the elbow out and up:
    // u = clamp((wrist above shoulder + 0.10) / 0.40, 0, 1), out + riseOut u, down x (1 - riseDown u).
    float poleOut = 0.4f, poleDown = 1.0f, poleBack = 0.3f, riseOut = 0.6f, riseDown = 0.8f;
    // The wrist's range (degrees): flexion, extension, radial, ulnar deviation (the right's ulnar 55:
    // the game's own grip bends it 50.7 degrees).
    float flex = 70.0f, ext = 65.0f, radial = 25.0f, ulnar = 40.0f;
    float maxTwist = 150.0f * kDeg;   // beyond it the shares hold; back only after a whole turn
    float swivelStep = 12.0f * kDeg;  // the elbow's way turns at most this a frame
};

// Kept from frame to frame, one per arm (reset: a fresh State).
struct State {
    bool have = false;
    float pole[3] = {};
    float twist = 0.0f;
    float azimuth = 0.0f;
};

struct Result {
    float frame[3][12];  // the elbow end (no twist), twist bone A, B
    float fold[3];       // inside the forearm: where its folded arm (sleeve, upper arm) goes
    float elbow[3];      // the forearm's elbow end as drawn
    float wrist[3];
    float bend = 0, flexion = 0, deviation = 0, twist = 0;  // radians (flexion +, ulnar +)
    bool reach = true, clamped = false, twistHeld = false;
};

namespace detail {
inline float Dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline void Cross(const float* a, const float* b, float* o) {
    const float r[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    std::memcpy(o, r, sizeof(r));
}
inline float Normalize(float* v) {
    const float n = std::sqrt(Dot(v, v));
    if (n > 1e-9f)
        v[0] /= n, v[1] /= n, v[2] /= n;
    return n;
}
inline void Rotate(const float* m, const float* v, float* o) {
    const float r[3] = {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[4] * v[0] + m[5] * v[1] + m[6] * v[2],
                        m[8] * v[0] + m[9] * v[1] + m[10] * v[2]};
    std::memcpy(o, r, sizeof(r));
}
inline void RotateT(const float* m, const float* v, float* o) {  // the rotation's transpose
    const float r[3] = {m[0] * v[0] + m[4] * v[1] + m[8] * v[2], m[1] * v[0] + m[5] * v[1] + m[9] * v[2],
                        m[2] * v[0] + m[6] * v[1] + m[10] * v[2]};
    std::memcpy(o, r, sizeof(r));
}
inline void Turn(const float* axis, float angle, const float* v, float* o) {  // v turned about the unit axis
    const float c = std::cos(angle), s = std::sin(angle), k = Dot(axis, v) * (1 - c);
    float x[3];
    Cross(axis, v, x);
    for (int i = 0; i < 3; ++i)
        o[i] = v[i] * c + x[i] * s + axis[i] * k;
}
inline void Across(float* v, const float* u) {  // v without its part along the unit u
    const float d = Dot(v, u);
    for (int i = 0; i < 3; ++i)
        v[i] -= d * u[i];
}
// A rigid placement turned by angle about the unit axis through point (in place).
inline void TurnPlacement(float* m, const float* axis, float angle, const float* point) {
    float column[3], moved[3];
    for (int j = 0; j < 3; ++j) {
        column[0] = m[j], column[1] = m[4 + j], column[2] = m[8 + j];
        Turn(axis, angle, column, moved);
        m[j] = moved[0], m[4 + j] = moved[1], m[8 + j] = moved[2];
    }
    const float from[3] = {m[3] - point[0], m[7] - point[1], m[11] - point[2]};
    Turn(axis, angle, from, moved);
    m[3] = point[0] + moved[0], m[7] = point[1] + moved[1], m[11] = point[2] + moved[2];
}
}  // namespace detail

// hand: the hand's rigid placement (rotation orthonormal) into the target space; shoulder: the
// estimated joint; out/up/back: the body's unit axes there (out to this arm's side).
inline void Solve(const float* hand, const float* shoulder, const float* out, const float* up, const float* back,
                  bool right, const Params& p, State& state, Result& r) {
    using namespace detail;
    const float sx = right ? -1.0f : 1.0f;
    const float S[3] = {kShoulder[0] * sx, kShoulder[1], kShoulder[2]}, E[3] = {kElbow[0] * sx, kElbow[1], kElbow[2]},
                W[3] = {kWrist[0] * sx, kWrist[1], kWrist[2]}, M[3] = {kFoldAt[0] * sx, kFoldAt[1], kFoldAt[2]};
    float f0[3] = {W[0] - E[0], W[1] - E[1], W[2] - E[2]}, u0[3] = {E[0] - S[0], E[1] - S[1], E[2] - S[2]}, n0[3], b0[3];
    Normalize(f0), Normalize(u0);
    Cross(u0, f0, n0), Normalize(n0);  // the elbow's hinge in the bind pose
    Cross(f0, n0, b0);
    // The wrist's axes in the bind space: flexion turns the forearm (in the hand's frame) towards the
    // back of the hand (-palm), ulnar deviation towards the thumb (-across).
    float ax[3] = {kAcross[0] * sx, kAcross[1], kAcross[2]}, pa[3] = {kPalm[0] * sx, kPalm[1], kPalm[2]};
    Across(ax, f0), Normalize(ax);
    Across(pa, f0), Across(pa, ax), Normalize(pa);
    float w[3];
    for (int i = 0; i < 3; ++i)
        w[i] = hand[i * 4] * W[0] + hand[i * 4 + 1] * W[1] + hand[i * 4 + 2] * W[2] + hand[i * 4 + 3];
    std::memcpy(r.wrist, w, sizeof(w));
    float D[3] = {w[0] - shoulder[0], w[1] - shoulder[1], w[2] - shoulder[2]};
    const float d = Normalize(D);
    float hf[3];
    Rotate(hand, f0, hf);
    Normalize(hf);
    if (!(d > 1e-3f))
        std::memcpy(D, hf, sizeof(D));
    // The pole: out, down and back, further out (and less down) as the hand rises above the shoulder.
    const float rise = std::clamp((Dot(D, up) * d + 0.10f) / 0.40f, 0.0f, 1.0f);
    const float o = p.poleOut + p.riseOut * rise, dn = p.poleDown * (1.0f - p.riseDown * rise);
    float P[3];
    for (int i = 0; i < 3; ++i)
        P[i] = out[i] * o - up[i] * dn + back[i] * p.poleBack;
    Across(P, D);
    float last[3] = {state.pole[0], state.pole[1], state.pole[2]};
    Across(last, D);
    const float length = Normalize(P);
    if (state.have && Normalize(last) > 1e-4f) {
        if (length < 0.2f) {  // along the shoulder-wrist line: lean on last frame's way
            const float k = 1.0f - length / 0.2f;
            for (int i = 0; i < 3; ++i)
                P[i] = P[i] * (1 - k) + last[i] * k;
            Normalize(P);
        }
        // At most swivelStep a frame from last frame's way.
        const float c = std::clamp(Dot(P, last), -1.0f, 1.0f), a = std::acos(c);
        if (a > p.swivelStep) {
            float axis[3];
            Cross(last, P, axis);
            if (Normalize(axis) < 1e-6f)
                std::memcpy(axis, D, sizeof(axis));
            Turn(axis, p.swivelStep, last, P);
            Normalize(P);
        }
    }
    if (Normalize(P) < 1e-4f) {  // no way at all: any across the shoulder-wrist line
        const float any[3] = {D[1], -D[0], 0.0f};
        std::memcpy(P, any, sizeof(P));
        if (Normalize(P) < 1e-4f)
            P[0] = 1, P[1] = 0, P[2] = 0;  // that line along z
    }
    std::memcpy(state.pole, P, sizeof(P));
    // The elbow: the two bones' triangle on the shoulder-wrist line, its apex towards the pole.
    // Beyond reach the arm is straight, pointing at the shoulder.
    const float lo = std::fabs(kUpper - kFore) + 1e-3f, hi = kUpper + kFore - 1e-4f;
    r.reach = d <= hi;
    const float dc = std::clamp(d, lo, hi);
    const float x = (kUpper * kUpper - kFore * kFore + dc * dc) / (2 * dc);
    const float y = std::sqrt(std::max(kUpper * kUpper - x * x, 0.0f));
    float e[3], f1[3];
    for (int i = 0; i < 3; ++i)
        e[i] = shoulder[i] + D[i] * x + P[i] * y, f1[i] = w[i] - e[i];
    if (Normalize(f1) < 1e-4f)
        std::memcpy(f1, D, sizeof(f1));
    // The wrist's bend in the hand's frame, held inside the ellipse of its range.
    {
        float u[3];
        RotateT(hand, f1, u);
        Normalize(u);
        const float along = Dot(u, f0), fl = -Dot(u, pa), ul = -Dot(u, ax);
        const float side = std::sqrt(fl * fl + ul * ul);
        const float bend = std::atan2(side, along);
        const float az = side > 1e-6f ? std::atan2(ul, fl) : state.azimuth;
        const float lx = (std::cos(az) >= 0 ? p.flex : p.ext) * kDeg, ly = (std::sin(az) >= 0 ? p.ulnar : p.radial) * kDeg;
        const float cx = std::cos(az) / lx, cy = std::sin(az) / ly;
        const float most = 1.0f / std::sqrt(cx * cx + cy * cy);
        r.clamped = bend > most;
        const float b = std::min(bend, most);
        r.bend = b, r.flexion = b * std::cos(az), r.deviation = b * std::sin(az);
        state.azimuth = az;
        if (r.clamped) {
            float v[3];
            for (int i = 0; i < 3; ++i)
                v[i] = f0[i] * std::cos(b) + std::sin(b) * (-pa[i] * std::cos(az) - ax[i] * std::sin(az));
            Rotate(hand, v, f1);
            Normalize(f1);
        }
    }
    float n1[3];
    Cross(P, D, n1);
    Across(n1, f1);
    if (Normalize(n1) < 1e-4f) {
        Rotate(hand, n0, n1);
        Across(n1, f1);
        Normalize(n1);
    }
    float b1[3];
    Cross(f1, n1, b1);
    // The hand's twist about the forearm: its hinge swung onto f1, measured against n1.
    float hn[3];
    Rotate(hand, n0, hn);
    {
        float axis[3];
        Cross(hf, f1, axis);
        const float s = Normalize(axis), cc = Dot(hf, f1);
        if (s > 1e-6f)
            Turn(axis, std::atan2(s, cc), hn, hn);
    }
    float x1[3];
    Cross(n1, hn, x1);
    float phi = std::atan2(Dot(x1, f1), Dot(n1, hn));
    if (state.have) {  // unwound against last frame's
        while (phi - state.twist > kPi)
            phi -= 2 * kPi;
        while (phi - state.twist < -kPi)
            phi += 2 * kPi;
    }
    r.twistHeld = std::fabs(phi) > p.maxTwist;
    phi = std::clamp(phi, -p.maxTwist, p.maxTwist);
    state.twist = phi;
    state.have = true;
    r.twist = phi;
    // Each frame takes {f0, n0, b0} onto {f1, n1, b1} turned about f1 by its share of the twist, the
    // wrist joint landing on the hand's.
    for (int s = 0; s < 3; ++s) {
        float n[3], b[3];
        Turn(f1, phi * p.share[s], n1, n), Turn(f1, phi * p.share[s], b1, b);
        float* m = r.frame[s];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                m[i * 4 + j] = f1[i] * f0[j] + n[i] * n0[j] + b[i] * b0[j];
        for (int i = 0; i < 3; ++i)
            m[i * 4 + 3] = w[i] - (m[i * 4] * W[0] + m[i * 4 + 1] * W[1] + m[i * 4 + 2] * W[2]);
    }
    for (int i = 0; i < 3; ++i) {
        const float* m = r.frame[0];
        r.fold[i] = m[i * 4] * M[0] + m[i * 4 + 1] * M[1] + m[i * 4 + 2] * M[2] + m[i * 4 + 3];
        r.elbow[i] = w[i] - f1[i] * kFore;
    }
}

// For tests (hands.cpp's stand-in "test bend"): in the bind space, the forearm's direction (elbow ->
// wrist) and the axes a hand turns about to flex (fingers towards the palm) and to deviate ulnarly
// (towards the little finger), each through the wrist joint (kWrist, mirrored for the right).
inline void WristAxes(bool right, float f0[3], float flexAxis[3], float ulnarAxis[3]) {
    using namespace detail;
    const float sx = right ? -1.0f : 1.0f;
    f0[0] = (kWrist[0] - kElbow[0]) * sx, f0[1] = kWrist[1] - kElbow[1], f0[2] = kWrist[2] - kElbow[2];
    Normalize(f0);
    float ax[3] = {kAcross[0] * sx, kAcross[1], kAcross[2]}, pa[3] = {kPalm[0] * sx, kPalm[1], kPalm[2]};
    Across(ax, f0), Normalize(ax);
    Across(pa, f0), Across(pa, ax), Normalize(pa);
    Cross(f0, pa, flexAxis), Normalize(flexAxis);
    Cross(f0, ax, ulnarAxis), Normalize(ulnarAxis);
}

// For tests (hands.cpp's stand-in "test neutral"): the hand (as Solve's, changed in place) turned
// about its wrist joint until its straight wrist lies along the forearm Solve gives it (a few rounds,
// each solved afresh: a clamped wrist's forearm moves as the hand turns).
inline void Neutral(float* hand, const float* shoulder, const float* out, const float* up, const float* back,
                    bool right, const Params& p) {
    using namespace detail;
    float f0[3], flexAxis[3], ulnarAxis[3];
    WristAxes(right, f0, flexAxis, ulnarAxis);
    for (int round = 0; round < 8; ++round) {
        State fresh;
        Result r;
        Solve(hand, shoulder, out, up, back, right, p, fresh, r);
        float f1[3] = {r.wrist[0] - r.elbow[0], r.wrist[1] - r.elbow[1], r.wrist[2] - r.elbow[2]}, hf[3], axis[3];
        Normalize(f1);
        Rotate(hand, f0, hf);
        Normalize(hf);
        Cross(hf, f1, axis);
        const float s = Normalize(axis), angle = std::atan2(s, Dot(hf, f1));
        if (!(s > 1e-7f) || angle < 1e-5f)
            break;
        TurnPlacement(hand, axis, angle, r.wrist);
    }
}

// For tests (hands.cpp's stand-in "test bend"): the hand (as Solve's, changed in place) turned about
// its wrist joint by flexion, then ulnar deviation, then twist about the line its straight wrist
// takes (degrees; each about the hand's own axes as they are by then, WristAxes').
inline void BendWrist(float* hand, bool right, float flexion, float deviation, float twist) {
    using namespace detail;
    float f0[3], axes[3][3], at[3];
    WristAxes(right, f0, axes[0], axes[1]);
    std::memcpy(axes[2], f0, sizeof(f0));
    const float sx = right ? -1.0f : 1.0f, W[3] = {kWrist[0] * sx, kWrist[1], kWrist[2]};
    for (int i = 0; i < 3; ++i)
        at[i] = hand[i * 4] * W[0] + hand[i * 4 + 1] * W[1] + hand[i * 4 + 2] * W[2] + hand[i * 4 + 3];
    const float degrees[3] = {flexion, deviation, twist};
    for (int k = 0; k < 3; ++k) {
        if (degrees[k] == 0.0f)
            continue;
        float axis[3];
        Rotate(hand, axes[k], axis);
        Normalize(axis);
        TurnPlacement(hand, axis, degrees[k] * kDeg, at);
    }
}

}  // namespace armik
