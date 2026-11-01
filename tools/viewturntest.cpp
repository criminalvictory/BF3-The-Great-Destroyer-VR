// The view cameras' turn to the head (src/viewturn.h, as camera.cpp TurnToHead runs it), without the game: the
// player's camera turned exactly as before, and a scripted camera (a cutscene's or a ride's, its rows' w lanes
// filled) turned about its own yaw with the horizon level. A camera here is BF3's LinearTransform: the rows right,
// up, back and translation in the game's world (y up). A turn is a quaternion in OpenXR's axes (x right, y up, -z
// forward), a yaw turning left positive, except BF3's look yaws (atan2 of the back row's x and -z), which grow
// turning right. The scripted cameras from the logs are refused ones as camera.cpp logged them (up and forward to
// two decimals, made square here, the right row across them as the player's camera has it), with their w lanes.
//   1  the player's camera (w lanes 0 or -0): Classify takes it as IsCamera always did, whatever the switch; on
//      20000 random ones (any yaw, pitch within 85 degrees, a fifth rolled, a quarter mirrored) with random heads,
//      turn states (with and without a look yaw, either sign) and mirror flags, viewturn::Turn gives the bits the turn
//      before this change gave (copied below from camera.cpp at 6ee0128), the matrix and the yaw taken out
//   2  the same on recorded turns (the viewlogs: build\bf3vr.log, Comrades, 2026-11-01; screenshots\logs\
//      bf3vr_1831_viewlog.log, The Great Destroyer, 2026-10-31: the hand's yaw, the look yaw written, the camera's yaw
//      and place, the head's yaw): the same bits, and the yaw shown as logged (0.025 deg: the log's rounding)
//   3  the check: the logged scripted cameras and 20000 synthetic ones taken as scripted with "cutsceneturn" on and
//      refused with it off; refused as well: a w lane of 10 or more, NaN or infinite; the translation NaN or
//      infinite; a row 5 degrees off square (2 is taken); a row 10% long; 200000 matrices of random floats. And
//      IsCamera as before on 200000 random and perturbed matrices
//   4  a scripted camera turned (the logged ones and 20000 synthetic: any yaw, pitch within 80 degrees, any roll, the
//      w lanes as the game fills them) with any head: its forward the camera's yaw times the head's (0.01 deg), its
//      rows too (1e-5); the w lanes and the translation kept bit for bit; no yaw taken out; the picture's body as
//      xr_runtime.cpp reads it (ShownTurn: camera + taken) the camera's own yaw, and the yaw shown that less the head's
//   5  the horizon: a level head (yaw only) gives up (0 1 0) and the right and back rows flat (1e-6) whatever the
//      camera's pitch and roll; the head pitched as well, the right row still flat; the head rolled, the picture
//      rolled by the head's roll alone (0.01 deg)
//   6  the eye shift (views.cpp MakeEyes: each eye half the eye distance along the turned main camera's right row):
//      along the head's right as the room turned by the camera's yaw has it (1e-5), across the forward, level for a
//      level head; refused, the eyes went along the camera's own right, rolled with it
//   7  mirrored (rows of the other hand): with mirrorfix the same forward and up as the unmirrored twin and the right
//      row the other way (the head's turn the same way in the world, as a mirrored camera's always was); without, the
//      head's yaw and roll the other way (as "mirrorfix 0" always did); a level camera, or one only pitched, comes out
//      as the turn before this change gave it with nothing taken out (1e-5), mirrored or not, while a rolled one
//      kept its roll that way and is level this way
//   8  a forward near the vertical: pitching down to 90 degrees and back up at 90 deg/s, the yaw still (rolled 25
//      degrees and not): the yaw kept (0.01 deg), no frame turning it; straight down with no last frame, along the
//      ground the way the picture's top points; straight up, the other way; random ones within 6 degrees of the
//      vertical, going on from a last frame a few degrees away and not: never NaN, always level and square
//   9  offered again once turned (camera.cpp's again guard): turned a second time, the head's yaw counts twice; the
//      turned camera's yaw and pitch are the ones shown, as the guard looks for (0.01 deg), and no fresh camera of a
//      swaying, turning drive passes for one (the guard's tolerances, copied from camera.cpp)
//  10  the w lanes (the finding): in the logged scripted cameras they're their rotation quaternion's 2(yw - xy),
//      -2(xy + yw) and ww + xx - yy - zz (0.06: the log's rounding)
//  11  a scripted camera's turn takes under 1 us, with its first-person camera turned along too
//  12  the review's finding (2026-11-01): a scripted camera held still, then turned 1 degree about its own up, right
//      or back row or a random line (the logged ones, 10000 random, and every half degree from level to straight
//      down): the yaw it's made level about turns at most 1.45 degrees at once and 2 within a second (at 833abbd,
//      from its back row's own yaw alone: 5.8 for the interrogation's camera of 23:57:12, 9.5 at 84 degrees, and up
//      to 180 for a rolled camera leaving the 6-degree cone), and from level to straight down it changes smoothly
//      (at 833abbd it fell from 9.5 to 0 at 85 degrees)
//  13  the drive of Comrades as at 833abbd: cameras within 45 degrees of level (20000 random, any roll within 30,
//      either hand, with no last frame, one a few degrees away and a far one; a drive frame by frame) made level bit
//      for bit as at 833abbd; the logged drive cameras going on from one a degree away within 0.01 degrees of it
//  14  going on from the last frame: a steep camera spinning about the world's up followed (0.01 deg a frame);
//      panning about its own up, its yaw turns as far as the camera turned about the world's up (0.01 deg), its own
//      yaw much further; a cut (45 degrees in a frame) takes its own yaw; tumbling over the top or rolling through
//      upside down: never NaN, always level and square, the largest step a frame shown; a frame's cameras made level
//      from the same last frame alike, bit for bit (made one after another from one Level they'd differ: camera.cpp
//      latches it)
//  15  the body (views.cpp hands TurnToHead each eye's first-person camera, which the player's own arms, gun and legs
//      are drawn with, with its main camera): with a scripted camera and no look written, turned by the main camera's
//      turn as the main camera's own frame has it, put on it in its own (the turned main camera's rows taken apart
//      along the main's as the game made them, the first-person camera's rows put together again in those parts:
//      1e-6), about its own place, its w lanes and place kept bit for bit: as the main camera (w lanes as its own, or
//      0; anywhere near it) it comes out as the turned main camera (1e-6); turned from it about a line in the world, or
//      in its own frame (about a line in the world too), it comes out as the turned one turned so about that line
//      (1e-5), mirrored or not, rows a little off unit or square too; the knees and the gun as the game put them stay
//      on the scene they were drawn over as the head turns, the first-person camera as the main one or turned a few
//      degrees from it, in the arms' space (0.001 deg; at 3cbdb33, the turn put on it from the world's side, 2 degrees
//      of pitch between the two put them up to 3.9 degrees off it; left as the game made it, they moved with the head);
//      a normal picture's (the player's camera, 20000 with random turns, look or not, mirrored or not) left bit for bit,
//      and the main camera turned bit for bit as at 6ee0128; with the look still written, or not a camera, left bit for
//      bit; the main camera turned bit for bit as without one
//  16  the body's own views turned where they're uploaded (skin.cpp's body lock: the first-person camera +0x930
//      turned left the body moving with the head, 2026-11-01 09:24): a 352-byte per-view upload (the view at +48,
//      the projection at +112, the view-projection at +176, the one for points less the eye at +240, the eye at
//      +320; the projection's w row either way, an x offset or not, the products as the game's floats or rounded from
//      double) in the arms' space, for a first-person camera as the scripted one or a few degrees from it: read, its
//      view turned as TurnWith turns that camera (1e-6), still rigid; the view-projection the projection times the
//      turned view, the one for points less the eye the projection times its turn alone (1e-5); the eye kept (+320
//      bit for bit, from the turned view 1e-5 m); the projection and the rest bit for bit; the knees and the gun on
//      the scene they were drawn over (0.001 deg); its view-projection alone in the world (80 bytes) turned alike:
//      the same rows, the eye kept, what each row gives at the eye kept, points near the eye where the turned view
//      shows them (2e-4 of the picture). Which uploads are turned (LockBodyPerView, LockBodyWorldView): the body's own
//      by the frame's view build's turn, the newest or the one the main view's own upload names; the main view's
//      (its turned camera's rows, in the world or not), the turned first-person camera's, one looking away, one
//      elsewhere, and what isn't a view (random, orthographic, not the projection's product, +240 otherwise, NaN,
//      infinite, zeros) left bit for bit. A normal picture's (no scripted build) all left bit for bit. The FOV fix
//      after the turn as before it (1e-6). The time
// Usage: viewturntest   The exit code is the number of failed checks.

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>

#include "../src/viewturn.h"

