// Skinning research. A skinned mesh such as the first-person arms is drawn with a block of bone
// matrices in a constant buffer; the vertex shader bends the mesh with them. BF3's gameplay
// skeleton (finder.cpp, F12) turned out not to be what's drawn; these uploads are.
//
// Found in game: BF3 draws every skinned mesh with general-purpose 60-bone blocks, each mesh cut
// into chunks of up to 60 bones and drawn in several passes. Lifting blocks by their shape alone
// moved the gun but flickered, because only some chunks and passes matched. So blocks are picked
// by where they are instead: the player's own meshes are the only skinned ones at the camera.
// Each bone matrix is the bone's world transform times its inverse bind pose, whose translation
// lands near the mesh's origin, which for the player is within a couple of metres of the camera.
//
// Second round: counting blocks near the world origin as well caught small 4x4 blocks of other
// objects, and lifting uploads made with WRITE_NO_OVERWRITE re-lifted data the GPU was already
// drawing from. Both tore meshes into pieces. Third round: with only 3x4 rows near the camera in
// the world, nothing was lifted at all; the nearest seemed 2 m away. Fourth round, lifting rows
// near the origin instead (a camera-relative view model), found only a few there, while the arm
// and gun chunks (54-59 and 7-17 bones) sat 0.3-0.7 m from the camera in the world after all.
// The third round's camera position had come from the render camera, which moves in memory and
// can be stale; the weapon camera (fixed in bf3.exe) now gives it. Fifth round, 1.5 m from it:
// still nothing visible, and while standing still almost no rows were that close. The arms rig's
// origin is at the feet, so a bone matrix's translation can land a metre or two away from the
// camera depending on how the bone is turned. Insert now cycles the radius live (1.5, 3, 6 m)
// and the log counts uploads within each.
// Sixth round, radii 1.5/3/6 m: nothing within 3 m at all; at 6 m the nearest NPC (3.5 m away)
// jumped. So the player's arms aren't world-space blocks near the camera. The gun is most likely
// rigid camera-relative pieces: the small 4x4 blocks ~7 cm from the origin, which the first round
// lifted (and those rounds since excluded).
// Seventh round, run by a script (tools/lifttest.ps1): lifting those camera-relative blocks moved
// nothing either. The one round that moved the gun had also lifted appended data in 64 KB ring
// buffers. Now ring buffers get shadow copies, so the exact bytes appended can be found and only
// those are inspected and changed (the rest may be in use by the GPU).
// Lifted: camera-relative rows in any upload, and appended rows within 3 m of the camera.
//
// Draw hunt (2026-10-29, the user judging live in the headset): the gloves are one draw, index
// count 13755, start index 15051, drawn with 55 bones (3x4) from a fresh 4096-byte upload. The
// bones' translations sit ~1.9 m from the origin: they're in the character's own space, not the
// world, which is why every world-position search missed them. Forearm skin, sleeves and gun are
// draws of their own. Hand shift (Insert) draws that one draw with a changed copy of its bones,
// bound in place of BF3's buffer, so nothing BF3 uploaded is touched.

#include "skin.h"

#include <windows.h>
#include <d3d11.h>
#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <cstring>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "log.h"

namespace {

using MapFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT,
                                          D3D11_MAPPED_SUBRESOURCE*);
using UnmapFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT);
using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);

MapFn g_origMap[2] = {};
UnmapFn g_origUnmap[2] = {};
DrawIndexedFn g_origDrawIndexed[2] = {};

constexpr UINT kMinMatrices = 4;
constexpr float kLift = 0.15f;
constexpr float kViewModel = 1.0f;  // metres from the origin: camera-relative pieces
constexpr float kBucket = 0.5f;     // metres: grid for the where-relative-to-the-eyes histogram

// Layout 0: 3x4, three float4 rows with the translation in .w (48 bytes).
// Layout 1: 4x4 with the translation in the fourth row (64 bytes).
struct Signature {
    UINT size;
    UINT start;   // byte offset of the first matrix
    UINT count;
    UINT layout;
    bool operator<(const Signature& o) const {
        return std::tie(size, start, count, layout) < std::tie(o.size, o.start, o.count, o.layout);
    }
};

struct Seen {
    UINT uploads = 0;
    UINT viewUploads = 0;   // mean translation within kViewModel of the origin (camera-relative)
    UINT appended = 0;      // ... of those, uploads made with WRITE_NO_OVERWRITE (never touched)
    float nearestCamera = 1e9f;
    float nearestOrigin = 1e9f;
};

// Where blocks sit relative to the eyes, on a kBucket grid in the camera's own axes. Over a few
// seconds of walking, other characters' blocks drift between cells; the player's own arms, if
// drawn in the world, stay in the same few cells right at the eyes.
struct Cell {
    int right, up, forward;
    UINT layout;
    bool appended;
    bool operator<(const Cell& o) const {
        return std::tie(right, up, forward, layout, appended) <
               std::tie(o.right, o.up, o.forward, o.layout, o.appended);
    }
};

