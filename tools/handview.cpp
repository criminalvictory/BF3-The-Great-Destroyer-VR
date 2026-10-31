// Draws the left glove from the gloves' mesh dump (weapon.txt "dump" writes build\mesh_13755_15051_*)
// with src/handpose.cpp's finger poses, each pose a row seen from four sides (the back of the hand,
// the palm, the thumb's side, from behind the wrist as in first person), into one .bmp: the poses
// can be shaped and checked without the game. Coloured by finger: thumb red, index orange, middle
// yellow, ring green, little blue, palm grey.
// Usage: handview <dump folder> <out.bmp> [w=width] [z=zoom] [pose ...], a pose as
// "index,middle,ring,little,thumb,spread" (default: relaxed, spread, half closed, fist).
// "hand=r" draws the right glove instead (the bones 23-44), as made with its trigger finger bent:
// each "trigger=degrees" a row, the index finger bent that much by handpose::BendRightIndex
// (default 0, 20 and 45); "game=1" bends it from the game's own grip instead, as the gloves' depth
// pass was drawn when dumped (build\mesh_7617_49995_bones.bin).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "../src/handpose.h"

namespace {

constexpr int kFirst = 15051, kCount = 13755, kOffset = 186288, kStride = 48;  // the gloves' draw
constexpr int kPalette = 60;  // the gloves' draw's bones (both hands', by their numbers there)
constexpr int kTile = 320;
float g_scale = kTile / 0.30f;  // pixels a metre
int g_cull = 0;  // "c=1" / "c=-1": skip triangles wound one way on screen, as the game culls their backs
bool g_analyse = false;  // "a=1": list the glove's separate pieces and openings
bool g_patch = true;     // "p=0": without skin.cpp's patch over the gloves' cracks
bool g_right = false;    // "hand=r": the right glove, its trigger finger bent
bool g_game = false;     // "game=1": ... from the game's own grip (the gloves' depth pass's dumped bones)
// The hand's axes and middle in the bind space (as in handpose.cpp).
constexpr float kAlong[3] = {0.437f, -0.544f, 0.716f};
constexpr float kAcross[3] = {0.633f, -0.380f, -0.675f};
constexpr float kPalm[3] = {-0.639f, -0.748f, -0.179f};
constexpr float kMiddle[3] = {0.537f, 1.098f, 0.207f};
// ... and the right hand's: the left's mirrored (x the other way), as the right glove is.
constexpr float kRightAlong[3] = {-0.437f, -0.544f, 0.716f};
constexpr float kRightAcross[3] = {-0.633f, -0.380f, -0.675f};
constexpr float kRightPalm[3] = {0.639f, -0.748f, -0.179f};
constexpr float kRightMiddle[3] = {-0.537f, 1.098f, 0.207f};
float g_centre[3] = {kMiddle[0], kMiddle[1], kMiddle[2]};  // what the views look at
// Each bone's finger, for the colours (as in handpose.cpp).
constexpr int8_t kFinger[handpose::kBones] = {-1, -1, 0, 4, -1, -1, 2, 2, 1, 1, 3, 3, 3, 4, 4, 1, 4, 0, 0, 1, 3, 2, 2};
constexpr float kColour[6][3] = {{0.6f, 0.6f, 0.6f}, {0.9f, 0.25f, 0.2f}, {0.95f, 0.55f, 0.15f},
                                 {0.9f, 0.85f, 0.2f}, {0.3f, 0.8f, 0.3f}, {0.3f, 0.5f, 0.95f}};

// A bone's finger in the glove drawn (0 the thumb ... 4 the little finger, -1 the palm), or -2: a
// bone of the other hand.
int FingerOf(int bone) {
    if (g_right)
        return bone >= handpose::kRightFirst && bone < handpose::kRightFirst + handpose::kRightBones
                   ? handpose::kRightFinger[bone - handpose::kRightFirst]
                   : -2;
    return bone >= 0 && bone < handpose::kBones ? kFinger[bone] : -2;
}

float Half(uint16_t h) {
    int s = (h >> 15) & 1, e = (h >> 10) & 0x1F, m = h & 0x3FF;
    float v = e == 0 ? std::ldexp(m / 1024.0f, -14) : std::ldexp(1 + m / 1024.0f, e - 15);
    return s ? -v : v;
}

std::vector<uint8_t> ReadAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

struct Vertex {
    float pos[3];
    int bone[4];
    float weight[4];
    int finger;  // of its main bone
};

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

float Dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// Each finger's bones from the palm out (thumb, index ... little), the segments between handpose's
// joints; the right glove's as handpose.h's kRightFinger and kRightDepth have them.
constexpr int kSegmentBone[5][3] = {{18, 17, 2}, {9, 19, 15}, {7, 21, 22}, {11, 20, 10}, {13, 16, 3}};
constexpr int kRightSegmentBone[5][3] = {{35, 36, 39}, {26, 38, 31}, {25, 42, 43}, {27, 41, 40}, {29, 34, 33}};
const char* const kFingerName[5] = {"thumb", "index", "middle", "ring", "little"};

void Apply(const float* m, const float* p, float* out) {
    for (int k = 0; k < 3; ++k)
        out[k] = m[k * 4] * p[0] + m[k * 4 + 1] * p[1] + m[k * 4 + 2] * p[2] + m[k * 4 + 3];
}

// Affine 3x4 matrices (three rows: rotation | translation): a after b, and a turn with a shift undone.
void Compose(const float* a, const float* b, float* out) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j)
            out[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j];
        out[i * 4 + 3] += a[i * 4 + 3];
    }
}

