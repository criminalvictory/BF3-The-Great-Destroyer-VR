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
#include <map>
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

// ---- Skeleton finder (F12), research only: reads and logs, never writes ----
// Animated skeletons are stored as rows of bone records back to back. The first-person arms
// should be a row whose bones move or turn while the player reloads or aims. BF3's format isn't
// known, so several common ones are tried:
//   0: 64-byte LinearTransform (right, up, forward, translation rows), like the cameras
//   1: 32-byte QuatTransform, translation + scale first, then the rotation quaternion
//   2: 32-byte QuatTransform, rotation quaternion first, then translation + scale
//   3: 48-byte 3x4 matrix, rows of (axis x, y, z, translation component), as fed to GPUs
// Bones relative to their parent barely change position while animating, only rotation, so
// rotation counts as activity too.

constexpr size_t kMaxBoneRuns = 20000;
constexpr int kMinBones = 12;
constexpr int kMaxBones = 256;
constexpr int kBoneSamples = 100;  // 10 s at 10 Hz
constexpr int kBoneFormats = 4;
constexpr size_t kBoneStride[kBoneFormats] = {64, 32, 32, 48};
const char* const kBoneFormatName[kBoneFormats] = {"matrix 4x4", "quat (trans first)", "quat (rot first)",
                                                   "matrix 3x4"};

// One bone's position and an orientation signature (where its local z axis points).
struct BonePose {
    float pos[3];
    float axis[3];
};

bool Finite(const float* v, int n) {
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(v[i]) || std::fabs(v[i]) > 1e5f)
            return false;
    return true;
}

bool UnitRows(const float* a, const float* b, const float* c) {
    for (const float* v : {a, b, c})
        if (std::fabs(Dot3(v, v) - 1.0f) > 0.02f)
            return false;
    return std::fabs(Dot3(a, b)) < 0.02f && std::fabs(Dot3(a, c)) < 0.02f && std::fabs(Dot3(b, c)) < 0.02f;
}

// Reads one bone in a format; false if the bytes don't look like a bone in it.
bool ReadBone(int format, const float* m, BonePose& out) {
    switch (format) {
    case 0:
        if (m[3] != 0.0f || m[7] != 0.0f || m[11] != 0.0f || (m[15] != 1.0f && m[15] != 0.0f) || !Finite(m, 15) ||
            !UnitRows(m, m + 4, m + 8))
            return false;
        std::memcpy(out.pos, m + 12, sizeof(out.pos));
        std::memcpy(out.axis, m + 8, sizeof(out.axis));
        return true;
    case 1:
    case 2: {
        const float* t = format == 1 ? m : m + 4;
        const float* q = format == 1 ? m + 4 : m;
        if (!Finite(m, 8) || std::fabs(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3] - 1.0f) > 0.01f ||
            t[3] < 0.5f || t[3] > 2.0f)
            return false;
        std::memcpy(out.pos, t, sizeof(out.pos));
        // z axis rotated by q
        out.axis[0] = 2 * (q[0] * q[2] + q[3] * q[1]);
        out.axis[1] = 2 * (q[1] * q[2] - q[3] * q[0]);
        out.axis[2] = 1 - 2 * (q[0] * q[0] + q[1] * q[1]);
        return true;
    }
    default: {
        float r0[3] = {m[0], m[1], m[2]}, r1[3] = {m[4], m[5], m[6]}, r2[3] = {m[8], m[9], m[10]};
        if (!Finite(m, 12) || !UnitRows(r0, r1, r2))
            return false;
        out.pos[0] = m[3], out.pos[1] = m[7], out.pos[2] = m[11];
        std::memcpy(out.axis, r2, sizeof(out.axis));
        return true;
    }
    }
}

bool IsIdentityish(const BonePose& p) {
    return Dot3(p.pos, p.pos) < 1e-8f && std::fabs(p.axis[2] - 1.0f) < 1e-5f;
}

