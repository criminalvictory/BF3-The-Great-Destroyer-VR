#pragma once
// The view cameras' turn to the head (camera.cpp: TurnToHead for the eyes' cameras views.cpp builds, the
// breakpoints for the weapon camera), worked out on its own so tools/viewturntest.cpp can run it without
// the game.
//
// A camera is a Frostbite LinearTransform: the rows right, up, back and translation, four floats each, the
// fourth (w) lane of each unused. The player's own camera has those lanes 0, and IsCamera wants that. A
// scripted camera has them filled: a cutscene's or a ride's (the opening drive of Comrades, the interrogation's
// fight in The Great Destroyer, where the camera watch drops the weapon camera for the same reason). Refused,
// such a picture was held still in the room, and turning the head showed its black edges (the user,
// 2026-11-01). Its w lanes are leftovers of its rotation: in every refused camera logged with its up and
// forward (152: the drive, the interrogation), they're its quaternion's products 2(yw - xy), -2(xy + yw) and
// ww + xx - yy - zz to the log's rounding, as a quaternion made into a matrix four lanes at a time leaves
// them. They tell nothing the rows don't, and the game draws those cameras right with them filled (shifted
// for each eye along the right row too), so it draws from the rows' x, y and z. A scripted camera is turned
// by rewriting those, the w lanes left as they are.
//
// The player's camera is the body's yaw, the hand's yaw (the look angles follow the hand) and the game's
// pitch: ViewTurnLocal takes the hand's yaw and the pitch out and puts the head in. A scripted camera has no
// hand in it, and rolls as well as pitches (the car over bumps and through bends, the interrogation's camera
// on its side or upside down): ViewTurnLocal would leave its roll in, and take a hand's yaw out that isn't
// there. It's made level about its own yaw instead (MakeLevel), the head put on that: the game's camera is
// the body, the head stays the user's, the horizon level (the user's choice for cutscenes, 2026-10-30).

#include <algorithm>
#include <cmath>
#include <cstring>

#include "camera.h"