void InvertRigid(const float* m, float* out) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out[i * 4 + j] = m[j * 4 + i];
    for (int i = 0; i < 3; ++i)
        out[i * 4 + 3] = -(out[i * 4] * m[3] + out[i * 4 + 1] * m[7] + out[i * 4 + 2] * m[11]);
}

// The closest distance between segments p1-q1 and p2-q2.
float SegmentDistance(const float* p1, const float* q1, const float* p2, const float* q2) {
    float d1[3], d2[3], r[3];
    for (int i = 0; i < 3; ++i)
        d1[i] = q1[i] - p1[i], d2[i] = q2[i] - p2[i], r[i] = p1[i] - p2[i];
    float a = Dot(d1, d1), e = Dot(d2, d2), f = Dot(d2, r), c = Dot(d1, r), b = Dot(d1, d2);
    float s, t;
    float denom = a * e - b * b;
    s = denom > 1e-12f ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
    t = (b * s + f) / e;
    if (t < 0) {
        t = 0, s = std::clamp(-c / a, 0.0f, 1.0f);
    } else if (t > 1) {
        t = 1, s = std::clamp((b - c) / a, 0.0f, 1.0f);
    }
    float d[3];
    for (int i = 0; i < 3; ++i)
        d[i] = (p1[i] + d1[i] * s) - (p2[i] + d2[i] * t);
    return std::sqrt(Dot(d, d));
}

struct Image {
    int w, h;
    std::vector<float> rgb, depth;
    Image(int w, int h) : w(w), h(h), rgb(w * h * 3, 0.12f), depth(w * h, 1e9f) {}
    void Save(const char* path) const {
        int row = (w * 3 + 3) & ~3;
        uint32_t size = 54 + row * h;
        uint8_t header[54] = {'B', 'M'};
        std::memcpy(header + 2, &size, 4);
        uint32_t off = 54, info = 40, planes = 1 | (24 << 16), zero = 0;
        std::memcpy(header + 10, &off, 4);
        std::memcpy(header + 14, &info, 4);
        std::memcpy(header + 18, &w, 4);
        std::memcpy(header + 22, &h, 4);
        std::memcpy(header + 26, &planes, 4);
        std::memcpy(header + 30, &zero, 4);
        FILE* f = nullptr;
        if (fopen_s(&f, path, "wb") != 0 || !f)
            return;
        fwrite(header, 1, 54, f);
        std::vector<uint8_t> line(row, 0);
        for (int y = h - 1; y >= 0; --y) {
            for (int x = 0; x < w; ++x)
                for (int c = 0; c < 3; ++c) {
                    float v = rgb[(y * w + x) * 3 + (2 - c)];
                    line[x * 3 + c] = static_cast<uint8_t>(std::fmin(std::fmax(v, 0.0f), 1.0f) * 255);
                }
            fwrite(line.data(), 1, row, f);
        }
        fclose(f);
    }
};

// One view: the camera looks along forward, with up; a tile at (tx, ty).
struct View {
    float forward[3], up[3], right[3];
};

View MakeView(const float* forward, const float* upHint) {
    View v;
    std::memcpy(v.forward, forward, sizeof(v.forward));
    Normalize(v.forward);
    float d = Dot(upHint, v.forward);
    for (int i = 0; i < 3; ++i)
        v.up[i] = upHint[i] - d * v.forward[i];
    Normalize(v.up);
    Cross(v.forward, v.up, v.right);
    return v;
}

void DrawTriangle(Image& img, int tx, int ty, const View& view, const float* a, const float* b, const float* c,
                  const float* colour) {
    const float* p[3] = {a, b, c};
    float sx[3], sy[3], sz[3];
    for (int i = 0; i < 3; ++i) {
        float d[3] = {p[i][0] - g_centre[0], p[i][1] - g_centre[1], p[i][2] - g_centre[2]};
        sx[i] = tx + kTile / 2 + Dot(d, view.right) * g_scale;
        sy[i] = ty + kTile / 2 - Dot(d, view.up) * g_scale;
        sz[i] = Dot(d, view.forward);
    }
    float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]}, n[3];
    Cross(e1, e2, n);
    Normalize(n);
    float light[3];
    for (int i = 0; i < 3; ++i)
        light[i] = -view.forward[i] * 0.7f + view.up[i] * 0.6f + view.right[i] * 0.3f;
    Normalize(light);
    float shade = 0.25f + 0.55f * std::fabs(Dot(n, light)) + 0.2f * std::fabs(Dot(n, view.forward));
    int x0 = std::max(tx, static_cast<int>(std::floor(std::fmin(sx[0], std::fmin(sx[1], sx[2])))));
    int x1 = std::min(tx + kTile - 1, static_cast<int>(std::ceil(std::fmax(sx[0], std::fmax(sx[1], sx[2])))));
    int y0 = std::max(ty, static_cast<int>(std::floor(std::fmin(sy[0], std::fmin(sy[1], sy[2])))));
    int y1 = std::min(ty + kTile - 1, static_cast<int>(std::ceil(std::fmax(sy[0], std::fmax(sy[1], sy[2])))));
    float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (std::fabs(area) < 1e-6f || area * g_cull < 0)
        return;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
            float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;
            float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
            float& depth = img.depth[y * img.w + x];
            if (z >= depth)
                continue;
            depth = z;
            for (int k = 0; k < 3; ++k)
                img.rgb[(y * img.w + x) * 3 + k] = colour[k] * shade;
        }
}

