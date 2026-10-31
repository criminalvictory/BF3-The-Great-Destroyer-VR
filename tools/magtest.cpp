// The magazine taken from the pouch at the pelvis and pushed into the M416's well by the left hand
// (src/pouch.cpp, as hands.cpp runs it), on scripted frames at 72 Hz, without the game: the squeeze
// that takes one (the gun's state, the hand, the handguard), the zone and the body's heading under
// it, the funnel below the well's mouth (what's caught and what isn't), the ride up the line and the
// seat, tracking jumps, letting go, the hand lost, pulled off the line.
// The room is OpenXR LOCAL (y up, metres): the head level at (0 1.6 0) faces -z, and turns about the
// neck point the pouch goes by (7.5 cm below and 8 cm behind the eyes), as a head does. The view is
// the head's own space (x right, y up, z back); the gun is held still in it, turned (170 degrees
// about the view's up, the muzzle 8 degrees up, canted 12) so the module's matrices are tried off
// their axes. The gun's model space is x left, y up, z to the muzzle.
// The left hand is the magazine it holds: the glove holding it there comes from pouch::GloveFor,
// and the grip point (the fist's middle, what the zone goes by) is the hand's middle in the judge's
// fit, (0.045 -0.140 0.425) in the gun's space with the magazine seated, put into the room.
// The magazine's top middle in the gun's space gives s, its place along the well's line from home
// (0 seated, -8.7 cm at the mouth), h = -0.087 - s its depth below the mouth, lateral its distance
// from the line; angle is its whole turn from seated. "Drawn" is Result::magPose, the magazine as
// the mod draws it; its move is the most its top or its bottom's corners move in a frame.
// The module's state is its own (no reset): each check starts it afresh (Clear, the squeeze open,
// the head turned half round and back so the body's heading snaps to it).
// 1-9 are the checks asked for; 10-13 more of the design (the judge's synthesis, sections 4-8 and
// 11), counted too; those marked info are shown, not counted.
// Usage: magtest [-v ID]   (-v prints that check frame by frame, e.g. -v 4a)
// The exit code is the number of failed checks.

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

#include "../src/pouch.h"

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDegree = kPi / 180.0f;
constexpr float kDt = 1.0f / 72.0f;  // a frame at 72 Hz (13.9 ms)

// pouch.cpp's numbers (the design's): the seated magazine's top middle in the gun's space, s at the
// well's mouth, the funnel's depth below it, home and the most it moves a frame on the line.
constexpr float kTop[3] = {0.0f, 0.0413f, 0.415f};
constexpr float kMouth = -0.087f;
constexpr float kFunnelDepth = 0.06f;
constexpr float kHome = 0.005f;
constexpr float kMaxStep = 0.03f;
// The pouch: its middle that far below the neck and in front of it, its half sizes across, up and
// down, and front to back; the neck from the eyes in the head's axes; the neck with the eyes at
// (0 1.6 0), level.
constexpr float kZone[5] = {0.60f, 0.19f, 0.16f, 0.13f, 0.15f};
constexpr float kNeck[3] = {0.0f, -0.075f, 0.08f};
constexpr float kNeck0[3] = {0.0f, 1.525f, 0.08f};
// The hand's middle in the magazine's space (the gun's, with it seated; the judge's fit).
constexpr float kHandMiddle[3] = {0.045f, -0.140f, 0.425f};
// The magazine's top middle and its bottom's front and back corners: its moves are theirs.
constexpr float kMagPoints[3][3] = {{0.0f, 0.0413f, 0.415f}, {0.0f, -0.173f, 0.474f}, {0.0f, -0.173f, 0.381f}};
constexpr float kEps = 1e-5f;

std::string g_verbose;  // -v: the check printed frame by frame

float Clamp01(float v) {
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}
float Ease(float u) {  // 0 to 1, eased in and out
    u = Clamp01(u);
    return u * u * (3.0f - 2.0f * u);
}
int Frames(float seconds) {
    return static_cast<int>(std::lround(seconds / kDt));
}
float Distance(const float* a, const float* b) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}
float Wrap(float degrees) {
    return std::remainder(degrees, 360.0f);
}

// ---- Rigid poses (3x4, rows: rotation | translation) ----

struct Pose {
    float m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
};

Pose FromArray(const float* a) {
    Pose p;
    std::memcpy(p.m, a, sizeof(p.m));
    return p;
}

Pose Mul(const Pose& a, const Pose& b) {
    Pose out;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j)
            out.m[i * 4 + j] = a.m[i * 4] * b.m[j] + a.m[i * 4 + 1] * b.m[4 + j] + a.m[i * 4 + 2] * b.m[8 + j];
        out.m[i * 4 + 3] += a.m[i * 4 + 3];
    }
    return out;
}

Pose Inverse(const Pose& a) {
    Pose out;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out.m[i * 4 + j] = a.m[j * 4 + i];
    for (int i = 0; i < 3; ++i)
        out.m[i * 4 + 3] = -(out.m[i * 4] * a.m[3] + out.m[i * 4 + 1] * a.m[7] + out.m[i * 4 + 2] * a.m[11]);
    return out;
}

void Apply(const Pose& a, const float* p, float* out) {
    for (int i = 0; i < 3; ++i)
        out[i] = a.m[i * 4] * p[0] + a.m[i * 4 + 1] * p[1] + a.m[i * 4 + 2] * p[2] + a.m[i * 4 + 3];
}

Pose Move(float x, float y, float z) {
    Pose p;
    p.m[3] = x, p.m[7] = y, p.m[11] = z;
    return p;
}

// A turn of degrees about the axis (x y z) through the origin (right-handed).
Pose Turn(float x, float y, float z, float degrees) {
    const float n = std::sqrt(x * x + y * y + z * z);
    x /= n, y /= n, z /= n;
    const float c = std::cos(degrees * kDegree), s = std::sin(degrees * kDegree), t = 1.0f - c;
    const float r[9] = {t * x * x + c,     t * x * y - s * z, t * x * z + s * y, t * x * y + s * z, t * y * y + c,
                        t * y * z - s * x, t * x * z - s * y, t * y * z + s * x, t * z * z + c};
    Pose p;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            p.m[i * 4 + j] = r[i * 3 + j];
    return p;
}

// ... about that axis through a point.
Pose TurnAbout(float x, float y, float z, float degrees, const float* point) {
    return Mul(Mul(Move(point[0], point[1], point[2]), Turn(x, y, z, degrees)), Move(-point[0], -point[1], -point[2]));
}

// How far a pose turns (degrees, 0-180).
float Angle(const Pose& a) {
    return std::acos(std::clamp((a.m[0] + a.m[5] + a.m[10] - 1.0f) * 0.5f, -1.0f, 1.0f)) / kDegree;
}

// The most any of the magazine's points is apart between two poses (metres).
float Apart(const Pose& a, const Pose& b) {
    float most = 0.0f;
    for (const float* p : kMagPoints) {
        float pa[3], pb[3];
        Apply(a, p, pa);
        Apply(b, p, pb);
        most = std::max(most, Distance(pa, pb));
    }
    return most;
}

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

Quat AxisQuat(float x, float y, float z, float degrees) {
    const float n = std::sqrt(x * x + y * y + z * z), h = degrees * kDegree * 0.5f, s = std::sin(h) / n;
    return {x * s, y * s, z * s, std::cos(h)};
}

Quat QMul(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

void SetTurn(Pose& p, Quat q) {
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    const float x = q.x / n, y = q.y / n, z = q.z / n, w = q.w / n;
    const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                        2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                        2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            p.m[i * 4 + j] = r[i * 3 + j];
}

Quat ToQuat(const Pose& p) {
    const float* m = p.m;
    const float trace = m[0] + m[5] + m[10];
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        return {(m[9] - m[6]) / s, (m[2] - m[8]) / s, (m[4] - m[1]) / s, 0.25f * s};
    }
    if (m[0] > m[5] && m[0] > m[10]) {
        const float s = std::sqrt(1.0f + m[0] - m[5] - m[10]) * 2.0f;
        return {0.25f * s, (m[1] + m[4]) / s, (m[2] + m[8]) / s, (m[9] - m[6]) / s};
    }
    if (m[5] > m[10]) {
        const float s = std::sqrt(1.0f + m[5] - m[0] - m[10]) * 2.0f;
        return {(m[1] + m[4]) / s, 0.25f * s, (m[6] + m[9]) / s, (m[2] - m[8]) / s};
    }
    const float s = std::sqrt(1.0f + m[10] - m[0] - m[5]) * 2.0f;
    return {(m[2] + m[8]) / s, (m[6] + m[9]) / s, 0.25f * s, (m[4] - m[1]) / s};
}

Quat Slerp(const Quat& a, Quat b, float u) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0f)
        b = {-b.x, -b.y, -b.z, -b.w}, d = -d;
    float wa = 1.0f - u, wb = u;
    if (d < 0.9995f) {
        const float th = std::acos(d), s = std::sin(th);
        wa = std::sin((1.0f - u) * th) / s, wb = std::sin(u * th) / s;
    }
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