namespace viewturn {

constexpr float kPi = 3.14159265f;
// A scripted camera's w lanes: as large as its rotation's leftovers can be (1.41), with room to spare.
constexpr float kMostW = 10.0f;
// Its rows at right angles to within this (the dot of two of them; 0.05 is about 3 degrees).
constexpr float kSquare = 0.05f;
// Its back row this short along the ground (its forward within about 6 degrees of straight up or down), it
// doesn't tell the yaw: the last one is kept (MakeLevel).
constexpr float kVertical = 0.1f;

// A LinearTransform's three direction rows are unit length with a zero fourth component;
// arbitrary data almost never is.
inline bool IsCamera(const float* m) {
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

// A camera but for its w lanes (a scripted one's): the rows unit length as IsCamera wants them and at right
// angles, the w lanes and the translation finite, the w lanes no larger than leftovers of the rotation.
inline bool IsScriptedCamera(const float* m) {
    for (int row = 0; row < 3; ++row) {
        const float* v = m + 4 * row;
        const float len = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        if (!(std::fabs(len - 1.0f) < 0.05f) || !(std::fabs(v[3]) < kMostW))  // also rejects NaN and infinity
            return false;
    }
    if (!std::isfinite(m[12]) || !std::isfinite(m[13]) || !std::isfinite(m[14]))
        return false;
    auto dot = [m](int a, int b) {
        return m[4 * a] * m[4 * b] + m[4 * a + 1] * m[4 * b + 1] + m[4 * a + 2] * m[4 * b + 2];
    };
    return std::fabs(dot(0, 1)) < kSquare && std::fabs(dot(0, 2)) < kSquare && std::fabs(dot(1, 2)) < kSquare;
}

enum Kind { kNone, kOrdinary, kScripted };

// What TurnToHead makes of a view camera: the player's kind (IsCamera, as it always was), a scripted one
// (scripted: weapon.txt "cutsceneturn", on by default), or nothing it turns.
inline Kind Classify(const float* m, bool scripted) {
    if (IsCamera(m))
        return kOrdinary;
    return scripted && IsScriptedCamera(m) ? kScripted : kNone;
}

// Quaternions are (x, y, z, w), in OpenXR's axes: x right, y up, -z forward.
inline void QuatMul(const float* a, const float* b, float* out) {
    out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

inline void QuatRotate(const float* q, const float* v, float* out) {
    float t[3] = {2 * (q[1] * v[2] - q[2] * v[1]), 2 * (q[2] * v[0] - q[0] * v[2]), 2 * (q[0] * v[1] - q[1] * v[0])};
    out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

// BF3's camera rows are right (row0), up (row1) and backwards (row2): the true view direction
// is -row2. That's a left-handed basis, a mirror image of OpenXR's right-handed axes; mapping
// through it keeps turning, up/down and tilt all the right way round (confirmed in game: with
// row0 taken as left, both turning and tilt came out mirrored).

// The turn, in the camera's own frame, from where the hand aims to where the head looks.
// The game camera is body yaw * controller yaw * game pitch (the look angles follow the
// controller), so removing the controller yaw and the game's own pitch and applying the head
// leaves body yaw * head. taken: the yaw it removed (the picture shows the camera's + taken - head).
inline void ViewTurnLocal(const float* m, const camera::ViewTurn& turn, float* local, float* taken = nullptr) {
    float pitch = std::asin(std::clamp(-m[9], -1.0f, 1.0f));  // true forward = -row2
    float yaw = turn.controllerYaw;
    if (turn.haveLook) {
        // The look yaw this camera was built from, by BF3's own formula (the angle finder found
        // atan2(x, -z) of row 2), against the one written this frame: the difference is how far
        // the hand has turned since, so the hand's turn in this camera needs no lag guess. A
        // fixed frame lag (view_lag) was a frame too short or too long at a lower frame rate:
        // the picture swayed against or with the hand.
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

// Rotates camera m by local, a quaternion in its own (OpenXR-style) frame.
inline void TurnLocal(float* m, const float* local) {
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

// A scripted camera's yaw as last made level, kept for a forward too near the vertical to tell it by.
struct Level {
    bool have = false;
    float back[2] = {};  // the level back row's x and z
};

// Rows 0-2 made level about the camera's own yaw: the back row its own along the ground, the up row the
// world's, the right row across them the way round hand says (+1 the player's camera's way, -1 mirrored: a
// mirrored camera stays mirrored). Its forward too near straight up or down (kVertical), the last yaw made level
// is kept; with none, it faces along the ground the way the picture's top points (looking down; the other way
// looking up). The w lanes and the translation are left as they are.
inline void MakeLevel(float* m, int hand, Level& level) {
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

// A view camera turned to the head as TurnToHead turns it, once it has passed the checks: the player's kind by
// ViewTurnLocal, the yaw it took out returned; a scripted one made level about its own yaw (hand: its rows'
// handedness), then the head alone put on it, none taken out. mirrored: its rows mirrored from the player's
// camera's (camera.cpp tells), so the head's turn is mirrored the same way across its right row: yaw and roll
// the other way, pitch as it is.
inline float Turn(float* m, const camera::ViewTurn& turn, bool scripted, int hand, bool mirrored, Level& level) {
    float local[4], taken = 0.0f;
    if (scripted) {
        MakeLevel(m, hand, level);
        std::memcpy(local, turn.head, sizeof(local));
    } else {
        ViewTurnLocal(m, turn, local, &taken);
    }
    if (mirrored) {
        local[1] = -local[1];
        local[2] = -local[2];
    }
    TurnLocal(m, local);
    return taken;
}

// For the logs: a camera's pitch (up positive) and roll (radians), the roll as for the player's camera's rows
// (times its handedness for a mirrored one).
inline void PitchRoll(const float* m, float& pitch, float& roll) {
    pitch = std::asin(std::clamp(-m[9], -1.0f, 1.0f));
    roll = std::atan2(m[1], m[5]);
}

}
