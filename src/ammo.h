#pragma once

namespace ammo {

// The rounds in the gun in hand and the spare ones, as the game's own server counts them: that
// count decides whether the gun fires and when it reloads, and the HUD's copy follows it.
// Render thread. False when the soldier or the gun can't be found.
bool Get(int& loaded, int& spare);

// The magazine came out: one round stays in the chamber, the rest go back to the spare rounds.
void Eject();

// A new magazine went in: as full as the spare rounds allow, plus the chambered round.
void Insert();

// Every frame: notes how full the gun gets (a magazine plus a chambered round), which gives the
// magazine's size for Insert.
void Update();

// That size: 1 for a gun loaded a round at a time (the SMAW, the grenade launcher).
int MagazineSize();

// Testing (weapon.txt "refill"): the gun in hand gets 300 spare rounds. Render thread.
void Refill();

}
