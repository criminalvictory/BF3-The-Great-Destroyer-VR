// The player's look angles found from outside the game: every float pair, the yaw and then the
// pitch a set gap after it, that matches the game camera ([bf3+1F8AADC]+60, its forward row) the
// way the mod reads the angles (yaw = atan2(x, -z), pitch = -asin(y)). With "test", each pair's yaw
// is nudged and the pair kept only if the camera turns with it; the nudge is undone either way.
// Look a little up or down first: level, the pitch is 0 and matches every zero.
//   anglescan [gap=12] [test|scan] [yaw tolerance=0.002] [pitch tolerance=0.002]
//   anglescan refs <hex address> [hex max offset=400]   what points at or a little before it
//   anglescan path player|soldier <hex lo> <hex hi> [depth=2]   pointer paths into [lo, hi]
//   anglescan ao                                  objects shaped like the level's AO settings
//   anglescan pair <a> <b>                        every place float a sits just before float b
#include <windows.h>
#include <tlhelp32.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr float kTwoPi = 6.28318531f;
HANDLE g_game = nullptr;

bool Read(uintptr_t address, void* out, size_t size) {
    SIZE_T got = 0;
    return ReadProcessMemory(g_game, reinterpret_cast<void*>(address), out, size, &got) && got == size;
}

bool Write(uintptr_t address, float value) {
    SIZE_T put = 0;
    return WriteProcessMemory(g_game, reinterpret_cast<void*>(address), &value, sizeof(value), &put) && put == sizeof(value);
}

bool CameraAngles(float& yaw, float& pitch) {
    uint32_t camera = 0;
    float m[16];
    if (!Read(0x400000 + 0x1F8AADC, &camera, 4) || !camera || !Read(camera + 0x60, m, sizeof(m)))
        return false;
    const float* forward = m + 8;
    yaw = std::atan2(forward[0], -forward[2]);
    pitch = -std::asin(forward[1] < -1.0f ? -1.0f : forward[1] > 1.0f ? 1.0f : forward[1]);
    return true;
}

float YawError(float a, float b) {
    float d = a - b;
    d -= kTwoPi * std::nearbyint(d / kTwoPi);
    return std::fabs(d);
}

DWORD GamePid() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W e{sizeof(e)};
    DWORD pid = 0;
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
        if (!_wcsicmp(e.szExeFile, L"bf3.exe"))
            pid = e.th32ProcessID;
    CloseHandle(snap);
    return pid;
}

}

// Every dword in the game's memory that points at most maxOffset bytes before target (an object
// the target may sit in), and whether it's in bf3.exe's own image (a static to start a chain from).
int Refs(uintptr_t target, uintptr_t maxOffset) {
    std::vector<uint8_t> chunk;
    MEMORY_BASIC_INFORMATION mbi;
    int found = 0;
    for (uintptr_t at = 0x10000; at < 0xFFFF0000u && VirtualQueryEx(g_game, reinterpret_cast<void*>(at), &mbi, sizeof(mbi));
         at = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !mbi.Protect)
            continue;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        for (size_t offset = 0; offset < mbi.RegionSize; offset += 1 << 20) {
            const size_t size = std::min<size_t>(1 << 20, mbi.RegionSize - offset);
            chunk.resize(size);
            if (!Read(base + offset, chunk.data(), size))
                continue;
            for (size_t i = 0; i + 4 <= size; i += 4) {
                uint32_t v;
                std::memcpy(&v, &chunk[i], 4);
                if (v > target || target - v > maxOffset)
                    continue;
                const uintptr_t holder = base + offset + i;
                const bool image = holder >= 0x400000 && holder < 0x2800000;
                std::printf("  [%08X] = %08X, +%X to the target%s\n", static_cast<unsigned>(holder), v,
                            static_cast<unsigned>(target - v), image ? "  (in bf3.exe)" : "");
                if (++found > 200)
                    return 0;
            }
        }
    }
    std::printf("%d\n", found);
    return 0;
}

uint32_t Pointer(uintptr_t address) {
    uint32_t v = 0;
    return Read(address, &v, 4) ? v : 0;
}

