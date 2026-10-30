#pragma once

namespace god {

// Every frame: tops the player's health back up ([god] in bf3vr.ini), campaign only. A single
// hit big enough to kill within one frame can still kill.
void Apply();

// End: god mode on or off (saved).
void Toggle();

}
