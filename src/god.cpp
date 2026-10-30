#include "god.h"

#include <windows.h>

#include <cmath>
#include <mutex>
#include <vector>

#include "camera.h"
#include "config.h"
#include "finder.h"
#include "log.h"

namespace {

bool SafeWrite(uintptr_t address, float value) {
    __try {
        *reinterpret_cast<float*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Saved pointer chains to the health kept breaking (the game reuses that memory), so the health
// found this session is pinned by address instead, as long as its copies still agree.
std::mutex g_sessionMutex;
std::vector<uintptr_t> g_session;
bool g_sessionLost = false;

void ApplySession(float full) {
    std::lock_guard lock(g_sessionMutex);
    if (g_session.empty() || g_sessionLost)
        return;
    // The value most of them show; under 100 after a hit.
    std::vector<float> values(g_session.size(), -1.0f);
    for (size_t i = 0; i < g_session.size(); ++i)
        camera::SafeRead(g_session[i], &values[i], sizeof(float));
    float majority = -1.0f;
    size_t best = 0;
    for (float v : values) {
        size_t same = 0;
        for (float w : values)
            same += std::fabs(v - w) < 0.01f;
        if (same > best)
            best = same, majority = v;
    }
    if (best * 2 < g_session.size()) {
        // They no longer show one value: not health any more (reused memory). Stop writing.
        g_sessionLost = true;
        Log("God mode: the health found this session no longer agrees with itself; stopped (F16 finds it again)");
        return;
    }
    if (!(majority > 0.0f && majority < full))
        return;
    for (size_t i = 0; i < g_session.size(); ++i)
        if (std::fabs(values[i] - majority) < 0.01f)
            SafeWrite(g_session[i], full);
}

}

namespace god {

void Apply() {
    // While the health finder compares values, health must be left as the game sets it.
    if (finder::HealthFinderActive())
        return;
    config::God g = config::GetGod();
    if (!g.enabled)
        return;
    ApplySession(g.value);
    for (const config::CameraPointer& p : g.health) {
        uintptr_t address = 0;
        float health = 0.0f;
        if (!camera::Resolve(p, address) || !camera::SafeRead(address, &health, sizeof(health)))
            continue;
        // Only top up a living player: at 0 the game has already moved on to dying.
        if (std::isfinite(health) && health > 0.0f && health < g.value)
            SafeWrite(address, g.value);
    }
}

void SetSessionHealth(const std::vector<uintptr_t>& addresses) {
    std::lock_guard lock(g_sessionMutex);
    g_session = addresses;
    g_sessionLost = false;
    Log("God mode: pinning %zu health values found this session", addresses.size());
}

void Toggle() {
    config::God g = config::GetGod();
    g.enabled = !g.enabled;
    config::SetGod(g);
    Log("God mode %s%s", g.enabled ? "on" : "off",
        g.health.empty() ? " (health not found yet: Home at full health, after damage, after refill)" : "");
    Beep(g.enabled ? 1200 : 440, 200);
}

}
