// The free left glove kept out of the rifle (src/collide.cpp, as hands.cpp runs it) on scripted hand
// paths at 72 Hz, without the game: the rifle's collision shape (build\guns\m416.bin, only read),
// the glove posed open and relaxed by src/handpose.cpp as during a slap, the gun held still (all in
// its model space: x left, y up, z to the muzzle; metres). For each scenario:
//   pen   the deepest any of the glove's spheres goes into the gun (mm, by the shape itself)
//   thru  whether the glove ever ends up beyond the gun from where the hand came in
//   jerk  the most the glove moves in a frame beyond what the tracked hand moves (mm, any sphere)
//   rev   back-and-forth reversals of the correction (the glove's offset from the hand) in one
//         contact while the hand keeps moving the same way, or is still
//   slap  frames reload.cpp takes for a slap of the bolt release (a fresh touch in its zone, the
//         palm faster than 0.25 m/s); each slap into the zone needs one
//   ms    the solver's time a frame: the most, of each frame's fastest of seven runs (the game may be
//         running alongside)
// Pass: pen <= 2 mm, never through, jerk <= 3 mm, rev <= 1, the slap felt, <= 0.3 ms a frame.
// Usage: collidetest [m416.bin] [-v "scenario"] [probe]
//   -v prints the named scenario frame by frame; probe prints the gun's sides and the glove's touch.
// The exit code is the number of failed checks.

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "../src/collide.h"
#include "../src/gunshape.h"
#include "../src/handpose.h"

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDt = 1.0f / 72.0f;  // a frame at 72 Hz (13.9 ms)
// As hands.cpp holds the free glove: the fingers apart (g_spread), the palm drawn wider
// (g_leftWidth), and the palm point reload.cpp goes by (g_leftAnchor: where the arm folds into the
// glove, in the gloves' bind space).
constexpr float kSpread = 0.7f, kWidth = 1.15f;
constexpr float kAnchor[3] = {0.502f, 1.146f, 0.144f};
// reload.cpp's bolt release slap: the glove touching the receiver's left side (x), between these
// heights (y) and along it (z), a touch that wasn't there the frame before, the palm faster than this.
constexpr float kSlapSide = 0.005f;
constexpr float kSlapZone[4] = {-0.04f, 0.075f, 0.26f, 0.48f};
constexpr float kTouchSlapSpeed = 0.25f;
// The hand's axes in the gloves' bind space (handpose.cpp): along the fingers, out of the palm.
constexpr float kAlong[3] = {0.437f, -0.544f, 0.716f};
constexpr float kPalm[3] = {-0.639f, -0.748f, -0.179f};
// The pass marks.
constexpr float kMaxPen = 0.002f, kMaxJerk = 0.003f;
constexpr int kMaxRev = 1;
constexpr double kMaxMs = 0.3;
constexpr float kSignificant = 0.0002f;  // a change of the correction this big in a frame counts for rev
constexpr float kTouching = 0.004f;      // collide.cpp's touch: a sphere this near the gun

float Dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float Length(const float* a) { return std::sqrt(Dot(a, a)); }
void Cross(const float* a, const float* b, float* out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
void Normalize(float* v) {
    const float n = Length(v);
    if (n > 1e-12f)
        v[0] /= n, v[1] /= n, v[2] /= n;
}
void Transform(const float* m, const float* p, float* out) {
    for (int i = 0; i < 3; ++i)
        out[i] = m[i * 4] * p[0] + m[i * 4 + 1] * p[1] + m[i * 4 + 2] * p[2] + m[i * 4 + 3];
}
void Mul3(const float* a, const float* b, float* out) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
}
void Apply3(const float* m, const float* v, float* out) {
    for (int i = 0; i < 3; ++i)
        out[i] = m[i * 3] * v[0] + m[i * 3 + 1] * v[1] + m[i * 3 + 2] * v[2];
}
// A turn of degrees about a unit axis (row-major 3x3, right-handed).
void AxisAngle(const float* axis, float degrees, float* m) {
    const float c = std::cos(degrees * kPi / 180), s = std::sin(degrees * kPi / 180), t = 1 - c;
    const float x = axis[0], y = axis[1], z = axis[2];
    const float r[9] = {t * x * x + c,     t * x * y - s * z, t * x * z + s * y, t * x * y + s * z, t * y * y + c,
                        t * y * z - s * x, t * x * z - s * y, t * y * z + s * x, t * z * z + c};
    std::memcpy(m, r, sizeof(r));
}
float Clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
float Ease(float u) {  // 0 to 1, eased in and out
    u = Clamp01(u);
    return u * u * (3 - 2 * u);
}

