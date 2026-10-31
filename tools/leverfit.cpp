// Fits the left glove onto the rifle's charging handle without the game: the glove (the gloves'
// mesh dump, posed by src/handpose.cpp as the mod does) palm down, fingers forward over the handle's
// T bar (the gun's mesh dump, posed in the gun's model space: x left, y up, z to the muzzle), the
// index and middle fingers hooked round the bar. Searches where to put the hand: as many index and
// middle finger vertices as possible on the bar's top and front faces, none inside the bar, the
// shaft, or the receiver and stock under it. Prints the placement for hands.cpp and draws the fit
// from four sides into a .bmp.
// Usage: leverfit <dump folder> <out.bmp> [curl index,middle,ring,little,thumb,spread] [w=width]
//                 [tilt=degrees] [at=x,y,z]


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
float g_scale = kTile / 0.16f;  // pixels a metre: 16 cm across a tile
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: leverfit <dump folder> <out.bmp> [curl i,m,r,l,thumb,spread] [w=width] [tilt=deg] [at=x,y,z]\n");
        return 1;
    }
    const std::string dir = argv[1];
    handpose::Fingers fingers;
    fingers.curl[0] = fingers.curl[1] = 0.62f;
    fingers.curl[2] = fingers.curl[3] = 0.9f;
    fingers.thumb = 0.35f;
    fingers.spread = 0.05f;
    float width = 1.15f, tilt = 0.0f;
    bool fixedAt = false, close = false;
    float window = 0.006f, stepSize = 0.0005f;
    float at[3] = {};
    for (int i = 3; i < argc; ++i) {
        if (!std::strncmp(argv[i], "w=", 2))
            width = static_cast<float>(atof(argv[i] + 2));
        else if (!std::strncmp(argv[i], "tilt=", 5))
            tilt = static_cast<float>(atof(argv[i] + 5));
        else if (!std::strncmp(argv[i], "apart=", 6))
            sscanf_s(argv[i] + 6, "%f,%f", &fingers.apart[0], &fingers.apart[1]);
        else if (!std::strncmp(argv[i], "win=", 4))
            window = static_cast<float>(atof(argv[i] + 4));
        else if (!std::strncmp(argv[i], "step=", 5))
            stepSize = static_cast<float>(atof(argv[i] + 5));
        else if (!std::strncmp(argv[i], "close=", 6))
            close = atoi(argv[i] + 6) != 0;
        else if (!std::strncmp(argv[i], "at=", 3))
            fixedAt = sscanf_s(argv[i] + 3, "%f,%f,%f", &at[0], &at[1], &at[2]) == 3;
        else
            sscanf_s(argv[i], "%f,%f,%f,%f,%f,%f", &fingers.curl[0], &fingers.curl[1], &fingers.curl[2], &fingers.curl[3],
                     &fingers.thumb, &fingers.spread);
    }

    Mesh gun;
    std::vector<GloveVertex> glove;
    std::vector<int> gloveTris;
    if (!LoadGun(dir, gun) || !LoadGlove(dir, glove, gloveTris)) {
        fprintf(stderr, "the gun's or the gloves' mesh dump isn't in %s\n", dir.c_str());
        return 1;
    }

    // The handle's T bar: bone 0's vertices up at the receiver's top, behind its shaft's latch.
    Box bar{{1, 1, 1}, {-1, -1, -1}, "the bar"};
    for (size_t v = 0; v < gun.part.size(); ++v) {
        const float* p = &gun.pos[v * 3];
        if (gun.part[v] != 0 || p[1] < 0.05f || p[2] < 0.24f || p[2] > 0.285f)
            continue;
        for (int k = 0; k < 3; ++k)
            bar.lo[k] = std::min(bar.lo[k], p[k]), bar.hi[k] = std::max(bar.hi[k], p[k]);
    }
    printf("the T bar: x %.4f..%.4f, y %.4f..%.4f, z %.4f..%.4f\n", bar.lo[0], bar.hi[0], bar.lo[1], bar.hi[1],
           bar.lo[2], bar.hi[2]);
    // Its shaft and latch just in front of it, which go between the index and middle fingers (the
    // user: it went through them).
    Box shaft{{1, 1, 1}, {-1, -1, -1}, "the shaft"};
    for (size_t v = 0; v < gun.part.size(); ++v) {
        const float* p = &gun.pos[v * 3];
        if (gun.part[v] != 0 || p[1] < 0.05f || p[2] <= bar.hi[2] || p[2] > bar.hi[2] + 0.04f)
            continue;
        for (int k = 0; k < 3; ++k)
            shaft.lo[k] = std::min(shaft.lo[k], p[k]), shaft.hi[k] = std::max(shaft.hi[k], p[k]);
    }
    printf("the shaft and latch: x %.4f..%.4f, y %.4f..%.4f, z %.4f..%.4f\n", shaft.lo[0], shaft.hi[0], shaft.lo[1],
           shaft.hi[1], shaft.lo[2], shaft.hi[2]);
    // The gun near the handle (all of it but the magazine) as solid voxels, 0.5 mm.
    const std::vector<Box> solid{bar, shaft};
    Solid body;
    {
        const float lo[3] = {-0.05f, -0.03f, 0.15f}, hi[3] = {0.05f, 0.12f, 0.35f};
        body.Build(gun, lo, hi, 0.0005f);
    }
    auto inBody = [&](const float* p) { return body.Depth(p); };

    // The posed glove in its bind space.
    const std::vector<float> posed = PoseGlove(glove, fingers, width);
    // Palm down, fingers forward: along -> +z, across (thumb to little finger) -> +x, the back of the
    // hand -> +y; then pitched down by tilt degrees (fingers lower) about the gun's x.
    const float back[3] = {-kPalm[0], -kPalm[1], -kPalm[2]};
    float r[9];
    for (int j = 0; j < 3; ++j)
        r[0 * 3 + j] = kAcross[j], r[1 * 3 + j] = back[j], r[2 * 3 + j] = kAlong[j];
    const float t = tilt * 3.14159265f / 180.0f, c = std::cos(t), s = std::sin(t);
    float rotation[9];
    for (int j = 0; j < 3; ++j) {
        rotation[0 * 3 + j] = r[0 * 3 + j];
        rotation[1 * 3 + j] = c * r[1 * 3 + j] + s * r[2 * 3 + j];
        rotation[2 * 3 + j] = -s * r[1 * 3 + j] + c * r[2 * 3 + j];
    }
    std::vector<float> turned(posed.size());
    for (size_t i = 0; i < glove.size(); ++i)
        for (int k = 0; k < 3; ++k)
            turned[i * 3 + k] = rotation[k * 3] * posed[i * 3] + rotation[k * 3 + 1] * posed[i * 3 + 1] +
                                rotation[k * 3 + 2] * posed[i * 3 + 2];

    // Where to put it: the bar inside the curled index and middle fingers. Each curled finger is a
    // ring round a hole (its vertices' centre is the hole's); the two rings' centres go to the bar's
    // centre, the index on the shaft's right and the middle on its left. Then, every 0.5 mm within
    // 6 mm, the most of their vertices within 2 mm of the bar's sides (top, front, bottom, back:
    // round it) and nothing inside the gun.
    struct Fit {
        int faces[4];  // touching the bar's top, front, bottom, back
        float sink;
        const char* where;
        int finger;  // the deepest vertex's (-1 the palm, 0 the thumb ... 4 the little finger)
    };
    auto measure = [&](const float* offset) {
        Fit f{{}, 0.0f, "", -1};
        for (size_t i = 0; i < glove.size(); ++i) {
            const float p[3] = {turned[i * 3] + offset[0], turned[i * 3 + 1] + offset[1], turned[i * 3 + 2] + offset[2]};
            for (const Box& b : solid) {
                const float in = Inside(b, p);
                if (in > f.sink)
                    f.sink = in, f.where = b.name, f.finger = glove[i].finger;
            }
            if (const float in = inBody(p); in > f.sink)
                f.sink = in, f.where = "the gun", f.finger = glove[i].finger;
            if ((glove[i].finger != 1 && glove[i].finger != 2) || Outside(bar, p) > 0.002f || Inside(bar, p) > 0)
                continue;
            if (p[1] >= bar.hi[1] - 0.0005f)
                ++f.faces[0];
            else if (p[2] >= bar.hi[2] - 0.0005f)
                ++f.faces[1];
            else if (p[1] <= bar.lo[1] + 0.0005f)
                ++f.faces[2];
            else if (p[2] <= bar.lo[2] + 0.0005f)
                ++f.faces[3];
        }
        return f;
    };
    auto score = [](const Fit& f) {
        const int sides = (f.faces[0] > 0) + (f.faces[1] > 0) + (f.faces[2] > 0) + (f.faces[3] > 0);
        return sides * 100.0f + f.faces[0] + f.faces[1] + f.faces[2] + f.faces[3] - std::max(f.sink - 0.001f, 0.0f) * 2000000.0f;
    };
    float best[3] = {};
    if (fixedAt) {
        std::memcpy(best, at, sizeof(best));
    } else {
        float rings[2][3] = {}, counts[2] = {};
        for (size_t i = 0; i < glove.size(); ++i) {
            const int f = glove[i].finger - 1;
            if (f != 0 && f != 1)
                continue;
            for (int k = 0; k < 3; ++k)
                rings[f][k] += turned[i * 3 + k];
            ++counts[f];
        }
        float guess[3];
        for (int k = 0; k < 3; ++k)
            guess[k] = (bar.lo[k] + bar.hi[k]) / 2 - (rings[0][k] / counts[0] + rings[1][k] / counts[1]) / 2;
        printf("the rings' centres %.1f mm apart across\n",
               std::fabs(rings[0][0] / counts[0] - rings[1][0] / counts[1]) * 1000);
        float bestScore = -1e9f;
        for (float dx = -window; dx <= window; dx += stepSize)
            for (float dy = -window; dy <= window; dy += stepSize)
                for (float dz = -window; dz <= window; dz += stepSize) {
                    const float o[3] = {guess[0] + dx, guess[1] + dy, guess[2] + dz};
                    const float sc = score(measure(o));
                    if (sc > bestScore)
                        bestScore = sc, std::memcpy(best, o, sizeof(best));
                }
    }
    const Fit fit = measure(best);
    printf("curl index %.2f middle %.2f ring %.2f little %.2f thumb %.2f spread %.2f, width %.2f, tilt %.0f\n",
           fingers.curl[0], fingers.curl[1], fingers.curl[2], fingers.curl[3], fingers.thumb, fingers.spread, width, tilt);
    static const char* const kNames[6] = {"the palm", "the thumb", "the index finger", "the middle finger",
                                          "the ring finger", "the little finger"};
    printf("at %.4f,%.4f,%.4f: index/middle vertices on the bar's top %d, front %d, bottom %d, back %d; deepest: %s "
           "%.1f mm in %s\n",
           best[0], best[1], best[2], fit.faces[0], fit.faces[1], fit.faces[2], fit.faces[3],
           fit.sink > 0 ? kNames[std::clamp(fit.finger + 1, 0, 5)] : "nothing", fit.sink * 1000, fit.where);
    // The placement for hands.cpp: the glove's bind space -> the gun's model space (rows).
    printf("placement: {%.5ff, %.5ff, %.5ff, %.5ff, %.5ff, %.5ff, %.5ff, %.5ff, %.5ff, %.5ff, %.5ff, %.5ff}\n",
           rotation[0], rotation[1], rotation[2], best[0], rotation[3], rotation[4], rotation[5], best[1], rotation[6],
           rotation[7], rotation[8], best[2]);

    // Four views round the bar: from the left side, from above, from behind (as the shooter), from
    // in front and to the left. close=1: 6 cm across, only the handle's end and the index and middle
    // fingers, from the left, the right, above and behind, to see them round the bar.
    if (close)
        g_scale = kTile / 0.06f;
    Image img(kTile * 4, kTile);
    const View wide[4] = {MakeView(-1, 0, 0, 0, 1, 0), MakeView(0, -1, 0, 0, 0, 1), MakeView(0, -0.3f, 1, 0, 1, 0),
                          MakeView(0.6f, -0.4f, -1, 0, 1, 0)};
    const View near[4] = {MakeView(-1, 0, 0, 0, 1, 0), MakeView(1, 0, 0, 0, 1, 0), MakeView(0, -1, 0, 0, 0, 1),
                          MakeView(0, -0.2f, 1, 0, 1, 0)};
    const View* views = close ? near : wide;
    const float centre[3] = {(bar.lo[0] + bar.hi[0]) / 2, bar.hi[1], (bar.lo[2] + bar.hi[2]) / 2};
    constexpr float kGunColour[3] = {0.55f, 0.55f, 0.6f}, kHandle[3] = {0.85f, 0.35f, 0.85f};
    constexpr float kFingerColour[6][3] = {{0.6f, 0.5f, 0.4f}, {0.9f, 0.25f, 0.2f}, {0.95f, 0.55f, 0.15f},
                                           {0.9f, 0.85f, 0.2f}, {0.3f, 0.8f, 0.3f}, {0.3f, 0.5f, 0.95f}};
    for (int v = 0; v < 4; ++v) {
        for (size_t i = 0; i + 2 < gun.tris.size(); i += 3) {
            const float* a = &gun.pos[gun.tris[i] * 3];
            if (std::fabs(a[2] - centre[2]) > (close ? 0.04f : 0.12f) || (close && a[1] < 0.03f))
                continue;
            DrawTriangle(img, v * kTile, 0, views[v], centre, g_scale, a, &gun.pos[gun.tris[i + 1] * 3], &gun.pos[gun.tris[i + 2] * 3],
                         gun.part[gun.tris[i]] == 0 ? kHandle : kGunColour);
        }
        for (size_t i = 0; i + 2 < gloveTris.size(); i += 3) {
            const int finger = glove[gloveTris[i]].finger;
            if (close && finger != 1 && finger != 2)
                continue;
            float p[3][3];
            for (int k = 0; k < 3; ++k)
                for (int j = 0; j < 3; ++j)
                    p[k][j] = turned[gloveTris[i + k] * 3 + j] + best[j];
            DrawTriangle(img, v * kTile, 0, views[v], centre, g_scale, p[0], p[1], p[2], kFingerColour[finger + 1]);
        }
    }
    img.Save(argv[2]);
    printf("saved %s\n", argv[2]);
    return 0;
}
