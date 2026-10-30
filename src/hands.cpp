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
    XrPosef aim, grip;
    if (input::AimPose(aim) && input::HandPose(1, grip)) {
        // The gun model's axes are x left, y up, z forward (its muzzle; the first try took z for
        // back and the gun pointed at the player), the aim pose's x right, y up, z back: half a
        // turn about y between them.
        const float flip[9] = {-1, 0, 0, 0, 1, 0, 0, 0, -1};
        float aimTurn[9], turn[9], gunTurn[9];
        QuatToMatrix(aim.orientation, aimTurn);
        Mul3(toView, aimTurn, turn);
        Mul3(turn, flip, gunTurn);
        toViewPoint(grip.position, t.rightWrist);
        // Where the game's own right hand holds the gun, unless grip.txt says otherwise.
        float anchor[3];
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
        handpose::Fingers fingers;
        fingers.curl[0] = g_index;
        fingers.curl[1] = fingers.curl[2] = fingers.curl[3] = g_grip;
        // Folded part way on its own; with the grip held, down onto the curled middle finger (also
        // when the index finger points: hovering over it looked wrong, user 2026-10-30).
        fingers.thumb = g_thumb * (0.6f + 0.4f * g_grip);
        fingers.spread = g_spread;
        handpose::Pose(fingers, g_leftWidth, t.leftFingers);
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
    // The bolt and its manual releases (reload.cpp): the gun and the left palm, both in the view's space.
    reload::Update(t.gun ? t.gunPose : nullptr, t.left ? t.leftWrist : nullptr, input::LeftSqueeze());
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