namespace {

// ---- The turn as camera.cpp had it before scripted cameras were turned (6ee0128), copied as it was ----
namespace old {

constexpr float kPi = 3.14159265f;

// A LinearTransform's three direction rows are unit length with a zero fourth component;
// arbitrary data almost never is.
bool IsCamera(const float* m) {
    if (m[3] != 0.0f || m[7] != 0.0f || m[11] != 0.0f)
        return false;
    for (int row = 0; row < 3; ++row) {
        const float* v = m + 4 * row;
        float len = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        if (!(std::fabs(len - 1.0f) < 0.05f))  // also rejects NaN
            return false;
    }
    return true;
}

// Quaternions are (x, y, z, w), in OpenXR's axes: x right, y up, -z forward.
void QuatMul(const float* a, const float* b, float* out) {
    out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

void QuatRotate(const float* q, const float* v, float* out) {
    float t[3] = {2 * (q[1] * v[2] - q[2] * v[1]), 2 * (q[2] * v[0] - q[0] * v[2]), 2 * (q[0] * v[1] - q[1] * v[0])};
    out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

void ViewTurnLocal(const float* m, const camera::ViewTurn& turn, float* local, float* taken = nullptr) {
    float pitch = std::asin(std::clamp(-m[9], -1.0f, 1.0f));  // true forward = -row2
    float yaw = turn.controllerYaw;
    if (turn.haveLook) {
        float behind = std::atan2(m[8], -m[10]) - turn.lookYaw;
        if (behind > kPi)
            behind -= 2 * kPi;
        if (behind < -kPi)
            behind += 2 * kPi;
        yaw = turn.currentYaw + turn.lookSign * behind;
    }
    if (taken)
        *taken = yaw;
    float qYaw[4] = {0.0f, std::sin(yaw / 2), 0.0f, std::cos(yaw / 2)};
    float qPitch[4] = {std::sin(pitch / 2), 0.0f, 0.0f, std::cos(pitch / 2)};
    float aim[4];
    QuatMul(qYaw, qPitch, aim);
    float aimInverse[4] = {-aim[0], -aim[1], -aim[2], aim[3]};
    QuatMul(aimInverse, turn.head, local);
}

void TurnLocal(float* m, const float* local) {
    float right[3] = {m[0], m[1], m[2]};
    float up[3] = {m[4], m[5], m[6]};
    float forward[3] = {-m[8], -m[9], -m[10]};

    // Local OpenXR axes onto the game camera's true axes.
    auto toWorld = [&](const float* v, float* out) {
        for (int i = 0; i < 3; ++i)
            out[i] = v[0] * right[i] + v[1] * up[i] - v[2] * forward[i];
    };
    const float x[3] = {1, 0, 0}, y[3] = {0, 1, 0}, minusZ[3] = {0, 0, -1};
    float axis[3], newRight[3], newUp[3], newForward[3];
    QuatRotate(local, x, axis);
    toWorld(axis, newRight);
    QuatRotate(local, y, axis);
    toWorld(axis, newUp);
    QuatRotate(local, minusZ, axis);
    toWorld(axis, newForward);

    for (int i = 0; i < 3; ++i) {
        m[i] = newRight[i];
        m[4 + i] = newUp[i];
        m[8 + i] = -newForward[i];
    }
}

// TurnToHead's turn of a camera that passed IsCamera (the again guard aside): ViewTurnLocal, the mirror, TurnLocal.
float Turn(float* transform, const camera::ViewTurn& turn, bool mirrored) {
    float local[4], taken = 0.0f;
    ViewTurnLocal(transform, turn, local, &taken);
    if (mirrored) {
        local[1] = -local[1];
        local[2] = -local[2];
    }
    TurnLocal(transform, local);
    return taken;
}

// A scripted camera made level as at 833abbd (viewturn.h then), copied as it was, for checks 12 and 13.
constexpr float kVertical = 0.1f;

struct Level {
    bool have = false;
    float back[2] = {};  // the level back row's x and z
};

void MakeLevel(float* m, int hand, Level& level) {
    float x = m[8], z = m[10];
    if (x * x + z * z < kVertical * kVertical) {
        if (level.have) {
            x = level.back[0], z = level.back[1];
        } else {
            const float away = m[9] > 0.0f ? -1.0f : 1.0f;  // the back row up: looking down
            x = away * m[4], z = away * m[6];
        }
    }
    const float length = std::sqrt(x * x + z * z);
    if (length > 1e-6f) {
        x /= length, z /= length;
    } else {
        x = 0.0f, z = 1.0f;  // never for a camera Classify passes (its up row then lies along the ground)
    }
    level.have = true;
    level.back[0] = x, level.back[1] = z;
    const float h = hand < 0 ? -1.0f : 1.0f;
    m[0] = h * z, m[1] = 0.0f, m[2] = -h * x;
    m[4] = 0.0f, m[5] = 1.0f, m[6] = 0.0f;
    m[8] = x, m[9] = 0.0f, m[10] = z;
}

// The first-person camera turned with a scripted one as at 3cbdb33 (viewturn.h then), copied as it was, for check
// 15b: the main camera's turn put on it from the world's side (its rows times before's inverse times after's).
bool TurnWith(float* m, const float* before, const float* after) {
    if (viewturn::Classify(m, true) == viewturn::kNone)
        return false;
    const float* a = before;
    const float* b = before + 4;
    const float* c = before + 8;
    const float across[3][3] = {{b[1] * c[2] - b[2] * c[1], b[2] * c[0] - b[0] * c[2], b[0] * c[1] - b[1] * c[0]},
                                {c[1] * a[2] - c[2] * a[1], c[2] * a[0] - c[0] * a[2], c[0] * a[1] - c[1] * a[0]},
                                {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}};
    const float det = a[0] * across[0][0] + a[1] * across[0][1] + a[2] * across[0][2];
    if (!(std::fabs(det) > 0.5f))  // also rejects NaN
        return false;
    float rows[9];
    for (int row = 0; row < 3; ++row) {
        const float* v = m + 4 * row;
        float part[3];
        for (int k = 0; k < 3; ++k)
            part[k] = (v[0] * across[k][0] + v[1] * across[k][1] + v[2] * across[k][2]) / det;
        for (int i = 0; i < 3; ++i)
            rows[3 * row + i] = part[0] * after[i] + part[1] * after[4 + i] + part[2] * after[8 + i];
    }
    for (int row = 0; row < 3; ++row)
        for (int i = 0; i < 3; ++i)
            m[4 * row + i] = rows[3 * row + i];
    return true;
}

}  // namespace old

constexpr double kPi = 3.14159265358979;

double Deg(double radians) {
    return radians * 180 / kPi;
}
double Abs(float v) {
    return std::fabs(static_cast<double>(v));
}
double Rad(double degrees) {
    return degrees * kPi / 180;
}
// An angle's difference brought within -180 to 180 degrees.
double Wrap(double degrees) {
    return std::remainder(degrees, 360.0);
}

// ---- Directions and turns in double (viewturn.h itself works in float) ----
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
// The angle between two directions (degrees), from the cross and the dot: it keeps its digits near 0.
double AngleDeg(const V3& a, const V3& b) {
    return Deg(std::atan2(Len(Cross(a, b)), Dot(a, b)));
}
// A direction's yaw along the ground, OpenXR's sense (turning left positive, 0 along -z).
double YawOf(const V3& d) {
    return std::atan2(-d.x, -d.z);
}

struct Q {
    double x = 0, y = 0, z = 0, w = 1;
};
Q AxisAngle(const V3& axis, double radians) {
    const V3 a = Unit(axis);
    const double s = std::sin(radians / 2);
    return {a.x * s, a.y * s, a.z * s, std::cos(radians / 2)};
}
Q Mul(const Q& a, const Q& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
// v turned by unit q: v + w t + u x t, with t = 2 u x v (u the vector part).
V3 Rotate(const Q& q, const V3& v) {
    const V3 u(q.x, q.y, q.z);
    const V3 t = Cross(u, v) * 2.0;
    return v + t * q.w + Cross(u, t);
}
Q Yaw(double radians) {
    return AxisAngle({0, 1, 0}, radians);
}
Q Pitch(double radians) {
    return AxisAngle({1, 0, 0}, radians);
}
Q Roll(double radians) {
    return AxisAngle({0, 0, 1}, radians);
}
// Yaw, then pitch about the turned right, then roll about the turned back (radians).
Q Ypr(double yaw, double pitch, double roll) {
    return Mul(Mul(Yaw(yaw), Pitch(pitch)), Roll(roll));
}

// ---- Cameras as BF3 has them ----
const V3 kX(1, 0, 0), kY(0, 1, 0), kZ(0, 0, 1), kForward(0, 0, -1);

V3 Row(const float* m, int r) {
    return {m[4 * r], m[4 * r + 1], m[4 * r + 2]};
}
bool Finite(const float* m) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(m[i]))
            return false;
    return true;
}
// The rows' handedness as camera.cpp TurnToHead tells it: the sign of their determinant.
int Hand(const float* m) {
    const float *r0 = m, *r1 = m + 4, *r2 = m + 8;
    const float det = r0[0] * (r1[1] * r2[2] - r1[2] * r2[1]) - r0[1] * (r1[0] * r2[2] - r1[2] * r2[0]) +
                      r0[2] * (r1[0] * r2[1] - r1[1] * r2[0]);
    return det < 0.0f ? -1 : 1;
}
// The w lanes a scripted camera of rotation q has (the finding, check 10): q's products.
void FilledLanes(const Q& q, double* w) {
    w[0] = 2 * (q.y * q.w - q.x * q.y);
    w[1] = -2 * (q.x * q.y + q.y * q.w);
    w[2] = q.w * q.w + q.x * q.x - q.y * q.y - q.z * q.z;
}
// A camera's 16 floats: rotation q's right, up and back (the right the other way for hand -1), its place, and its
// w lanes 0 (the player's camera) or as the game fills a scripted camera's.
void MakeCamera(float* m, const Q& q, int hand, const V3& at, bool filled) {
    const V3 r = Rotate(q, kX), u = Rotate(q, kY), b = Rotate(q, kZ);
    double w[3];
    FilledLanes(q, w);
    for (int i = 0; i < 3; ++i) {
        m[i] = static_cast<float>(hand * r[i]);
        m[4 + i] = static_cast<float>(u[i]);
        m[8 + i] = static_cast<float>(b[i]);
        m[12 + i] = static_cast<float>(at[i]);
    }
    m[3] = filled ? static_cast<float>(w[0]) : 0.0f;
    m[7] = filled ? static_cast<float>(w[1]) : 0.0f;
    m[11] = filled ? static_cast<float>(w[2]) : 0.0f;
    m[15] = 1.0f;
}
// The rotation whose right, up and back are a camera's rows (the right taken across up and back as the player's
// camera has it, so mirrored rows give their unmirrored twin's).
Q RotationOf(const V3& up, const V3& back) {
    const V3 b = Unit(back);
    const V3 u = Unit(up - b * Dot(up, b));
    const V3 r = Cross(u, b);
    const double R[3][3] = {{r.x, u.x, b.x}, {r.y, u.y, b.y}, {r.z, u.z, b.z}};  // columns right, up, back
    const double trace = R[0][0] + R[1][1] + R[2][2];
    Q q;
    if (trace > 0) {
        const double s = std::sqrt(trace + 1) * 2;
        q = {(R[2][1] - R[1][2]) / s, (R[0][2] - R[2][0]) / s, (R[1][0] - R[0][1]) / s, 0.25 * s};
    } else if (R[0][0] > R[1][1] && R[0][0] > R[2][2]) {
        const double s = std::sqrt(1 + R[0][0] - R[1][1] - R[2][2]) * 2;
        q = {0.25 * s, (R[0][1] + R[1][0]) / s, (R[0][2] + R[2][0]) / s, (R[2][1] - R[1][2]) / s};
    } else if (R[1][1] > R[2][2]) {
        const double s = std::sqrt(1 + R[1][1] - R[0][0] - R[2][2]) * 2;
        q = {(R[0][1] + R[1][0]) / s, 0.25 * s, (R[1][2] + R[2][1]) / s, (R[0][2] - R[2][0]) / s};
    } else {
        const double s = std::sqrt(1 + R[2][2] - R[0][0] - R[1][1]) * 2;
        q = {(R[0][2] + R[2][0]) / s, (R[1][2] + R[2][1]) / s, 0.25 * s, (R[1][0] - R[0][1]) / s};
    }
    return q;
}

// The turn wanted for a scripted camera: its own yaw (its forward along the ground) times the head.
Q Wanted(const float* m, const Q& head) {
    return Mul(Yaw(YawOf(Row(m, 2) * -1.0)), head);
}
// How far a turned camera's rows are from the turn wanted (the largest difference of any of their numbers), its
// right row the other way for hand -1.
double RowsOff(const float* m, const Q& want, int hand) {
    const V3 expected[3] = {Rotate(want, kX) * hand, Rotate(want, kY), Rotate(want, kZ)};
    double off = 0;
    for (int r = 0; r < 3; ++r)
        for (int i = 0; i < 3; ++i)
            off = std::max(off, std::fabs(Row(m, r)[i] - expected[r][i]));
    return off;
}
// The turn's head and the rest of TurnToHead's turn state (only the head matters to a scripted camera).
camera::ViewTurn TurnFor(const Q& head) {
    camera::ViewTurn turn{};
    turn.head[0] = static_cast<float>(head.x), turn.head[1] = static_cast<float>(head.y);
    turn.head[2] = static_cast<float>(head.z), turn.head[3] = static_cast<float>(head.w);
    turn.lookSign = -1.0f;
    return turn;
}
Q HeadOf(const camera::ViewTurn& turn) {
    return {turn.head[0], turn.head[1], turn.head[2], turn.head[3]};
}

// ---- Random cameras and heads (the same every run) ----
std::mt19937 g_rng(20261101);
double Uniform(double a, double b) {
    return std::uniform_real_distribution<double>(a, b)(g_rng);
}
bool Coin(double p = 0.5) {
    return Uniform(0, 1) < p;
}
Q RandomRotation() {
    std::normal_distribution<double> n(0.0, 1.0);
    Q q{n(g_rng), n(g_rng), n(g_rng), n(g_rng)};
    const double l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / l, q.y / l, q.z / l, q.w / l};
}
// Half any way at all, half as a head turns (yaw any, pitch within 70 degrees, roll within 40).
Q RandomHead() {
    return Coin() ? RandomRotation() : Ypr(Uniform(-kPi, kPi), Rad(Uniform(-70, 70)), Rad(Uniform(-40, 40)));
}
V3 RandomPlace() {
    return {Uniform(-2000, 2000), Uniform(-50, 150), Uniform(-2000, 2000)};
}
// A scripted camera as the game makes one: any yaw, pitch within the given degrees, any roll, its w lanes filled.
Q RandomScripted(double pitchMost = 80) {
    return Ypr(Uniform(-kPi, kPi), Rad(Uniform(-pitchMost, pitchMost)), Rad(Uniform(-180, 180)));
}

// ---- The logs ----
// Recorded turns with the look yaw written (the viewlog's "View:" lines, degrees): the turn takes the hand's yaw out
// by the look yaw, so the yaw shown is the hand's + the look yaw - the head's for a camera that doesn't roll; these
// are lines where it does (the others' cameras rolled a little: walking, the train).
struct Recorded {
    const char* at;
    float hand, written, camera, x, y, z, head, shown;
};
const Recorded kRecorded[] = {
    {"Comrades 00:34:50.667", 6.71f, 333.25f, -26.19f, -177.790f, 1.202f, -1529.349f, -23.29f, 3.25f},
    {"Comrades 00:36:54.368", 4.40f, 347.49f, -14.43f, -154.378f, 1.441f, 73.112f, -24.64f, 16.53f},
    {"Comrades 00:38:28.112", -14.55f, 6.46f, 6.46f, -154.405f, 1.441f, 73.114f, -19.63f, 11.54f},
    {"Comrades 00:38:51.719", 3.02f, 258.58f, -101.43f, -154.322f, 1.441f, 73.022f, -26.98f, -71.42f},
    {"Comrades 00:39:15.184", -77.23f, 338.83f, -21.18f, -154.369f, 1.441f, 73.109f, -47.23f, -51.17f},
    {"Comrades 00:41:35.859", 1.29f, 141.35f, 141.36f, -166.374f, 1.601f, 3.313f, -28.71f, 171.35f},
    {"Comrades 00:41:59.302", 4.32f, 138.35f, 138.65f, -166.376f, 1.601f, 3.315f, -25.68f, 168.35f},
    {"Comrades 00:43:33.046", -28.66f, 110.05f, 109.92f, -172.969f, 1.602f, 2.187f, -27.61f, 109.00f},
    {"Comrades 00:45:53.682", -25.41f, 232.12f, -127.93f, -160.022f, 1.451f, 17.807f, -25.03f, -128.26f},
    {"Comrades 00:46:17.127", -27.63f, 233.98f, -126.34f, -160.020f, 1.451f, 17.809f, -7.63f, -146.02f},
    {"Destroyer 18:30:11.581", -39.64f, 178.76f, 178.92f, -1906.537f, 68.485f, -1344.347f, -14.02f, 153.13f},
    {"Destroyer 18:30:44.987", -56.50f, 204.13f, -155.80f, 1397.065f, 82.556f, 1074.055f, -34.92f, -177.45f},
    {"Destroyer 18:31:41.322", -43.35f, 346.33f, -17.16f, 1401.508f, 82.557f, 1071.023f, -38.66f, -18.36f},
    {"Destroyer 18:33:29.769", -32.70f, 6.51f, 4.56f, -1905.534f, 68.487f, -1344.399f, -34.60f, 8.41f},
};

// Refused (scripted) cameras as camera.cpp logged them ("Camera: the first refused"): the w lanes, the up row and the
// back row (the logged forward turned round). The drive: build\bf3vr.log; the interrogation: screenshots\logs\
// bf3vr_0004_cantmove2.log and bf3vr_0029_firsttest.log.
struct Logged {
    const char* at;
    double w[3], up[3], back[3];
};
const Logged kLogged[] = {
    {"Comrades 00:35:03.539", {0.806, -0.865, -0.548}, {0.00, 1.00, 0.07}, {0.84, 0.03, -0.55}},
    {"Comrades 00:35:05.531", {0.912, -0.915, -0.406}, {-0.01, 1.00, 0.01}, {0.91, 0.01, -0.41}},
    {"Comrades 00:35:07.536", {0.942, -0.833, -0.453}, {-0.12, 0.99, 0.07}, {0.88, 0.14, -0.45}},
    {"Comrades 00:35:32.380", {0.567, -0.856, -0.686}, {0.09, 0.98, 0.19}, {0.72, 0.06, -0.69}},
    {"Comrades 00:35:36.376", {-0.002, 0.017, -1.000}, {-0.01, 1.00, 0.02}, {-0.01, 0.02, -1.00}},
    {"Comrades 00:35:43.386", {-0.880, 0.766, -0.561}, {0.12, 0.99, 0.10}, {-0.82, 0.16, -0.55}},
    {"Comrades 00:35:45.374", {-1.044, 0.902, -0.215}, {0.12, 0.99, 0.00}, {-0.97, 0.11, -0.22}},
    {"Comrades 00:35:56.381", {0.079, -0.071, -0.997}, {-0.01, 0.98, 0.17}, {0.07, 0.17, -0.98}},
    {"Comrades 00:36:11.379", {-1.110, 0.852, -0.035}, {0.27, 0.96, 0.02}, {-0.96, 0.27, -0.03}},
    {"Comrades 00:36:12.379", {-1.161, 0.757, 0.000}, {0.40, 0.92, -0.01}, {-0.92, 0.40, -0.00}},
    {"Comrades 00:36:26.378", {0.313, -0.131, -0.966}, {-0.18, 0.70, 0.69}, {0.18, 0.71, -0.68}},
    {"Comrades 00:36:34.379", {0.754, -0.058, -0.842}, {-0.41, 0.90, 0.18}, {0.36, 0.34, -0.87}},
    {"Comrades 00:36:41.378", {1.188, -0.678, -0.116}, {-0.47, 0.88, 0.02}, {0.87, 0.47, -0.12}},
    {"Comrades 00:36:42.378", {1.237, -0.625, 0.141}, {-0.44, 0.87, -0.22}, {0.89, 0.46, 0.05}},
    {"Destroyer 23:57:05.771", {0.326, 0.796, -0.786}, {-0.51, 0.79, 0.36}, {-0.33, 0.20, -0.92}},
    {"Destroyer 23:57:11.773", {-0.408, 0.557, -0.867}, {0.03, 0.91, 0.41}, {-0.51, 0.37, -0.78}},
    {"Destroyer 23:57:12.775", {0.177, 0.144, -0.972}, {-0.18, -0.07, 0.98}, {-0.16, 0.99, 0.04}},
    {"Destroyer 00:03:53.901", {0.224, -0.982, -0.679}, {0.23, 0.81, 0.54}, {0.70, 0.25, -0.67}},
    {"Destroyer 00:03:54.897", {0.663, 0.004, -0.872}, {-0.23, 0.78, -0.58}, {0.43, -0.45, -0.78}},
    {"Destroyer 00:03:55.897", {1.196, 0.743, 0.087}, {-0.97, -0.04, -0.25}, {0.23, 0.24, -0.94}},
    {"Destroyer 00:10:46.756", {0.019, -0.195, -0.987}, {0.14, 0.56, -0.82}, {0.05, -0.83, -0.55}},
    {"Destroyer 00:10:47.758", {-0.067, 0.180, -0.960}, {-0.28, -0.51, -0.81}, {-0.03, -0.84, 0.54}},
};
constexpr int kLoggedCount = static_cast<int>(sizeof(kLogged) / sizeof(kLogged[0]));

// A logged camera made square, at its place, with its w lanes as logged.
void LoggedCamera(const Logged& l, float* m) {
    const Q q = RotationOf({l.up[0], l.up[1], l.up[2]}, {l.back[0], l.back[1], l.back[2]});
    MakeCamera(m, q, 1, {-236.63, 1.22, -1225.62}, false);
    m[3] = static_cast<float>(l.w[0]), m[7] = static_cast<float>(l.w[1]), m[11] = static_cast<float>(l.w[2]);
}

// ---- Reporting ----
int g_failed = 0, g_counted = 0;

void Report(const char* id, const char* what, bool pass, const char* format, ...) {
    char numbers[1536];
    va_list args;
    va_start(args, format);
    std::vsnprintf(numbers, sizeof(numbers), format, args);
    va_end(args);
    std::printf("%-3s %-5s %s | %s\n", id, pass ? "PASS" : "FAIL", what, numbers);
    ++g_counted;
    g_failed += !pass;
}

// A random player's camera as the game makes one: any yaw, pitch within 85 degrees, a fifth of them rolled, a
// quarter of them mirrored, the w lanes 0 (a tenth -0), the rows' lengths a little off now and then.
void RandomPlayerCamera(float* m) {
    const Q q = Ypr(Uniform(-kPi, kPi), Rad(Uniform(-85, 85)), Coin(0.2) ? Rad(Uniform(-30, 30)) : 0.0);
    MakeCamera(m, q, Coin(0.25) ? -1 : 1, RandomPlace(), false);
    if (Coin(0.1))
        m[3] = m[7] = m[11] = -0.0f;
    if (Coin(0.1))
        for (int i = 0; i < 3; ++i)
            m[4 + i] *= 1.01f;
}
// The rest of a turn state as xr_runtime.cpp makes them: with a look yaw half the time (written within 0 to 2 pi),
// either sign, any hand's yaw.
camera::ViewTurn RandomTurn() {
    camera::ViewTurn turn = TurnFor(RandomHead());
    turn.controllerYaw = static_cast<float>(Uniform(-kPi, kPi));
    turn.haveLook = Coin();
    turn.currentYaw = static_cast<float>(Uniform(-kPi, kPi));
    turn.lookYaw = static_cast<float>(Uniform(0, 2 * kPi));
    turn.lookSign = Coin(0.8) ? -1.0f : 1.0f;
    turn.nudging = Coin(0.1);
    return turn;
}

// ---- 1. The player's camera: the same bits as before ----
void Check1() {
    int notOrdinary = 0, badMatrix = 0, badTaken = 0, touched = 0;
    constexpr int kRuns = 20000;
    for (int n = 0; n < kRuns; ++n) {
        float m[16];
        RandomPlayerCamera(m);
        const camera::ViewTurn turn = RandomTurn();
        const bool mirrored = Coin();
        if (!old::IsCamera(m) || viewturn::Classify(m, true) != viewturn::kOrdinary ||
            viewturn::Classify(m, false) != viewturn::kOrdinary)
            ++notOrdinary;
        float a[16], b[16];
        std::memcpy(a, m, sizeof(a));
        std::memcpy(b, m, sizeof(b));
        viewturn::Level level;
        const float takenNow = viewturn::Turn(a, turn, false, Hand(m), mirrored, level);
        const float takenBefore = old::Turn(b, turn, mirrored);
        badMatrix += std::memcmp(a, b, sizeof(a)) != 0;
        badTaken += std::memcmp(&takenNow, &takenBefore, sizeof(takenNow)) != 0;
        touched += level.have;
    }
    Report("1a", "the player's camera taken as before, whatever the switch", notOrdinary == 0,
           "%d of %d not taken as the player's kind (the old IsCamera and Classify on and off)", notOrdinary, kRuns);
    Report("1b", "the player's camera turned bit for bit as before", badMatrix == 0 && badTaken == 0 && touched == 0,
           "%d of %d matrices and %d yaws taken out differ from the turn at 6ee0128; the scripted yaw touched %d times",
           badMatrix, kRuns, badTaken, touched);
}

// ---- 2. Recorded turns ----
void Check2() {
    int bad = 0;
    double worst = 0;
    const char* worstAt = "";
    for (const Recorded& r : kRecorded) {
        // The camera as logged: BF3's yaw turned into OpenXR's (180 less), level (its pitch is taken out exactly).
        float m[16];
        MakeCamera(m, Yaw(kPi - Rad(r.camera)), 1, {r.x, r.y, r.z}, false);
        camera::ViewTurn turn = TurnFor(Yaw(Rad(r.head)));
        turn.haveLook = true;
        turn.currentYaw = static_cast<float>(Rad(r.hand));
        turn.lookYaw = static_cast<float>(Rad(r.written));
        turn.lookSign = -1.0f;  // bf3vr.ini invert_yaw=0
        float a[16], b[16];
        std::memcpy(a, m, sizeof(a));
        std::memcpy(b, m, sizeof(b));
        viewturn::Level level;
        const float takenNow = viewturn::Turn(a, turn, false, 1, false, level);
        const float takenBefore = old::Turn(b, turn, false);
        bad += std::memcmp(a, b, sizeof(a)) != 0 || std::memcmp(&takenNow, &takenBefore, sizeof(takenNow)) != 0;
        const double shown = Deg(std::atan2(a[8], -a[10]));
        const double off = std::fabs(Wrap(shown - r.shown));
        if (off > worst)
            worst = off, worstAt = r.at;
    }
    const int count = static_cast<int>(sizeof(kRecorded) / sizeof(kRecorded[0]));
    Report("2", "recorded turns: the same bits, and the yaw shown as logged", bad == 0 && worst < 0.025,
           "%d of %d differ from the turn at 6ee0128; the yaw shown at most %.4f deg from the log's (%s)", bad, count,
           worst, worstAt);
}

// ---- 3. The check ----
void Check3() {
    // The logged scripted cameras and synthetic ones: scripted with the switch on, refused with it off.
    int loggedBad = 0, synthBad = 0;
    for (const Logged& l : kLogged) {
        float m[16];
        LoggedCamera(l, m);
        loggedBad +=
            viewturn::Classify(m, true) != viewturn::kScripted || viewturn::Classify(m, false) != viewturn::kNone;
    }
    constexpr int kSynth = 20000;
    for (int n = 0; n < kSynth; ++n) {
        float m[16];
        MakeCamera(m, RandomScripted(90), Coin(0.25) ? -1 : 1, RandomPlace(), true);
        const bool anyLane = m[3] != 0.0f || m[7] != 0.0f || m[11] != 0.0f;  // all three 0 by chance: the player's kind
        if (anyLane)
            synthBad +=
                viewturn::Classify(m, true) != viewturn::kScripted || viewturn::Classify(m, false) != viewturn::kNone;
    }
    Report("3a", "scripted cameras taken with the switch on, refused with it off", loggedBad == 0 && synthBad == 0,
           "%d of %d logged and %d of %d synthetic taken wrongly", loggedBad, kLoggedCount, synthBad, kSynth);

    // What must be refused, from a good scripted camera.
    float good[16];
    LoggedCamera(kLogged[0], good);
    struct Spoil {
        const char* what;
        void (*spoil)(float*);
        bool taken;
    };
    const Spoil spoils[] = {
        {"a w lane of 10", [](float* m) { m[7] = 10.0f; }, false},
        {"a w lane of -12", [](float* m) { m[3] = -12.0f; }, false},
        {"a w lane NaN", [](float* m) { m[11] = std::numeric_limits<float>::quiet_NaN(); }, false},
        {"a w lane infinite", [](float* m) { m[3] = std::numeric_limits<float>::infinity(); }, false},
        {"the translation NaN", [](float* m) { m[13] = std::numeric_limits<float>::quiet_NaN(); }, false},
        {"the translation infinite", [](float* m) { m[14] = -std::numeric_limits<float>::infinity(); }, false},
        {"the up row 5 degrees off square",
         [](float* m) {
             const float c = std::cos(0.0872665f), s = std::sin(0.0872665f);
             for (int i = 0; i < 3; ++i)
                 m[4 + i] = c * m[4 + i] + s * m[i];
         },
         false},
        {"the up row 2 degrees off square",
         [](float* m) {
             const float c = std::cos(0.0349066f), s = std::sin(0.0349066f);
             for (int i = 0; i < 3; ++i)
                 m[4 + i] = c * m[4 + i] + s * m[i];
         },
         true},
        {"the back row 10% long", [](float* m) { for (int i = 8; i < 11; ++i) m[i] *= 1.1f; }, false},
        {"the right row NaN", [](float* m) { m[1] = std::numeric_limits<float>::quiet_NaN(); }, false},
    };
    int spoiledBad = 0;
    char wrong[512] = "";
    for (const Spoil& s : spoils) {
        float m[16];
        std::memcpy(m, good, sizeof(m));
        s.spoil(m);
        const bool taken = viewturn::Classify(m, true) == viewturn::kScripted;
        if (taken != s.taken) {
            ++spoiledBad;
            std::snprintf(wrong + std::strlen(wrong), sizeof(wrong) - std::strlen(wrong), " %s;", s.what);
        }
    }
    Report("3b", "refused: w lanes too large, NaN or infinite, the place NaN or infinite, rows off square or long",
           spoiledBad == 0, "%d of %d spoiled cameras taken wrongly%s", spoiledBad,
           static_cast<int>(sizeof(spoils) / sizeof(spoils[0])), wrong);

    // Random floats are never cameras; the old IsCamera and Classify agree on everything.
    constexpr int kRandom = 200000;
    int randomTaken = 0, disagree = 0, ordinaryTaken = 0;
    for (int n = 0; n < kRandom; ++n) {
        float m[16];
        if (n % 2 == 0) {
            for (float& f : m)
                f = static_cast<float>(Uniform(-2, 2));
        } else {
            // Near-cameras: a camera's rows lengthened or shortened up to 4%, the w lanes 0 or filled, some
            // lanes spoiled.
            MakeCamera(m, RandomRotation(), Coin() ? 1 : -1, RandomPlace(), Coin(0.3));
            for (int r = 0; r < 3; ++r) {
                const float scale = static_cast<float>(Uniform(0.96, 1.04));
                for (int i = 0; i < 3; ++i)
                    m[4 * r + i] *= scale;
            }
            if (Coin(0.2))
                m[4 * static_cast<int>(Uniform(0, 3)) + 3] = static_cast<float>(Uniform(-0.01, 0.01));
        }
        const bool before = old::IsCamera(m);
        const viewturn::Kind on = viewturn::Classify(m, true), off = viewturn::Classify(m, false);
        disagree += before != (on == viewturn::kOrdinary) || before != (off == viewturn::kOrdinary) ||
                    before != viewturn::IsCamera(m) || (off == viewturn::kScripted);
        if (n % 2 == 0) {
            randomTaken += on == viewturn::kScripted;
            ordinaryTaken += on == viewturn::kOrdinary;
        }
    }
    Report("3c", "random floats never taken; the player's kind exactly as IsCamera always took it",
           randomTaken == 0 && ordinaryTaken == 0 && disagree == 0,
           "%d of %d random matrices taken as scripted, %d as the player's; %d of %d disagree with the old IsCamera",
           randomTaken, kRandom / 2, ordinaryTaken, disagree, kRandom);
}

// ---- 4. A scripted camera turned ----
struct TurnStats {
    double forward = 0, rows = 0, body = 0, shown = 0;  // the worst: degrees, numbers, degrees, degrees
    int keptBad = 0, takenBad = 0, notFinite = 0, runs = 0;
};

// One scripted camera turned with one head, measured against what's wanted.
void TurnScripted(const float* cam, const Q& head, TurnStats& s) {
    float m[16];
    std::memcpy(m, cam, sizeof(m));
    const camera::ViewTurn turn = TurnFor(head);
    viewturn::Level level;
    const float taken = viewturn::Turn(m, turn, true, Hand(cam), false, level);
    ++s.runs;
    if (!Finite(m)) {
        ++s.notFinite;
        return;
    }
    const Q want = Wanted(cam, HeadOf(turn));
    s.forward = std::max(s.forward, AngleDeg(Row(m, 2) * -1.0, Rotate(want, kForward)));
    s.rows = std::max(s.rows, RowsOff(m, want, Hand(cam)));
    const int kept[] = {3, 7, 11, 12, 13, 14, 15};
    for (int i : kept)
        s.keptBad += std::memcmp(&m[i], &cam[i], sizeof(float)) != 0;
    s.takenBad += taken != 0.0f;
    // The picture's body as camera.cpp publishes it for a scripted camera (its yaw as made level, BF3's sense) and
    // xr_runtime.cpp reads it (camera + taken): the camera's own yaw; the yaw shown, that less the head's.
    const double body = Deg(std::atan2(level.back[0], -level.back[1]) + taken);
    const double cameraYaw = 180.0 - Deg(YawOf(Row(cam, 2) * -1.0));
    s.body = std::max(s.body, std::fabs(Wrap(body - cameraYaw)));
    const V3 headForward = Rotate(HeadOf(turn), kForward);
    if (std::fabs(headForward.y) < 0.95) {
        const double shown = Deg(std::atan2(m[8], -m[10]));
        s.shown = std::max(s.shown, std::fabs(Wrap(shown - (body - Deg(YawOf(headForward))))));
    }
}

void Check4() {
    TurnStats logged, synth;
    for (const Logged& l : kLogged) {
        float m[16];
        LoggedCamera(l, m);
        for (int k = 0; k < 200; ++k)
            TurnScripted(m, RandomHead(), logged);
    }
    for (int n = 0; n < 20000; ++n) {
        float m[16];
        MakeCamera(m, RandomScripted(80), 1, RandomPlace(), true);
        TurnScripted(m, RandomHead(), synth);
    }
    auto report = [](const char* id, const char* name, const TurnStats& s) {
        char what[128];
        std::snprintf(what, sizeof(what), "%s turned: the camera's yaw times the head, the w lanes and the place kept",
                      name);
        Report(id, what,
               s.notFinite == 0 && s.forward < 0.01 && s.rows < 1e-5 && s.keptBad == 0 && s.takenBad == 0 &&
                   s.body < 0.01 && s.shown < 0.01,
               "%d turns: the forward at most %.5f deg off, the rows %.2g; %d w lanes or places changed; %d took a yaw "
               "out; the body %.5f deg from the camera's yaw, the yaw shown %.5f deg from the body less the head's; %d "
               "not finite",
               s.runs, s.forward, s.rows, s.keptBad, s.takenBad, s.body, s.shown, s.notFinite);
    };
    report("4a", "the logged scripted cameras", logged);
    report("4b", "synthetic scripted cameras", synth);
}

// ---- 5. The horizon ----
void Check5() {
    double levelUp = 0, levelFlat = 0, pitchedFlat = 0, rolled = 0;
    for (int n = 0; n < 5000; ++n) {
        float cam[16];
        MakeCamera(cam, RandomScripted(80), 1, RandomPlace(), true);
        const double yaw = Uniform(-kPi, kPi), pitch = Rad(Uniform(-80, 80)), roll = Rad(Uniform(-45, 45));
        const Q heads[3] = {Yaw(yaw), Mul(Yaw(yaw), Pitch(pitch)), Ypr(yaw, Rad(Uniform(-60, 60)), roll)};
        for (int h = 0; h < 3; ++h) {
            float m[16];
            std::memcpy(m, cam, sizeof(m));
            viewturn::Level level;
            viewturn::Turn(m, TurnFor(heads[h]), true, 1, false, level);
            if (h == 0) {
                levelUp = std::max(levelUp, Len(Row(m, 1) - kY));
                levelFlat = std::max({levelFlat, Abs(m[1]), Abs(m[9])});
            } else if (h == 1) {
                pitchedFlat = std::max(pitchedFlat, Abs(m[1]));
            } else {
                rolled = std::max(rolled, std::fabs(Wrap(Deg(std::atan2(m[1], m[5]) - roll))));
            }
        }
    }
    Report("5", "the horizon level: only the head tips it", levelUp < 1e-6 && levelFlat < 1e-6 && pitchedFlat < 1e-6 &&
                                                            rolled < 0.01,
           "a level head: up at most %.2g from (0 1 0), the right and back rows %.2g from flat; pitched as well: the "
           "right row %.2g from flat; rolled: the picture's roll at most %.5f deg from the head's",
           levelUp, levelFlat, pitchedFlat, rolled);
}

// ---- 6. The eye shift ----
void Check6() {
    double along = 0, across = 0, levelEyes = 0, refusedTilt = 0;
    for (int n = 0; n < 5000; ++n) {
        float cam[16];
        if (n < kLoggedCount)
            LoggedCamera(kLogged[n], cam);
        else
            MakeCamera(cam, RandomScripted(80), 1, RandomPlace(), true);
        const bool levelHead = n % 2 == 0;
        const Q head = levelHead ? Yaw(Uniform(-kPi, kPi)) : RandomHead();
        float m[16];
        std::memcpy(m, cam, sizeof(m));
        viewturn::Level level;
        viewturn::Turn(m, TurnFor(head), true, 1, false, level);
        // views.cpp: each eye moved along the turned main camera's right row by its offset (left negative).
        const double ipd = 0.064;
        const V3 left = Row(m, 0) * (-ipd / 2), right = Row(m, 0) * (ipd / 2);
        const V3 eyes = right - left;
        const Q want = Wanted(cam, head);
        along = std::max(along, Len(eyes * (1 / ipd) - Rotate(want, kX)));
        across = std::max(across, std::fabs(Dot(eyes, Row(m, 2))));
        if (levelHead)
            levelEyes = std::max(levelEyes, std::fabs(eyes.y));
        // Refused, the eyes went along the camera's own right: one above the other as it rolled (the logged ones).
        if (n < kLoggedCount)
            refusedTilt = std::max(refusedTilt, std::fabs(ipd * cam[1]));
    }
    Report("6", "the eye shift along the head's right", along < 1e-5 && across < 1e-6 && levelEyes < 1e-6,
           "the eyes' line at most %.2g from the head's right turned by the camera's yaw, %.2g along the forward; %.2g "
           "m one above the other for a level head (refused, the logged cameras' eyes up to %.0f mm one above the "
           "other: their roll)",
           along, across, levelEyes, refusedTilt * 1000);
}

// ---- 7. Mirrored ----
void Check7() {
    double fixedOff = 0, unfixedOff = 0;
    for (int n = 0; n < 5000; ++n) {
        const Q q = RandomScripted(80), head = RandomHead();
        const V3 at = RandomPlace();
        float twin[16], mirror[16];
        MakeCamera(twin, q, 1, at, true);
        MakeCamera(mirror, q, -1, at, true);
        // Each made level from no last frame (a run's first), as the twins would each be.
        viewturn::Level forTwin, forMirror;
        // With mirrorfix: the twin's picture, its right row the other way.
        float a[16], b[16];
        std::memcpy(a, twin, sizeof(a));
        std::memcpy(b, mirror, sizeof(b));
        viewturn::Turn(a, TurnFor(head), true, 1, false, forTwin);
        viewturn::Turn(b, TurnFor(head), true, -1, true, forMirror);
        fixedOff =
            std::max({fixedOff, Len(Row(b, 0) + Row(a, 0)), Len(Row(b, 1) - Row(a, 1)), Len(Row(b, 2) - Row(a, 2))});
        // Without ("mirrorfix 0"): the twin's picture for the head's yaw and roll the other way.
        const Q reversed{head.x, -head.y, -head.z, head.w};
        std::memcpy(a, twin, sizeof(a));
        std::memcpy(b, mirror, sizeof(b));
        forTwin = forMirror = viewturn::Level{};
        viewturn::Turn(a, TurnFor(reversed), true, 1, false, forTwin);
        viewturn::Turn(b, TurnFor(head), true, -1, false, forMirror);
        unfixedOff =
            std::max({unfixedOff, Len(Row(b, 0) + Row(a, 0)), Len(Row(b, 1) - Row(a, 1)), Len(Row(b, 2) - Row(a, 2))});
    }
    Report("7a", "mirrored: the head's turn the same way in the world (mirrorfix), the other way without",
           fixedOff < 1e-5 && unfixedOff < 1e-5,
           "with mirrorfix at most %.2g from the twin's rows (its right the other way), without %.2g from the twin's "
           "with the head's yaw and roll reversed",
           fixedOff, unfixedOff);

    // As the turn before this change gave the same camera with its w lanes 0 and nothing taken out (no look yaw, the
    // held yaw 0), mirrored as camera.cpp says when the player's camera is right-handed.
    double levelOff = 0, pitchedOff = 0, rolledOldRoll = 0, rolledNewRoll = 0;
    for (int n = 0; n < 5000; ++n) {
        const int hand = n % 2 == 0 ? 1 : -1;
        const double yaw = Uniform(-kPi, kPi);
        const Q head = Yaw(Uniform(-kPi, kPi));  // level, so a roll left in shows plainly
        const Q cameras[3] = {Yaw(yaw), Mul(Yaw(yaw), Pitch(Rad(Uniform(-80, 80)))),
                              Ypr(yaw, Rad(Uniform(-30, 30)), Rad(20))};
        for (int c = 0; c < 3; ++c) {
            float filled[16], zero[16];
            const V3 at = RandomPlace();
            MakeCamera(filled, cameras[c], hand, at, true);
            MakeCamera(zero, cameras[c], hand, at, false);
            camera::ViewTurn turn = TurnFor(head);
            turn.controllerYaw = 0.0f;
            turn.haveLook = false;
            viewturn::Level level;
            viewturn::Turn(filled, turn, true, hand, hand < 0, level);
            old::Turn(zero, turn, hand < 0);
            double off = 0;
            for (int r = 0; r < 3; ++r)
                off = std::max(off, Len(Row(filled, r) - Row(zero, r)));
            if (c == 0)
                levelOff = std::max(levelOff, off);
            else if (c == 1)
                pitchedOff = std::max(pitchedOff, off);
            else {
                rolledOldRoll = std::max(rolledOldRoll, std::fabs(Deg(std::atan2(hand * zero[1], zero[5]))));
                rolledNewRoll = std::max(rolledNewRoll, std::fabs(Deg(std::atan2(hand * filled[1], filled[5]))));
            }
        }
    }
    Report("7b", "a level or only pitched scripted camera turned as the old turn turned it, mirrored or not",
           levelOff < 1e-5 && pitchedOff < 1e-5 && rolledNewRoll < 1e-4 && rolledOldRoll > 10,
           "level at most %.2g from the old turn's rows, pitched %.2g; rolled 20 degrees, the old turn left up to %.2f "
           "deg of roll in for a level head, this one %.5f",
           levelOff, pitchedOff, rolledOldRoll, rolledNewRoll);
}

// ---- 8. A forward near the vertical ----
void Check8() {
    // Pitching down to the vertical and back up, 90 deg/s at 72 Hz, the yaw still (rolled, and not), one run.
    double sweepOff = 0, sweepStep = 0;
    for (double roll : {0.0, 25.0}) {
        const double yaw = Rad(30);
        viewturn::Level level;
        double last = 0;
        bool haveLast = false;
        for (int frame = 0; frame <= 144; ++frame) {
            const double pitch = -std::min(frame, 144 - frame) * 1.25;  // 0 down to -90 and back
            float m[16];
            MakeCamera(m, Ypr(yaw, Rad(pitch), Rad(roll)), 1, {0, 0, 0}, true);
            viewturn::Turn(m, TurnFor(Q{}), true, 1, false, level);
            const double shown = Deg(YawOf(Row(m, 2) * -1.0));
            sweepOff = std::max(sweepOff, std::fabs(Wrap(shown - Deg(yaw))));
            if (haveLast)
                sweepStep = std::max(sweepStep, std::fabs(Wrap(shown - last)));
            last = shown, haveLast = true;
        }
    }
    // Straight down and straight up with no yaw kept: along the ground the way the picture's top points (down), the
    // other way (up): for a camera that doesn't roll, its own yaw.
    double downOff = 0, upOff = 0;
    for (int n = 0; n < 1000; ++n) {
        const double yaw = Uniform(-kPi, kPi);
        for (double pitch : {-90.0, 90.0}) {
            float m[16];
            MakeCamera(m, Mul(Yaw(yaw), Pitch(Rad(pitch))), 1, {0, 0, 0}, true);
            const V3 top = Row(m, 1);
            viewturn::Level level;
            viewturn::Turn(m, TurnFor(Q{}), true, 1, false, level);
            const V3 forward = Row(m, 2) * -1.0;
            double& worst = pitch < 0 ? downOff : upOff;
            worst = std::max({worst, AngleDeg(forward, Unit(V3(top.x, 0, top.z) * (pitch < 0 ? 1.0 : -1.0))),
                              std::fabs(Wrap(Deg(YawOf(forward) - yaw)))});
        }
    }
    // Random ones within 6 degrees of the vertical, going on from a last frame a few degrees away (its yaw any way)
    // and from none, any roll, the head any way.
    int bad = 0;
    double square = 0;
    for (int n = 0; n < 20000; ++n) {
        const double pitch = (Coin() ? 1 : -1) * Uniform(84, 90);
        const Q q = Ypr(Uniform(-kPi, kPi), Rad(pitch), Rad(Uniform(-180, 180)));
        const int camHand = Coin(0.25) ? -1 : 1;
        float m[16];
        MakeCamera(m, q, camHand, RandomPlace(), true);
        viewturn::Level level;
        if (Coin()) {
            float last[16];
            MakeCamera(last, Mul(AxisAngle({Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)}, Rad(Uniform(0, 5))), q),
                       camHand, RandomPlace(), true);
            viewturn::MakeLevel(last, camHand, level);
            const double kept = Uniform(-kPi, kPi);
            level.back[0] = static_cast<float>(std::sin(kept)), level.back[1] = static_cast<float>(std::cos(kept));
        }
        const int hand = Hand(m);
        float flat[16];
        std::memcpy(flat, m, sizeof(flat));
        viewturn::Level forFlat = level;
        viewturn::MakeLevel(flat, hand, forFlat);
        viewturn::Turn(m, TurnFor(RandomHead()), true, hand, false, level);
        bad += !Finite(m) || !Finite(flat) || Len(Row(flat, 1) - kY) > 1e-6 || std::fabs(flat[1]) > 1e-6 ||
               std::fabs(flat[9]) > 1e-6;
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b)
                square = std::max(square, std::fabs(Dot(Row(m, a), Row(m, b)) - (a == b ? 1.0 : 0.0)));
    }
    Report("8", "a forward near the vertical: the yaw kept, or the picture's top along the ground",
           sweepOff < 0.01 && sweepStep < 0.01 && downOff < 0.01 && upOff < 0.01 && bad == 0 && square < 1e-5,
           "pitching to 90 and back: the yaw at most %.5f deg off, %.5f a frame; straight down %.5f deg from the "
           "picture's top, straight up %.5f from the other way; %d of 20000 within 6 degrees not finite or not level, "
           "the rows %.2g from square",
           sweepOff, sweepStep, downOff, upOff, bad, square);
}