struct BoneRun {
    uintptr_t address;
    int format;
    int count;
    bool local;       // positions near the origin (model or parent space) rather than near the camera
    int valid;        // samples where the whole run still looked like bones
    int activeBones;  // bones that moved or turned relative to the camera while watched
    float maxMove;    // largest movement of any bone relative to the camera, metres
    float maxTurn;    // largest turn of any bone's axis, radians
    float distance;   // mean distance of the bones from the camera (world runs) or origin (local)
};

float Distance3(const float* a, const float* b) {
    float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return std::sqrt(Dot3(d, d));
}

bool ReadRun(const BoneRun& run, std::vector<BonePose>& out) {
    size_t stride = kBoneStride[run.format];
    std::vector<uint8_t> raw(stride * run.count);
    if (!camera::SafeRead(run.address, raw.data(), raw.size()))
        return false;
    out.resize(run.count);
    for (int b = 0; b < run.count; ++b)
        if (!ReadBone(run.format, reinterpret_cast<const float*>(raw.data() + b * stride), out[b]))
            return false;
    return true;
}

void ScanBones(Block<Region>& regions, Block<uint8_t>& buffer, const float* camera, Block<BoneRun>& out) {
    const float origin[3] = {0, 0, 0};
    ForEachChunk(regions, buffer, [&](uintptr_t address, const uint8_t* data, size_t bytes) {
        for (size_t i = 0; i + 32 <= bytes;) {
            size_t advance = 16;
            for (int format = 0; format < kBoneFormats; ++format) {
                size_t stride = kBoneStride[format];
                int count = 0, identities = 0;
                BonePose pose;
                float nearCamera = 1e9f, farFromOrigin = 0.0f, sumCamera = 0.0f, sumOrigin = 0.0f;
                while (count < kMaxBones && i + (count + 1) * stride <= bytes &&
                       ReadBone(format, reinterpret_cast<const float*>(data + i + count * stride), pose)) {
                    identities += IsIdentityish(pose);
                    float dc = Distance3(pose.pos, camera), d0 = Distance3(pose.pos, origin);
                    nearCamera = std::min(nearCamera, dc);
                    farFromOrigin = std::max(farFromOrigin, d0);
                    sumCamera += dc;
                    sumOrigin += d0;
                    ++count;
                }
                // Empty pools are rows of identity transforms; a skeleton is mostly real bones.
                if (count < kMinBones || identities * 10 > count * 7)
                    continue;
                bool world = nearCamera < 3.0f;
                bool local = !world && farFromOrigin < 3.0f;
                if (world || local) {
                    BoneRun run{address + i, format, count, local, 0, 0, 0.0f, 0.0f,
                                (local ? sumOrigin : sumCamera) / count};
                    if (!out.Push(run))
                        return;
                }
                advance = std::max(advance, count * stride);
                break;
            }
            i += advance;
        }
    });
}

// Watches each run while the player reloads and aims. Movement is measured relative to the
// camera, so walking or turning doesn't count; only the arms' own animation does.
void TrackBones(Block<BoneRun>& runs) {
    std::vector<std::vector<BonePose>> first(runs.size);
    std::vector<std::vector<uint8_t>> active(runs.size);
    std::vector<BonePose> pose;
    for (int sample = 0; sample < kBoneSamples; ++sample) {
        Sleep(100);
        camera::Transform cam;
        if (!camera::Read(cam))
            continue;
        for (size_t r = 0; r < runs.size; ++r) {
            BoneRun& run = runs.data[r];
            if (!ReadRun(run, pose))
                continue;
            ++run.valid;
            if (!run.local)
                for (BonePose& p : pose)
                    for (int k = 0; k < 3; ++k)
                        p.pos[k] -= cam.trans[k];
            if (first[r].empty()) {
                first[r] = pose;
                active[r].assign(run.count, 0);
                continue;
            }
            for (int b = 0; b < run.count; ++b) {
                float move = Distance3(pose[b].pos, first[r][b].pos);
                float turn = std::acos(std::clamp(Dot3(pose[b].axis, first[r][b].axis), -1.0f, 1.0f));
                run.maxMove = std::max(run.maxMove, move);
                run.maxTurn = std::max(run.maxTurn, turn);
                if (move > 0.02f || turn > 0.1f)
                    active[r][b] = 1;
            }
        }
    }
    for (size_t r = 0; r < runs.size; ++r)
        for (uint8_t a : active[r])
            runs.data[r].activeBones += a;
}

