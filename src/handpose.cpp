#include "handpose.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

constexpr float kRad = 3.14159265f / 180.0f;

// The hand's axes in the gloves' bind space (the principal axes of the left hand's vertices): along
// the fingers, across the hand from the thumb to the little finger, and out of the palm.
constexpr float kAlong[3] = {0.437f, -0.544f, 0.716f};
constexpr float kAcross[3] = {0.633f, -0.380f, -0.675f};
constexpr float kPalm[3] = {-0.639f, -0.748f, -0.179f};
constexpr float kMiddle[3] = {0.537f, 1.098f, 0.207f};  // the hand's centre, widened from

// Each finger's joints, where its bones' weights blend in the mesh, moved onto the finger's middle
// line (the blends lean up to 8 mm towards a neighbour, which tilted the bends: the fingers curled
// slanting into each other, user 2026-10-30): the thumb's base, knuckle and last joint, the others'
// knuckle, middle and last joint; then the tip.
constexpr float kJoint[5][4][3] = {
    {{0.4876f, 1.1188f, 0.1914f}, {0.4914f, 1.1173f, 0.2255f}, {0.4811f, 1.0978f, 0.2644f}, {0.4807f, 1.0905f, 0.3016f}},
    {{0.5397f, 1.0967f, 0.2509f}, {0.5571f, 1.0694f, 0.2765f}, {0.5707f, 1.0584f, 0.2920f}, {0.5855f, 1.0376f, 0.3128f}},
    {{0.5566f, 1.0883f, 0.2386f}, {0.5727f, 1.0598f, 0.2662f}, {0.5839f, 1.0467f, 0.2821f}, {0.6005f, 1.0244f, 0.3070f}},
    {{0.5631f, 1.0754f, 0.2145f}, {0.5814f, 1.0526f, 0.2437f}, {0.5919f, 1.0398f, 0.2603f}, {0.6038f, 1.0165f, 0.2839f}},
    {{0.5661f, 1.0716f, 0.1939f}, {0.5817f, 1.0523f, 0.2182f}, {0.5915f, 1.0419f, 0.2326f}, {0.5992f, 1.0266f, 0.2476f}},
};
// The fingers' directions (the principal axes of their vertices): within 3 degrees of parallel, so
// bent across these they curl straight into the palm.
constexpr float kDirection[5][3] = {{-0.0373f, -0.2208f, 0.9746f},
                                    {0.4764f, -0.6044f, 0.6385f},
                                    {0.4662f, -0.5723f, 0.6747f},
                                    {0.4017f, -0.5973f, 0.6942f},
                                    {0.3832f, -0.6272f, 0.6781f}};
// Each bone's finger (0 the thumb ... 4 the little finger; -1 the wrist and palm) and how many of
// its joints lie between it and the palm (0: a bone of the palm itself).
constexpr int8_t kFinger[handpose::kBones] = {-1, -1, 0, 4, -1, -1, 2, 2, 1, 1, 3, 3, 3, 4, 4, 1, 4, 0, 0, 1, 3, 2, 2};
constexpr int8_t kDepth[handpose::kBones] = {0, 0, 3, 3, 0, 0, 0, 1, 0, 1, 3, 1, 0, 1, 0, 3, 2, 2, 1, 2, 2, 2, 3};
constexpr bool IsCuff(int bone) { return bone == 0 || bone == 4 || bone == 5; }