// ---- 9. Offered again once turned ----
// camera.cpp's again guard: a camera without a look yaw whose yaw is the last frame's shown yaw within kRepeatSame,
// more than kRepeatJump from the last frame's camera, and its pitch the last shown pitch within kRepeatPitch.
constexpr double kRepeatSame = 0.1, kRepeatJump = 5.0, kRepeatPitch = 0.5;  // degrees (camera.cpp)
struct Last {
    double cameraYaw, shownYaw, shownPitch;  // BF3's yaws and the pitch, degrees
};
double BfYaw(const float* m) {
    return Deg(std::atan2(m[8], -m[10]));
}
double BfPitch(const float* m) {
    return Deg(std::asin(std::clamp(-m[9], -1.0f, 1.0f)));
}
bool Again(const float* m, const Last& last) {
    return std::fabs(Wrap(BfYaw(m) - last.shownYaw)) < kRepeatSame &&
           std::fabs(Wrap(BfYaw(m) - last.cameraYaw)) > kRepeatJump && std::fabs(BfPitch(m) - last.shownPitch) < kRepeatPitch;
}

void Check9() {
    // Turned twice: the head's yaw counted twice.
    float m[16];
    LoggedCamera(kLogged[9], m);  // the drive, 00:36:12: pitched 24 degrees down
    const Q head = Mul(Yaw(Rad(30)), Pitch(Rad(10)));
    float once[16], twice[16];
    std::memcpy(once, m, sizeof(once));
    viewturn::Level level;
    viewturn::Turn(once, TurnFor(head), true, 1, false, level);
    std::memcpy(twice, once, sizeof(twice));
    const bool stillScripted = viewturn::Classify(once, true) == viewturn::kScripted;
    viewturn::Turn(twice, TurnFor(head), true, 1, false, level);
    const double cameraYaw = Deg(YawOf(Row(m, 2) * -1.0));
    const double onceOff = Wrap(Deg(YawOf(Row(once, 2) * -1.0)) - cameraYaw);
    const double twiceOff = Wrap(Deg(YawOf(Row(twice, 2) * -1.0)) - cameraYaw);
    const Last last{BfYaw(m), BfYaw(once), BfPitch(once)};
    const bool caught = Again(once, last);

    // A drive: the car turning at up to 60 deg/s and swaying, the camera bobbing, the head looking about; each frame's
    // fresh camera against the last frame's: none passes for one already turned.
    int passed = 0, frames = 0;
    double carYaw = 0;
    Last ref{};
    for (int frame = 0; frame < 72 * 120; ++frame) {
        const double t = frame / 72.0;
        carYaw += Rad(60 * std::sin(t * 0.7)) / 72.0;
        const double headYaw = Rad(70 * std::sin(t * 0.37) + 20 * std::sin(t * 2.3));
        float fresh[16];
        MakeCamera(fresh, Ypr(carYaw, Rad(3 * std::sin(t * 9) - 5), Rad(4 * std::sin(t * 5.3))), 1,
                   {100 * t, 1.4, 20 * std::sin(t)}, true);
        if (frame > 0) {
            ++frames;
            passed += Again(fresh, ref);
        }
        float turned[16];
        std::memcpy(turned, fresh, sizeof(turned));
        viewturn::Turn(turned, TurnFor(Mul(Yaw(headYaw), Pitch(Rad(10 * std::sin(t * 0.9))))), true, 1, false, level);
        ref = {BfYaw(fresh), BfYaw(turned), BfPitch(turned)};
    }
    Report("9", "offered again: the head's yaw twice if turned twice, which the again guard catches",
           stillScripted && std::fabs(onceOff - 30) < 0.01 && std::fabs(twiceOff - 60) < 0.01 && caught && passed == 0,
           "turned once %.3f deg from the camera's yaw, twice %.3f (a 30-degree head); the turned camera %s for one "
           "turned (still taken as scripted: %s); %d of %d fresh cameras of a drive passed for one",
           onceOff, twiceOff, caught ? "passes" : "doesn't pass", stillScripted ? "yes" : "no", passed, frames);
}