// Writes each bone of the best runs to skeleton.csv next to the DLL, so the hands and arms can
// be picked out. World runs are given relative to the camera in its own axes.
void DumpBones(const std::vector<BoneRun>& best) {
    camera::Transform cam;
    if (!camera::Read(cam))
        return;
    wchar_t path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&DumpBones), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\'))
        wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"skeleton.csv");
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"w") != 0 || !f)
        return;
    fprintf(f, "run,address,format,space,bone,right,up,forward,x,y,z\n");
    const float forward[3] = {-cam.forward[0], -cam.forward[1], -cam.forward[2]};
    std::vector<BonePose> pose;
    for (size_t r = 0; r < best.size(); ++r) {
        const BoneRun& run = best[r];
        if (!ReadRun(run, pose))
            continue;
        for (int b = 0; b < run.count; ++b) {
            const float* t = pose[b].pos;
            float rel[3] = {t[0] - cam.trans[0], t[1] - cam.trans[1], t[2] - cam.trans[2]};
            if (run.local)
                rel[0] = t[0], rel[1] = t[1], rel[2] = t[2];
            fprintf(f, "%zu,%08X,%d,%s,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n", r + 1, static_cast<unsigned>(run.address),
                    run.format, run.local ? "local" : "world", b, Dot3(rel, cam.right), Dot3(rel, cam.up),
                    Dot3(rel, forward), t[0], t[1], t[2]);
        }
    }
    fclose(f);
    Log("Skeleton finder: bone positions written to skeleton.csv");
}

// Logs the player's own bones: the stretch of the best row within arm's reach of the camera.
// (Writing them didn't move the arms drawn: this is the gameplay skeleton. See skin.cpp.)
void LogPlayerBones(const std::vector<BoneRun>& rows) {
    camera::Transform cam;
    if (!camera::Read(cam))
        return;
    std::vector<BonePose> pose;
    for (const BoneRun& run : rows) {
        if (run.local || run.format != 1 || !ReadRun(run, pose))
            continue;
        int first = -1, last = -1;
        for (int b = 0; b < run.count; ++b) {
            if (Distance3(pose[b].pos, cam.trans) < 1.2f) {
                if (first < 0)
                    first = b;
                last = b;
            }
        }
        if (first < 0)
            continue;
        Log("Skeleton finder: player's bones are %d-%d of %08X", first, last, static_cast<unsigned>(run.address));
        return;
    }
}