// The field's distance at p (metres; false off its grid).
bool Field(const float* p, float& distance) {
    float normal[3];
    return gunshape::Distance(p, distance, normal);
}

// The glove as hands.cpp poses the free hand: open and relaxed (as during a slap).
struct Glove {
    float bones[handpose::kBones][12];
    std::vector<float> centre, radius;  // each sphere posed, in the gloves' bind space
    std::vector<int> finger;
    float palm[3] = {};  // the palm's spheres' middle, posed (the point the paths move)
    int count = 0;
};

Glove MakeGlove() {
    Glove g;
    handpose::Fingers fingers;
    fingers.spread = kSpread;
    handpose::Pose(fingers, kWidth, g.bones);
    const collide::Sphere* spheres = collide::Spheres(g.count);
    int palms = 0;
    for (int i = 0; i < g.count; ++i) {
        float c[3];
        Transform(g.bones[spheres[i].bone], spheres[i].centre, c);
        g.centre.insert(g.centre.end(), c, c + 3);
        g.radius.push_back(spheres[i].radius);
        g.finger.push_back(spheres[i].finger);
        if (spheres[i].finger == -1) {
            for (int k = 0; k < 3; ++k)
                g.palm[k] += c[k];
            ++palms;
        }
    }
    for (float& v : g.palm)
        v /= static_cast<float>(palms);
    return g;
}

// The tracked hand in a frame: the glove's turn (bind space -> the gun's model space) and where its
// palm point is in the gun's model space.
struct Hand {
    float r[9];
    float p[3];
};

// The glove's placement for hands.cpp's toGun (rows: rotation | translation).
void Placement(const Glove& g, const Hand& h, float* m) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            m[i * 4 + j] = h.r[i * 3 + j];
        m[i * 4 + 3] = h.p[i] - (h.r[i * 3] * g.palm[0] + h.r[i * 3 + 1] * g.palm[1] + h.r[i * 3 + 2] * g.palm[2]);
    }
}

// Sphere i placed by m and moved by shift.
void SphereAt(const Glove& g, const float* m, const float* shift, int i, float* out) {
    Transform(m, &g.centre[i * 3], out);
    for (int k = 0; k < 3; ++k)
        out[k] += shift[k];
}

// The deepest any sphere is in the gun (metres; negative, the nearest is that far off) and which.
float Deepest(const Glove& g, const float* m, const float* shift, int* which = nullptr) {
    float deepest = -1.0f;
    for (int i = 0; i < g.count; ++i) {
        float p[3], distance;
        SphereAt(g, m, shift, i, p);
        if (!Field(p, distance))
            continue;
        if (g.radius[i] - distance > deepest) {
            deepest = g.radius[i] - distance;
            if (which)
                *which = i;
        }
    }
    return deepest;
}

// The turn putting the glove's fingers along `along` and its palm's face towards `palm` (unit,
// across each other; the gun's model space).
void Facing(const float* along, const float* palm, float* r) {
    auto frame = [](const float* a, const float* b, float* u1, float* u2, float* u3) {
        std::memcpy(u1, a, 3 * sizeof(float));
        Normalize(u1);
        const float d = Dot(b, u1);
        for (int k = 0; k < 3; ++k)
            u2[k] = b[k] - d * u1[k];
        Normalize(u2);
        Cross(u1, u2, u3);
    };
    float u1[3], u2[3], u3[3], v1[3], v2[3], v3[3];
    frame(kAlong, kPalm, u1, u2, u3);
    frame(along, palm, v1, v2, v3);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i * 3 + j] = v1[i] * u1[j] + v2[i] * u2[j] + v3[i] * u3[j];
}

