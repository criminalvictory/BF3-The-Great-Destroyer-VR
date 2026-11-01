#pragma once

namespace pad {

// Walking by an Xbox controller's left stick, which BF3 reads through XInput: any direction, as
// fast as the stick is pushed (W/A/S/D only give eight directions at one speed). The mod's stick,
// turned from where the head faces into where the game faces, is put in as controller 0's left
// stick; nothing else of a controller is (no buttons, no right stick). Hooks XInputGetState in the
// game's own XInput the first time it's asked to.
//
// Each frame: the move in the game's frame (x right, y forward, -1..1, length up to 1). False when
// XInput couldn't be hooked, or while the game isn't reading controller 0 (the keys are used then):
// some runs it stops asking for it (the user, 2026-11-01: "i cannot move ... after fighting in the
// interrogation room"; the same in two runs on 2026-10-31).
bool SetMove(float x, float y);

}