// The gloves' depth passes (their 16-byte vertices; each bone mapped to the gloves' as skin.cpp's
// kGloveDepthLeft and kBodyDepthLeft, and handpose.h's kGloveDepthRight and kBodyDepthRight): drawn
// first, they hide the glove's colour wherever their surface stands out of the glove's, which shows
// as a hole.
struct DepthDraw {
    int count, first, offset;
    std::vector<int8_t> left, right;
};
const DepthDraw kDepthDraws[] = {
    {7617, 49995, 406096, {-1, -1, 14, 13, 16, -1, 8, 9, 19, 15, 18, 17, 2},
     {std::begin(handpose::kGloveDepthRight), std::end(handpose::kGloveDepthRight)}},
    {21189, 28806, 344880, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 4,
                            0,  1,  6,  7,  21, 22, 12, 11, 20, 10, 14, 13, 16, 3,  8,  9,  19, 15, 18, 17, 2},
     {std::begin(handpose::kBodyDepthRight), std::end(handpose::kBodyDepthRight)}},
};

std::vector<Vertex> LoadDepthVertices(const std::string& dir) {
    std::vector<Vertex> out;
    for (const DepthDraw& d : kDepthDraws) {
        char name[64];
        snprintf(name, sizeof(name), "\\mesh_%d_%d_", d.count, d.first);
        auto vb = ReadAll(dir + name + "vb0.bin"), ib = ReadAll(dir + name + "ib.bin");
        if (ib.size() < (d.first + d.count) * 2u)
            continue;
        const std::vector<int8_t>& map = g_right ? d.right : d.left;
        std::vector<bool> seen(65536);
        for (int j = d.first; j < d.first + d.count; ++j) {
            uint16_t v;
            std::memcpy(&v, &ib[2 * j], 2);
            if (seen[v])
                continue;
            seen[v] = true;
            const uint8_t* o = &vb[d.offset + v * 16];
            Vertex x{};
            uint16_t h[3];
            std::memcpy(h, o, 6);
            bool mine = true;  // only on this hand's bones
            for (int k = 0; k < 3; ++k)
                x.pos[k] = Half(h[k]);
            for (int s = 0; s < 4; ++s) {
                x.weight[s] = o[12 + s] / 255.0f;
                int b = o[8 + s];
                x.bone[s] = b < static_cast<int>(map.size()) ? map[b] : -1;
                if (x.weight[s] > 0 && x.bone[s] < 0)
                    mine = false;
            }
            if (mine)
                out.push_back(x);
        }
    }
    return out;
}

void Skin(const Vertex& x, float (*bones)[12], float* out) {
    out[0] = out[1] = out[2] = 0;
    for (int s = 0; s < 4; ++s) {
        if (x.weight[s] <= 0 || x.bone[s] < 0 || x.bone[s] >= kPalette)
            continue;
        float p[3];
        Apply(bones[x.bone[s]], x.pos, p);
        for (int k = 0; k < 3; ++k)
            out[k] += x.weight[s] * p[k];
    }
}