// ---- 10. The w lanes ----
void Check10() {
    double off[3] = {};
    const char* worst[3] = {"", "", ""};
    for (const Logged& l : kLogged) {
        const Q q = RotationOf({l.up[0], l.up[1], l.up[2]}, {l.back[0], l.back[1], l.back[2]});
        double w[3];
        FilledLanes(q, w);
        for (int i = 0; i < 3; ++i)
            if (std::fabs(w[i] - l.w[i]) > off[i])
                off[i] = std::fabs(w[i] - l.w[i]), worst[i] = l.at;
    }
    Report("10", "the w lanes are the rotation's quaternion products", off[0] < 0.06 && off[1] < 0.06 && off[2] < 0.06,
           "%d logged scripted cameras: 2(yw - xy) at most %.3f from m[3] (%s), -2(xy + yw) %.3f from m[7] (%s), "
           "ww + xx - yy - zz %.3f from m[11] (%s)",
           kLoggedCount, off[0], worst[0], off[1], worst[1], off[2], worst[2]);
}

// ---- 11. The time a turn takes ----
void Check11() {
    float cameras[64][16];
    camera::ViewTurn turns[64];
    for (int k = 0; k < 64; ++k) {
        MakeCamera(cameras[k], RandomScripted(89), 1, RandomPlace(), true);
        turns[k] = TurnFor(RandomHead());
    }
    LARGE_INTEGER frequency, a, b;
    QueryPerformanceFrequency(&frequency);
    viewturn::Level level;
    volatile float sink = 0;
    constexpr int kTurns = 2000000;
    QueryPerformanceCounter(&a);
    for (int n = 0; n < kTurns; ++n) {
        float m[16];
        std::memcpy(m, cameras[n & 63], sizeof(m));
        viewturn::Turn(m, turns[n & 63], true, 1, false, level);
        sink = sink + m[0];
    }
    QueryPerformanceCounter(&b);
    const double us =
        static_cast<double>(b.QuadPart - a.QuadPart) * 1e6 / static_cast<double>(frequency.QuadPart) / kTurns;
    // With its first-person camera (the same rows) turned along, as views.cpp has each eye's main camera turned.
    QueryPerformanceCounter(&a);
    for (int n = 0; n < kTurns; ++n) {
        float m[16], body[16];
        std::memcpy(m, cameras[n & 63], sizeof(m));
        std::memcpy(body, cameras[n & 63], sizeof(body));
        viewturn::Turn(m, turns[n & 63], true, 1, false, level, body);
        sink = sink + m[0] + body[0];
    }
    QueryPerformanceCounter(&b);
    const double withBody =
        static_cast<double>(b.QuadPart - a.QuadPart) * 1e6 / static_cast<double>(frequency.QuadPart) / kTurns;
    Report("11", "the time a scripted camera's turn takes", us < 1.0 && withBody < 1.0,
           "%.3f us a turn, %.3f with its first-person camera turned along (< 1)", us, withBody);
}

// ---- Made level frame after frame ----
// A camera made level (the Level going on), the camera itself left as it was: the yaw it was made level about, BF3's
// sense, degrees.
double NewLevel(const float* cam, viewturn::Level& level) {
    float m[16];
    std::memcpy(m, cam, sizeof(m));
    viewturn::MakeLevel(m, Hand(cam), level);
    return Deg(std::atan2(level.back[0], -level.back[1]));
}
double OldLevel(const float* cam, old::Level& level) {
    float m[16];
    std::memcpy(m, cam, sizeof(m));
    old::MakeLevel(m, Hand(cam), level);
    return Deg(std::atan2(level.back[0], -level.back[1]));
}
// A camera's own yaw, BF3's sense, degrees (viewturn::OwnYaw).
double OwnLevel(const float* cam) {
    float x = 0.0f, z = 0.0f;
    viewturn::OwnYaw(cam, x, z);
    return Deg(std::atan2(x, -z));
}
// A camera's rows turned by degrees about a line in the world (OpenXR's sense: about up, turning left), the rest kept.
void TurnedAbout(const float* cam, const V3& line, double degrees, float* out) {
    std::memcpy(out, cam, 16 * sizeof(float));
    const Q q = AxisAngle(line, Rad(degrees));
    for (int r = 0; r < 3; ++r) {
        const V3 v = Rotate(q, Row(cam, r));
        out[4 * r] = static_cast<float>(v.x), out[4 * r + 1] = static_cast<float>(v.y);
        out[4 * r + 2] = static_cast<float>(v.z);
    }
}
// Level, square and finite: a camera's rows as MakeLevel leaves them.
bool LevelAndSquare(const float* m) {
    if (!Finite(m) || std::fabs(m[1]) > 1e-6 || std::fabs(m[9]) > 1e-6 || Len(Row(m, 1) - kY) > 1e-6)
        return false;
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b)
            if (std::fabs(Dot(Row(m, a), Row(m, b)) - (a == b ? 1.0 : 0.0)) > 1e-5)
                return false;
    return true;
}

// ---- 12. The finding: how far a turn of the camera turns the yaw it's made level about ----
struct Response {
    double atOnce = 0, inASecond = 0, oldAtOnce = 0, pan = 0, oldPan = 0;
};

// A camera held still (200 frames), then turned 1 degree about its own up, right and back rows and two random lines,
// both ways: the most the yaw it's made level about turns at once and within a second held there (72 frames), and
// at 833abbd at once (from the still camera's frame, as the game ran it); the turn about its own up (a pan) alone.
Response RespondTo(const float* cam) {
    Response r;
    viewturn::Level still;
    double y0 = 0;
    for (int f = 0; f < 200; ++f)
        y0 = NewLevel(cam, still);
    old::Level oldStill;
    const double oldY0 = OldLevel(cam, oldStill);
    const V3 lines[5] = {Row(cam, 1), Row(cam, 0), Row(cam, 2), {Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)},
                         {Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)}};
    for (int k = 0; k < 5; ++k) {
        for (double way : {-1.0, 1.0}) {
            float t[16];
            TurnedAbout(cam, lines[k], way, t);
            viewturn::Level a = still;
            const double atOnce = std::fabs(Wrap(NewLevel(t, a) - y0));
            viewturn::Level b = still;
            double y = 0;
            for (int f = 0; f < 72; ++f)
                y = NewLevel(t, b);
            old::Level o = oldStill;
            const double oldAtOnce = std::fabs(Wrap(OldLevel(t, o) - oldY0));
            r.atOnce = std::max(r.atOnce, atOnce);
            r.inASecond = std::max({r.inASecond, atOnce, std::fabs(Wrap(y - y0))});
            r.oldAtOnce = std::max(r.oldAtOnce, oldAtOnce);
            if (k == 0) {
                r.pan = std::max(r.pan, atOnce);
                r.oldPan = std::max(r.oldPan, oldAtOnce);
            }
        }
    }
    return r;
}

void Check12() {
    // The logged scripted cameras: the drive's and the interrogation's.
    Response logged, interrogation;
    for (int n = 0; n < kLoggedCount; ++n) {
        float m[16];
        LoggedCamera(kLogged[n], m);
        const Response r = RespondTo(m);
        logged.atOnce = std::max(logged.atOnce, r.atOnce);
        logged.inASecond = std::max(logged.inASecond, r.inASecond);
        logged.oldAtOnce = std::max(logged.oldAtOnce, r.oldAtOnce);
        if (std::strcmp(kLogged[n].at, "Destroyer 23:57:12.775") == 0)
            interrogation = r;
    }
    // Random cameras, any way at all, either hand.
    Response random;
    for (int n = 0; n < 10000; ++n) {
        float m[16];
        MakeCamera(m, RandomRotation(), Coin(0.25) ? -1 : 1, RandomPlace(), true);
        const Response r = RespondTo(m);
        random.atOnce = std::max(random.atOnce, r.atOnce);
        random.inASecond = std::max(random.inASecond, r.inASecond);
        random.oldAtOnce = std::max(random.oldAtOnce, r.oldAtOnce);
    }
    // Every half degree from level to straight down, not rolled and rolled 90 degrees: the pan's turn at once, and how
    // much it changes from one half degree to the next.
    Response sweep;
    double step = 0, oldStep = 0, old84 = 0, old85 = 0;
    for (double roll : {0.0, 90.0}) {
        double lastPan = 0, lastOldPan = 0;
        for (int k = 0; k <= 180; ++k) {
            float m[16];
            MakeCamera(m, Ypr(Rad(30), Rad(-0.5 * k), Rad(roll)), 1, {0, 0, 0}, true);
            const Response r = RespondTo(m);
            sweep.atOnce = std::max(sweep.atOnce, r.atOnce);
            sweep.inASecond = std::max(sweep.inASecond, r.inASecond);
            if (k > 0) {
                step = std::max(step, std::fabs(r.pan - lastPan));
                oldStep = std::max(oldStep, std::fabs(r.oldPan - lastOldPan));
            }
            lastPan = r.pan, lastOldPan = r.oldPan;
            if (roll == 0.0 && k == 168)
                old84 = r.oldPan;
            if (roll == 0.0 && k == 170)
                old85 = r.oldPan;
        }
    }
    const double atOnce = std::max({logged.atOnce, random.atOnce, sweep.atOnce});
    const double inASecond = std::max({logged.inASecond, random.inASecond, sweep.inASecond});
    Report("12", "the review's finding: a 1-degree turn of a scripted camera turns its level yaw at most 1.45 at once",
           atOnce < 1.45 && inASecond < 2.0 && step < 0.15 && interrogation.inASecond < 1.01,
           "at most %.3f deg at once, %.3f within a second (the logged %.3f and %.3f, %d of them; 10000 random %.3f and "
           "%.3f; level to straight down %.3f and %.3f); the interrogation's camera of 23:57:12 %.3f at once (at "
           "833abbd %.2f); a pan's turn changing at most %.3f deg from one half degree of pitch to the next (at 833abbd "
           "%.2f: %.2f at 84 degrees, %.2f at 85); at 833abbd at once the logged ones up to %.2f, random ones %.2f",
           atOnce, inASecond, logged.atOnce, logged.inASecond, kLoggedCount, random.atOnce, random.inASecond,
           sweep.atOnce, sweep.inASecond, interrogation.atOnce, interrogation.oldAtOnce, step, oldStep, old84, old85,
           logged.oldAtOnce, random.oldAtOnce);
}