void RunSkeleton() {
    SkipOwnMemory();
    camera::Transform cam;
    if (!camera::Read(cam)) {
        Log("Skeleton finder: can't read the camera ([camera] base in bf3vr.ini)");
        BeepFailure();
        return;
    }
    BeepStart();
    Log("Skeleton finder: hold still while memory is scanned...");
    Block<Region> regions(kMaxRegions);
    Block<uint8_t> buffer(kChunkSize);
    Block<BoneRun> runs(kMaxBoneRuns);
    if (!regions.data || !buffer.data || !runs.data) {
        Log("Skeleton finder: out of memory");
        BeepFailure();
        return;
    }
    CollectRegions(regions);
    ScanBones(regions, buffer, cam.trans, runs);
    size_t perFormat[kBoneFormats] = {};
    for (const BoneRun& r : runs)
        ++perFormat[r.format];
    Log("Skeleton finder: %zu rows of bones (4x4: %zu, quat t-first: %zu, quat q-first: %zu, 3x4: %zu)%s",
        runs.size, perFormat[0], perFormat[1], perFormat[2], perFormat[3],
        runs.size == runs.capacity ? " (hit the limit)" : "");
    if (!runs.size) {
        BeepFailure();
        return;
    }

    BeepMoveNow();
    Log("Skeleton finder: watching for 10 s. Reload, aim down sights and swing the gun around.");
    TrackBones(runs);
    BeepWorking();

    // Arms: stayed valid, and many of their bones moved or turned, by arm-sized amounts.
    std::vector<BoneRun> best;
    for (const BoneRun& r : runs)
        if (r.valid >= kBoneSamples * 8 / 10 && r.activeBones >= 5 && r.maxMove < 2.0f)
            best.push_back(r);
    std::sort(best.begin(), best.end(),
              [](const BoneRun& a, const BoneRun& b) { return a.activeBones > b.activeBones; });
    Log("Skeleton finder: %zu rows had 5 or more bones move or turn", best.size());
    if (best.size() > 15)
        best.resize(15);
    for (size_t i = 0; i < best.size(); ++i)
        Log("Skeleton finder: #%zu at %08X  %s  %d bones (%d active)  %s  moved %.2f m  turned %.0f deg  "
            "distance %.2f m",
            i + 1, static_cast<unsigned>(best[i].address), kBoneFormatName[best[i].format], best[i].count,
            best[i].activeBones, best[i].local ? "local" : "near camera", best[i].maxMove,
            best[i].maxTurn * 57.3f, best[i].distance);
    if (best.empty()) {
        BeepFailure();
        return;
    }
    std::vector<BoneRun> dump(best.begin(), best.begin() + std::min<size_t>(best.size(), 5));
    DumpBones(dump);
    LogPlayerBones(dump);

    // Pointers to the top few, to see whether they can be found again after a restart.
    std::vector<Candidate> targets;
    for (const BoneRun& r : dump) {
        Candidate c{};
        c.address = r.address;
        targets.push_back(c);
    }
    CollectRegions(regions);
    std::vector<Link> links;
    std::vector<Hit> hits = FindPointers(targets, regions, buffer, links);
    std::vector<Chain> chains = RankChains(hits, links, targets);
    for (size_t i = 0; i < targets.size(); ++i) {
        auto it = std::find_if(chains.begin(), chains.end(),
                               [&](const Chain& c) { return c.camera == static_cast<int>(i); });
        if (it == chains.end())
            Log("Skeleton finder: no static pointer to #%zu", i + 1);
        else
            LogChain(*it, static_cast<int>(i + 1));
    }
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

// ---- Health finder (Home, three times) ----
// At full health the player's health reads 100. Values that read 100, then dropped after taking
// damage, then came back to 100 once health refilled, are the player's health.

constexpr size_t kMaxHealthCandidates = 400000;
constexpr float kFullHealth = 100.0f;

Block<uintptr_t>* g_healthCandidates = nullptr;
std::atomic<int> g_healthStep{0};  // 0 = idle, 1 = scanned at full health, 2 = filtered after damage

void HealthFirstScan() {
    SkipOwnMemory();
    delete g_healthCandidates;
    g_healthCandidates = new Block<uintptr_t>(kMaxHealthCandidates);
    Block<Region> regions(kMaxRegions);
    Block<uint8_t> buffer(kChunkSize);
    if (!g_healthCandidates->data || !regions.data || !buffer.data) {
        Log("Health finder: out of memory");
        BeepFailure();
        return;
    }
    CollectRegions(regions);
    ForEachChunk(regions, buffer, [&](uintptr_t address, const uint8_t* data, size_t bytes) {
        for (size_t i = 0; i + 4 <= bytes; i += 4)
            if (*reinterpret_cast<const float*>(data + i) == kFullHealth && !g_healthCandidates->Push(address + i))
                return;
    });
    Log("Health finder: %zu values read %.0f. Take a little damage, then press Home.", g_healthCandidates->size,
        kFullHealth);
    g_healthStep = 1;
    BeepStart();
}

// Keeps the candidates whose value passes the test.
template <class Test>
size_t HealthFilter(Test test) {
    size_t kept = 0;
    for (uintptr_t address : *g_healthCandidates) {
        float v = 0.0f;
        if (camera::SafeRead(address, &v, sizeof(v)) && test(v))
            g_healthCandidates->data[kept++] = address;
    }
    g_healthCandidates->size = kept;
    return kept;
}

void HealthSecondStep() {
    size_t kept = HealthFilter([](float v) { return v > 0.0f && v < kFullHealth; });
    Log("Health finder: %zu of them dropped. Wait for health to refill, then press Home.", kept);
    if (!kept) {
        g_healthStep = 0;
        BeepFailure();
        return;
    }
    g_healthStep = 2;
    for (int i = 0; i < 2; ++i)
        Beep(1000, 80), Sleep(60);
}

// Several values follow the player's health: the game's own and copies made from it (the screen's,
// for one). Keeping the first few kept only copies, an array 8 bytes apart, and the player still
// died. Writing the game's own value shows which it is: the copies follow it within a moment,
// while writing a copy changes nothing else. Sorts the candidates by how many followed.
std::vector<uintptr_t> RankBySource(const std::vector<uintptr_t>& candidates) {
    constexpr float kTest = 50.0f;
    std::vector<std::pair<int, uintptr_t>> ranked;
    for (uintptr_t address : candidates) {
        float v = 0.0f;
        if (!camera::SafeRead(address, &v, sizeof(v)) || v != kFullHealth || !SafeWriteFloat(address, kTest))
            continue;
        Sleep(400);
        int followed = 0;
        for (uintptr_t other : candidates) {
            float w = 0.0f;
            if (other != address && camera::SafeRead(other, &w, sizeof(w)) && std::fabs(w - kTest) < 1.0f)
                ++followed;
        }
        Log("Health finder: writing %08X moved %d other copies", static_cast<unsigned>(address), followed);
        ranked.push_back({followed, address});
        // Back to full; the copies catch up from the game's own value.
        for (uintptr_t other : candidates) {
            float w = 0.0f;
            if (camera::SafeRead(other, &w, sizeof(w)) && w > 0.0f && w < kFullHealth)
                SafeWriteFloat(other, kFullHealth);
        }
        Sleep(300);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<uintptr_t> out;
    for (const auto& [followed, address] : ranked)
        out.push_back(address);
    return out;
}

void HealthThirdStep() {
    SkipOwnMemory();
    size_t kept = HealthFilter([](float v) { return v == kFullHealth; });
    Log("Health finder: %zu went 100 -> lower -> 100", kept);
    std::vector<uintptr_t> candidates;
    for (uintptr_t address : *g_healthCandidates) {
        if (candidates.size() == 64)
            break;
        candidates.push_back(address);
        Log("Health finder: health at %08X", static_cast<unsigned>(address));
    }
    std::vector<Candidate> found;
    for (uintptr_t address : RankBySource(candidates)) {
        if (found.size() == config::kMaxCameras)
            break;
        Candidate c{};
        c.address = address;
        found.push_back(c);
    }
    delete g_healthCandidates;
    g_healthCandidates = nullptr;
    g_healthStep = 0;
    if (found.empty()) {
        Log("Health finder: nothing followed. Press Home at full health to start again.");
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

    config::God god = config::GetGod();
    god.health.clear();
    for (size_t i = 0; i < found.size(); ++i) {
        auto it = std::find_if(chains.begin(), chains.end(),
                               [&](const Chain& c) { return c.camera == static_cast<int>(i); });
        if (it == chains.end())
            continue;
        LogChain(*it, static_cast<int>(god.health.size() + 1));
        god.health.push_back(it->pointer);
    }
    if (god.health.empty()) {
        Log("Health finder: found health but no static pointer to it");
        BeepFailure();
        return;
    }
    god.enabled = true;
    config::SetGod(god);
    Log("Health finder: saved %zu pointers; god mode on (End toggles it)", god.health.size());
    BeepSuccess();
}

// The three steps without anyone at the keys (F16): scans at full health, waits for the moment
// several candidates drop below 100 together (the player was hit: their health and its copies),
// keeps those, waits for them to refill, then finishes as the third step does.
void RunHealthAuto() {
    HealthFirstScan();
    if (g_healthStep.load() != 1 || !g_healthCandidates)
        return;
    Block<uintptr_t>& c = *g_healthCandidates;
    std::vector<uint8_t> wasFull(c.size, 1);
    std::vector<uintptr_t> hit;
    // A hit: values that just left 100 all for the same number (the player's health and its
    // copies), still together and below 100 three seconds later (BF3 waits several seconds after
    // a hit before health refills), then climbing back through values in between: health refills
    // gradually. Earlier tries took any values that dropped together (noise half a second after
    // the scan), then a group that went 100 -> 30 -> 100 within two seconds, then groups at 15.00
    // and 0.11 that jumped straight back.
    auto refillsGradually = [](const std::vector<uintptr_t>& group) {
        float last = -1.0f;
        int steps = 0;
        for (int poll = 0; poll < 600; ++poll) {  // up to about 90 seconds
            Sleep(150);
            size_t full = 0;
            float first = -1.0f;
            for (uintptr_t address : group) {
                float v = 0.0f;
                if (!camera::SafeRead(address, &v, sizeof(v)))
                    continue;
                full += v == kFullHealth;
                if (first < 0.0f)
                    first = v;
            }
            if (full * 2 > group.size())
                return steps >= 2;
            if (last >= 0.0f && first > last + 0.05f && first < kFullHealth)
                ++steps;
            last = first;
        }
        return false;
    };
    for (int poll = 0; poll < 1200 && hit.empty(); ++poll) {  // up to about 3 minutes
        Sleep(150);
        std::map<long, std::vector<uintptr_t>> byValue;
        for (size_t i = 0; i < c.size; ++i) {
            float v = 0.0f;
            bool readable = camera::SafeRead(c.data[i], &v, sizeof(v));
            if (wasFull[i] && readable && v >= 1.0f && v < kFullHealth - 0.5f)
                byValue[std::lround(v * 100.0f)].push_back(c.data[i]);
            wasFull[i] = readable && v == kFullHealth;
        }
        // The player's health had 15 copies in the first, manual run; counters and timers that
        // pass 100 come in twos and threes.
        for (const auto& [key, group] : byValue) {
            if (group.size() < 4 || group.size() > 64)
                continue;
            auto stillTogether = [&group]() {
                float first = -1.0f;
                for (uintptr_t address : group) {
                    float v = 0.0f;
                    if (!camera::SafeRead(address, &v, sizeof(v)) || !(v > 0.0f && v < kFullHealth) ||
                        (first >= 0.0f && std::fabs(v - first) > 0.01f))
                        return false;
                    first = v;
                }
                return true;
            };
            Sleep(1000);
            bool together = stillTogether();
            if (together) {
                Sleep(2000);
                together = stillTogether();
            }
            Log("Health finder (auto): %zu values dropped to %.2f; %s", group.size(), key / 100.0f,
                together ? "still together and low three seconds later" : "noise");
            if (!together)
                continue;
            if (refillsGradually(group)) {
                Log("Health finder (auto): they refilled gradually, like health");
                hit = group;
                break;
            }
            Log("Health finder (auto): they jumped straight back (or never refilled): not health");
        }
    }
    if (hit.empty()) {
        Log("Health finder (auto): no hit seen; nothing changed");
        g_healthStep = 0;
        BeepFailure();
        return;
    }
    c.size = 0;
    for (uintptr_t address : hit)
        c.Push(address);
    g_healthStep = 2;
    HealthThirdStep();
}

void RunHealthStep() {
    switch (g_healthStep.load()) {
    case 0: HealthFirstScan(); break;
    case 1: HealthSecondStep(); break;
    default: HealthThirdStep(); break;
    }
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

void StartSkeleton() {
    StartJob(RunSkeleton);
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

void HealthAuto() {
    StartJob(RunHealthAuto);
}

void HealthStep() {
    StartJob(RunHealthStep);
}

bool HealthFinderActive() {
    return g_healthStep.load() != 0;
}

}
