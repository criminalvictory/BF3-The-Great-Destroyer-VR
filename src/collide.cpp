#include "collide.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

#include "gunshape.h"

namespace {

using collide::Sphere;

void Transform(const float* m, const float* p, float* out) {
    for (int i = 0; i < 3; ++i)
        out[i] = m[i * 4] * p[0] + m[i * 4 + 1] * p[1] + m[i * 4 + 2] * p[2] + m[i * 4 + 3];
}

// The glove as spheres on its bones (tools/gunshape.cpp measured them from the gloves' mesh dump:
// bone, centre in the glove's bind space, radius), posed with the fingers, in the gun's model space;
// the gun's shape (gunshape.cpp) pushes the deepest one out along its way out, a few times over. The
// glove is drawn there; the reloads still go by the hand.
constexpr Sphere kHandSpheres[] = {
    {2, 0, {0.4796f, 1.0925f, 0.2857f}, 0.0129f},   // the thumb
    {17, 0, {0.4846f, 1.1074f, 0.2477f}, 0.0151f},  //
    {18, 0, {0.4810f, 1.0975f, 0.2051f}, 0.0120f},  //
    {8, 1, {0.5293f, 1.1138f, 0.2400f}, 0.0162f},   // the index finger
    {9, 1, {0.5485f, 1.0843f, 0.2631f}, 0.0160f},   //
    {15, 1, {0.5801f, 1.0441f, 0.3061f}, 0.0098f},  //
    {19, 1, {0.5641f, 1.0664f, 0.2833f}, 0.0136f},  //
    {6, 2, {0.5594f, 1.1058f, 0.2176f}, 0.0075f},   // the middle finger
    {7, 2, {0.5616f, 1.0711f, 0.2509f}, 0.0157f},   //
    {21, 2, {0.5792f, 1.0552f, 0.2750f}, 0.0125f},  //
    {22, 2, {0.5944f, 1.0301f, 0.2984f}, 0.0111f},  //
    {10, 3, {0.6002f, 1.0224f, 0.2762f}, 0.0109f},  // the ring finger
    {11, 3, {0.5717f, 1.0643f, 0.2288f}, 0.0135f},  //
    {12, 3, {0.5618f, 1.0978f, 0.1915f}, 0.0087f},  //
    {20, 3, {0.5847f, 1.0447f, 0.2514f}, 0.0132f},  //
    {3, 4, {0.5960f, 1.0322f, 0.2407f}, 0.0087f},   // the little finger
    {13, 4, {0.5747f, 1.0642f, 0.2069f}, 0.0131f},  //
    {14, 4, {0.5589f, 1.0779f, 0.1713f}, 0.0088f},  //
    {16, 4, {0.5868f, 1.0504f, 0.2225f}, 0.0117f},  //
    {0, -1, {0.4686f, 1.1414f, 0.1480f}, 0.0150f},  // the palm, 3 x 3 along and across it
    {0, -1, {0.4826f, 1.1329f, 0.1330f}, 0.0150f},
    {0, -1, {0.4967f, 1.1245f, 0.1181f}, 0.0150f},
    {0, -1, {0.4773f, 1.1306f, 0.1623f}, 0.0150f},
    {0, -1, {0.4913f, 1.1221f, 0.1473f}, 0.0150f},
    {0, -1, {0.5054f, 1.1137f, 0.1323f}, 0.0150f},
    {0, -1, {0.4860f, 1.1197f, 0.1765f}, 0.0150f},
    {0, -1, {0.5000f, 1.1113f, 0.1615f}, 0.0150f},
    {0, -1, {0.5141f, 1.1029f, 0.1465f}, 0.0150f},
};
constexpr float kTouching = 0.004f;  // a sphere this near the gun touches it
constexpr int kAllSpheres = 5;       // Deepest's finger for every sphere

// How far the spheres of a finger (kHandSpheres' numbering, or kAllSpheres) are in the gun at most
// (metres; negative, the nearest is that far off), the glove placed by toGun (its bind space to the
// gun's model space) with these bones and moved by shift (the gun's model space). False without
// the gun's shape there.
bool Deepest(const float (*bones)[12], const float* toGun, const float* shift, int finger, float& deepest) {
    deepest = -1.0f;
    bool any = false;
    for (const Sphere& s : kHandSpheres) {
        if (finger != kAllSpheres && s.finger != finger)
            continue;
        float posed[3], p[3], distance, way[3];
        Transform(bones[s.bone], s.centre, posed);
        Transform(toGun, posed, p);
        for (int k = 0; k < 3; ++k)
            p[k] += shift[k];
        if (!gunshape::Distance(p, distance, way))
            continue;
        any = true;
        deepest = std::max(deepest, s.radius - distance);
    }
    return any;
}

// The way out of the gun for the glove placed by toGun with these bones: shift (the gun's model
// space) gets the move; touch, where it touches the gun's surface, if it does. True when it touches.
bool PushOut(const float (*bones)[12], const float* toGun, float* shift, float* touch) {
    float centres[std::size(kHandSpheres)][3];
    for (size_t i = 0; i < std::size(kHandSpheres); ++i) {
        float posed[3];
        Transform(bones[kHandSpheres[i].bone], kHandSpheres[i].centre, posed);
        Transform(toGun, posed, centres[i]);
    }
    shift[0] = shift[1] = shift[2] = 0.0f;
    bool touched = false;
    for (int round = 0; round < 4; ++round) {
        float deepest = -kTouching, push[3] = {}, surface[3] = {};
        for (size_t i = 0; i < std::size(kHandSpheres); ++i) {
            const float p[3] = {centres[i][0] + shift[0], centres[i][1] + shift[1], centres[i][2] + shift[2]};
            float distance, way[3];
            if (!gunshape::Distance(p, distance, way))
                continue;
            const float in = kHandSpheres[i].radius - distance;
            if (in <= deepest)
                continue;
            deepest = in;
            for (int k = 0; k < 3; ++k)
                push[k] = way[k] * std::max(in, 0.0f), surface[k] = p[k] - way[k] * distance;
        }
        if (deepest <= -kTouching)
            break;
        if (round == 0) {
            touched = true;
            std::memcpy(touch, surface, sizeof(surface));
        }
        if (deepest <= 0.0f)
            break;
        for (int k = 0; k < 3; ++k)
            shift[k] += push[k];
    }
    return touched;
}

// How far (0-1) the glove placed by toGun can move from shift a to shift b in a straight line
// without going into the gun: in steps of kSweepStep at most, so a fast hand can't step over the
// gun (a hard slap moved the hand further in a frame than half the gun is wide, and pushed out from
// there the glove came out of the far side: user, 2026-10-30), then halved down to the first touch.
constexpr float kSweepStep = 0.004f;
float Reach(const float (*bones)[12], const float* toGun, const float* a, const float* b) {
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const int steps = std::max(1, static_cast<int>(std::ceil(length / kSweepStep)));
    float deepest, last = 0.0f;
    auto in = [&](float f) {
        const float p[3] = {a[0] + d[0] * f, a[1] + d[1] * f, a[2] + d[2] * f};
        return Deepest(bones, toGun, p, kAllSpheres, deepest) && deepest > 0.0f;
    };
    for (int i = 1; i <= steps; ++i) {
        const float f = static_cast<float>(i) / steps;
        if (in(f)) {
            float low = last, high = f;
            for (int k = 0; k < 6; ++k) {
                const float mid = (low + high) / 2;
                (in(mid) ? high : low) = mid;
            }
            return low;
        }
        last = f;
    }
    return 1.0f;
}

}

