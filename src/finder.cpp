// In-game camera finder (F10). Runs on a background thread in three phases:
//
//  1. Scan:  every 16-byte aligned block of writable memory shaped like a Frostbite
//            LinearTransform with no roll becomes a candidate.
//  2. Track: for 20 seconds the player looks up and down, turns and walks. Candidates that
//            stay valid and move like a first-person camera survive.
//  3. Point: a reverse pointer search from the survivors back to static data in bf3.exe,
//            like Cheat Engine's pointer scan. The best chain goes into bf3vr.ini.
//
// The player can't see the log from inside the headset, so each phase is announced with beeps.

#include "finder.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "camera.h"
#include "config.h"
#include "fov.h"
#include "log.h"

namespace {

constexpr size_t kMaxCandidates = 200000;
constexpr int kTrackSamples = 200;  // 20 s at 10 Hz
constexpr int kMaxMisses = 40;      // invalid samples tolerated (camera shake, level streaming)
constexpr float kMaxStep = 2.0f;    // metres per sample; more than this is a teleport, not walking
constexpr size_t kMaxSurvivors = 8;
constexpr size_t kMaxCopies = 64;
constexpr int kMaxDepth = 4;
constexpr uintptr_t kMaxOffset = 0x1000;
constexpr size_t kMaxTargets = 50000;
constexpr size_t kMaxRegions = 65536;
constexpr size_t kChunkSize = 1 << 20;

std::atomic<bool> g_busy{false};

// Allocation bases the scans must skip: the finder's own buffers, its stack and the mod DLL.
// Anything that holds raw game addresses lives in a Block, so the pointer search never
// mistakes the finder's own bookkeeping for game pointers.
std::vector<uintptr_t> g_skip;

template <class T>
struct Block {
    T* data = nullptr;
    size_t size = 0;
    size_t capacity = 0;

