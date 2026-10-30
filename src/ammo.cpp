#include "ammo.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>

#include "camera.h"
#include "god.h"
#include "log.h"
#include "skin.h"

namespace {

// Found 2026-10-30 by firing and searching (tools/findints.ps1, tools/pathto.ps1): the server's
// soldier holds its weapons component at +0x4C0, whose +0x248 is the firing state of the gun in
// hand (+0x258 the grenades'). In it, +0xF4 is the rounds loaded (magazine and chamber), +0xF8 the
// spare rounds. When its loaded count ran out the game reloaded by itself (30 in, 30 fewer spare);
// the client has objects of the same kind (+0xBC, +0x1D8, +0x580) that follow the server's count.
constexpr uintptr_t kWeaponsComponent = 0x4C0;
constexpr uintptr_t kCurrentFiring = 0x248;
constexpr uintptr_t kFiringType = 0x1DAD810;  // vtable, bf3.exe offset
constexpr uintptr_t kLoaded = 0xF4;
constexpr uintptr_t kSpare = 0xF8;

uintptr_t g_soldier = 0;  // the server's soldier the guns below belong to
uintptr_t g_firing = 0;   // the gun in hand
// The most rounds each gun has held: a full magazine and one in the chamber.
std::unordered_map<uintptr_t, int> g_fullest;

uintptr_t ReadPointer(uintptr_t address) {
    uint32_t value = 0;
    return camera::SafeRead(address, &value, sizeof(value)) ? value : 0;
}

bool WriteInt(uintptr_t address, int value) {
    __try {
        *reinterpret_cast<int*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t Firing() {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uintptr_t soldier = god::PlayerServerSoldier();
    uintptr_t weapons = soldier ? ReadPointer(soldier + kWeaponsComponent) : 0;
    uintptr_t firing = weapons ? ReadPointer(weapons + kCurrentFiring) : 0;
    return firing && ReadPointer(firing) == base + kFiringType ? firing : 0;
}

bool Read(uintptr_t firing, int& loaded, int& spare) {
    return camera::SafeRead(firing + kLoaded, &loaded, sizeof(loaded)) &&
           camera::SafeRead(firing + kSpare, &spare, sizeof(spare)) && loaded >= 0 && loaded < 10000 &&
           spare >= 0 && spare < 100000;
}

// The magazine's size. The game's own data has it (AmmoConfigData.MagazineCapacity) but where the
// gun's copy sits isn't known yet; the gun starts a checkpoint full, a magazine and a chambered round.
int Capacity() {
    auto it = g_fullest.find(g_firing);
    return std::max(1, (it == g_fullest.end() ? 0 : it->second) - 1);
}

}

namespace ammo {

bool Get(int& loaded, int& spare) {
    uintptr_t firing = Firing();
    return firing && Read(firing, loaded, spare);
}

void Update() {
    uintptr_t soldier = god::PlayerServerSoldier();
    if (soldier != g_soldier) {  // a new life or a checkpoint: new guns, magazines in
        g_soldier = soldier;
        g_fullest.clear();
        skin::InsertMagazine();
    }
    uintptr_t firing = Firing();
    int loaded = 0, spare = 0;
    if (!firing || !Read(firing, loaded, spare))
        return;
    g_firing = firing;
    int& fullest = g_fullest[firing];
    fullest = std::max(fullest, loaded);
}

void Eject() {
    uintptr_t firing = Firing();
    int loaded = 0, spare = 0;
    if (!firing || !Read(firing, loaded, spare)) {
        Log("Magazine: the gun's rounds weren't found; only the picture changes");
        return;
    }
    int chambered = std::min(loaded, 1);
    WriteInt(firing + kSpare, spare + loaded - chambered);
    WriteInt(firing + kLoaded, chambered);
    Log("Magazine: out; %d rounds back to the spare ones, %d left in the chamber", loaded - chambered, chambered);
}

void Insert() {
    uintptr_t firing = Firing();
    int loaded = 0, spare = 0;
    if (!firing || !Read(firing, loaded, spare)) {
        Log("Magazine: the gun's rounds weren't found; only the picture changes");
        return;
    }
    int take = std::min(Capacity(), spare);
    WriteInt(firing + kSpare, spare - take);
    WriteInt(firing + kLoaded, loaded + take);
    Log("Magazine: in; %d rounds loaded, %d spare (a magazine holds %d)", loaded + take, spare - take, Capacity());
}

int MagazineSize() {
    return Capacity();
}

}
