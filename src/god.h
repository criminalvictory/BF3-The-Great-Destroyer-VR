#pragma once

#include <cstdint>
#include <vector>

namespace god {

// Every frame: keeps the player's health and maximum health at [god] value in bf3vr.ini, in the
// game's own server (which decides deaths) and in the client's copy (which draws the screen).
// Campaign only. Scripted deaths, like leaving the area of operation, still happen.
void Apply();

// End: god mode on or off (saved).
void Toggle();

// Health values found this session (absolute addresses: every value that showed the player's
// health after a hit, the game's own and its copies). Pinned while most of them still agree with
// each other; the game reuses the memory after a level load, and then they stop agreeing.
void SetSessionHealth(const std::vector<uintptr_t>& addresses);

}