    explicit Block(size_t cap) : capacity(cap) {
        data = static_cast<T*>(VirtualAlloc(nullptr, cap * sizeof(T), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (data)
            g_skip.push_back(reinterpret_cast<uintptr_t>(data));
        else
            capacity = 0;
    }
    ~Block() {
        if (data) {
            g_skip.erase(std::remove(g_skip.begin(), g_skip.end(), reinterpret_cast<uintptr_t>(data)), g_skip.end());
            VirtualFree(data, 0, MEM_RELEASE);
        }
    }
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;

    bool Push(const T& v) {
        if (size == capacity)
            return false;
        data[size++] = v;
        return true;
    }
    T* begin() { return data; }
    T* end() { return data + size; }
};

struct Region {
    uintptr_t base;
    size_t size;
};

struct Module {
    uintptr_t base;
    uintptr_t end;
    bool Contains(uintptr_t a) const { return a >= base && a < end; }
};

Module GameModule() {
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return {base, base + nt->OptionalHeader.SizeOfImage};
}

bool IsWritable(DWORD protect) {
    if (protect & (PAGE_GUARD | PAGE_NOACCESS))
        return false;
    return (protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// All committed writable memory except what g_skip names.
void CollectRegions(Block<Region>& out) {
    out.size = 0;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    auto addr = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    auto max = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
    MEMORY_BASIC_INFORMATION mbi;
    while (addr < max && VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi))) {
        auto base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        auto allocation = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
        if (mbi.State == MEM_COMMIT && IsWritable(mbi.Protect) &&
            std::find(g_skip.begin(), g_skip.end(), allocation) == g_skip.end())
            out.Push({base, mbi.RegionSize});
        addr = base + mbi.RegionSize;
    }
}

// Calls fn(address, data, bytes) for every readable chunk of every region.
template <class Fn>
void ForEachChunk(Block<Region>& regions, Block<uint8_t>& buffer, Fn&& fn) {
    for (Region& r : regions) {
        for (uintptr_t offset = 0; offset < r.size; offset += kChunkSize) {
            size_t bytes = std::min<size_t>(kChunkSize, r.size - offset);
            if (camera::SafeRead(r.base + offset, buffer.data, bytes))
                fn(r.base + offset, buffer.data, bytes);
        }
    }
}

float Dot3(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// A first-person camera: orthonormal rotation, little roll, not axis-aligned, sane position.
bool LooksLikeCamera(const float* m, float tolerance, float maxRoll) {
    if (m[3] != 0.0f || m[7] != 0.0f || m[11] != 0.0f || (m[15] != 1.0f && m[15] != 0.0f))
        return false;
    for (int i = 0; i < 15; ++i)
        if (!std::isfinite(m[i]))
            return false;
    const float* right = m;
    const float* up = m + 4;
    const float* forward = m + 8;
    const float* trans = m + 12;
    if (std::fabs(Dot3(right, right) - 1) > tolerance || std::fabs(Dot3(up, up) - 1) > tolerance ||
        std::fabs(Dot3(forward, forward) - 1) > tolerance || std::fabs(Dot3(right, up)) > tolerance ||
        std::fabs(Dot3(right, forward)) > tolerance || std::fabs(Dot3(up, forward)) > tolerance)
        return false;
    if (std::fabs(right[1]) > maxRoll)
        return false;
    // Countless static objects sit exactly on the world axes; a camera practically never does.
    if (std::fabs(forward[0]) > 0.9999f || std::fabs(forward[1]) > 0.9999f || std::fabs(forward[2]) > 0.9999f)
        return false;
    for (int i = 0; i < 3; ++i)
        if (std::fabs(trans[i]) > 50000.0f)
            return false;
    return trans[0] != 0.0f || trans[1] != 0.0f || trans[2] != 0.0f;
}

struct Candidate {
    uintptr_t address;
    float start[3];
    float last[3];
    float lastForward[3];
    float minForward[3];
    float maxForward[3];
    float maxMoved;
    int misses;
    int teleports;  // jumps no camera makes: recycled projectile and effect slots do this
    bool tracked;
    int copies;     // identical survivors, including itself
};

void Scan(Block<Region>& regions, Block<uint8_t>& buffer, Block<Candidate>& out) {
    ForEachChunk(regions, buffer, [&](uintptr_t address, const uint8_t* data, size_t bytes) {
        for (size_t i = 0; i + 64 <= bytes; i += 16) {
            auto m = reinterpret_cast<const float*>(data + i);
            if (!LooksLikeCamera(m, 1e-3f, 0.05f))
                continue;
            Candidate c{};
            c.address = address + i;
            for (int k = 0; k < 3; ++k)
                c.minForward[k] = c.maxForward[k] = m[8 + k];
            if (!out.Push(c))
                return;
        }
    });
}

void Track(Block<Candidate>& candidates) {
    for (int sample = 0; sample < kTrackSamples; ++sample) {
        Sleep(100);
        for (Candidate& c : candidates) {
            if (c.misses > kMaxMisses || c.teleports)
                continue;
            float m[16];
            // Looser than the scan: camera shake from nearby fire adds a little roll.
            if (!camera::SafeRead(c.address, m, sizeof(m)) || !LooksLikeCamera(m, 1e-2f, 0.15f)) {
                ++c.misses;
                continue;
            }
            const float* forward = m + 8;
            const float* trans = m + 12;
            if (!c.tracked) {
                // Tracking starts here rather than at scan time: the scan itself takes seconds.
                std::memcpy(c.start, trans, sizeof(c.start));
                std::memcpy(c.last, trans, sizeof(c.last));
                std::memcpy(c.lastForward, forward, sizeof(c.lastForward));
                c.tracked = true;
            }
            float step[3] = {trans[0] - c.last[0], trans[1] - c.last[1], trans[2] - c.last[2]};
            if (std::sqrt(Dot3(step, step)) > kMaxStep || Dot3(forward, c.lastForward) < -0.5f) {
                ++c.teleports;
                continue;
            }
            std::memcpy(c.last, trans, sizeof(c.last));
            std::memcpy(c.lastForward, forward, sizeof(c.lastForward));
            for (int i = 0; i < 3; ++i) {
                c.minForward[i] = std::min(c.minForward[i], forward[i]);
                c.maxForward[i] = std::max(c.maxForward[i], forward[i]);
            }
            float d[3] = {trans[0] - c.start[0], trans[1] - c.start[1], trans[2] - c.start[2]};
            c.maxMoved = std::max(c.maxMoved, std::sqrt(Dot3(d, d)));
        }
    }
}

// Frostbite keeps several copies of the view (game camera, render view, ...). The player's
// camera tends to show up as a group of identical transforms; a lone projectile doesn't.
void CountCopies(std::vector<Candidate>& survivors) {
    std::vector<std::array<float, 16>> values(survivors.size());
    for (size_t i = 0; i < survivors.size(); ++i)
        camera::SafeRead(survivors[i].address, values[i].data(), sizeof(float) * 16);
    for (size_t i = 0; i < survivors.size(); ++i) {
        survivors[i].copies = 0;
        for (size_t j = 0; j < survivors.size(); ++j) {
            bool same = true;
            for (int k = 0; k < 16 && same; ++k)
                same = std::fabs(values[i][k] - values[j][k]) < 1e-3f;
            survivors[i].copies += same;
        }
    }
}

// Survivors moved like the player's view: looked up and down, turned, and walked, without teleporting.
// copies receives every member of the biggest group of identical survivors, for F6 copy testing.
std::vector<Candidate> Filter(Block<Candidate>& candidates, std::vector<uintptr_t>& copies) {
    size_t alive = 0, teleported = 0, lookedUpDown = 0, turned = 0, walked = 0;
    std::vector<Candidate> out;
    for (Candidate& c : candidates) {
        if (c.misses > kMaxMisses || !c.tracked)
            continue;
        if (c.teleports) {
            ++teleported;
            continue;
        }
        ++alive;
        bool pitch = c.maxForward[1] - c.minForward[1] > 0.3f;
        bool yaw = (c.maxForward[0] - c.minForward[0]) + (c.maxForward[2] - c.minForward[2]) > 0.6f;
        bool moved = c.maxMoved > 1.0f;
        lookedUpDown += pitch;
        turned += yaw;
        walked += moved;
        if (pitch && yaw && moved)
            out.push_back(c);
    }
    Log("Finder: %zu teleported (projectiles, effects); of %zu others, %zu looked up/down, %zu turned, %zu moved; "
        "%zu match all three",
        teleported, alive, lookedUpDown, turned, walked, out.size());
    CountCopies(out);
    std::stable_sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) {
        if (a.copies != b.copies)
            return a.copies > b.copies;
        return a.misses < b.misses;
    });
    copies.clear();
    for (const Candidate& c : out)
        if (c.copies == out[0].copies && copies.size() < kMaxCopies)
            copies.push_back(c.address);
    if (out.size() > kMaxSurvivors)
        out.resize(kMaxSurvivors);
    return out;
}

// One step of a pointer chain: [something] + offset reaches the parent link's address.
// Offsets only, so these can live on the normal heap.
struct Link {
    int parent;  // -1 for a camera itself
    uintptr_t offset;
    int camera;
};

struct Target {
    uintptr_t address;
    int link;
};

struct Hit {
    uintptr_t moduleOffset;
    int link;
};

std::vector<uintptr_t> ChainOffsets(const std::vector<Link>& links, int link) {
    std::vector<uintptr_t> offsets;
    for (int i = link; links[i].parent != -1; i = links[i].parent)
        offsets.push_back(links[i].offset);
    return offsets;
}

std::vector<Hit> FindPointers(const std::vector<Candidate>& cameras, Block<Region>& regions, Block<uint8_t>& buffer,
                              std::vector<Link>& links) {
    const Module module = GameModule();
    Block<Target> current(kMaxTargets), next(kMaxTargets);
    std::vector<Hit> hits;
    for (size_t i = 0; i < cameras.size(); ++i) {
        links.push_back({-1, 0, static_cast<int>(i)});
        current.Push({cameras[i].address, static_cast<int>(links.size() - 1)});
    }

    for (int depth = 1; depth <= kMaxDepth && current.size; ++depth) {
        std::sort(current.begin(), current.end(), [](const Target& a, const Target& b) { return a.address < b.address; });
        const uintptr_t lo = current.data[0].address - kMaxOffset;
        const uintptr_t hi = current.data[current.size - 1].address;
        next.size = 0;
        size_t levelHits = 0;

        ForEachChunk(regions, buffer, [&](uintptr_t address, const uint8_t* data, size_t bytes) {
            for (size_t i = 0; i + sizeof(uintptr_t) <= bytes; i += sizeof(uintptr_t)) {
                uintptr_t value = *reinterpret_cast<const uintptr_t*>(data + i);
                if (value < lo || value > hi || (value & 3))
                    continue;
                const Target* t = std::lower_bound(current.begin(), current.end(), value,
                                                   [](const Target& a, uintptr_t v) { return a.address < v; });
                for (; t != current.end() && t->address - value <= kMaxOffset; ++t) {
                    uintptr_t where = address + i;
                    links.push_back({t->link, t->address - value, links[t->link].camera});
                    int link = static_cast<int>(links.size() - 1);
                    if (module.Contains(where)) {
                        hits.push_back({where - module.base, link});
                        ++levelHits;
                    } else {
                        next.Push({where, link});
                    }
                }
            }
        });
        Log("Finder: pointer level %d: %zu static pointers, %zu heap pointers to follow", depth, levelHits, next.size);
        std::swap(current.data, next.data);
        std::swap(current.size, next.size);
        if (hits.size() >= 200 || links.size() > 4000000)
            break;
    }
    return hits;
}

struct Chain {
    config::CameraPointer pointer;
    int camera;
};

// Shortest chains first, then smallest offsets. Every chain is re-followed to make sure it
// really ends at its camera.
std::vector<Chain> RankChains(const std::vector<Hit>& hits, const std::vector<Link>& links,
                              const std::vector<Candidate>& cameras) {
    std::vector<Chain> chains;
    for (const Hit& h : hits) {
        Chain c{{h.moduleOffset, ChainOffsets(links, h.link)}, links[h.link].camera};
        uintptr_t resolved = 0;
        if (!camera::Resolve(c.pointer, resolved) || resolved != cameras[c.camera].address)
            continue;
        bool duplicate = std::any_of(chains.begin(), chains.end(), [&](const Chain& o) {
            return o.pointer.base == c.pointer.base && o.pointer.offsets == c.pointer.offsets;
        });
        if (!duplicate)
            chains.push_back(c);
    }
    auto offsetSum = [](const Chain& c) {
        uintptr_t s = 0;
        for (uintptr_t o : c.pointer.offsets)
            s += o;
        return s;
    };
    std::stable_sort(chains.begin(), chains.end(), [&](const Chain& a, const Chain& b) {
        if (a.pointer.offsets.size() != b.pointer.offsets.size())
            return a.pointer.offsets.size() < b.pointer.offsets.size();
        return offsetSum(a) < offsetSum(b);
    });
    return chains;
}

void LogChain(const Chain& c, int rank) {
    char offsets[128] = "";
    size_t len = 0;
    for (size_t i = 0; i < c.pointer.offsets.size(); ++i)
        len += sprintf_s(offsets + len, sizeof(offsets) - len, i ? ",%X" : "%X",
                         static_cast<unsigned>(c.pointer.offsets[i]));
    Log("Finder: #%d  base=%X  offsets=%s  (camera %d)", rank, static_cast<unsigned>(c.pointer.base), offsets,
        c.camera + 1);
}

void BeepStart() { Beep(880, 150); }
void BeepMoveNow() { for (int i = 0; i < 3; ++i) Beep(1200, 120), Sleep(80); }
void BeepWorking() { Beep(880, 150); }
void BeepSuccess() { Beep(660, 120), Beep(880, 120), Beep(1100, 250); }
void BeepFailure() { Beep(300, 700); }

// The scans must not see this thread's stack or the mod DLL.
void SkipOwnMemory() {
    int stackMarker = 0;
    MEMORY_BASIC_INFORMATION mbi;
    VirtualQuery(&stackMarker, &mbi, sizeof(mbi));
    g_skip.push_back(reinterpret_cast<uintptr_t>(mbi.AllocationBase));
    VirtualQuery(reinterpret_cast<void*>(&SkipOwnMemory), &mbi, sizeof(mbi));
    g_skip.push_back(reinterpret_cast<uintptr_t>(mbi.AllocationBase));
}

void Run() {
    SkipOwnMemory();
    BeepStart();
    Log("Finder: scanning memory for camera-shaped transforms...");
    Block<Region> regions(kMaxRegions);
    Block<uint8_t> buffer(kChunkSize);
    Block<Candidate> candidates(kMaxCandidates);
    if (!regions.data || !buffer.data || !candidates.data) {
        Log("Finder: out of memory");
        BeepFailure();
        return;
    }
    CollectRegions(regions);
    Scan(regions, buffer, candidates);
    Log("Finder: %zu candidates in %zu regions%s", candidates.size, regions.size,
        candidates.size == candidates.capacity ? " (hit the limit)" : "");
    if (!candidates.size) {
        BeepFailure();
        return;
    }

    BeepMoveNow();
    Log("Finder: tracking for 20 s. Look up and down, turn around, and walk.");
    Track(candidates);
    BeepWorking();

    std::vector<uintptr_t> copies;
    std::vector<Candidate> cameras = Filter(candidates, copies);
    camera::SetTestCopies(copies);
    if (!copies.empty())
        Log("Finder: %zu identical copies kept. F6 = hunt for the cull camera (one copy every few "
            "seconds), F5 = save the one that brings the missing scenery back.",
            copies.size());
    if (cameras.empty()) {
        Log("Finder: nothing moved like the camera. Run it again and look up/down, turn and walk the whole 20 s.");
        BeepFailure();
        return;
    }
    for (size_t i = 0; i < cameras.size(); ++i) {
        float m[16] = {};
        camera::SafeRead(cameras[i].address, m, sizeof(m));
        Log("Finder: camera %zu at %08X  pos (%.2f, %.2f, %.2f)  moved %.1f m  copies %d  misses %d", i + 1,
            static_cast<unsigned>(cameras[i].address), m[12], m[13], m[14], cameras[i].maxMoved, cameras[i].copies,
            cameras[i].misses);
    }

    Log("Finder: searching for pointers from bf3.exe...");
    CollectRegions(regions);
    std::vector<Link> links;
    std::vector<Hit> hits = FindPointers(cameras, regions, buffer, links);
    std::vector<Chain> chains = RankChains(hits, links, cameras);
    if (chains.empty()) {
        Log("Finder: found the camera but no static pointer to it. Use CAMERA_GUIDE.md step 3 with the address above.");
        BeepFailure();
        return;
    }

    for (size_t i = 0; i < chains.size() && i < 15; ++i)
        LogChain(chains[i], static_cast<int>(i + 1));
    // Pointers already in bf3vr.ini were usually confirmed with the copy test; never replace them unasked.
    if (!config::GetCameras().empty()) {
        Log("Finder: kept the existing bf3vr.ini pointers. Use F6/F11/F5 to test and save copies instead.");
    } else {
        config::SetCamera(chains[0].pointer);
        Log("Finder: saved #1 to bf3vr.ini. Restart the level and check the 'Camera probe' lines still follow you.");
    }
    BeepSuccess();
}

// ---- Look-angle finder (F3) ----
// BF3 keeps the player's yaw and pitch as plain floats and builds the camera from them every
// frame. The finder reads the game camera's forward vector, works out what the angles would be
// in every plausible convention (which axes, which signs, radians or degrees), and keeps the
// floats that go on matching one convention while the player moves the mouse around.

constexpr size_t kMaxAngleCandidates = 400000;
constexpr int kAngleSamples = 200;  // 10 s at 20 Hz
constexpr int kMaxAngleMisses = 40;
constexpr size_t kMaxAngleTests = 40;  // per kind; each write test takes 150 ms
constexpr float kTwoPi = 6.28318531f;
constexpr float kRadToDeg = 57.2957795f;

// 0-7: yaw = atan2 of the forward vector's horizontal parts, in each order and sign combination.
// 8-9: pitch = +-asin(forward.y). +16: stored in degrees instead of radians.
constexpr int kEncodings[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};
constexpr int kEncodingCount = sizeof(kEncodings) / sizeof(kEncodings[0]);

bool IsYawEncoding(int e) {
    return (e & 15) < 8;
}

bool IsDegrees(int e) {
    return (e & 16) != 0;
}

float EncodedAngle(int e, const float* forward) {
    int kind = e & 15;
    float a;
    if (kind < 8) {
        float x = (kind & 1) ? -forward[0] : forward[0];
        float z = (kind & 2) ? -forward[2] : forward[2];
        a = (kind & 4) ? std::atan2(z, x) : std::atan2(x, z);
    } else {
        a = std::asin(std::clamp(forward[1], -1.0f, 1.0f));
        if (kind == 9)
            a = -a;
    }
    return IsDegrees(e) ? a * kRadToDeg : a;
}

// Difference between a stored value and an angle, in radians. Yaw may carry any number of whole
// turns (some games let it run up as the player spins), so it's compared modulo one turn.
float AngleError(int e, float value, float target) {
    float d = value - target;
    if (IsYawEncoding(e)) {
        float period = IsDegrees(e) ? 360.0f : kTwoPi;
        d -= period * std::nearbyint(d / period);
    }
    float err = std::fabs(d);
    return IsDegrees(e) ? err / kRadToDeg : err;
}

struct AngleCandidate {
    uintptr_t address;
    int encoding;
    int misses;
    float last;
    float travel;  // radians moved while tracking: a constant that happens to match isn't an angle
};

bool ReadForward(const config::CameraPointer& pointer, float* forward) {
    uintptr_t address = 0;
    camera::Transform t;
    if (!camera::Resolve(pointer, address) || !camera::SafeRead(address, &t, sizeof(t)))
        return false;
    std::memcpy(forward, t.forward, 3 * sizeof(float));
    return std::fabs(Dot3(forward, forward) - 1.0f) < 0.05f;
}

void ScanAngles(Block<Region>& regions, Block<uint8_t>& buffer, const float* forward, bool withPitch,
                Block<AngleCandidate>& out) {
    float targets[kEncodingCount];
    for (int k = 0; k < kEncodingCount; ++k)
        targets[k] = EncodedAngle(kEncodings[k], forward);
    ForEachChunk(regions, buffer, [&](uintptr_t address, const uint8_t* data, size_t bytes) {
        for (size_t i = 0; i + 4 <= bytes; i += 4) {
            float v = *reinterpret_cast<const float*>(data + i);
            float magnitude = std::fabs(v);
            if (!(magnitude > 1e-4f && magnitude < 1000.0f))  // also rejects NaN
                continue;
            for (int k = 0; k < kEncodingCount; ++k) {
                int e = kEncodings[k];
                if (!withPitch && !IsYawEncoding(e))
                    continue;
                if (AngleError(e, v, targets[k]) < 0.002f) {
                    if (!out.Push({address + i, e, 0, v, 0.0f}))
                        return;
                    break;
                }
            }
        }
    });
}

void TrackAngles(const config::CameraPointer& gameCamera, Block<AngleCandidate>& candidates) {
    for (int sample = 0; sample < kAngleSamples; ++sample) {
        Sleep(50);
        float forward[3];
        if (!ReadForward(gameCamera, forward))
            continue;
        for (AngleCandidate& c : candidates) {
            if (c.misses > kMaxAngleMisses)
                continue;
            float v = 0.0f;
            // The camera is rebuilt from the angles once per frame, so allow a frame of lag.
            if (!camera::SafeRead(c.address, &v, sizeof(v)) ||
                AngleError(c.encoding, v, EncodedAngle(c.encoding, forward)) > 0.05f) {
                ++c.misses;
                continue;
            }
            c.travel += AngleError(c.encoding, v, c.last);
            c.last = v;
        }
    }
}

const char* EncodingName(int e) {
    static const char* kinds[] = {"atan2(x,z)",   "atan2(-x,z)",  "atan2(x,-z)", "atan2(-x,-z)",
                                  "atan2(z,x)",   "atan2(z,-x)",  "atan2(-z,x)", "atan2(-z,-x)",
                                  "asin(y)",      "-asin(y)"};
    return kinds[e & 15];
}

// Every candidate of one kind (yaw or pitch) that followed the mouse: fewest misses first.
std::vector<AngleCandidate> TrackedAngles(Block<AngleCandidate>& candidates, bool yaw) {
    std::vector<AngleCandidate> out;
    for (const AngleCandidate& c : candidates)
        if (IsYawEncoding(c.encoding) == yaw && c.misses <= kMaxAngleMisses && c.travel > 1.0f)
            out.push_back(c);
    std::sort(out.begin(), out.end(), [](const AngleCandidate& a, const AngleCandidate& b) {
        if (a.misses != b.misses)
            return a.misses < b.misses;
        return a.travel > b.travel;
    });
    if (out.size() > kMaxAngleTests)
        out.resize(kMaxAngleTests);
    return out;
}

bool SafeWriteFloat(uintptr_t address, float value) {
    __try {
        *reinterpret_cast<float*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// BF3 keeps dozens of copies of the look angles and rebuilds them from the real pair every
// frame. Nudge the value and see whether it sticks: a copy snaps back, the real angle keeps it
// (the view turns slightly for a moment). The nudge is undone either way.
bool KeepsWrites(const AngleCandidate& c) {
    float delta = IsDegrees(c.encoding) ? 10.0f : 0.17f;
    float before = 0.0f, after = 0.0f;
    if (!camera::SafeRead(c.address, &before, sizeof(before)) || !SafeWriteFloat(c.address, before + delta))
        return false;
    Sleep(150);
    if (!camera::SafeRead(c.address, &after, sizeof(after)))
        return false;
    bool kept = std::fabs(after - (before + delta)) < delta * 0.3f;
    if (kept)
        SafeWriteFloat(c.address, after - delta);
    return kept;
}

// The real yaw and pitch sit side by side, but only one of them may have made the shortlist.
// Look around a real angle for a value of the other kind that matches the camera right now.
void AddNeighbours(const AngleCandidate& found, const config::CameraPointer& gameCamera,
                   std::vector<AngleCandidate>& out) {
    float forward[3];
    if (!ReadForward(gameCamera, forward))
        return;
    bool wantYaw = !IsYawEncoding(found.encoding);
    for (int offset = -0x40; offset <= 0x40; offset += 4) {
        uintptr_t address = found.address + offset;
        float v = 0.0f;
        if (!offset || !camera::SafeRead(address, &v, sizeof(v)) || !std::isfinite(v) || v == 0.0f)
            continue;
        for (int e : kEncodings) {
            if (IsYawEncoding(e) == wantYaw && AngleError(e, v, EncodedAngle(e, forward)) < 0.01f) {
                out.push_back({address, e, 0, v, 0.0f});
                break;
            }
        }
    }
}

// Writes test every tracked candidate and returns the real ones (yaw first in the list order).
std::vector<AngleCandidate> RealAngles(Block<AngleCandidate>& candidates, const config::CameraPointer& gameCamera) {
    std::vector<AngleCandidate> tests = TrackedAngles(candidates, true);
    std::vector<AngleCandidate> pitches = TrackedAngles(candidates, false);
    tests.insert(tests.end(), pitches.begin(), pitches.end());

    std::vector<AngleCandidate> real;
    for (size_t i = 0; i < tests.size(); ++i) {
        const AngleCandidate c = tests[i];
        bool already = std::any_of(real.begin(), real.end(), [&](const AngleCandidate& r) { return r.address == c.address; });
        if (already || !KeepsWrites(c))
            continue;
        Log("Angle finder: real %s at %08X  %s %s", IsYawEncoding(c.encoding) ? "yaw" : "pitch",
            static_cast<unsigned>(c.address), EncodingName(c.encoding), IsDegrees(c.encoding) ? "deg" : "rad");
        real.push_back(c);
        // Its partner may not have made the shortlist; queue its neighbours for testing too.
        AddNeighbours(c, gameCamera, tests);
    }
    Log("Angle finder: %zu of %zu candidates kept a test write", real.size(), tests.size());
    return real;
}

// The player's yaw and pitch usually sit side by side in one object; prefer such a pair.
void PickPair(const std::vector<AngleCandidate>& yaws, const std::vector<AngleCandidate>& pitches,
              const AngleCandidate*& yaw, const AngleCandidate*& pitch) {
    yaw = yaws.empty() ? nullptr : &yaws[0];
    pitch = pitches.empty() ? nullptr : &pitches[0];
    uintptr_t best = 0x200;
    for (const AngleCandidate& y : yaws)
        for (const AngleCandidate& p : pitches) {
            uintptr_t gap = y.address > p.address ? y.address - p.address : p.address - y.address;
            if (gap < best)
                best = gap, yaw = &y, pitch = &p;
        }
}

void RunAngles() {
    SkipOwnMemory();
    config::Aim aim = config::GetAim();
    float forward[3];
    if (!ReadForward(aim.gameCamera, forward)) {
        Log("Angle finder: can't read the game camera ([aim] game_camera in bf3vr.ini)");
        BeepFailure();
        return;
    }
    // Looking level, pitch is ~0 and would match millions of zeros; leave it for another run.
    bool withPitch = std::fabs(forward[1]) > 0.1f;
    if (!withPitch)
        Log("Angle finder: looking almost level, so only yaw can be found this time. "
            "Look a little up or down when pressing F3 to find pitch too.");

    BeepStart();
    Log("Angle finder: hold still while memory is scanned...");
    Block<Region> regions(kMaxRegions);
    Block<uint8_t> buffer(kChunkSize);
    Block<AngleCandidate> candidates(kMaxAngleCandidates);
    if (!regions.data || !buffer.data || !candidates.data) {
        Log("Angle finder: out of memory");
        BeepFailure();
        return;
    }
    CollectRegions(regions);
    ScanAngles(regions, buffer, forward, withPitch, candidates);
    Log("Angle finder: %zu candidates%s", candidates.size,
        candidates.size == candidates.capacity ? " (hit the limit)" : "");
    if (!candidates.size) {
        BeepFailure();
        return;
    }

    BeepMoveNow();
    Log("Angle finder: tracking for 10 s. Move the mouse around: left, right, up and down.");
    TrackAngles(aim.gameCamera, candidates);
    BeepWorking();

    Log("Angle finder: hold the mouse still; test-writing candidates (the view may twitch)...");
    std::vector<AngleCandidate> real = RealAngles(candidates, aim.gameCamera);
    std::vector<AngleCandidate> yaws, pitches;
    for (const AngleCandidate& c : real)
        (IsYawEncoding(c.encoding) ? yaws : pitches).push_back(c);
    const AngleCandidate* yaw = nullptr;
    const AngleCandidate* pitch = nullptr;
    PickPair(yaws, pitches, yaw, pitch);
    if (!yaw && !pitch) {
        Log("Angle finder: nothing followed the mouse and kept a test write. Run it again, keep moving the mouse "
            "for the whole 10 s, then hold it still.");
        BeepFailure();
        return;
    }

    std::vector<Candidate> found;
    for (const AngleCandidate* a : {yaw, pitch}) {
        if (!a)
            continue;
        Candidate c{};
        c.address = a->address;
        found.push_back(c);
    }
    CollectRegions(regions);
    std::vector<Link> links;
    std::vector<Hit> hits = FindPointers(found, regions, buffer, links);
    std::vector<Chain> chains = RankChains(hits, links, found);

    // Chains are ranked best first, so the first one per angle is that angle's best.
    auto bestFor = [&](uintptr_t address) -> const Chain* {
        for (const Chain& c : chains)
            if (found[c.camera].address == address)
                return &c;
        return nullptr;
    };
    bool saved = false;
    if (yaw) {
        if (const Chain* c = bestFor(yaw->address)) {
            aim.yaw = c->pointer;
            aim.yawDegrees = IsDegrees(yaw->encoding);
            // Confirmed in game for atan2(x,-z): BF3's yaw grows turning right, head yaw turning left.
            if ((yaw->encoding & 15) == 2)
                aim.invertYaw = true;
            LogChain(*c, 1);
            saved = true;
        } else {
            Log("Angle finder: found yaw at %08X but no static pointer to it", static_cast<unsigned>(yaw->address));
        }
    }
    if (pitch) {
        if (const Chain* c = bestFor(pitch->address)) {
            aim.pitch = c->pointer;
            aim.pitchDegrees = IsDegrees(pitch->encoding);
            // BF3's camera forward row points backwards, so -asin(forward.y) grows as the player
            // looks up, like head pitch; plain asin(forward.y) is the opposite.
            aim.invertPitch = (pitch->encoding & 15) == 8;
            LogChain(*c, 2);
            saved = true;
        } else {
            Log("Angle finder: found pitch at %08X but no static pointer to it",
                static_cast<unsigned>(pitch->address));
        }
    }
    if (!saved) {
        BeepFailure();
        return;
    }
    config::SetAim(aim);
    Log("Angle finder: saved to bf3vr.ini (yaw %s, pitch %s)", aim.yaw.base ? "set" : "missing",
        aim.pitch.base ? "set" : "missing");
    BeepSuccess();
}

// F5 during the cull hunt: the copy being tried made the missing scenery come back. Stop the
// hunt there and save a pointer to it as the cull camera.
void RunSaveCull() {
    SkipOwnMemory();
    uintptr_t address = camera::HuntCandidate();
    camera::StopHunt();
    if (!address) {
        Log("Cull hunt: press F6 to start the hunt before F5");
        BeepFailure();
        return;
    }
    BeepStart();
    Log("Cull hunt: searching for a pointer to %08X...", static_cast<unsigned>(address));

    Candidate c{};
    c.address = address;
    std::vector<Candidate> cameras{c};
    Block<Region> regions(kMaxRegions);
    Block<uint8_t> buffer(kChunkSize);
    if (!regions.data || !buffer.data) {
        Log("Cull hunt: out of memory");
        BeepFailure();
        return;
    }
    CollectRegions(regions);
    std::vector<Link> links;
    std::vector<Hit> hits = FindPointers(cameras, regions, buffer, links);
    std::vector<Chain> chains = RankChains(hits, links, cameras);
    if (chains.empty()) {
        Log("Cull hunt: no static pointer to that copy");
        BeepFailure();
        return;
    }
    LogChain(chains[0], 1);
    config::SetCull(chains[0].pointer);
    Log("Cull hunt: saved as the cull camera ([camera] cull_base in bf3vr.ini)");
    BeepSuccess();
}

// ---- FOV finder (Home, three times) ----
// The player moves BF3's FOV slider 90 -> 60 -> 90, pressing Home at each stop. Values that read
// as 90, then 60, then 90 again (in any of a few plausible encodings) are the setting.

constexpr size_t kMaxFovCandidates = 400000;

struct FovCandidate {
    uintptr_t address;
    int encoding;
};

// Kept between the three key presses. Holds raw game addresses, so it's a Block the scans skip.
Block<FovCandidate>* g_fovCandidates = nullptr;
std::atomic<int> g_fovStep{0};  // 0 = idle, 1 = scanned at 90, 2 = filtered at 60

void FovFirstScan() {
    SkipOwnMemory();
    delete g_fovCandidates;
    g_fovCandidates = new Block<FovCandidate>(kMaxFovCandidates);
    Block<Region> regions(kMaxRegions);
    Block<uint8_t> buffer(kChunkSize);
    if (!g_fovCandidates->data || !regions.data || !buffer.data) {
        Log("FOV finder: out of memory");
        BeepFailure();
        return;
    }
    CollectRegions(regions);
    // What each float encoding stores at 90, worked out once rather than per value (the same
    // slack as fov::Matches). The one integer encoding is plain 90.
    struct FloatTarget {
        int encoding;
        float value;
        float slack;
    };
    std::vector<FloatTarget> targets;
    for (int e = 0; e < fov::kEncodingSpace; ++e) {
        if (!fov::Valid(e) || e % 8 == 6)
            continue;
        uint32_t bits = fov::Encode(e, 90.0f);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        // tan(45) = 1.0: memory is full of 1.0s, which would crowd out everything else.
        if (std::fabs(value - 1.0f) < 1e-3f)
            continue;
        targets.push_back({e, value, 2e-4f * std::fmax(1.0f, std::fabs(value))});
    }
    ForEachChunk(regions, buffer, [&](uintptr_t address, const uint8_t* data, size_t bytes) {
        for (size_t i = 0; i + 4 <= bytes; i += 4) {
            uint32_t bits = *reinterpret_cast<const uint32_t*>(data + i);
            if (bits == 90) {
                if (!g_fovCandidates->Push({address + i, 6}))
                    return;
                continue;
            }
            float v;
            std::memcpy(&v, &bits, sizeof(v));
            float magnitude = std::fabs(v);
            if (!(magnitude > 0.1f && magnitude < 200.0f))  // every float target lies in here; rejects NaN
                continue;
            for (const FloatTarget& t : targets) {
                if (std::fabs(v - t.value) <= t.slack) {
                    if (!g_fovCandidates->Push({address + i, t.encoding}))
                        return;
                    break;
                }
            }
        }
    });
    Log("FOV finder: %zu values read as 90. Now set the FOV slider to 60 and press Home.", g_fovCandidates->size);
    g_fovStep = 1;
    BeepStart();
}

// Keeps the candidates that now read as `degrees`.
size_t FovFilter(float degrees) {
    size_t kept = 0;
    for (FovCandidate& c : *g_fovCandidates) {
        uint32_t v = 0;
        if (camera::SafeRead(c.address, &v, sizeof(v)) && fov::Matches(c.encoding, v, degrees))
            g_fovCandidates->data[kept++] = c;
    }
    g_fovCandidates->size = kept;
    return kept;
}

void FovSecondStep() {
    size_t kept = FovFilter(60.0f);
    Log("FOV finder: %zu of them now read as 60. Set the slider back to 90 and press Home.", kept);
    if (!kept) {
        g_fovStep = 0;
        BeepFailure();
        return;
    }
    g_fovStep = 2;
    for (int i = 0; i < 2; ++i)
        Beep(1000, 80), Sleep(60);
}

void FovThirdStep() {
    SkipOwnMemory();
    size_t kept = FovFilter(90.0f);
    Log("FOV finder: %zu followed the slider 90 -> 60 -> 90", kept);
    // Move the survivors out of the Block (there are few) and free it before the pointer search.
    std::vector<Candidate> found;
    std::vector<int> encodings;
    for (const FovCandidate& c : *g_fovCandidates) {
        if (found.size() == config::kMaxFovTargets)
            break;
        Candidate candidate{};
        candidate.address = c.address;
        found.push_back(candidate);
        encodings.push_back(c.encoding);
        Log("FOV finder: setting at %08X (encoding %d)", static_cast<unsigned>(c.address), c.encoding);
    }
    delete g_fovCandidates;
    g_fovCandidates = nullptr;
    g_fovStep = 0;
    if (found.empty()) {
        Log("FOV finder: nothing followed the slider. Press Home to start again at 90.");
        BeepFailure();
        return;
    }

    BeepWorking();
    Block<Region> regions(kMaxRegions);
    Block<uint8_t> buffer(kChunkSize);
    if (!regions.data || !buffer.data) {
        BeepFailure();
        return;
    }
    CollectRegions(regions);
    std::vector<Link> links;
    std::vector<Hit> hits = FindPointers(found, regions, buffer, links);
    std::vector<Chain> chains = RankChains(hits, links, found);

    config::Fov f = config::GetFov();
    f.targets.clear();
    for (size_t i = 0; i < found.size(); ++i) {
        auto it = std::find_if(chains.begin(), chains.end(),
                               [&](const Chain& c) { return c.camera == static_cast<int>(i); });
        if (it == chains.end())
            continue;
        LogChain(*it, static_cast<int>(f.targets.size() + 1));
        f.targets.push_back({it->pointer, encodings[i]});
    }
    if (f.targets.empty()) {
        Log("FOV finder: found the setting but no static pointer to it");
        BeepFailure();
        return;
    }
    config::SetFov(f);
    Log("FOV finder: saved %zu targets; the FOV is now set to %.0f (End changes it)", f.targets.size(), f.value);
    BeepSuccess();
}

void RunFovStep() {
    switch (g_fovStep.load()) {
    case 0: FovFirstScan(); break;
    case 1: FovSecondStep(); break;
    default: FovThirdStep(); break;
    }
}

DWORD WINAPI Thread(LPVOID job) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    reinterpret_cast<void (*)()>(job)();
    g_skip.clear();
    g_busy = false;
    return 0;
}

void StartJob(void (*job)()) {
    if (g_busy.exchange(true))
        return;
    if (HANDLE t = CreateThread(nullptr, 0, Thread, reinterpret_cast<LPVOID>(job), 0, nullptr))
        CloseHandle(t);
    else
        g_busy = false;
}

}

namespace finder {

void Start() {
    StartJob(Run);
}

void SaveCull() {
    StartJob(RunSaveCull);
}

void StartAngles() {
    StartJob(RunAngles);
}

void FovStep() {
    StartJob(RunFovStep);
}

bool FovFinderActive() {
    return g_fovStep.load() != 0;
}

}