// The right glove's (handpose.h), bent from the game's own grip: so its fingers turn as the game's
// rig turns them. That grip (the gloves' depth pass's dumped bones, build\mesh_7617_49995_bones.bin)
// turns each finger's middle and last joints about one axis, and each joint about a line that stays
// put (to 0.01 mm). Each joint is the point of its line nearest where the bones' weights blend in
// the mesh (found as the left's above: within 9 mm, the index finger's within 6), the tip as the
// left's: on the finger's middle line, as far out as it reaches (the index finger's line runs along
// -0.473, -0.617, 0.629). Found the left's way, the axis (across that line and the palm, the left's
// kPalm mirrored) was 10 degrees off the rig's: enough to bend the trigger finger slanting off the
// game's own curl. The thumb, whose joints turn about three different axes, is the left's way.
constexpr float kRightJoint[5][4][3] = {
    {{-0.4890f, 1.1214f, 0.1899f}, {-0.4894f, 1.1168f, 0.2220f}, {-0.4798f, 1.0986f, 0.2628f}, {-0.4782f, 1.0904f, 0.3019f}},
    {{-0.5372f, 1.0958f, 0.2449f}, {-0.5567f, 1.0701f, 0.2773f}, {-0.5695f, 1.0542f, 0.2933f}, {-0.5843f, 1.0392f, 0.3112f}},
    {{-0.5486f, 1.0881f, 0.2345f}, {-0.5716f, 1.0595f, 0.2660f}, {-0.5850f, 1.0425f, 0.2855f}, {-0.5989f, 1.0254f, 0.3051f}},
    {{-0.5599f, 1.0772f, 0.2129f}, {-0.5806f, 1.0512f, 0.2444f}, {-0.5925f, 1.0360f, 0.2635f}, {-0.6034f, 1.0196f, 0.2823f}},
    {{-0.5645f, 1.0712f, 0.1934f}, {-0.5802f, 1.0518f, 0.2175f}, {-0.5888f, 1.0410f, 0.2299f}, {-0.5997f, 1.0275f, 0.2463f}},
};
// Each finger's axis (positive turns curl it towards the palm).
constexpr float kRightAxis[5][3] = {{0.7683f, 0.6321f, 0.1012f},
                                    {0.4704f, 0.4589f, 0.7538f},
                                    {0.4783f, 0.4689f, 0.7425f},
                                    {0.4969f, 0.4923f, 0.7146f},
                                    {0.5063f, 0.5039f, 0.6998f}};
// The fingers are moved apart on the widened palm and slimmed to this across, rather than widened
// with it (the whole hand widened alike looked thick, "sausage-y": user 2026-10-30). The cracks this
// opened in the glove are closed by skin.cpp's patch. handpose::Tune "slim".
float g_slim = 0.88f;

// Degrees towards the palm at the knuckle, the middle and the last joint (index ... little), relaxed
// and in a fist; and how far each finger turns towards the thumb when spread.
struct Bend {
    float knuckle, middle, last;
};
constexpr Bend kRelaxed[4] = {{10, 14, 8}, {12, 18, 10}, {14, 22, 12}, {16, 26, 14}};
// (The index curls tighter at its middle joint: rolled like the others, its middle bone lay where
// the folded thumb goes.)
Bend g_fist[4] = {{75, 115, 65}, {90, 102, 62}, {92, 104, 62}, {94, 104, 62}};  // handpose::Tune
constexpr float kSpread[4] = {12, 1, -5, -17};  // the middle and ring a little closer: user 2026-10-30
// ... and curled all the way (the glove's thick fingers, side by side, touched).
float g_curlSpread[4] = {0, 0, 0, 0};
// The thumb: across in front of the palm (about the hand's length) and bent at its base, knuckle and
// last joint. Folded in, it rests on the curled middle finger (tools/handview.cpp's check: within a
// millimetre; folded further, it sank into the fingers).
struct ThumbBend {
    float across, base, knuckle, last;
};
constexpr ThumbBend kThumbRelaxed = {0, 0, 4, 6};
ThumbBend g_thumbFist = {35, 18, 40, 31};  // handpose::Tune
// How far the thumb can fold (0-1) before it runs into the index and middle fingers as they curl:
// rows the index finger's curl 0, 0.25 ... 1, columns the middle finger's (tools/handview.cpp's
// clipping check, 1.5 mm at most). Half curled, their tips stand out across its path; both curled
// all the way, it lies on them (the fist).
constexpr float kThumbRoom[5][5] = {
    {1.0f, 1.0f, 0.7f, 0.8f, 1.0f},
    {1.0f, 1.0f, 0.7f, 0.8f, 1.0f},
    {0.4f, 0.4f, 0.4f, 0.4f, 0.4f},
    {0.2f, 0.2f, 0.2f, 0.2f, 0.2f},
    {0.4f, 0.4f, 0.4f, 0.4f, 1.0f},
};

