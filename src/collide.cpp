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

float Dot(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float Length(const float* v) {
    return std::sqrt(Dot(v, v));
}

// The glove as spheres on its bones (tools/gunshape.cpp measured them from the gloves' mesh dump:
// bone, centre in the glove's bind space, radius), posed with the fingers, in the gun's model space,
// kept out of the gun's shape (gunshape.cpp). The glove is drawn there; the reloads still go by the
// hand.
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
constexpr int kSpheres = static_cast<int>(std::size(kHandSpheres));

// The hand's middle in the gloves' bind space (handpose.cpp's kMiddle): the point the glove is
// kept by from frame to frame and turned about. (It was kept by the bind space's own origin, which
// lies 1.2 m from the hand: every small turn of the hand or the gun swung that point centimetres,
// and the glove, swept from there, jumped about; 2026-10-31, tools/collidetest.cpp.)
constexpr float kMiddle[3] = {0.537f, 1.098f, 0.207f};
constexpr float kTouching = 0.004f;  // a sphere this near the gun touches it ...
constexpr float kLetGo = 0.006f;     // ... and touching, the glove lets go this far off (no flicker)
constexpr float kPatch = 0.01f;      // the spheres this near make up where it touches
constexpr float kContact = 0.0005f;  // a sphere this near holds the glove's way along the surface
constexpr float kNear = 0.0015f;     // ... and the way it catches up with the hand (a sphere about to touch)
constexpr float kSkin = 0.0003f;     // how far into the gun a move may take a sphere (pushed out after)
// m a frame the glove may move beyond the hand's own move, back to the hand (collidetest's sweep of
// it, kNear and kDeep, 2026-10-31: the fewest failures and the least jerk).
constexpr float kCatchUp = 0.0015f;
constexpr float kFarApart = 0.2f;    // the hand this far from the glove: the glove follows it again

// The glove this frame: each sphere's centre from the glove's middle, turned as the hand is (the
// gun's model space), and its radius.
struct Glove {
    float offset[kSpheres][3];
    float radius[kSpheres];
};

// How far sphere i is off the gun's surface with the glove's middle at m (metres; negative, in it);
// 1 off the shape's grid (5 cm of air round the gun and more: nothing there touches it).
float Gap(const Glove& g, const float* m, int i) {
    const float p[3] = {m[0] + g.offset[i][0], m[1] + g.offset[i][1], m[2] + g.offset[i][2]};
    float distance;
    return gunshape::Distance(p, distance) ? distance - g.radius[i] : 1.0f;
}

// The same with the gun's way out there (a unit vector); false off the grid.
bool Way(const Glove& g, const float* m, int i, float& gap, float* way) {
    const float p[3] = {m[0] + g.offset[i][0], m[1] + g.offset[i][1], m[2] + g.offset[i][2]};
    float distance;
    if (!gunshape::Distance(p, distance, way))
        return false;
    gap = distance - g.radius[i];
    return true;
}

float LeastGap(const Glove& g, const float* m) {
    float least = 1.0f;
    for (int i = 0; i < kSpheres; ++i)
        least = std::min(least, Gap(g, m, i));
    return least;
}

// Out of the gun: the glove's middle moved the least way that takes every sphere in it out (each
// along its own way out, the deepest first, together; a few rounds as the ways change). Pushing
// only the deepest sphere out flipped between spheres from frame to frame.
void PushOut(const Glove& g, float* m) {
    for (int round = 0; round < 4; ++round) {
        float depth[kSpheres], way[kSpheres][3];
        int order[kSpheres], n = 0;
        for (int i = 0; i < kSpheres; ++i) {
            float gap;
            if (Gap(g, m, i) < 0.0f && Way(g, m, i, gap, way[i]) && gap < 0.0f) {
                depth[i] = -gap;
                order[n++] = i;
            }
        }
        if (!n)
            return;
        std::sort(order, order + n, [&](int a, int b) { return depth[a] > depth[b]; });
        float move[3] = {};
        for (int pass = 0; pass < 3; ++pass)
            for (int k = 0; k < n; ++k) {
                const int i = order[k];
                const float more = depth[i] - Dot(way[i], move);
                if (more > 0.0f)
                    for (int c = 0; c < 3; ++c)
                        move[c] += way[i][c] * more;
            }
        for (int c = 0; c < 3; ++c)
            m[c] += move[c];
    }
}

// How far (0-1) the glove's middle can go from a to b in a straight line, no sphere more than kSkin
// into the gun on the way: in steps of kSweepStep at most, so a fast hand can't step over the gun (a
// hard slap moved the hand further in a frame than half the gun is wide, and pushed out from there
// the glove came out of the far side: user, 2026-10-30), then halved down to the first touch.
constexpr float kSweepStep = 0.004f;
float Sweep(const Glove& g, const float* a, const float* b) {
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const int steps = std::max(1, static_cast<int>(std::ceil(Length(d) / kSweepStep)));
    auto in = [&](float f) {
        const float p[3] = {a[0] + d[0] * f, a[1] + d[1] * f, a[2] + d[2] * f};
        return LeastGap(g, p) < -kSkin;
    };
    float last = 0.0f;
    for (int i = 1; i <= steps; ++i) {
        const float f = static_cast<float>(i) / steps;
        if (in(f)) {
            float low = last, high = f;
            for (int k = 0; k < 8; ++k) {
                const float mid = (low + high) / 2;
                (in(mid) ? high : low) = mid;
            }
            return low;
        }
        last = f;
    }
    return 1.0f;
}

// The ways out at the spheres touching the gun (within `within` of it, or in it) with the middle at
// m; how many.
int Touching(const Glove& g, const float* m, float (*ways)[3], float within = kContact) {
    int n = 0;
    for (int i = 0; i < kSpheres; ++i) {
        float gap;
        if (Gap(g, m, i) < within && Way(g, m, i, gap, ways[n]))
            ++n;
    }
    return n;
}

// v with its parts into the gun where those spheres touch it taken off.
void AlongSurface(float* v, const float (*ways)[3], int n) {
    for (int pass = 0; pass < 3; ++pass)
        for (int k = 0; k < n; ++k) {
            const float into = Dot(v, ways[k]);
            if (into < 0.0f)
                for (int c = 0; c < 3; ++c)
                    v[c] -= ways[k][c] * into;
        }
}

// Moves m by v as far as the gun lets it (sweeping), budget less what it travelled; how far (0-1).
float Go(const Glove& g, float* m, float* v, float& budget) {
    float length = Length(v);
    if (length < 1e-6f)
        return 1.0f;
    if (length > budget) {
        for (int c = 0; c < 3; ++c)
            v[c] *= budget / length;
        length = budget;
    }
    const float b[3] = {m[0] + v[0], m[1] + v[1], m[2] + v[2]};
    const float f = Sweep(g, m, b);
    for (int c = 0; c < 3; ++c)
        m[c] += v[c] * f;
    budget -= length * f;
    return f;
}

// m moved by v along the gun's surface where spheres touch it: swept as far as that goes, then a few
// small steps each pushed back out of the gun, so it rides over the surface's small bumps (swept to
// each, it stuck on one, then slipped, frame after frame), and swept on again past them; budget less
// what it travelled.
constexpr float kGlideStep = 0.002f;  // m a step (thinner than any part of the gun)
void Glide(const Glove& g, float* m, const float* v, float& budget) {
    float rest[3] = {v[0], v[1], v[2]}, ways[kSpheres][3];
    for (int round = 0; round < 8 && budget > 1e-6f; ++round) {
        int n = Touching(g, m, ways);
        AlongSurface(rest, ways, n);
        const float f = Go(g, m, rest, budget);
        for (int c = 0; c < 3; ++c)
            rest[c] *= 1.0f - f;
        if (f >= 1.0f)
            return;
        for (int step = 0; step < 3 && budget > 1e-6f; ++step) {
            n = Touching(g, m, ways);
            float move[3] = {rest[0], rest[1], rest[2]};
            AlongSurface(move, ways, n);
            const float length = Length(move);
            if (length < 1e-5f)
                return;
            const float go = std::min({length, kGlideStep, budget});
            for (int c = 0; c < 3; ++c)
                m[c] += move[c] * (go / length), rest[c] = move[c] * (1.0f - go / length);
            budget -= go;
            PushOut(g, m);
        }
    }
}

// v with its part off the surface where those spheres touch it kept to `most` (each).
void LiftAtMost(float* v, const float (*ways)[3], int n, float most) {
    for (int k = 0; k < n; ++k) {
        const float off = Dot(v, ways[k]);
        if (off > most)
            for (int c = 0; c < 3; ++c)
                v[c] -= ways[k][c] * (off - most);
    }
}

// Catching up with the hand along the surface (half the way along it at most, kCatchUp), less the
// deeper the hand is: slid along the surface towards a hand deep in the gun, all the way to the
// hand turned with each small bump of the surface's way out, a few mm one way, then back, frame after
// frame (tools/collidetest.cpp, 2026-10-31). Its way: along the surface where the glove touches or
// nearly (a sphere a mm or two off the side: sliding up the bevel on the receiver's top edge towards
// a hand in its side, the glove crept up past the hand until the palm met the side). Kept only if
// it brings the glove nearer the hand: where a few spheres touch at once, which of them hold it
// turns the way along the surface, and the glove went back and forth between two places.
constexpr float kDeep = 0.02f;  // the hand this far into the gun from the glove: no catching up along it
void CatchUp(const Glove& g, float* m, const float* to, float& budget) {
    float ways[kSpheres][3];
    const int n = Touching(g, m, ways, kNear);
    float toward[3] = {to[0] - m[0], to[1] - m[1], to[2] - m[2]};
    const float now = Length(toward);
    AlongSurface(toward, ways, n);
    const float across = Length(toward), deep = std::sqrt(std::max(now * now - across * across, 0.0f));
    const float trust = std::clamp(1.0f - deep / kDeep, 0.0f, 1.0f);
    if (across < 1e-6f || trust <= 0.0f)
        return;
    const float step = std::min(kCatchUp, 0.5f * across) * trust / across;
    const float was[3] = {m[0], m[1], m[2]};
    for (float& t : toward)
        t *= step;
    Glide(g, m, toward, budget);
    const float d[3] = {to[0] - m[0], to[1] - m[1], to[2] - m[2]};
    if (Length(d) >= now)
        std::memcpy(m, was, sizeof(was));
}

// From m towards to (the hand), as far as the gun lets it and no faster than the hand moved and a
// little (it comes back to the hand without jumping). handMove: the tracked hand's move this frame.
// It goes with the hand's own move, less any part of it back towards the glove (a hand coming back
// out of the gun leaves the glove resting where it is until the hand reaches it) and no further
// towards the hand than the hand is (a hand coming out of the gun past the glove in one frame took
// it 3 cm past the hand: 2026-10-31), and kCatchUp more towards the hand: straight there to the first
// touch, then the rest of that move along the surface, then catching up along it. Resting on the gun
// with the way to the hand blocked (the hand in the gun, or beyond it): no more than kLift a frame
// of that move off the surface (back from the gun's far side in one frame, beside the glove, the
// hand took it 15 cm out with it), and no catching up straight into the gun (it only slid into the
// gun's skin and back out, creeping).
constexpr float kLeaving = 0.001f;  // m towards the hand before the way stops: it isn't blocked
constexpr float kLift = 0.003f;     // m a frame off the surface with the hand's move, blocked
void Slide(const Glove& g, float* m, const float* to, const float* handMove) {
    float ways[kSpheres][3], toHand[3] = {to[0] - m[0], to[1] - m[1], to[2] - m[2]};
    const float apart = Length(toHand);
    if (apart < 1e-6f)
        return;
    const float press[3] = {toHand[0] / apart, toHand[1] / apart, toHand[2] / apart};
    float follow[3] = {handMove[0], handMove[1], handMove[2]};
    const float toward = Dot(follow, press);
    const float off = toward < 0.0f ? toward : std::max(toward - apart, 0.0f);
    for (int c = 0; c < 3; ++c)
        follow[c] -= press[c] * off;
    bool blocked = false;
    const int n = Touching(g, m, ways);
    if (n > 0) {
        blocked = LeastGap(g, to) < 0.0f;  // the hand's own glove in the gun
        if (!blocked) {                    // or the gun between them (the hand beyond it)
            const float was[3] = {m[0], m[1], m[2]};
            float way[3] = {toHand[0], toHand[1], toHand[2]}, all = apart;
            Go(g, m, way, all);
            blocked = apart - all < kLeaving;
            std::memcpy(m, was, sizeof(was));
        }
        if (blocked)
            LiftAtMost(follow, ways, n, kLift);
    }
    float budget = Length(follow) + kCatchUp, rest[3] = {}, straight[3];
    if (!blocked) {
        for (int c = 0; c < 3; ++c)
            rest[c] = to[c] - m[c] - follow[c];
        const float left = Length(rest);
        if (left > kCatchUp)
            for (float& r : rest)
                r *= kCatchUp / left;
    }
    for (int c = 0; c < 3; ++c)
        straight[c] = follow[c] + rest[c];
    const float f = Go(g, m, straight, budget);
    if (f >= 1.0f && !blocked)
        return;
    for (int c = 0; c < 3; ++c)
        rest[c] = follow[c] * (1.0f - f);
    Glide(g, m, rest, budget);
    CatchUp(g, m, to, budget);
}

}

