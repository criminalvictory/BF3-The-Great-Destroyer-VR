// The mod's reload sounds (src/sounds.cpp) played outside the game, to hear or record them on their
// own: playtest [name=bolt] [pan=0.3]. The clips are build\sounds\<name>.wav, next to this program.
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "../src/sounds.h"

namespace logx {
void Write(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}
}

int main(int argc, char** argv) {
    sounds::Play(argc > 1 ? argv[1] : "bolt", argc > 2 ? static_cast<float>(std::atof(argv[2])) : 0.3f);
    Sleep(1000);
    return 0;
}
