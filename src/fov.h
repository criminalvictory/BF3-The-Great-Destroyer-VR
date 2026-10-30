#pragma once
#include <cstdint>

namespace fov {

// Encodings are mapping * 8 + representation (see fov.cpp); kMappings * 8 covers them all.
constexpr int kMappings = 5;
constexpr int kEncodingSpace = kMappings * 8;
bool Valid(int encoding);

// The 4 bytes BF3 would store for a menu FOV of `degrees` in an encoding.
uint32_t Encode(int encoding, float degrees);

// Whether a stored value matches a menu FOV of `degrees` in an encoding (floats with a little slack).
bool Matches(int encoding, uint32_t stored, float degrees);

// Every frame: writes [fov] value into the saved targets, past the menu's limit.
void Apply();

// End: steps [fov] value 90 -> 100 -> ... -> 130 -> 90 and saves it.
void CycleValue();

}
