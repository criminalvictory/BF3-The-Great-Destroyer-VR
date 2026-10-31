#pragma once

namespace gunshape {

// The rifle's collision shape (tools/gunshape.cpp writes it from the gun's mesh dump to
// build\guns\m416.bin, next to the DLL): from a point in the gun's model space (x left, y up, z to
// the muzzle; metres), the distance to the gun's surface, negative inside, and the way out (a unit
// vector, the shape's gradient). False without the file, or off its grid (5 cm of air round the
// gun and more: nothing there touches it). Any thread once loaded; the first call loads it.
bool Distance(const float* p, float& distance, float* normal);
// The distance alone (a seventh of the work: no way out).
bool Distance(const float* p, float& distance);

// For tools (tools/collidetest.cpp): the shape from the file at path instead of next to the module.
// Only before the first Distance (the shape loads once); true when it loaded.
bool Load(const wchar_t* path);

}
