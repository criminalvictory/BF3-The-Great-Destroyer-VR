#pragma once

namespace sounds {

// The game's own reload sounds, cut from a recording of its reload (tools/capture.cpp) into
// build\sounds\<name>.wav, played when the hands do the same by hand: "mag_out", "mag_in",
// "bolt" (the bolt going home), "charge" (the charging handle drawn back). pan: -1 left to 1
// right; the reloads play in the middle (user, 2026-10-30: not off to the gun's side). A file
// that isn't there plays nothing; a changed one is read again. Any thread.
void Play(const char* name, float pan = 0.0f);

}