// Pointer paths from the local player (or its soldier) to [lo, hi]: each step a field of the
// last object (the first 0x400 bytes), up to depth steps; read-only.
int Path(const char* rootName, uintptr_t lo, uintptr_t hi, int depth) {
    const uintptr_t context = Pointer(0x400000 + 0x1F80B58);
    const uintptr_t players = context ? Pointer(context + 0x30) : 0;
    const uintptr_t player = players ? Pointer(players + 0xBC) : 0;
    const uintptr_t soldier = player ? Pointer(player + 0x3D8) : 0;
    const uintptr_t root = !std::strcmp(rootName, "soldier") ? soldier : player;
    std::printf("player %08X, soldier %08X; from the %s\n", static_cast<unsigned>(player),
                static_cast<unsigned>(soldier), rootName);
    if (!root)
        return 1;
    struct Node {
        uintptr_t address;
        std::vector<uint32_t> offsets;
    };
    std::vector<Node> level{{root, {}}};
    std::vector<uintptr_t> seen{root};
    int found = 0;
    for (int d = 0; d <= depth && !level.empty(); ++d) {
        std::vector<Node> next;
        for (const Node& n : level) {
            uint32_t fields[0x100];
            if (!Read(n.address, fields, sizeof(fields)))
                continue;
            for (uint32_t i = 0; i < 0x100; ++i) {
                const uint32_t v = fields[i];
                if (v >= lo && v <= hi) {
                    std::printf("  ");
                    for (uint32_t o : n.offsets)
                        std::printf("+%X -> ", o);
                    std::printf("+%X = %08X (target - %X)\n", i * 4, v, static_cast<unsigned>(hi - v));
                    if (++found > 100)
                        return 0;
                } else if (d < depth && v >= 0x01000000 && v < 0xFFFF0000u && !(v & 3) &&
                           std::find(seen.begin(), seen.end(), v) == seen.end() && seen.size() < 200000) {
                    seen.push_back(v);
                    Node child{v, n.offsets};
                    child.offsets.push_back(i * 4);
                    next.push_back(child);
                }
            }
        }
        level.swap(next);
    }
    std::printf("%d\n", found);
    return 0;
}

// Objects shaped like BF3's DynamicAOComponentData (the level's ambient occlusion settings, from
// its type tables): a vtable in bf3.exe, Realm +0x60, SsaoMaxDistanceOuter +0x64, SsaoFade +0x68,
// SsaoRadius +0x6C, SsaoMaxDistanceInner +0x70, HbaoRadius +0x74, HbaoAttenuation +0x78,
// HbaoAngleBias +0x7C, HbaoPowerExponent +0x80, HbaoContrast +0x84, HbaoMaxFootprintRadius +0x88,
// Enable +0x8C. Read-only.
int AoScan() {
    std::vector<uint8_t> chunk;
    MEMORY_BASIC_INFORMATION mbi;
    int found = 0;
    auto in = [](float v, float lo, float hi) { return v >= lo && v <= hi; };
    for (uintptr_t at = 0x10000; at < 0xFFFF0000u && VirtualQueryEx(g_game, reinterpret_cast<void*>(at), &mbi, sizeof(mbi));
         at = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !mbi.Protect)
            continue;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        for (size_t offset = 0; offset < mbi.RegionSize; offset += 1 << 20) {
            const size_t size = std::min<size_t>(1 << 20, mbi.RegionSize - offset);
            chunk.resize(size + 0x90);
            if (!Read(base + offset, chunk.data(), size))
                continue;
            for (size_t i = 0; i + 0x90 <= size; i += 4) {
                const uint8_t* o = &chunk[i];
                uint32_t vtable, realm;
                float f[10];
                std::memcpy(&vtable, o, 4);
                std::memcpy(&realm, o + 0x60, 4);
                std::memcpy(f, o + 0x64, sizeof(f));  // +0x64 .. +0x88
                if (vtable < 0x400000 || vtable >= 0x2800000 || realm > 3 || o[0x8C] != 1)
                    continue;
                const float radius = f[4], attenuation = f[5], bias = f[6], power = f[7], contrast = f[8], footprint = f[9];
                if (!in(radius, 0.05f, 20.0f) || !in(attenuation, 0.0f, 20.0f) || !in(bias, 0.0f, 3.0f) ||
                    !in(power, 0.05f, 20.0f) || !in(contrast, 0.0f, 50.0f) || !in(footprint, 0.0001f, 10.0f))
                    continue;
                std::printf("  %08X vtable %08X realm %u enable %u: ssao outer %.3f fade %.3f radius %.3f inner %.3f | "
                            "hbao radius %.3f attenuation %.3f bias %.3f power %.3f contrast %.3f footprint %.4f\n",
                            static_cast<unsigned>(base + offset + i), vtable, realm, o[0x8C], f[0], f[1], f[2], f[3],
                            radius, attenuation, bias, power, contrast, footprint);
                if (++found > 40)
                    return 0;
            }
        }
    }
    std::printf("%d\n", found);
    return 0;
}

// Every place two floats sit next to each other (a then b), with the 16 dwords before; read-only.
int Pair(float a, float b) {
    std::vector<uint8_t> chunk;
    MEMORY_BASIC_INFORMATION mbi;
    int found = 0;
    for (uintptr_t at = 0x10000; at < 0xFFFF0000u && VirtualQueryEx(g_game, reinterpret_cast<void*>(at), &mbi, sizeof(mbi));
         at = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !mbi.Protect)
            continue;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        for (size_t offset = 0; offset < mbi.RegionSize; offset += 1 << 20) {
            const size_t size = std::min<size_t>(1 << 20, mbi.RegionSize - offset);
            chunk.resize(size);
            if (!Read(base + offset, chunk.data(), size))
                continue;
            for (size_t i = 0; i + 8 <= size; i += 4) {
                float x, y;
                std::memcpy(&x, &chunk[i], 4);
                std::memcpy(&y, &chunk[i + 4], 4);
                if (x != a || y != b)
                    continue;
                const uintptr_t where = base + offset + i;
                float before[16] = {};
                Read(where - 0x40, before, sizeof(before));
                std::printf("  %08X:", static_cast<unsigned>(where));
                for (float f : before)
                    std::printf(" %.3g", f);
                std::printf("\n");
                if (++found > 40)
                    return 0;
            }
        }
    }
    std::printf("%d\n", found);
    return 0;
}