// The magazine's pose between two (u 0-1): turned along the way, the hand's middle on a line.
Pose Between(const Pose& a, const Pose& b, float u) {
    Pose p;
    SetTurn(p, Slerp(ToQuat(a), ToQuat(b), u));
    float ha[3], hb[3], turned[3];
    Apply(a, kHandMiddle, ha);
    Apply(b, kHandMiddle, hb);
    Apply(p, kHandMiddle, turned);  // its translation still 0
    for (int i = 0; i < 3; ++i)
        p.m[i * 4 + 3] = ha[i] + (hb[i] - ha[i]) * u - turned[i];
    return p;
}

// ---- The head, the neck and the pouch ----

// The head turned about the neck point the pouch goes by: yaw left, pitch up, roll (degrees).
XrPosef HeadAt(const float* neck, float yaw, float pitch, float roll = 0.0f) {
    const Quat q = QMul(QMul(AxisQuat(0, 1, 0, yaw), AxisQuat(1, 0, 0, pitch)), AxisQuat(0, 0, 1, roll));
    Pose turn;
    SetTurn(turn, q);
    float off[3];
    Apply(turn, kNeck, off);
    XrPosef p{};
    p.orientation = {q.x, q.y, q.z, q.w};
    p.position = {neck[0] - off[0], neck[1] - off[1], neck[2] - off[2]};
    return p;
}

Pose FromXr(const XrPosef& x) {
    Pose p;
    SetTurn(p, {x.orientation.x, x.orientation.y, x.orientation.z, x.orientation.w});
    p.m[3] = x.position.x, p.m[7] = x.position.y, p.m[11] = x.position.z;
    return p;
}

// The pouch's middle with the body's heading yaw (degrees, left of -z).
void MiddleFor(const float* neck, float yaw, float* out) {
    out[0] = neck[0] - std::sin(yaw * kDegree) * kZone[1];
    out[1] = neck[1] - kZone[0];
    out[2] = neck[2] - std::cos(yaw * kDegree) * kZone[1];
}

// A point in the pouch's axes (right, up, forward from its middle; metres), into the room.
void PouchPoint(const float* neck, float yaw, float right, float up, float forward, float* out) {
    float m[3];
    MiddleFor(neck, yaw, m);
    const float a = yaw * kDegree;
    const float r[3] = {std::cos(a), 0.0f, -std::sin(a)}, f[3] = {-std::sin(a), 0.0f, -std::cos(a)};
    for (int i = 0; i < 3; ++i)
        out[i] = m[i] + right * r[i] + forward * f[i];
    out[1] += up;
}

// How far out of the pouch a room point is (1 its edge), the body's heading yaw.
float SizeAt(const float* neck, float yaw, const float* p) {
    float m[3];
    MiddleFor(neck, yaw, m);
    const float a = yaw * kDegree;
    const float d[3] = {p[0] - m[0], p[1] - m[1], p[2] - m[2]};
    const float right = d[0] * std::cos(a) - d[2] * std::sin(a), forward = -d[0] * std::sin(a) - d[2] * std::cos(a);
    return std::sqrt((right / kZone[2]) * (right / kZone[2]) + (d[1] / kZone[3]) * (d[1] / kZone[3]) +
                     (forward / kZone[4]) * (forward / kZone[4]));
}

// The body's heading the module went by (degrees), from a squeeze's Result::fromMiddle with the
// grip at `grip`: the grip from the neck, in the body's axes, is (fromMiddle right, its forward +
// the pouch's 19 cm).
float HeadingFrom(const float* neck, const float* grip, const float* fromMiddle) {
    const float alpha = std::atan2(-(grip[0] - neck[0]), -(grip[2] - neck[2]));
    return Wrap((alpha + std::atan2(fromMiddle[0], fromMiddle[2] + kZone[1])) / kDegree);
}

// ---- The magazine's place by the well ----

struct Place {
    float top[3];
    float s, h, lateral, angle;
};

Place PlaceOf(const Pose& inGun) {
    Place p;
    Apply(inGun, kTop, p.top);
    p.s = p.top[1] - kTop[1];
    p.h = kMouth - p.s;
    p.lateral = std::sqrt(p.top[0] * p.top[0] + (p.top[2] - kTop[2]) * (p.top[2] - kTop[2]));
    p.angle = Angle(inGun);
    return p;
}

// The gun as the right hand holds it still in the view (its model space into the view's).
Pose GunPose() {
    return Mul(Mul(Mul(Move(0.10f, -0.28f, -0.20f), Turn(0, 1, 0, 170.0f)), Turn(1, 0, 0, -8.0f)), Turn(0, 0, 1, 12.0f));
}

// The magazine upright in the room, its front forward (-z), its left to the left.
Pose Upright() {
    return Turn(0, 1, 0, 180.0f);
}

Pose g_gloveOfSeated;      // the glove holding a magazine at the gun's own pose (GloveFor: hold^-1)
float g_handInGlove[3] = {};  // the hand's middle in the gloves' bind space

// ---- The scripted frames ----

struct Sim {
    std::string id;
    bool verbose = false;
    pouch::Frame f;   // this frame's inputs, but dt, the glove, the gun's pose and (unless given) the grip
    Pose head;        // the room <- the view (f.head)
    Pose gun;         // the view <- the gun's model space
    Pose mag;         // the magazine as the hand holds it, in the view
    bool gripGiven = false;
    bool zeroGun = false;
    int frame = 0;
    float t = 0.0f;
    pouch::Result r;
    bool line = false;  // on the well's line: caught, and not off it, seated, let go or gone since
    bool drawnBefore = false;
    float before[3][3] = {};
    float move = 0.0f;  // the drawn magazine's this frame (0 unless drawn this frame and the last)
    int took = 0, caught = 0, seated = 0, offLine = 0, letGo = 0, putBack = 0, cancelled = 0;

    void Head(const XrPosef& p) {
        f.head = p;
        head = FromXr(p);
    }
    void InGun(const Pose& e) { mag = Mul(gun, e); }
    // The hand's middle at p in the room, the magazine turned so in the room.
    void AtGrip(const float* p, const Pose& turn) {
        Pose room = turn;
        float o[3];
        Apply(turn, kHandMiddle, o);
        room.m[3] = p[0] - o[0], room.m[7] = p[1] - o[1], room.m[11] = p[2] - o[2];
        mag = Mul(Inverse(head), room);
    }
    Pose HandInGun() const { return Mul(Inverse(gun), mag); }
    bool Drawn(Pose& e) const {
        if (!r.mag)
            return false;
        e = Mul(Inverse(gun), FromArray(r.magPose));
        return true;
    }

    const pouch::Result& Step() {
        f.dt = kDt;
        pouch::GloveFor(mag.m, f.glove);
        std::memcpy(f.gunPose, gun.m, sizeof(f.gunPose));
        if (zeroGun)
            std::memset(f.gunPose, 0, sizeof(f.gunPose));  // hands.cpp: t.gunPose is all zeros without t.gun
        if (!gripGiven) {
            float v[3], p[3];
            Apply(mag, kHandMiddle, v);
            Apply(head, v, p);
            f.leftGrip = {p[0], p[1], p[2]};
        }
        r = pouch::Update(f);
        ++frame;
        t += kDt;
        took += r.took, caught += r.caught, seated += r.seated, offLine += r.offLine;
        letGo += r.letGo, putBack += r.putBack, cancelled += r.cancelled;
        if (r.caught)
            line = true;
        if (r.offLine || r.seated || r.letGo || r.putBack || r.cancelled)
            line = false;
        move = 0.0f;
        Pose e;
        if (Drawn(e)) {
            float now[3][3];
            for (int k = 0; k < 3; ++k)
                Apply(e, kMagPoints[k], now[k]);
            if (drawnBefore)
                for (int k = 0; k < 3; ++k)
                    move = std::max(move, Distance(now[k], before[k]));
            std::memcpy(before, now, sizeof(now));
            drawnBefore = true;
        } else {
            drawnBefore = false;
        }
        if (verbose)
            Print();
        return r;
    }

    void Print() const {
        const Place p = PlaceOf(HandInGun());
        std::printf("%-4s %4d %6.3f sq %.2f %s | hand s %6.1f h %6.1f lat %5.1f ang %5.1f", id.c_str(), frame, t,
                    f.squeeze, f.left ? "L" : "-", p.s * 1000, p.h * 1000, p.lateral * 1000, p.angle);
        Pose e;
        if (Drawn(e)) {
            const Place d = PlaceOf(e);
            std::printf(" | drawn s %6.1f lat %5.1f ang %5.1f move %5.1f", d.s * 1000, d.lateral * 1000, d.angle,
                        move * 1000);
        } else {
            std::printf(" | not drawn                                    ");
        }
        std::printf(" | blend %.2f%s%s%s%s%s%s%s%s%s%s %s\n", r.gloveBlend, r.holding ? " holding" : "",
                    r.reached ? " REACHED" : "", r.took ? " TOOK" : "", r.caught ? " CAUGHT" : "",
                    r.offLine ? " OFFLINE" : "", r.seated ? " SEATED" : "", r.letGo ? " LETGO" : "",
                    r.putBack ? " PUTBACK" : "", r.cancelled ? " CANCELLED" : "", line ? " (line)" : "",
                    *r.note ? r.note : "");
    }
};

