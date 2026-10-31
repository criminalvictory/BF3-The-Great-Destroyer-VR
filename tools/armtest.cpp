// The forearm placed from its hand (src/armik.h, as hands.cpp runs it for the left glove), without
// the game: the bind pose, the whole arm moved rigidly, the game's own rifle hold from the mesh dumps,
// the twist sweep, the wrist's readings and range, degenerate poses, the time a solve takes, and the
// stand-in's test hooks. The gloves' bind space: x to the left, y up, z forward (metres). Checks 4-8
// pose the left arm in a view's space (x right, y up, z back), as hands.cpp does.
//   1  the bind pose, the pole along the bind elbow's own way: the frames move points near the arm
//      0.5 mm at most (each arm)
//   2  500 random rigid moves of the whole arm (hand, shoulder and body axes): the frames follow
//      within 0.5 mm (each arm)
//   3  the game's own rifle hold (build\mesh_*_bones.bin: the body's and the gloves' depth passes'
//      bones and the sleeves' chest; skipped without them): from the game's shoulder, hand and chest
//      axes, the left forearm within 1 degree of the game's and its twist within 1 degree of the
//      game's -99.2 (the hand against its forearm bone, about the elbow-wrist line); the right
//      forearm within 3 degrees
//   4  the hand rolled 1 degree a frame about its forearm, 400 frames: twist bone B turns 0.7
//      degrees a frame at most, apart from the one flip back after the hold at 150 degrees
//   5  the hand bent about its wrist joint (flexion, then ulnar deviation, -80 to 80 degrees): read
//      within 0.5 degrees of the bend, held at the range's limits (flexion 70, extension 65, ulnar
//      40, radial 25) and only there
//   6  degenerate poses (the wrist on the shoulder, far out of reach): finite frames
//   7  a solve takes under 5 us
//   8  the stand-in's tests (hands.cpp, build\arms.txt "test move -0.25 0.05 0.35", "test neutral",
//      "test bend"): the wrist in reach and bent at the elbow; each bend read as given within 0.5
//      degrees, the forearm staying put (its elbow end within 1 mm)
// Usage: armtest [dump folder]   (the mesh dumps' folder; default E:\BF3 CLAUDE VR PROJECT\build)
// The exit code is the number of failed checks.

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "../src/armik.h"

namespace {

constexpr double kPiD = 3.14159265358979;

double Deg(double radians) {
    return radians * 180 / kPiD;
}

// ---- Points and rigid poses in double (3x4, rows: rotation | translation) ----
struct V3 {
    double x = 0, y = 0, z = 0;
    V3() = default;
    V3(double a, double b, double c) : x(a), y(b), z(c) {}
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(double s) const { return {x * s, y * s, z * s}; }
    double operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
};
double Dot(const V3& a, const V3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
V3 Cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double Len(const V3& a) {
    return std::sqrt(Dot(a, a));
}
V3 Unit(const V3& a) {
    const double l = Len(a);
    return l > 1e-15 ? a * (1.0 / l) : a;
}
double AngleDeg(const V3& a, const V3& b) {
    return Deg(std::acos(std::clamp(Dot(Unit(a), Unit(b)), -1.0, 1.0)));
}
V3 FromF(const float* f) {
    return {f[0], f[1], f[2]};
}

struct M34 {
    double m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    V3 Apply(const V3& p) const {
        return {m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
                m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]};
    }
    V3 Rot(const V3& p) const {
        return {m[0] * p.x + m[1] * p.y + m[2] * p.z, m[4] * p.x + m[5] * p.y + m[6] * p.z,
                m[8] * p.x + m[9] * p.y + m[10] * p.z};
    }
};
M34 Mul(const M34& a, const M34& b) {
    M34 o;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j)
            o.m[i * 4 + j] = a.m[i * 4] * b.m[j] + a.m[i * 4 + 1] * b.m[4 + j] + a.m[i * 4 + 2] * b.m[8 + j];
        o.m[i * 4 + 3] += a.m[i * 4 + 3];
    }
    return o;
}
// A turn of radians about the unit axis through pivot.
M34 RotAbout(const V3& axis, double angle, const V3& pivot) {
    const double c = std::cos(angle), s = std::sin(angle), t = 1 - c, x = axis.x, y = axis.y, z = axis.z;
    const double r[9] = {t * x * x + c,     t * x * y - s * z, t * x * z + s * y, t * x * y + s * z, t * y * y + c,
                         t * y * z - s * x, t * x * z - s * y, t * y * z + s * x, t * z * z + c};
    M34 o;
    for (int i = 0; i < 3; ++i) {
        o.m[i * 4] = r[i * 3], o.m[i * 4 + 1] = r[i * 3 + 1], o.m[i * 4 + 2] = r[i * 3 + 2];
        o.m[i * 4 + 3] = pivot[i] - (r[i * 3] * pivot.x + r[i * 3 + 1] * pivot.y + r[i * 3 + 2] * pivot.z);
    }
    return o;
}
void ToF(const M34& m, float* f) {
    for (int i = 0; i < 12; ++i)
        f[i] = static_cast<float>(m.m[i]);
}
M34 ToM(const float* f) {
    M34 m;
    for (int i = 0; i < 12; ++i)
        m.m[i] = f[i];
    return m;
}
// How far a 3x4's rotation turns from another's (degrees).
double TurnBetween(const M34& a, const M34& b) {
    double trace = 0;  // of a's rotation times b's transposed
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k)
            trace += a.m[i * 4 + k] * b.m[i * 4 + k];
    return Deg(std::acos(std::clamp((trace - 1) / 2, -1.0, 1.0)));
}

