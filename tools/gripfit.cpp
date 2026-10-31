// Fits the left glove round the rifle's handguard without the game (the user: a hand made for
// holding the gun there, the fingers wrapped round it, no clipping, as on the charging handle).
// The glove (the gloves' mesh dump, posed by src/handpose.cpp as the mod does) palm up under the
// handguard, the fingers across under it and curled up round its far (right) side, the thumb along
// its near (left) side, the index finger towards the muzzle. Searches the hand's roll about the
// barrel, its turn about the palm, the fingers' curl and the hand's place for the most glove
// touching the handguard (within 2 mm of its surface) and nothing in it (the gun's distance field,
// 1 mm). Prints the placement for hands.cpp and draws the fit from four sides into a .bmp.
// Usage: gripfit <dump folder> <out.bmp> [z=place along the gun, default 0.56]

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "meshdump.h"

using namespace meshdump;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: gripfit <dump folder> <out.bmp> [z=metres along the gun]\n");
        return 1;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string dir = argv[1];
    float z0 = 0.56f;
    for (int i = 3; i < argc; ++i)
        if (!std::strncmp(argv[i], "z=", 2))
            z0 = static_cast<float>(atof(argv[i] + 2));

    Mesh gun;
    std::vector<GloveVertex> glove;
    std::vector<int> gloveTris;
    if (!LoadGun(dir, gun) || !LoadGlove(dir, glove, gloveTris)) {
        std::fprintf(stderr, "the gun's or the gloves' mesh dump isn't in %s\n", dir.c_str());
        return 1;
    }
    // The gun round the handguard as a distance field, 1 mm.
    Solid solid;
    const float lo[3] = {-0.07f, -0.10f, z0 - 0.16f}, hi[3] = {0.07f, 0.11f, z0 + 0.16f};
    solid.Build(gun, lo, hi, 0.001f);
    const std::vector<float> field = DistanceField(solid);
    auto distance = [&](const float* p) {
        int x, y, z;
        return solid.Cell(p, x, y, z) ? field[solid.At(x, y, z)] : 0.05f;
    };
    // The handguard's cross-sections: the solid's extent across and up, every 2 cm along it.
    for (float z = z0 - 0.12f; z <= z0 + 0.121f; z += 0.02f) {
        float a[3] = {1, 1, 1}, b[3] = {-1, -1, -1};
        for (int y = 0; y < solid.n[1]; ++y)
            for (int x = 0; x < solid.n[0]; ++x) {
                const float p[3] = {lo[0] + (x + 0.5f) * solid.cell, lo[1] + (y + 0.5f) * solid.cell, z};
                if (distance(p) > 0.0f)
                    continue;
                for (int k = 0; k < 2; ++k)
                    a[k] = std::min(a[k], p[k]), b[k] = std::max(b[k], p[k]);
            }
        std::printf("at z %.2f: x %.3f..%.3f, y %.3f..%.3f\n", z, a[0], b[0], a[1], b[1]);
    }
    // At z0: the handguard's middle (the solid's extent there, below the sight rail's top).
    float a[2] = {1, 1}, b[2] = {-1, -1};
    for (int y = 0; y < solid.n[1]; ++y)
        for (int x = 0; x < solid.n[0]; ++x) {
            const float p[3] = {lo[0] + (x + 0.5f) * solid.cell, lo[1] + (y + 0.5f) * solid.cell, z0};
            if (distance(p) > 0.0f)
                continue;
            for (int k = 0; k < 2; ++k)
                a[k] = std::min(a[k], p[k]), b[k] = std::max(b[k], p[k]);
        }
    const float middle[3] = {(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, z0};
    std::printf("the handguard at z %.2f: middle (%.3f, %.3f), %.0f mm across, %.0f mm high\n", z0, middle[0], middle[1],
                (b[0] - a[0]) * 1000, (b[1] - a[1]) * 1000);

    // The hand closes on the handguard as a hand does (the user's picture, 2026-10-30: the palm
    // under it, the fingers wrapped up its far side, the thumb round the near side): placed open
    // (fingers straight, thumb out) with its palm's middle under the handguard, raised until it
    // meets the underside, then each finger and the thumb curled until it touches the gun (2.5% of a
    // curl at a time, the last without going in). The fit with the most glove touching wins; the
    // fingers round the far (right) side and the thumb count for more.
    struct Fit {
        float roll = 0, turn = 0;
        handpose::Fingers fingers;
        float rotation[9] = {}, at[3] = {};
        float score = -1e30f;
        int touching = 0, thumb = 0, palm = 0;
    };
    Fit best;
    const float pi = 3.14159265f;
    constexpr float kWidth = 1.15f, kAllowed = 0.0006f, kTouch = 0.002f, kStep = 0.025f;
    // Every second vertex, by finger (0 the palm, 1 the thumb .. 5 the little finger).
    std::vector<int> byFinger[6];
    for (size_t i = 0; i < glove.size(); i += 2)
        if (glove[i].finger >= -1)
            byFinger[glove[i].finger + 1].push_back(static_cast<int>(i));
    auto posePoint = [&](const float (*bones)[12], int i, float* out) {
        out[0] = out[1] = out[2] = 0.0f;
        for (int s = 0; s < 4; ++s) {
            if (glove[i].weight[s] <= 0 || glove[i].bone[s] >= handpose::kBones)
                continue;
            float p[3];
            Apply(bones[glove[i].bone[s]], glove[i].pos, p);
            for (int k = 0; k < 3; ++k)
                out[k] += glove[i].weight[s] * p[k];
        }
    };
    handpose::Fingers open;
    open.spread = 0.1f;
    float openBones[handpose::kBones][12];
    handpose::Pose(open, kWidth, openBones);
    // The palm's middle: the hand is placed by it (curling doesn't move it).
    float palm[3] = {};
    for (int i : byFinger[0]) {
        float p[3];
        posePoint(openBones, i, p);
        for (int k = 0; k < 3; ++k)
            palm[k] += p[k] / byFinger[0].size();
    }
    for (float rollDeg = -30; rollDeg <= 60; rollDeg += 10)
        for (float turnDeg = -30; turnDeg <= 30; turnDeg += 10) {
            // Palm up turned by roll about the barrel towards the right (-x); across (thumb to
            // little finger) back along the gun; along (wrist to fingers) to the right; then turned
            // about the palm's normal, the fingers a little forward or back.
            const float r = rollDeg * pi / 180, t = turnDeg * pi / 180;
            float P[3] = {-std::sin(r), std::cos(r), 0.0f}, C[3] = {0, 0, -1}, A[3];
            Cross(P, C, A);  // A x C = -P, as kAlong x kAcross = -kPalm
            float A2[3], C2[3];
            for (int k = 0; k < 3; ++k)
                A2[k] = A[k] * std::cos(t) + C[k] * std::sin(t), C2[k] = C[k] * std::cos(t) - A[k] * std::sin(t);
            float m[9];  // [A2 C2 P] [kAlong kAcross kPalm]^T: the bind axes onto these
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    m[i * 3 + j] = A2[i] * kAlong[j] + C2[i] * kAcross[j] + P[i] * kPalm[j];
            // A glove point (posed, bind space) on the gun with the palm's middle at at.
            auto place = [&](const float* p, const float* at, float* out) {
                const float d[3] = {p[0] - palm[0], p[1] - palm[1], p[2] - palm[2]};
                for (int k = 0; k < 3; ++k)
                    out[k] = m[k * 3] * d[0] + m[k * 3 + 1] * d[1] + m[k * 3 + 2] * d[2] + at[k];
            };
            // How deep these fingers' vertices are in the gun.
            auto sinkOf = [&](const float (*bones)[12], const float* at, int from, int to) {
                float sink = 0.0f;
                for (int f = from; f <= to; ++f)
                    for (int i : byFinger[f]) {
                        float p[3], q[3];
                        posePoint(bones, i, p);
                        place(p, at, q);
                        sink = std::max(sink, -distance(q));
                    }
                return sink;
            };
            for (float dx = -0.03f; dx <= 0.05f; dx += 0.004f) {
                // Raised open under the handguard until it meets it.
                float low = -0.13f, high = 0.02f;
                float at[3] = {middle[0] + dx, middle[1] + low, z0};
                if (sinkOf(openBones, at, 0, 5) > kAllowed)
                    continue;
                for (int step = 0; step < 9; ++step) {
                    const float mid = (low + high) / 2;
                    at[1] = middle[1] + mid;
                    (sinkOf(openBones, at, 0, 5) <= kAllowed ? low : high) = mid;
                }
                at[1] = middle[1] + low;
                // Each finger, then the thumb, curled until it touches.
                handpose::Fingers fingers = open;
                float bones[handpose::kBones][12];
                for (int which = 0; which <= 4; ++which) {
                    float& curl = which == 4 ? fingers.thumb : fingers.curl[which];
                    const int finger = which == 4 ? 1 : which + 2;
                    for (float c = kStep; c <= 1.0f + 1e-4f; c += kStep) {
                        curl = c;
                        handpose::Pose(fingers, kWidth, bones);
                        if (sinkOf(bones, at, finger, finger) > kAllowed) {
                            curl = c - kStep;
                            break;
                        }
                    }
                }
                handpose::Pose(fingers, kWidth, bones);
                int touching = 0, thumb = 0, palmTouch = 0, far = 0;
                for (int f = 0; f <= 5; ++f)
                    for (int i : byFinger[f]) {
                        float p[3], q[3];
                        posePoint(bones, i, p);
                        place(p, at, q);
                        if (distance(q) >= kTouch)
                            continue;
                        if (f == 0)
                            ++palmTouch;
                        else if (f == 1)
                            ++thumb;
                        else
                            ++touching, far += q[0] < middle[0];
                    }
                const float score = 2.0f * touching + 2.0f * far + 3.0f * thumb + palmTouch;
                if (score > best.score) {
                    best.score = score, best.touching = touching, best.thumb = thumb, best.palm = palmTouch;
                    best.roll = rollDeg, best.turn = turnDeg, best.fingers = fingers;
                    std::memcpy(best.rotation, m, sizeof(m));
                    // The palm's middle goes to at: the translation is at - m * palm.
                    for (int k = 0; k < 3; ++k)
                        best.at[k] = at[k] - (m[k * 3] * palm[0] + m[k * 3 + 1] * palm[1] + m[k * 3 + 2] * palm[2]);
                }
            }
        }
    const handpose::Fingers& fingers = best.fingers;
    std::printf("best: roll %.0f, turn %.0f; curl index %.3f middle %.3f ring %.3f little %.3f thumb %.3f; touching "
                "(of half the vertices): fingers %d, thumb %d, palm %d\n",
                best.roll, best.turn, fingers.curl[0], fingers.curl[1], fingers.curl[2], fingers.curl[3], fingers.thumb,
                best.touching, best.thumb, best.palm);
    const float* m = best.rotation;
    std::printf("placement: {%.5ff, %.5ff, %.5ff, %.5ff, %.5ff, %.5ff, %.5ff, %.5ff, %.5ff, %.5ff, %.5ff, %.5ff}\n", m[0],
                m[1], m[2], best.at[0], m[3], m[4], m[5], best.at[1], m[6], m[7], m[8], best.at[2]);

    // All the glove's vertices with the best fit: how deep the deepest is, and which finger.
    const std::vector<float> posed = PoseGlove(glove, fingers, kWidth);
    std::vector<float> placed(glove.size() * 3);
    float deepest = 0.0f;
    int deepestFinger = -1;
    for (size_t i = 0; i < glove.size(); ++i) {
        for (int k = 0; k < 3; ++k)
            placed[i * 3 + k] = m[k * 3] * posed[i * 3] + m[k * 3 + 1] * posed[i * 3 + 1] + m[k * 3 + 2] * posed[i * 3 + 2] +
                                best.at[k];
        if (const float d = -distance(&placed[i * 3]); d > deepest)
            deepest = d, deepestFinger = glove[i].finger;
    }
    static const char* const kNames[6] = {"the palm", "the thumb", "the index finger", "the middle finger",
                                          "the ring finger", "the little finger"};
    std::printf("all vertices: deepest %.1f mm in, %s\n", deepest * 1000, kNames[std::clamp(deepestFinger + 1, 0, 5)]);

    // Four views: from the left side, from below, from behind (the shooter), from in front.
    const float scale = kTile / 0.16f;
    Image img(kTile * 4, kTile);
    const View views[4] = {MakeView(-1, 0, 0, 0, 1, 0), MakeView(0, 1, 0, 0, 0, 1), MakeView(0, -0.2f, 1, 0, 1, 0),
                           MakeView(0, -0.2f, -1, 0, 1, 0)};
    constexpr float kGunColour[3] = {0.55f, 0.55f, 0.6f};
    constexpr float kFingerColour[6][3] = {{0.6f, 0.5f, 0.4f}, {0.9f, 0.25f, 0.2f}, {0.95f, 0.55f, 0.15f},
                                           {0.9f, 0.85f, 0.2f}, {0.3f, 0.8f, 0.3f}, {0.3f, 0.5f, 0.95f}};
    for (int v = 0; v < 4; ++v) {
        for (size_t i = 0; i + 2 < gun.tris.size(); i += 3) {
            const float* p = &gun.pos[gun.tris[i] * 3];
            if (std::fabs(p[2] - z0) > 0.12f || gun.part[gun.tris[i]] == kMagazineBone)
                continue;
            DrawTriangle(img, v * kTile, 0, views[v], middle, scale, p, &gun.pos[gun.tris[i + 1] * 3],
                         &gun.pos[gun.tris[i + 2] * 3], kGunColour);
        }
        for (size_t i = 0; i + 2 < gloveTris.size(); i += 3)
            DrawTriangle(img, v * kTile, 0, views[v], middle, scale, &placed[gloveTris[i] * 3],
                         &placed[gloveTris[i + 1] * 3], &placed[gloveTris[i + 2] * 3],
                         kFingerColour[glove[gloveTris[i]].finger + 1]);
    }
    img.Save(argv[2]);
    std::printf("saved %s\n", argv[2]);
    return 0;
}
