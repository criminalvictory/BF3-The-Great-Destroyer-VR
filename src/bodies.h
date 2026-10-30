#pragma once

namespace bodies {

// Once a second: makes dead soldiers' bodies stay [world] corpse_seconds (bf3vr.ini) before the
// game removes them, by raising the TimeForCorpse of every soldier the game's server lists.
// Campaign only; 0 leaves the game's own times.
void Apply();

}