std::mutex g_mutex;
std::map<Signature, Seen> g_seen;
std::map<Cell, UINT> g_cells;
std::atomic<bool> g_lift{false};
// Research hooks: bone searches in every upload, ring-buffer comparisons, draw counting for the
// hunt. They ran on every graphics call and cost frame rate (the game went from about 87 to 45
// fps while they grew), so they run only while research is on (Pause, or F15 / Insert research).
std::atomic<bool> g_research{false};
// Camera position and axes (right, up, true forward), written on the render thread.
float g_camera[3] = {};
float g_axes[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

// ---- Draw hunt ----
// A draw is "skinned" if a block of bone matrices was uploaded on the same thread just before it.
// A mesh's draws keep the same index count, start index and base vertex every frame, so those
// identify it. The hunt hides half the candidates at a time until the arms disappear.

struct DrawKey {
    UINT indexCount;
    UINT startIndex;
    INT baseVertex;
    bool operator==(const DrawKey& o) const {
        return indexCount == o.indexCount && startIndex == o.startIndex && baseVertex == o.baseVertex;
    }
};
struct DrawKeyHash {
    size_t operator()(const DrawKey& k) const {
        return (static_cast<size_t>(k.indexCount) * 73856093u) ^ (static_cast<size_t>(k.startIndex) * 19349663u) ^
               static_cast<size_t>(k.baseVertex);
    }
};
using DrawSet = std::unordered_set<DrawKey, DrawKeyHash>;

// The bone block uploaded most recently on this thread, for whichever draw follows it.
struct BoneInfo {
    UINT count = 0, layout = 0, size = 0;
    UINT start = 0;        // byte offset of the first matrix
    bool fresh = false;
    float first[12] = {};  // the first matrix's three rows
    float mean[3] = {};    // mean translation
    float dCamera = 0.0f, dOrigin = 0.0f;
};
thread_local BoneInfo t_bones;
thread_local int t_drawsSinceBones = 1000;
constexpr int kSkinnedWithin = 3;  // draws after a bone upload that still count as using it

std::mutex g_drawMutex;
std::unordered_map<DrawKey, UINT, DrawKeyHash> g_skinned;  // skinned draws seen, with counts
std::vector<DrawKey> g_huntRange;                          // candidates still in the running
bool g_haveFinal = false;
DrawKey g_final{};
BoneInfo g_finalBones;
bool g_finalSeen = false;
// Draws to hide. Published whole and never freed (a handful per hunt), so draw threads can read
// it without a lock.
std::atomic<const DrawSet*> g_hidden{nullptr};

// ---- Hand shift ----
constexpr DrawKey kGloves{13755, 15051, 0};  // from the hunt
// Shifting the gloves left a black hand shape behind: another pass, a draw of its own with its
// own 60 bones (a second hunt found it). Matching its bones to the gloves' didn't work.
struct HandDraw {
    DrawKey key;
    UINT bones;
};
constexpr HandDraw kHandDraws[] = {{kGloves, 55}, {{7617, 49995, 0}, 60}};
constexpr UINT kGloveUploadSize = 4096;
constexpr float kHandShift = 0.20f;  // metres up
std::atomic<bool> g_handShift{false};

// A copy of this thread's latest bone upload, kept only while hand shift is on and only for
// blocks the size of the gloves'.
struct Upload {
    ID3D11Resource* resource = nullptr;
    std::vector<uint8_t> bytes;
    BoneInfo bones;
};
// Gloves draws since the last log: shifted, or drawn normally because there was no fresh copy of
// their bones, or because BF3's buffer wasn't bound to the vertex shader.
std::atomic<UINT> g_shifted{0}, g_noCopy{0}, g_noSlot{0}, g_notSkinned{0};

thread_local Upload t_upload;

// Bone selector, for working out which gloves bones belong to which hand: hands.txt next to the
// DLL lists gloves palette numbers ("0-27, 40" or "all"); only those are shifted. Read by Probe.
// Found with it (the user judging in the headset): bones 0-23 are the left hand, 24-54 the right;
// each group lifts its hand whole, without stretching. Bone 23 alone moves nothing visible. The
// second pass's black forearm lifts along with the right hand: its forearm hangs on hand bones.
constexpr UINT kLeftHandBones[2] = {0, 23};
constexpr UINT kRightHandBones[2] = {24, 54};
std::mutex g_selMutex;
bool g_selectAll = true;
bool g_selected[64] = {};
std::vector<float> g_selValues;  // the selected gloves bones' latest matrices, 12 floats each
float g_shift[3] = {0.0f, kHandShift, 0.0f};
constexpr float kSameBone = 0.02f;

bool MatchesSelected(const float* bone) {
    for (size_t i = 0; i + 12 <= g_selValues.size(); i += 12) {
        bool same = true;
        for (int j = 0; j < 12 && same; ++j)
            same = std::fabs(bone[j] - g_selValues[i + j]) < kSameBone;
        if (same)
            return true;
    }
    return false;
}

void ReadSelection() {
    static FILETIME lastWrite{};
    wchar_t path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ReadSelection), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\'))
        wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"hands.txt");
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &info) ||
        CompareFileTime(&info.ftLastWriteTime, &lastWrite) == 0)
        return;
    lastWrite = info.ftLastWriteTime;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"r") != 0 || !f)
        return;
    char text[512] = {};
    fread(text, 1, sizeof(text) - 1, f);
    fclose(f);

    // "shift x y z" (metres, in the bones' own space) after the bone list sets the direction.
    float shift[3] = {0.0f, kHandShift, 0.0f};
    if (char* s = std::strstr(text, "shift")) {
        sscanf_s(s + 5, "%f %f %f", &shift[0], &shift[1], &shift[2]);
        *s = 0;
    }
    bool all = std::strstr(text, "all") != nullptr;
    bool selected[64] = {};
    for (const char* p = text; *p;) {
        if (*p < '0' || *p > '9') {
            ++p;
            continue;
        }
        char* end = nullptr;
        long a = std::strtol(p, &end, 10), b = a;
        p = end;
        if (*p == '-') {
            b = std::strtol(p + 1, &end, 10);
            p = end;
        }
        for (long k = a; k <= b && k < 64; ++k)
            if (k >= 0)
                selected[k] = true;
    }
    std::lock_guard lock(g_selMutex);
    g_selectAll = all;
    std::memcpy(g_selected, selected, sizeof(selected));
    std::memcpy(g_shift, shift, sizeof(shift));
    g_selValues.clear();
    Log("Hands: selection now: %s, shifted by (%.2f, %.2f, %.2f)", all ? "all bones" : text, shift[0], shift[1],
        shift[2]);
}
thread_local ID3D11Buffer* t_shiftBuffer = nullptr;  // this thread's stand-in for BF3's buffer
thread_local UINT t_shiftSize = 0;

