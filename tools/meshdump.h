// The game's meshes from the mod's dumps (weapon.txt "dump", the hands' dump), for the tools that
// fit the left glove to the rifle and build its collision shape without the game: loading the gun
// (posed in its model space: x left, y up, z to the muzzle) and the left glove (in its bind space,
// posed by src/handpose.cpp as the mod does), the gun as a solid of voxels, and drawing both into a
// .bmp from four sides. Header only; each tool includes it once.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../src/handpose.h"

namespace meshdump {

constexpr int kGloveFirst = 15051, kGloveCount = 13755, kGloveOffset = 186288, kStride = 48;
// The hand's axes in the gloves' bind space (handpose.cpp).
constexpr float kAlong[3] = {0.437f, -0.544f, 0.716f};
constexpr float kAcross[3] = {0.633f, -0.380f, -0.675f};
constexpr float kPalm[3] = {-0.639f, -0.748f, -0.179f};
// Each glove bone's finger: 0 thumb .. 4 little, -1 the palm.
constexpr int8_t kFinger[handpose::kBones] = {-1, -1, 0, 4, -1, -1, 2, 2, 1, 1, 3, 3, 3, 4, 4, 1, 4, 0, 0, 1, 3, 2, 2};
// The gun's bones: 0 the charging handle, 3 the ejection port's cover, 4 the magazine.
constexpr int kMagazineBone = 4;

inline float Half(uint16_t h) {
    int s = (h >> 15) & 1, e = (h >> 10) & 0x1F, m = h & 0x3FF;
    float v = e == 0 ? std::ldexp(m / 1024.0f, -14) : std::ldexp(1 + m / 1024.0f, e - 15);
    return s ? -v : v;
}

inline std::vector<uint8_t> ReadAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

inline float Dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline void Cross(const float* a, const float* b, float* o) {
    o[0] = a[1] * b[2] - a[2] * b[1], o[1] = a[2] * b[0] - a[0] * b[2], o[2] = a[0] * b[1] - a[1] * b[0];
}
inline void Normalize(float* v) {
    const float n = std::sqrt(Dot(v, v));
    if (n > 1e-9f)
        v[0] /= n, v[1] /= n, v[2] /= n;
}
// m: 3 rows of rotation | translation.
inline void Apply(const float* m, const float* p, float* o) {
    for (int k = 0; k < 3; ++k)
        o[k] = m[k * 4] * p[0] + m[k * 4 + 1] * p[1] + m[k * 4 + 2] * p[2] + m[k * 4 + 3];
}

struct Box {
    float lo[3], hi[3];
    const char* name;
};
// How deep p is inside the box (metres; <= 0 outside).
inline float Inside(const Box& b, const float* p) {
    float d = 1e9f;
    for (int k = 0; k < 3; ++k)
        d = std::min(d, std::min(p[k] - b.lo[k], b.hi[k] - p[k]));
    return d;
}
// The distance from p to the box (0 inside).
inline float Outside(const Box& b, const float* p) {
    float s = 0.0f;
    for (int k = 0; k < 3; ++k) {
        const float e = std::max(std::max(b.lo[k] - p[k], 0.0f), p[k] - b.hi[k]);
        s += e * e;
    }
    return std::sqrt(s);
}

struct Mesh {
    std::vector<float> pos;   // xyz per vertex
    std::vector<int> part;    // the gun's bone with the most weight
    std::vector<int> tris;
};

// The gun's colour pass, posed and put in its body's (bone 2's) frame.
inline bool LoadGun(const std::string& dir, Mesh& gun) {
    auto vb = ReadAll(dir + "\\gun_vb0.bin"), ib = ReadAll(dir + "\\gun_ib.bin"), bones = ReadAll(dir + "\\gun_bones.bin");
    if (vb.empty() || ib.size() < 12768 * 2 || bones.size() < 6 * 48)
        return false;
    const float* b = reinterpret_cast<const float*>(bones.data());
    const float* body = b + 2 * 12;
    // The body's inverse (a rotation with a translation).
    float inv[12];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            inv[i * 4 + j] = body[j * 4 + i];
    for (int i = 0; i < 3; ++i)
        inv[i * 4 + 3] = -(inv[i * 4] * body[3] + inv[i * 4 + 1] * body[7] + inv[i * 4 + 2] * body[11]);
    const size_t n = vb.size() / kStride;
    gun.pos.resize(n * 3);
    gun.part.resize(n);
    for (size_t v = 0; v < n; ++v) {
        const uint8_t* o = &vb[v * kStride];
        uint16_t h[3];
        std::memcpy(h, o, 6);
        const float p[3] = {Half(h[0]), Half(h[1]), Half(h[2])};
        float sum[3] = {}, wsum = 0.0f;
        int main = 0, mainWeight = -1;
        for (int s = 0; s < 4; ++s) {
            const int bone = o[8 + s], w = o[12 + s];
            if (!w || bone > 5)
                continue;
            float q[3];
            Apply(b + bone * 12, p, q);
            for (int k = 0; k < 3; ++k)
                sum[k] += q[k] * w;
            wsum += static_cast<float>(w);
            if (w > mainWeight)
                mainWeight = w, main = bone;
        }
        if (wsum > 0)
            for (int k = 0; k < 3; ++k)
                sum[k] /= wsum;
        Apply(inv, sum, &gun.pos[v * 3]);
        gun.part[v] = main;
    }
    for (int i = 0; i + 2 < 12768; i += 3)
        for (int c = 0; c < 3; ++c) {
            uint16_t v;
            std::memcpy(&v, &ib[2 * (i + c)], 2);
            gun.tris.push_back(v);
        }
    return true;
}

struct GloveVertex {
    float pos[3];
    int bone[4];
    float weight[4];
    int finger;  // -1 the palm, 0 the thumb .. 4 the little finger
};

inline bool LoadGlove(const std::string& dir, std::vector<GloveVertex>& verts, std::vector<int>& tris) {
    auto vb = ReadAll(dir + "\\mesh_13755_15051_vb0.bin"), ib = ReadAll(dir + "\\mesh_13755_15051_ib.bin");
    if (vb.size() < kGloveOffset || ib.size() < (kGloveFirst + kGloveCount) * 2u)
        return false;
    std::vector<int> index(65536, -1);
    for (int j = kGloveFirst; j + 2 < kGloveFirst + kGloveCount; j += 3) {
        int corner[3];
        bool left = true;
        for (int c = 0; c < 3; ++c) {
            uint16_t v;
            std::memcpy(&v, &ib[2 * (j + c)], 2);
            const uint8_t* o = &vb[kGloveOffset + v * kStride];
            if (index[v] < 0) {
                GloveVertex x{};
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
                x.finger = x.bone[best] < handpose::kBones ? kFinger[x.bone[best]] : -2;
                index[v] = static_cast<int>(verts.size());
                verts.push_back(x);
            }
            for (int s = 0; s < 4; ++s)
                if (verts[index[v]].weight[s] > 0 && verts[index[v]].bone[s] >= handpose::kBones)
                    left = false;
            corner[c] = index[v];
        }
        if (left)
            tris.insert(tris.end(), corner, corner + 3);
    }
    return true;
}

// The glove's vertices posed (handpose::Pose) in its bind space, xyz each.
inline std::vector<float> PoseGlove(const std::vector<GloveVertex>& glove, const handpose::Fingers& fingers,
                                    float width) {
    float bones[handpose::kBones][12];
    handpose::Pose(fingers, width, bones);
    std::vector<float> posed(glove.size() * 3);
    for (size_t i = 0; i < glove.size(); ++i) {
        float sum[3] = {};
        for (int s = 0; s < 4; ++s) {
            if (glove[i].weight[s] <= 0 || glove[i].bone[s] >= handpose::kBones)
                continue;
            float p[3];
            Apply(bones[glove[i].bone[s]], glove[i].pos, p);
            for (int k = 0; k < 3; ++k)
                sum[k] += glove[i].weight[s] * p[k];
        }
        std::memcpy(&posed[i * 3], sum, sizeof(sum));
    }
    return posed;
}

// The gun as solid voxels: its triangles' surface, then everything the outside air can't reach
// through the grid (the inside), each voxel with its depth in steps from the air (0 air, 1 the
// surface, more inside). Parts can be left out (the magazine, which comes out).
struct Solid {
    float origin[3] = {}, cell = 0.001f;
    int n[3] = {};
    std::vector<int16_t> depth;

