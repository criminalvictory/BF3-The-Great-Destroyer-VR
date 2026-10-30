#include "reload.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "ammo.h"
#include "log.h"
#include "skin.h"

namespace {

// Where on the gun the left hand works it, in the gun's model space (x left, y up, z towards the
// muzzle; metres), from the mesh dump as the game poses it: the charging handle is bone 0 (the user
// saw it wiggle), its T at the back of the receiver's top (z 0.26, y 0.06); the magazine well is at
// z 0.38-0.47, and the bolt release sits on the left side just behind it.
constexpr float kHandle[3] = {0.0f, 0.08f, 0.27f};
constexpr float kHandleReach = 0.09f;    // the palm within this of the handle grabs it
constexpr float kBoltRelease[3] = {0.03f, 0.01f, 0.37f};
constexpr float kSlapReach = 0.10f;
constexpr float kSlapSpeed = 0.7f;       // m/s: slower is a touch, not a slap
constexpr float kPullTravel = 0.07f;     // as far back as the handle goes
constexpr float kPullToCharge = 0.045f;  // pulled at least this far and let go: a round chambered
constexpr float kBoltTravel = 0.025f;    // the bolt held back, as the ejection port shows it
constexpr double kSpringHome = 0.06;     // s: the handle let go, back home

bool g_boltBack = false;
bool g_grabbed = false;
float g_grabZ = 0.0f, g_pull = 0.0f, g_maxPull = 0.0f, g_lastSqueeze = 0.0f;
double g_releasedAt = -1.0;
float g_lastPalm[3] = {};
bool g_havePalm = false;
double g_lastTime = 0.0;
int g_lastLoaded = -1;
std::atomic<float> g_handleBack{0.0f}, g_boltOffset{0.0f};  // what DrawGun draws

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

void Chamber(const char* how) {
    if (!g_boltBack)
        return;
    // With the magazine out or empty there's nothing to chamber: it stays back for a full one (the
    // one round the gun keeps never fires, so one round left is empty).
    int loaded = 0, spare = 0;
    if (skin::MagazineOut() || !ammo::Get(loaded, spare) || loaded <= 1) {
        Log("Reload: %s with no rounds to chamber", how);
        return;
    }
    g_boltBack = false;
    Log("Reload: %s: a round is chambered", how);
}

}

namespace reload {

void Update(const float* gunPose, const float* leftPalm, float squeeze) {
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

    float palm[3] = {};
    const bool have = gunPose && leftPalm;
    if (have)
        ToGun(gunPose, leftPalm, palm);

    // The charging handle: the grip closing near it grabs it (not a hand coming along closed: the
    // hand on the handguard slides past it), back with the hand, let go.
    const bool closing = squeeze > 0.6f && g_lastSqueeze <= 0.6f;
    if (have && !g_grabbed && closing && Distance(palm, kHandle) < kHandleReach) {
        g_grabbed = true;
        g_grabZ = palm[2];
        g_pull = g_maxPull = 0.0f;
        g_releasedAt = -1.0;
        Log("Reload: charging handle grabbed");
    }
    if (g_grabbed) {
        if (!have || squeeze < 0.4f) {
            g_grabbed = false;
            g_releasedAt = now;
            if (g_maxPull >= kPullToCharge)
                Chamber("charging handle pulled");
            Log("Reload: charging handle let go, pulled %.0f mm", g_maxPull * 1000.0f);
        } else {
            g_pull = std::clamp(g_grabZ - palm[2], 0.0f, kPullTravel);
            g_maxPull = std::max(g_maxPull, g_pull);
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
    // The bolt rides back with the handle, and stays back while locked.
    g_boltOffset = std::min(std::max(g_boltBack ? kBoltTravel : 0.0f, handle), 0.04f);

    // The bolt release: a quick slap at the left side of the receiver, behind the magazine well.
    if (have && g_havePalm && dt > 0.0f && !g_grabbed && g_boltBack) {
        const float speed = Distance(palm, g_lastPalm) / dt;
        if (speed > kSlapSpeed && Distance(palm, kBoltRelease) < kSlapReach)
            Chamber("bolt release slapped");
    }
    if (have)
        std::memcpy(g_lastPalm, palm, sizeof(palm));
    g_havePalm = have;
    g_lastSqueeze = squeeze;
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

float BoltOffset() {
    return g_boltOffset.load(std::memory_order_relaxed);
}

}
