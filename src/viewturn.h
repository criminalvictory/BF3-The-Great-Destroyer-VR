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
// there. It's made level about its yaw instead (MakeLevel: its own where that's sure, else going on from the
// last frame's by its turn about the world's up), the head put on that: the game's camera is the body, the head
// stays the user's, the horizon level (the user's choice for cutscenes, 2026-10-30).
//
// The player's own body (its first-person arms, gun and legs) is drawn with a camera of its own, the view's
// first-person one (views.cpp), posed where the game put it for that camera. In ordinary play the mod places the gun
// and arms itself, in that camera's own space, and leaves the camera as it is. In a scripted picture the game draws
// them, and with the main camera alone turned they were drawn as if the head hadn't moved, glued to the view (the
// user, 2026-11-01, in the drive of Comrades and the interrogation: "I can see like my gun and my legs kind of moving
// wherever I turn my head ... Only the head moves"). So a scripted picture's first-person camera is turned with its
// main camera, by the same turn, about its own place (TurnWith): the turn as the main camera's own frame has it, put
// on the first-person camera in its own, so that what either showed at a place in the picture moves across it alike.
// The body is drawn where the scene puts it, seen from where the head looks, even with the first-person camera's rows
// turned from the main one's.

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
// doesn't tell the camera's own yaw: the way the picture's top points does (OwnYaw).
constexpr float kVertical = 0.1f;
// Its own yaw's share of the yaw it's made level about (MakeLevel), by its back row's length along the ground: all
// of it up to 45 degrees of pitch (a turn of the camera swings its own yaw at most 1.41 times as far there: the
// drive of Comrades, steepest 45 down), none past 60 (twice as far), smooth in between.
constexpr float kOwnWhole = 0.7071f;
constexpr float kOwnNone = 0.5f;
// Its rows' dots with the last frame's summed (the turn between the two's trace, 1 + 2 cos of its angle) below this,
// it turned more than 30 degrees in a frame: a cut to another shot, which takes its own yaw (MakeLevel).
constexpr float kCut = 2.732f;

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

// A scripted camera's yaw as made level, which the next frame's goes on from (MakeLevel), and the rows it was made
// level from, as the game made them, to tell how far the camera has turned since.
struct Level {
    bool have = false;
    float back[2] = {};  // the level back row's x and z
    float rows[9] = {};  // the right, up and back rows' x, y and z
};

// A camera's own yaw, from it alone, as the level back row's x and z: its back row's own along the ground. Its
// forward within about 6 degrees of straight up or down (kVertical), the way the picture's top points (looking
// down; the other way looking up).
inline void OwnYaw(const float* m, float& x, float& z) {
    x = m[8], z = m[10];
    if (x * x + z * z < kVertical * kVertical) {
        const float away = m[9] > 0.0f ? -1.0f : 1.0f;  // the back row up: looking down
        x = away * m[4], z = away * m[6];
    }
    const float length = std::sqrt(x * x + z * z);
    if (length > 1e-6f) {
        x /= length, z /= length;
    } else {
        x = 0.0f, z = 1.0f;  // never for a camera Classify passes (its up row then lies along the ground)
    }
}

// Rows 0-2 made level about the camera's yaw: the back row along the ground that way, the up row the world's, the
// right row across them the way round hand says (+1 the player's camera's way, -1 mirrored: a mirrored camera stays
// mirrored). The w lanes and the translation are left as they are.
//
// The yaw is the camera's own (OwnYaw) where that's sure: up to 45 degrees of pitch (kOwnWhole), all the drive of
// Comrades. Steeper it isn't. Taken from the back row along the ground alone, a turn of the camera swung the yaw
// 1/cos(pitch) times as far: 5.7 times looking 80 degrees down, 9.5 at 84, and for the interrogation's camera of
// 23:57:12 (80 down, on its side) 5.8, so the room swung several times faster than the camera shook (the review,
// 2026-11-01); a rolled camera leaving the 6-degree cone, inside which the picture's top or the last yaw had been
// kept, jumped by as much as it had rolled from there, up to 180. So steeper, the yaw goes on from the last frame's
// by the camera's turn about the world's up since (its rows' turns along the ground), which no pitching or rolling
// about a level line moves, and only a share of the way to its own (none past 60 degrees, kOwnNone). A 1-degree
// turn of the camera turns it at most 1.44 degrees at once and 1.87 within a second, as the share takes it the rest
// of the way (tools/viewturntest.cpp). A run's first camera (have false), and one that turned more than 30 degrees
// since the last frame's (a cut, kCut), take their own. Each camera of a frame must go on from the same last frame
// (camera.cpp latches it), or the eyes and their cull cameras would each be taken a share further.
inline void MakeLevel(float* m, int hand, Level& level) {
    // The turn since the last frame's camera: about the world's up, the dots and crosses of the rows' parts along
    // the ground summed (that turn's cos and sin, each times the same); its trace, the rows' dots summed.
    float along = 0.0f, across = 0.0f, trace = 0.0f;
    for (int row = 0; row < 3; ++row) {
        const float* now = m + 4 * row;
        const float* last = level.rows + 3 * row;
        along += now[0] * last[0] + now[2] * last[2];
        across += now[0] * last[2] - now[2] * last[0];
        trace += now[0] * last[0] + now[1] * last[1] + now[2] * last[2];
    }
    const float ground = std::sqrt(m[8] * m[8] + m[10] * m[10]);  // the back row's length along the ground: cos pitch
    const float t = std::clamp((ground - kOwnNone) / (kOwnWhole - kOwnNone), 0.0f, 1.0f);
    const float share = t * t * (3.0f - 2.0f * t);
    float x = 0.0f, z = 0.0f;
    if (level.have && trace > kCut && share < 1.0f) {
        const float step = std::sqrt(along * along + across * across);  // about 2: the turn is under 30 degrees
        const float c = along / step, s = across / step;
        x = level.back[0] * c + level.back[1] * s;
        z = level.back[1] * c - level.back[0] * s;
        if (share > 0.0f) {
            // The share of the way to its own yaw, turned the same way round as the step.
            float ownX = 0.0f, ownZ = 0.0f;
            OwnYaw(m, ownX, ownZ);
            const float off = share * std::atan2(z * ownX - x * ownZ, x * ownX + z * ownZ);
            const float co = std::cos(off), so = std::sin(off);
            const float turnedX = x * co + z * so;
            z = z * co - x * so;
            x = turnedX;
        }
        const float length = std::sqrt(x * x + z * z);
        x /= length, z /= length;
    } else {
        OwnYaw(m, x, z);
    }
    for (int row = 0; row < 3; ++row)
        for (int i = 0; i < 3; ++i)
            level.rows[3 * row + i] = m[4 * row + i];
    level.have = true;
    level.back[0] = x, level.back[1] = z;
    const float h = hand < 0 ? -1.0f : 1.0f;
    m[0] = h * z, m[1] = 0.0f, m[2] = -h * x;
    m[4] = 0.0f, m[5] = 1.0f, m[6] = 0.0f;
    m[8] = x, m[9] = 0.0f, m[10] = z;
}