void PublishHidden(std::vector<DrawKey>::const_iterator begin, std::vector<DrawKey>::const_iterator end) {
    g_hidden = new DrawSet(begin, end);
}

// The pointer each in-progress Map wrote to; Map and Unmap of one buffer happen on one thread.
struct PendingMap {
    ID3D11Resource* resource;
    void* data;
    UINT size;
    bool fresh;  // WRITE_DISCARD: the whole buffer is new, nothing in it is in use by the GPU
};
thread_local PendingMap t_pending[4] = {};

void BeepAsync(DWORD frequency, DWORD duration) {
    DWORD packed = (frequency << 16) | duration;
    if (HANDLE t = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
            DWORD v = static_cast<DWORD>(reinterpret_cast<uintptr_t>(p));
            Beep(v >> 16, v & 0xFFFF);
            return 0;
        }, reinterpret_cast<LPVOID>(static_cast<uintptr_t>(packed)), 0, nullptr))
        CloseHandle(t);
}

float Dot3(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Three rotation rows: unit length (a little scale allowed) and at right angles.
bool RotationRows(const float* a, const float* b, const float* c) {
    for (const float* v : {a, b, c}) {
        float len = Dot3(v, v);
        if (!(len > 0.8f && len < 1.25f))  // also rejects NaN
            return false;
    }
    return std::fabs(Dot3(a, b)) < 0.05f && std::fabs(Dot3(a, c)) < 0.05f && std::fabs(Dot3(b, c)) < 0.05f;
}

bool IsMatrix(const float* m, UINT layout) {
    if (layout == 0)
        return RotationRows(m, m + 4, m + 8);
    return RotationRows(m, m + 4, m + 8) && m[3] == 0.0f && m[7] == 0.0f && m[11] == 0.0f && m[15] == 1.0f;
}

void Translation(const float* m, UINT layout, float* out) {
    if (layout == 0)
        out[0] = m[3], out[1] = m[7], out[2] = m[11];
    else
        out[0] = m[12], out[1] = m[13], out[2] = m[14];
}

// Every row of bone matrices in an upload.
void FindRows(const float* data, UINT size, std::vector<Signature>& out) {
    UINT floats = size / 4;
    for (UINT layout = 0; layout < 2; ++layout) {
        UINT stride = layout == 0 ? 12 : 16;
        for (UINT i = 0; i + stride <= floats;) {
            UINT count = 0;
            while (i + (count + 1) * stride <= floats && IsMatrix(data + i + count * stride, layout))
                ++count;
            if (count >= kMinMatrices)
                out.push_back({size, i * 4, count, layout});
            i += count ? count * stride : 4;
        }
    }
}

// Looks at `size` bytes of an upload: a whole buffer filled with WRITE_DISCARD (fresh), or just
// the bytes BF3 appended to a ring buffer with WRITE_NO_OVERWRITE. Either way nothing in it has
// reached the GPU yet, so it's safe to change.
void Inspect(void* data, UINT size, bool fresh) {
    if (size < kMinMatrices * 48)
        return;
    std::vector<Signature> rows;
    FindRows(static_cast<float*>(data), size, rows);
    if (rows.empty())
        return;
    bool lift = g_lift.load(std::memory_order_relaxed);
    std::lock_guard lock(g_mutex);
    UINT largest = 0;
    for (const Signature& s : rows) {
        float* m = static_cast<float*>(data) + s.start / 4;
        UINT stride = s.layout == 0 ? 12 : 16;
        float mean[3] = {};
        for (UINT k = 0; k < s.count; ++k) {
            float t[3];
            Translation(m + k * stride, s.layout, t);
            for (int i = 0; i < 3; ++i)
                mean[i] += t[i] / s.count;
        }
        float toCamera[3] = {mean[0] - g_camera[0], mean[1] - g_camera[1], mean[2] - g_camera[2]};
        float dCamera = std::sqrt(Dot3(toCamera, toCamera));
        float dOrigin = std::sqrt(Dot3(mean, mean));
        bool cameraRelative = dOrigin < kViewModel;
        if (s.count > largest) {
            largest = s.count;
            t_bones.count = s.count;
            t_bones.layout = s.layout;
            t_bones.size = size;
            t_bones.start = s.start;
            t_bones.fresh = fresh;
            std::memcpy(t_bones.first, m, sizeof(t_bones.first));
            std::memcpy(t_bones.mean, mean, sizeof(t_bones.mean));
            t_bones.dCamera = dCamera;
            t_bones.dOrigin = dOrigin;
            t_drawsSinceBones = 0;
        }
        Seen& seen = g_seen[s];
        ++seen.uploads;
        seen.viewUploads += cameraRelative;
        seen.appended += !fresh;
        seen.nearestCamera = std::min(seen.nearestCamera, dCamera);
        seen.nearestOrigin = std::min(seen.nearestOrigin, dOrigin);
        if (dCamera < 3.0f) {
            Cell cell{static_cast<int>(std::floor(Dot3(toCamera, g_axes[0]) / kBucket)),
                      static_cast<int>(std::floor(Dot3(toCamera, g_axes[1]) / kBucket)),
                      static_cast<int>(std::floor(Dot3(toCamera, g_axes[2]) / kBucket)), s.layout, !fresh};
            ++g_cells[cell];
        }

        // Camera-relative pieces in any upload; appended rows near the camera in the world too
        // (the first round, which moved the gun, had lifted appended data).
        if (lift && (cameraRelative || (!fresh && dCamera < 3.0f)))
            for (UINT k = 0; k < s.count; ++k)
                m[k * stride + (s.layout == 0 ? 7 : 13)] += kLift;  // vertical translation
    }
}

HRESULT Map(int which, ID3D11DeviceContext* ctx, ID3D11Resource* resource, UINT sub, D3D11_MAP type, UINT flags,
            D3D11_MAPPED_SUBRESOURCE* mapped) {
    HRESULT hr = g_origMap[which](ctx, resource, sub, type, flags, mapped);
    if (FAILED(hr) || !mapped || (type != D3D11_MAP_WRITE_DISCARD && type != D3D11_MAP_WRITE_NO_OVERWRITE))
        return hr;
    D3D11_RESOURCE_DIMENSION dim;
    resource->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_BUFFER)
        return hr;
    D3D11_BUFFER_DESC desc;
    static_cast<ID3D11Buffer*>(resource)->GetDesc(&desc);
    if (!(desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER))
        return hr;
    for (PendingMap& p : t_pending) {
        if (!p.resource) {
            p = {resource, mapped->pData, desc.ByteWidth, type == D3D11_MAP_WRITE_DISCARD};
            break;
        }
    }
    return hr;
}