// ---- 13. The drive of Comrades as at 833abbd ----
void Check13() {
    // Cameras within 45 degrees of level, any roll within 30, either hand: from no last frame, from one a few degrees
    // away (going on) and from a far one (a cut).
    int bad = 0, runs = 0;
    for (int n = 0; n < 20000; ++n) {
        const Q q = Ypr(Uniform(-kPi, kPi), Rad(Uniform(-44.9, 44.9)), Rad(Uniform(-30, 30)));
        const int hand = Coin(0.25) ? -1 : 1;
        float cam[16], nearby[16], faraway[16];
        MakeCamera(cam, q, hand, RandomPlace(), true);
        MakeCamera(nearby, Mul(AxisAngle({Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)}, Rad(Uniform(0, 5))), q),
                   hand, RandomPlace(), true);
        MakeCamera(faraway, RandomRotation(), hand, RandomPlace(), true);
        for (int k = 0; k < 3; ++k) {
            viewturn::Level level;
            if (k > 0)
                NewLevel(k == 1 ? nearby : faraway, level);
            float a[16], b[16];
            std::memcpy(a, cam, sizeof(a));
            std::memcpy(b, cam, sizeof(b));
            viewturn::MakeLevel(a, hand, level);
            old::Level oldLevel;
            old::MakeLevel(b, hand, oldLevel);
            bad += std::memcmp(a, b, sizeof(a)) != 0;
            ++runs;
        }
    }
    // A drive frame by frame (as check 9's: the car turning at up to 60 deg/s and swaying, the camera bobbing).
    int driveBad = 0, frames = 0;
    {
        viewturn::Level level;
        old::Level oldLevel;
        double carYaw = 0;
        for (int frame = 0; frame < 72 * 120; ++frame) {
            const double t = frame / 72.0;
            carYaw += Rad(60 * std::sin(t * 0.7)) / 72.0;
            float cam[16], a[16], b[16];
            MakeCamera(cam, Ypr(carYaw, Rad(3 * std::sin(t * 9) - 5), Rad(4 * std::sin(t * 5.3))), 1,
                       {100 * t, 1.4, 20 * std::sin(t)}, true);
            std::memcpy(a, cam, sizeof(a));
            std::memcpy(b, cam, sizeof(b));
            viewturn::MakeLevel(a, 1, level);
            old::MakeLevel(b, 1, oldLevel);
            driveBad += std::memcmp(a, b, sizeof(a)) != 0;
            ++frames;
        }
    }
    // The logged drive cameras, going on from themselves a degree away: the steepest (45.3 down) goes on a hair.
    double loggedOff = 0;
    int logged = 0;
    for (const Logged& l : kLogged) {
        if (std::strncmp(l.at, "Comrades", 8) != 0)
            continue;
        float cam[16], last[16];
        LoggedCamera(l, cam);
        TurnedAbout(cam, {0.3, 1, 0.2}, 1.0, last);
        viewturn::Level level;
        NewLevel(last, level);
        old::Level oldLevel;
        loggedOff = std::max(loggedOff, std::fabs(Wrap(NewLevel(cam, level) - OldLevel(cam, oldLevel))));
        ++logged;
    }
    Report("13", "the drive of Comrades as at 833abbd: within 45 degrees of level, the rows made level bit for bit",
           bad == 0 && driveBad == 0 && loggedOff < 0.01,
           "%d of %d random cameras (no last frame, a near one, a far one) and %d of %d frames of a drive differ from "
           "833abbd's; the %d logged drive cameras going on from a degree away at most %.5f deg from it",
           bad, runs, driveBad, frames, logged, loggedOff);
}

// ---- 14. Going on from the last frame ----
void Check14() {
    // A steep camera spinning about the world's up, 30 deg/s for 2 s (BF3's yaw falls as it turns left).
    double spinOff = 0;
    for (double pitch : {65.0, 75.0, 85.0, 89.0}) {
        for (double roll : {0.0, 40.0}) {
            float cam[16];
            MakeCamera(cam, Ypr(Rad(20), Rad(-pitch), Rad(roll)), 1, {0, 0, 0}, true);
            viewturn::Level level;
            const double y0 = NewLevel(cam, level);
            for (int f = 1; f <= 144; ++f) {
                float t[16];
                TurnedAbout(cam, kY, 30.0 * f / 72, t);
                spinOff = std::max(spinOff, std::fabs(Wrap(NewLevel(t, level) - y0 + 30.0 * f / 72)));
            }
        }
    }
    // Panning about its own up, 20 degrees in 2 s, looking 70, 80 and 85 degrees down: the yaw turns as far as the
    // camera turned about the world's up (each frame's turn's twist about it, summed), its own yaw much further.
    double panOff = 0, ownMost = 0, levelMost = 0;
    for (double pitch : {70.0, 80.0, 85.0}) {
        float cur[16];
        MakeCamera(cur, Ypr(0, Rad(-pitch), 0), 1, {0, 0, 0}, true);
        viewturn::Level level;
        const double y0 = NewLevel(cur, level), own0 = OwnLevel(cur);
        double aboutUp = 0;
        for (int f = 0; f < 144; ++f) {
            const V3 up = Row(cur, 1);
            const Q step = AxisAngle(up, Rad(20.0 / 144));
            aboutUp += Deg(2 * std::atan2(step.y, step.w));
            float t[16];
            TurnedAbout(cur, up, 20.0 / 144, t);
            std::memcpy(cur, t, sizeof(cur));
            NewLevel(cur, level);
        }
        const double turned = Wrap(Deg(std::atan2(level.back[0], -level.back[1])) - y0);
        panOff = std::max(panOff, std::fabs(turned + aboutUp));
        levelMost = std::max(levelMost, std::fabs(turned));
        ownMost = std::max(ownMost, std::fabs(Wrap(OwnLevel(cur) - own0)));
    }
    // A cut: 45 degrees from the last frame's camera, its own yaw, bit for bit (as from no last frame).
    int cutBad = 0;
    for (int n = 0; n < 2000; ++n) {
        const Q q = RandomScripted(89);
        float last[16], cam[16];
        MakeCamera(last, q, 1, RandomPlace(), true);
        MakeCamera(cam, Mul(AxisAngle({Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)}, Rad(45)), q), 1, RandomPlace(),
                   true);
        viewturn::Level level, none;
        NewLevel(last, level);
        float a[16], b[16];
        std::memcpy(a, cam, sizeof(a));
        std::memcpy(b, cam, sizeof(b));
        viewturn::MakeLevel(a, 1, level);
        viewturn::MakeLevel(b, 1, none);
        cutBad += std::memcmp(a, b, sizeof(a)) != 0;
    }
    // Tumbling, 2 degrees a frame through 360: over the top, rolling at 17 and 75 degrees down, about a random line.
    int tumbleBad = 0;
    double tumbleStep[4] = {};
    for (int kind = 0; kind < 4; ++kind) {
        const Q start = kind == 1 ? Pitch(Rad(-17)) : kind == 2 ? Pitch(Rad(-75)) : Q{};
        const V3 line = kind == 0 ? kX : kind == 3 ? V3(0.4, 0.7, -0.3) : Rotate(start, kZ);
        viewturn::Level level;
        double last = 0;
        for (int f = 0; f <= 180; ++f) {
            float m[16];
            MakeCamera(m, Mul(AxisAngle(line, Rad(2.0 * f)), start), 1, {0, 0, 0}, true);
            float flat[16];
            std::memcpy(flat, m, sizeof(flat));
            viewturn::MakeLevel(flat, 1, level);
            tumbleBad += !LevelAndSquare(flat);
            const double y = Deg(std::atan2(level.back[0], -level.back[1]));
            if (f > 0)
                tumbleStep[kind] = std::max(tumbleStep[kind], std::fabs(Wrap(y - last)));
            last = y;
        }
    }
    // A frame's cameras (an eye's and the other eye's copy), looking 55 degrees down, its yaw going on from a last frame
    // 2 degrees away whose yaw was 10 off its own: from the same last frame alike; one after the other from one Level
    // (the second taken a share further), apart.
    int frameBad = 0;
    double apart = 0;
    for (int n = 0; n < 1000; ++n) {
        const Q q = Ypr(Uniform(-kPi, kPi), Rad(-55), Rad(Uniform(-30, 30)));
        float last[16], cam[16];
        MakeCamera(last, Mul(AxisAngle({Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)}, Rad(2)), q), 1, RandomPlace(),
                   true);
        MakeCamera(cam, q, 1, RandomPlace(), true);
        viewturn::Level latched;
        NewLevel(last, latched);
        const float c = std::cos(0.1745f), s = std::sin(0.1745f);
        const float x = latched.back[0], z = latched.back[1];
        latched.back[0] = x * c + z * s, latched.back[1] = z * c - x * s;
        viewturn::Level forOne = latched, forOther = latched, shared = latched;
        float one[16], other[16];
        std::memcpy(one, cam, sizeof(one));
        std::memcpy(other, cam, sizeof(other));
        viewturn::MakeLevel(one, 1, forOne);
        viewturn::MakeLevel(other, 1, forOther);
        frameBad += std::memcmp(one, other, sizeof(one)) != 0;
        const double first = NewLevel(cam, shared), second = NewLevel(cam, shared);
        apart = std::max(apart, std::fabs(Wrap(second - first)));
    }
    Report("14", "going on from the last frame: the turns about the world's up, a cut, tumbling, a frame's cameras",
           spinOff < 0.01 && panOff < 0.01 && levelMost < 7.0 && ownMost > 40 && cutBad == 0 && tumbleBad == 0 &&
               tumbleStep[1] < 0.01 && frameBad == 0 && apart > 0.01,
           "spinning about the world's up at 65-89 down followed within %.5f deg a frame; panning 20 degrees about its "
           "own up at 70-85 down, the yaw turned at most %.2f deg, %.5f from the turn about the world's up, its own yaw "
           "up to %.1f; %d of 2000 cuts not their own yaw; tumbling, %d frames not level and square, the largest step "
           "a frame %.2f (over the top), %.4f and %.2f (rolling at 17 and 75 down), %.2f (a random line); %d of 1000 "
           "frames' two cameras apart (one after the other from one Level they'd be up to %.2f deg apart)",
           spinOff, levelMost, panOff, ownMost, cutBad, tumbleBad, tumbleStep[0], tumbleStep[1], tumbleStep[2],
           tumbleStep[3], frameBad, apart);
}

// ---- 15. The body: the first-person camera turned with a scripted one ----
// The most any of a's rows' numbers is from b's.
double RowsApart(const float* a, const float* b) {
    double off = 0;
    for (int r = 0; r < 3; ++r)
        for (int i = 0; i < 3; ++i)
            off = std::max(off, std::fabs(static_cast<double>(a[4 * r + i]) - b[4 * r + i]));
    return off;
}
// The w lanes and the place (what TurnWith leaves as they are) as they were, bit for bit.
bool LanesAndPlaceKept(const float* now, const float* was) {
    for (int i : {3, 7, 11, 12, 13, 14, 15})
        if (std::memcmp(&now[i], &was[i], sizeof(float)) != 0)
            return false;
    return true;
}
bool SameLevel(const viewturn::Level& a, const viewturn::Level& b) {
    return a.have == b.have && std::memcmp(a.back, b.back, sizeof(a.back)) == 0 &&
           std::memcmp(a.rows, b.rows, sizeof(a.rows)) == 0;
}
// How far a first-person camera's rows as turned are from the main camera's turn put on them, in double: each of the
// turned main camera's rows taken apart along the main camera's rows as the game made them (Cramer's rule: its parts
// there are its dots with the columns of the main's inverse), the first-person camera's row put together again from
// its rows as the game made them in those parts (in rows: after times before's inverse times the body).
double DeltaOff(const float* turned, const float* body, const float* before, const float* after) {
    const V3 a = Row(before, 0), b = Row(before, 1), c = Row(before, 2);
    const double det = Dot(a, Cross(b, c));
    double off = 0;
    for (int r = 0; r < 3; ++r) {
        const V3 v = Row(after, r);
        const double p0 = Dot(v, Cross(b, c)) / det, p1 = Dot(a, Cross(v, c)) / det, p2 = Dot(a, Cross(b, v)) / det;
        const V3 want = Row(body, 0) * p0 + Row(body, 1) * p1 + Row(body, 2) * p2;
        for (int i = 0; i < 3; ++i)
            off = std::max(off, std::fabs(turned[4 * r + i] - want[i]));
    }
    return off;
}
// A camera's rows turned by q in its own frame (its right, up and back as q turns x, y and z there), the rest kept.
void TurnedInOwnFrame(const float* cam, const Q& q, float* out) {
    float rows[9];
    for (int r = 0; r < 3; ++r) {
        const V3 e = Rotate(q, r == 0 ? kX : r == 1 ? kY : kZ);
        const V3 v = Row(cam, 0) * e.x + Row(cam, 1) * e.y + Row(cam, 2) * e.z;
        rows[3 * r] = static_cast<float>(v.x), rows[3 * r + 1] = static_cast<float>(v.y);
        rows[3 * r + 2] = static_cast<float>(v.z);
    }
    std::memcpy(out, cam, 16 * sizeof(float));
    for (int r = 0; r < 3; ++r)
        for (int i = 0; i < 3; ++i)
            out[4 * r + i] = rows[3 * r + i];
}
// The Level a picture's cameras go on from: none (a run's first), or a last frame's a few degrees away.
viewturn::Level LevelFrom(const float* cam) {
    viewturn::Level level;
    if (Coin()) {
        float last[16];
        TurnedAbout(cam, {Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)}, Uniform(0, 5), last);
        NewLevel(last, level);
    }
    return level;
}

