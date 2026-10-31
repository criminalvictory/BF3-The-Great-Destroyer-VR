// Free hands. Found 2026-10-30: BF3 draws the first-person arms and gun in a space of their own
// with the world's axes, whose origin sits on whole-metre world coordinates near the player (the
// eye was at (0.69, 1.68, 0.18) in it, and (1457.69, 70.68, -1564.82) in the world). The view-
// projection uploaded for those draws gives the eye's position and the view's axes in that space,
// so a controller's pose relative to the head lands there directly: this works out the poses
// relative to the head, and skin.cpp places them with each draw's own view-projection.
//
// The gun model's axes match the aim pose's (x right, y up, z back towards the shooter), so the
// gun takes the right controller's aim orientation, with its grip at the right controller's grip.
// The left glove's bind space has x to the left, y up, z forward, the palm down: it is turned onto
// the left grip pose (fingers along -Z, the back of the hand along +X) and placed there.
// build\grip.txt tunes the grip points and the left hand's turn while the game runs.

#include "hands.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "collide.h"
#include "handpose.h"
#include "input.h"
#include "log.h"
#include "reload.h"
#include "skin.h"

namespace {

bool g_on = true;

// The gun's grip in its model space (behind the trigger, below the receiver: the mesh dump puts
// the trigger at y -0.07, z 0.165), and the left glove's palm in its bind space (the centre of
// its root bone's vertices).
float g_gunGrip[3] = {0.0f, -0.11f, 0.21f};
bool g_gunGripTuned = false;  // grip.txt set it; otherwise skin::GunGrip, the game's own hold
float g_leftAnchor[3] = {0.502f, 1.146f, 0.144f};
float g_leftTurn[3] = {0.0f, 0.0f, 0.0f};  // degrees about the grip's x, y, z, after the default
// The left glove's placement on the left grip pose: the gloves' bind space -> the grip's frame
// (rows: rotation | translation). The View button's line-up sets it (saved as grip.txt
// "leftpose"); built in is the user's own on the Steam Frame (2026-10-30, the most reliable one),
// so a clean build keeps it. grip.txt "leftturn" without a "leftpose" makes it from the default
// turn and palm instead.
float g_leftOffset[12] = {-0.65114f, -0.68371f, -0.32949f, 1.15898f,  -0.54118f, 0.72265f,
                          -0.43004f, -0.39106f, 0.53211f,  -0.10169f, -0.84054f, 0.02741f};
bool g_leftOffsetSet = true;
// The line-up: the first View press holds the glove still, the hand is put into it, the second
// press keeps where the hand is relative to the glove.
bool g_lining = false;
float g_heldLeft[12] = {};
// The left glove's fingers, smoothed: the index finger (the trigger), the other three (the grip)
// and the thumb (on the stick).
float g_index = 0.0f, g_grip = 0.0f, g_thumb = 0.0f;
// How far apart the relaxed fingers are (0-1: the user wanted space between them) and how much
// wider the palm is drawn (it looked skinny; the whole glove 10% bigger looked huge, the whole hand
// widened alike thick): grip.txt "spread 0.7", "leftwidth 1.15".
float g_spread = 0.7f;
float g_leftWidth = 1.15f;
// grip.txt "fingers i g t": the index finger, the other three and the thumb held so (0-1 each)
// whatever the controller says, to check the poses in the game.
bool g_fingersFixed = false;
float g_fixedFingers[3] = {};

// Row-major 3x3 from a quaternion (x, y, z, w).
void QuatToMatrix(const XrQuaternionf& q, float* m) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    m[0] = 1 - 2 * (y * y + z * z), m[1] = 2 * (x * y - z * w), m[2] = 2 * (x * z + y * w);
    m[3] = 2 * (x * y + z * w), m[4] = 1 - 2 * (x * x + z * z), m[5] = 2 * (y * z - x * w);
    m[6] = 2 * (x * z - y * w), m[7] = 2 * (y * z + x * w), m[8] = 1 - 2 * (x * x + y * y);
}

void Mul3(const float* a, const float* b, float* out) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
}

void Transpose3(const float* a, float* out) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out[i * 3 + j] = a[j * 3 + i];
}