float Lerp(float a, float b, float t) { return a + (b - a) * t; }
float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

float ThumbRoom(float index, float middle) {
    const float x = Clamp01(index) * 4, y = Clamp01(middle) * 4;
    const int i = x < 3 ? static_cast<int>(x) : 3, j = y < 3 ? static_cast<int>(y) : 3;
    return Lerp(Lerp(kThumbRoom[i][j], kThumbRoom[i][j + 1], y - j),
                Lerp(kThumbRoom[i + 1][j], kThumbRoom[i + 1][j + 1], y - j), x - i);
}

void Normalize(float* v) {
    float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n > 1e-9f)
        v[0] /= n, v[1] /= n, v[2] /= n;
}

void Cross(const float* a, const float* b, float* out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// Affine 3x4 matrices (three rows: rotation | translation).
void Compose(const float* a, const float* b, float* out) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j)
            out[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j];
        out[i * 4 + 3] += a[i * 4 + 3];
    }
}

void Transform(const float* m, const float* p, float* out) {
    for (int i = 0; i < 3; ++i)
        out[i] = m[i * 4] * p[0] + m[i * 4 + 1] * p[1] + m[i * 4 + 2] * p[2] + m[i * 4 + 3];
}

// A turn of degrees about the unit axis through pivot (right-handed).
void TurnAbout(const float* axis, float degrees, const float* pivot, float* out) {
    float c = std::cos(degrees * kRad), s = std::sin(degrees * kRad), t = 1 - c;
    float x = axis[0], y = axis[1], z = axis[2];
    const float r[9] = {t * x * x + c,     t * x * y - s * z, t * x * z + s * y,
                        t * x * y + s * z, t * y * y + c,     t * y * z - s * x,
                        t * x * z - s * y, t * y * z + s * x, t * z * z + c};
    for (int i = 0; i < 3; ++i) {
        out[i * 4] = r[i * 3], out[i * 4 + 1] = r[i * 3 + 1], out[i * 4 + 2] = r[i * 3 + 2];
        out[i * 4 + 3] = pivot[i] - (r[i * 3] * pivot[0] + r[i * 3 + 1] * pivot[1] + r[i * 3 + 2] * pivot[2]);
    }
}

// Scaled by s along the unit axis, about the plane through point across it.
void ScaleAlong(const float* axis, float s, const float* point, float* out) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            out[i * 4 + j] = (i == j ? 1.0f : 0.0f) + (s - 1) * axis[i] * axis[j];
        out[i * 4 + 3] = point[i] - (out[i * 4] * point[0] + out[i * 4 + 1] * point[1] + out[i * 4 + 2] * point[2]);
    }
}