    size_t At(int x, int y, int z) const { return (static_cast<size_t>(z) * n[1] + y) * n[0] + x; }
    bool Cell(const float* p, int& x, int& y, int& z) const {
        x = static_cast<int>(std::floor((p[0] - origin[0]) / cell));
        y = static_cast<int>(std::floor((p[1] - origin[1]) / cell));
        z = static_cast<int>(std::floor((p[2] - origin[2]) / cell));
        return x >= 0 && x < n[0] && y >= 0 && y < n[1] && z >= 0 && z < n[2];
    }
    // How deep p is in the gun (metres; the surface voxel's own cell doesn't count).
    float Depth(const float* p) const {
        int x, y, z;
        if (!Cell(p, x, y, z))
            return 0.0f;
        const int d = depth[At(x, y, z)];
        return d > 1 ? (d - 1) * cell : 0.0f;
    }
    bool Filled(int x, int y, int z) const { return depth[At(x, y, z)] > 0; }

    void Build(const Mesh& gun, const float* lo, const float* hi, float size, int leaveOut = kMagazineBone) {
        cell = size;
        for (int k = 0; k < 3; ++k) {
            origin[k] = lo[k];
            n[k] = static_cast<int>(std::ceil((hi[k] - lo[k]) / size));
        }
        depth.assign(static_cast<size_t>(n[0]) * n[1] * n[2], -1);
        std::vector<uint8_t> surface(depth.size(), 0);
        for (size_t t = 0; t + 2 < gun.tris.size(); t += 3) {
            if (gun.part[gun.tris[t]] == leaveOut)
                continue;
            const float* a = &gun.pos[gun.tris[t] * 3];
            const float* b = &gun.pos[gun.tris[t + 1] * 3];
            const float* c = &gun.pos[gun.tris[t + 2] * 3];
            float span = 0.0f;
            for (int k = 0; k < 3; ++k)
                span = std::max({span, std::fabs(b[k] - a[k]), std::fabs(c[k] - a[k]), std::fabs(c[k] - b[k])});
            const int steps = std::max(1, static_cast<int>(span / (cell * 0.4f)));
            for (int i = 0; i <= steps; ++i)
                for (int j = 0; i + j <= steps; ++j) {
                    float p[3];
                    for (int k = 0; k < 3; ++k)
                        p[k] = a[k] + (b[k] - a[k]) * i / steps + (c[k] - a[k]) * j / steps;
                    int x, y, z;
                    if (Cell(p, x, y, z))
                        surface[At(x, y, z)] = 1;
                }
        }
        // The air: from the grid's faces, through anything that isn't surface.
        std::vector<size_t> queue;
        for (int z = 0; z < n[2]; ++z)
            for (int y = 0; y < n[1]; ++y)
                for (int x = 0; x < n[0]; ++x)
                    if ((x == 0 || y == 0 || z == 0 || x == n[0] - 1 || y == n[1] - 1 || z == n[2] - 1) &&
                        !surface[At(x, y, z)]) {
                        depth[At(x, y, z)] = 0;
                        queue.push_back(At(x, y, z));
                    }
        const int step[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        auto spread = [&](bool air) {
            for (size_t q = 0; q < queue.size(); ++q) {
                const size_t i = queue[q];
                const int x = static_cast<int>(i % n[0]), y = static_cast<int>(i / n[0] % n[1]),
                          z = static_cast<int>(i / n[0] / n[1]);
                for (const auto& s : step) {
                    const int nx = x + s[0], ny = y + s[1], nz = z + s[2];
                    if (nx < 0 || ny < 0 || nz < 0 || nx >= n[0] || ny >= n[1] || nz >= n[2])
                        continue;
                    const size_t m = At(nx, ny, nz);
                    if (depth[m] != -1 || (air && surface[m]))
                        continue;
                    depth[m] = static_cast<int16_t>(air ? 0 : depth[i] + 1);
                    queue.push_back(m);
                }
            }
        };
        spread(true);
        // The solid's depth: steps from the air.
        queue.clear();
        for (size_t i = 0; i < depth.size(); ++i)
            if (depth[i] == 0)
                queue.push_back(i);
        spread(false);
    }
};

// Distances from the surface voxels: the nearest surface voxel found by spreading out from all of
// them at once, each voxel taking a neighbour's nearest when it is nearer (close to exact).
inline std::vector<float> DistanceField(const Solid& solid) {
    const size_t count = solid.depth.size();
    std::vector<int16_t> site(count * 3, -1);
    std::vector<float> distance(count, 1e9f);
    std::vector<size_t> queue;
    for (size_t i = 0; i < count; ++i)
        if (solid.depth[i] == 1) {
            const int x = static_cast<int>(i % solid.n[0]), y = static_cast<int>(i / solid.n[0] % solid.n[1]),
                      z = static_cast<int>(i / solid.n[0] / solid.n[1]);
            site[i * 3] = static_cast<int16_t>(x), site[i * 3 + 1] = static_cast<int16_t>(y),
            site[i * 3 + 2] = static_cast<int16_t>(z);
            distance[i] = 0.0f;
            queue.push_back(i);
        }
    for (size_t q = 0; q < queue.size(); ++q) {
        const size_t i = queue[q];
        const int x = static_cast<int>(i % solid.n[0]), y = static_cast<int>(i / solid.n[0] % solid.n[1]),
                  z = static_cast<int>(i / solid.n[0] / solid.n[1]);
        const int sx = site[i * 3], sy = site[i * 3 + 1], sz = site[i * 3 + 2];
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy, nz = z + dz;
                    if ((!dx && !dy && !dz) || nx < 0 || ny < 0 || nz < 0 || nx >= solid.n[0] || ny >= solid.n[1] ||
                        nz >= solid.n[2])
                        continue;
                    const size_t m = solid.At(nx, ny, nz);
                    const float ex = static_cast<float>(nx - sx), ey = static_cast<float>(ny - sy),
                                ez = static_cast<float>(nz - sz);
                    const float d = std::sqrt(ex * ex + ey * ey + ez * ez);
                    if (d + 1e-4f < distance[m]) {
                        distance[m] = d;
                        site[m * 3] = static_cast<int16_t>(sx), site[m * 3 + 1] = static_cast<int16_t>(sy),
                        site[m * 3 + 2] = static_cast<int16_t>(sz);
                        queue.push_back(m);
                    }
                }
    }
    // In metres, negative inside (the surface voxels half a cell in).
    for (size_t i = 0; i < count; ++i) {
        float d = distance[i] * solid.cell;
        if (solid.depth[i] >= 1)
            d = -d - 0.5f * solid.cell;
        distance[i] = d;
    }
    return distance;
}

// ---- Drawing ----
constexpr int kTile = 400;
struct Image {
    int w, h;
    std::vector<float> rgb, depth;
    Image(int w, int h) : w(w), h(h), rgb(w * h * 3, 0.12f), depth(w * h, 1e9f) {}
    void Save(const char* path) const {
        const int row = (w * 3 + 3) & ~3;
        uint32_t size = 54 + row * h, off = 54, info = 40, planes = 1 | (24 << 16), zero = 0;
        uint8_t header[54] = {'B', 'M'};
        std::memcpy(header + 2, &size, 4), std::memcpy(header + 10, &off, 4), std::memcpy(header + 14, &info, 4);
        std::memcpy(header + 18, &w, 4), std::memcpy(header + 22, &h, 4), std::memcpy(header + 26, &planes, 4);
        std::memcpy(header + 30, &zero, 4);
        FILE* f = nullptr;
        if (fopen_s(&f, path, "wb") || !f)
            return;
        fwrite(header, 1, 54, f);
        std::vector<uint8_t> line(row, 0);
        for (int y = h - 1; y >= 0; --y) {
            for (int x = 0; x < w; ++x)
                for (int c = 0; c < 3; ++c)
                    line[x * 3 + c] = static_cast<uint8_t>(std::clamp(rgb[(y * w + x) * 3 + (2 - c)], 0.0f, 1.0f) * 255);
            fwrite(line.data(), 1, row, f);
        }
        fclose(f);
    }
};
struct View {
    float forward[3], up[3], right[3];
};
inline View MakeView(float fx, float fy, float fz, float ux, float uy, float uz) {
    View v{{fx, fy, fz}, {ux, uy, uz}, {}};
    Normalize(v.forward);
    const float d = Dot(v.up, v.forward);
    for (int i = 0; i < 3; ++i)
        v.up[i] -= d * v.forward[i];
    Normalize(v.up);
    Cross(v.forward, v.up, v.right);
    return v;
}
// scale: pixels a metre.
inline void DrawTriangle(Image& img, int tx, int ty, const View& view, const float* centre, float scale, const float* a,
                         const float* b, const float* c, const float* colour) {
    const float* p[3] = {a, b, c};
    float sx[3], sy[3], sz[3];
    for (int i = 0; i < 3; ++i) {
        const float d[3] = {p[i][0] - centre[0], p[i][1] - centre[1], p[i][2] - centre[2]};
        sx[i] = tx + kTile / 2 + Dot(d, view.right) * scale;
        sy[i] = ty + kTile / 2 - Dot(d, view.up) * scale;
        sz[i] = Dot(d, view.forward);
    }
    float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]}, nrm[3];
    Cross(e1, e2, nrm);
    Normalize(nrm);
    float light[3];
    for (int i = 0; i < 3; ++i)
        light[i] = -view.forward[i] * 0.7f + view.up[i] * 0.6f + view.right[i] * 0.3f;
    Normalize(light);
    const float shade = 0.25f + 0.55f * std::fabs(Dot(nrm, light)) + 0.2f * std::fabs(Dot(nrm, view.forward));
    const int x0 = std::max(tx, static_cast<int>(std::floor(std::min({sx[0], sx[1], sx[2]}))));
    const int x1 = std::min(tx + kTile - 1, static_cast<int>(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
    const int y0 = std::max(ty, static_cast<int>(std::floor(std::min({sy[0], sy[1], sy[2]}))));
    const int y1 = std::min(ty + kTile - 1, static_cast<int>(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
    const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (std::fabs(area) < 1e-6f)
        return;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area;
            const float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area;
            const float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;
            const float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
            float& depth = img.depth[y * img.w + x];
            if (z >= depth)
                continue;
            depth = z;
            for (int k = 0; k < 3; ++k)
                img.rgb[(y * img.w + x) * 3 + k] = colour[k] * shade;
        }
}

}