// A turn about x, then y, then z, in degrees.
void Euler(const float* degrees, float* m) {
    constexpr float kRad = 3.14159265f / 180.0f;
    float cx = std::cos(degrees[0] * kRad), sx = std::sin(degrees[0] * kRad);
    float cy = std::cos(degrees[1] * kRad), sy = std::sin(degrees[1] * kRad);
    float cz = std::cos(degrees[2] * kRad), sz = std::sin(degrees[2] * kRad);
    float rx[9] = {1, 0, 0, 0, cx, -sx, 0, sx, cx}, ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy},
          rz[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 1}, t[9];
    Mul3(ry, rx, t);
    Mul3(rz, t, m);
}

// Affine 3x4 matrices (three rows: rotation | translation).
void Compose(const float* a, const float* b, float* out) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j)
            out[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j];
        out[i * 4 + 3] += a[i * 4 + 3];
    }
}

// The inverse of a rotation | translation (no scale).
void InvertRigid(const float* m, float* out) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out[i * 4 + j] = m[j * 4 + i];
    for (int i = 0; i < 3; ++i)
        out[i * 4 + 3] = -(out[i * 4] * m[3] + out[i * 4 + 1] * m[7] + out[i * 4 + 2] * m[11]);
}

void Transform(const float* m, const float* p, float* out) {
    for (int i = 0; i < 3; ++i)
        out[i] = m[i * 4] * p[0] + m[i * 4 + 1] * p[1] + m[i * 4 + 2] * p[2] + m[i * 4 + 3];
}

// A pose matrix (three rows: rotation | translation) putting the point anchor at position.
void PlaceAt(const float* rotation, const float* anchor, const float* position, float* out) {
    for (int i = 0; i < 3; ++i) {
        const float* r = rotation + i * 3;
        out[i * 4] = r[0], out[i * 4 + 1] = r[1], out[i * 4 + 2] = r[2];
        out[i * 4 + 3] = position[i] - (r[0] * anchor[0] + r[1] * anchor[1] + r[2] * anchor[2]);
    }
}

