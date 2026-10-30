#pragma once

namespace window {

// Called every frame. In windowed mode, keeps BF3's window at the square size from [window] size
// in bf3vr.ini. BF3 only adds view height until its window is square, so square gives the most
// view top to bottom, and a bigger square gives a sharper headset image. The window may be
// bigger than the monitor: the mod lifts Windows' screen-size limit for it.
void Maintain();

// Per-eye views (views.cpp): the window twice as wide, so each eye's half is the square size.
void SetWide(bool wide);

}