// How far back from q (against dir, the way the hand comes in) the palm point is when the glove,
// turned by r, first touches the gun coming in along dir.
float FirstTouch(const Glove& g, const float* r, const float* q, const float* dir) {
    auto deepestAt = [&](float s) {
        Hand h;
        std::memcpy(h.r, r, sizeof(h.r));
        for (int k = 0; k < 3; ++k)
            h.p[k] = q[k] - dir[k] * s;
        float m[12];
        Placement(g, h, m);
        const float zero[3] = {};
        return Deepest(g, m, zero);
    };
    float free = 0.2f;
    for (float s = 0.2f; s > -0.1f; s -= 0.001f) {
        if (deepestAt(s) >= 0.0f) {
            float low = s, high = free;  // low touches, high is free
            for (int k = 0; k < 30; ++k) {
                const float mid = (low + high) / 2;
                (deepestAt(mid) >= 0.0f ? low : high) = mid;
            }
            return high;
        }
        free = s;
    }
    return 0.0f;
}

// A stroke: in at speed v for `approach` metres, on in slowing to a stop over `depth`, back out
// the same way, then still: how far in at time t.
float Stroke(float t, float v, float approach, float depth) {
    const float a = v * v / (2 * depth);
    const float t1 = approach / v, t2 = t1 + 2 * depth / v, t3 = t2 + 2 * depth / v, t4 = t3 + approach / v;
    if (t < t1)
        return v * t;
    if (t < t2) {
        const float u = t - t1;
        return approach + v * u - 0.5f * a * u * u;
    }
    if (t < t3) {
        const float u = t - t2;
        return approach + depth - 0.5f * a * u * u;
    }
    if (t < t4)
        return approach - v * (t - t3);
    return 0.0f;
}
float StrokeTime(float v, float approach, float depth) { return 2 * approach / v + 4 * depth / v; }

struct Scenario {
    std::string name;
    int frames = 0;
    std::function<Hand(float)> at;  // the tracked hand at time t
    bool slap = false;              // into the bolt release's zone: reload.cpp must feel it
};

struct Result {
    float pen = 0, jerk = 0;
    bool through = false;
    int rev = 0, slaps = 0, touchFrames = 0;
    double msMax = 0, msAvg = 0;
};

double Ticks() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart);
}