// The most two placements put points near the arm apart (mirrored for the right).
double MaxApart(const M34& a, const M34& b, bool right) {
    const V3 points[4] = {{0.37, 1.26, -0.01}, {0.51, 1.12, 0.16}, {0.44, 1.21, 0.06}, {0.30, 1.30, 0.10}};
    double most = 0;
    for (const V3& p0 : points) {
        const V3 p(right ? -p0.x : p0.x, p0.y, p0.z);
        most = std::max(most, Len(a.Apply(p) - b.Apply(p)));
    }
    return most;
}

V3 Joint(const float* joint, bool right) {
    return {right ? -joint[0] : joint[0], joint[1], joint[2]};
}

bool Finite(const armik::Result& r) {
    for (const auto& frame : r.frame)
        for (float v : frame)
            if (!std::isfinite(v))
                return false;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(r.fold[i]) || !std::isfinite(r.elbow[i]))
            return false;
    return true;
}

// A draw's bones as the game drew it, from the mod's mesh dump (weapon.txt "dump"): 12 floats each.
std::vector<M34> LoadBones(const std::string& dir, int count, int start) {
    char name[64];
    std::snprintf(name, sizeof(name), "\\mesh_%d_%d_bones.bin", count, start);
    std::ifstream f(dir + name, std::ios::binary);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    std::vector<M34> bones(bytes.size() / 48);
    for (size_t k = 0; k < bones.size(); ++k) {
        float m[12];
        std::memcpy(m, &bytes[k * 48], sizeof(m));
        bones[k] = ToM(m);
    }
    return bones;
}

// ---- Reporting ----
int g_failed = 0, g_counted = 0, g_skipped = 0;

