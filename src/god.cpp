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

uintptr_t ReadPointer(uintptr_t address) {
    uint32_t value = 0;
    return camera::SafeRead(address, &value, sizeof(value)) ? value : 0;
}

// The campaign runs the game's own server inside bf3.exe, and the health that decides whether the
// player dies is the server's: offset 0x20 of its copy of the soldier. The client's copy holds
// the same number at the same offset, draws the screen by it (blood at the edges when it's low)
// and only catches up when the server sends a hit, so both are written. Both copies point at the
// soldier's data, whose maximum health the game measures the hurt look against: raising it keeps
// the screen clean and makes even a big blast in one frame survivable.
constexpr uintptr_t kClientGameContext = 0x1F80B58;  // offsets in bf3.exe
constexpr uintptr_t kServerEntities = 0x1EB8EE4;     // a table of the server's entities
constexpr size_t kServerEntitySlots = 256;
constexpr uintptr_t kServerSoldierType = 0x1CE1268;  // vtables, bf3.exe offsets
constexpr uintptr_t kSoldierDataType = 0x1DB0CC8;
constexpr uintptr_t kSoldierData = 0x14;
constexpr uintptr_t kHealth = 0x20;
constexpr uintptr_t kMaxHealth = 0xEC;  // in the soldier's data

uintptr_t g_serverSoldier = 0;
uintptr_t g_lastClientSoldier = 0;
// The data whose maximum was raised, and what it was.
uintptr_t g_raisedData = 0;
float g_originalMax = 0.0f;

uintptr_t ClientSoldier(uintptr_t base) {
    uintptr_t context = ReadPointer(base + kClientGameContext);
    uintptr_t players = context ? ReadPointer(context + 0x30) : 0;
    uintptr_t player = players ? ReadPointer(players + 0xBC) : 0;
    return player ? ReadPointer(player + 0x3D8) : 0;
}

bool IsServerSoldier(uintptr_t base, uintptr_t entity, uintptr_t data) {
    return entity && ReadPointer(entity) == base + kServerSoldierType && ReadPointer(entity + kSoldierData) == data;
}

// The player's soldier among the server's: the one built from the same data as the client's.
uintptr_t ServerSoldier(uintptr_t base, uintptr_t data) {
    if (IsServerSoldier(base, g_serverSoldier, data))
        return g_serverSoldier;
    g_serverSoldier = 0;
    uint32_t slots[kServerEntitySlots] = {};
    if (!camera::SafeRead(base + kServerEntities, slots, sizeof(slots)))
        return 0;
    for (size_t i = 0; i < kServerEntitySlots; ++i) {
        if (IsServerSoldier(base, slots[i], data)) {
            g_serverSoldier = slots[i];
            Log("God mode: the server's soldier is at %08X (table slot %zu)", slots[i], i);
            return slots[i];
        }
    }
    return 0;
}

void TopUp(uintptr_t address, float full) {
    float health = 0.0f;
    // At 0 the game has already moved on to dying.
    if (camera::SafeRead(address, &health, sizeof(health)) && std::isfinite(health) && health > 0.0f &&
        health < full)
        SafeWrite(address, full);
}

void ApplyServer(float full) {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const uintptr_t client = ClientSoldier(base);
    const uintptr_t data = client ? ReadPointer(client + kSoldierData) : 0;
    if (!data || ReadPointer(data) != base + kSoldierDataType)
        return;
    float max = 0.0f;
    if (camera::SafeRead(data + kMaxHealth, &max, sizeof(max)) && max != full && max > 0.0f && max < 1e7f) {
        if (data != g_raisedData) {
            g_raisedData = data;
            g_originalMax = max;
        }
        SafeWrite(data + kMaxHealth, full);
    }
    const uintptr_t server = ServerSoldier(base, data);
    if (client != g_lastClientSoldier) {
        g_lastClientSoldier = client;
        if (!server)
            Log("God mode: the server's soldier isn't in its table; only the maximum is raised");
    }
    TopUp(client + kHealth, full);
    if (server)
        TopUp(server + kHealth, full);
}

// God mode off: the maximum goes back, and health above it with it.
void RestoreServer() {
    if (!g_raisedData)
        return;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (ReadPointer(g_raisedData) == base + kSoldierDataType)
        SafeWrite(g_raisedData + kMaxHealth, g_originalMax);
    const uintptr_t client = ClientSoldier(base);
    if (client && ReadPointer(client + kSoldierData) == g_raisedData) {
        SafeWrite(client + kHealth, g_originalMax);
        if (uintptr_t server = ServerSoldier(base, g_raisedData))
            SafeWrite(server + kHealth, g_originalMax);
    }
    g_raisedData = 0;
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
    if (!g.enabled) {
        RestoreServer();
        return;
    }
    ApplyServer(g.value);
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
    Log("God mode %s (health kept at %.0f)", g.enabled ? "on" : "off", g.value);
    Beep(g.enabled ? 1200 : 440, 200);
}

}