Result Run(const Glove& g, const Scenario& s, bool verbose) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    const double toMs = 1000.0 / static_cast<double>(frequency.QuadPart);
    const int n = s.frames, spheres = g.count;
    std::vector<double> times(n, 1e30);
    std::vector<float> shifts(n * 3), tracked(static_cast<size_t>(n) * spheres * 3), drawn(static_cast<size_t>(n) * spheres * 3);
    std::vector<float> touches(n * 3), anchors(n * 3), palms(n * 3), handPalms(n * 3), pens(n);
    std::vector<char> touched(n);
    for (int run = 0; run < 7; ++run) {
        collide::State state;
        for (int f = 0; f < n; ++f) {
            const Hand h = s.at(f * kDt);
            float m[12], shift[3], touch[3] = {};
            Placement(g, h, m);
            const double before = Ticks();
            const bool on = collide::KeepOut(g.bones, m, state, shift, touch);
            const double after = Ticks();
            times[f] = std::min(times[f], (after - before) * toMs);
            if (run)
                continue;
            touched[f] = on;
            std::memcpy(&shifts[f * 3], shift, sizeof(shift));
            std::memcpy(&touches[f * 3], touch, sizeof(touch));
            const float zero[3] = {};
            for (int i = 0; i < spheres; ++i) {
                SphereAt(g, m, zero, i, &tracked[(static_cast<size_t>(f) * spheres + i) * 3]);
                SphereAt(g, m, shift, i, &drawn[(static_cast<size_t>(f) * spheres + i) * 3]);
            }
            Transform(m, kAnchor, &anchors[f * 3]);
            Transform(m, g.palm, &handPalms[f * 3]);
            for (int k = 0; k < 3; ++k)
                palms[f * 3 + k] = handPalms[f * 3 + k] + shift[k];
            pens[f] = Deepest(g, m, shift);
        }
    }
    Result r;
    auto at = [&](const std::vector<float>& v, int f, int i) { return &v[(static_cast<size_t>(f) * spheres + i) * 3]; };
    // Contacts: runs of touching frames (gaps under 3 frames joined), each with the 2 frames after.
    std::vector<int> contact(n, -1);
    int contacts = 0;
    for (int f = 0, last = -100; f < n; ++f) {
        if (!touched[f])
            continue;
        if (f - last > 3)
            ++contacts;
        for (int k = std::max(last + 1, f - (f - last <= 3 ? 3 : 0)); k <= std::min(n - 1, f + 2); ++k)
            contact[k] = contacts - 1;
        last = f;
    }
    std::vector<int> reversals(contacts + 1, 0);  // the last: outside any contact
    int lastSignificant = -1;
    bool wasTouching = false;
    for (int f = 0; f < n; ++f) {
        r.pen = std::max(r.pen, pens[f]);
        r.touchFrames += touched[f];
        // Through: from where the hand came in to the glove's palm, the gun in the way.
        {
            const float* a = &handPalms[0];
            const float* b = &palms[f * 3];
            const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            const int steps = std::max(1, static_cast<int>(Length(d) / 0.001f));
            for (int k = 0; k <= steps; ++k) {
                const float u = static_cast<float>(k) / steps, p[3] = {a[0] + d[0] * u, a[1] + d[1] * u, a[2] + d[2] * u};
                float distance;
                if (Field(p, distance) && distance < -0.003f) {
                    r.through = true;
                    break;
                }
            }
        }
        float frameJerk = 0.0f;
        if (f > 0) {
            for (int i = 0; i < spheres; ++i) {
                const float* g0 = at(drawn, f - 1, i);
                const float* g1 = at(drawn, f, i);
                const float* h0 = at(tracked, f - 1, i);
                const float* h1 = at(tracked, f, i);
                const float dg[3] = {g1[0] - g0[0], g1[1] - g0[1], g1[2] - g0[2]};
                const float dh[3] = {h1[0] - h0[0], h1[1] - h0[1], h1[2] - h0[2]};
                frameJerk = std::max(frameJerk, Length(dg) - Length(dh));
            }
            r.jerk = std::max(r.jerk, frameJerk);
            // Reversals of the correction, unless the hand turned back along it too (a sphere's
            // motion along it changed sign between the two changes).
            const float change[3] = {shifts[f * 3] - shifts[f * 3 - 3], shifts[f * 3 + 1] - shifts[f * 3 - 2],
                                     shifts[f * 3 + 2] - shifts[f * 3 - 1]};
            if (Length(change) >= kSignificant) {
                if (lastSignificant > 0) {
                    const int l = lastSignificant;
                    float before[3] = {shifts[l * 3] - shifts[l * 3 - 3], shifts[l * 3 + 1] - shifts[l * 3 - 2],
                                       shifts[l * 3 + 2] - shifts[l * 3 - 1]};
                    if (Dot(change, before) < 0.0f) {
                        Normalize(before);
                        bool handTurned = false;
                        for (int i = 0; i < spheres && !handTurned; ++i) {
                            const float* h0 = at(tracked, f - 1, i);
                            const float* h1 = at(tracked, f, i);
                            const float* k0 = at(tracked, l - 1, i);
                            const float* k1 = at(tracked, l, i);
                            const float now[3] = {h1[0] - h0[0], h1[1] - h0[1], h1[2] - h0[2]};
                            const float then[3] = {k1[0] - k0[0], k1[1] - k0[1], k1[2] - k0[2]};
                            const float a = Dot(now, before), b = Dot(then, before);
                            handTurned = a * b < 0.0f && std::fabs(a) > 1e-5f && std::fabs(b) > 1e-5f;
                        }
                        if (!handTurned)
                            ++reversals[contact[f] >= 0 ? contact[f] : contacts];
                    }
                }
                lastSignificant = f;
            }
        }
        // reload.cpp's slap: a fresh touch in the zone, the palm point faster than 0.25 m/s.
        const float* t = &touches[f * 3];
        const bool onRelease = touched[f] && t[0] > kSlapSide && t[1] > kSlapZone[0] && t[1] < kSlapZone[1] &&
                               t[2] > kSlapZone[2] && t[2] < kSlapZone[3];
        float speed = 0.0f;
        if (f > 0) {
            const float d[3] = {anchors[f * 3] - anchors[f * 3 - 3], anchors[f * 3 + 1] - anchors[f * 3 - 2],
                                anchors[f * 3 + 2] - anchors[f * 3 - 1]};
            speed = Length(d) / kDt;
        }
        const bool slapped = f > 0 && onRelease && speed > kTouchSlapSpeed && !wasTouching;
        r.slaps += slapped;
        wasTouching = touched[f];
        if (verbose)
            std::printf("%4d %6.1f ms  hand %7.1f %7.1f %7.1f  glove %7.1f %7.1f %7.1f  shift %6.1f %6.1f %6.1f  %s "
                        "%6.1f %6.1f %6.1f  pen %5.1f  jerk %5.1f  speed %4.2f%s  %.3f ms\n",
                        f, f * kDt * 1000, handPalms[f * 3] * 1000, handPalms[f * 3 + 1] * 1000,
                        handPalms[f * 3 + 2] * 1000, palms[f * 3] * 1000, palms[f * 3 + 1] * 1000,
                        palms[f * 3 + 2] * 1000, shifts[f * 3] * 1000, shifts[f * 3 + 1] * 1000,
                        shifts[f * 3 + 2] * 1000, touched[f] ? "touch" : "     ", t[0] * 1000, t[1] * 1000,
                        t[2] * 1000, pens[f] * 1000, frameJerk * 1000, speed, slapped ? " SLAP" : "", times[f]);
    }
    for (int c : reversals)
        r.rev = std::max(r.rev, c);
    double sum = 0;
    for (double t : times) {
        r.msMax = std::max(r.msMax, t);
        sum += t;
    }
    r.msAvg = n ? sum / n : 0;
    return r;
}