// ---- First-person FOV ----
// With the render FOV widened (camera.cpp), the gun and arms were still drawn at the game's own
// 55 degrees, shown life-size twice too big; writing the render object's first-person view
// changed its matrices but not the gun. So the fix is made in what reaches the graphics card: in
// every constant buffer upload, a matrix that projects with the game's own FOV is rescaled to the
// wide one. Such a matrix (projection, or view times projection) has the x and y columns of its
// rotation part scaled by 1/tan(FOV/2) and a unit w column; stored transposed, the same in rows.
// Scaling those two columns (all four rows) by wide/game gives the wide projection exactly.
std::atomic<float> g_gameScale{0.0f}, g_wideScale{0.0f};  // 1/tan(FOV/2); 0 = off
// Matrices rescaled since the last log, by where they came from: a small buffer filled whole,
// a ring buffer filled whole, or bytes appended to a ring buffer.
enum UploadKind { kSmall, kRingWhole, kRingAppend, kUploadKinds };
std::atomic<uint32_t> g_rescaled[kUploadKinds];

bool Near(float a, float b) {
    return std::fabs(a - b) < 0.002f * b;  // also rejects NaN
}

float Length3(float x, float y, float z) {
    return std::sqrt(x * x + y * y + z * z);
}

std::atomic<int64_t> g_rescaleTicks{0};  // time spent scanning, for the log
// Uploads scanned and matched, by size in 256-byte steps (the last bucket: 16 KB and up).
constexpr int kSizeBuckets = 65;
std::atomic<uint32_t> g_scannedBySize[kSizeBuckets], g_matchedBySize[kSizeBuckets];

int SizeBucket(UINT size) {
    return std::min(static_cast<int>(size / 256), kSizeBuckets - 1);
}
// Scanning every upload cost about 2 ms a frame (85,000 uploads a second), most of them sizes that
// never held a match (the 4 KB bone blocks, 1-1.5 KB blocks). So only sizes that have matched
// are scanned, plus everything for one frame every two seconds, which finds new ones.
std::atomic<bool> g_sizeMatched[kSizeBuckets];
std::atomic<bool> g_scanAll{true};

