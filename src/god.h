#pragma once

#include <cstdint>
#include <vector>

namespace god {

// Every frame: tops the player's health back up ([god] in bf3vr.ini), campaign only. A single
// hit big enough to kill within one frame can still kill.
void Apply();

// End: god mode on or off (saved).
void Toggle();

// Health values found this session (absolute addresses: every value that showed the player's
// health after a hit, the game's own and its copies). Pinned while most of them still agree with
// each other; the game reuses the memory after a level load, and then they stop agreeing.
void SetSessionHealth(const std::vector<uintptr_t>& addresses);

}
