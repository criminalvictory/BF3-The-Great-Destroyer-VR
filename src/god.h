#pragma once

namespace god {

// Every frame: keeps the player's health and maximum health at [god] value in bf3vr.ini, in the
// game's own server (which decides deaths) and in the client's copy (which draws the screen).
// Campaign only. Scripted deaths, like leaving the area of operation, still happen.
void Apply();

// End: god mode on or off (saved).
void Toggle();

}
