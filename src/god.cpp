#include "god.h"

#include <windows.h>

#include <cmath>

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

}

namespace god {

void Apply() {
    // While the health finder compares values, health must be left as the game sets it.
    if (finder::HealthFinderActive())
        return;
    config::God g = config::GetGod();
    if (!g.enabled)
        return;
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

void Toggle() {
    config::God g = config::GetGod();
    g.enabled = !g.enabled;
    config::SetGod(g);
    Log("God mode %s%s", g.enabled ? "on" : "off",
        g.health.empty() ? " (health not found yet: Home at full health, after damage, after refill)" : "");
    Beep(g.enabled ? 1200 : 440, 200);
}

}