// The module afresh: nothing held, the squeeze open (armed, nothing crossed), the glove's blend gone,
// the gun's magazine counted in, the body's heading snapped to a level head at (0 1.6 0) (a turn of
// more than 45 degrees in a frame is a recentre). Then the script's defaults: the gun hot (its
// magazine out), the hand free, spare rounds, the palm far from the handguard.
void Fresh(Sim& sim, const char* id) {
    pouch::Clear();
    pouch::SetZone(kZone, true);
    pouch::Frame f;
    f.dt = kDt;
    f.left = true;
    f.leftGrip = {3.0f, 0.0f, 3.0f};
    f.head = HeadAt(kNeck0, 180.0f, 0.0f);
    pouch::Update(f);
    f.head = HeadAt(kNeck0, 0.0f, 0.0f);
    for (int i = 0; i < 4; ++i)
        pouch::Update(f);
    sim = Sim{};
    sim.id = id;
    sim.verbose = !g_verbose.empty() && g_verbose == id;
    sim.gun = GunPose();
    sim.Head(HeadAt(kNeck0, 0.0f, 0.0f));
    sim.f.left = true;
    sim.f.gun = true;
    sim.f.wellEmpty = true;
    sim.f.dry = false;
    sim.f.spare = true;
    sim.f.handFree = true;
    sim.f.guardDistance = 1.0f;
    sim.InGun(Move(0.0f, -0.20f, 0.0f));
}

void Still(Sim& sim, float seconds, const std::function<void()>& each = {}) {
    for (int i = 0, n = Frames(seconds); i < n; ++i) {
        sim.Step();
        if (each)
            each();
    }
}

// A magazine taken where the hand is, as pouch.txt "test" does (the pouch counted as reached): the
// squeeze open 3 frames, a fresh squeeze, 3 frames more held still.
bool TakeHere(Sim& sim) {
    sim.f.squeeze = 0.0f;
    sim.f.forceInside = true;
    for (int i = 0; i < 3; ++i)
        sim.Step();
    sim.f.squeeze = 1.0f;
    sim.Step();
    const bool took = sim.r.took;
    sim.f.forceInside = false;
    for (int i = 0; i < 3; ++i)
        sim.Step();
    return took && pouch::Holding();
}

// A magazine taken with the grip at `at` (a fresh squeeze there), held upright facing forward.
bool TakeAt(Sim& sim, const float* at) {
    sim.AtGrip(at, Upright());
    sim.f.squeeze = 0.0f;
    for (int i = 0; i < 3; ++i)
        sim.Step();
    sim.f.squeeze = 1.0f;
    sim.Step();
    const bool took = sim.r.took;
    for (int i = 0; i < 3; ++i)
        sim.Step();
    return took && pouch::Holding();
}

// The hand moves the magazine along the well's line (the gun's +y) from s0 to s1 at v m/s, the rest
// of its pose `shape` (its pose with its top's place at home); each() after every frame.
void Slide(Sim& sim, const Pose& shape, float s0, float s1, float v, const std::function<void()>& each = {}) {
    const float step = v * kDt;
    float s = s0;
    while (s != s1) {
        s = s1 > s0 ? std::min(s + step, s1) : std::max(s - step, s1);
        sim.InGun(Mul(Move(0.0f, s, 0.0f), shape));
        sim.Step();
        if (each)
            each();
    }
}

// What the drawn magazine did over a push into the well.
struct Watch {
    float caughtH = -1.0f, caughtAngle = 0.0f, caughtLateral = 0.0f, caughtAt = -1.0f;
    float seatedAt = -1.0f, offS = 1.0f, offH = 0.0f, offLateral = 0.0f, offAngle = 0.0f, offJump = 0.0f;
    float drawnSMost = -1.0f;  // on the line
    float moveMost = 0.0f;     // any frame it's drawn this frame and the last
    float mouthAngle = -1.0f, mouthLateral = -1.0f;  // on the line, the first frame at the mouth or above
    float wellAngleMost = 0.0f, wellLateralMost = 0.0f;  // on the line, at the mouth or above
    float gloveOff = 0.0f;  // the glove the module draws from the one holding the drawn magazine (at the hand)

    void After(Sim& sim) {
        if (sim.r.seated)
            sim.f.wellEmpty = false;  // the game: the magazine is in (skin::InsertMagazine)
        const Place hand = PlaceOf(sim.HandInGun());
        if (sim.r.caught)
            caughtH = hand.h, caughtAngle = hand.angle, caughtLateral = hand.lateral, caughtAt = sim.t;
        if (sim.r.offLine && offS > 0.5f)
            offS = hand.s, offH = hand.h, offLateral = hand.lateral, offAngle = hand.angle, offJump = sim.move;
        if (sim.r.seated)
            seatedAt = sim.t;
        moveMost = std::max(moveMost, sim.move);
        Pose e;
        if (sim.line && sim.Drawn(e)) {
            const Place d = PlaceOf(e);
            drawnSMost = std::max(drawnSMost, d.s);
            if (d.s >= kMouth - kEps) {
                if (mouthAngle < 0.0f)
                    mouthAngle = d.angle, mouthLateral = d.lateral;
                wellAngleMost = std::max(wellAngleMost, d.angle);
                wellLateralMost = std::max(wellLateralMost, d.lateral);
            }
            if (sim.r.gloveBlend > 0.0f) {
                float g[12], a[3], b[3];
                pouch::GloveFor(sim.r.magPose, g);
                Apply(FromArray(g), g_handInGlove, a);
                Apply(FromArray(sim.r.glove), g_handInGlove, b);
                gloveOff = std::max(gloveOff, Distance(a, b));
            }
        }
    }
};

// ---- Reporting ----

int g_failed = 0, g_failedAsked = 0, g_counted = 0, g_infoOff = 0;