void Check15() {
    // a. Turned with a scripted main camera (no look written): as the main camera, turned from it about a line in the
    // world or in its own frame, its rows a little off unit length and square; the main camera turned bit for bit as
    // without it.
    double asMain = 0, worldTurn = 0, ownTurn = 0, offSquare = 0, delta = 0;
    int keptBad = 0, mainBad = 0, notFinite = 0, unclassified = 0, runs = 0;
    auto picture = [&](const float* cam, const float* body, const Q& head, int hand, bool mirrored, float* mainOut,
                       float* bodyOut) {
        float m[16], b[16], alone[16];
        std::memcpy(m, cam, sizeof(m));
        std::memcpy(b, body, sizeof(b));
        std::memcpy(alone, cam, sizeof(alone));
        const viewturn::Level from = LevelFrom(cam);
        viewturn::Level withBody = from, without = from;
        const camera::ViewTurn turn = TurnFor(head);
        viewturn::Turn(m, turn, true, hand, mirrored, withBody, b);
        viewturn::Turn(alone, turn, true, hand, mirrored, without);
        ++runs;
        mainBad += std::memcmp(m, alone, sizeof(m)) != 0 || !SameLevel(withBody, without);
        notFinite += !Finite(b);
        delta = std::max(delta, DeltaOff(b, body, cam, m));
        keptBad += !LanesAndPlaceKept(b, body);
        std::memcpy(mainOut, m, sizeof(m));
        std::memcpy(bodyOut, b, sizeof(b));
    };
    for (int n = 0; n < kLoggedCount * 100 + 20000; ++n) {
        float cam[16];
        int hand = 1;
        if (n < kLoggedCount * 100) {
            LoggedCamera(kLogged[n % kLoggedCount], cam);
        } else {
            hand = Coin(0.25) ? -1 : 1;
            MakeCamera(cam, RandomScripted(89), hand, RandomPlace(), true);
        }
        const bool mirrored = hand < 0 && Coin(0.8);  // mirrorfix on, mostly
        const Q head = RandomHead();
        float m[16], b[16], body[16];
        // The main camera's rows, its w lanes or 0 (the player's kind), at its place or up to half a metre off.
        std::memcpy(body, cam, sizeof(body));
        if (Coin(0.3))
            body[3] = body[7] = body[11] = 0.0f;
        if (Coin())
            for (int i = 12; i < 15; ++i)
                body[i] += static_cast<float>(Uniform(-0.5, 0.5));
        picture(cam, body, head, hand, mirrored, m, b);
        asMain = std::max(asMain, RowsApart(b, m));
        // Turned from the main camera about a line in the world (a few degrees, or any): the turned one turned so.
        const V3 line(Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1));
        const double degrees = Coin() ? Uniform(0, 5) : Uniform(-180, 180);
        TurnedAbout(cam, line, degrees, body);
        picture(cam, body, head, hand, mirrored, m, b);
        float want[16];
        TurnedAbout(m, line, degrees, want);
        worldTurn = std::max(worldTurn, RowsApart(b, want));
        // Turned from it in its own frame (a few degrees, or any), which is about its own line in the world as the game
        // made it (the other way round for rows of the other hand): after times before's inverse times it is the turned
        // one turned so about that line. (3cbdb33 gave the turned one turned so in its own frame: the body slid off the
        // scene, 15b.)
        const Q own = Coin() ? AxisAngle({Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)}, Rad(Uniform(0, 5)))
                             : RandomRotation();
        TurnedInOwnFrame(cam, own, body);
        picture(cam, body, head, hand, mirrored, m, b);
        const V3 ownLine = Row(cam, 0) * own.x + Row(cam, 1) * own.y + Row(cam, 2) * own.z;
        const double ownDegrees = Deg(2 * std::atan2(Len({own.x, own.y, own.z}), own.w));
        TurnedAbout(m, ownLine, hand * ownDegrees, want);
        ownTurn = std::max(ownTurn, RowsApart(b, want));
    }
    // Rows a little off unit length and square, still taken as scripted: as the main camera, out as the turned one.
    for (int n = 0; n < 5000; ++n) {
        float cam[16];
        MakeCamera(cam, RandomScripted(89), 1, RandomPlace(), true);
        const float c = std::cos(0.0349066f), s = std::sin(0.0349066f);  // the up row 2 degrees towards the right
        for (int i = 0; i < 3; ++i)
            cam[4 + i] = c * cam[4 + i] + s * cam[i];
        const float scale[3] = {1.02f, 0.985f, 1.015f};
        for (int r = 0; r < 3; ++r)
            for (int i = 0; i < 3; ++i)
                cam[4 * r + i] *= scale[r];
        if (viewturn::Classify(cam, true) != viewturn::kScripted) {
            ++unclassified;
            continue;
        }
        float m[16], b[16];
        picture(cam, cam, RandomHead(), 1, false, m, b);
        offSquare = std::max(offSquare, RowsApart(b, m));
    }
    Report("15a", "a scripted picture's first-person camera turned by its main camera's turn, about its own place",
           asMain < 1e-6 && worldTurn < 1e-5 && ownTurn < 1e-5 && offSquare < 1e-6 && delta < 1e-6 && keptBad == 0 &&
               mainBad == 0 && notFinite == 0 && unclassified == 0,
           "%d pictures: as the main camera (its w lanes or 0, up to half a metre off) at most %.2g from the turned main "
           "camera; turned from it about a line in the world, %.2g from the turned one turned so; in its own frame, "
           "%.2g from the turned one turned so about its line in the world; its rows a little off unit length and "
           "square, %.2g; %.2g from the turned main camera times the main's inverse as the game made it times its "
           "rows; %d w lanes or places changed, %d main cameras turned otherwise than without it, %d not finite (%d "
           "off-square cameras not taken as scripted)",
           runs, asMain, worldTurn, ownTurn, offSquare, delta, keptBad, mainBad, notFinite, unclassified);

    // b. The knees and the gun as the game put them (below and ahead of the first-person camera), seen in the picture
    // as the head turns, each against the scene it was drawn over (the same way out of the main camera as the game
    // made it): the first-person camera as the main one, or turned from it in its own frame (2 and 10 degrees of
    // pitch, 15 of yaw, 8 of roll, about a random line up to 1, 5 and 20), at the main camera's place in the arms'
    // space (the world less whole metres), mirrored or not. From the turned first-person camera where the turned main
    // camera sees that scene; at 3cbdb33 (the main camera's turn put on it from the world's side) off it, about as far
    // as the two cameras' rows were apart or further, though not as the main one (which 15b then had alone); from the
    // first-person camera as the game made it, where they stayed whatever the head did.
    const V3 points[3] = {{-0.15, -0.55, -0.35}, {0.15, -0.55, -0.35}, {0.12, -0.18, -0.40}};  // right, up, back
    // A way in a camera's picture: along that camera's right, up and back.
    auto seen = [](const float* c, const V3& d) {
        return Unit({Dot(d, Row(c, 0)), Dot(d, Row(c, 1)), Dot(d, Row(c, 2))});
    };
    auto randomLine = [] { return V3(Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)); };
    constexpr int kFromMain = 8;
    const char* const fromMainNames[kFromMain] = {"as the main one", "2 of pitch", "10 of pitch", "15 of yaw",
                                                  "8 of roll",       "up to 1",    "up to 5",     "up to 20"};
    double sceneOff[kFromMain] = {}, oldOff[kFromMain] = {}, gluedOff = 0;
    int pictures = 0, placeBad = 0;
    for (int n = 0; n < kLoggedCount + 200; ++n) {
        float cam[16];
        int hand = 1;
        if (n < kLoggedCount) {
            LoggedCamera(kLogged[n], cam);
        } else {
            hand = Coin(0.25) ? -1 : 1;
            MakeCamera(cam, RandomScripted(60), hand, RandomPlace(), true);
        }
        // The first-person camera's turn from the main one, in its own frame.
        const Q fromMain[kFromMain] = {Q{},
                                       Pitch(Rad(2)),
                                       Pitch(Rad(10)),
                                       Yaw(Rad(15)),
                                       Roll(Rad(8)),
                                       AxisAngle(randomLine(), Rad(Uniform(0, 1))),
                                       AxisAngle(randomLine(), Rad(Uniform(0, 5))),
                                       AxisAngle(randomLine(), Rad(Uniform(0, 20)))};
        for (int k = 0; k < kFromMain; ++k) {
            // The first-person camera as the game made it: the main camera's rows turned so, its w lanes the main's, its
            // place the main camera's in the arms' space.
            float body[16];
            TurnedInOwnFrame(cam, fromMain[k], body);
            for (int i = 12; i < 15; ++i)
                body[i] = cam[i] - std::round(cam[i]);
            for (int yaw = -70; yaw <= 70; yaw += 10) {
                for (int pitch = -50; pitch <= 50; pitch += 25) {
                    float m[16], b[16], before[16];
                    std::memcpy(m, cam, sizeof(m));
                    std::memcpy(b, body, sizeof(b));
                    std::memcpy(before, body, sizeof(before));
                    viewturn::Level level;
                    viewturn::Turn(m, TurnFor(Mul(Yaw(Rad(yaw)), Pitch(Rad(pitch)))), true, hand, hand < 0, level, b);
                    old::TurnWith(before, cam, m);
                    placeBad += !LanesAndPlaceKept(b, body);
                    ++pictures;
                    for (const V3& p : points) {
                        // Where the game put it, from the first-person camera, and the scene it was drawn over: that
                        // same way out of the main camera, as the game made them.
                        const V3 at = Row(body, 0) * p.x + Row(body, 1) * p.y + Row(body, 2) * p.z;
                        const V3 way = seen(body, at);
                        const V3 scene = Row(cam, 0) * way.x + Row(cam, 1) * way.y + Row(cam, 2) * way.z;
                        const V3 there = seen(m, scene);  // where the turned main camera shows that scene
                        sceneOff[k] = std::max(sceneOff[k], AngleDeg(seen(b, at), there));
                        oldOff[k] = std::max(oldOff[k], AngleDeg(seen(before, at), there));
                        gluedOff = std::max(gluedOff, AngleDeg(way, there));
                    }
                }
            }
        }
    }
    double sceneMost = 0;
    char now[384] = "", then[384] = "";
    for (int k = 0; k < kFromMain; ++k) {
        sceneMost = std::max(sceneMost, sceneOff[k]);
        std::snprintf(now + std::strlen(now), sizeof(now) - std::strlen(now), "%s%s %.2g", k ? ", " : "",
                      fromMainNames[k], sceneOff[k]);
        std::snprintf(then + std::strlen(then), sizeof(then) - std::strlen(then), "%s%s %.2f", k ? ", " : "",
                      fromMainNames[k], oldOff[k]);
    }
    Report("15b", "the knees and the gun stay on the scene they were drawn over as the head turns",
           sceneMost < 0.001 && oldOff[1] > 2 && gluedOff > 60 && placeBad == 0,
           "%d pictures (the head 70 left to 70 right, 50 down to 50 up; %d cameras, the first-person camera turned from "
           "the main one by degrees, or not): seen from the turned first-person camera at most this many degrees from "
           "where the turned main camera shows the scene they were drawn over: %s (at 3cbdb33: %s); as the game made "
           "it, up to %.1f off it (moved with the head, and not made level with the camera); %d w lanes or places "
           "changed",
           pictures, kLoggedCount + 200, now, then, gluedOff, placeBad);

    // c. A normal picture (the player's camera, look or not, mirrored or not): the first-person camera (the main
    // camera's copy, another player's camera or a scripted one) left bit for bit, the main camera turned bit for bit
    // as at 6ee0128.
    int normalBodyBad = 0, normalMainBad = 0, normalTouched = 0;
    constexpr int kNormal = 20000;
    for (int n = 0; n < kNormal; ++n) {
        float m[16], body[16];
        RandomPlayerCamera(m);
        const camera::ViewTurn turn = RandomTurn();
        const bool mirrored = Coin();
        if (n % 3 == 0)
            std::memcpy(body, m, sizeof(body));
        else if (n % 3 == 1)
            RandomPlayerCamera(body);
        else
            MakeCamera(body, RandomScripted(80), 1, RandomPlace(), true);
        float a[16], was[16], before[16];
        std::memcpy(a, m, sizeof(a));
        std::memcpy(was, body, sizeof(was));
        std::memcpy(before, m, sizeof(before));
        viewturn::Level level;
        const float takenNow = viewturn::Turn(a, turn, false, Hand(m), mirrored, level, body);
        const float takenBefore = old::Turn(before, turn, mirrored);
        normalBodyBad += std::memcmp(body, was, sizeof(body)) != 0;
        normalMainBad +=
            std::memcmp(a, before, sizeof(a)) != 0 || std::memcmp(&takenNow, &takenBefore, sizeof(takenNow)) != 0;
        normalTouched += level.have;
    }
    Report("15c", "a normal picture's first-person camera left bit for bit", normalBodyBad == 0 && normalMainBad == 0 &&
                                                                              normalTouched == 0,
           "%d of %d first-person cameras changed; %d main cameras or yaws taken out differ from the turn at 6ee0128; "
           "the scripted yaw touched %d times",
           normalBodyBad, kNormal, normalMainBad, normalTouched);

    // d. A scripted picture with the look still written (its first pictures can come so), and a first-person camera
    // that isn't a camera: left bit for bit, the main camera turned bit for bit as without it. And TurnWith from rows
    // with no inverse (all 0, two alike): left bit for bit.
    int lookBad = 0, junkBad = 0, junkMainBad = 0, junkTaken = 0, noInverseBad = 0;
    constexpr int kLook = 5000, kJunk = 20000;
    for (int n = 0; n < kLook; ++n) {
        float cam[16];
        MakeCamera(cam, RandomScripted(80), 1, RandomPlace(), true);
        camera::ViewTurn turn = RandomTurn();
        turn.haveLook = true;
        float m[16], alone[16], body[16];
        std::memcpy(m, cam, sizeof(m));
        std::memcpy(alone, cam, sizeof(alone));
        std::memcpy(body, cam, sizeof(body));
        viewturn::Level a, b;
        viewturn::Turn(m, turn, true, 1, false, a, body);
        viewturn::Turn(alone, turn, true, 1, false, b);
        lookBad += std::memcmp(body, cam, sizeof(body)) != 0 || std::memcmp(m, alone, sizeof(m)) != 0;
    }
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    for (int n = 0; n < kJunk; ++n) {
        float cam[16], body[16];
        MakeCamera(cam, RandomScripted(80), 1, RandomPlace(), true);
        std::memcpy(body, cam, sizeof(body));
        switch (n % 6) {
        case 0:
            for (float& f : body)
                f = static_cast<float>(Uniform(-2, 2));
            break;
        case 1:
            body[4 + n % 3] = nan;
            break;
        case 2:
            body[8 + n % 3] = n % 2 ? inf : -inf;
            break;
        case 3:
            std::memset(body, 0, sizeof(body));
            break;
        case 4:
            for (int i = 0; i < 3; ++i)
                body[i] *= 1.1f;
            break;
        default:
            body[7] = 10.0f;
            break;
        }
        if (viewturn::Classify(body, true) != viewturn::kNone) {
            ++junkTaken;
            continue;
        }
        float m[16], alone[16], was[16];
        std::memcpy(m, cam, sizeof(m));
        std::memcpy(alone, cam, sizeof(alone));
        std::memcpy(was, body, sizeof(was));
        const camera::ViewTurn turn = TurnFor(RandomHead());
        viewturn::Level a, b;
        viewturn::Turn(m, turn, true, 1, false, a, body);
        viewturn::Turn(alone, turn, true, 1, false, b);
        junkBad += std::memcmp(body, was, sizeof(body)) != 0;
        junkMainBad += std::memcmp(m, alone, sizeof(m)) != 0;
    }
    for (int n = 0; n < 1000; ++n) {
        float cam[16], body[16], was[16], before[16];
        MakeCamera(cam, RandomScripted(80), 1, RandomPlace(), true);
        MakeCamera(body, RandomScripted(80), 1, RandomPlace(), true);
        std::memcpy(was, body, sizeof(was));
        std::memcpy(before, cam, sizeof(before));
        if (n % 2 == 0)
            std::memset(before, 0, 12 * sizeof(float));
        else
            std::memcpy(before + 4, before, 4 * sizeof(float));  // the up row the right row
        noInverseBad += viewturn::TurnWith(body, before, cam) || std::memcmp(body, was, sizeof(body)) != 0;
    }
    Report("15d", "the look still written, or not a camera: the first-person camera left bit for bit",
           lookBad == 0 && junkBad == 0 && junkMainBad == 0 && junkTaken == 0 && noInverseBad == 0,
           "%d of %d with the look written changed or changed the main camera; %d of %d not a camera changed (%d taken "
           "for one), %d changed the main camera; %d of 1000 turned from rows with no inverse",
           lookBad, kLook, junkBad, kJunk - junkTaken, junkTaken, junkMainBad, noInverseBad);
}

// ---- 16. The body's own views turned where they're uploaded (skin.cpp's body lock) ----
constexpr int kUploadFloats = viewturn::kPerViewBytes / 4, kWorldFloats = viewturn::kWorldViewBytes / 4;

// A perspective projection as the game uploads one (rows, points taken as columns): its x and y scales from the FOV
// and aspect, an x offset, its z row from the near and far planes, its w row 0 0 -1 0 (rh: the view's z row the
// camera's back row) or 0 0 1 0 (the forward).
void MakeProjection(double* p, double fovDeg, double aspect, double ox, double nearPlane, double farPlane, bool rh) {
    std::fill(p, p + 16, 0.0);
    const double sy = 1.0 / std::tan(Rad(fovDeg) / 2), s = rh ? -1.0 : 1.0;
    p[0] = sy / aspect;
    p[2] = ox;
    p[5] = sy;
    p[10] = s * farPlane / (farPlane - nearPlane);
    p[11] = -farPlane * nearPlane / (farPlane - nearPlane);
    p[14] = s;
}
// a times b (row-major 4x4 floats, points as columns), in double.
void ProductOf(const float* a, const float* b, double* out) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            double sum = 0;
            for (int k = 0; k < 4; ++k)
                sum += static_cast<double>(a[4 * r + k]) * b[4 * k + c];
            out[4 * r + c] = sum;
        }
}
// A camera's view (rows, points as columns): its rows (the back row the other way round for !rh), the translation
// each row's less its dot with the eye, the last row 0 0 0 1, in floats.
void ViewOf(const float* cam, const V3& eye, bool rh, float* view) {
    for (int r = 0; r < 3; ++r) {
        const V3 row = Row(cam, r) * (r == 2 && !rh ? -1.0 : 1.0);
        for (int i = 0; i < 3; ++i)
            view[4 * r + i] = static_cast<float>(row[i]);
        view[4 * r + 3] = static_cast<float>(-Dot(row, eye));
    }
    view[12] = view[13] = view[14] = 0.0f;
    view[15] = 1.0f;
}
// A body's per-view upload as the game makes one: filler, the view of the camera cam at eye, the projection p, the
// view-projection and the one for points less the eye (in floats as viewturn::Mul4 has it, or worked out in double
// and rounded: the game's own order may differ), the eye.
void MakeUpload(float* f, const float* cam, const V3& eye, const double* p, bool rh, bool roundedProducts) {
    for (int i = 0; i < kUploadFloats; ++i)
        f[i] = static_cast<float>(Uniform(-3, 3));
    float* view = f + viewturn::kViewAt;
    float* proj = f + viewturn::kProjectionAt;
    ViewOf(cam, eye, rh, view);
    for (int i = 0; i < 16; ++i)
        proj[i] = static_cast<float>(p[i]);
    double vp[16];
    ProductOf(proj, view, vp);
    if (roundedProducts) {
        for (int i = 0; i < 16; ++i)
            f[viewturn::kViewProjectionAt + i] = static_cast<float>(vp[i]);
    } else {
        viewturn::Mul4(proj, view, f + viewturn::kViewProjectionAt);
    }
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 3; ++c)
            f[viewturn::kFromEyeAt + 4 * r + c] = static_cast<float>(vp[4 * r + c] - static_cast<double>(proj[4 * r + 3]) * view[12 + c]);
        f[viewturn::kFromEyeAt + 4 * r + 3] = proj[4 * r + 3];
    }
    for (int i = 0; i < 3; ++i)
        f[viewturn::kEyeAt + i] = static_cast<float>(eye[i]);
}
// The view-projection alone in the world (80 bytes: it, then filler), in floats.
void MakeWorldUpload(float* w, const float* cam, const V3& eye, const double* p, bool rh) {
    float view[16], proj[16];
    ViewOf(cam, eye, rh, view);
    for (int i = 0; i < 16; ++i)
        proj[i] = static_cast<float>(p[i]);
    viewturn::Mul4(proj, view, w);
    for (int i = 16; i < kWorldFloats; ++i)
        w[i] = static_cast<float>(Uniform(-3, 3));
}
// The most any of n floats is from what's wanted, against its size (or 1).
double RelOff(const float* got, const double* want, int n) {
    double off = 0;
    for (int i = 0; i < n; ++i)
        off = std::max(off, std::fabs(got[i] - want[i]) / (1.0 + std::fabs(want[i])));
    return off;
}
// The eye of a view-projection (where its x, y and w rows are 0), in double.
V3 EyeOf(const float* vp) {
    const int rows[3] = {0, 1, 3};
    double a[3][3], b[3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            a[i][j] = vp[4 * rows[i] + j];
        b[i] = -static_cast<double>(vp[4 * rows[i] + 3]);
    }
    auto det3 = [](const double m[3][3]) {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    };
    const double det = det3(a);
    double e[3];
    for (int k = 0; k < 3; ++k) {
        double c[3][3];
        std::memcpy(c, a, sizeof(c));
        for (int i = 0; i < 3; ++i)
            c[i][k] = b[i];
        e[k] = det3(c) / det;
    }
    return {e[0], e[1], e[2]};
}
// A view's eye from the view itself: its turn's transpose times its translation, turned round (each row times its
// translation, summed and turned round).
V3 EyeOfView(const float* view) {
    V3 e;
    for (int r = 0; r < 3; ++r)
        e = e - V3(view[4 * r], view[4 * r + 1], view[4 * r + 2]) * view[4 * r + 3];
    return e;
}
// Nine numbers' rows (right, up, back) as directions.
V3 Row9(const float* rows, int r) {
    return {rows[3 * r], rows[3 * r + 1], rows[3 * r + 2]};
}
// A way in a camera's picture (rows, nine numbers): along its right, up and back.
V3 Seen9(const float* rows, const V3& d) {
    return Unit({Dot(d, Row9(rows, 0)), Dot(d, Row9(rows, 1)), Dot(d, Row9(rows, 2))});
}
// A view build's turn as camera.cpp notes it for a scripted main camera cam turned to head (viewturn::Turn as
// TurnToHead runs it), with its first-person camera fp (or none), the main camera as turned in turned.
camera::BodyTurn MakeBuild(const float* cam, const Q& head, int hand, bool mirrored, const float* fp, float* turned) {
    camera::BodyTurn b;
    float m[16], body[16] = {};
    std::memcpy(m, cam, sizeof(m));
    if (fp)
        std::memcpy(body, fp, sizeof(body));
    viewturn::Level level;
    viewturn::Turn(m, TurnFor(head), true, hand, mirrored, level, fp ? body : nullptr);
    viewturn::OwnFrameTurn(cam, m, b.turn);
    for (int r = 0; r < 3; ++r)
        for (int i = 0; i < 3; ++i) {
            b.before[3 * r + i] = cam[4 * r + i];
            b.after[3 * r + i] = m[4 * r + i];
            if (fp) {
                b.firstPersonMade[3 * r + i] = fp[4 * r + i];
                b.firstPerson[3 * r + i] = body[4 * r + i];
            }
        }
    for (int i = 0; i < 3; ++i)
        b.place[i] = cam[12 + i];
    if (turned)
        std::memcpy(turned, m, sizeof(m));
    return b;
}
// A camera with rows (nine numbers) at a place, its w lanes 0.
void CameraOf(const float* rows, const V3& at, float* m) {
    for (int r = 0; r < 3; ++r) {
        for (int i = 0; i < 3; ++i)
            m[4 * r + i] = rows[3 * r + i];
        m[4 * r + 3] = 0.0f;
    }
    for (int i = 0; i < 3; ++i)
        m[12 + i] = static_cast<float>(at[i]);
    m[15] = 1.0f;
}
// The FOV fix's rule (skin.cpp RescaleProjections) on a per-view upload: the x and y rows of the projection, the
// view-projection and the one for points less the eye times k.
void Rescale(float* f, float k) {
    for (int at : {viewturn::kProjectionAt, viewturn::kViewProjectionAt, viewturn::kFromEyeAt})
        for (int c = 0; c < 8; ++c)
            f[at + c] *= k;
}
// An eye in the arms' space: the world's axes, the origin whole metres near the feet.
V3 RandomArmsEye() {
    return {Uniform(-1, 1), Uniform(0.5, 2.0), Uniform(-1, 1)};
}
// A random projection as the body's could be: 40 to 110 degrees, square or nearly, an x offset now and then.
void RandomProjection(double* p, bool rh) {
    MakeProjection(p, Uniform(40, 110), Uniform(0.9, 1.1), Coin(0.3) ? Uniform(-0.05, 0.05) : 0.0, Uniform(0.01, 0.1),
                   Uniform(100, 5000), rh);
}