void Report(const char* id, const char* what, bool pass, const char* format, ...) {
    char numbers[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(numbers, sizeof(numbers), format, args);
    va_end(args);
    std::printf("%-3s %-5s %s | %s\n", id, pass ? "PASS" : "FAIL", what, numbers);
    ++g_counted;
    g_failed += !pass;
}

void Skip(const char* id, const char* what, const char* why) {
    std::printf("%-3s %-5s %s | %s\n", id, "SKIP", what, why);
    ++g_skipped;
}

// ---- 1, 2: the bind pose and rigid moves ----
void BindAndRigid() {
    for (int side = 0; side < 2; ++side) {
        const bool right = side == 1;
        const double sx = right ? -1 : 1;
        const V3 S = Joint(armik::kShoulder, right), E = Joint(armik::kElbow, right), W = Joint(armik::kWrist, right);
        // The pole along the bind elbow's own way off the shoulder-wrist line, in the body's axes (out
        // to this arm's side: the bind space's x for the left; up y; back -z); no rising-hand term.
        const V3 D = Unit(W - S), pe = Unit((E - S) - D * Dot(E - S, D));
        armik::Params q;
        q.poleOut = static_cast<float>(pe.x * sx), q.poleDown = static_cast<float>(-pe.y),
        q.poleBack = static_cast<float>(-pe.z), q.riseOut = 0, q.riseDown = 0;
        const float out[3] = {static_cast<float>(sx), 0, 0}, up[3] = {0, 1, 0}, back[3] = {0, 0, -1};
        const float hand[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        const float shoulder[3] = {static_cast<float>(S.x), static_cast<float>(S.y), static_cast<float>(S.z)};
        armik::State st;
        armik::Result r;
        armik::Solve(hand, shoulder, out, up, back, right, q, st, r);
        double worst = 0;
        for (const auto& frame : r.frame)
            worst = std::max(worst, MaxApart(ToM(frame), M34{}, right));
        Report(right ? "1R" : "1L", right ? "the right arm's bind pose" : "the left arm's bind pose", worst <= 0.0005,
               "the frames move points near the arm %.3f mm at most (<= 0.5); bend %.2f deg, twist %.2f deg "
               "(the pole: out %.2f down %.2f back %.2f)",
               worst * 1000, Deg(r.bend), Deg(r.twist), q.poleOut, q.poleDown, q.poleBack);
        // The whole arm turned and moved at random: the frames move with it.
        std::mt19937 g(7 + side);
        std::uniform_real_distribution<double> u(-1, 1);
        double moved = 0;
        for (int n = 0; n < 500; ++n) {
            const V3 axis = Unit(V3(u(g), u(g), u(g)));
            M34 shift;
            shift.m[3] = u(g), shift.m[7] = u(g), shift.m[11] = u(g);
            const M34 G = Mul(RotAbout(axis, u(g) * 3.1, V3()), shift);
            float gh[12], gs[3], go[3], gu[3], gb[3];
            ToF(G, gh);
            const V3 s2 = G.Apply(S), o2 = G.Rot(V3(sx, 0, 0)), u2 = G.Rot(V3(0, 1, 0)), b2 = G.Rot(V3(0, 0, -1));
            for (int i = 0; i < 3; ++i)
                gs[i] = static_cast<float>(s2[i]), go[i] = static_cast<float>(o2[i]), gu[i] = static_cast<float>(u2[i]),
                gb[i] = static_cast<float>(b2[i]);
            armik::State s3;
            armik::Result r2;
            armik::Solve(gh, gs, go, gu, gb, right, q, s3, r2);
            for (const auto& frame : r2.frame)
                moved = std::max(moved, MaxApart(ToM(frame), G, right));
        }
        Report(right ? "2R" : "2L", right ? "the right arm moved rigidly" : "the left arm moved rigidly", moved <= 0.0005,
               "500 random turns and shifts: the frames within %.3f mm of the move (<= 0.5)", moved * 1000);
    }
}

// ---- 3: the game's own rifle hold ----
void GameHold(const std::string& dir) {
    const std::vector<M34> chest = LoadBones(dir, 8529, 0), body = LoadBones(dir, 21189, 28806),
                           gloves = LoadBones(dir, 7617, 49995);
    if (chest.empty() || body.size() < 22 || gloves.size() < 19) {
        Skip("3", "the game's own rifle hold", "no mesh dumps (mesh_8529_0, mesh_21189_28806, mesh_7617_49995 bones)");
        return;
    }
    // The body's axes from the chest bone (the sleeves' draw's 'c'): left, up, forward.
    const V3 left = Unit(chest[0].Rot(V3(1, 0, 0))), up = Unit(chest[0].Rot(V3(0, 1, 0))),
             forward = Unit(chest[0].Rot(V3(0, 0, 1)));
    for (int side = 0; side < 2; ++side) {
        const bool right = side == 1;
        // The left arm is in the body's depth pass (upper arm 17, forearm 18, twist bones 19 and 20, the
        // hand 21: the glove's bone 1), the right in the gloves' (14 ... 18).
        const std::vector<M34>& P = right ? gloves : body;
        const int upper = right ? 14 : 17, fore = right ? 15 : 18, A = right ? 16 : 19, B = right ? 17 : 20,
                  hand = right ? 18 : 21;
        const V3 s = P[upper].Apply(Joint(armik::kShoulder, right)), out = right ? left * -1 : left, back = forward * -1;
        float h[12], sh[3], o[3], u[3], b[3];
        ToF(P[hand], h);
        for (int i = 0; i < 3; ++i)
            sh[i] = static_cast<float>(s[i]), o[i] = static_cast<float>(out[i]), u[i] = static_cast<float>(up[i]),
            b[i] = static_cast<float>(back[i]);
        armik::Params q;
        if (right)
            q.poleOut = 0.9f, q.ulnar = 55.0f;
        armik::State st;
        armik::Result r;
        armik::Solve(h, sh, o, u, b, right, q, st, r);
        const V3 f0 = Unit(Joint(armik::kWrist, right) - Joint(armik::kElbow, right));
        const double apart = AngleDeg(P[fore].Rot(f0), ToM(r.frame[0]).Rot(f0));
        const double elbow = Len(P[fore].Apply(Joint(armik::kElbow, right)) - ToM(r.frame[0]).Apply(Joint(armik::kElbow, right)));
        char frames[256];
        std::snprintf(frames, sizeof(frames), "; frames against the game's bones %d/%d/%d: %.1f/%.1f/%.1f mm at most", fore,
                      A, B, MaxApart(ToM(r.frame[0]), P[fore], right) * 1000, MaxApart(ToM(r.frame[1]), P[A], right) * 1000,
                      MaxApart(ToM(r.frame[2]), P[B], right) * 1000);
        if (!right) {
            constexpr double kGameTwist = -99.2;  // the game's left hand against its forearm bone (the dumps)
            const bool pass = apart <= 1.0 && std::fabs(Deg(r.twist) - kGameTwist) <= 1.0;
            Report("3L", "the game's left hold rebuilt", pass,
                   "the forearm %.2f deg from the game's (<= 1), its elbow end %.0f mm off; twist %.1f deg (the game's "
                   "%.1f, within 1); bend %.1f (flexion %+.1f, ulnar %+.1f)%s%s",
                   apart, elbow * 1000, Deg(r.twist), kGameTwist, Deg(r.bend), Deg(r.flexion), Deg(r.deviation),
                   r.clamped ? " clamped" : "", frames);
        } else {
            Report("3R", "the game's right hold rebuilt", apart <= 3.0,
                   "the forearm %.2f deg from the game's (<= 3), its elbow end %.0f mm off; twist %.1f deg (the game's "
                   "44.1); bend %.1f (flexion %+.1f, ulnar %+.1f)%s%s",
                   apart, elbow * 1000, Deg(r.twist), Deg(r.bend), Deg(r.flexion), Deg(r.deviation),
                   r.clamped ? " clamped" : "", frames);
        }
    }
}

// ---- 4-7 on one pose: the left arm in a view's space (x right, y up, z back), the shoulder out to
// the left of the neck, the wrist in front of the chest; the hand turned so its straight wrist lies
// along the solved forearm (worked out here in double, apart from armik::Neutral). ----
struct Pose {
    float out[3] = {-1, 0, 0}, up[3] = {0, 1, 0}, back[3] = {0, 0, 1};
    float shoulder[3] = {-0.164f, 1.42f, 0.08f};
    V3 wrist{-0.08, 1.30, -0.36};
    M34 base;  // the hand
};

Pose MakePose(const armik::Params& p) {
    Pose s;
    // The glove's bind axes (x left, y up, z forward) onto the view's (x right, y up, z back).
    const double turn[12] = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0};
    std::memcpy(s.base.m, turn, sizeof(turn));
    const V3 at = s.base.Apply(Joint(armik::kWrist, false));
    s.base.m[3] += s.wrist.x - at.x, s.base.m[7] += s.wrist.y - at.y, s.base.m[11] += s.wrist.z - at.z;
    const V3 f0 = Unit(Joint(armik::kWrist, false) - Joint(armik::kElbow, false));
    for (int round = 0; round < 6; ++round) {
        float h[12];
        ToF(s.base, h);
        armik::State st;
        armik::Result r;
        armik::Solve(h, s.shoulder, s.out, s.up, s.back, false, p, st, r);
        const V3 f1 = Unit(FromF(r.wrist) - FromF(r.elbow)), hf = Unit(s.base.Rot(f0)), axis = Cross(hf, f1);
        if (Len(axis) > 1e-12)
            s.base = Mul(RotAbout(Unit(axis), std::atan2(Len(axis), Dot(hf, f1)), s.wrist), s.base);
    }
    return s;
}

void Bends(const Pose& s, const armik::Params& p) {
    const V3 f0 = Unit(Joint(armik::kWrist, false) - Joint(armik::kElbow, false));
    V3 pa(armik::kPalm[0], armik::kPalm[1], armik::kPalm[2]), ax(armik::kAcross[0], armik::kAcross[1], armik::kAcross[2]);
    ax = Unit(ax - f0 * Dot(ax, f0));
    pa = Unit(pa - f0 * Dot(pa, f0) - ax * Dot(pa, ax));
    struct Kind {
        const char* id;
        const char* name;
        bool flexion;  // read as flexion (else ulnar deviation)
        V3 axis;
        double most, least;  // the range: up to most positive, least negative
    } kinds[] = {{"5a", "flexion", true, Cross(f0, pa), p.flex, p.ext},
                 {"5b", "ulnar deviation", false, Cross(f0, ax), p.ulnar, p.radial}};
    float h[12];
    ToF(s.base, h);
    armik::State st0;
    armik::Result r0;
    armik::Solve(h, s.shoulder, s.out, s.up, s.back, false, p, st0, r0);
    for (const Kind& k : kinds) {
        double worst = 0, worstOther = 0, movedFree = 0;
        int wrongHold = 0;
        std::string readings;
        for (double a : {-80.0, -60.0, -30.0, 0.0, 30.0, 60.0, 80.0}) {
            const M34 H = Mul(RotAbout(Unit(s.base.Rot(k.axis)), a * kPiD / 180, s.wrist), s.base);
            ToF(H, h);
            armik::State st;
            armik::Result r;
            armik::Solve(h, s.shoulder, s.out, s.up, s.back, false, p, st, r);
            const double want = std::clamp(a, -k.least, k.most);
            const bool held = a > k.most || a < -k.least;
            const double read = Deg(k.flexion ? r.flexion : r.deviation), other = Deg(k.flexion ? r.deviation : r.flexion);
            worst = std::max(worst, std::fabs(read - want));
            worstOther = std::max(worstOther, std::fabs(other));
            wrongHold += held != r.clamped;
            if (!held)
                movedFree = std::max(movedFree, Len(FromF(r.elbow) - FromF(r0.elbow)));
            char one[48];
            std::snprintf(one, sizeof(one), " %+.0f->%+.1f%s", a, read, r.clamped ? "h" : "");
            readings += one;
        }
        Report(k.id, k.name, worst <= 0.5 && worstOther <= 0.5 && wrongHold == 0,
               "read within %.2f deg of the bend held to its range (<= 0.5), the other way %.2f; %d held wrongly; the "
               "forearm's elbow end moved %.1f mm unheld; bent->read (h held):%s",
               worst, worstOther, wrongHold, movedFree * 1000, readings.c_str());
    }
}

void TwistSweep(const Pose& s, const armik::Params& p) {
    const V3 f0 = Unit(Joint(armik::kWrist, false) - Joint(armik::kElbow, false));
    armik::State st;
    double mostB = 0, last = 0;
    int heldFrom = -1, flips = 0, flipAt = -1;
    M34 lastB;
    for (int deg = 0; deg <= 400; ++deg) {
        const M34 H = Mul(RotAbout(Unit(s.base.Rot(f0)), deg * kPiD / 180, s.wrist), s.base);
        float h[12];
        ToF(H, h);
        armik::Result r;
        armik::Solve(h, s.shoulder, s.out, s.up, s.back, false, p, st, r);
        const double twist = Deg(r.twist);
        if (r.twistHeld && heldFrom < 0)
            heldFrom = deg;
        const bool flip = deg > 0 && std::fabs(twist - last) > 90;
        if (flip && flipAt < 0)
            flipAt = deg;
        flips += flip;
        const M34 B = ToM(r.frame[2]);
        if (deg > 0 && !flip)
            mostB = std::max(mostB, TurnBetween(B, lastB));
        lastB = B;
        last = twist;
    }
    Report("4", "the hand rolled about its forearm", mostB <= 0.7 && flips == 1 && heldFrom >= 0,
           "twist bone B turns %.3f deg a frame at most (<= 0.7) with the hand's 1; the twist held from %d deg of roll, "
           "%d flip%s back (at %d deg; one wanted)",
           mostB, heldFrom, flips, flips == 1 ? "" : "s", flipAt);
}

void Degenerate(const Pose& s, const armik::Params& p) {
    int bad = 0;
    std::string seen;
    const V3 Wb = Joint(armik::kWrist, false);
    for (const V3& w : {V3(-0.164, 1.42, 0.08), V3(-0.164, 1.425, 0.08), V3(-0.164, 0.6, 0.08), V3(-0.164, 2.3, 0.08),
                        V3(-0.164, 1.42, -1.2), V3(0.6, 1.4, -0.2)}) {
        M34 H = s.base;
        const V3 at = H.Apply(Wb);
        H.m[3] += w.x - at.x, H.m[7] += w.y - at.y, H.m[11] += w.z - at.z;
        float h[12];
        ToF(H, h);
        armik::State st;
        armik::Result r;
        for (int frame = 0; frame < 3; ++frame)
            armik::Solve(h, s.shoulder, s.out, s.up, s.back, false, p, st, r);
        bad += !Finite(r);
        char one[96];
        std::snprintf(one, sizeof(one), " (%.2f %.2f %.2f) %s%s", w.x, w.y, w.z, r.reach ? "in reach" : "out of reach",
                      Finite(r) ? "" : " NOT FINITE");
        seen += one;
    }
    Report("6", "degenerate poses", bad == 0, "%d of 6 not finite; wrists at%s", bad, seen.c_str());
}

void Cost(const Pose& s, const armik::Params& p) {
    LARGE_INTEGER frequency, a, b;
    QueryPerformanceFrequency(&frequency);
    float h[12];
    ToF(s.base, h);
    armik::State st;
    armik::Result r;
    volatile float sink = 0;
    constexpr int kSolves = 200000;
    QueryPerformanceCounter(&a);
    for (int n = 0; n < kSolves; ++n) {
        h[3] = -0.5f + (n % 100) * 0.002f;
        armik::Solve(h, s.shoulder, s.out, s.up, s.back, false, p, st, r);
        sink = sink + r.frame[2][3];
    }
    QueryPerformanceCounter(&b);
    const double us = static_cast<double>(b.QuadPart - a.QuadPart) * 1e6 / static_cast<double>(frequency.QuadPart) / kSolves;
    Report("7", "the time a solve takes", us < 5.0, "%.3f us a solve (< 5)", us);
}

// ---- 8: the stand-in's left hand (input.cpp: under the handguard, palm up, from the stand-in's
// aim 0.08 right, 0.25 down, 0.45 in front of the eyes) with build\grip.txt's line-up, the head
// level at the view's origin, as hands.cpp places it; then arms.txt's test lines. ----
void StandIn(const armik::Params& p) {
    // grip.txt's leftpose (2026-10-31): the glove on the left controller's grip pose.
    const double offset[12] = {-0.78243, -0.59231, -0.19229, 1.12604,  -0.50836, 0.78587,
                               -0.35215, -0.48218, 0.35969,  -0.17776, -0.91598, 0.22423};
    M34 lineUp;
    std::memcpy(lineUp.m, offset, sizeof(offset));
    const double half = -0.7853982;  // a quarter turn about the forward axis: palm up
    M34 controller = RotAbout(V3(0, 0, 1), 2 * half, V3());
    controller.m[3] = 0.08 - 0.04, controller.m[7] = -0.25 - 0.07, controller.m[11] = -0.45 - 0.30;
    M34 glove = Mul(controller, lineUp);
    glove.m[3] += -0.25, glove.m[7] += 0.05, glove.m[11] += 0.35;  // "test move -0.25 0.05 0.35"
    // The shoulder as hands.cpp estimates it: the neck 7.5 cm below and 8 cm behind the eyes, then 16.4
    // cm out, 16 cm down and 2 cm back; the body facing where the head does.
    const float out[3] = {-1, 0, 0}, up[3] = {0, 1, 0}, back[3] = {0, 0, 1};
    const float shoulder[3] = {-0.164f, -0.075f - 0.16f, 0.08f + 0.02f};
    float hand[12];
    ToF(glove, hand);
    armik::Neutral(hand, shoulder, out, up, back, false, p);  // "test neutral"
    armik::State st0;
    armik::Result r0;
    armik::Solve(hand, shoulder, out, up, back, false, p, st0, r0);
    const V3 s(shoulder[0], shoulder[1], shoulder[2]), w = FromF(r0.wrist), e = FromF(r0.elbow);
    const double reach = Len(w - s);
    // The elbow's bend (180 straight): from the two bones' lengths and the reach.
    const double a = armik::kUpper, b = armik::kFore, dc = std::clamp(reach, 0.0406, 0.5611);
    const double elbowBend = 180 - Deg(std::acos(std::clamp((a * a + b * b - dc * dc) / (2 * a * b), -1.0, 1.0)));
    Report("8a", "the stand-in's test hand neutral", r0.reach && elbowBend > 30 && Deg(r0.bend) < 0.5,
           "the wrist %.3f m from the shoulder (reach 0.561), the elbow bent %.0f deg (> 30); the wrist bent %.2f deg "
           "after \"test neutral\" (< 0.5); twist %+.1f",
           reach, elbowBend, Deg(r0.bend), Deg(r0.twist));
    // (The stand-in's hand is palm up: its twist is about +100 degrees at neutral, so a roll of +80 goes
    // past the hold at 150: 8c.)
    struct Test {
        float flex, dev, twist;
    } tests[] = {{30, 0, 0}, {40, 0, 0}, {-40, 0, 0}, {0, 30, 0}, {0, -20, 0}, {0, 0, -80}};
    double worst = 0, movedMost = 0;
    int held = 0;
    std::string readings;
    for (const Test& t : tests) {
        float bent[12];
        std::memcpy(bent, hand, sizeof(bent));
        armik::BendWrist(bent, false, t.flex, t.dev, t.twist);  // "test bend"
        armik::State st;
        armik::Result r;
        armik::Solve(bent, shoulder, out, up, back, false, p, st, r);
        const double twistWant = std::remainder(Deg(r0.twist) + t.twist, 360.0);
        worst = std::max({worst, std::fabs(Deg(r.flexion) - t.flex), std::fabs(Deg(r.deviation) - t.dev),
                          std::fabs(Deg(r.twist) - twistWant)});
        movedMost = std::max(movedMost, Len(FromF(r.elbow) - e));
        held += r.clamped || r.twistHeld;
        char one[96];
        std::snprintf(one, sizeof(one), " (%g %g %g)->(%+.1f %+.1f %+.1f)", t.flex, t.dev, t.twist, Deg(r.flexion),
                      Deg(r.deviation), Deg(r.twist) - Deg(r0.twist));
        readings += one;
    }
    Report("8b", "the stand-in's test bends", worst <= 0.5 && movedMost <= 0.001 && held == 0,
           "read within %.2f deg of the bends (<= 0.5), the forearm's elbow end moved %.2f mm at most (<= 1), %d held; "
           "bent (flexion ulnar twist)->read:%s",
           worst, movedMost * 1000, held, readings.c_str());
    // "test bend 0 0 80" as the game runs it, the state carried on from the neutral hand's frame: the
    // twist unwound past 180 and held at +150.
    {
        float bent[12];
        std::memcpy(bent, hand, sizeof(bent));
        armik::BendWrist(bent, false, 0, 0, 80);
        armik::State st;
        armik::Result r;
        armik::Solve(hand, shoulder, out, up, back, false, p, st, r);
        armik::Solve(bent, shoulder, out, up, back, false, p, st, r);
        const double most = Deg(p.maxTwist);
        Report("8c", "the stand-in's roll past the hold", r.twistHeld && std::fabs(Deg(r.twist) - most) <= 0.5,
               "neutral %+.1f deg rolled 80 more: twist %+.1f%s (held at %+.0f wanted)", Deg(r0.twist), Deg(r.twist),
               r.twistHeld ? " held" : "", most);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "E:\\BF3 CLAUDE VR PROJECT\\build";
    const armik::Params p;  // the left's
    std::printf("armtest: src/armik.h, the forearm from its hand. Upper arm %.4f m, forearm %.4f m; the left's pole "
                "out %.1f down %.1f back %.1f, wrist range flexion %.0f extension %.0f radial %.0f ulnar %.0f, twist "
                "shares %.3f/%.3f, held from %.0f deg.\n",
                armik::kUpper, armik::kFore, p.poleOut, p.poleDown, p.poleBack, p.flex, p.ext, p.radial, p.ulnar,
                p.share[1], p.share[2], Deg(p.maxTwist));
    BindAndRigid();
    GameHold(dir);
    const Pose pose = MakePose(p);
    TwistSweep(pose, p);
    Bends(pose, p);
    Degenerate(pose, p);
    Cost(pose, p);
    StandIn(p);
    std::printf("%d of %d checks failed%s\n", g_failed, g_counted,
                g_skipped ? " (the game's hold skipped: no mesh dumps)" : "");
    return g_failed;
}
