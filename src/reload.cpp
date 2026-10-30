#include "reload.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "ammo.h"
#include "effects.h"
#include "log.h"
#include "skin.h"
#include "sounds.h"

namespace {

// Where on the gun the left hand works it, in the gun's model space (x left, y up, z towards the
// muzzle; metres), from the mesh dump as the game poses it: the charging handle is bone 0 (the user
// saw it wiggle), its T at the back of the receiver's top (z 0.26, y 0.06); the magazine well is at
// z 0.38-0.47, and the bolt release sits on the left side just behind it.
constexpr float kHandle[3] = {0.0f, 0.08f, 0.27f};
// Where the palm point (where the arm folds into the glove) is with the glove's fingers on the
// handle: 13 cm behind it and 6 cm above (the user's twelve tries, 2026-10-30, all within 7 cm).
constexpr float kHandleGrip[3] = {0.035f, 0.14f, 0.135f};
constexpr float kHandleReach = 0.09f;    // the palm within this of the grip point grabs the handle
constexpr float kBoltRelease[3] = {0.03f, 0.01f, 0.37f};
constexpr float kSlapReach = 0.10f;
constexpr float kSlapSpeed = 0.7f;       // m/s: slower is a touch, not a slap
constexpr float kPullTravel = 0.07f;     // as far back as the handle goes
constexpr float kPullToCharge = 0.045f;  // pulled at least this far and let go: a round chambered
constexpr double kSpringHome = 0.06;     // s: the handle let go, back home
// The ejection port's cover springs open on its hinge when the bolt moves and swings shut once it
// has been still a moment (the user: it opens and shuts, it doesn't slide).
constexpr float kCoverOpen = 2.8f;       // radians (160 degrees): out and down against the receiver
constexpr double kCoverSwing = 0.05;     // s to spring open
constexpr double kCoverHold = 0.6;       // s open after the bolt last moved
constexpr double kCoverShut = 0.2;       // s to swing shut

bool g_boltBack = false;
bool g_grabbed = false;
float g_grabZ = 0.0f, g_pull = 0.0f, g_maxPull = 0.0f, g_lastTrigger = 0.0f;
double g_releasedAt = -1.0;
float g_lastPalm[3] = {};
bool g_havePalm = false;
double g_lastTime = 0.0;
int g_lastLoaded = -1;
float g_cover = 0.0f;  // 0 shut, 1 open
double g_boltMovedAt = -1.0;
std::atomic<float> g_handleBack{0.0f}, g_coverAngle{0.0f};  // what DrawGun draws
std::atomic<float> g_pan{0.3f};  // where the gun is, left (-1) to right (1), for its sounds
bool g_chargeSounded = false;  // this pull of the handle has made its sound

double Now() {
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / static_cast<double>(frequency.QuadPart);
}

// A point in the view's space into the gun's model space (the pose's rows: rotation | translation).
void ToGun(const float* pose, const float* p, float* out) {
    const float d[3] = {p[0] - pose[3], p[1] - pose[7], p[2] - pose[11]};
    for (int i = 0; i < 3; ++i)
        out[i] = pose[i] * d[0] + pose[4 + i] * d[1] + pose[8 + i] * d[2];
}

float Distance(const float* a, const float* b) {
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

// True when a round went in and the bolt is home.
bool Chamber(const char* how) {
    if (!g_boltBack)
        return false;
    // With the magazine out or empty there's nothing to chamber: it stays back for a full one (the
    // one round the gun keeps never fires, so one round left is empty).
    int loaded = 0, spare = 0;
    if (skin::MagazineOut() || !ammo::Get(loaded, spare) || loaded <= 1) {
        Log("Reload: %s with no rounds to chamber", how);
        return false;
    }
    g_boltBack = false;
    Log("Reload: %s: a round is chambered", how);
    return true;
}

}

namespace reload {

void Update(const float* gunPose, const float* leftPalm, float trigger) {
    const double now = Now();
    const float dt = g_lastTime > 0.0 ? static_cast<float>(now - g_lastTime) : 0.0f;
    g_lastTime = now;

    // Run dry: the last round fired, the bolt locks back. (The gun keeps one round the game never
    // fires, or it would reload by itself: one round left is empty.)
    int loaded = 0, spare = 0;
    if (skin::RifleInHand() && ammo::Get(loaded, spare)) {
        if (g_lastLoaded > 1 && loaded <= 1 && !skin::MagazineOut() && !g_boltBack) {
            g_boltBack = true;
            Log("Reload: the magazine ran dry; the bolt is locked back");
        }
        g_lastLoaded = loaded;
    }

    if (gunPose)
        g_pan = std::clamp(gunPose[3] / 0.35f, -1.0f, 1.0f);  // the view's x is to the right

    float palm[3] = {};
    const bool have = gunPose && leftPalm;
    if (have)
        ToGun(gunPose, leftPalm, palm);

    // The charging handle: the left trigger pressed near it grabs it (the user's choice, 2026-10-30;
    // not a trigger already held as the hand comes along), back with the hand, the trigger let go.
    const bool closing = trigger > 0.6f && g_lastTrigger <= 0.6f;
    if (closing && have)
        Log("Reload: left trigger pressed, the palm %.0f mm from the charging handle's grip point (%.3f %.3f %.3f "
            "on the gun)",
            Distance(palm, kHandleGrip) * 1000.0f, palm[0], palm[1], palm[2]);
    if (have && !g_grabbed && closing && Distance(palm, kHandleGrip) < kHandleReach) {
        g_grabbed = true;
        g_grabZ = palm[2];
        g_pull = g_maxPull = 0.0f;
        g_releasedAt = -1.0;
        g_chargeSounded = false;
        Log("Reload: charging handle grabbed");
    }
    if (g_grabbed) {
        if (!have || trigger < 0.4f) {
            g_grabbed = false;
            g_releasedAt = now;
            if (g_maxPull >= kPullToCharge) {
                Chamber("charging handle pulled");
                if (!g_boltBack)  // the bolt goes home (empty, it stays caught back)
                    sounds::Play("bolt", g_pan);
            }
            Log("Reload: charging handle let go, pulled %.0f mm", g_maxPull * 1000.0f);
        } else {
            g_pull = std::clamp(g_grabZ - palm[2], 0.0f, kPullTravel);
            g_maxPull = std::max(g_maxPull, g_pull);
            if (g_pull > 0.02f && !g_chargeSounded) {
                g_chargeSounded = true;
                sounds::Play("charge", g_pan);
            }
        }
    }
    float handle = g_grabbed ? g_pull : 0.0f;
    if (!g_grabbed && g_releasedAt >= 0.0) {
        const double t = (now - g_releasedAt) / kSpringHome;
        if (t < 1.0)
            handle = g_pull * static_cast<float>(1.0 - t);
        else
            g_releasedAt = -1.0, g_pull = 0.0f;
    }
    g_handleBack = handle;

    // The cover: open while the bolt moves (a shot, locked back, the handle drawn), then shut.
    if (effects::MsSinceShot() < 100 || g_boltBack || handle > 0.01f)
        g_boltMovedAt = now;
    const bool open = g_boltMovedAt >= 0.0 && now - g_boltMovedAt < kCoverHold;
    g_cover = open ? std::min(g_cover + dt / static_cast<float>(kCoverSwing), 1.0f)
                   : std::max(g_cover - dt / static_cast<float>(kCoverShut), 0.0f);
    g_coverAngle = kCoverOpen * g_cover * (2.0f - g_cover);  // fast at first, easing into place

    // The bolt release: a quick slap at the left side of the receiver, behind the magazine well.
    if (have && g_havePalm && dt > 0.0f && !g_grabbed && g_boltBack) {
        const float speed = Distance(palm, g_lastPalm) / dt;
        if (speed > kSlapSpeed && Distance(palm, kBoltRelease) < kSlapReach && Chamber("bolt release slapped"))
            sounds::Play("bolt", g_pan);
    }
    if (have)
        std::memcpy(g_lastPalm, palm, sizeof(palm));
    g_havePalm = have;
    g_lastTrigger = trigger;
}

bool BoltBack() {
    return g_boltBack;
}

void ReleaseBolt() {
    if (g_boltBack)
        Log("Reload: the game's own reload chambers a round");
    g_boltBack = false;
}

float HandleBack() {
    return g_handleBack.load(std::memory_order_relaxed);
}

float CoverAngle() {
    return g_coverAngle.load(std::memory_order_relaxed);
}

float GunPan() {
    return g_pan.load(std::memory_order_relaxed);
}

}