void RescaleProjections(uint8_t* data, UINT size, UploadKind kind) {
    float game = g_gameScale.load(std::memory_order_relaxed), wide = g_wideScale.load(std::memory_order_relaxed);
    if (!(game > 0.0f && wide > 0.0f) || Near(wide, game))
        return;
    int bucket = SizeBucket(size);
    if (!g_sizeMatched[bucket].load(std::memory_order_relaxed) && !g_scanAll.load(std::memory_order_relaxed))
        return;
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    g_scannedBySize[bucket].fetch_add(1, std::memory_order_relaxed);
    float k = wide / game;
    // Runs on every constant buffer upload, thousands a frame: squared lengths first, and only
    // a near match pays for the rest.
    float lo = game * game * 0.995f, hi = game * game * 1.005f;
    auto f = reinterpret_cast<float*>(data);
    UINT count = size / 4;
    for (UINT i = 0; i + 16 <= count; i += 4) {  // a matrix starts at any 16-byte register
        float* m = f + i;
        float column = m[0] * m[0] + m[4] * m[4] + m[8] * m[8];
        float row = m[0] * m[0] + m[1] * m[1] + m[2] * m[2];
        if (!(column > lo && column < hi) && !(row > lo && row < hi))
            continue;
        if (Near(Length3(m[0], m[4], m[8]), game) && Near(Length3(m[1], m[5], m[9]), game) &&
            Near(Length3(m[3], m[7], m[11]), 1.0f)) {
            for (int r = 0; r < 4; ++r)
                m[4 * r] *= k, m[4 * r + 1] *= k;
        } else if (Near(Length3(m[0], m[1], m[2]), game) && Near(Length3(m[4], m[5], m[6]), game) &&
                   Near(Length3(m[12], m[13], m[14]), 1.0f)) {
            for (int c = 0; c < 4; ++c)
                m[c] *= k, m[4 + c] *= k;
        } else {
            continue;
        }
        g_rescaled[kind].fetch_add(1, std::memory_order_relaxed);
        g_matchedBySize[bucket].fetch_add(1, std::memory_order_relaxed);
        g_sizeMatched[bucket].store(true, std::memory_order_relaxed);
        i += 12;  // past this matrix
    }
    QueryPerformanceCounter(&t1);
    g_rescaleTicks.fetch_add(t1.QuadPart - t0.QuadPart, std::memory_order_relaxed);
}

// Ring buffers: big constant buffers BF3 appends to with WRITE_NO_OVERWRITE between DISCARDs.
// Earlier parts may already be in use by the GPU, so only what was just written may be touched.
// A shadow copy of each ring buffer shows which bytes changed since the last upload.
constexpr UINT kMinRingSize = 16384;
std::mutex g_shadowMutex;
std::unordered_map<ID3D11Resource*, std::vector<uint8_t>> g_shadows;

// The changed bytes between data and shadow, in whole 16-byte registers: [begin, end).
bool ChangedRange(const uint8_t* data, const std::vector<uint8_t>& shadow, UINT size, UINT& begin, UINT& end) {
    UINT first = size, last = 0;
    for (UINT i = 0; i + 16 <= size; i += 16) {
        if (std::memcmp(data + i, shadow.data() + i, 16) != 0) {
            first = std::min(first, i);
            last = i + 16;
        }
    }
    begin = first;
    end = last;
    return first < last;
}

void HandleUpload(const PendingMap& p) {
    auto data = static_cast<uint8_t*>(p.data);
    bool research = g_research.load(std::memory_order_relaxed);
    if (p.size < kMinRingSize) {
        if (p.fresh) {
            RescaleProjections(data, p.size, kSmall);
            if (research)
                Inspect(data, p.size, true);
            // Every upload the gloves' size, not only those whose bones all passed as matrices:
            // only a third did (scaled bones break the row).
            if (g_handShift.load(std::memory_order_relaxed) && p.size == kGloveUploadSize) {
                t_upload.resource = p.resource;
                t_upload.bytes.assign(data, data + p.size);
                t_upload.bones = t_drawsSinceBones == 0 ? t_bones : BoneInfo{};
            } else if (p.resource == t_upload.resource) {
                t_upload.resource = nullptr;  // refilled with something else: the copy is stale
            }
        }
        return;
    }
    if (p.resource == t_upload.resource)
        t_upload.resource = nullptr;
    // Finding what was appended to a ring buffer means comparing all of it with a shadow copy on
    // every upload: only research needs that.
    if (!research) {
        if (p.fresh)
            RescaleProjections(data, p.size, kRingWhole);
        return;
    }
    std::lock_guard lock(g_shadowMutex);
    std::vector<uint8_t>& shadow = g_shadows[p.resource];
    if (p.fresh || shadow.size() != p.size) {
        if (p.fresh) {
            RescaleProjections(data, p.size, kRingWhole);
            Inspect(data, p.size, true);
        }
        shadow.assign(data, data + p.size);
        return;
    }
    UINT begin = 0, end = 0;
    if (!ChangedRange(data, shadow, p.size, begin, end))
        return;
    RescaleProjections(data + begin, end - begin, kRingAppend);
    Inspect(data + begin, end - begin, false);
    std::memcpy(shadow.data() + begin, data + begin, end - begin);  // including any lift
}

void Unmap(int which, ID3D11DeviceContext* ctx, ID3D11Resource* resource, UINT sub) {
    for (PendingMap& p : t_pending) {
        if (p.resource == resource) {
            // Still writable here: the data reaches the GPU only once Unmap is called.
            HandleUpload(p);
            p = {};
            break;
        }
    }
    g_origUnmap[which](ctx, resource, sub);
}