// One finger of a posed hand bent further (degrees; negative opens it), spread over its three
// joints (the middle one most), about where those joints are in that pose. at: its bones by how
// many joints lie between each and the palm (1-3; null where the palette hasn't one), each from the
// bind space to wherever the hand is drawn, changed in place; joint: its joints in the bind space,
// axis the one it bends about there (unit; positive turns curl it towards the palm).
void BendFinger(float* const at[4], const float (*joint)[3], const float* axis, float degrees) {
    constexpr float kShare[3] = {0.35f, 0.4f, 0.25f};  // of the bend at the knuckle, middle and last joint
    if (!at[1])
        return;
    // Its bending axis, turned into the pose as its first bone is.
    float posedAxis[3];
    const float* first = at[1];
    for (int i = 0; i < 3; ++i)
        posedAxis[i] = first[i * 4] * axis[0] + first[i * 4 + 1] * axis[1] + first[i * 4 + 2] * axis[2];
    Normalize(posedAxis);
    // Each joint where the pose has it (it moves with the bone just past it).
    float turns[3][12];
    for (int j = 0; j < 3; ++j) {
        float posed[3];
        Transform(at[j + 1] ? at[j + 1] : first, joint[j], posed);
        TurnAbout(posedAxis, degrees * kShare[j], posed, turns[j]);
    }
    // A bone turns at every joint between it and the palm, the farthest first.
    for (int d = 1; d <= 3; ++d) {
        if (!at[d])
            continue;
        float m[12], turned[12];
        std::memcpy(m, at[d], sizeof(m));
        for (int j = d - 1; j >= 0; --j) {
            Compose(turns[j], m, turned);
            std::memcpy(m, turned, sizeof(m));
        }
        std::memcpy(at[d], m, sizeof(m));
    }
}

}

namespace handpose {

void Pose(const Fingers& fingers, float width, float out[kBones][12]) {
    // The palm is widened across, about the hand's middle (the wrist's cuff half as much); each
    // finger moves apart with it and is slimmed about its own middle line, then bends.
    float widen[12], widenCuff[12];
    ScaleAlong(kAcross, width, kMiddle, widen);
    ScaleAlong(kAcross, 1 + (width - 1) * 0.5f, kMiddle, widenCuff);
    float fit[5][12];       // each finger onto the widened palm
    float joint[5][4][3];   // its joints there
    float chain[5][3][12];  // its moves after one, two, three joints from the palm
    float bendAxis[5][3];
    for (int f = 0; f < 5; ++f) {
        Cross(kDirection[f], kPalm, bendAxis[f]);
        Normalize(bendAxis[f]);
        float base[3];
        Transform(widen, kJoint[f][0], base);
        ScaleAlong(bendAxis[f], f == 0 ? 1.0f : g_slim, kJoint[f][0], fit[f]);
        for (int i = 0; i < 3; ++i)
            fit[f][i * 4 + 3] += base[i] - kJoint[f][0][i];
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 3; ++i)
                joint[f][j][i] = kJoint[f][j][i] + base[i] - kJoint[f][0][i];
    }
    for (int f = 1; f < 5; ++f) {
        const int k = f - 1;
        const float c = Clamp01(fingers.curl[k]);
        const float* axis = bendAxis[f];
        const float spread = kSpread[k] * Clamp01(fingers.spread) * (1 - c) + g_curlSpread[k] * c + fingers.apart[k];
        float apart[12], bent[12], knuckle[12], middle[12], last[12];
        TurnAbout(kPalm, spread, joint[f][0], apart);
        TurnAbout(axis, Lerp(kRelaxed[k].knuckle, g_fist[k].knuckle, c), joint[f][0], bent);
        Compose(apart, bent, knuckle);
        TurnAbout(axis, Lerp(kRelaxed[k].middle, g_fist[k].middle, c), joint[f][1], middle);
        TurnAbout(axis, Lerp(kRelaxed[k].last, g_fist[k].last, c), joint[f][2], last);
        std::memcpy(chain[f][0], knuckle, sizeof(knuckle));
        Compose(chain[f][0], middle, chain[f][1]);
        Compose(chain[f][1], last, chain[f][2]);
    }
    {
        // The thumb's pad faces the palm and the fingers; it bends towards them, and its base turns
        // it across in front of the palm.
        const float t = std::fmin(Clamp01(fingers.thumb), ThumbRoom(fingers.curl[0], fingers.curl[1]));
        float along[3] = {joint[0][2][0] - joint[0][1][0], joint[0][2][1] - joint[0][1][1],
                          joint[0][2][2] - joint[0][1][2]};
        Normalize(along);
        float pad[3] = {kPalm[0] + kAcross[0], kPalm[1] + kAcross[1], kPalm[2] + kAcross[2]};
        float axis[3];
        Cross(along, pad, axis);
        Normalize(axis);
        float across[12], base[12], knuckle[12], last[12];
        const ThumbBend& fist = g_thumbFist;
        TurnAbout(kAlong, Lerp(kThumbRelaxed.across, fist.across, t), joint[0][0], across);
        TurnAbout(axis, Lerp(kThumbRelaxed.base, fist.base, t), joint[0][0], base);
        Compose(across, base, chain[0][0]);
        TurnAbout(axis, Lerp(kThumbRelaxed.knuckle, fist.knuckle, t), joint[0][1], knuckle);
        TurnAbout(axis, Lerp(kThumbRelaxed.last, fist.last, t), joint[0][2], last);
        Compose(chain[0][0], knuckle, chain[0][1]);
        Compose(chain[0][1], last, chain[0][2]);
    }
    for (int b = 0; b < kBones; ++b) {
        const int f = kFinger[b], depth = kDepth[b];
        if (f < 0 || depth == 0)
            std::memcpy(out[b], IsCuff(b) ? widenCuff : widen, sizeof(widen));
        else
            Compose(chain[f][depth - 1], fit[f], out[b]);
    }
}

