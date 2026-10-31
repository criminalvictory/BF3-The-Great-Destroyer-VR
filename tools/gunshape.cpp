// The rifle's collision shape for the mod, from the gun's mesh dump: a signed distance field over
// the gun in its model space (x left, y up, z to the muzzle), the metres from any point to the
// gun's surface, negative inside. The mod keeps the left glove out of the gun with it and feels the
// bolt release being slapped (src/gunshape.cpp). The magazine is left out (it comes out).
// Also measures the left glove for the mod: spheres along its palm and fingers, each on a glove
// bone, as source lines for src/hands.cpp.
// Usage: gunshape <dump folder> <out.bin> [cell metres, default 0.003]
// The file: "GSDF", version 2, the cells along x, y and z (int32 each), the origin (3 floats), the
// cell's size (float), then an int16 a cell (tenths of a millimetre, x fastest, then y, then z).
// (Version 1 had whole millimetres in a byte: too coarse, the hand pushed out of the gun jittered.)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "meshdump.h"

using namespace meshdump;

namespace {


// The glove's spheres, in its bind space (the mod poses them with the glove's bones): on each
// finger bone, its vertices' middle and how far they reach from the finger's line (its thickness);
// the palm as a grid of spheres whose fronts are the palm's skin.
void MeasureGlove(const std::vector<GloveVertex>& glove) {
    std::vector<float> posed(glove.size() * 3);
    for (size_t i = 0; i < glove.size(); ++i)
        std::memcpy(&posed[i * 3], glove[i].pos, sizeof(glove[i].pos));
    std::printf("// The left glove's collision spheres (tools/gunshape.cpp): bone, centre in the glove's bind space,\n"
                "// radius (metres).\n");
    // Fingers: each bone with most of a vertex's weight.
    for (int bone = 0; bone < handpose::kBones; ++bone) {
        if (kFinger[bone] < 0)
            continue;
        float sum[3] = {};
        int n = 0;
        for (size_t i = 0; i < glove.size(); ++i) {
            int best = 0;
            for (int s = 1; s < 4; ++s)
                if (glove[i].weight[s] > glove[i].weight[best])
                    best = s;
            if (glove[i].bone[best] != bone || glove[i].weight[best] < 0.5f)
                continue;
            for (int k = 0; k < 3; ++k)
                sum[k] += posed[i * 3 + k];
            ++n;
        }
        if (n < 8)
            continue;
        const float centre[3] = {sum[0] / n, sum[1] / n, sum[2] / n};
        // The thickness: the median distance from the centre, across the finger (off its along axis).
        std::vector<float> across;
        for (size_t i = 0; i < glove.size(); ++i) {
            int best = 0;
            for (int s = 1; s < 4; ++s)
                if (glove[i].weight[s] > glove[i].weight[best])
                    best = s;
            if (glove[i].bone[best] != bone || glove[i].weight[best] < 0.5f)
                continue;
            float d[3];
            for (int k = 0; k < 3; ++k)
                d[k] = posed[i * 3 + k] - centre[k];
            const float along = Dot(d, kAlong);
            for (int k = 0; k < 3; ++k)
                d[k] -= along * kAlong[k];
            across.push_back(std::sqrt(Dot(d, d)));
        }
        std::nth_element(across.begin(), across.begin() + across.size() / 2, across.end());
        std::printf("    {%d, {%.4ff, %.4ff, %.4ff}, %.4ff},  // finger %d, %d vertices\n", bone, centre[0], centre[1],
                    centre[2], across[across.size() / 2], kFinger[bone], n);
    }
    // The palm: its vertices (the palm's bones) along, across and through the hand, trimmed of the
    // wrist's cuff and the thumb's root (10th to 90th percentiles).
    std::vector<int> palm;
    for (size_t i = 0; i < glove.size(); ++i)
        if (glove[i].finger == -1)
            palm.push_back(static_cast<int>(i));
    auto range = [&](const float* axis, float low, float high, float& from, float& to) {
        std::vector<float> v;
        for (int i : palm)
            v.push_back(Dot(&posed[i * 3], axis));
        std::sort(v.begin(), v.end());
        from = v[static_cast<size_t>(v.size() * low)], to = v[static_cast<size_t>(v.size() * high)];
    };
    float along0, along1, across0, across1, back, front;
    range(kAlong, 0.1f, 0.9f, along0, along1);
    range(kAcross, 0.1f, 0.9f, across0, across1);
    range(kPalm, 0.1f, 0.9f, back, front);
    // Spheres 1.5 cm across at most, their fronts on the palm's skin (what slaps and holds).
    const float radius = std::min(0.015f, (front - back) / 2);
    const float through = front - radius;
    std::printf("    // the palm: along %.3f..%.3f, across %.3f..%.3f, through %.3f..%.3f\n", along0, along1, across0,
                across1, back, front);
    for (int u = 0; u < 3; ++u)
        for (int v = 0; v < 3; ++v) {
            const float a = along0 + (along1 - along0) * (0.15f + 0.35f * u);
            const float b = across0 + (across1 - across0) * (0.15f + 0.35f * v);
            float c[3];
            for (int k = 0; k < 3; ++k)
                c[k] = kAlong[k] * a + kAcross[k] * b + kPalm[k] * through;
            std::printf("    {0, {%.4ff, %.4ff, %.4ff}, %.4ff},  // the palm\n", c[0], c[1], c[2], radius);
        }
}

}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: gunshape <dump folder> <out.bin> [cell metres]\n");
        return 1;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string dir = argv[1];
    const float cell = argc > 3 ? static_cast<float>(atof(argv[3])) : 0.003f;
    Mesh gun;
    if (!LoadGun(dir, gun)) {
        std::fprintf(stderr, "the gun's mesh dump isn't in %s\n", dir.c_str());
        return 1;
    }
    // The vertices the colour pass draws (the buffer holds others, posed by other bones).
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (int v : gun.tris) {
        if (gun.part[v] == kMagazineBone)
            continue;
        for (int k = 0; k < 3; ++k)
            lo[k] = std::min(lo[k], gun.pos[v * 3 + k]), hi[k] = std::max(hi[k], gun.pos[v * 3 + k]);
    }
    std::printf("the gun (no magazine): x %.3f..%.3f, y %.3f..%.3f, z %.3f..%.3f\n", lo[0], hi[0], lo[1], hi[1], lo[2],
                hi[2]);
    // 2 mm voxels with 5 cm of air round the gun, then the field sampled every cell.
    constexpr float kMargin = 0.05f, kFine = 0.002f;
    float from[3], to[3];
    for (int k = 0; k < 3; ++k)
        from[k] = lo[k] - kMargin, to[k] = hi[k] + kMargin;
    Solid solid;
    solid.Build(gun, from, to, kFine);
    std::printf("solid: %d x %d x %d voxels of %.0f mm\n", solid.n[0], solid.n[1], solid.n[2], kFine * 1000);
    const std::vector<float> field = DistanceField(solid);

    int n[3];
    for (int k = 0; k < 3; ++k)
        n[k] = static_cast<int>(std::floor((to[k] - from[k]) / cell)) + 1;
    std::vector<int16_t> out(static_cast<size_t>(n[0]) * n[1] * n[2]);
    for (int z = 0; z < n[2]; ++z)
        for (int y = 0; y < n[1]; ++y)
            for (int x = 0; x < n[0]; ++x) {
                const float p[3] = {from[0] + x * cell, from[1] + y * cell, from[2] + z * cell};
                int vx, vy, vz;
                float d = kMargin;
                if (solid.Cell(p, vx, vy, vz))
                    d = field[solid.At(vx, vy, vz)];
                out[(static_cast<size_t>(z) * n[1] + y) * n[0] + x] =
                    static_cast<int16_t>(std::clamp(std::lround(d * 10000.0f), -32767l, 32767l));
            }
    FILE* f = nullptr;
    if (fopen_s(&f, argv[2], "wb") || !f) {
        std::fprintf(stderr, "can't write %s\n", argv[2]);
        return 1;
    }
    const int32_t version = 2;
    std::fwrite("GSDF", 1, 4, f);
    std::fwrite(&version, 4, 1, f);
    std::fwrite(n, 4, 3, f);
    std::fwrite(from, 4, 3, f);
    std::fwrite(&cell, 4, 1, f);
    std::fwrite(out.data(), 2, out.size(), f);
    std::fclose(f);
    std::printf("wrote %s: %d x %d x %d cells of %.0f mm from (%.3f, %.3f, %.3f), %zu bytes\n", argv[2], n[0], n[1],
                n[2], cell * 1000, from[0], from[1], from[2], out.size() * 2 + 32);
    // A few points to check by: the charging handle's T, beside the receiver's left, above it.
    const float checks[][3] = {{0.0f, 0.06f, 0.27f}, {0.06f, 0.02f, 0.37f}, {0.0f, 0.15f, 0.30f}, {0.0f, -0.03f, 0.55f}};
    for (const auto& p : checks) {
        int vx, vy, vz;
        const float d = solid.Cell(p, vx, vy, vz) ? field[solid.At(vx, vy, vz)] : kMargin;
        std::printf("  at (%.2f, %.2f, %.2f): %.1f mm\n", p[0], p[1], p[2], d * 1000);
    }

    std::vector<GloveVertex> glove;
    std::vector<int> gloveTris;
    if (LoadGlove(dir, glove, gloveTris))
        MeasureGlove(glove);
    return 0;
}