void Report(const char* id, const char* what, bool pass, bool info, const char* format, ...) {
    char numbers[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(numbers, sizeof(numbers), format, args);
    va_end(args);
    std::printf("%-4s %-5s %s | %s\n", id, info ? (pass ? "info" : "info*") : pass ? "PASS" : "FAIL", what, numbers);
    if (info) {
        g_infoOff += !pass;
        return;
    }
    ++g_counted;
    if (!pass) {
        ++g_failed;
        if (std::atoi(id) <= 9)
            ++g_failedAsked;
    }
}

const char* YesNo(bool b) {
    return b ? "yes" : "no";
}

// ---- 1. Taking a magazine at the pouch's middle ----

void Scenario1() {
    struct Case {
        const char* id;
        const char* what;
        bool empty, dry, free, spare;
        float guard;
        bool gun, take;
        const char* why;
    };
    const Case cases[] = {
        {"1a", "at the pouch's middle, the gun hot (its magazine out), the hand free: takes one", true, false, true,
         true, 1.0f, true, true, "took a magazine"},
        {"1b", "dry with its magazine in (the bolt back: hot too): takes one", false, true, true, true, 1.0f, true,
         true, "took a magazine"},
        {"1c", "its magazine in and not dry: nothing (X first)", false, false, true, true, 1.0f, true, false,
         "X first"},
        {"1d", "the hand not free (handFree false): nothing", true, false, false, true, 1.0f, true, false, "busy"},
        {"1e", "no spare rounds: nothing", true, false, true, false, 1.0f, true, false, "no rounds"},
        {"1f", "the palm 8 cm from the handguard's line (guardDistance 0.08): nothing", true, false, true, true, 0.08f,
         true, false, "handguard"},
        {"1g", "no gun the pouch knows (gun false): nothing", true, false, true, true, 1.0f, false, false, "no gun"},
    };
    float middle[3];
    MiddleFor(kNeck0, 0.0f, middle);
    for (const Case& c : cases) {
        Sim sim;
        Fresh(sim, c.id);
        sim.f.wellEmpty = c.empty, sim.f.dry = c.dry, sim.f.handFree = c.free, sim.f.spare = c.spare;
        sim.f.guardDistance = c.guard, sim.f.gun = c.gun;
        sim.AtGrip(middle, Upright());
        sim.f.squeeze = 0.0f;
        Still(sim, 5 * kDt);
        sim.f.squeeze = 1.0f;
        bool reached = false;
        float size = -1.0f;
        std::string note;
        for (int i = 0; i < 12; ++i) {
            sim.Step();
            if (sim.r.reached)
                reached = true, size = sim.r.size, note = sim.r.note;
        }
        const bool pass = reached && sim.took == (c.take ? 1 : 0) && note.find(c.why) != std::string::npos &&
                          pouch::Holding() == c.take;
        Report(c.id, c.what, pass, false,
               "grip (%.3f %.3f %.3f): the squeeze reached it %s (%.2f of its size), took %d, holding %s, \"%s\"",
               sim.f.leftGrip.x, sim.f.leftGrip.y, sim.f.leftGrip.z, YesNo(reached), size, sim.took,
               YesNo(pouch::Holding()), note.c_str());
    }
}

// ---- 2. The squeeze's edge and its 0.15 s ----

void Scenario2() {
    float middle[3];
    MiddleFor(kNeck0, 0.0f, middle);
    {
        Sim sim;
        Fresh(sim, "2a");
        float rest[3];
        PouchPoint(kNeck0, 0.0f, 0.0f, 0.25f, 0.11f, rest);  // in front of the belly, 27 cm from its middle
        sim.AtGrip(rest, Upright());
        sim.f.squeeze = 0.0f;
        Still(sim, 0.1f);
        sim.f.squeeze = 1.0f;
        sim.Step();
        const float crossedAt = sim.t;
        Still(sim, 0.5f);
        float enteredAt = -1.0f;
        for (int i = 1, n = Frames(0.3f); i <= n; ++i) {
            float p[3];
            const float u = Ease(static_cast<float>(i) / n);
            for (int k = 0; k < 3; ++k)
                p[k] = rest[k] + (middle[k] - rest[k]) * u;
            sim.AtGrip(p, Upright());
            sim.Step();
            if (enteredAt < 0.0f && SizeAt(kNeck0, 0.0f, p) <= 1.0f)
                enteredAt = sim.t;
        }
        Still(sim, 0.5f);
        Report("2a", "a squeeze already held while the hand sweeps into the pouch: nothing", sim.took == 0, false,
               "squeezed %.2f s before the hand came in, took %d", enteredAt - crossedAt, sim.took);
    }
    for (int k = 0; k < 2; ++k) {
        const char* id = k == 0 ? "2b" : "2c";
        const float v = k == 0 ? 0.5f : 0.25f, out = 0.05f;
        Sim sim;
        Fresh(sim, id);
        float start[3];
        PouchPoint(kNeck0, 0.0f, 0.0f, 0.0f, kZone[4] + out, start);  // 5 cm in front of its front
        sim.AtGrip(start, Upright());
        sim.f.squeeze = 0.0f;
        Still(sim, 0.1f);
        sim.f.squeeze = 1.0f;
        sim.Step();
        int lines = sim.r.squeezed;  // the log lines this squeeze gives (pouch.cpp: one, when it settles)
        const float crossedAt = sim.t, crossedSize = SizeAt(kNeck0, 0.0f, start);
        float enteredAt = -1.0f, tookAt = sim.r.took ? sim.t : -1.0f, d = 0.0f;
        while (d < kZone[4] + out) {
            d = std::min(d + v * kDt, kZone[4] + out);
            float p[3];
            PouchPoint(kNeck0, 0.0f, 0.0f, 0.0f, kZone[4] + out - d, p);
            sim.AtGrip(p, Upright());
            sim.Step();
            lines += sim.r.squeezed;
            if (enteredAt < 0.0f && SizeAt(kNeck0, 0.0f, p) <= 1.0f)
                enteredAt = sim.t;
            if (sim.r.took && tookAt < 0.0f)
                tookAt = sim.t;
        }
        Still(sim, 0.3f, [&] { lines += sim.r.squeezed; });
        const bool take = k == 0;
        const bool pass =
            sim.took == (take ? 1 : 0) && (!take || std::fabs(tookAt - enteredAt) < 1e-4f) && lines == 1;
        Report(id,
               take ? "squeezed 5 cm outside the pouch, the hand in 0.11 s later (0.5 m/s): takes one"
                    : "squeezed 5 cm outside the pouch, the hand in 0.21 s later (0.25 m/s): nothing",
               pass, false, "squeezed at %.2f of its size, the hand in %.3f s later, took %d%s; %d log line(s)",
               crossedSize, enteredAt - crossedAt, sim.took,
               tookAt >= 0.0f ? (tookAt == enteredAt ? " (as it came in)" : " (later)") : "", lines);
    }
}

// ---- 3. The body's heading ----

void Scenario3() {
    float middle0[3];
    MiddleFor(kNeck0, 0.0f, middle0);
    // Looking down 60 degrees and turning the head 30 left (slowly: 2 s, so the heading would settle
    // towards the head if it weren't held) with the hand in the pouch; probes (fresh squeezes every
    // other frame; no gun, so nothing's taken) give the heading the module went by.
    for (int k = 0; k < 2; ++k) {
        const bool reaching = k == 0;
        const char* id = reaching ? "3a" : "3c";
        Sim sim;
        Fresh(sim, id);
        sim.f.gun = false;
        const float rest[3] = {kNeck0[0], kNeck0[1] - 0.30f, kNeck0[2] - 0.35f};  // 34 cm from the pouch's middle
        float most = 0.0f, mostHeading = 0.0f;
        int probes = 0;
        const int reach = Frames(0.6f), turn = Frames(2.0f), hold = Frames(2.0f);
        for (int i = 1; i <= reach + turn + hold; ++i) {
            const float u = reaching ? Ease(static_cast<float>(i) / reach) : 0.0f;
            const float w = Ease(static_cast<float>(i - reach) / turn);
            sim.Head(HeadAt(kNeck0, 30.0f * w, -60.0f * w));
            float p[3];
            for (int j = 0; j < 3; ++j)
                p[j] = rest[j] + (middle0[j] - rest[j]) * u;
            sim.AtGrip(p, Upright());
            sim.f.squeeze = i % 2 == 0 ? 1.0f : 0.0f;
            sim.Step();
            if (!sim.r.squeezed)
                continue;
            const float heading = HeadingFrom(kNeck0, p, sim.r.fromMiddle);
            float m[3];
            MiddleFor(kNeck0, heading, m);
            ++probes;
            if (Distance(m, middle0) > most)
                most = Distance(m, middle0), mostHeading = heading;
        }
        if (reaching)
            Report("3a", "looking down 60 deg and turning the head 30 deg left while reaching: the pouch stays put",
                   most < 0.03f, false, "the pouch moved at most %.1f cm (the body's heading %.1f deg) over %d probes",
                   most * 100, mostHeading, probes);
        else
            Report("3c", "the same head turn with the hand 34 cm off (not reaching): the pouch settles after the head",
                   true, true, "the pouch moved %.1f cm (the body's heading %.1f deg) over %d probes", most * 100,
                   mostHeading, probes);
    }
    // The whole body turned 90 degrees in 1 s (the neck still, the head with it), the hand 30 cm
    // above the pouch's place for the new heading (not reaching), probed every other frame.
    {
        Sim sim;
        Fresh(sim, "3b");
        sim.f.gun = false;
        const int before = Frames(0.2f), turn = Frames(1.0f), after = Frames(10.0f);
        float lagMost = 0.0f, lagEnd = -1.0f, lag1 = -1.0f, lag10 = -1.0f, sizeEnd = 0.0f, size1 = 0.0f;
        for (int i = 1; i <= before + turn + after; ++i) {
            const float yaw = 90.0f * Ease(static_cast<float>(i - before) / turn);
            sim.Head(HeadAt(kNeck0, yaw, 0.0f));
            float p[3];
            PouchPoint(kNeck0, yaw, 0.0f, 0.30f, 0.0f, p);
            sim.AtGrip(p, Turn(0, 1, 0, 180.0f + yaw));
            sim.f.squeeze = i % 2 == 0 ? 1.0f : 0.0f;
            sim.Step();
            if (!sim.r.squeezed)
                continue;
            const float heading = HeadingFrom(kNeck0, p, sim.r.fromMiddle);
            const float lag = std::fabs(Wrap(yaw - heading));
            lagMost = std::max(lagMost, lag);
            const float since = (i - before - turn) * kDt;  // after the turn
            float front[3];
            MiddleFor(kNeck0, 90.0f, front);  // where the pouch is for the new heading
            if (since >= 0.0f && lagEnd < 0.0f)
                lagEnd = lag, sizeEnd = SizeAt(kNeck0, heading, front);
            if (since >= 1.0f && lag1 < 0.0f)
                lag1 = lag, size1 = SizeAt(kNeck0, heading, front);
            if (since >= 10.0f - 2 * kDt && lag10 < 0.0f)
                lag10 = lag;
        }
        const bool pass = lagMost <= 36.0f && sizeEnd <= 1.0f && lag10 >= 0.0f && lag10 <= 5.0f;
        Report("3b", "the whole body turned 90 deg over 1 s: the pouch follows", pass, false,
               "the heading at most %.1f deg behind; at the turn's end %.1f deg (the new pouch's middle at %.2f of "
               "the zone's size), 1 s later %.1f deg (%.2f), 10 s later %.1f deg",
               lagMost, lagEnd, sizeEnd, lag1, size1, lag10);
    }
}

// ---- 4. Pushed in upright ----

void Scenario4() {
    for (int k = 0; k < 2; ++k) {
        const char* id = k == 0 ? "4a" : "4b";
        const float v = k == 0 ? 0.3f : 2.0f;
        Sim sim;
        Fresh(sim, id);
        sim.InGun(Move(0.0f, -0.20f, 0.0f));
        const bool took = TakeHere(sim);
        Watch w;
        Slide(sim, Pose(), -0.20f, 0.01f, v, [&] { w.After(sim); });
        Still(sim, 0.3f, [&] { w.After(sim); });
        sim.f.squeeze = 0.0f;
        Still(sim, 0.2f, [&] { w.After(sim); });
        const bool pass = took && sim.caught == 1 && w.caughtH > 0.0f && w.caughtH <= kFunnelDepth + kEps &&
                          sim.seated == 1 && sim.offLine == 0 && sim.letGo == 0 && sim.cancelled == 0 &&
                          w.drawnSMost <= kEps && w.moveMost <= kMaxStep + kEps && w.gloveOff < 0.001f;
        char what[160];
        std::snprintf(what, sizeof(what),
                      "upright from 20 cm below home, pushed up the gun's +y at %.1f m/s: caught, rides, seats once", v);
        Report(id, what, pass, false,
               "took %s; caught %d at h %.1f cm; seated %d, %.2f s after the catch; the drawn s at most %+.2f mm, "
               "moved at most %.1f mm a frame; off the line %d, let go %d, gone %d; the module's glove %.2f mm from "
               "the one holding its magazine",
               YesNo(took), sim.caught, w.caughtH * 100, sim.seated, w.seatedAt - w.caughtAt, w.drawnSMost * 1000,
               w.moveMost * 1000, sim.offLine, sim.letGo, sim.cancelled, w.gloveOff * 1000);
    }
}

// ---- 5. Wrong entries ----

void Scenario5() {
    struct Wrong {
        const char* id;
        const char* what;
        Pose shape;
    };
    const Wrong wrongs[] = {
        {"5a", "backwards (turned 180 deg about the gun's y), pushed up and back: never caught", TurnAbout(0, 1, 0, 180.0f, kTop)},
        {"5b", "sideways (90 deg about y): never caught", TurnAbout(0, 1, 0, 90.0f, kTop)},
        {"5c", "upside down (180 deg about the gun's z): never caught", TurnAbout(0, 0, 1, 180.0f, kTop)},
        {"5d", "tilted 40 deg fore and aft (about x): never caught", TurnAbout(1, 0, 0, 40.0f, kTop)},
        {"5e", "tilted 40 deg sideways (about z): never caught", TurnAbout(0, 0, 1, 40.0f, kTop)},
        {"5f", "upright, 5 cm to the side of the line: never caught", Move(0.05f, 0.0f, 0.0f)},
        {"5g", "upright, 5 cm forward of the line: never caught", Move(0.0f, 0.0f, 0.05f)},
    };
    for (const Wrong& c : wrongs) {
        Sim sim;
        Fresh(sim, c.id);
        sim.InGun(Mul(Move(0.0f, -0.20f, 0.0f), c.shape));
        TakeHere(sim);
        float angleLeast = 999.0f, lateralLeast = 999.0f;
        auto each = [&] {
            const Place p = PlaceOf(sim.HandInGun());
            if (p.h >= 0.0f && p.h <= kFunnelDepth)
                angleLeast = std::min(angleLeast, p.angle), lateralLeast = std::min(lateralLeast, p.lateral);
        };
        Slide(sim, c.shape, -0.20f, 0.0f, 0.3f, each);
        Slide(sim, c.shape, 0.0f, -0.20f, 0.3f, each);
        Report(c.id, c.what, sim.caught == 0, false, "caught %d (in the funnel's depths the hand %.0f deg, %.1f cm off)",
               sim.caught, angleLeast, lateralLeast * 100);
    }
    {
        // The top 3 cm above the mouth (in the gun), down to 5 cm below the mouth (in the funnel, not
        // out of it) and back up.
        Sim sim;
        Fresh(sim, "5h");
        const float s0 = kMouth + 0.03f, s1 = kMouth - 0.05f;
        sim.InGun(Move(0.0f, s0, 0.0f));
        TakeHere(sim);
        float caughtH = -1.0f, sMost = -1.0f;
        bool down = true, caughtDown = false;
        auto each = [&] {
            if (sim.r.caught && caughtH < -0.5f)
                caughtH = PlaceOf(sim.HandInGun()).h, caughtDown = down;
            Pose e;
            if (sim.line && sim.Drawn(e))
                sMost = std::max(sMost, PlaceOf(e).s);
        };
        Slide(sim, Pose(), s0, s1, 0.3f, each);
        down = false;
        Slide(sim, Pose(), s1, s0, 0.3f, each);
        char numbers[256] = "";
        if (sim.caught)
            std::snprintf(numbers, sizeof(numbers), " at h %.1f cm (%s); on the line at the end %s, drawn up to s %.1f cm",
                          caughtH * 100, caughtDown ? "coming down" : "going up", YesNo(sim.line), sMost * 100);
        Report("5h", "down from above through the receiver (the top 3 cm above the mouth, down 8 cm, back up): never caught",
               sim.caught == 0, false, "caught %d%s", sim.caught, numbers);
    }
    {
        // In from the side above the mouth: the top 2 cm into the receiver, from 12 cm to the side.
        Sim sim;
        Fresh(sim, "5i");
        const float s = kMouth + 0.02f;
        sim.InGun(Move(0.12f, s, 0.0f));
        TakeHere(sim);
        for (float x = 0.12f; x > 0.0f;) {
            x = std::max(x - 0.3f * kDt, 0.0f);
            sim.InGun(Move(x, s, 0.0f));
            sim.Step();
        }
        Still(sim, 0.3f);
        Report("5i", "in from the side above the mouth (2 cm into the receiver): never caught", sim.caught == 0, false,
               "caught %d", sim.caught);
    }
}

// ---- 6. A tracking jump on the line ----

void Scenario6() {
    Sim sim;
    Fresh(sim, "6");
    sim.InGun(Move(0.0f, -0.20f, 0.0f));
    TakeHere(sim);
    Slide(sim, Pose(), -0.20f, -0.06f, 0.3f);
    Still(sim, 0.2f);
    const bool onBefore = sim.line;
    sim.InGun(Move(0.0f, -0.06f + 0.20f, 0.0f));  // 20 cm up the line for a frame
    sim.Step();
    const float moveJump = sim.move;
    const bool drawnJump = sim.r.mag, offJump = sim.r.offLine, seatedJump = sim.r.seated;
    sim.InGun(Move(0.0f, -0.06f, 0.0f));
    sim.Step();
    const float moveAfter = sim.move;
    Still(sim, 0.5f);
    Pose e;
    const bool drawn = sim.Drawn(e);
    const Place d = PlaceOf(e);
    const bool pass = onBefore && drawnJump && moveJump <= kMaxStep + kEps && moveAfter <= kMaxStep + kEps &&
                      sim.seated == 0 && !seatedJump;
    Report("6", "on the line 6 cm from home, the hand 20 cm up for one frame: moves <= 3 cm, no seat", pass, false,
           "on the line before %s; the jump frame: the drawn magazine moved %.1f cm (off the line %s), the frame "
           "after %.1f cm; seated %d, off the line %d in all; at the end %s, drawn at s %.1f cm, %.1f cm off the line",
           YesNo(onBefore), moveJump * 100, YesNo(offJump), moveAfter * 100, sim.seated, sim.offLine,
           sim.line ? "on the line" : "in the hand", drawn ? d.s * 100 : 0.0f, drawn ? d.lateral * 100 : 0.0f);
}

// ---- 7. Letting go ----

void Scenario7() {
    float middle[3];
    MiddleFor(kNeck0, 0.0f, middle);
    {
        Sim sim;
        Fresh(sim, "7a");
        const bool took = TakeAt(sim, middle);
        float air[3];
        PouchPoint(kNeck0, 0.0f, -0.30f, 0.25f, 0.10f, air);  // out to the left at the waist
        for (int i = 1, n = Frames(0.5f); i <= n; ++i) {
            float p[3];
            const float u = Ease(static_cast<float>(i) / n);
            for (int k = 0; k < 3; ++k)
                p[k] = middle[k] + (air[k] - middle[k]) * u;
            sim.AtGrip(p, Upright());
            sim.Step();
        }
        Still(sim, 0.1f);
        sim.f.squeeze = 0.3f;
        sim.Step();
        const pouch::Result r = sim.r;
        Report("7a", "let go (squeeze 0.3) in the air: letGo", took && r.letGo && !r.putBack && !pouch::Holding() &&
                                                                    sim.caught == 0,
               false, "took %s; at %.2f of the pouch's size: letGo %d, putBack %d, holding %s, caught on the way %d",
               YesNo(took), SizeAt(kNeck0, 0.0f, air), r.letGo, r.putBack, YesNo(pouch::Holding()), sim.caught);
    }
    {
        Sim sim;
        Fresh(sim, "7b");
        const bool took = TakeAt(sim, middle);
        Still(sim, 0.2f);
        sim.f.squeeze = 0.3f;
        sim.Step();
        const pouch::Result r = sim.r;
        Report("7b", "let go at the pouch: putBack", took && r.putBack && !r.letGo && !pouch::Holding(), false,
               "took %s; letGo %d, putBack %d, holding %s", YesNo(took), r.letGo, r.putBack, YesNo(pouch::Holding()));
    }
    {
        Sim sim;
        Fresh(sim, "7c");
        TakeHere(sim);
        Slide(sim, Pose(), -0.20f, -0.03f, 0.3f);
        Still(sim, 3 * kDt);
        const bool on = sim.line;
        sim.f.squeeze = 0.3f;
        sim.Step();
        const pouch::Result r = sim.r;
        Report("7c", "let go on the line 3 cm before home: letGo", on && r.letGo && !r.putBack && !r.seated &&
                                                                       sim.seated == 0 && !pouch::Holding(),
               false, "on the line %s; letGo %d, putBack %d, seated %d", YesNo(on), r.letGo, r.putBack, sim.seated);
    }
    {
        Sim sim;
        Fresh(sim, "7d");
        TakeHere(sim);
        float s = -0.20f, firstS = 0.0f;
        bool first = false, second = false;
        pouch::Result r;
        for (int i = 0; i < Frames(2.0f) && !second; ++i) {
            s = std::min(s + 0.3f * kDt, 0.005f);
            sim.InGun(Move(0.0f, s, 0.0f));
            sim.Step();
            Pose e;
            if (first) {
                r = sim.r, second = true;
            } else if (sim.line && sim.Drawn(e) && PlaceOf(e).s >= -kHome) {
                first = true, firstS = PlaceOf(e).s;
                sim.f.squeeze = 0.3f;  // let go on the next frame, the second at home
            }
        }
        Report("7d", "let go on the second frame at home: seated, not letGo", second && r.seated && !r.letGo && !r.putBack,
               false, "the first frame at home at s %.1f mm; the next (squeeze 0.3): seated %d, letGo %d, putBack %d",
               firstS * 1000, r.seated, r.letGo, r.putBack);
    }
    // Let go mid-swing in the air: it falls at the hand's speed (at most 3 m/s), along the swing (the
    // design's test 11.17; 7a's hand is still when it lets go).
    for (int k = 0; k < 3; ++k) {
        const char* id = k == 0 ? "7e" : k == 1 ? "7f" : "7g";
        const float speed = k == 0 ? 1.5f : k == 1 ? 5.0f : 0.6f;
        const float dir[3] = {k == 2 ? 0.0f : -0.8f, k == 2 ? -1.0f : 0.36f, k == 2 ? 0.0f : -0.48f};
        Sim sim;
        Fresh(sim, id);
        const bool took = TakeAt(sim, middle);
        float air[3];
        PouchPoint(kNeck0, 0.0f, -0.30f, 0.25f, 0.10f, air);
        for (int i = 1, n = Frames(0.5f); i <= n; ++i) {
            float p[3];
            const float u = Ease(static_cast<float>(i) / n);
            for (int c = 0; c < 3; ++c)
                p[c] = middle[c] + (air[c] - middle[c]) * u;
            sim.AtGrip(p, Upright());
            sim.Step();
        }
        const float n = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        float p[3] = {air[0], air[1], air[2]};
        for (int i = 0; i < 6; ++i) {
            for (int c = 0; c < 3; ++c)
                p[c] += dir[c] / n * speed * kDt;
            sim.AtGrip(p, Upright());
            if (i == 5)
                sim.f.squeeze = 0.3f;
            sim.Step();
        }
        const pouch::Result r = sim.r;
        const float want = std::min(speed, 3.0f);
        const float* v = r.dropVelocity;
        const float got = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        float along = 0.0f;
        for (int c = 0; c < 3; ++c)
            along += v[c] * dir[c] / n;
        Report(id, "let go in the air mid-swing: dropped at the hand's speed (design 11.17)",
               took && r.letGo && std::fabs(got - want) <= 0.1f * want && along >= 0.99f * got, false,
               "took %s; letGo %d; swing %.2f m/s, want %.2f; dropVelocity (%.3f %.3f %.3f) = %.3f m/s, %.3f along",
               YesNo(took), r.letGo, speed, want, v[0], v[1], v[2], got, along);
    }
}

// ---- 8. The hand lost, another magazine in ----

void Scenario8() {
    for (int k = 0; k < 2; ++k) {
        const char* id = k == 0 ? "8a" : "8b";
        const float lost = k == 0 ? 0.5f : 1.2f;
        Sim sim;
        Fresh(sim, id);
        TakeHere(sim);  // upright 20 cm below home, in the hand
        Still(sim, 0.1f);
        sim.f.left = false;
        const float lostAt = sim.t;
        bool heldLost = true, drawnLost = false;
        float goneAfter = -1.0f;
        for (int i = 0, n = Frames(lost); i < n; ++i) {
            sim.Step();
            if (sim.r.cancelled && goneAfter < 0.0f)
                goneAfter = sim.t - lostAt;
            if (goneAfter < 0.0f && !sim.r.holding)
                heldLost = false;
            drawnLost |= sim.r.mag;
        }
        sim.f.left = true;
        sim.Step();
        const bool backHolding = sim.r.holding && pouch::Holding(), backDrawn = sim.r.mag;
        Still(sim, 0.3f);
        if (k == 0)
            Report(id, "the left hand lost 0.5 s while held, then back: still held", sim.cancelled == 0 && heldLost &&
                                                                                      backHolding && backDrawn &&
                                                                                      pouch::Holding(),
                   false, "gone %d; held through the loss %s (drawn while lost %s); back: holding %s, drawn %s",
                   sim.cancelled, YesNo(heldLost), YesNo(drawnLost), YesNo(backHolding), YesNo(backDrawn));
        else
            Report(id, "the left hand lost 1.2 s while held: cancelled", sim.cancelled == 1 && goneAfter > 0.95f &&
                                                                             goneAfter < 1.05f && !backHolding &&
                                                                             sim.letGo == 0 && !pouch::Holding(),
                   false, "gone %d, %.3f s after the loss; back: holding %s; letGo %d", sim.cancelled, goneAfter,
                   YesNo(backHolding), sim.letGo);
    }
    {
        Sim sim;
        Fresh(sim, "8c");
        TakeHere(sim);
        Still(sim, 0.1f);
        sim.f.wellEmpty = false;  // X, the game's reload or a new life put one in
        sim.Step();
        const bool now = sim.r.cancelled;
        Still(sim, 0.2f);
        Report("8c", "another magazine going in (wellEmpty true -> false) while held: cancelled",
               now && sim.cancelled == 1 && !pouch::Holding() && sim.letGo == 0 && sim.seated == 0, false,
               "gone on that frame %s, gone %d in all, holding %s, letGo %d, seated %d", YesNo(now), sim.cancelled,
               YesNo(pouch::Holding()), sim.letGo, sim.seated);
    }
    {
        Sim sim;
        Fresh(sim, "8d");
        TakeHere(sim);
        Slide(sim, Pose(), -0.20f, -0.06f, 0.3f);
        Still(sim, 0.1f);
        const bool on = sim.line;
        sim.f.left = false;
        float sLeast = 1.0f, sMost = -1.0f;
        bool allDrawn = true;
        for (int i = 0, n = Frames(0.5f); i < n; ++i) {
            sim.Step();
            Pose e;
            if (sim.Drawn(e))
                sLeast = std::min(sLeast, PlaceOf(e).s), sMost = std::max(sMost, PlaceOf(e).s);
            else
                allDrawn = false;
        }
        sim.f.left = true;
        Still(sim, 0.2f);
        const bool pass = on && sim.cancelled == 0 && allDrawn && std::fabs(sLeast + 0.06f) < 0.001f &&
                          std::fabs(sMost + 0.06f) < 0.001f && sim.line;
        Report("8d", "(more) the hand lost 0.5 s on the line 6 cm from home: kept at its depth, drawn there", pass, false,
               "gone %d; drawn every lost frame %s, at s %.1f to %.1f cm; on the line after %s", sim.cancelled,
               YesNo(allDrawn), sLeast * 100, sMost * 100, YesNo(sim.line));
    }
}

// ---- 9. Pulled off the line ----

void Scenario9() {
    Sim sim;
    Fresh(sim, "9");
    TakeHere(sim);
    const float s = kMouth - 0.04f;  // caught, then held 4 cm below the mouth
    Slide(sim, Pose(), -0.20f, s, 0.3f);
    Still(sim, 0.1f);
    const bool on = sim.line;
    float offAt = -1.0f, jump = 0.0f, offFromHand = 0.0f, x = 0.0f;
    auto check = [&] {
        if (offAt < 0.0f)
            return;
        offFromHand = std::max(offFromHand, sim.r.mag ? Apart(FromArray(sim.r.magPose), sim.mag) : 1.0f);
    };
    while (x < 0.10f) {
        x = std::min(x + 0.5f * kDt, 0.10f);
        sim.InGun(Move(x, s, 0.0f));
        sim.Step();
        if (sim.r.offLine && offAt < 0.0f)
            offAt = x, jump = sim.move;
        check();
    }
    Still(sim, 0.3f, check);
    const bool pass = on && sim.offLine == 1 && offAt > 0.0f && pouch::Holding() && sim.r.holding && sim.r.mag &&
                      offFromHand < 0.001f && sim.caught == 1;
    Report("9", "caught 4 cm below the mouth, the hand moved 10 cm sideways: offLine, back in the hand", pass, false,
           "on the line before %s; off the line %d, with the hand %.1f cm to the side (the drawn magazine jumped "
           "%.1f cm to it); after: holding %s, drawn at the hand within %.2f mm; caught again %d",
           YesNo(on), sim.offLine, offAt * 100, jump * 100, YesNo(pouch::Holding()), offFromHand * 1000,
           sim.caught - 1);
}

// ---- 10. The zone's edges (more) ----

void Scenario10() {
    {
        Sim sim;
        Fresh(sim, "10a");
        sim.f.gun = false;
        struct Face {
            const char* name;
            float right, up, forward;
        };
        const Face faces[] = {{"right", 1, 0, 0}, {"left", -1, 0, 0}, {"up", 0, 1, 0},
                              {"down", 0, -1, 0}, {"front", 0, 0, 1}, {"back", 0, 0, -1}};
        int wrong = 0;
        std::string misses;
        for (const Face& face : faces)
            for (const float k : {0.95f, 1.05f}) {
                float p[3];
                PouchPoint(kNeck0, 0.0f, face.right * kZone[2] * k, face.up * kZone[3] * k, face.forward * kZone[4] * k, p);
                sim.AtGrip(p, Upright());
                sim.f.squeeze = 0.0f;
                Still(sim, 2 * kDt);
                sim.f.squeeze = 1.0f;
                sim.Step();
                if (sim.r.reached != (k < 1.0f)) {
                    ++wrong;
                    char m[64];
                    std::snprintf(m, sizeof(m), " %s at %.2f", face.name, k);
                    misses += m;
                }
            }
        Report("10a", "(more) each face of the pouch: a squeeze at 0.95 of its size reaches it, at 1.05 doesn't",
               wrong == 0, false, "%d of 12 wrong%s", wrong, misses.c_str());
    }
    {
        Sim sim;
        Fresh(sim, "10b");
        struct Spot {
            const char* name;
            float right, up, forward;
        };
        const Spot spots[] = {{"the chest", 0.0f, 0.35f, 0.05f},
                              {"25 cm right at the hip", 0.25f, 0.0f, 0.0f},
                              {"the thigh", 0.0f, -0.25f, 0.05f},
                              {"behind the hip", 0.0f, 0.0f, -0.30f}};
        int taken = 0;
        std::string which;
        for (const Spot& spot : spots) {
            float p[3];
            PouchPoint(kNeck0, 0.0f, spot.right, spot.up, spot.forward, p);
            sim.AtGrip(p, Upright());
            sim.f.squeeze = 0.0f;
            Still(sim, 2 * kDt);
            sim.f.squeeze = 1.0f;
            Still(sim, 3 * kDt);
            if (sim.took > taken) {
                taken = sim.took;
                which += std::string(" ") + spot.name;
                pouch::Clear();
            }
        }
        Report("10b", "(more) squeezes at the chest, 25 cm right at the hip, the thigh, behind: nothing", taken == 0,
               false, "took %d%s", taken, which.c_str());
    }
}

// ---- 11. A crooked entry, straightened (more) ----

void Scenario11() {
    struct Tilt {
        const char* id;
        const char* what;
        float x, y, z;
    };
    const Tilt tilts[] = {
        {"11a", "(more) 20 deg fore and aft and 3 cm to the side, caught 5 cm below the mouth: straightened, seats", 1, 0, 0},
        {"11b", "(more) 20 deg sideways and 3 cm to the side, caught 5 cm below the mouth: straightened, seats", 0, 0, 1},
    };
    for (const Tilt& c : tilts) {
        Sim sim;
        Fresh(sim, c.id);
        const Pose shape = Mul(Move(0.03f, 0.0f, 0.0f), TurnAbout(c.x, c.y, c.z, 20.0f, kTop));
        const float s0 = kMouth - 0.10f;
        sim.InGun(Mul(Move(0.0f, s0, 0.0f), shape));
        TakeHere(sim);
        Watch w;
        Slide(sim, shape, s0, 0.01f, 0.3f, [&] { w.After(sim); });
        Still(sim, 0.3f, [&] { w.After(sim); });
        const bool pass = sim.caught == 1 && w.caughtH >= 0.0f && w.caughtH <= kFunnelDepth + kEps && sim.offLine == 0 &&
                          sim.seated == 1 && w.mouthAngle >= 0.0f && w.mouthAngle <= 3.0f + 0.01f &&
                          w.mouthLateral <= 0.006f + kEps;
        char off[160] = "";
        if (sim.offLine)
            std::snprintf(off, sizeof(off), " (the hand's top at h %.1f cm: %s the mouth)", w.offH * 100,
                          w.offH < 0.0f ? "above" : "below");
        Report(c.id, c.what, pass, false,
               "caught %d at h %.1f cm (the hand %.0f deg, %.1f cm off); the drawn magazine at the mouth %.1f deg, "
               "%.1f mm off; off the line %d%s; seated %d; moved at most %.1f mm a frame",
               sim.caught, w.caughtH * 100, w.caughtAngle, w.caughtLateral * 100, w.mouthAngle, w.mouthLateral * 1000,
               sim.offLine, off, sim.seated, w.moveMost * 1000);
    }
}

// ---- 12. The wrist turned inside the well (more) ----

void Scenario12() {
    struct Wrist {
        const char* id;
        const char* what;
        float x, y, z;
    };
    const Wrist wrists[] = {
        {"12a", "(more) on the line 5 cm from home, the wrist rolled 10 deg (about the gun's z): stays on", 0, 0, 1},
        {"12b", "(more) ... the wrist pitched 10 deg (about x): stays on", 1, 0, 0},
        {"12c", "(more) ... the wrist twisted 10 deg (about y): stays on", 0, 1, 0},
    };
    const float s = -0.05f;
    for (const Wrist& c : wrists) {
        Sim sim;
        Fresh(sim, c.id);
        TakeHere(sim);
        Slide(sim, Pose(), -0.20f, s, 0.3f);
        Still(sim, 0.1f);
        const bool on = sim.line;
        float offAt = -1.0f;
        for (int i = 1, n = Frames(0.2f); i <= n; ++i) {
            const float a = 10.0f * Ease(static_cast<float>(i) / n);
            sim.InGun(Mul(Move(0.0f, s, 0.0f), TurnAbout(c.x, c.y, c.z, a, kHandMiddle)));
            sim.Step();
            if (sim.r.offLine && offAt < 0.0f)
                offAt = a;
        }
        Still(sim, 0.3f);
        // What the module measures inside the well (the glove's origin, its bind space's, from the
        // drawn glove's) and the hand's own middle, with the full 10 degrees.
        const Pose hand = Mul(Move(0.0f, s, 0.0f), TurnAbout(c.x, c.y, c.z, 10.0f, kHandMiddle)), drawn = Move(0.0f, s, 0.0f);
        const Pose gloveHand = Mul(hand, g_gloveOfSeated), gloveDrawn = Mul(drawn, g_gloveOfSeated);
        const float origin[3] = {0, 0, 0};
        float a[3], b[3], ha[3], hb[3];
        Apply(gloveHand, origin, a);
        Apply(gloveDrawn, origin, b);
        Apply(hand, kHandMiddle, ha);
        Apply(drawn, kHandMiddle, hb);
        char off[64] = "";
        if (sim.offLine)
            std::snprintf(off, sizeof(off), " at %.1f deg", offAt);
        Report(c.id, c.what, on && sim.offLine == 0 && sim.line, false,
               "off the line %d%s; with 10 deg the glove's origin is %.1f cm from the drawn glove's (what the module "
               "measures inside the well, 12 cm allowed), the hand's middle %.1f cm",
               sim.offLine, off, Distance(a, b) * 100, Distance(ha, hb) * 100);
    }
    {
        Sim sim;
        Fresh(sim, "12d");
        TakeHere(sim);
        Slide(sim, Pose(), -0.20f, s, 0.3f);
        Still(sim, 0.1f);
        float x = 0.0f, offAt = -1.0f;
        while (x < 0.15f) {
            x = std::min(x + 0.5f * kDt, 0.15f);
            sim.InGun(Move(x, s, 0.0f));
            sim.Step();
            if (sim.r.offLine && offAt < 0.0f)
                offAt = x;
        }
        Still(sim, 0.2f);
        Report("12d", "(more) on the line 5 cm from home, the hand pulled 15 cm sideways: off the line", sim.offLine == 1,
               false, "off the line %d (the hand %.1f cm to the side)", sim.offLine, offAt * 100);
    }
}

// ---- 13. The whole reload (more) ----

void Scenario13() {
    Sim sim;
    Fresh(sim, "13");
    float middle[3];
    MiddleFor(kNeck0, 0.0f, middle);
    const bool took = TakeAt(sim, middle);
    Watch w;
    const Pose from = sim.mag, to = Mul(sim.gun, Move(0.0f, -0.20f, 0.0f));
    for (int i = 1, n = Frames(0.7f); i <= n; ++i) {
        sim.mag = Between(from, to, Ease(static_cast<float>(i) / n));
        sim.Step();
        w.After(sim);
    }
    Slide(sim, Pose(), -0.20f, 0.005f, 0.5f, [&] { w.After(sim); });
    Still(sim, 0.3f, [&] { w.After(sim); });
    const bool pass = took && sim.caught == 1 && sim.seated == 1 && sim.offLine == 0 && sim.letGo == 0 &&
                      sim.cancelled == 0 && sim.putBack == 0;
    Report("13", "(more) taken at the pouch, brought up under the well in 0.7 s, pushed in at 0.5 m/s: seats", pass,
           false, "took %s; caught %d at h %.1f cm, seated %d; off the line %d, let go %d, gone %d; moved at most %.1f "
           "mm a frame", YesNo(took), sim.caught, w.caughtH * 100, sim.seated, sim.offLine, sim.letGo, sim.cancelled,
           w.moveMost * 1000);
}

// ---- 14, 15. Shown, not counted ----

void Scenario14() {
    // The gun's pose missing (gunTracked false, gunPose all zeros, as hands.cpp gives it) for n frames.
    struct Case {
        const char* id;
        const char* what;
        bool onLine;
        int frames;
        bool expectKept;
    } cases[] = {
        {"14a", "the right controller's pose missing 1 frame while held: kept", false, 1, true},
        {"14b", "... 1 frame while on the line: kept, no off-line, then seats", true, 1, true},
        {"14c", "... 10 frames while on the line: kept", true, 10, true},
        {"14d", "... 1.2 s while held: gone (the 1 s rule)", false, Frames(1.2f), false},
    };
    for (const Case& c : cases) {
        Sim sim;
        Fresh(sim, c.id);
        TakeHere(sim);
        if (c.onLine)
            Slide(sim, Pose(), -0.20f, -0.06f, 0.3f);
        Still(sim, 0.1f);
        int drawnLost = 0, offLost = 0;
        for (int i = 0; i < c.frames; ++i) {
            sim.f.gunTracked = false;
            sim.zeroGun = true;
            sim.Step();
            drawnLost += sim.r.mag;
            offLost += sim.r.offLine;
        }
        sim.f.gunTracked = true;
        sim.zeroGun = false;
        const bool lineAfter = sim.line;
        Still(sim, 0.2f);
        const int offAfter = sim.offLine;
        int seated = 0;
        if (c.onLine && pouch::Holding()) {
            Slide(sim, Pose(), -0.06f, 0.005f, 0.3f, [&] {
                if (sim.r.seated)
                    sim.f.wellEmpty = false;
            });
            seated = sim.seated;
        }
        const bool kept = sim.cancelled == 0 && (c.onLine ? seated == 1 && offAfter == 0 : pouch::Holding());
        Report(c.id, c.what, c.expectKept ? kept : sim.cancelled == 1 && !pouch::Holding(), false,
               "gone %d; drawn while lost %d frames (zero gun pose), off the line while lost %d; on the line after %s, "
               "off the line in all %d; seated %d; holding at the end %s",
               sim.cancelled, drawnLost, offLost, YesNo(lineAfter), sim.offLine, seated, YesNo(pouch::Holding()));
    }
    // The weapon itself gone (Frame::gun false, the pose there): still gone at once.
    {
        Sim sim;
        Fresh(sim, "14e");
        TakeHere(sim);
        Still(sim, 0.1f);
        sim.f.gun = false;
        sim.Step();
        const bool goneNow = sim.r.cancelled;
        sim.f.gun = true;
        Still(sim, 0.1f);
        Report("14e", "the weapon switched (Frame::gun false) while held: gone at once", goneNow && !pouch::Holding(),
               false, "gone that frame %s, holding after %s", YesNo(goneNow), YesNo(pouch::Holding()));
    }
    // A fresh squeeze at the pouch while the gun's pose is missing (hands.cpp leaves guardDistance at
    // its default 1 m then: the handguard rule isn't measured).
    {
        Sim sim;
        Fresh(sim, "14f");
        sim.f.guardDistance = 1.0f;
        sim.f.squeeze = 0.0f;
        sim.f.forceInside = true;
        for (int i = 0; i < 3; ++i)
            sim.Step();
        sim.f.gunTracked = false;
        sim.zeroGun = true;
        sim.f.squeeze = 1.0f;
        sim.Step();
        const bool took = sim.r.took;
        const char* note = sim.r.note;
        sim.f.gunTracked = true;
        sim.zeroGun = false;
        sim.f.forceInside = false;
        Still(sim, 0.1f);
        Report("14f", "a squeeze at the pouch with the gun's pose missing (the handguard rule unmeasured): nothing taken",
               !took, true, "took %s (\"%s\"), holding after %s", YesNo(took), note, YesNo(pouch::Holding()));
    }
}

void Scenario15() {
    Sim sim;
    Fresh(sim, "15");
    TakeHere(sim);
    Still(sim, 0.1f);
    sim.f.left = false;
    Still(sim, 5 * kDt);
    sim.f.squeeze = 0.0f;  // let go of while the hand is lost
    sim.Step();
    const bool goneNow = sim.r.cancelled;
    Still(sim, 4 * kDt);
    sim.f.left = true;
    sim.Step();
    const bool letGoBack = sim.r.letGo;
    Report("15", "let go of while the hand is lost: gone at once (the design, section 6)", goneNow, true,
           "gone on letting go %s; when the hand came back: letGo %s, holding %s", YesNo(goneNow), YesNo(letGoBack),
           YesNo(pouch::Holding()));
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0 && i + 1 < argc)
            g_verbose = argv[++i];
    pouch::SetZone(kZone, true);
    {
        float glove[12];
        const Pose seated{};
        pouch::GloveFor(seated.m, glove);
        g_gloveOfSeated = FromArray(glove);
        Apply(Inverse(g_gloveOfSeated), kHandMiddle, g_handInGlove);
    }
    float middle[3];
    MiddleFor(kNeck0, 0.0f, middle);
    {
        const Pose gun = GunPose();
        float top[3], view[3];
        Apply(gun, kTop, view);
        const Pose head = FromXr(HeadAt(kNeck0, 0.0f, 0.0f));
        Apply(head, view, top);
        std::printf("magtest: src/pouch.cpp at 72 Hz. The eyes at (0 1.6 0), the neck at (%.3f %.3f %.3f), the pouch's "
                    "middle at (%.3f %.3f %.3f); the gun's magazine home at (%.3f %.3f %.3f) in the room; the hand's "
                    "middle in the gloves' bind space (%.4f %.4f %.4f), the glove's origin %.2f m from it.\n",
                    kNeck0[0], kNeck0[1], kNeck0[2], middle[0], middle[1], middle[2], top[0], top[1], top[2],
                    g_handInGlove[0], g_handInGlove[1], g_handInGlove[2],
                    std::sqrt(g_handInGlove[0] * g_handInGlove[0] + g_handInGlove[1] * g_handInGlove[1] +
                              g_handInGlove[2] * g_handInGlove[2]));
    }
    Scenario1();
    Scenario2();
    Scenario3();
    Scenario4();
    Scenario5();
    Scenario6();
    Scenario7();
    Scenario8();
    Scenario9();
    Scenario10();
    Scenario11();
    Scenario12();
    Scenario13();
    Scenario14();
    Scenario15();
    std::printf("%d of %d checks failed (%d of them in 1-9, the ones asked for); %d info checks marked *\n", g_failed,
                g_counted, g_failedAsked, g_infoOff);
    return g_failed;
}
