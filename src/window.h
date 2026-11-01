#pragma once

struct HWND__;

namespace window {

// Called every frame. In windowed mode, keeps BF3's window at the square size from [window] size
// in bf3vr.ini. BF3 only adds view height until its window is square, so square gives the most
// view top to bottom, and a bigger square gives a sharper headset image. The window may be
// bigger than the monitor: the mod lifts Windows' screen-size limit for it. With BF3 in front,
// also keeps the mouse pointer on the part of its window that's on screen, off the taskbar.
// Fitted ([window] fit, the default), the window itself fills the monitor instead, borderless, in the shape of
// the picture it shows, and the game is told it's still that size: it draws as before, and Windows scales the
// picture into the window.
void Maintain();

// Per-eye views (views.cpp): the window twice as wide, so each eye's half is the square size.
void SetWide(bool wide);

// The window fitted to the monitor or as big as the game draws (weapon.txt "windowfit 0/1").
void SetFit(bool on);

// The game's own fullscreen (its swap chain's state): the window is left as the game and DXGI make it, still told
// the size the game draws at.
void SetGameFullscreen(bool on);

// The shape of the picture the window shows, width over height: the stream view's (streamview.cpp) while it's
// on; 0 = the game's picture's own.
void SetShape(float aspect);

// While the window is fitted: the size the game is told its window is, and draws at, for that window (the swap
// chain's). False when it's not the game's window or the window isn't fitted.
bool GameSize(HWND__* window, int& width, int& height);

// True with the mouse pointer on BF3's own window, moving it there first if it can. An injected
// click also lands on the window under the pointer, so the mod's clicks wait for this.
bool PointerOnGame();

}