// Draws the gloves with a shifted copy of their bones: the copy goes into this thread's own
// constant buffer, bound in the slot where BF3's buffer is, then BF3's is put back.
bool DrawShifted(int which, ID3D11DeviceContext* ctx, UINT count, UINT start, INT base, UINT bones) {
    const Upload& u = t_upload;
    if (!u.resource || u.bytes.empty()) {
        ++g_noCopy;
        return false;
    }
    ID3D11Buffer* bound[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ctx->VSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, bound);
    int slot = -1;
    for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i) {
        if (bound[i] && bound[i] == u.resource)
            slot = static_cast<int>(i);
        if (bound[i])
            bound[i]->Release();
    }
    if (slot < 0) {
        ++g_noSlot;
        return false;
    }

    UINT size = static_cast<UINT>(u.bytes.size());
    if (!t_shiftBuffer || t_shiftSize != size) {
        if (t_shiftBuffer)
            t_shiftBuffer->Release();
        t_shiftBuffer = nullptr;
        ID3D11Device* device = nullptr;
        ctx->GetDevice(&device);
        D3D11_BUFFER_DESC desc{size, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
        HRESULT hr = device->CreateBuffer(&desc, nullptr, &t_shiftBuffer);
        device->Release();
        if (FAILED(hr))
            return false;
        t_shiftSize = size;
    }
    // BF3's own Map/Unmap, not the hooked ones: this upload isn't one to inspect.
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(g_origMap[which](ctx, t_shiftBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return false;
    auto m = static_cast<float*>(mapped.pData);
    std::memcpy(m, u.bytes.data(), size);
    // The bones start at the front of the buffer. (They pass as matrices only some of the time,
    // scaled bones break the row, so the count comes from the hunt instead.)
    // Only the selected bones (hands.txt). The gloves' palette is numbered as the selection is; the
    // second pass has a palette of its own, so its bones are picked by matching the selected
    // gloves bones' values (the same bone has the same matrix in both).
    bool gloves = bones == kHandDraws[0].bones;
    std::lock_guard lock(g_selMutex);
    if (gloves)
        g_selValues.clear();
    for (UINT k = 0; k < bones && k * 12 + 12 <= size / 4; ++k) {
        float* bone = m + k * 12;
        bool selected = g_selectAll || (gloves ? k < 64 && g_selected[k] : MatchesSelected(bone));
        if (!selected)
            continue;
        if (gloves && !g_selectAll)
            g_selValues.insert(g_selValues.end(), bone, bone + 12);
        bone[3] += g_shift[0];  // translation (y is up in character space)
        bone[7] += g_shift[1];
        bone[11] += g_shift[2];
    }
    g_origUnmap[which](ctx, t_shiftBuffer, 0);

    ctx->VSSetConstantBuffers(slot, 1, &t_shiftBuffer);
    g_origDrawIndexed[which](ctx, count, start, base);
    auto original = static_cast<ID3D11Buffer*>(u.resource);
    ctx->VSSetConstantBuffers(slot, 1, &original);
    ++g_shifted;
    return true;
}

void DrawIndexed(int which, ID3D11DeviceContext* ctx, UINT count, UINT start, INT base) {
    if (!g_research.load(std::memory_order_relaxed)) {
        // Only the hands' own draws matter: a couple of comparisons, no lock.
        if (g_handShift.load(std::memory_order_relaxed)) {
            DrawKey key{count, start, base};
            for (const HandDraw& h : kHandDraws)
                if (h.key == key && DrawShifted(which, ctx, count, start, base, h.bones))
                    return;
        }
        g_origDrawIndexed[which](ctx, count, start, base);
        return;
    }
    bool skinned = t_drawsSinceBones < kSkinnedWithin;
    ++t_drawsSinceBones;
    if (skinned) {
        DrawKey key{count, start, base};
        UINT handBones = 0;
        for (const HandDraw& h : kHandDraws)
            if (h.key == key)
                handBones = h.bones;
        {
            std::lock_guard lock(g_drawMutex);
            ++g_skinned[key];
            if (g_haveFinal && key == g_final) {
                g_finalBones = t_bones;
                g_finalSeen = true;
            }
        }
        const DrawSet* hidden = g_hidden.load(std::memory_order_acquire);
        if (hidden && hidden->count(key))
            return;
        if (handBones && g_handShift.load(std::memory_order_relaxed) &&
            DrawShifted(which, ctx, count, start, base, handBones))
            return;
    } else if (g_handShift.load(std::memory_order_relaxed) && DrawKey{count, start, base} == kGloves) {
        ++g_notSkinned;  // no bone upload just before it: this pass reuses an earlier one
    }
    g_origDrawIndexed[which](ctx, count, start, base);
}

void STDMETHODCALLTYPE DrawIndexed0(ID3D11DeviceContext* c, UINT count, UINT start, INT base) {
    DrawIndexed(0, c, count, start, base);
}
void STDMETHODCALLTYPE DrawIndexed1(ID3D11DeviceContext* c, UINT count, UINT start, INT base) {
    DrawIndexed(1, c, count, start, base);
}

HRESULT STDMETHODCALLTYPE Map0(ID3D11DeviceContext* c, ID3D11Resource* r, UINT s, D3D11_MAP t, UINT f,
                               D3D11_MAPPED_SUBRESOURCE* m) {
    return Map(0, c, r, s, t, f, m);
}
HRESULT STDMETHODCALLTYPE Map1(ID3D11DeviceContext* c, ID3D11Resource* r, UINT s, D3D11_MAP t, UINT f,
                               D3D11_MAPPED_SUBRESOURCE* m) {
    return Map(1, c, r, s, t, f, m);
}
void STDMETHODCALLTYPE Unmap0(ID3D11DeviceContext* c, ID3D11Resource* r, UINT s) {
    Unmap(0, c, r, s);
}
void STDMETHODCALLTYPE Unmap1(ID3D11DeviceContext* c, ID3D11Resource* r, UINT s) {
    Unmap(1, c, r, s);
}

// The camera-relative kinds of block, and where world-space blocks within 3 m sat relative to the
// eyes, since the last press.
void LogSeen() {
    std::vector<std::pair<Signature, Seen>> rows;
    UINT total = 0;
    for (const auto& [s, seen] : g_seen) {
        total += seen.uploads;
        if (seen.viewUploads)
            rows.push_back({s, seen});
    }
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.second.viewUploads > b.second.viewUploads; });
    Log("Skin: %zu kinds of bone block, %u uploads; %zu kinds camera-relative (within %.1f m of the origin)",
        g_seen.size(), total, rows.size(), kViewModel);
    for (size_t i = 0; i < rows.size() && i < 15; ++i)
        Log("Skin:   %u matrices (%s) at +%u in %u-byte uploads: %u of %u uploads camera-relative (%u appended "
            "uploads), nearest %.2f m from origin",
            rows[i].first.count, rows[i].first.layout == 0 ? "3x4" : "4x4", rows[i].first.start, rows[i].first.size,
            rows[i].second.viewUploads, rows[i].second.uploads, rows[i].second.appended,
            rows[i].second.nearestOrigin);

    std::vector<std::pair<Cell, UINT>> cells(g_cells.begin(), g_cells.end());
    std::sort(cells.begin(), cells.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    Log("Skin: world-space blocks within 3 m, by where they sat relative to the eyes (%zu cells of %.1f m):",
        cells.size(), kBucket);
    for (size_t i = 0; i < cells.size() && i < 12; ++i)
        Log("Skin:   right %+.1f..%+.1f  up %+.1f..%+.1f  forward %+.1f..%+.1f  (%s, %s): %u uploads",
            cells[i].first.right * kBucket, (cells[i].first.right + 1) * kBucket, cells[i].first.up * kBucket,
            (cells[i].first.up + 1) * kBucket, cells[i].first.forward * kBucket,
            (cells[i].first.forward + 1) * kBucket, cells[i].first.layout == 0 ? "3x4" : "4x4",
            cells[i].first.appended ? "appended" : "fresh", cells[i].second);
    g_seen.clear();
    g_cells.clear();
}

}

