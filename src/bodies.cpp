#include "bodies.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <unordered_set>

#include "camera.h"
#include "config.h"
#include "log.h"

namespace {

// The campaign's own server (inside bf3.exe) lists its soldiers in a table, the one god.cpp finds
// the player in. Each soldier points at its data, which points at its health module data, which
// holds how long its body lies before it is removed (15 s for most, 30 s for the player).
constexpr uintptr_t kServerEntities = 0x1EB8EE4;  // offsets in bf3.exe
constexpr size_t kServerEntitySlots = 256;
constexpr uintptr_t kServerSoldierType = 0x1CE1268;  // vtables, bf3.exe offsets
constexpr uintptr_t kHealthModuleType = 0x1CC8A58;
constexpr uintptr_t kSoldierData = 0x14;
constexpr uintptr_t kHealthModule = 0xE8;  // in the soldier's data
constexpr uintptr_t kTimeForCorpse = 0x08;  // in the health module data

uintptr_t ReadPointer(uintptr_t address) {
    uint32_t value = 0;
    return camera::SafeRead(address, &value, sizeof(value)) ? value : 0;
}

bool SafeWrite(uintptr_t address, float value) {
    __try {
        *reinterpret_cast<float*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}

namespace bodies {

void Apply() {
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    const float seconds = config::Get().corpseSeconds;
    if (seconds <= 0.0f || now - last < 1000)
        return;
    last = now;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint32_t slots[kServerEntitySlots] = {};
    if (!camera::SafeRead(base + kServerEntities, slots, sizeof(slots)))
        return;
    std::unordered_set<uintptr_t> done;  // modules are shared by many soldiers
    for (uint32_t soldier : slots) {
        if (!soldier || ReadPointer(soldier) != base + kServerSoldierType)
            continue;
        const uintptr_t data = ReadPointer(soldier + kSoldierData);
        const uintptr_t module = data ? ReadPointer(data + kHealthModule) : 0;
        if (!module || ReadPointer(module) != base + kHealthModuleType || !done.insert(module).second)
            continue;
        float current = 0.0f;
        if (!camera::SafeRead(module + kTimeForCorpse, &current, sizeof(current)) || !std::isfinite(current) ||
            current < 0.0f || current >= seconds)
            continue;
        if (SafeWrite(module + kTimeForCorpse, seconds))
            Log("Bodies: soldiers' bodies at %08X now stay %.0f s (were %.0f s)", static_cast<unsigned>(module),
                seconds, current);
    }
}

}