void BendFurther(float bones[kBones][12], const float bend[5]) {
    for (int f = 0; f < 5; ++f) {
        if (bend[f] == 0.0f)
            continue;
        // The finger's bones by how many joints lie between each and the palm (1-3), bent across its
        // direction and the palm.
        float* at[4] = {};
        for (int b = 0; b < kBones; ++b)
            if (kFinger[b] == f && kDepth[b] >= 1 && kDepth[b] <= 3)
                at[kDepth[b]] = bones[b];
        float axis[3];
        Cross(kDirection[f], kPalm, axis);
        Normalize(axis);
        BendFinger(at, kJoint[f], axis, bend[f]);
    }
}

void BendRight(float palette[][12], const float bend[5], const int8_t* map, size_t count) {
    // Each finger's bones by how many joints lie between each and the palm (1-3), where the palette
    // has them: its own numbers, or the gloves' through map.
    float* at[5][4] = {};
    const size_t entries = map ? count : static_cast<size_t>(kRightFirst + kRightBones);
    for (size_t k = 0; k < entries; ++k) {
        const int b = (map ? map[k] : static_cast<int>(k)) - kRightFirst;
        if (b < 0 || b >= kRightBones || kRightFinger[b] < 0 || kRightDepth[b] < 1)
            continue;
        at[kRightFinger[b]][kRightDepth[b]] = palette[k];
    }
    for (int f = 0; f < 5; ++f)
        if (bend[f] != 0.0f)
            BendFinger(at[f], kRightJoint[f], kRightAxis[f], bend[f]);
}

void BendRightIndex(float palette[][12], float degrees, const int8_t* map, size_t count) {
    const float bend[5] = {0.0f, degrees, 0.0f, 0.0f, 0.0f};
    BendRight(palette, bend, map, count);
}

void Joints(float out[5][4][3]) { std::memcpy(out, kJoint, sizeof(kJoint)); }

void RightJoints(float out[5][4][3]) { std::memcpy(out, kRightJoint, sizeof(kRightJoint)); }

void RightAxes(float out[5][3]) { std::memcpy(out, kRightAxis, sizeof(kRightAxis)); }

void Tune(const char* name, const float* v, int count) {
    if (std::strcmp(name, "thumb") == 0 && count == 4)
        g_thumbFist = {v[0], v[1], v[2], v[3]};
    else if (std::strncmp(name, "fist", 4) == 0 && name[4] >= '1' && name[4] <= '4' && count == 3)
        g_fist[name[4] - '1'] = {v[0], v[1], v[2]};
    else if (std::strcmp(name, "curlspread") == 0 && count == 4)
        std::memcpy(g_curlSpread, v, sizeof(g_curlSpread));
    else if (std::strcmp(name, "slim") == 0 && count == 1)
        g_slim = v[0];
}

}
