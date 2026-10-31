#pragma once

namespace crash {

// Logs a crash the game doesn't recover from (where, what it touched, the stack: dllmain.cpp);
// called from Present, it keeps the mod's crash filter on top of any the game sets later.
void KeepOnTop();

}