namespace collide {

void Reset(State& state) {
    state.have = false;
    state.touching = false;
}

bool KeepOut(const float (*bones)[12], const float* toGun, State& state, float* shift, float* touch) {
    // The glove's spheres about its middle, turned as the hand is; the tracked hand's middle.
    Glove g;
    for (int i = 0; i < kSpheres; ++i) {
        float posed[3], d[3];
        Transform(bones[kHandSpheres[i].bone], kHandSpheres[i].centre, posed);
        for (int k = 0; k < 3; ++k)
            d[k] = posed[k] - kMiddle[k];
        for (int r = 0; r < 3; ++r)
            g.offset[i][r] = toGun[r * 4] * d[0] + toGun[r * 4 + 1] * d[1] + toGun[r * 4 + 2] * d[2];
        g.radius[i] = kHandSpheres[i].radius;
    }
    float hand[3], m[3];
    Transform(toGun, kMiddle, hand);
    std::memcpy(m, hand, sizeof(m));
    // From where it was drawn last frame, unless the hand has gone far round the gun (20 cm from the
    // glove held off it): then it follows the hand again.
    bool kept = false;
    float moved[3] = {};
    if (state.have) {
        const float apart[3] = {state.middle[0] - hand[0], state.middle[1] - hand[1], state.middle[2] - hand[2]};
        if (Length(apart) <= kFarApart) {
            for (int k = 0; k < 3; ++k)
                moved[k] = hand[k] - state.handMiddle[k];
            std::memcpy(m, state.middle, sizeof(m));
            kept = true;
        }
    }
    // Out of the gun where it was (the hand turned, the fingers closed or the gun moved into it), then
    // towards the hand along the gun's surface (Slide), and out of the gun's skin it may have slid into.
    PushOut(g, m);
    if (kept)
        Slide(g, m, hand, moved);
    PushOut(g, m);
    std::memcpy(state.middle, m, sizeof(m));
    std::memcpy(state.handMiddle, hand, sizeof(hand));
    state.have = true;
    for (int k = 0; k < 3; ++k)
        shift[k] = m[k] - hand[k];
    // Touching (once touching, until a little further off), and where: the surface under the spheres
    // near it, the nearest most (the first sphere to touch is often the thumb on an edge, the palm
    // landing just after).
    float gaps[kSpheres], least = 1.0f;
    for (int i = 0; i < kSpheres; ++i)
        least = std::min(least, gaps[i] = Gap(g, m, i));
    state.touching = least < (state.touching ? kLetGo : kTouching);
    if (!state.touching)
        return false;
    float sum[3] = {}, weights = 0.0f;
    for (int i = 0; i < kSpheres; ++i) {
        float gap, way[3];
        if (gaps[i] >= kPatch || !Way(g, m, i, gap, way))
            continue;
        const float w = (kPatch - gap) * (kPatch - gap), reach = gap + g.radius[i];
        for (int k = 0; k < 3; ++k)
            sum[k] += w * (m[k] + g.offset[i][k] - way[k] * reach);
        weights += w;
    }
    if (weights <= 0.0f)
        return state.touching = false;
    for (int k = 0; k < 3; ++k)
        touch[k] = sum[k] / weights;
    return true;
}

const Sphere* Spheres(int& count) {
    count = kSpheres;
    return kHandSpheres;
}

}