void Check16() {
    // The knees and the gun as the game put them (right, up, back from the first-person camera).
    const V3 points[3] = {{-0.15, -0.55, -0.35}, {0.15, -0.55, -0.35}, {0.12, -0.18, -0.40}};
    // a. The math: a body upload turned, its world view-projection alike.
    double rowsRead = 0, eyeRead = 0, withOff = 0, vpOff = 0, fromEyeOff = 0, eyeViewOff = 0, eyeVpOff = 0,
           rigidOff = 0, sceneOff = 0, gluedOff = 0, worldRows = 0, worldEye = 0, worldTurnedRows = 0, worldEyeKept = 0,
           worldAtEye = 0, worldSeen = 0, worldVsArms = 0;
    int unread = 0, worldUnread = 0, keptBad = 0, worldKeptBad = 0, signBad = 0, runs = 0, worldSeenPoints = 0;
    for (int n = 0; n < kLoggedCount * 50 + 20000; ++n) {
        float cam[16];
        int hand = 1;
        if (n < kLoggedCount * 50) {
            LoggedCamera(kLogged[n % kLoggedCount], cam);
        } else {
            hand = Coin(0.25) ? -1 : 1;
            MakeCamera(cam, RandomScripted(80), hand, RandomPlace(), true);
        }
        const bool mirrored = hand < 0 && Coin(0.8);
        // The body's camera: the scripted one's rows, or a few degrees from them in its own frame (the game's own
        // first-person camera), its w lanes 0.
        float fp[16];
        const Q apart = Coin(0.3) ? Q{} : AxisAngle({Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)}, Rad(Uniform(0, 10)));
        TurnedInOwnFrame(cam, apart, fp);
        fp[3] = fp[7] = fp[11] = 0.0f;
        float turnedCam[16];
        const camera::BodyTurn build = MakeBuild(cam, RandomHead(), hand, mirrored, nullptr, turnedCam);
        const V3 eye = RandomArmsEye();
        const bool rh = Coin(0.8);
        double p[16];
        RandomProjection(p, rh);
        float f[kUploadFloats], was[kUploadFloats];
        MakeUpload(f, fp, eye, p, rh, Coin());
        std::memcpy(was, f, sizeof(f));
        ++runs;
        viewturn::BodyView v;
        if (viewturn::ReadPerView(f, v) != viewturn::kPerViewOk) {
            ++unread;
            continue;
        }
        float fpRows[9];
        for (int r = 0; r < 3; ++r)
            for (int i = 0; i < 3; ++i)
                fpRows[3 * r + i] = fp[4 * r + i];
        for (int i = 0; i < 9; ++i)
            rowsRead = std::max(rowsRead, Abs(v.rows[i] - fpRows[i]));
        eyeRead = std::max(eyeRead, Len(V3(v.eye[0], v.eye[1], v.eye[2]) - eye));
        signBad += v.zSign != (rh ? 1.0f : -1.0f);
        viewturn::TurnPerView(f, v, build.turn);
        // The turned view's camera rows as TurnWith turns that camera (the view description's +0x930 is turned so).
        float withTurn[16];
        std::memcpy(withTurn, fp, sizeof(withTurn));
        viewturn::TurnWith(withTurn, cam, turnedCam);
        viewturn::BodyView t;
        const bool reread = viewturn::ReadPerView(f, t) == viewturn::kPerViewOk;
        unread += !reread;
        for (int r = 0; r < 3; ++r)
            for (int i = 0; i < 3; ++i)
                withOff = std::max(withOff, Abs(t.rows[3 * r + i] - withTurn[4 * r + i]));
        // Still rigid.
        const float* view = f + viewturn::kViewAt;
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b)
                rigidOff = std::max(rigidOff, std::fabs(Dot(Row(view, a), Row(view, b)) - (a == b ? 1.0 : 0.0)));
        // The view-projection the projection times the turned view; the one for points less the eye the projection
        // times its turn alone, its last column the projection's.
        double pv[16], fe[16];
        ProductOf(f + viewturn::kProjectionAt, view, pv);
        vpOff = std::max(vpOff, RelOff(f + viewturn::kViewProjectionAt, pv, 16));
        const float* proj = f + viewturn::kProjectionAt;
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 3; ++c)
                fe[4 * r + c] = static_cast<double>(proj[4 * r]) * view[c] + static_cast<double>(proj[4 * r + 1]) * view[4 + c] +
                                static_cast<double>(proj[4 * r + 2]) * view[8 + c];
            fe[4 * r + 3] = proj[4 * r + 3];
        }
        fromEyeOff = std::max(fromEyeOff, RelOff(f + viewturn::kFromEyeAt, fe, 16));
        // The eye kept: from the turned view, and where the turned view-projection's x, y and w rows are 0.
        eyeViewOff = std::max(eyeViewOff, Len(EyeOfView(view) - eye));
        eyeVpOff = std::max(eyeVpOff, Len(EyeOf(f + viewturn::kViewProjectionAt) - eye));
        // The rest as it was, bit for bit: +0 to +48, the view's last row, the projection, +240's last column, +304 on.
        bool kept = std::memcmp(f, was, 48) == 0 && std::memcmp(view + 12, was + viewturn::kViewAt + 12, 16) == 0 &&
                    std::memcmp(proj, was + viewturn::kProjectionAt, 64) == 0 &&
                    std::memcmp(f + 76, was + 76, 48) == 0;
        for (int r = 0; r < 4; ++r)
            kept = kept && std::memcmp(&f[viewturn::kFromEyeAt + 4 * r + 3], &was[viewturn::kFromEyeAt + 4 * r + 3],
                                       sizeof(float)) == 0;
        keptBad += !kept;
        // On the scene: where the turned view shows the knees and the gun, where the turned main camera shows the
        // scene they were drawn over (that same way out of the main camera as the game made it).
        for (const V3& q : points) {
            const V3 at = Row(fp, 0) * q.x + Row(fp, 1) * q.y + Row(fp, 2) * q.z;
            const V3 way = Seen9(fpRows, at);
            const V3 scene = Row(cam, 0) * way.x + Row(cam, 1) * way.y + Row(cam, 2) * way.z;
            const V3 there = {Dot(scene, Row(turnedCam, 0)), Dot(scene, Row(turnedCam, 1)), Dot(scene, Row(turnedCam, 2))};
            sceneOff = std::max(sceneOff, AngleDeg(Seen9(t.rows, at), Unit(there)));
            gluedOff = std::max(gluedOff, AngleDeg(way, Unit(there)));
        }
        // Its view-projection alone in the world (80 bytes): every other one within 100 m of the world's origin, where
        // a near point's place in the picture is checked (further out a float view-projection's own rounding moves it
        // as much, the game's as well: the reason the body is drawn in the arms' space), the rest up to 2 km out.
        const bool nearOrigin = n % 2 == 0;
        const double out = nearOrigin ? 100 : 2000;
        const V3 eyeWorld = eye + V3(std::round(Uniform(-out, out)), std::round(Uniform(-20, 80)), std::round(Uniform(-out, out)));
        float w[kWorldFloats], wasWorld[kWorldFloats];
        MakeWorldUpload(w, fp, eyeWorld, p, rh);
        std::memcpy(wasWorld, w, sizeof(w));
        viewturn::BodyView wv;
        if (!viewturn::ReadViewProjection(w, wv)) {
            ++worldUnread;
            continue;
        }
        for (int i = 0; i < 9; ++i)
            worldRows = std::max(worldRows, Abs(wv.rows[i] - fpRows[i]));
        worldEye = std::max(worldEye, Len(V3(wv.eye[0], wv.eye[1], wv.eye[2]) - eyeWorld));
        viewturn::TurnViewProjection(w, wv, build.turn);
        worldKeptBad += std::memcmp(w + 16, wasWorld + 16, (kWorldFloats - 16) * sizeof(float)) != 0;
        viewturn::BodyView tw;
        if (!viewturn::ReadViewProjection(w, tw)) {
            ++worldUnread;
            continue;
        }
        for (int i = 0; i < 9; ++i)
            worldTurnedRows = std::max(worldTurnedRows, Abs(tw.rows[i] - t.rows[i]));
        worldEyeKept = std::max(worldEyeKept, Len(EyeOf(w) - eyeWorld));
        for (int r = 0; r < 4; ++r) {
            // What each row gives at the eye it was turned about (the uploaded view-projection's own, as read: in floats
            // a few hundred metres out it's a tenth of a millimetre from the one it was made from), kept, against the
            // sizes summed there, as uploaded or as turned, whichever's larger.
            double now = w[4 * r + 3], then = wasWorld[4 * r + 3];
            double sizeNow = std::fabs(w[4 * r + 3]), sizeThen = std::fabs(wasWorld[4 * r + 3]);
            for (int c = 0; c < 3; ++c) {
                now += static_cast<double>(w[4 * r + c]) * wv.eye[c];
                then += static_cast<double>(wasWorld[4 * r + c]) * wv.eye[c];
                sizeNow += std::fabs(w[4 * r + c] * wv.eye[c]);
                sizeThen += std::fabs(wasWorld[4 * r + c] * wv.eye[c]);
            }
            worldAtEye = std::max(worldAtEye, std::fabs(now - then) / (1.0 + std::max(sizeNow, sizeThen)));
            // Its turn alike in both spaces (the arms' space has the world's axes).
            double arms[3], world[3];
            for (int c = 0; c < 3; ++c)
                arms[c] = f[viewturn::kViewProjectionAt + 4 * r + c], world[c] = w[4 * r + c];
            for (int c = 0; c < 3; ++c)
                worldVsArms = std::max(worldVsArms, std::fabs(arms[c] - world[c]) / (1.0 + std::fabs(arms[c])));
        }
        // Points near the eye where the turned view shows them in the picture (x and y over w, where they're in it):
        // through the turned view-projection, and through the projection and the turned view in double. The knees and
        // the gun as the game put them, and points 30 to 80 cm ahead of the turned view.
        const V3 ahead[4] = {{0.08, -0.1, -0.35}, {-0.1, 0.06, -0.5}, {0.0, -0.12, -0.8}, {0.12, 0.1, -0.6}};
        for (int k = 0; k < 7 && nearOrigin; ++k) {
            const V3 d = k < 3 ? Row(fp, 0) * points[k].x + Row(fp, 1) * points[k].y + Row(fp, 2) * points[k].z
                               : Row9(t.rows, 0) * ahead[k - 3].x + Row9(t.rows, 1) * ahead[k - 3].y +
                                     Row9(t.rows, 2) * ahead[k - 3].z;  // from the eye
            if (Dot(Row9(t.rows, 2), d) > -0.2)  // not 20 cm ahead of the turned view
                continue;
            const V3 at = eyeWorld + d;
            const double seen[4] = {Dot(Row9(t.rows, 0), d), Dot(Row9(t.rows, 1), d),
                                    (rh ? 1.0 : -1.0) * Dot(Row9(t.rows, 2), d), 1.0};
            double got[4] = {}, exact[4] = {};
            for (int r = 0; r < 4; ++r) {
                got[r] = w[4 * r + 3];
                for (int c = 0; c < 3; ++c)
                    got[r] += static_cast<double>(w[4 * r + c]) * at[c];
                for (int c = 0; c < 4; ++c)
                    exact[r] += p[4 * r + c] * seen[c];
            }
            if (std::fabs(exact[0] / exact[3]) > 1.2 || std::fabs(exact[1] / exact[3]) > 1.2)
                continue;  // out of the picture
            ++worldSeenPoints;
            worldSeen = std::max(worldSeen, std::max(std::fabs(got[0] / got[3] - exact[0] / exact[3]),
                                                     std::fabs(got[1] / got[3] - exact[1] / exact[3])));
        }
    }
    Report("16a", "a body view turned where it's uploaded: as TurnWith turns its camera, the view-projections remade "
                  "from the projection, the eye kept, on the scene; its world view-projection alike",
           unread == 0 && worldUnread == 0 && signBad == 0 && rowsRead < 1e-7 && eyeRead < 1e-5 && withOff < 1e-6 &&
               rigidOff < 1e-5 && vpOff < 1e-5 && fromEyeOff < 1e-5 && eyeViewOff < 1e-5 && eyeVpOff < 1e-4 &&
               keptBad == 0 && sceneOff < 0.001 && gluedOff > 60 && worldRows < 1e-5 && worldEye < 1e-3 &&
               worldTurnedRows < 1e-5 && worldEyeKept < 1e-3 && worldAtEye < 1e-6 && worldSeen < 2e-4 &&
               worldSeenPoints > 20000 && worldVsArms < 1e-5 && worldKeptBad == 0,
           "%d uploads (%d not read, %d of their world views; %d with the projection's w row taken the wrong way): read "
           "as the body's camera %.2g, its eye %.2g m; turned, %.2g from TurnWith's turn of that camera, rigid within "
           "%.2g; the view-projection %.2g from the projection times the turned view, the one for points less the eye "
           "%.2g from its turn alone; the eye %.2g m from the turned view, %.2g m from the turned view-projection; %d "
           "with anything else changed; the knees and the gun %.2g degrees off the scene (as the game made it, up to "
           "%.1f). In the world: read %.2g, its eye %.2g m; turned, %.2g from the arms' view's turned rows and %.2g in "
           "its turn, the eye %.2g m away, at the eye %.2g off, %d points near it in the picture %.2g off where the "
           "turned view shows them; %d with the rest changed",
           runs, unread, worldUnread, signBad, rowsRead, eyeRead, withOff, rigidOff, vpOff, fromEyeOff, eyeViewOff,
           eyeVpOff, keptBad, sceneOff, gluedOff, worldRows, worldEye, worldTurnedRows, worldVsArms, worldEyeKept,
           worldAtEye, worldSeenPoints, worldSeen, worldKeptBad);

    // b. Which uploads are turned, in a frame of one to four view builds (the newest first), each scripted camera with
    // its first-person camera (+0x930) a tenth of a degree from it, as the drive's was.
    int verdictBad = 0, changedBad = 0, turnBad = 0, useBad = 0, faultBad = 0, cases = 0;
    auto expect = [&](viewturn::BodyVerdict got, viewturn::BodyVerdict want, const float* now, const float* was,
                      int floats) {
        ++cases;
        verdictBad += got != want;
        if (want != viewturn::kBodyTurned)
            changedBad += std::memcmp(now, was, floats * sizeof(float)) != 0;
    };
    for (int n = 0; n < 4000; ++n) {
        const int count = 1 + n % 4;
        camera::BodyTurn builds[4];
        for (int b = 0; b < count; ++b) {
            float cam[16], fp[16];
            const int hand = Coin(0.25) ? -1 : 1;
            MakeCamera(cam, RandomScripted(70), hand, RandomPlace(), true);
            TurnedInOwnFrame(cam, AxisAngle({Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)}, Rad(0.1)), fp);
            builds[b] = MakeBuild(cam, Ypr(Rad(Uniform(-120, 120)), Rad(Uniform(-60, 60)), Rad(Uniform(-20, 20))), hand,
                                  hand < 0, fp, nullptr);
        }
        const bool rh = Coin(0.8);
        double p[16];
        RandomProjection(p, rh);
        float f[kUploadFloats], was[kUploadFloats], cam[16];
        viewturn::BodyView v;
        viewturn::PerViewFault fault;
        const int b = static_cast<int>(Uniform(0, count - 1e-9));
        const V3 place(builds[b].place[0], builds[b].place[1], builds[b].place[2]);
        // The main view's own (its turned camera's rows), in the world and an eye's shift along its right, or in the
        // arms' space: left as it is, and it names its build for the frame.
        {
            CameraOf(builds[b].after, place, cam);
            const V3 at = Coin() ? place + Row9(builds[b].after, 0) * 0.032 : RandomArmsEye();
            MakeUpload(f, cam, at, p, rh, Coin());
            std::memcpy(was, f, sizeof(f));
            int use = -1;
            expect(viewturn::LockBodyPerView(f, builds, count, use, v, fault), viewturn::kMainView, f, was, kUploadFloats);
            useBad += use != b;
        }
        // The turned first-person camera's: left as it is, naming nothing.
        {
            CameraOf(builds[b].firstPerson, place, cam);
            MakeUpload(f, cam, RandomArmsEye(), p, rh, Coin());
            std::memcpy(was, f, sizeof(f));
            int use = -1;
            expect(viewturn::LockBodyPerView(f, builds, count, use, v, fault), viewturn::kTurnedBefore, f, was,
                   kUploadFloats);
            useBad += use != -1;
        }
        // The body's own (a few degrees from the newest scripted camera), with no main view first: turned by the newest
        // build's turn; after the main view of build b: by build b's.
        for (int named = 0; named < 2; ++named) {
            float rows[16];
            CameraOf(builds[named ? b : 0].before, place, rows);
            TurnedInOwnFrame(rows, AxisAngle({Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)}, Rad(Uniform(0.5, 10))),
                             cam);
            MakeUpload(f, cam, RandomArmsEye(), p, rh, Coin());
            std::memcpy(was, f, sizeof(f));
            int use = named ? b : -1;
            float alike[kUploadFloats];
            std::memcpy(alike, f, sizeof(f));
            viewturn::BodyView read;
            viewturn::ReadPerView(alike, read);
            viewturn::TurnPerView(alike, read, builds[named ? b : 0].turn);
            expect(viewturn::LockBodyPerView(f, builds, count, use, v, fault), viewturn::kBodyTurned, f, was,
                   kUploadFloats);
            turnBad += std::memcmp(f, alike, sizeof(f)) != 0;
            useBad += use != (named ? b : 0);
        }
        // Looking away from the scripted camera (60 to 180 degrees): left as it is.
        {
            float rows[16];
            CameraOf(builds[0].before, place, rows);
            TurnedInOwnFrame(rows, AxisAngle({Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)}, Rad(Uniform(60, 180))), cam);
            const V3 back = Row(cam, 2);
            if (Dot(back, Row9(builds[0].before, 2)) < 0.6) {
                MakeUpload(f, cam, RandomArmsEye(), p, rh, Coin());
                std::memcpy(was, f, sizeof(f));
                int use = -1;
                expect(viewturn::LockBodyPerView(f, builds, count, use, v, fault), viewturn::kAway, f, was, kUploadFloats);
            }
        }
        // Another view in the world: left as it is.
        {
            MakeCamera(cam, RandomRotation(), 1, RandomPlace(), false);
            MakeUpload(f, cam, place + V3(Uniform(-50, 50), 6, Uniform(-50, 50)), p, rh, Coin());
            std::memcpy(was, f, sizeof(f));
            int use = -1;
            expect(viewturn::LockBodyPerView(f, builds, count, use, v, fault), viewturn::kElsewhere, f, was, kUploadFloats);
        }
        // Not per-view constants: left as they are, each for its reason.
        {
            float rows[16];
            CameraOf(builds[0].before, place, rows);
            const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
            for (int kind = 0; kind < 9; ++kind) {
                MakeUpload(f, rows, RandomArmsEye(), p, rh, Coin());
                viewturn::PerViewFault want = viewturn::kPerViewOk;
                switch (kind) {
                case 0:  // random floats
                    for (float& x : f)
                        x = static_cast<float>(Uniform(-2, 2));
                    want = viewturn::kNotRigid;
                    break;
                case 1:  // an orthographic projection (a shadow's): its w row 0 0 0 1
                    f[viewturn::kProjectionAt + 14] = 0.0f, f[viewturn::kProjectionAt + 15] = 1.0f;
                    want = viewturn::kNotPerspective;
                    break;
                case 2:  // the view-projection not the projection's product (one of its numbers 1% of its size off, or 0.01)
                    f[viewturn::kViewProjectionAt + n % 12] += 0.01f * (1.0f + std::fabs(f[viewturn::kViewProjectionAt + n % 12]));
                    want = viewturn::kNotProduct;
                    break;
                case 3:  // +240 otherwise
                    f[viewturn::kFromEyeAt + n % 16] += 0.05f;
                    want = viewturn::kNotFromEye;
                    break;
                case 4:  // the view's rows a little long
                    for (int i = 0; i < 3; ++i)
                        f[viewturn::kViewAt + i] *= 1.01f;
                    want = viewturn::kNotRigid;
                    break;
                case 5:  // NaN in the view's translation
                    f[viewturn::kViewAt + 3 + 4 * (n % 3)] = nan;
                    want = viewturn::kNotRigid;
                    break;
                case 6:  // infinite in the view-projection
                    f[viewturn::kViewProjectionAt + n % 16] = n % 2 ? inf : -inf;
                    want = viewturn::kNotProduct;
                    break;
                case 7:  // NaN in the projection
                    f[viewturn::kProjectionAt + (n % 2 ? 0 : 5)] = nan;
                    want = viewturn::kNotPerspective;
                    break;
                default:  // zeros
                    std::memset(f, 0, sizeof(f));
                    want = viewturn::kNotRigid;
                    break;
                }
                std::memcpy(was, f, sizeof(f));
                int use = -1;
                expect(viewturn::LockBodyPerView(f, builds, count, use, v, fault), viewturn::kNotView, f, was,
                       kUploadFloats);
                faultBad += fault != want;
                useBad += use != -1;
            }
        }
        // The view-projections alone in the world (80 bytes): the body's (its eye by the scripted camera's place) turned
        // by the frame's build's turn; the main view's, the turned first-person camera's, one away from the place, one
        // looking away, and random numbers left as they are.
        {
            float w[kWorldFloats], wasWorld[kWorldFloats], rows[16];
            CameraOf(builds[0].before, place, rows);
            TurnedInOwnFrame(rows, AxisAngle({Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)}, Rad(Uniform(0.5, 10))),
                             cam);
            const V3 by0(builds[0].place[0], builds[0].place[1], builds[0].place[2]);
            const V3 byCamera = by0 + V3(Uniform(-0.5, 0.5), Uniform(-0.5, 0.5), Uniform(-0.5, 0.5));
            MakeWorldUpload(w, cam, byCamera, p, rh);
            std::memcpy(wasWorld, w, sizeof(w));
            float alike[kWorldFloats];
            std::memcpy(alike, w, sizeof(w));
            viewturn::BodyView read;
            if (viewturn::ReadViewProjection(alike, read))
                viewturn::TurnViewProjection(alike, read, builds[0].turn);
            int use = -1;
            expect(viewturn::LockBodyWorldView(w, builds, count, use, v), viewturn::kBodyTurned, w, wasWorld, kWorldFloats);
            turnBad += std::memcmp(w, alike, sizeof(w)) != 0;
            useBad += use != 0;
            MakeWorldUpload(w, cam, by0 + V3(0, 3, 0), p, rh);  // 3 m from the scripted camera
            std::memcpy(wasWorld, w, sizeof(w));
            use = -1;
            expect(viewturn::LockBodyWorldView(w, builds, count, use, v), viewturn::kElsewhere, w, wasWorld, kWorldFloats);
            CameraOf(builds[b].after, place, cam);
            MakeWorldUpload(w, cam, place, p, rh);
            std::memcpy(wasWorld, w, sizeof(w));
            use = -1;
            expect(viewturn::LockBodyWorldView(w, builds, count, use, v), viewturn::kMainView, w, wasWorld, kWorldFloats);
            useBad += use != b;
            CameraOf(builds[b].firstPerson, place, cam);
            MakeWorldUpload(w, cam, place, p, rh);
            std::memcpy(wasWorld, w, sizeof(w));
            use = -1;
            expect(viewturn::LockBodyWorldView(w, builds, count, use, v), viewturn::kTurnedBefore, w, wasWorld,
                   kWorldFloats);
            TurnedInOwnFrame(rows, AxisAngle({Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)}, Rad(Uniform(60, 180))), cam);
            if (Dot(Row(cam, 2), Row9(builds[0].before, 2)) < 0.6) {
                MakeWorldUpload(w, cam, byCamera, p, rh);
                std::memcpy(wasWorld, w, sizeof(w));
                use = -1;
                expect(viewturn::LockBodyWorldView(w, builds, count, use, v), viewturn::kAway, w, wasWorld, kWorldFloats);
            }
            for (float& x : w)
                x = static_cast<float>(Uniform(-2, 2));
            std::memcpy(wasWorld, w, sizeof(w));
            use = -1;
            expect(viewturn::LockBodyWorldView(w, builds, count, use, v), viewturn::kNotView, w, wasWorld, kWorldFloats);
        }
    }
    Report("16b", "which uploads are turned: the body's own by the frame's build's turn; the main view's, the turned "
                  "first-person camera's, one away, one elsewhere and what isn't a view left bit for bit",
           verdictBad == 0 && changedBad == 0 && turnBad == 0 && useBad == 0 && faultBad == 0,
           "%d uploads in 4000 frames of 1 to 4 view builds: %d taken for another kind, %d of those left as they are "
           "changed, %d of the body's turned otherwise than by its frame's build's turn (the newest, or the one the main "
           "view named), %d frames' builds chosen wrong, %d not read for another reason",
           cases, verdictBad, changedBad, turnBad, useBad, faultBad);

    // c. A normal picture (no scripted view build: the player's camera, an unturned one, "cutsceneturn 0", the look
    // written): every upload left bit for bit, whatever it is and whatever the builds' numbers hold.
    int normalChanged = 0, normalTaken = 0, normalUploads = 0;
    for (int n = 0; n < 20000; ++n) {
        camera::BodyTurn builds[4];
        float cam[16], fp[16];
        MakeCamera(cam, RandomScripted(70), 1, RandomPlace(), true);
        TurnedInOwnFrame(cam, AxisAngle({1, 0, 0}, Rad(0.1)), fp);
        for (camera::BodyTurn& b : builds)
            b = MakeBuild(cam, RandomHead(), 1, false, fp, nullptr);
        const bool rh = Coin(0.8);
        double p[16];
        RandomProjection(p, rh);
        float f[kUploadFloats], was[kUploadFloats], w[kWorldFloats], wasWorld[kWorldFloats], rows[16];
        const int kind = n % 5;
        if (kind == 0)
            CameraOf(builds[0].before, RandomArmsEye(), rows);  // the body's as the game made it
        else if (kind == 1)
            CameraOf(builds[0].after, RandomArmsEye(), rows);  // the main view's
        else
            MakeCamera(rows, RandomRotation(), 1, RandomPlace(), false);
        const V3 byCamera(builds[0].place[0], builds[0].place[1] + 0.2, builds[0].place[2]);
        MakeUpload(f, rows, kind < 3 ? RandomArmsEye() : byCamera, p, rh, Coin());
        MakeWorldUpload(w, rows, byCamera, p, rh);
        if (kind == 4)
            for (float& x : f)
                x = static_cast<float>(Uniform(-2, 2));
        std::memcpy(was, f, sizeof(f));
        std::memcpy(wasWorld, w, sizeof(w));
        int use = -1;
        viewturn::BodyView v;
        viewturn::PerViewFault fault;
        normalTaken += viewturn::LockBodyPerView(f, builds, 0, use, v, fault) != viewturn::kNotView;
        normalTaken += viewturn::LockBodyWorldView(w, builds, 0, use, v) != viewturn::kNotView;
        normalTaken += use != -1;
        normalChanged += std::memcmp(f, was, sizeof(f)) != 0;
        normalChanged += std::memcmp(w, wasWorld, sizeof(w)) != 0;
        normalUploads += 2;
    }
    Report("16c", "a normal picture's uploads untouched, bit for bit", normalChanged == 0 && normalTaken == 0,
           "%d of %d uploads changed (the body's, the main view's, others', random numbers), %d taken for anything",
           normalChanged, normalUploads, normalTaken);

    // d. The FOV fix after the turn as before it: the x and y rows of the projection and the two view-projections
    // widened (from the game's 55 degrees to 100), before or after the turn, alike; read as per-view constants either
    // way. And the world view-projection alike.
    double composeOff = 0, worldComposeOff = 0;
    int composeUnread = 0;
    const float k = static_cast<float>(std::tan(Rad(55) / 2) / std::tan(Rad(100) / 2));
    for (int n = 0; n < 20000; ++n) {
        float cam[16], fp[16], turnedCam[16];
        MakeCamera(cam, RandomScripted(80), 1, RandomPlace(), true);
        TurnedInOwnFrame(cam, AxisAngle({Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)}, Rad(Uniform(0, 10))), fp);
        const camera::BodyTurn build = MakeBuild(cam, RandomHead(), 1, false, nullptr, turnedCam);
        const bool rh = Coin(0.8);
        double p[16];
        MakeProjection(p, 55, 1.0, 0.0, 0.05, 2000, rh);
        float first[kUploadFloats], second[kUploadFloats];
        MakeUpload(first, fp, RandomArmsEye(), p, rh, Coin());
        std::memcpy(second, first, sizeof(first));
        viewturn::BodyView v;
        composeUnread += viewturn::ReadPerView(first, v) != viewturn::kPerViewOk;
        viewturn::TurnPerView(first, v, build.turn);
        Rescale(first, k);
        Rescale(second, k);
        viewturn::BodyView widened;
        composeUnread += viewturn::ReadPerView(second, widened) != viewturn::kPerViewOk;
        viewturn::TurnPerView(second, widened, build.turn);
        double want[kUploadFloats];
        for (int i = 0; i < kUploadFloats; ++i)
            want[i] = first[i];
        composeOff = std::max(composeOff, RelOff(second, want, kUploadFloats));
        float w1[kWorldFloats], w2[kWorldFloats];
        MakeWorldUpload(w1, fp, V3(Uniform(-300, 300), Uniform(0, 60), Uniform(-300, 300)), p, rh);
        std::memcpy(w2, w1, sizeof(w1));
        viewturn::BodyView a, b;
        composeUnread += !viewturn::ReadViewProjection(w1, a);
        viewturn::TurnViewProjection(w1, a, build.turn);
        for (int c = 0; c < 8; ++c)
            w1[c] *= k, w2[c] *= k;
        composeUnread += !viewturn::ReadViewProjection(w2, b);
        viewturn::TurnViewProjection(w2, b, build.turn);
        // The rows' turns alike; their last numbers against the sizes summed into them (each turned about its own eye
        // as read, in floats a few hundred metres out a hundredth of a millimetre apart).
        for (int r = 0; r < 4; ++r) {
            double size = 1.0 + std::fabs(w1[4 * r + 3]);
            for (int c = 0; c < 3; ++c) {
                worldComposeOff = std::max(worldComposeOff, std::fabs(w2[4 * r + c] - w1[4 * r + c]) /
                                                                (1.0 + std::fabs(w1[4 * r + c])));
                size += std::fabs(w1[4 * r + c] * a.eye[c]);
            }
            worldComposeOff = std::max(worldComposeOff, std::fabs(w2[4 * r + 3] - w1[4 * r + 3]) / size);
        }
    }
    // (The world's last numbers: each turned about its eye as read from floats, which moves about 2.4e-7 of the eye's
    // distance; times the row's turn, against the sizes summed there, up to a few millionths.)
    Report("16d", "the FOV fix after the turn as before it", composeOff < 1e-6 && worldComposeOff < 5e-6 &&
                                                                 composeUnread == 0,
           "widened from 55 to 100 degrees after the turn or before it: %.2g apart (the world view-projection %.2g); %d "
           "not read once widened",
           composeOff, worldComposeOff, composeUnread);

    // e. The time: a body view read and turned, a 352-byte upload that isn't per-view constants, a world view.
    {
        camera::BodyTurn builds[4];
        float bodies[64][kUploadFloats], junk[64][kUploadFloats], worlds[64][kWorldFloats];
        for (int b = 0; b < 4; ++b) {
            float cam[16];
            MakeCamera(cam, RandomScripted(60), 1, RandomPlace(), true);
            builds[b] = MakeBuild(cam, RandomHead(), 1, false, nullptr, nullptr);
        }
        for (int i = 0; i < 64; ++i) {
            float rows[16], cam[16];
            CameraOf(builds[0].before, {0, 0, 0}, rows);
            TurnedInOwnFrame(rows, AxisAngle({Uniform(-1, 1), Uniform(-1, 1), Uniform(-1, 1)}, Rad(Uniform(0, 5))), cam);
            double p[16];
            RandomProjection(p, true);
            MakeUpload(bodies[i], cam, RandomArmsEye(), p, true, false);
            const V3 byCamera(builds[0].place[0], builds[0].place[1], builds[0].place[2]);
            MakeWorldUpload(worlds[i], cam, byCamera, p, true);
            for (float& x : junk[i])
                x = static_cast<float>(Uniform(-2, 2));
        }
        LARGE_INTEGER frequency, a, b;
        QueryPerformanceFrequency(&frequency);
        volatile float sink = 0;
        constexpr int kCalls = 400000;
        auto time = [&](auto call) {
            QueryPerformanceCounter(&a);
            for (int n = 0; n < kCalls; ++n)
                call(n & 63);
            QueryPerformanceCounter(&b);
            return static_cast<double>(b.QuadPart - a.QuadPart) * 1e6 / static_cast<double>(frequency.QuadPart) / kCalls;
        };
        viewturn::BodyView v;
        viewturn::PerViewFault fault;
        const double body = time([&](int i) {
            float f[kUploadFloats];
            std::memcpy(f, bodies[i], sizeof(f));
            int use = -1;
            viewturn::LockBodyPerView(f, builds, 4, use, v, fault);
            sink = sink + f[20];
        });
        const double notView = time([&](int i) {
            int use = -1;
            viewturn::LockBodyPerView(junk[i], builds, 4, use, v, fault);
            sink = sink + junk[i][20];
        });
        const double world = time([&](int i) {
            float w[kWorldFloats];
            std::memcpy(w, worlds[i], sizeof(w));
            int use = -1;
            viewturn::LockBodyWorldView(w, builds, 4, use, v);
            sink = sink + w[3];
        });
        Report("16e", "the time the body lock takes an upload", body < 2.0 && notView < 2.0 && world < 2.0,
               "%.3f us a body view read and turned, %.3f a 352-byte upload that isn't per-view constants, %.3f a world "
               "view-projection (< 2)",
               body, notView, world);
    }
}

}  // namespace

int main() {
    std::printf("viewturntest: src/viewturn.h, the view cameras' turn to the head. A scripted camera's w lanes up to "
                "%.0f, its rows square within %.2f; its own yaw all of the yaw it's made level about up to %.0f "
                "degrees of pitch, none past %.0f, the picture's top within %.1f of the vertical; a cut past %.0f "
                "degrees a frame.\n",
                viewturn::kMostW, viewturn::kSquare, Deg(std::acos(viewturn::kOwnWhole)),
                Deg(std::acos(viewturn::kOwnNone)), Deg(std::asin(viewturn::kVertical)),
                Deg(std::acos((viewturn::kCut - 1) / 2)));
    Check1();
    Check2();
    Check3();
    Check4();
    Check5();
    Check6();
    Check7();
    Check8();
    Check9();
    Check10();
    Check11();
    Check12();
    Check13();
    Check14();
    Check15();
    Check16();
    std::printf("%d of %d checks failed\n", g_failed, g_counted);
    return g_failed;
}