// build\grip.txt: "gun x y z" (the gun's grip in its model space), "left x y z" (the left palm in
// the gloves' bind space), "leftturn x y z" (degrees), "on" / "off".
void ReadTuning() {
    static FILETIME lastWrite{};
    static ULONGLONG lastCheck = 0;
    ULONGLONG now = GetTickCount64();
    if (now - lastCheck < 300)
        return;
    lastCheck = now;
    wchar_t path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ReadTuning), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\'))
        wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"grip.txt");
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &info) ||
        CompareFileTime(&info.ftLastWriteTime, &lastWrite) == 0)
        return;
    lastWrite = info.ftLastWriteTime;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"r") != 0 || !f)
        return;
    char line[512];
    g_fingersFixed = false;
    bool pose = false, turn = false;
    while (fgets(line, sizeof(line), f)) {
        float v[12];
        if (std::strncmp(line, "fingers", 7) == 0 && sscanf_s(line + 7, "%f %f %f", &v[0], &v[1], &v[2]) == 3)
            std::memcpy(g_fixedFingers, v, sizeof(g_fixedFingers)), g_fingersFixed = true;
        else if (std::strncmp(line, "gun", 3) == 0 && sscanf_s(line + 3, "%f %f %f", &v[0], &v[1], &v[2]) == 3)
            std::memcpy(g_gunGrip, v, 3 * sizeof(float)), g_gunGripTuned = true;
        else if (std::strncmp(line, "leftpose", 8) == 0 &&
                 sscanf_s(line + 8, "%f %f %f %f %f %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                          &v[6], &v[7], &v[8], &v[9], &v[10], &v[11]) == 12)
            std::memcpy(g_leftOffset, v, sizeof(g_leftOffset)), g_leftOffsetSet = pose = true;
        else if (std::strncmp(line, "spread", 6) == 0 && sscanf_s(line + 6, "%f", &v[0]) == 1)
            g_spread = std::fmin(std::fmax(v[0], 0.0f), 1.0f);
        else if (std::strncmp(line, "leftwidth", 9) == 0 && sscanf_s(line + 9, "%f", &v[0]) == 1)
            g_leftWidth = std::fmin(std::fmax(v[0], 0.8f), 1.5f);
        else if (std::strncmp(line, "leftturn", 8) == 0 && sscanf_s(line + 8, "%f %f %f", &v[0], &v[1], &v[2]) == 3)
            std::memcpy(g_leftTurn, v, 3 * sizeof(float)), turn = true;
        else if (std::strncmp(line, "left", 4) == 0 && sscanf_s(line + 4, "%f %f %f", &v[0], &v[1], &v[2]) == 3)
            std::memcpy(g_leftAnchor, v, 3 * sizeof(float));
    }
    fclose(f);
    if (turn && !pose)
        g_leftOffsetSet = false;  // made from the turn and palm (Update)
    Log("Hands: gun grip (%.3f %.3f %.3f), left palm (%.3f %.3f %.3f), left turn (%.0f %.0f %.0f), fingers apart "
        "%.2f, palm %.2f wide",
        g_gunGrip[0], g_gunGrip[1], g_gunGrip[2], g_leftAnchor[0], g_leftAnchor[1], g_leftAnchor[2], g_leftTurn[0],
        g_leftTurn[1], g_leftTurn[2], g_spread, g_leftWidth);
}

// The free left glove can't go into the gun (the user: the hand went through the gun when it
// slapped the bolt release): collide.cpp keeps it out, from frame to frame. The glove is drawn
// there; the reloads still go by the hand.
collide::State g_collide;

// Keeps the glove out of the gun (collide::KeepOut) and moves it there, its wrist with it. touch
// (the gun's model space) gets where it touches the gun's surface, if it does. True when it touches.
bool KeepOutOfGun(skin::HandTargets& t, const handpose::Fingers& fingers, float* touch) {
    float bones[handpose::kBones][12], inverse[12], toGun[12], shift[3];
    handpose::Pose(fingers, g_leftWidth, bones);
    InvertRigid(t.gunPose, inverse);
    Compose(inverse, t.leftPose, toGun);
    const bool touched = collide::KeepOut(bones, toGun, g_collide, shift, touch);
    if (shift[0] || shift[1] || shift[2]) {
        // Into the view's space with the gun's turn, onto the glove and its wrist.
        for (int r = 0; r < 3; ++r) {
            const float v = t.gunPose[r * 4] * shift[0] + t.gunPose[r * 4 + 1] * shift[1] + t.gunPose[r * 4 + 2] * shift[2];
            t.leftPose[r * 4 + 3] += v;
            t.leftWrist[r] += v;
        }
    }
    return touched;
}

// The left hand on the handguard (the user: time the left hand works as a grip for the gun): the
// left grip squeezed with the palm near the handguard takes hold. The gun then points from the
// right hand towards the left, by the smallest turn from where the right hand alone points it
// (the right hand keeps the roll), and the glove stays on the handguard where it took hold. The
// game aims and fires along it too (AimOrientation). It holds until the grip is let go: it had
// let go whenever the hand strayed from where the right hand alone points the gun, which any
// sway did, and it took hold only within 13 cm, where the user's wrist sits on the handguard
// (100 to 128 mm in the log, 2026-10-30). Holding, the glove is the game's own left hand as its
// animation holds this gun (skin::GameLeftGrip: made for it, launcher and all, so it doesn't clip),
// on the free gun, its fingers moving a little with the controller's (BendWithController). A grip
// fitted to the gun's collision shape went through the grenade launcher, which isn't in the shape;
// the user wanted only the game's (2026-10-30).
// The handguard's middle line in the gun's model space: round the bore, its cross-section 57 mm
// across and 73 mm high from z 0.46 to 0.68 (tools/gripfit.cpp, from the gun's mesh dump).
constexpr float kGuardFrom[3] = {0.0f, 0.045f, 0.46f}, kGuardTo[3] = {0.0f, 0.045f, 0.68f};
constexpr float kGuardReach = 0.25f;   // the palm (the wrist's fold) within this of it takes hold
constexpr float kHandsTogether = 0.10f;  // the left hand back by the right: off to the receiver
bool g_supporting = false;
bool g_freshSqueeze = true;  // the grip squeezed since the hand last let go (or never let go)
float g_supportPoint[3] = {};   // the palm when it took hold, in the gun's model space
float g_gripBlend = 0.0f;  // 0 the glove where the hand is, 1 on the handguard
constexpr float kGripBlendStep = 0.2f;  // a frame: on or off the handguard in 5 frames (70 ms)
float g_fingerBend[5] = {};  // the controller's extra bend of the held fingers, degrees (smoothed)
bool g_aimTurned = false;
XrQuaternionf g_aimTurn{0, 0, 0, 1};  // the right controller's aim, turned to point the gun

float Length(const float* v) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// The smallest turn taking unit vector a onto unit vector b (row-major 3x3).
void Arc(const float* a, const float* b, float* m) {
    const float v[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    const float c = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    const float k = c > -0.999f ? 1.0f / (1.0f + c) : 0.0f;
    const float x[9] = {0, -v[2], v[1], v[2], 0, -v[0], -v[1], v[0], 0};
    float xx[9];
    Mul3(x, x, xx);
    for (int i = 0; i < 9; ++i)
        m[i] = (i % 4 == 0 ? 1.0f : 0.0f) + x[i] + xx[i] * k;
}

XrQuaternionf MatrixToQuat(const float* m) {
    XrQuaternionf q;
    const float trace = m[0] + m[4] + m[8];
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s, 0.25f * s};
    } else if (m[0] > m[4] && m[0] > m[8]) {
        const float s = std::sqrt(1.0f + m[0] - m[4] - m[8]) * 2.0f;
        q = {0.25f * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s, (m[7] - m[5]) / s};
    } else if (m[4] > m[8]) {
        const float s = std::sqrt(1.0f + m[4] - m[0] - m[8]) * 2.0f;
        q = {(m[1] + m[3]) / s, 0.25f * s, (m[5] + m[7]) / s, (m[2] - m[6]) / s};
    } else {
        const float s = std::sqrt(1.0f + m[8] - m[0] - m[4]) * 2.0f;
        q = {(m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25f * s, (m[3] - m[1]) / s};
    }
    return q;
}

XrQuaternionf QuatMul(const XrQuaternionf& a, const XrQuaternionf& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// The palm's offset from the nearest point of the handguard's line (r) and its length, in the
// gun's model space.
float FromGuard(const float* p, float* r = nullptr) {
    const float d[3] = {kGuardTo[0] - kGuardFrom[0], kGuardTo[1] - kGuardFrom[1], kGuardTo[2] - kGuardFrom[2]};
    const float e[3] = {p[0] - kGuardFrom[0], p[1] - kGuardFrom[1], p[2] - kGuardFrom[2]};
    float s = (d[0] * e[0] + d[1] * e[1] + d[2] * e[2]) / (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    s = std::fmin(std::fmax(s, 0.0f), 1.0f);
    float offset[3] = {e[0] - d[0] * s, e[1] - d[1] * s, e[2] - d[2] * s};
    if (r)
        std::memcpy(r, offset, sizeof(offset));
    return Length(offset);
}

// The left hand on the charging handle (the user: the index and middle fingers wrapped round the
// handle's T bar, touching it, not near it). The hand's place on the gun was fitted from the mesh
// dumps (tools/leverfit.cpp: the glove posed as below, palm down, fingers forward over the bar):
// the bar sits inside the curled index and middle fingers, which touch its top, front, bottom and
// back, either side of the shaft; the rest of the hand on the receiver's rear. While the handle is
// held the glove is there, sliding back with it; it blends in from the tracked hand and back out.
// The pull itself is measured from the hand (reload.cpp).
constexpr float kOnLever[12] = {0.63300f, -0.38000f, -0.67500f, 0.24232f,  0.63900f, 0.74800f,
                                0.17900f, -1.10752f, 0.43700f,  -0.54400f, 0.71600f, 0.43907f};
constexpr float kLeverCurl[4] = {0.62f, 0.62f, 0.85f, 0.85f};  // index, middle (round the bar), ring, little
constexpr float kLeverThumb = 0.35f, kLeverSpread = 0.4f;
float g_lever = 0.0f;  // 0 the tracked hand, 1 on the handle

// a and b (rotation | translation rows) blended: s 0 is a, 1 is b.
void Blend(const float* a, const float* b, float s, float* out) {
    float ra[9], rb[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            ra[i * 3 + j] = a[i * 4 + j], rb[i * 3 + j] = b[i * 4 + j];
    XrQuaternionf qa = MatrixToQuat(ra), qb = MatrixToQuat(rb);
    if (qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w < 0)
        qb = {-qb.x, -qb.y, -qb.z, -qb.w};
    XrQuaternionf q{qa.x + (qb.x - qa.x) * s, qa.y + (qb.y - qa.y) * s, qa.z + (qb.z - qa.z) * s, qa.w + (qb.w - qa.w) * s};
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    q = {q.x / n, q.y / n, q.z / n, q.w / n};
    float m[9];
    QuatToMatrix(q, m);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            out[i * 4 + j] = m[i * 3 + j];
        out[i * 4 + 3] = a[i * 4 + 3] + (b[i * 4 + 3] - a[i * 4 + 3]) * s;
    }
}

void OnTheLever(skin::HandTargets& t, handpose::Fingers& fingers) {
    const bool held = t.gun && reload::HandleHeld();
    g_lever += ((held ? 1.0f : 0.0f) - g_lever) * 0.35f;
    if (g_lever < 0.001f || !t.gun)
        return;
    // On the handle, slid back with it (along the gun's -z).
    const float back[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -reload::HandleBack()};
    float slid[12], onHandle[12], blended[12];
    Compose(t.gunPose, back, slid);
    Compose(slid, kOnLever, onHandle);
    Blend(t.leftPose, onHandle, g_lever, blended);
    std::memcpy(t.leftPose, blended, sizeof(blended));
    Transform(t.leftPose, g_leftAnchor, t.leftWrist);
    for (int i = 0; i < 4; ++i)
        fingers.curl[i] += (kLeverCurl[i] - fingers.curl[i]) * g_lever;
    fingers.thumb += (kLeverThumb - fingers.thumb) * g_lever;
    fingers.spread += (kLeverSpread - fingers.spread) * g_lever;
}

// The game's grip a little alive: each held finger bent or opened a few degrees with the
// controller's (the index with the trigger, the others with the grip, the thumb with the thumb on
// the stick), about the game's own pose (the user found its fingers stiff, 2026-10-30).
void BendWithController(float (*game)[12]) {
    const float squeeze = std::fmin(std::fmax((input::LeftSqueeze() - 0.4f) / 0.6f, 0.0f), 1.0f);
    const float target[5] = {input::LeftThumbOnStick() ? 5.0f : -6.0f, -12.0f + 18.0f * input::LeftTrigger(),
                             -8.0f + 14.0f * squeeze, -8.0f + 14.0f * squeeze, -8.0f + 14.0f * squeeze};
    for (int f = 0; f < 5; ++f)
        g_fingerBend[f] += (target[f] - g_fingerBend[f]) * 0.3f;
    handpose::BendFurther(game, g_fingerBend);
}

// With both hands tracked: takes hold, holds or lets go, and when holding points the gun from
// the right hand to the left (the glove goes onto the handguard in Update). gunTurn: the gun's turn
// as the right hand alone points it (view space, model -> view); anchor: the right hand's place on
// the gun (model space); headTurn: the view's axes in the recentered space; aim: the right
// controller's aim orientation there.
void Support(skin::HandTargets& t, const float* gunTurn, const float* anchor, const float* headTurn,
             const XrQuaternionf& aim) {
    const float squeeze = input::LeftSqueeze();
    if (squeeze < 0.4f)
        g_freshSqueeze = true;
    float inverse[12], palm[3];
    InvertRigid(t.gunPose, inverse);
    Transform(inverse, t.leftWrist, palm);
    const float apart[3] = {t.leftWrist[0] - t.rightWrist[0], t.leftWrist[1] - t.rightWrist[1],
                            t.leftWrist[2] - t.rightWrist[2]};
    const float between = Length(apart);
    const float distance = FromGuard(palm);
    // Squeezed near the handguard, or squeezed and then brought to it.
    if (!g_supporting && squeeze > 0.6f && g_freshSqueeze && !reload::HandleHeld() && distance < kGuardReach &&
        between > kHandsTogether) {
        g_supporting = true;
        std::memcpy(g_supportPoint, palm, sizeof(palm));
        Log("Hands: the left hand took hold of the handguard (%.0f mm from its line)", distance * 1000.0f);
    } else if (g_supporting && squeeze < 0.4f) {
        // Only the grip let go lets go (the user: it dropped the gun when swung far right or left,
        // the hands coming within 10 cm; "locked no matter what until I let go", 2026-10-30).
        g_supporting = false;
        g_freshSqueeze = false;
        Log("Hands: the left hand let go of the handguard");
    }
    g_aimTurned = false;
    if (!g_supporting)
        return;
    // The gun's line from the right hand to where the left took hold, as the right hand points it,
    // turned onto the line between the hands; with the hands too close for that line to be steady,
    // the right hand alone points it.
    float model[3] = {g_supportPoint[0] - anchor[0], g_supportPoint[1] - anchor[1], g_supportPoint[2] - anchor[2]};
    const float lm = Length(model), la = Length(apart);
    if (!(lm > 0.05f) || !(la > kHandsTogether))
        return;
    float from[3], to[3], turn[9], turned[9];
    for (int i = 0; i < 3; ++i)
        model[i] /= lm, to[i] = apart[i] / la;
    for (int i = 0; i < 3; ++i)
        from[i] = gunTurn[i * 3] * model[0] + gunTurn[i * 3 + 1] * model[1] + gunTurn[i * 3 + 2] * model[2];
    Arc(from, to, turn);
    Mul3(turn, gunTurn, turned);
    PlaceAt(turned, anchor, t.rightWrist, t.gunPose);
    // The same turn in the recentered space, for the game's aim: headTurn turn headTurn^T.
    float headT[9], a[9], inRoom[9];
    Transpose3(headTurn, headT);
    Mul3(headTurn, turn, a);
    Mul3(a, headT, inRoom);
    g_aimTurn = QuatMul(MatrixToQuat(inRoom), aim);
    g_aimTurned = true;
}

// The left glove's line-up, kept in grip.txt (rewritten whole).
void SaveLeftOffset() {
    wchar_t path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&SaveLeftOffset), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\'))
        wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"grip.txt");
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"w") != 0 || !f)
        return;
    if (g_gunGripTuned)
        fprintf(f, "gun %.4f %.4f %.4f\n", g_gunGrip[0], g_gunGrip[1], g_gunGrip[2]);
    fprintf(f, "left %.4f %.4f %.4f\n", g_leftAnchor[0], g_leftAnchor[1], g_leftAnchor[2]);
    fprintf(f, "spread %.2f\n", g_spread);
    fprintf(f, "leftwidth %.2f\n", g_leftWidth);
    const float* o = g_leftOffset;
    fprintf(f, "leftpose %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f\n", o[0], o[1], o[2], o[3], o[4],
            o[5], o[6], o[7], o[8], o[9], o[10], o[11]);
    fclose(f);
}

}