// The receiver's left side: where the gun's surface is along x from outside, at (y, z).
float SideAt(float y, float z, float from, float step) {
    for (float x = from; std::fabs(x) < 0.2f; x += step) {
        const float p[3] = {x, y, z};
        float distance;
        if (Field(p, distance) && distance <= 0.0f)
            return x;
    }
    return NAN;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::wstring path = L"E:\\BF3 CLAUDE VR PROJECT\\build\\guns\\m416.bin";
    std::string verbose;
    bool probe = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "-v") && i + 1 < argc)
            verbose = argv[++i];
        else if (!std::strcmp(argv[i], "probe"))
            probe = true;
        else
            path.assign(argv[i], argv[i] + std::strlen(argv[i]));
    }
    if (!gunshape::Load(path.c_str())) {
        std::fprintf(stderr, "can't read the gun's shape from %ls\n", path.c_str());
        return 100;
    }
    const Glove g = MakeGlove();

    // The glove's turns. The slap as the user's video shows it (2026-10-30): the palm flat on the
    // receiver's left side (facing -x), the fingers up and leaning a little towards the muzzle. Palm
    // down on the top, fingers forward; palm to the right side (+x), fingers up.
    const float upForward[3] = {0.0f, std::cos(20 * kPi / 180), std::sin(20 * kPi / 180)};
    const float forward[3] = {0, 0, 1};
    const float toRight[3] = {-1, 0, 0}, toLeft[3] = {1, 0, 0}, down[3] = {0, -1, 0};
    float leftSide[9], top[9], rightSide[9];
    Facing(upForward, toRight, leftSide);
    Facing(forward, down, top);
    Facing(upForward, toLeft, rightSide);
    // The bolt release (inside the receiver, at the palm's height and place along it) and the top.
    const float release[3] = {0.0f, 0.0f, 0.37f}, topAt[3] = {0.0f, 0.0f, 0.35f};
    const float inLeft[3] = {-1, 0, 0}, inRight[3] = {1, 0, 0}, inTop[3] = {0, -1, 0};
    const float touchLeft = FirstTouch(g, leftSide, release, inLeft);
    const float touchTop = FirstTouch(g, top, topAt, inTop);
    const float touchRight = FirstTouch(g, rightSide, release, inRight);

    if (probe) {
        std::printf("the receiver's left side (x of its surface, mm) at heights y (rows) and along z (columns):\n      ");
        for (float z = 0.26f; z < 0.481f; z += 0.02f)
            std::printf(" %6.0f", z * 1000);
        std::printf("\n");
        for (float y = -0.04f; y < 0.0751f; y += 0.01f) {
            std::printf("%5.0f ", y * 1000);
            for (float z = 0.26f; z < 0.481f; z += 0.02f)
                std::printf(" %6.1f", SideAt(y, z, 0.1f, -0.0002f) * 1000);
            std::printf("\n");
        }
        std::printf("the right side (x, mm) at y 10, along z:\n      ");
        for (float z = 0.26f; z < 0.481f; z += 0.02f)
            std::printf(" %6.1f", SideAt(0.01f, z, -0.1f, 0.0002f) * 1000);
        std::printf("\nthe top (y, mm) at x 0, along z:\n      ");
        for (float z = 0.26f; z < 0.481f; z += 0.02f) {
            float y = 0.15f;
            for (; y > -0.1f; y -= 0.0002f) {
                const float p[3] = {0.0f, y, z};
                float distance;
                if (Field(p, distance) && distance <= 0.0f)
                    break;
            }
            std::printf(" %6.1f", y * 1000);
        }
        std::printf("\n");
        auto report = [&](const char* name, const float* r, const float* q, const float* dir, float s) {
            Hand h;
            std::memcpy(h.r, r, sizeof(h.r));
            for (int k = 0; k < 3; ++k)
                h.p[k] = q[k] - dir[k] * s;
            float m[12];
            Placement(g, h, m);
            const float zero[3] = {};
            int which = -1;
            const float deepest = Deepest(g, m, zero, &which);
            float c[3], distance, normal[3];
            SphereAt(g, m, zero, which, c);
            gunshape::Distance(c, distance, normal);
            std::printf("%s: the palm point %.1f mm back from (%.0f %.0f %.0f) at the first touch (palm at %.1f %.1f "
                        "%.1f): sphere %d (finger %d) at %.1f %.1f %.1f, %.2f mm, its way out %.2f %.2f %.2f\n",
                        name, s * 1000, q[0] * 1000, q[1] * 1000, q[2] * 1000, h.p[0] * 1000, h.p[1] * 1000,
                        h.p[2] * 1000, which, g.finger[which], c[0] * 1000, c[1] * 1000, c[2] * 1000, deepest * 1000,
                        normal[0], normal[1], normal[2]);
            // Every sphere's gap to the gun there.
            for (int i = 0; i < g.count; ++i) {
                float p[3], d;
                SphereAt(g, m, zero, i, p);
                if (Field(p, d) && d - g.radius[i] < 0.01f)
                    std::printf("    sphere %2d finger %2d gap %5.1f mm at %6.1f %6.1f %6.1f\n", i, g.finger[i],
                                (d - g.radius[i]) * 1000, p[0] * 1000, p[1] * 1000, p[2] * 1000);
            }
        };
        report("left side", leftSide, release, inLeft, touchLeft);
        report("top", top, topAt, inTop, touchTop);
        report("right side", rightSide, release, inRight, touchRight);
        return 0;
    }

    // The scenarios: the hand's palm point moved and the glove turned, frame by frame.
    auto hand = [](const float* r, const float* q, const float* dir, float back) {
        Hand h;
        std::memcpy(h.r, r, sizeof(h.r));
        for (int k = 0; k < 3; ++k)
            h.p[k] = q[k] - dir[k] * back;
        return h;
    };
    // The hand h turned by degrees about a unit axis through its wrist (where the arm folds in).
    auto turned = [&g](Hand h, const float* axis, float degrees) {
        float q[9], r[9], wrist[3], d[3], a[3], v[3];
        for (int k = 0; k < 3; ++k)
            d[k] = kAnchor[k] - g.palm[k];
        Apply3(h.r, d, a);
        for (int k = 0; k < 3; ++k)
            wrist[k] = h.p[k] + a[k], v[k] = h.p[k] - wrist[k];
        AxisAngle(axis, degrees, q);
        Mul3(q, h.r, r);
        std::memcpy(h.r, r, sizeof(r));
        Apply3(q, v, a);
        for (int k = 0; k < 3; ++k)
            h.p[k] = wrist[k] + a[k];
        return h;
    };
    auto frames = [](float seconds) { return static_cast<int>(std::ceil(seconds / kDt)) + 1; };
    std::vector<Scenario> scenarios;
    const float* L = leftSide;
    // Resting on the receiver's left side, 4 mm in: come in over half a second, then still.
    scenarios.push_back({"rest on the side, still", frames(1.5f), [=](float t) {
                             return hand(L, release, inLeft, touchLeft + 0.03f - 0.034f * Ease(t / 0.5f));
                         }});
    // ... with both hands swaying (the hand against the gun a few mm each way, a little turned).
    scenarios.push_back({"rest on the side, swaying", frames(3.5f), [=](float t) {
                             const float s = Ease(t / 0.5f), w = 2 * kPi;
                             Hand h = hand(L, release, inLeft, touchLeft + 0.03f - 0.034f * s);
                             h.p[0] += s * 0.002f * std::sin(w * 0.7f * t);
                             h.p[1] += s * 0.003f * std::sin(w * 0.5f * t);
                             h.p[2] += s * 0.003f * std::sin(w * 0.6f * t + 1.0f);
                             const float z[3] = {0, 0, 1};
                             return turned(h, z, s * 2.0f * std::sin(w * 0.4f * t));
                         }});
    // Slaps at the bolt release, straight in: from 5 cm off, on in past the surface and back.
    for (const float v : {0.3f, 0.5f, 1.0f}) {
        const float depth = v < 0.4f ? 0.015f : v < 0.7f ? 0.02f : 0.03f;
        char name[64];
        std::snprintf(name, sizeof(name), "soft slap %.1f m/s", v);
        scenarios.push_back({name, frames(StrokeTime(v, 0.05f, depth) + 0.15f), [=](float t) {
                                 return hand(L, release, inLeft, touchLeft + 0.05f - Stroke(t, v, 0.05f, depth));
                             }, true});
    }
    // A soft slap coming down and forward onto it (as a real slap swings in).
    {
        float in[3] = {-1.0f, -0.35f, 0.2f};
        Normalize(in);
        const float s0 = FirstTouch(g, L, release, in);
        scenarios.push_back({"soft slap 0.5 m/s, angled", frames(StrokeTime(0.5f, 0.05f, 0.02f) + 0.15f),
                             [=](float t) {
                                 return hand(L, release, in, s0 + 0.05f - Stroke(t, 0.5f, 0.05f, 0.02f));
                             }, true});
    }
    // Hard slaps on through the gun to 10 cm past its middle (x 0), and back.
    for (const float v : {3.0f, 6.0f}) {
        const float depth = touchLeft + 0.10f;
        char name[64];
        std::snprintf(name, sizeof(name), "hard slap %.0f m/s", v);
        scenarios.push_back({name, frames(StrokeTime(v, 0.06f, depth) + 0.15f), [=](float t) {
                                 return hand(L, release, inLeft, touchLeft + 0.06f - Stroke(t, v, 0.06f, depth));
                             }, true});
    }
    // A soft slap with the fingers forward along the gun (they meet the handguard first).
    {
        float forwardSide[9];
        const float ahead[3] = {0.0f, std::sin(15 * kPi / 180), std::cos(15 * kPi / 180)};
        Facing(ahead, toRight, forwardSide);
        const float s0 = FirstTouch(g, forwardSide, release, inLeft);
        scenarios.push_back({"slap 0.5 m/s, fingers forward", frames(StrokeTime(0.5f, 0.05f, 0.02f) + 0.15f),
                             [=](float t) {
                                 Hand h = hand(forwardSide, release, inLeft, s0 + 0.05f - Stroke(t, 0.5f, 0.05f, 0.02f));
                                 return h;
                             }});
    }
    // Sliding along the side pressed 2 cm in: in at the back of the zone, along to its front, out.
    scenarios.push_back({"slide along the side, 2 cm in", frames(2.0f), [=](float t) {
                             const float in = Ease(t / 0.4f) - Ease((t - 1.6f) / 0.4f);
                             const float along = 0.14f * Ease((t - 0.45f) / 1.05f);  // 0.2 m/s at most
                             Hand h = hand(L, release, inLeft, touchLeft + 0.03f - 0.05f * in);
                             h.p[2] += -0.07f + along;
                             return h;
                         }});
    // The wrist turned 40 degrees each way while touching (4 mm in): about the fingers (roll), across
    // the hand (pitch: the fingers towards and away from the gun) and about the palm (yaw).
    {
        float axes[3][3], a[3];
        Apply3(L, kAlong, axes[0]);
        Apply3(L, kPalm, a);
        Cross(axes[0], a, axes[1]);
        std::memcpy(axes[2], a, sizeof(a));
        const char* names[3] = {"roll the wrist 40 deg touching", "pitch the wrist 40 deg touching",
                                "yaw the wrist 40 deg touching"};
        for (int i = 0; i < 3; ++i) {
            float axis[3];
            std::memcpy(axis, axes[i], sizeof(axis));
            Normalize(axis);
            scenarios.push_back({names[i], frames(3.0f), [=](float t) {
                                     Hand h = hand(L, release, inLeft, touchLeft + 0.03f - 0.034f * Ease(t / 0.5f));
                                     const float u = std::clamp((t - 0.6f) / 2.0f, 0.0f, 1.0f);
                                     return turned(h, axis, 40.0f * std::sin(2 * kPi * u));
                                 }});
        }
    }
    // The top and the right side: pressed 1 cm in at 0.3 m/s and back.
    scenarios.push_back({"touch the top", frames(StrokeTime(0.3f, 0.04f, 0.01f) + 0.15f), [=](float t) {
                             return hand(top, topAt, inTop, touchTop + 0.04f - Stroke(t, 0.3f, 0.04f, 0.01f));
                         }});
    scenarios.push_back({"touch the right side", frames(StrokeTime(0.3f, 0.04f, 0.01f) + 0.15f), [=](float t) {
                             return hand(rightSide, release, inRight,
                                         touchRight + 0.04f - Stroke(t, 0.3f, 0.04f, 0.01f));
                         }});
    // The hand on through the gun and out of its far side (to 10 cm past its middle), and back.
    {
        const float depth = touchLeft + 0.10f;
        scenarios.push_back({"through the gun and out", frames(StrokeTime(0.3f, 0.05f, depth) + 0.15f), [=](float t) {
                                 return hand(L, release, inLeft, touchLeft + 0.05f - Stroke(t, 0.3f, 0.05f, depth));
                             }});
    }
    // Tracking jumps of 15 cm while resting 3 mm in: through the gun, and along its side.
    scenarios.push_back({"tracking jump 15 cm through", frames(1.5f), [=](float t) {
                             const float back = touchLeft + 0.03f - 0.033f * Ease(t / 0.4f);
                             const bool away = t > 0.55f && t < 1.0f;
                             return hand(L, release, inLeft, back - (away ? 0.15f : 0.0f));
                         }});
    scenarios.push_back({"tracking jump 15 cm along", frames(1.5f), [=](float t) {
                             Hand h = hand(L, release, inLeft, touchLeft + 0.03f - 0.033f * Ease(t / 0.4f));
                             h.p[2] += -0.07f + (t > 0.55f && t < 1.0f ? 0.15f : 0.0f);
                             return h;
                         }});

    int failed = 0, failedScenarios = 0;
    std::printf("%-32s %6s %7s %5s %8s %4s %5s %7s %7s  %s\n", "scenario", "frames", "pen mm", "thru", "jerk mm", "rev",
                "slap", "ms max", "ms avg", "result");
    for (const Scenario& s : scenarios) {
        const bool show = !verbose.empty() && s.name.find(verbose) != std::string::npos;
        if (show)
            std::printf("--- %s\n", s.name.c_str());
        const Result r = Run(g, s, show);
        const bool penBad = r.pen > kMaxPen, jerkBad = r.jerk > kMaxJerk, revBad = r.rev > kMaxRev,
                   slapBad = s.slap && r.slaps < 1, msBad = r.msMax > kMaxMs;
        const int bad = penBad + r.through + jerkBad + revBad + slapBad + msBad;
        failed += bad;
        failedScenarios += bad > 0;
        std::printf("%-32s %6d %6.1f%s %5s %7.1f%s %3d%s %4d%s %6.3f%s %7.3f  %s\n", s.name.c_str(), s.frames,
                    std::max(r.pen, 0.0f) * 1000, penBad ? "*" : " ", r.through ? "YES*" : "no", r.jerk * 1000,
                    jerkBad ? "*" : " ", r.rev, revBad ? "*" : " ", r.slaps, slapBad ? "*" : s.slap ? " " : "-",
                    r.msMax, msBad ? "*" : " ", r.msAvg, bad ? "FAIL" : "pass");
    }
    std::printf("%d checks failed in %d of %zu scenarios (pass: pen <= %.0f mm, never through, jerk <= %.0f mm, rev <= "
                "%d, slaps into the zone felt, <= %.1f ms a frame)\n",
                failed, failedScenarios, scenarios.size(), kMaxPen * 1000, kMaxJerk * 1000, kMaxRev, kMaxMs);
    return failed;
}