namespace skin {

bool InstallHooks(void* const (&map)[2], void* const (&unmap)[2], void* const (&drawIndexed)[2]) {
    void* mapHooks[2] = {reinterpret_cast<void*>(&Map0), reinterpret_cast<void*>(&Map1)};
    void* unmapHooks[2] = {reinterpret_cast<void*>(&Unmap0), reinterpret_cast<void*>(&Unmap1)};
    void* drawHooks[2] = {reinterpret_cast<void*>(&DrawIndexed0), reinterpret_cast<void*>(&DrawIndexed1)};
    for (int i = 0; i < 2; ++i) {
        if (i == 1 && map[1] == map[0]) {
            // Deferred contexts share the immediate context's functions: one hook covers both.
            g_origMap[1] = g_origMap[0];
            g_origUnmap[1] = g_origUnmap[0];
            g_origDrawIndexed[1] = g_origDrawIndexed[0];
            break;
        }
        if (MH_CreateHook(map[i], mapHooks[i], reinterpret_cast<void**>(&g_origMap[i])) != MH_OK ||
            MH_CreateHook(unmap[i], unmapHooks[i], reinterpret_cast<void**>(&g_origUnmap[i])) != MH_OK ||
            MH_CreateHook(drawIndexed[i], drawHooks[i], reinterpret_cast<void**>(&g_origDrawIndexed[i])) != MH_OK) {
            Log("Skin: hooking Map/Unmap/DrawIndexed failed");
            return false;
        }
    }
    return true;
}

void HuntStart() {
    // The hunt picks from the skinned draws counted while research is on.
    if (!g_research.exchange(true)) {
        Log("Draw hunt: research turned on to count skinned draws; press F15 again in a few seconds");
        return;
    }
    std::lock_guard lock(g_drawMutex);
    // Pressed during a hunt: stop it and show everything again, for a clean reference picture.
    if (g_hidden.load()) {
        g_hidden = nullptr;
        g_huntRange.clear();
        g_haveFinal = false;
        Log("Draw hunt: stopped; nothing hidden");
        return;
    }
    std::vector<std::pair<DrawKey, UINT>> seen(g_skinned.begin(), g_skinned.end());
    std::sort(seen.begin(), seen.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    g_huntRange.clear();
    for (const auto& [key, count] : seen)
        g_huntRange.push_back(key);
    g_haveFinal = false;
    g_finalSeen = false;
    if (g_huntRange.empty()) {
        g_hidden = nullptr;
        Log("Draw hunt: no skinned draws seen yet");
        return;
    }
    PublishHidden(g_huntRange.begin(), g_huntRange.begin() + (g_huntRange.size() + 1) / 2);
    Log("Draw hunt: %zu skinned draw kinds; hiding the first %zu", g_huntRange.size(),
        (g_huntRange.size() + 1) / 2);
}

void HuntAnswer(bool vanished) {
    std::lock_guard lock(g_drawMutex);
    if (g_huntRange.size() <= 1) {
        Log("Draw hunt: nothing to narrow (start it with F15)");
        return;
    }
    size_t half = (g_huntRange.size() + 1) / 2;
    if (vanished)
        g_huntRange.resize(half);
    else
        g_huntRange.erase(g_huntRange.begin(), g_huntRange.begin() + half);
    if (g_huntRange.size() == 1) {
        g_final = g_huntRange[0];
        g_haveFinal = true;
        g_finalSeen = false;
        PublishHidden(g_huntRange.begin(), g_huntRange.end());  // keep it hidden, to confirm
        Log("Draw hunt: found it: index count %u, start index %u, base vertex %d (hidden; bone data follows)",
            g_final.indexCount, g_final.startIndex, g_final.baseVertex);
        return;
    }
    PublishHidden(g_huntRange.begin(), g_huntRange.begin() + (g_huntRange.size() + 1) / 2);
    Log("Draw hunt: arms %s; %zu candidates left, hiding %zu", vanished ? "vanished" : "still there",
        g_huntRange.size(), (g_huntRange.size() + 1) / 2);
}

void Probe() {
    static ULONGLONG lastLog = 0, lastRead = 0, lastFullScan = 0;
    ULONGLONG now = GetTickCount64();
    if (now - lastRead >= 300) {
        lastRead = now;
        ReadSelection();
    }
    // Every upload size is scanned for one frame (this call to the next) every two seconds.
    bool scanAll = now - lastFullScan >= 2000;
    if (scanAll)
        lastFullScan = now;
    g_scanAll.store(scanAll, std::memory_order_relaxed);
    if (now - lastLog < 1000)
        return;
    lastLog = now;
    if (g_wideScale.load() > 0.0f) {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        Log("First-person FOV: matrices rescaled from the game's FOV in the last second: %u in small buffers, %u in "
            "whole ring buffers, %u appended to ring buffers; scanning took %.1f ms",
            g_rescaled[kSmall].exchange(0), g_rescaled[kRingWhole].exchange(0), g_rescaled[kRingAppend].exchange(0),
            1000.0 * static_cast<double>(g_rescaleTicks.exchange(0)) / static_cast<double>(frequency.QuadPart));
        char sizes[512] = {};
        int used = 0;
        for (int b = 0; b < kSizeBuckets && used < 480; ++b) {
            uint32_t scanned = g_scannedBySize[b].exchange(0), matched = g_matchedBySize[b].exchange(0);
            if (scanned >= 100 || matched)
                used += snprintf(sizes + used, sizeof(sizes) - used, " %u-%uB: %u scanned %u matched;", b * 256,
                                 b * 256 + 255, scanned, matched);
        }
        Log("First-person FOV: uploads by size:%s", sizes);
    }
    if (g_handShift.load())
        Log("Hands: last second's gloves draws: %u shifted, %u without a fresh copy, %u with the buffer not bound, "
            "%u with no bone upload just before",
            g_shifted.exchange(0), g_noCopy.exchange(0), g_noSlot.exchange(0), g_notSkinned.exchange(0));
    BoneInfo b;
    {
        std::lock_guard lock(g_drawMutex);
        if (!g_haveFinal || !g_finalSeen)
            return;
        b = g_finalBones;
    }
    float rel[3] = {b.mean[0] - g_camera[0], b.mean[1] - g_camera[1], b.mean[2] - g_camera[2]};
    Log("Draw hunt: arms' bones: %u matrices (%s) in %u-byte %s upload; mean translation (%.2f, %.2f, %.2f), "
        "%.2f m from camera (right %.2f, up %.2f, forward %.2f), %.2f m from origin",
        b.count, b.layout == 0 ? "3x4" : "4x4", b.size, b.fresh ? "fresh" : "appended", b.mean[0], b.mean[1],
        b.mean[2], b.dCamera, Dot3(rel, g_axes[0]), Dot3(rel, g_axes[1]), Dot3(rel, g_axes[2]), b.dOrigin);
    Log("Draw hunt:   first matrix rows (%.3f %.3f %.3f %.3f) (%.3f %.3f %.3f %.3f) (%.3f %.3f %.3f %.3f)",
        b.first[0], b.first[1], b.first[2], b.first[3], b.first[4], b.first[5], b.first[6], b.first[7],
        b.first[8], b.first[9], b.first[10], b.first[11]);
}

void SetProjectionFix(float gameFov, float wideFov) {
    bool on = gameFov > 0.0f && wideFov > 0.0f;
    g_gameScale = on ? 1.0f / std::tan(gameFov / 2) : 0.0f;
    g_wideScale = on ? 1.0f / std::tan(wideFov / 2) : 0.0f;
}

void ToggleHandShift() {
    bool on = !g_handShift.load();
    g_handShift = on;
    Log("Hands: shift %s", on ? "on: gloves drawn 20 cm higher" : "off");
    BeepAsync(on ? 1200 : 440, 200);
}

void SetCamera(const float* position, const float* right, const float* up, const float* forward) {
    std::lock_guard lock(g_mutex);
    for (int i = 0; i < 3; ++i) {
        g_camera[i] = position[i];
        g_axes[0][i] = right[i];
        g_axes[1][i] = up[i];
        g_axes[2][i] = forward[i];
    }
}

void ToggleResearch() {
    bool on = !g_research.load();
    g_research = on;
    Log("Skin: research hooks %s", on ? "on (bone searches, ring-buffer comparisons, draw counting)" : "off");
}

void ToggleLift() {
    bool lift = !g_lift.load();
    g_lift = lift;
    if (lift)
        g_research = true;  // the lift happens in the bone search
    {
        std::lock_guard lock(g_mutex);
        LogSeen();
    }
    Log("Skin: lift %s", lift ? "on: camera-relative pieces, 15 cm up" : "off");
    BeepAsync(lift ? 1200 : 440, 200);
}

}
