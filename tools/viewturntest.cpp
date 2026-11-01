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
//   8  a forward near the vertical: pitching down to 90 degrees and back up at 90 deg/s, the yaw still: the yaw kept
//      (0.01 deg), no frame turning it; straight down with no yaw kept, along the ground the way the picture's top
//      points; straight up, the other way; random ones within 6 degrees of the vertical, with and without a yaw kept:
//      never NaN, always level and square
//   9  offered again once turned (camera.cpp's again guard): turned a second time, the head's yaw counts twice; the
//      turned camera's yaw and pitch are the ones shown, as the guard looks for (0.01 deg), and no fresh camera of a
//      swaying, turning drive passes for one (the guard's tolerances, copied from camera.cpp)
//  10  the w lanes (the finding): in the logged scripted cameras they're their rotation quaternion's 2(yw - xy),
//      -2(xy + yw) and ww + xx - yy - zz (0.06: the log's rounding)
//  11  a scripted camera's turn takes under 1 us
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
        viewturn::Level level;
        // With mirrorfix: the twin's picture, its right row the other way.
        float a[16], b[16];
        std::memcpy(a, twin, sizeof(a));
        std::memcpy(b, mirror, sizeof(b));
        viewturn::Turn(a, TurnFor(head), true, 1, false, level);
        viewturn::Turn(b, TurnFor(head), true, -1, true, level);
        fixedOff =
            std::max({fixedOff, Len(Row(b, 0) + Row(a, 0)), Len(Row(b, 1) - Row(a, 1)), Len(Row(b, 2) - Row(a, 2))});
        // Without ("mirrorfix 0"): the twin's picture for the head's yaw and roll the other way.
        const Q reversed{head.x, -head.y, -head.z, head.w};
        std::memcpy(a, twin, sizeof(a));
        std::memcpy(b, mirror, sizeof(b));
        viewturn::Turn(a, TurnFor(reversed), true, 1, false, level);
        viewturn::Turn(b, TurnFor(head), true, -1, false, level);
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
    // Random ones within 6 degrees of the vertical, with a yaw kept and without, any roll, the head any way.
    int bad = 0;
    double square = 0;
    for (int n = 0; n < 20000; ++n) {
        const double pitch = (Coin() ? 1 : -1) * Uniform(84, 90);
        float m[16];
        MakeCamera(m, Ypr(Uniform(-kPi, kPi), Rad(pitch), Rad(Uniform(-180, 180))), Coin(0.25) ? -1 : 1, RandomPlace(),
                   true);
        viewturn::Level level;
        if (Coin()) {
            level.have = true;
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
    Report("11", "the time a scripted camera's turn takes", us < 1.0, "%.3f us a turn (< 1)", us);
}

}  // namespace

int main() {
    std::printf("viewturntest: src/viewturn.h, the view cameras' turn to the head. A scripted camera's w lanes up to "
                "%.0f, its rows square within %.2f, its yaw kept with the forward within %.1f degrees of the vertical.\n",
                viewturn::kMostW, viewturn::kSquare, Deg(std::asin(viewturn::kVertical)));
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
    std::printf("%d of %d checks failed\n", g_failed, g_counted);
    return g_failed;
}