// m's rows turned by the turn that took a camera's rows from before to after, about m's own place: that turn as
// before's own frame has it (each of after's rows taken apart along before's rows: its dots with the columns of
// before's inverse, exact for rows a little off unit length or square), put on m in m's own frame (each of m's rows
// put together again from m's rows in the same parts). With one FOV and aspect for both (views.cpp), what m showed at
// a place in the picture then shows where after shows what before showed there: the player's body stays on the scene
// it was drawn over, however m's rows are turned from before's. m as before comes out as after; m turned from before
// about a line in the world comes out turned so from after. Put on m from the world's side instead (3cbdb33), the
// body slid off the scene about as far as the two cameras' rows were apart, or further: up to 3.9 degrees for a
// first-person camera 2 degrees pitched from the main one, 19.7 for 10 (found by the review, 2026-11-01; measured in
// tools/viewturntest.cpp 15b, the head up to 70 degrees round). Its w lanes and its translation are left as they are.
// False, m left as it is, if it isn't a camera of either kind (Classify), or if before's rows have no inverse (never
// for a camera Classify passes).
inline bool TurnWith(float* m, const float* before, const float* after) {
    if (Classify(m, true) == kNone)
        return false;
    // The columns of before's inverse, times its determinant: the crosses of its rows.
    const float* a = before;
    const float* b = before + 4;
    const float* c = before + 8;
    const float across[3][3] = {{b[1] * c[2] - b[2] * c[1], b[2] * c[0] - b[0] * c[2], b[0] * c[1] - b[1] * c[0]},
                                {c[1] * a[2] - c[2] * a[1], c[2] * a[0] - c[0] * a[2], c[0] * a[1] - c[1] * a[0]},
                                {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}};
    const float det = a[0] * across[0][0] + a[1] * across[0][1] + a[2] * across[0][2];
    if (!(std::fabs(det) > 0.5f))  // also rejects NaN
        return false;
    // The turn in before's own frame: each of after's rows in parts along before's.
    float turn[3][3];
    for (int row = 0; row < 3; ++row) {
        const float* v = after + 4 * row;
        for (int k = 0; k < 3; ++k)
            turn[row][k] = (v[0] * across[k][0] + v[1] * across[k][1] + v[2] * across[k][2]) / det;
    }
    // Put on m in its own frame: each row made of m's rows in those parts.
    float rows[9];
    for (int row = 0; row < 3; ++row)
        for (int i = 0; i < 3; ++i)
            rows[3 * row + i] = turn[row][0] * m[i] + turn[row][1] * m[4 + i] + turn[row][2] * m[8 + i];
    for (int row = 0; row < 3; ++row)
        for (int i = 0; i < 3; ++i)
            m[4 * row + i] = rows[3 * row + i];
    return true;
}

// A view camera turned to the head as TurnToHead turns it, once it has passed the checks: the player's kind by
// ViewTurnLocal, the yaw it took out returned; a scripted one made level about its yaw (MakeLevel, going on from
// level, the last frame's; hand: its rows' handedness), then the head alone put on it, none taken out. mirrored:
// its rows mirrored from the player's camera's (camera.cpp tells), so the head's turn is mirrored the same way
// across its right row: yaw and roll the other way, pitch as it is. body: the view's first-person camera, which the
// player's own body is drawn with, or nullptr. With a scripted camera it's turned by the same turn, in its own frame,
// about its own place (TurnWith), unless the look angles are written (a scripted camera's first pictures can come
// with them still written): the mod places the gun and arms itself then. With the player's kind it's always left as
// it is.
inline float Turn(float* m, const camera::ViewTurn& turn, bool scripted, int hand, bool mirrored, Level& level,
                  float* body = nullptr) {
    const bool withBody = body && scripted && !turn.haveLook;
    float before[16];
    if (withBody)
        std::memcpy(before, m, sizeof(before));
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
    if (withBody)
        TurnWith(body, before, m);
    return taken;
}

// For the logs: a camera's pitch (up positive) and roll (radians), the roll as for the player's camera's rows
// (times its handedness for a mirrored one).
inline void PitchRoll(const float* m, float& pitch, float& roll) {
    pitch = std::asin(std::clamp(-m[9], -1.0f, 1.0f));
    roll = std::atan2(m[1], m[5]);
}

}
