// Free hands. Found 2026-10-30: BF3 draws the first-person arms and gun in a space of their own
// with the world's axes, whose origin sits on whole-metre world coordinates near the player (the
// eye was at (0.69, 1.68, 0.18) in it, and (1457.69, 70.68, -1564.82) in the world). The view-
// projection uploaded for those draws (skin::FirstPersonViews) gives the eye's position and the
// view's axes in that space, so a controller's pose relative to the head lands there directly.
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

#include "input.h"
#include "log.h"
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
constexpr float kEyeHeight = 1.6f;          // the floor this far below the eye (standing)

void Cross(const float* a, const float* b, float* out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

bool Normalize(float* v) {
    float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!(length > 1e-6f))
        return false;
    v[0] /= length, v[1] /= length, v[2] /= length;
    return true;
}

// The eye: where the clip x, y and w rows of a view-projection are all zero.
bool SolveEye(const float* m, float* eye) {
    const float* r[3] = {m, m + 4, m + 12};
    float a[3][3], b[3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            a[i][j] = r[i][j];
        b[i] = -r[i][3];
    }
    float det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
                a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    if (!(std::fabs(det) > 1e-9f))
        return false;
    for (int k = 0; k < 3; ++k) {
        float c[3][3];
        std::memcpy(c, a, sizeof(c));
        for (int i = 0; i < 3; ++i)
            c[i][k] = b[i];
        eye[k] = (c[0][0] * (c[1][1] * c[2][2] - c[1][2] * c[2][1]) - c[0][1] * (c[1][0] * c[2][2] - c[1][2] * c[2][0]) +
                  c[0][2] * (c[1][0] * c[2][1] - c[1][1] * c[2][0])) /
                 det;
    }
    return true;
}

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
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        float v[3];
        if (std::strncmp(line, "gun", 3) == 0 && sscanf_s(line + 3, "%f %f %f", &v[0], &v[1], &v[2]) == 3)
            std::memcpy(g_gunGrip, v, sizeof(v)), g_gunGripTuned = true;
        else if (std::strncmp(line, "leftturn", 8) == 0 && sscanf_s(line + 8, "%f %f %f", &v[0], &v[1], &v[2]) == 3)
            std::memcpy(g_leftTurn, v, sizeof(v));
        else if (std::strncmp(line, "left", 4) == 0 && sscanf_s(line + 4, "%f %f %f", &v[0], &v[1], &v[2]) == 3)
            std::memcpy(g_leftAnchor, v, sizeof(v));
    }
    fclose(f);
    Log("Hands: gun grip (%.3f %.3f %.3f), left palm (%.3f %.3f %.3f), left turn (%.0f %.0f %.0f)", g_gunGrip[0],
        g_gunGrip[1], g_gunGrip[2], g_leftAnchor[0], g_leftAnchor[1], g_leftAnchor[2], g_leftTurn[0], g_leftTurn[1],
        g_leftTurn[2]);
}

}

namespace hands {

void Update(const XrPosef& head) {
    ReadTuning();
    float arms[16], world[16], eye[3], eyeWorld[3];
    if (!g_on || !skin::FirstPersonViews(arms, world) || !SolveEye(arms, eye) || !SolveEye(world, eyeWorld)) {
        skin::SetHands(nullptr);
        return;
    }
    // The view's axes in the arms' space: OpenXR's head axes (x right, y up, z back) map onto them.
    float right[3] = {arms[0], arms[1], arms[2]}, up[3] = {arms[4], arms[5], arms[6]},
          back[3] = {-arms[12], -arms[13], -arms[14]};
    if (!Normalize(right) || !Normalize(up) || !Normalize(back)) {
        skin::SetHands(nullptr);
        return;
    }
    float view[9] = {right[0], up[0], back[0], right[1], up[1], back[1], right[2], up[2], back[2]};
    float headTurn[9], headInverse[9];
    QuatToMatrix(head.orientation, headTurn);
    Transpose3(headTurn, headInverse);
    float toArms[9];  // the recentered space's directions -> the arms' space
    Mul3(view, headInverse, toArms);
    auto position = [&](const XrVector3f& p, float* out) {
        float d[3] = {p.x - head.position.x, p.y - head.position.y, p.z - head.position.z};
        for (int i = 0; i < 3; ++i)
            out[i] = eye[i] + toArms[i * 3] * d[0] + toArms[i * 3 + 1] * d[1] + toArms[i * 3 + 2] * d[2];
    };
    auto orientation = [&](const XrQuaternionf& q, float* out) {
        float m[9];
        QuatToMatrix(q, m);
        Mul3(toArms, m, out);
    };

    skin::HandTargets t;
    XrPosef aim, grip;
    if (input::AimPose(aim) && input::HandPose(1, grip)) {
        // The gun model's axes are x left, y up, z forward (its muzzle; the first try took z for
        // back and the gun pointed at the player), the aim pose's x right, y up, z back: half a
        // turn about y between them.
        const float flip[9] = {-1, 0, 0, 0, 1, 0, 0, 0, -1};
        float turn[9], gunTurn[9];
        orientation(aim.orientation, turn);
        Mul3(turn, flip, gunTurn);
        position(grip.position, t.rightWrist);
        // Where the game's own right hand holds the gun, unless grip.txt says otherwise.
        float anchor[3];
        if (g_gunGripTuned || !skin::GunGrip(anchor))
            std::memcpy(anchor, g_gunGrip, sizeof(anchor));
        PlaceAt(gunTurn, anchor, t.rightWrist, t.gunPose);
        t.gun = true;
    }
    if (input::HandPose(0, grip)) {
        // The glove's bind axes onto the grip pose's: x (outward) -> +Y, y (back of the hand) -> +X,
        // z (fingers) -> -Z. Then the tuning turn.
        const float glove[9] = {0, 1, 0, 1, 0, 0, 0, 0, -1};
        float tuned[9], local[9], turn[9];
        Euler(g_leftTurn, tuned);
        Mul3(tuned, glove, local);
        orientation(grip.orientation, turn);
        float full[9];
        Mul3(turn, local, full);
        position(grip.position, t.leftWrist);
        PlaceAt(full, g_leftAnchor, t.leftWrist, t.leftPose);
        t.left = true;
    }
    for (int i = 0; i < 3; ++i)
        t.origin[i] = eyeWorld[i] - eye[i];
    t.floor = eye[1] - kEyeHeight;
    skin::SetHands(t.gun || t.left ? &t : nullptr);
}

void Toggle() {
    g_on = !g_on;
    if (!g_on)
        skin::SetHands(nullptr);
    Log("Hands: %s", g_on ? "free (the gun in the right hand, the left hand on its own)" : "the game's own");
    Beep(g_on ? 1200 : 440, 200);
}

}
