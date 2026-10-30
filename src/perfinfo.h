#pragma once

namespace perfinfo {

// BF3's main thread asks Windows for the PC's memory statistics (GetPerformanceInfo) every frame.
// To answer, Windows counts every process and thread on the PC: 11 ms a call with SteamVR,
// browsers and the rest running (384 processes, 7,800 threads), most of the main thread's frame;
// with two views the frame no longer fit the headset's 13.9 ms, and turning with the stick
// stuttered. The game is answered from a copy refreshed once a second on a thread of its own.
// Call after MH_Initialize; MH_EnableHook(MH_ALL_HOOKS) turns it on.
bool Install();

// Every 10 s: how often the game asked, and what a refresh of the copy costs.
void LogStats();

}