// How far each depth vertex stands out of the glove's surface (along the nearest glove vertex's
// normal, by its triangles' winding), in mm.
std::vector<float> DepthOut(const std::vector<Vertex>& depth, float (*bones)[12], const std::vector<float>& posed,
                            const std::vector<int>& tris) {
    std::vector<float> normal(posed.size(), 0.0f);
    for (size_t t = 0; t < tris.size(); t += 3) {
        const float *a = &posed[tris[t] * 3], *b = &posed[tris[t + 1] * 3], *c = &posed[tris[t + 2] * 3];
        float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]}, n[3];
        Cross(e1, e2, n);
        for (int k = 0; k < 3; ++k)
            for (int i = 0; i < 3; ++i)
                normal[tris[t + k] * 3 + i] += n[i];
    }
    for (size_t i = 0; i < normal.size(); i += 3)
        Normalize(&normal[i]);
    std::vector<float> out;
    for (const Vertex& x : depth) {
        float p[3];
        Skin(x, bones, p);
        float best = 1e9f;
        size_t nearest = 0;
        for (size_t i = 0; i < posed.size(); i += 3) {
            float d[3] = {p[0] - posed[i], p[1] - posed[i + 1], p[2] - posed[i + 2]};
            float d2 = Dot(d, d);
            if (d2 < best && Dot(&normal[i], &normal[i]) > 0.5f)
                best = d2, nearest = i;
        }
        float d[3] = {p[0] - posed[nearest], p[1] - posed[nearest + 1], p[2] - posed[nearest + 2]};
        out.push_back(Dot(d, &normal[nearest]) * 1000);
    }
    return out;
}

}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: handview <dump folder> <out.bmp> [width] [index,middle,ring,little,thumb,spread ...]\n"
                        "       handview <dump folder> <out.bmp> hand=r [game=1] [trigger=degrees ...]\n");
        return 1;
    }
    std::string dir = argv[1];
    auto vb = ReadAll(dir + "\\mesh_13755_15051_vb0.bin"), ib = ReadAll(dir + "\\mesh_13755_15051_ib.bin");
    if (vb.size() < kOffset || ib.size() < (kFirst + kCount) * 2u) {
        fprintf(stderr, "no gloves' mesh dump in %s\n", dir.c_str());
        return 1;
    }
    float width = 1.0f, offset = 0.0f, aside = 0.0f;
    std::vector<handpose::Fingers> poses;
    std::vector<float> triggers;  // the right glove's rows: its index finger bent this far (degrees)
    for (int i = 3; i < argc; ++i) {
        if (std::strncmp(argv[i], "w=", 2) == 0) {
            width = static_cast<float>(atof(argv[i] + 2));
        } else if (std::strncmp(argv[i], "z=", 2) == 0) {
            g_scale *= static_cast<float>(atof(argv[i] + 2));
        } else if (std::strncmp(argv[i], "c=", 2) == 0) {
            g_cull = atoi(argv[i] + 2);
        } else if (std::strncmp(argv[i], "a=", 2) == 0) {
            g_analyse = atoi(argv[i] + 2) != 0;
        } else if (std::strncmp(argv[i], "p=", 2) == 0) {
            g_patch = atoi(argv[i] + 2) != 0;
        } else if (std::strncmp(argv[i], "o=", 2) == 0) {  // the view's centre, this far along the fingers
            offset = static_cast<float>(atof(argv[i] + 2));
        } else if (std::strncmp(argv[i], "s=", 2) == 0) {  // ... and this far across towards the little finger
            aside = static_cast<float>(atof(argv[i] + 2));
        } else if (std::strncmp(argv[i], "hand=", 5) == 0) {
            g_right = argv[i][5] == 'r' || argv[i][5] == 'R';
        } else if (std::strncmp(argv[i], "trigger=", 8) == 0) {
            triggers.push_back(static_cast<float>(atof(argv[i] + 8)));
        } else if (std::strncmp(argv[i], "game=", 5) == 0) {
            g_game = atoi(argv[i] + 5) != 0;
        } else if (const char* colon = std::strchr(argv[i], ':')) {  // handpose::Tune: "name:1,2,3"
            std::string name(argv[i], static_cast<size_t>(colon - argv[i]));
            float v[4];
            int n = sscanf_s(colon + 1, "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]);
            handpose::Tune(name.c_str(), v, n);
        } else {
            handpose::Fingers f;
            sscanf_s(argv[i], "%f,%f,%f,%f,%f,%f", &f.curl[0], &f.curl[1], &f.curl[2], &f.curl[3], &f.thumb,
                     &f.spread);
            poses.push_back(f);
        }
    }
    if (poses.empty()) {
        handpose::Fingers relaxed, spread, half, fist;
        relaxed.spread = 0.4f;
        spread.spread = 1.0f;
        for (int k = 0; k < 4; ++k)
            half.curl[k] = 0.5f, fist.curl[k] = 1.0f;
        half.thumb = 0.5f, fist.thumb = 1.0f;
        poses = {relaxed, spread, half, fist};
    }
    if (triggers.empty())
        triggers = {0.0f, 20.0f, 45.0f};
    // The hand's axes and middle, the view's centre moved along the fingers ("o=") and across ("s=").
    const float* along = g_right ? kRightAlong : kAlong;
    const float* across = g_right ? kRightAcross : kAcross;
    const float* palmOut = g_right ? kRightPalm : kPalm;
    const float* middle = g_right ? kRightMiddle : kMiddle;
    for (int k = 0; k < 3; ++k)
        g_centre[k] = middle[k] + along[k] * offset + across[k] * aside;
    // The hand's triangles: every corner weighted only to its bones.
    std::vector<Vertex> verts;
    std::vector<int> tris;
    std::vector<int> index(65536, -1);
    for (int j = kFirst; j + 2 < kFirst + kCount; j += 3) {
        int corner[3];
        bool mine = true;
        for (int c = 0; c < 3; ++c) {
            uint16_t v;
            std::memcpy(&v, &ib[2 * (j + c)], 2);
            const uint8_t* o = &vb[kOffset + v * kStride];
            if (index[v] < 0) {
                Vertex x{};
                uint16_t h[3];
                std::memcpy(h, o, 6);
                int best = 0;
                for (int k = 0; k < 3; ++k)
                    x.pos[k] = Half(h[k]);
                for (int s = 0; s < 4; ++s) {
                    x.bone[s] = o[8 + s], x.weight[s] = o[12 + s] / 255.0f;
                    if (x.weight[s] > x.weight[best])
                        best = s;
                }
                x.finger = FingerOf(x.bone[best]);
                index[v] = static_cast<int>(verts.size());
                verts.push_back(x);
            }
            const Vertex& x = verts[index[v]];
            for (int s = 0; s < 4; ++s)
                if (x.weight[s] > 0 && FingerOf(x.bone[s]) < -1)
                    mine = false;
            corner[c] = index[v];
        }
        if (mine)
            tris.insert(tris.end(), corner, corner + 3);
    }
    printf("%zu triangles of the %s hand\n", tris.size() / 3, g_right ? "right" : "left");
    // skin.cpp's patch over the gloves' three cracks, as the game draws it ("p=0": without). They are
    // the left glove's: the right is drawn as the game holds the gun, only its trigger finger bent.
    if (g_patch && !g_right) {
        constexpr uint16_t kPatch[] = {7, 8, 1723, 7, 1723, 8, 9, 11, 1597, 9, 1597, 11, 13, 14, 1393, 13, 1393, 14};
        for (uint16_t v : kPatch)
            tris.push_back(index[v]);
    }
    // The mesh's open edges (one triangle only, corners welded by position): drawn cyan.
    std::vector<bool> openEdge(tris.size() / 3);
    std::vector<int> weld(verts.size());  // each corner's first at its place
    {
        std::map<std::tuple<float, float, float>, int> at;
        for (size_t i = 0; i < verts.size(); ++i)
            weld[i] = at.emplace(std::make_tuple(verts[i].pos[0], verts[i].pos[1], verts[i].pos[2]),
                                 static_cast<int>(i))
                          .first->second;
        std::map<std::pair<int, int>, int> edges;
        for (size_t t = 0; t < tris.size(); t += 3)
            for (int e = 0; e < 3; ++e) {
                int a = weld[tris[t + e]], b = weld[tris[t + (e + 1) % 3]];
                ++edges[{std::min(a, b), std::max(a, b)}];
            }
        int open = 0;
        for (size_t t = 0; t < tris.size(); t += 3)
            for (int e = 0; e < 3; ++e) {
                int a = weld[tris[t + e]], b = weld[tris[t + (e + 1) % 3]];
                if (edges[{std::min(a, b), std::max(a, b)}] == 1)
                    openEdge[t / 3] = true, ++open;
            }
        printf("%d open edges\n", open);
        if (g_analyse) {
            // Pieces: triangles joined by shared corners; openings: open edges joined end to end.
            std::vector<int> parent(verts.size());
            for (size_t i = 0; i < parent.size(); ++i)
                parent[i] = static_cast<int>(i);
            auto find = [&](int i) {
                while (parent[i] != i)
                    i = parent[i] = parent[parent[i]];
                return i;
            };
            for (size_t t = 0; t < tris.size(); t += 3)
                for (int e = 1; e < 3; ++e)
                    parent[find(weld[tris[t + e]])] = find(weld[tris[t]]);
            std::vector<int> loop(verts.size());
            for (size_t i = 0; i < loop.size(); ++i)
                loop[i] = static_cast<int>(i);
            auto findLoop = [&](int i) {
                while (loop[i] != i)
                    i = loop[i] = loop[loop[i]];
                return i;
            };
            std::vector<bool> onOpen(verts.size());
            for (const auto& [edge, n] : edges)
                if (n == 1) {
                    loop[findLoop(edge.first)] = findLoop(edge.second);
                    onOpen[edge.first] = onOpen[edge.second] = true;
                }
            // Where a set of corners sits (cm from the hand's middle: along the fingers, across to the
            // little finger, out of the palm) and its main bones.
            auto describe = [&](const std::vector<int>& members) {
                float c[3] = {};
                std::map<int, float> bones;
                for (int i : members) {
                    for (int k = 0; k < 3; ++k)
                        c[k] += verts[i].pos[k] / members.size();
                    for (int s = 0; s < 4; ++s)
                        if (verts[i].weight[s] > 0)
                            bones[verts[i].bone[s]] += verts[i].weight[s];
                }
                float d[3] = {c[0] - middle[0], c[1] - middle[1], c[2] - middle[2]};
                std::vector<std::pair<float, int>> top;
                for (const auto& [b, w] : bones)
                    top.push_back({w, b});
                std::sort(top.rbegin(), top.rend());
                printf("at along %5.1f across %5.1f palm %5.1f; bones", Dot(d, along) * 100, Dot(d, across) * 100,
                       Dot(d, palmOut) * 100);
                for (size_t k = 0; k < top.size() && k < 4; ++k)
                    printf(" %d(%.0f%%)", top[k].second, 100 * top[k].first / members.size());
                printf("\n");
            };
            std::vector<bool> used(verts.size());
            for (int i : tris)
                used[weld[i]] = true;
            std::map<int, std::vector<int>> pieces, openings;
            for (size_t i = 0; i < verts.size(); ++i) {
                if (weld[i] != static_cast<int>(i) || !used[i])
                    continue;
                pieces[find(static_cast<int>(i))].push_back(static_cast<int>(i));
                if (onOpen[i])
                    openings[findLoop(static_cast<int>(i))].push_back(static_cast<int>(i));
            }
            printf("%zu pieces:\n", pieces.size());
            for (const auto& [id, members] : pieces) {
                printf("  piece of %3zu corners ", members.size());
                describe(members);
            }
            printf("%zu openings:\n", openings.size());
            for (const auto& [id, members] : openings) {
                printf("  opening of %2zu corners in the piece of %3zu, ", members.size(), pieces[find(id)].size());
                describe(members);
                if (members.size() > 4)
                    continue;
                // A small one: its corners (their index in the draw, mm, bones), and any other corner
                // within 2 mm of each.
                for (int i : members) {
                    const Vertex& x = verts[i];
                    int raw = static_cast<int>(std::find(index.begin(), index.end(), i) - index.begin());
                    printf("    corner %d (vertex %d of the buffer) at (%.1f %.1f %.1f) mm, bones", i, raw,
                           x.pos[0] * 1000, x.pos[1] * 1000, x.pos[2] * 1000);
                    for (int s = 0; s < 4; ++s)
                        if (x.weight[s] > 0)
                            printf(" %d:%.2f", x.bone[s], x.weight[s]);
                    printf("; near");
                    for (size_t j = 0; j < verts.size(); ++j) {
                        float d[3] = {verts[j].pos[0] - x.pos[0], verts[j].pos[1] - x.pos[1], verts[j].pos[2] - x.pos[2]};
                        if (static_cast<int>(j) != i && used[weld[j]] && weld[j] != weld[i] && Dot(d, d) < 4e-6f)
                            printf(" %zu (%.1f mm)", j, std::sqrt(Dot(d, d)) * 1000);
                    }
                    printf("\n");
                }
            }
        }
    }
    // First person: the hand held out as on a controller, the palm sideways and the thumb on top,
    // seen from behind and above.
    float firstPerson[3], thumbUp[3];
    for (int i = 0; i < 3; ++i)
        firstPerson[i] = along[i] * 0.55f + across[i] * 0.75f + palmOut[i] * 0.35f, thumbUp[i] = -across[i];
    const float back[3] = {palmOut[0], palmOut[1], palmOut[2]}, palm[3] = {-palmOut[0], -palmOut[1], -palmOut[2]},
                side[3] = {across[0], across[1], across[2]};
    const View views[4] = {MakeView(back, along), MakeView(palm, along), MakeView(side, along),
                           MakeView(firstPerson, thumbUp)};
    const size_t rows = g_right ? triggers.size() : poses.size();
    Image img(kTile * 4, kTile * static_cast<int>(rows));
    std::vector<float> skinned(verts.size() * 3);
    std::vector<bool> drawn(verts.size());  // the corners of the hand's triangles
    for (int i : tris)
        drawn[i] = true;
    // Where the right glove's rows start: as made, or ("game=1") as the game last drew it, each of
    // the gloves' depth pass's dumped bones put on the gloves' bone handpose.h's kGloveDepthRight
    // gives it (which shows that map too: a wrong one tears the glove apart), its palm (32) kept
    // where the mesh has it, so the views see the hand as they see it made.
    float rowStart[kPalette][12] = {};
    for (auto& m : rowStart)
        m[0] = m[5] = m[10] = 1;
    if (g_right && g_game) {
        auto dumped = ReadAll(dir + "\\mesh_7617_49995_bones.bin");
        const float* p = reinterpret_cast<const float*>(dumped.data());
        const int8_t* map = handpose::kGloveDepthRight;
        const int entries = static_cast<int>(std::size(handpose::kGloveDepthRight));
        int palmEntry = -1;
        for (int e = 0; e < entries; ++e)
            if (map[e] == 32)
                palmEntry = e;
        if (palmEntry < 0 || dumped.size() < entries * 48u) {
            fprintf(stderr, "no gloves' depth pass bones in %s\n", dir.c_str());
            return 1;
        }
        float inverse[12];
        InvertRigid(p + palmEntry * 12, inverse);
        for (int e = 0; e < entries; ++e)
            if (map[e] >= 0)
                Compose(inverse, p + e * 12, rowStart[map[e]]);
    }
    // Clipping: each finger segment as a capsule along its joints, as thick as half its vertices
    // reach, posed; how deep any vertex of one finger sinks into another's capsules (mm; below zero,
    // the gap between them), against the mesh as made. The webbing between the fingers' first
    // segments (their first 35%) and the thumb's palm bone aren't counted.
    float joints[5][4][3];
    if (g_right)
        handpose::RightJoints(joints);
    else
        handpose::Joints(joints);
    const int(*segmentBone)[3] = g_right ? kRightSegmentBone : kSegmentBone;
    std::vector<int> mainBone(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        int best = 0;
        for (int s = 1; s < 4; ++s)
            if (verts[i].weight[s] > verts[i].weight[best])
                best = s;
        mainBone[i] = verts[i].bone[best];
    }
    std::vector<float> sunk(verts.size());  // how deep each vertex is in another finger (drawn magenta)
    auto overlaps = [&](float (*bones)[12], const std::vector<float>& posed, const char* name) {
        std::fill(sunk.begin(), sunk.end(), 0.0f);
        float a[5][3][3], b[5][3][3], radius[5][3];
        std::vector<std::pair<int, size_t>> owned[5];  // each finger's counted vertices (segment, index)
        for (int f = 0; f < 5; ++f)
            for (int k = 0; k < 3; ++k) {
                const int bone = segmentBone[f][k];
                float start[3], end[3];
                Apply(bones[bone], joints[f][k], start);
                Apply(bones[bone], joints[f][k + 1], end);
                const float from = f > 0 && k == 0 ? 0.35f : 0.0f;
                for (int i = 0; i < 3; ++i)
                    a[f][k][i] = start[i] + (end[i] - start[i]) * from, b[f][k][i] = end[i];
                std::vector<float> d;
                float axis[3] = {end[0] - start[0], end[1] - start[1], end[2] - start[2]};
                float length2 = Dot(axis, axis);
                for (size_t i = 0; i < verts.size(); ++i) {
                    if (mainBone[i] != bone)
                        continue;
                    float r[3] = {posed[i * 3] - start[0], posed[i * 3 + 1] - start[1], posed[i * 3 + 2] - start[2]};
                    if (Dot(r, axis) < from * length2)
                        continue;
                    d.push_back(SegmentDistance(&posed[i * 3], &posed[i * 3], a[f][k], b[f][k]));
                    if (!(f == 0 && k == 0))
                        owned[f].push_back({k, i});
                }
                std::sort(d.begin(), d.end());
                radius[f][k] = d.empty() ? 0.0f : d[d.size() / 2];
            }
        printf("%-28s", name);
        for (int f = 0; f < 5; ++f)
            for (int g = f + 1; g < 5; ++g) {
                float worst = -1e9f;
                int where[2] = {};  // the segments: the sinking vertex's, the capsule's
                for (int pass = 0; pass < 2; ++pass) {
                    const int from = pass ? g : f, into = pass ? f : g;
                    for (const auto& [k, i] : owned[from])
                        for (int m = into == 0 ? 1 : 0; m < 3; ++m) {
                            float depth = radius[into][m] -
                                          SegmentDistance(&posed[i * 3], &posed[i * 3], a[into][m], b[into][m]);
                            if (depth > worst)
                                worst = depth, where[0] = pass ? m : k, where[1] = pass ? k : m;
                            sunk[i] = std::max(sunk[i], depth);
                        }
                }
                if (g == f + 1 || f == 0)
                    printf(" %s-%s %+5.1f(%d%d)", kFingerName[f], kFingerName[g], worst * 1000, where[0], where[1]);
            }
        printf("\n");
    };
    // The depth passes against the glove: how much further out of it each of their vertices stands
    // than as made (mm), and on which bones; and those made at a glove corner's place, how far from
    // it they are posed (their bones mapped right: not at all).
    const std::vector<Vertex> depthVerts = LoadDepthVertices(dir);
    std::vector<float> depthBind;
    std::vector<int> depthCorner(depthVerts.size(), -1);  // the glove corner at each one's place, if any
    {
        std::map<std::tuple<float, float, float>, int> corner;
        for (size_t i = 0; i < verts.size(); ++i)
            if (drawn[i])
                corner.emplace(std::make_tuple(verts[i].pos[0], verts[i].pos[1], verts[i].pos[2]), static_cast<int>(i));
        for (size_t i = 0; i < depthVerts.size(); ++i) {
            auto it = corner.find(std::make_tuple(depthVerts[i].pos[0], depthVerts[i].pos[1], depthVerts[i].pos[2]));
            if (it != corner.end())
                depthCorner[i] = it->second;
        }
    }
    auto depthReport = [&](float (*bones)[12], const std::vector<float>& posed) {
        std::vector<float> out = DepthOut(depthVerts, bones, posed, tris);
        float worst = 0, apart = 0;
        int count = 0, atCorner = 0, byBone[kPalette] = {};
        for (size_t i = 0; i < out.size(); ++i) {
            float more = out[i] - depthBind[i];
            worst = std::max(worst, more);
            if (more > 0.5f) {
                ++count;
                int best = 0;
                for (int s = 1; s < 4; ++s)
                    if (depthVerts[i].weight[s] > depthVerts[i].weight[best])
                        best = s;
                ++byBone[depthVerts[i].bone[best]];
            }
            if (depthCorner[i] >= 0) {
                float p[3];
                Skin(depthVerts[i], bones, p);
                const float* q = &posed[depthCorner[i] * 3];
                const float d[3] = {p[0] - q[0], p[1] - q[1], p[2] - q[2]};
                apart = std::max(apart, std::sqrt(Dot(d, d)));
                ++atCorner;
            }
        }
        printf("    depth passes: %d of %zu vertices over 0.5 mm further out, up to %.1f mm; bones", count, out.size(),
               worst);
        for (int b = 0; b < kPalette; ++b)
            if (byBone[b])
                printf(" %d:%d", b, byBone[b]);
        printf("; the %d at a glove corner's place at most %.2f mm from it\n", atCorner, apart * 1000);
    };
    {
        float identity[kPalette][12] = {};
        for (auto& m : identity)
            m[0] = m[5] = m[10] = 1;
        std::vector<float> bind(verts.size() * 3);
        for (size_t i = 0; i < verts.size(); ++i)
            std::memcpy(&bind[i * 3], verts[i].pos, sizeof(verts[i].pos));
        overlaps(identity, bind, "as made");
        depthBind = DepthOut(depthVerts, identity, bind, tris);
        float most = -1e9f, least = 1e9f;
        for (float d : depthBind)
            most = std::max(most, d), least = std::min(least, d);
        printf("    depth passes: %zu vertices of the %s hand, %.1f to %.1f mm out of the glove as made\n",
               depthBind.size(), g_right ? "right" : "left", least, most);
    }
    // Every vertex moved by bones.
    auto skinAll = [&](float (*bones)[12], std::vector<float>& out) {
        for (size_t i = 0; i < verts.size(); ++i) {
            const Vertex& x = verts[i];
            float p[3] = {};
            for (int s = 0; s < 4; ++s) {
                if (x.weight[s] <= 0 || x.bone[s] >= kPalette)
                    continue;
                const float* m = bones[x.bone[s]];
                for (int k = 0; k < 3; ++k)
                    p[k] += x.weight[s] * (m[k * 4] * x.pos[0] + m[k * 4 + 1] * x.pos[1] + m[k * 4 + 2] * x.pos[2] +
                                           m[k * 4 + 3]);
            }
            std::memcpy(&out[i * 3], p, sizeof(p));
        }
    };
    std::vector<float> unbent(verts.size() * 3);  // the right glove where its rows start
    skinAll(rowStart, unbent);
    for (size_t row = 0; row < rows; ++row) {
        float bones[kPalette][12];
        std::memcpy(bones, rowStart, sizeof(bones));
        char name[64];
        if (g_right) {
            // Its trigger finger pulled, as skin.cpp is to pull it where the game draws it.
            handpose::BendRightIndex(bones, triggers[row]);
            snprintf(name, sizeof(name), "trigger %g degrees%s", triggers[row], g_game ? " (game)" : "");
        } else {
            handpose::Pose(poses[row], width, bones);
            const handpose::Fingers& p = poses[row];
            snprintf(name, sizeof(name), "%.1f,%.1f,%.1f,%.1f,%.1f,%.1f", p.curl[0], p.curl[1], p.curl[2], p.curl[3],
                     p.thumb, p.spread);
        }
        skinAll(bones, skinned);
        overlaps(bones, skinned, name);
        depthReport(bones, skinned);
        // Tearing: corners made at one place pulled apart (their weights differ).
        float torn = 0.0f;
        for (size_t i = 0; i < verts.size(); ++i) {
            if (!drawn[i])
                continue;
            const float *p = &skinned[i * 3], *q = &skinned[weld[i] * 3];
            const float d[3] = {p[0] - q[0], p[1] - q[1], p[2] - q[2]};
            torn = std::max(torn, std::sqrt(Dot(d, d)));
        }
        printf("    seams: corners made at one place at most %.2f mm apart\n", torn * 1000);
        if (g_right) {
            // Only the trigger finger moves, curling towards the palm: how far each finger's vertices
            // went from where the row started, and where its tip went: towards the palm, back along
            // the finger (from its tip to its knuckle), across the hand towards the little finger,
            // and out of the plane it bends in (along handpose's axis, turned as its first bone is),
            // all as the hand is posed where the row started.
            float moved[6] = {};
            for (size_t i = 0; i < verts.size(); ++i) {
                if (!drawn[i])
                    continue;
                const float d[3] = {skinned[i * 3] - unbent[i * 3], skinned[i * 3 + 1] - unbent[i * 3 + 1],
                                    skinned[i * 3 + 2] - unbent[i * 3 + 2]};
                moved[verts[i].finger + 1] = std::max(moved[verts[i].finger + 1], std::sqrt(Dot(d, d)));
            }
            const int* bone = kRightSegmentBone[1];
            const float *first = rowStart[bone[0]], *second = rowStart[bone[1]];
            const float* palmBone = rowStart[24];  // the index finger's bone in the palm
            float axes[5][3], bent[3], towards[3], acrossHand[3], knuckle[3], from[3], to[3], d[3], back[3];
            handpose::RightAxes(axes);
            for (int k = 0; k < 3; ++k) {
                const float *r = first + k * 4, *q = palmBone + k * 4;
                bent[k] = r[0] * axes[1][0] + r[1] * axes[1][1] + r[2] * axes[1][2];
                towards[k] = q[0] * palmOut[0] + q[1] * palmOut[1] + q[2] * palmOut[2];
                acrossHand[k] = q[0] * across[0] + q[1] * across[1] + q[2] * across[2];
            }
            Normalize(bent);
            Normalize(towards);
            Normalize(acrossHand);
            Apply(first, joints[1][0], knuckle);
            Apply(rowStart[bone[2]], joints[1][3], from);
            Apply(bones[bone[2]], joints[1][3], to);
            for (int k = 0; k < 3; ++k)
                d[k] = to[k] - from[k], back[k] = knuckle[k] - from[k];
            Normalize(back);
            printf("    moved at most (mm): palm %.1f thumb %.1f index %.1f middle %.1f ring %.1f little %.1f\n"
                   "    the index tip (mm): %.1f towards the palm, %.1f back along the finger, %.1f across the hand, "
                   "%.1f out of its bend's plane\n",
                   moved[0] * 1000, moved[1] * 1000, moved[2] * 1000, moved[3] * 1000, moved[4] * 1000,
                   moved[5] * 1000, Dot(d, towards) * 1000, Dot(d, back) * 1000, Dot(d, acrossHand) * 1000,
                   Dot(d, bent) * 1000);
            // Its middle joint turned more than 10 degrees where the row starts (the game's grip): how
            // far handpose's axis is from the one that turn is about (the bones either side of the
            // joint, one undone from the other, in the bind space).
            float turn[9], about[3];
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    turn[i * 3 + j] = first[i] * second[j] + first[4 + i] * second[4 + j] + first[8 + i] * second[8 + j];
            about[0] = turn[7] - turn[5], about[1] = turn[2] - turn[6], about[2] = turn[3] - turn[1];
            const float angle = std::acos(std::clamp((turn[0] + turn[4] + turn[8] - 1) / 2, -1.0f, 1.0f));
            if (angle > 10.0f / 57.29578f) {
                Normalize(about);
                printf("    its middle joint turned %.0f degrees where it starts, about an axis %.1f degrees from "
                       "handpose's\n",
                       angle * 57.29578f, std::acos(std::min(1.0f, std::fabs(Dot(about, axes[1])))) * 57.29578f);
            }
        }
        constexpr float kMagenta[3] = {1.0f, 0.1f, 1.0f}, kCyan[3] = {0.1f, 1.0f, 1.0f};
        for (int v = 0; v < 4; ++v)
            for (size_t t = 0; t < tris.size(); t += 3) {
                const Vertex& a = verts[tris[t]];
                bool clipped = std::max({sunk[tris[t]], sunk[tris[t + 1]], sunk[tris[t + 2]]}) > 0.002f;
                DrawTriangle(img, v * kTile, static_cast<int>(row) * kTile, views[v], &skinned[tris[t] * 3],
                             &skinned[tris[t + 1] * 3], &skinned[tris[t + 2] * 3],
                             openEdge[t / 3] ? kCyan : clipped ? kMagenta : kColour[a.finger + 1]);
            }
    }
    img.Save(argv[2]);
    printf("saved %s\n", argv[2]);
    return 0;
}