int main(int argc, char** argv) {
    const DWORD pid = GamePid();
    g_game = pid ? OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION,
                               FALSE, pid)
                 : nullptr;
    if (g_game && argc > 2 && !std::strcmp(argv[1], "refs"))
        return Refs(std::strtoul(argv[2], nullptr, 16), argc > 3 ? std::strtoul(argv[3], nullptr, 16) : 0x400);
    if (g_game && argc > 1 && !std::strcmp(argv[1], "ao"))
        return AoScan();
    if (g_game && argc > 3 && !std::strcmp(argv[1], "pair"))
        return Pair(static_cast<float>(std::atof(argv[2])), static_cast<float>(std::atof(argv[3])));
    if (g_game && argc > 4 && !std::strcmp(argv[1], "path"))
        return Path(argv[2], std::strtoul(argv[3], nullptr, 16), std::strtoul(argv[4], nullptr, 16),
                    argc > 5 ? std::atoi(argv[5]) : 2);
    const int gap = argc > 1 ? std::atoi(argv[1]) : 12;
    const bool test = argc > 2 && !std::strcmp(argv[2], "test");
    const float yawTolerance = argc > 3 ? static_cast<float>(std::atof(argv[3])) : 0.002f;
    const float pitchTolerance = argc > 4 ? static_cast<float>(std::atof(argv[4])) : 0.002f;
    float yaw = 0.0f, pitch = 0.0f;
    if (!g_game || !CameraAngles(yaw, pitch)) {
        std::printf("can't read the game camera\n");
        return 1;
    }
    std::printf("camera: yaw %.4f, pitch %.4f\n", yaw, pitch);
    if (std::fabs(pitch) < 0.05f) {
        std::printf("(nearly level: look up or down a little for fewer false pairs)\n");
        if (test) {
            // A level pitch matches every zero: hundreds of pairs in random memory, and nudging
            // them crashed the game (2026-10-30).
            std::printf("not testing while level\n");
            return 1;
        }
    }

    std::vector<uintptr_t> pairs;
    std::vector<uint8_t> chunk;
    MEMORY_BASIC_INFORMATION mbi;
    for (uintptr_t at = 0x10000; at < 0xFFFF0000u && VirtualQueryEx(g_game, reinterpret_cast<void*>(at), &mbi, sizeof(mbi));
         at = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
        const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (mbi.State != MEM_COMMIT || !(mbi.Protect & writable) || (mbi.Protect & PAGE_GUARD))
            continue;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        for (size_t offset = 0; offset < mbi.RegionSize; offset += 1 << 20) {
            const size_t size = std::min<size_t>(1 << 20, mbi.RegionSize - offset);
            chunk.resize(size);
            if (!Read(base + offset, chunk.data(), size))
                continue;
            for (size_t i = 0; i + gap + 4 <= size; i += 4) {
                float y, p;
                std::memcpy(&y, &chunk[i], 4);
                if (!(std::fabs(y) < 1000.0f) || YawError(y, yaw) > yawTolerance)
                    continue;
                std::memcpy(&p, &chunk[i + gap], 4);
                if (std::fabs(p - pitch) < pitchTolerance)
                    pairs.push_back(base + offset + i);
            }
        }
    }
    std::printf("%zu pairs (yaw, pitch %d bytes on)\n", pairs.size(), gap);

    for (uintptr_t a : pairs) {
        uint32_t around[8] = {};
        Read(a - 16, around, sizeof(around));
        std::printf("  %08X:", static_cast<unsigned>(a));
        for (uint32_t d : around)
            std::printf(" %08X", d);
        if (test) {
            float before = 0.0f, after = 0.0f, camYaw0 = 0.0f, camYaw1 = 0.0f, camPitch = 0.0f;
            if (Read(a, &before, 4) && CameraAngles(camYaw0, camPitch) && Write(a, before + 0.1f)) {
                Sleep(150);
                Read(a, &after, 4);
                CameraAngles(camYaw1, camPitch);
                const bool kept = std::fabs(after - (before + 0.1f)) < 0.03f;
                float turned = camYaw1 - camYaw0;
                turned -= kTwoPi * std::nearbyint(turned / kTwoPi);
                if (kept)
                    Write(a, after - 0.1f);
                std::printf("  %s, camera turned %.3f%s", kept ? "kept" : "snapped back", turned,
                            kept && std::fabs(turned - 0.1f) < 0.03f ? "  <== REAL" : "");
            }
        }
        std::printf("\n");
    }
    return 0;
}
