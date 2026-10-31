#include "ammo.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>

#include "camera.h"
#include "config.h"
#include "god.h"
#include "log.h"
#include "reload.h"
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

// The rifle reloaded by hand (skin::RifleInHand, Blackburn's M416): its magazine's size. The game's
// own data has it (AmmoConfigData.MagazineCapacity), not found yet (tools/gunammo.ps1 looked two
// pointers deep from the firing state). Learning it from the most rounds seen went wrong: a reload
// on top of the game's own made it 59, then 88 (2026-10-30).
constexpr int kRifleMagazine = 30;

// Per gun (its firing state). The rifle's count, as the game keeps it, has one round more than
// the mod says (hidden): the trigger is let go at one round left, and the game, a frame or two
// behind, sometimes fired that one too; with none it reloaded by itself, a magazine on top of the
// hand's. With the extra one it never runs out.
struct Gun {
    int fullest = 0;      // the most rounds seen in it before the mod wrote its count
    int learned = 0;      // the magazine's size from the game's own reload, 0 not seen
    int hidden = 0;       // the extra round in the game's count
    int last = -1;        // the game's count last frame
    bool written = false;
};

uintptr_t g_soldier = 0;  // the server's soldier the guns below belong to
uintptr_t g_firing = 0;   // the gun in hand
std::unordered_map<uintptr_t, Gun> g_guns;

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

// The game's own counts.
bool ReadRaw(uintptr_t firing, int& loaded, int& spare) {
    return camera::SafeRead(firing + kLoaded, &loaded, sizeof(loaded)) &&
           camera::SafeRead(firing + kSpare, &spare, sizeof(spare)) && loaded >= 0 && loaded < 10000 &&
           spare >= 0 && spare < 100000;
}

// As the mod counts them: without the hidden round.
bool Read(uintptr_t firing, int& loaded, int& spare) {
    if (!ReadRaw(firing, loaded, spare))
        return false;
    auto it = g_guns.find(firing);
    if (it != g_guns.end())
        loaded = std::max(0, loaded - it->second.hidden);
    return true;
}

void Write(uintptr_t firing, int loaded, int spare) {
    Gun& gun = g_guns[firing];
    gun.written = true;
    gun.last = loaded + gun.hidden;
    WriteInt(firing + kSpare, spare);
    WriteInt(firing + kLoaded, gun.last);
}

// The magazine's size for the gun in hand.
int Capacity() {
    if (skin::RifleInHand())
        return kRifleMagazine;
    auto it = g_guns.find(g_firing);
    if (it == g_guns.end())
        return 1;
    return it->second.learned ? it->second.learned : std::max(1, it->second.fullest - 1);
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
        g_guns.clear();
        skin::InsertMagazine();
        reload::ReleaseBolt();
    }
    uintptr_t firing = Firing();
    int raw = 0, spare = 0;
    if (!firing || !ReadRaw(firing, raw, spare))
        return;
    Gun& gun = g_guns[firing];
    if (firing != g_firing)
        Log("Ammo: the gun in hand has %d rounds loaded, %d spare", raw - gun.hidden, spare);
    g_firing = firing;
    // The game's own reload (R held on X): its count up by a magazine without the mod writing it.
    // A magazine and the chambered round: its size, and the hidden round goes back on top.
    if (gun.last >= 1 && raw >= gun.last + 5) {
        gun.learned = raw - 1;
        if (gun.hidden) {
            WriteInt(firing + kLoaded, raw + gun.hidden);
            raw += gun.hidden;
        }
        Log("Ammo: the game reloaded by itself: %d rounds loaded (a magazine holds %d)", raw - gun.hidden,
            gun.learned);
    }
    if (!gun.written)
        gun.fullest = std::max(gun.fullest, raw);
    // The rifle reloaded by hand gets its hidden round.
    if (!gun.hidden && skin::RifleInHand() && raw >= 1) {
        gun.hidden = 1;
        gun.written = true;
        raw += 1;
        WriteInt(firing + kLoaded, raw);
    }
    gun.last = raw;
    // With god mode on, the spare rounds don't run out: testing the reloads by hand over and over
    // emptied them, and with none left the gun stayed locked back (2026-10-30).
    if (config::GetGod().enabled && spare < 60) {
        WriteInt(firing + kSpare, 300);
        Log("Ammo: spare rounds %d -> 300 (god mode keeps them up)", spare);
    }
}

void Eject() {
    uintptr_t firing = Firing();
    int loaded = 0, spare = 0;
    if (!firing || !Read(firing, loaded, spare)) {
        Log("Magazine: the gun's rounds weren't found; only the picture changes");
        return;
    }
    // One round stays (the chambered one, or the one an empty gun keeps), from the spare if the
    // game fired it.
    int kept = std::min(1, loaded + spare);
    Write(firing, kept, spare + loaded - kept);
    Log("Magazine: out; %d rounds back to the spare ones, %d left in the chamber", loaded - kept, kept);
}

void Insert() {
    uintptr_t firing = Firing();
    int loaded = 0, spare = 0;
    if (!firing || !Read(firing, loaded, spare)) {
        Log("Magazine: the gun's rounds weren't found; only the picture changes");
        return;
    }
    int take = std::min(Capacity(), spare);
    Write(firing, loaded + take, spare - take);
    Log("Magazine: in; %d rounds loaded, %d spare (a magazine holds %d)", loaded + take, spare - take, Capacity());
}

int MagazineSize() {
    return Capacity();
}

void Refill() {
    uintptr_t firing = Firing();
    int loaded = 0, spare = 0;
    if (!firing || !Read(firing, loaded, spare)) {
        Log("Ammo: the gun's rounds weren't found; no refill");
        return;
    }
    Write(firing, loaded, 300);
    Log("Ammo: spare rounds %d -> 300 (weapon.txt \"refill\"), %d loaded", spare, loaded);
}

void Empty() {
    uintptr_t firing = Firing();
    int loaded = 0, spare = 0;
    if (!firing || !Read(firing, loaded, spare)) {
        Log("Ammo: the gun's rounds weren't found; not emptied");
        return;
    }
    // The game's reload is wanted here, so the gun gets no hidden round until it has reloaded.
    Gun& gun = g_guns[firing];
    gun.hidden = 0;
    Write(firing, 0, spare + loaded);
    Log("Ammo: %d rounds in the gun -> 0, to the spare ones (weapon.txt \"empty\")", loaded);
}

}