namespace hands {

void Update(const XrPosef& head) {
    ReadTuning();
    if (!g_on) {
        skin::SetHands(nullptr);
        return;
    }
    // Everything is worked out in the recentered space, then given to skin.cpp in the view's own
    // space (relative to the head), which places it with each frame's eye. The gun flickered
    // while walking when this placed it in the arms' space itself: that space's origin jumps a
    // metre at a time as the player moves, and the targets were a frame late.
    float headTurn[9], toView[9];
    QuatToMatrix(head.orientation, headTurn);
    Transpose3(headTurn, toView);
    auto pose = [](const XrPosef& p, float* out) {
        float m[9];
        QuatToMatrix(p.orientation, m);
        const float at[3] = {p.position.x, p.position.y, p.position.z};
        for (int i = 0; i < 3; ++i)
            out[i * 4] = m[i * 3], out[i * 4 + 1] = m[i * 3 + 1], out[i * 4 + 2] = m[i * 3 + 2], out[i * 4 + 3] = at[i];
    };
    float headPose[12], fromHead[12];  // the recentered space -> the view's
    pose(head, headPose);
    InvertRigid(headPose, fromHead);
    auto toViewPoint = [&](const XrVector3f& p, float* out) {
        const float at[3] = {p.x, p.y, p.z};
        Transform(fromHead, at, out);
    };

    skin::HandTargets t;
    handpose::Fingers fingers;  // the left glove's, as the controller says
    XrPosef aim, grip;
    float gunTurn[9], anchor[3];  // the gun as the right hand alone holds it (view space), its grip
    if (input::AimPose(aim) && input::HandPose(1, grip)) {
        // The gun model's axes are x left, y up, z forward (its muzzle; the first try took z for
        // back and the gun pointed at the player), the aim pose's x right, y up, z back: half a
        // turn about y between them.
        const float flip[9] = {-1, 0, 0, 0, 1, 0, 0, 0, -1};
        float aimTurn[9], turn[9];
        QuatToMatrix(aim.orientation, aimTurn);
        Mul3(toView, aimTurn, turn);
        Mul3(turn, flip, gunTurn);
        toViewPoint(grip.position, t.rightWrist);
        // Where the game's own right hand holds the gun, unless grip.txt says otherwise.
        if (g_gunGripTuned || !skin::GunGrip(anchor)) {
            std::memcpy(anchor, g_gunGrip, sizeof(anchor));
        } else {
            static bool logged = false;
            if (!logged) {
                logged = true;
                Log("Hands: the game holds the gun at (%.3f %.3f %.3f) in its model space", anchor[0], anchor[1],
                    anchor[2]);
            }
        }
        PlaceAt(gunTurn, anchor, t.rightWrist, t.gunPose);
        t.gun = true;
    }
    if (input::HandPose(0, grip)) {
        if (!g_leftOffsetSet) {
            // The glove's bind axes onto the grip pose's: x (outward) -> +Y, y (back of the hand)
            // -> +X, z (fingers) -> -Z, then the tuning turn; the palm at the grip.
            const float glove[9] = {0, 1, 0, 1, 0, 0, 0, 0, -1};
            const float origin[3] = {};
            float tuned[9], local[9];
            Euler(g_leftTurn, tuned);
            Mul3(tuned, glove, local);
            PlaceAt(local, g_leftAnchor, origin, g_leftOffset);
        }
        // In the recentered space: the glove held still there during the line-up stays put in the
        // room while the head moves.
        float controller[12], glove[12];
        pose(grip, controller);
        Compose(controller, g_leftOffset, glove);
        if (input::TakeCalibratePress()) {
            if (!g_lining) {
                std::memcpy(g_heldLeft, glove, sizeof(g_heldLeft));
                g_lining = true;
                Log("Hands: the left glove is held still; put the left hand into it and press View again");
                Beep(880, 150);
            } else {
                float inverse[12];
                InvertRigid(controller, inverse);
                Compose(inverse, g_heldLeft, g_leftOffset);
                g_leftOffsetSet = true;
                g_lining = false;
                SaveLeftOffset();
                Log("Hands: the left glove is lined up with the hand (saved to grip.txt)");
                Beep(1320, 200);
            }
        }
        Compose(fromHead, g_lining ? g_heldLeft : glove, t.leftPose);
        Transform(t.leftPose, g_leftAnchor, t.leftWrist);  // the palm, where the arm folds into
        // The fingers follow the controller: the trigger the index finger, the grip the other three,
        // the thumb on the stick folds the thumb in; all of them, a fist. Relaxed while lining up.
        const float* fixed = g_fingersFixed ? g_fixedFingers : nullptr;
        float index = fixed ? fixed[0] : input::LeftTrigger(), grip = fixed ? fixed[1] : input::LeftSqueeze(),
              thumb = fixed ? fixed[2] : input::LeftThumbOnStick() ? 1.0f : 0.0f;
        g_index += ((g_lining ? 0.0f : index) - g_index) * 0.35f;
        g_grip += ((g_lining ? 0.0f : grip) - g_grip) * 0.35f;
        g_thumb += ((g_lining ? 0.0f : thumb) - g_thumb) * 0.35f;
        fingers.curl[0] = g_index;
        fingers.curl[1] = fingers.curl[2] = fingers.curl[3] = g_grip;
        // Folded part way on its own; with the grip held, down onto the curled middle finger (also
        // when the index finger points: hovering over it looked wrong, user 2026-10-30).
        fingers.thumb = g_thumb * (0.6f + 0.4f * g_grip);
        fingers.spread = g_spread;
        t.left = true;
    }
    // Once a second: frames where a controller wasn't tracked (the game's own gun or hand is drawn
    // for those).
    static int frames = 0, noGun = 0, noLeft = 0;
    static ULONGLONG lastLog = 0;
    ++frames, noGun += !t.gun, noLeft += !t.left;
    if (GetTickCount64() - lastLog >= 1000) {
        lastLog = GetTickCount64();
        if (noGun || noLeft)
            Log("Hands: of the last %d frames, %d without the right controller's aim or grip, %d without the left", frames,
                noGun, noLeft);
        frames = noGun = noLeft = 0;
    }
    // The reloads go by where the left hand really is, wherever the glove is drawn.
    float palm[3];
    std::memcpy(palm, t.leftWrist, sizeof(palm));
    // The glove as the controller holds it, to blend from onto the handguard and back.
    float tracked[12];
    std::memcpy(tracked, t.leftPose, sizeof(tracked));
    const bool trackedLeft = t.left;
    // Both hands on the gun: it points from the right hand to the left, the glove on the handguard.
    if (t.gun && t.left && !g_lining) {
        Support(t, gunTurn, anchor, headTurn, aim.orientation);
    } else {
        // Without the left controller's place the hold stays (the gun goes by the right hand, the
        // glove on the handguard); its grip button still lets go. Without the gun, nothing to hold.
        g_aimTurned = false;
        if (g_supporting && (!t.gun || g_lining)) {
            Log("Hands: the left hand let go of the handguard (no gun)");
            g_supporting = false;
        } else if (g_supporting && input::LeftSqueeze() < 0.4f) {
            Log("Hands: the left hand let go of the handguard");
            g_supporting = false;
            g_freshSqueeze = false;
        }
    }
    // Onto the handguard and off it over a few frames, not in one jump: the glove between where the
    // hand is and the game's own grip on the gun (its bones learned against the game's gun: game[0]
    // places the glove, the rest bend relative to it in BendWithController below).
    float game[handpose::kBones][12];
    const bool haveGame = t.gun && skin::GameLeftGrip(game);
    g_gripBlend = g_supporting && haveGame ? std::fmin(g_gripBlend + kGripBlendStep, 1.0f)
                                           : std::fmax(g_gripBlend - kGripBlendStep, 0.0f);
    if (g_gripBlend > 0.0f && haveGame) {
        float hold[12], blended[12];
        Compose(t.gunPose, game[0], hold);
        if (trackedLeft)
            Blend(tracked, hold, g_gripBlend, blended);
        else
            std::memcpy(blended, hold, sizeof(blended));  // the left controller not tracked: held as it is
        std::memcpy(t.leftPose, blended, sizeof(blended));
        Transform(t.leftPose, g_leftAnchor, t.leftWrist);
        t.left = true;
    }
    // The free glove stays out of the gun, and where it touches the gun is felt (the bolt release).
    float touch[3];
    const bool free = t.gun && t.left && !g_lining && !g_supporting && g_gripBlend == 0.0f && !reload::HandleHeld();
    if (!free)
        collide::Reset(g_collide);
    const bool touching = free && KeepOutOfGun(t, fingers, touch);
    // The bolt and its manual releases (reload.cpp): the gun and the left palm, both in the view's space.
    reload::Update(t.gun ? t.gunPose : nullptr, t.left ? palm : nullptr, input::LeftTrigger(), touching ? touch : nullptr);
    if (t.left) {
        OnTheLever(t, fingers);
        handpose::Pose(fingers, g_leftWidth, t.leftFingers);
        if (g_gripBlend > 0.0f && haveGame) {
            // The game's grip, its fingers moving a little with the controller's (less stiff: user,
            // 2026-10-30), relative to its palm bone; blended from the controller's own fingers.
            BendWithController(game);
            float root[12], held[12];
            InvertRigid(game[0], root);
            for (int b = 0; b < handpose::kBones; ++b) {
                Compose(root, game[b], held);
                Blend(t.leftFingers[b], held, g_gripBlend, t.leftFingers[b]);
            }
        }
    }
    skin::SetHands(t.gun || t.left ? &t : nullptr);
}

bool On() {
    return g_on;
}

bool AimOrientation(XrQuaternionf& out) {
    if (!g_on || !g_aimTurned)
        return false;
    out = g_aimTurn;
    return true;
}

void Toggle() {
    g_on = !g_on;
    if (!g_on)
        skin::SetHands(nullptr);
    Log("Hands: %s", g_on ? "free (the gun in the right hand, the left hand on its own)" : "the game's own");
    Beep(g_on ? 1200 : 440, 200);
}

}
