// FOV override. BF3's menu stops at 90, but that limit only lives in the menu; the renderer
// keeps its own value, worked out from the menu setting. Once the FOV finder has found where
// (finder.cpp, Home), this writes a wider value there every frame.
//
// An encoding is mapping * 8 + representation. The mapping turns the menu value S (degrees)
// into the angle actually stored; the representation is how that angle is written.
// The menu's own copy is plain degrees (encoding 0). In game, BF3 at "90" looked life-size at
// about 70 degrees top to bottom, which fits S being the width on a 4:3 screen (mapping 1).

#include "fov.h"

#include <windows.h>

#include <cmath>
#include <cstring>

#include "camera.h"
#include "config.h"
#include "finder.h"
#include "log.h"

namespace {

constexpr double kPi = 3.14159265358979;

// Mappings from the menu value S to the stored angle, in degrees.
double MappedDegrees(int mapping, double s) {
    static const double kRatio[] = {1.0, 3.0 / 4.0, 9.0 / 16.0, 4.0 / 3.0, 16.0 / 9.0};
    if (mapping == 0)
        return s;
    double half = s * kPi / 360.0;
    return 2.0 * std::atan(std::tan(half) * kRatio[mapping]) * 180.0 / kPi;
}

bool SafeWrite(uintptr_t address, uint32_t value) {
    __try {
        *reinterpret_cast<uint32_t*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

float AsFloat(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

}

namespace fov {

bool Valid(int encoding) {
    int mapping = encoding / 8, rep = encoding % 8;
    return mapping < kMappings && rep < 7 && (rep != 6 || mapping == 0);
}

uint32_t Encode(int encoding, float degrees) {
    int rep = encoding % 8;
    double angle = MappedDegrees(encoding / 8, degrees);
    if (rep == 6)
        return static_cast<uint32_t>(static_cast<int32_t>(std::lround(angle)));
    double radians = angle * kPi / 180.0;
    double v = 0.0;
    switch (rep) {
    case 0: v = angle; break;
    case 1: v = radians; break;
    case 2: v = angle / 2; break;
    case 3: v = radians / 2; break;
    case 4: v = std::tan(radians / 2); break;
    default: v = 1.0 / std::tan(radians / 2); break;
    }
    float f = static_cast<float>(v);
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

bool Matches(int encoding, uint32_t stored, float degrees) {
    uint32_t want = Encode(encoding, degrees);
    if (encoding % 8 == 6)
        return stored == want;
    float a = AsFloat(stored), b = AsFloat(want);
    return std::fabs(a - b) <= 2e-4f * std::fmax(1.0f, std::fabs(b));
}

void Apply() {
    // While the finder is comparing slider positions, the setting must stay as the menu left it.
    if (finder::FovFinderActive())
        return;
    config::Fov f = config::GetFov();
    if (f.value <= 0.0f)
        return;
    for (const config::Fov::Target& t : f.targets) {
        uintptr_t address = 0;
        uint32_t current = 0;
        if (!Valid(t.encoding) || !camera::Resolve(t.pointer, address) ||
            !camera::SafeRead(address, &current, sizeof(current)))
            continue;
        uint32_t want = Encode(t.encoding, f.value);
        if (current != want)
            SafeWrite(address, want);
    }
}

void CycleValue() {
    config::Fov f = config::GetFov();
    // From off (0) or anything below the menu's own range, start at 90 rather than stepping up from there.
    f.value = f.value < 90.0f || f.value >= 130.0f ? 90.0f : std::floor(f.value / 10.0f) * 10.0f + 10.0f;
    config::SetFov(f);
    Log("FOV override %.0f%s", f.value, f.targets.empty() ? " (not found yet: run the FOV finder with Home)" : "");
}

}