namespace collide {

void Reset(State& state) {
    state.shift[0] = state.shift[1] = state.shift[2] = 0.0f;
    state.haveLastGlove = false;
}

bool KeepOut(const float (*bones)[12], const float* toGun, State& state, float* out, float* touch) {
    float shift[3], push[3], deepest;
    std::memcpy(shift, state.shift, sizeof(shift));
    // The hand gone far round the gun (20 cm from the glove held off it): the glove follows it again.
    if (shift[0] * shift[0] + shift[1] * shift[1] + shift[2] * shift[2] > 0.2f * 0.2f) {
        shift[0] = shift[1] = shift[2] = 0.0f;
        state.haveLastGlove = false;
    }
    // From where it was drawn last frame towards the hand (with its kept shift), stopping at the
    // first touch: it can't pass through the gun however fast the hand moves.
    if (state.haveLastGlove) {
        const float last[3] = {state.lastGlove[0] - toGun[3], state.lastGlove[1] - toGun[7],
                               state.lastGlove[2] - toGun[11]};
        if (!(Deepest(bones, toGun, last, kAllSpheres, deepest) && deepest > 0.0f)) {
            const float f = Reach(bones, toGun, last, shift);
            for (int k = 0; k < 3; ++k)
                shift[k] = last[k] + (shift[k] - last[k]) * f;
        }
    }
    // Out of the gun from where it was (the gun moved into it).
    float from[12];
    std::memcpy(from, toGun, sizeof(from));
    for (int k = 0; k < 3; ++k)
        from[k * 4 + 3] += shift[k];
    PushOut(bones, from, push, touch);
    for (int k = 0; k < 3; ++k)
        shift[k] += push[k];
    // Back towards the hand as far as it goes without going in (stepped, so it can't hop through
    // to a hand on the gun's far side).
    if (shift[0] || shift[1] || shift[2]) {
        const float zero[3] = {};
        const float f = Reach(bones, toGun, shift, zero);
        for (float& s : shift)
            s *= 1.0f - f;
        if (shift[0] * shift[0] + shift[1] * shift[1] + shift[2] * shift[2] < 0.0005f * 0.0005f)
            shift[0] = shift[1] = shift[2] = 0.0f;
    }
    // Still held off by the gun: along its surface with the hand (the way to the hand less its part
    // into the gun), as far as that goes without going in, so it slides rather than sticks.
    if (shift[0] || shift[1] || shift[2]) {
        float at[12], surface[3], unused[3], distance, normal[3];
        std::memcpy(at, toGun, sizeof(at));
        for (int k = 0; k < 3; ++k)
            at[k * 4 + 3] += shift[k];
        if (PushOut(bones, at, unused, surface) && gunshape::Distance(surface, distance, normal)) {
            const float into = -(shift[0] * normal[0] + shift[1] * normal[1] + shift[2] * normal[2]);
            float along[3];
            for (int k = 0; k < 3; ++k)
                along[k] = -shift[k] - into * normal[k];
            if (into < 0.0f) {
                const float slid[3] = {shift[0] + along[0], shift[1] + along[1], shift[2] + along[2]};
                const float f = Reach(bones, toGun, shift, slid);
                for (int k = 0; k < 3; ++k)
                    shift[k] += along[k] * f;
            }
        }
    }
    std::memcpy(state.shift, shift, sizeof(shift));
    for (int k = 0; k < 3; ++k)
        state.lastGlove[k] = toGun[k * 4 + 3] + shift[k];
    state.haveLastGlove = true;
    // Where it touches the gun now, if it does.
    for (int k = 0; k < 3; ++k)
        from[k * 4 + 3] = toGun[k * 4 + 3] + shift[k];
    std::memcpy(out, shift, sizeof(shift));
    return PushOut(bones, from, push, touch);
}

const Sphere* Spheres(int& count) {
    count = static_cast<int>(std::size(kHandSpheres));
    return kHandSpheres;
}

}
