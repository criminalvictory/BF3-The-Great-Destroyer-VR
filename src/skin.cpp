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
#include <iterator>
#include <mutex>
#include <string>
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
// First-person draws: BF3 marks the gun and arms with stencil 32, set as the stencil reference of
// the draws that make them. The gun's attachments (scope, magazine, foregrip) are rigid meshes, not
// skinned, so a hunt for one of them ("hunt.txt" saying "first person") picks from these instead.
constexpr UINT kFirstPersonStencil = 32;
std::unordered_map<DrawKey, UINT, DrawKeyHash> g_firstPerson;  // seen while research is on
std::atomic<bool> g_huntFirstPerson{false};
int g_huntOnly = -1;  // hunt.txt "only N"

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
constexpr UINT kGunPrefix = 6 * 48;  // bytes of a bone upload the gun's draws use (bones 0-5)
constexpr float kHandShift = 0.20f;  // metres up
std::atomic<bool> g_handShift{false};

// The gun's own colour pass (the draw hunt found it: hidden, the gun turned black), 59 bones in a
// 4096-byte upload like the gloves'. Its bones carry the parts its animations move, the magazine
// among them. weapon.txt next to the DLL shifts chosen ones ("0-29 shift 0 -0.3 0"), to find them.
// Other soldiers' copies of the same gun are drawn with the same draw, their bones far out in the
// world; only the one whose bones sit near the origin (the player's, in the body's own space) moves.
constexpr DrawKey kWeapon{12768, 0, 0};
// With only this colour pass shifted, a black copy of the gun stayed behind: the gun is drawn in
// more passes, each cutting the mesh its own way. The colour pass's 12768 + 312 indices make the
// 13080 of another pass that draws it whole. Each draw has a bone palette of its own, so weapon.txt
// names the bones per pass. (Shifting 9273/12 and 9288/9288 along, other meshes, left more black.)
constexpr DrawKey kWeaponPasses[] = {kWeapon, {312, 12768, 0}, {13080, 13080, 0}};
constexpr size_t kWeaponPassCount = std::size(kWeaponPasses);
constexpr UINT kPaletteBones = 60;  // BF3's bone blocks hold up to 60
constexpr float kFirstPersonReach = 5.0f;  // metres from the origin
std::atomic<bool> g_weaponShift{false};  // weapon.txt names bones
struct PassSelection {
    bool bones[64] = {};
    bool any = false;
};
PassSelection g_weaponSelected[kWeaponPassCount];
float g_weaponOffset[3] = {};
bool g_weaponHide = false;  // weapon.txt "hide": the named bones vanish instead of moving
std::atomic<float> g_floor{0.0f};  // weapon.txt "floor": the ground's height in the body's space
std::atomic<bool> g_handsActive{false};  // free hands on (hands.cpp); read by the hooks without a lock
void NoteRecentUpload(ID3D11Resource* resource, const uint8_t* data, UINT size);
std::atomic<UINT> g_weaponShifted{0}, g_weaponFar{0};

// The player's own gun: bone 0 sits in the body's space, other soldiers' copies of it far out in
// the world.
bool PlayersOwn(const std::vector<uint8_t>& bones) {
    if (bones.size() < 48)
        return false;
    const auto* first = reinterpret_cast<const float*>(bones.data());
    return std::fabs(first[3]) < kFirstPersonReach && std::fabs(first[7]) < kFirstPersonReach &&
           std::fabs(first[11]) < kFirstPersonReach;
}

// weapon.txt "watch": how far each palette bone of each of the gun's passes moves against a
// reference bone (the colour pass's receiver, bone 1; bone 0 in the others) over each second. The
// gun's sway moves every bone alike; a reload moves the magazine some 20 cm, in every pass, which
// shows its bone in each palette.
std::atomic<bool> g_weaponWatch{false};
constexpr UINT kReceiverBone = 1;
struct WatchPass {
    UINT draws = 0;
    bool any = false;
    float lo[kPaletteBones][3] = {}, hi[kPaletteBones][3] = {};
};
std::mutex g_watchMutex;
WatchPass g_watch[kWeaponPassCount];
// Also with "watch": the matrices found with the game's FOV (the gun and arms' projection, which
// RescaleProjections widens), as found, one of each kind per second. If they carry the view as
// well, they say where the eyes are in the arms' space: what the hands need.
struct SeenProjection {
    float m[16];
    UINT size, offset;
    bool columns;
};
std::mutex g_projMutex;
std::vector<SeenProjection> g_seenProjections;

// The first-person view-projections (see skin::FirstPersonViews): in a 352-byte upload at +176 in
// the arms' own space, in an 80-byte upload at +0 in the world (found with "watch", 2026-10-30).
std::mutex g_fpMutex;
float g_fpArms[16] = {}, g_fpWorld[16] = {};
ULONGLONG g_fpArmsTime = 0, g_fpWorldTime = 0;

// The eye: where a view-projection's clip x, y and w rows are all zero.
bool SolveEye(const float* m, float* eye) {
    const float* r[3] = {m, m + 4, m + 12};
    auto det3 = [](const float a[3][3]) {
        return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
               a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    };
    float a[3][3], b[3];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            a[i][j] = r[i][j];
        b[i] = -r[i][3];
    }
    float det = det3(a);
    if (!(std::fabs(det) > 1e-9f))
        return false;
    for (int k = 0; k < 3; ++k) {
        float c[3][3];
        std::memcpy(c, a, sizeof(c));
        for (int i = 0; i < 3; ++i)
            c[i][k] = b[i];
        eye[k] = det3(c) / det;
    }
    return true;
}

// The view's own space (the eye at the origin, x right, y up, z back) -> the arms' space, from the
// latest first-person view-projection: its axes and eye. The hands' targets come in the view's
// space and each draw places them with this, so they stay put relative to the picture.
float g_viewToArms[12] = {};
bool g_haveViewToArms = false;
// ... and per constant buffer the view-projection went into: a draw uses the one bound to it, the
// view it is really drawn with. The latest upload, from another thread, was sometimes the next
// frame's: the gun jittered while walking by a frame's worth of movement (user, 2026-10-30).
struct ViewByBuffer {
    ID3D11Resource* buffer = nullptr;
    float viewToArms[12] = {};
};
ViewByBuffer g_viewByBuffer[8];
int g_viewByBufferNext = 0;
thread_local ID3D11Resource* t_uploadResource = nullptr;  // the upload being looked at
std::atomic<uint32_t> g_viewBound{0}, g_viewLatest{0}, g_viewDiffered{0};
// weapon.txt "trace": the next 120 gun colour draws, each with the eye the hands were placed from
// and the game's own gun body as drawn: while walking, a view a frame off shows as the gap between
// the two jumping by a frame's movement.
struct TraceRow {
    double time;
    DWORD thread;
    float eye[3], gun[3];
    uint32_t viewUploads;
};
std::mutex g_traceMutex;
std::vector<TraceRow> g_trace;
std::atomic<bool> g_tracing{false};
std::atomic<uint32_t> g_viewUploads{0};  // first-person view-projections uploaded so far
// The arms' space's origin in the world: whole metres (it jumps one as the player walks across).
// Only a near whole-metre difference between the two latest views counts: one of them a frame
// older while moving fast gives something in between, which is left out.
float g_fpOrigin[3] = {};
bool g_haveFpOrigin = false;

void NoteFirstPersonView(const float* m, UINT size, UINT offset) {
    bool arms = size == 352 && offset == 176, world = size == 80 && offset == 0;
    if (!arms && !world)
        return;
    std::lock_guard lock(g_fpMutex);
    std::memcpy(arms ? g_fpArms : g_fpWorld, m, sizeof(g_fpArms));
    (arms ? g_fpArmsTime : g_fpWorldTime) = GetTickCount64();
    float eyeArms[3];
    if (!SolveEye(g_fpArms, eyeArms))
        return;
    if (arms) {
        float axes[3][3] = {{m[0], m[1], m[2]}, {m[4], m[5], m[6]}, {-m[12], -m[13], -m[14]}};  // right, up, back
        for (auto& a : axes) {
            float length = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
            if (!(length > 1e-6f))
                return;
            a[0] /= length, a[1] /= length, a[2] /= length;
        }
        for (int i = 0; i < 3; ++i) {
            g_viewToArms[i * 4] = axes[0][i], g_viewToArms[i * 4 + 1] = axes[1][i];
            g_viewToArms[i * 4 + 2] = axes[2][i], g_viewToArms[i * 4 + 3] = eyeArms[i];
        }
        g_haveViewToArms = true;
        ++g_viewUploads;
        if (ID3D11Resource* buffer = t_uploadResource) {
            ViewByBuffer* slot = nullptr;
            for (ViewByBuffer& v : g_viewByBuffer)
                if (v.buffer == buffer)
                    slot = &v;
            if (!slot) {
                slot = &g_viewByBuffer[g_viewByBufferNext];
                g_viewByBufferNext = (g_viewByBufferNext + 1) % 8;
                slot->buffer = buffer;
            }
            std::memcpy(slot->viewToArms, g_viewToArms, sizeof(g_viewToArms));
        }
    }
    float eyeWorld[3];
    if (!g_fpWorldTime || !SolveEye(g_fpWorld, eyeWorld))
        return;
    float origin[3];
    for (int i = 0; i < 3; ++i) {
        float d = eyeWorld[i] - eyeArms[i];
        origin[i] = std::round(d);
        if (std::fabs(d - origin[i]) > 0.1f)
            return;
    }
    if (g_haveFpOrigin && std::memcmp(origin, g_fpOrigin, sizeof(origin)) != 0 &&
        g_weaponWatch.load(std::memory_order_relaxed))
        Log("Hands: the arms' space's origin moved to (%.0f %.0f %.0f)", origin[0], origin[1], origin[2]);
    std::memcpy(g_fpOrigin, origin, sizeof(origin));
    g_haveFpOrigin = true;
}

void Compose(const float* a, const float* b, float* out);

// The hands' targets, from the view's space into the arms' space with the view this draw is drawn
// with (the latest, if its constant buffer isn't known). False until a view has been seen.
bool ToArms(skin::HandTargets& t, ID3D11DeviceContext* ctx) {
    ID3D11Buffer* bound[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ctx->VSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, bound);
    float v[12];
    {
        std::lock_guard lock(g_fpMutex);
        if (!g_haveViewToArms) {
            for (ID3D11Buffer* b : bound)
                if (b)
                    b->Release();
            return false;
        }
        std::memcpy(v, g_viewToArms, sizeof(v));
        bool found = false;
        for (ID3D11Buffer* b : bound)
            for (const ViewByBuffer& view : g_viewByBuffer)
                if (b && !found && view.buffer == b) {
                    found = true;
                    if (std::memcmp(view.viewToArms, v, sizeof(v)) != 0)
                        ++g_viewDiffered;
                    std::memcpy(v, view.viewToArms, sizeof(v));
                }
        ++(found ? g_viewBound : g_viewLatest);
    }
    for (ID3D11Buffer* b : bound)
        if (b)
            b->Release();
    float pose[12];
    Compose(v, t.gunPose, pose);
    std::memcpy(t.gunPose, pose, sizeof(pose));
    Compose(v, t.leftPose, pose);
    std::memcpy(t.leftPose, pose, sizeof(pose));
    for (float* p : {t.rightWrist, t.leftWrist}) {
        float q[3];
        for (int i = 0; i < 3; ++i)
            q[i] = v[i * 4] * p[0] + v[i * 4 + 1] * p[1] + v[i * 4 + 2] * p[2] + v[i * 4 + 3];
        std::memcpy(p, q, sizeof(q));
    }
    return true;
}

void NoteProjection(const float* m, UINT size, UINT offset, bool columns) {
    std::lock_guard lock(g_projMutex);
    if (g_seenProjections.size() >= 8)
        return;
    for (const SeenProjection& s : g_seenProjections)
        if (s.size == size && s.offset == offset)
            return;
    SeenProjection s{};
    std::memcpy(s.m, m, sizeof(s.m));
    s.size = size;
    s.offset = offset;
    s.columns = columns;
    g_seenProjections.push_back(s);
}

// weapon.txt "dump": the gun's mesh as bound for its colour pass (vertex and index buffers) and its
// bones are written next to the DLL once, to read the magazine's shape from. "dump 13755 15051"
// dumps that draw instead (the gloves), as mesh_<index count>_<start>_*.bin.
std::atomic<bool> g_dumpWanted{false};
DrawKey g_dumpKey = kWeapon;  // written before g_dumpWanted is set
struct MeshDump {
    ID3D11Buffer* vb[4] = {};
    UINT stride[4] = {}, offset[4] = {};
    ID3D11Buffer* ib = nullptr;
    DXGI_FORMAT ibFormat = DXGI_FORMAT_UNKNOWN;
    UINT ibOffset = 0;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    DrawKey key{};
    std::vector<uint8_t> bones;
};
std::mutex g_dumpMutex;
MeshDump* g_dump = nullptr;  // taken on a draw thread, written out by the next Probe

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
void ReadWeaponSelection() {
    static FILETIME lastWrite{};
    wchar_t path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ReadWeaponSelection), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\'))
        wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"weapon.txt");
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &info) ||
        CompareFileTime(&info.ftLastWriteTime, &lastWrite) == 0)
        return;
    lastWrite = info.ftLastWriteTime;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"r") != 0 || !f)
        return;
    char text[1024] = {};
    fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    // Line by line:
    //   "37 shift 0 -0.3 0"  the colour pass's bones and the shift (metres, in the bones' space);
    //                        "hide" instead of the shift makes them vanish
    //   "pass 2: 41"         bones of kWeaponPasses[2] (its own palette) to treat alike
    //   "watch"              log which bones move, per pass;  "dump"  write the gun's mesh once
    //   "floor 0.05"         the ground's height in the body's space, for the dropped magazine
    float shift[3] = {}, floor = 0.0f;
    bool hide = false, watch = false, dump = false, any = false;
    DrawKey dumpKey = kWeapon;
    PassSelection selected[kWeaponPassCount] = {};
    for (char* line = text; line && *line;) {
        char* next = std::strchr(line, '\n');
        if (next)
            *next++ = 0;
        if (const char* f = std::strstr(line, "floor")) {
            sscanf_s(f + 5, "%f", &floor);
            line = next;
            continue;
        }
        if (const char* d = std::strstr(line, "dump")) {
            dump = true;
            UINT count = 0, start = 0;
            dumpKey = sscanf_s(d + 4, "%u %u", &count, &start) == 2 ? DrawKey{count, start, 0} : kWeapon;
            line = next;
            continue;
        }
        watch |= std::strstr(line, "watch") != nullptr;
        if (std::strstr(line, "trace")) {
            std::lock_guard lock(g_traceMutex);
            g_trace.clear();
            g_tracing = true;
            line = next;
            continue;
        }
        size_t pass = 0;
        const char* list = line;
        if (const char* p = std::strstr(line, "pass")) {
            pass = std::strtoul(p + 4, nullptr, 10);
            const char* colon = std::strchr(p, ':');
            list = colon ? colon + 1 : "";
            if (pass >= kWeaponPassCount)
                list = "";
        } else {
            hide |= std::strstr(line, "hide") != nullptr;
            if (char* s = std::strstr(line, "shift")) {
                sscanf_s(s + 5, "%f %f %f", &shift[0], &shift[1], &shift[2]);
                *s = 0;
            }
        }
        for (const char* p = list; *p;) {
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
                    selected[pass].bones[k] = selected[pass].any = any = true;
        }
        line = next;
    }
    {
        std::lock_guard lock(g_selMutex);
        std::copy(std::begin(selected), std::end(selected), std::begin(g_weaponSelected));
        std::memcpy(g_weaponOffset, shift, sizeof(shift));
        g_weaponHide = hide;
    }
    g_weaponShift = any;
    g_weaponWatch = watch;
    if (dump) {
        g_dumpKey = dumpKey;
        g_dumpWanted = true;
    }
    if (floor != g_floor.load())
        Log("Weapon: the floor for a dropped magazine is at %.2f m", floor);
    g_floor = floor;
    Log("Weapon: bones %s (in %d passes), %s; watch %s%s", any ? "chosen" : "none",
        static_cast<int>(std::count_if(std::begin(selected), std::end(selected),
                                       [](const PassSelection& s) { return s.any; })),
        hide ? "hidden" : "shifted", watch ? "on" : "off", dump ? "; dumping the gun's mesh" : "");
    if (any && !hide)
        Log("Weapon: shift (%.2f, %.2f, %.2f)", shift[0], shift[1], shift[2]);
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
        bool watch = g_weaponWatch.load(std::memory_order_relaxed);
        if (Near(Length3(m[0], m[4], m[8]), game) && Near(Length3(m[1], m[5], m[9]), game) &&
            Near(Length3(m[3], m[7], m[11]), 1.0f)) {
            if (watch)
                NoteProjection(m, size, i * 4, true);
            for (int r = 0; r < 4; ++r)
                m[4 * r] *= k, m[4 * r + 1] *= k;
        } else if (Near(Length3(m[0], m[1], m[2]), game) && Near(Length3(m[4], m[5], m[6]), game) &&
                   Near(Length3(m[12], m[13], m[14]), 1.0f)) {
            if (watch)
                NoteProjection(m, size, i * 4, false);
            NoteFirstPersonView(m, size, i * 4);
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
            if (g_weaponWatch.load(std::memory_order_relaxed))
                NoteRecentUpload(p.resource, data, p.size);  // before the rescale changes it
            t_uploadResource = p.resource;
            RescaleProjections(data, p.size, kSmall);
            t_uploadResource = nullptr;
            if (research)
                Inspect(data, p.size, true);
            // Every upload the gloves' size, not only those whose bones all passed as matrices:
            // only a third did (scaled bones break the row).
            // The gun's draws read only its first bones: those are always kept, for the magazine.
            // Whole uploads (some 600 a frame) only while something else needs them.
            if (p.size == kGloveUploadSize) {
                bool whole = g_handShift.load(std::memory_order_relaxed) ||
                             g_weaponShift.load(std::memory_order_relaxed) ||
                             g_weaponWatch.load(std::memory_order_relaxed) ||
                             g_dumpWanted.load(std::memory_order_relaxed) ||
                             g_handsActive.load(std::memory_order_relaxed);
                t_upload.resource = p.resource;
                t_upload.bytes.assign(data, data + (whole ? p.size : kGunPrefix));
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

// The vertex shader's constant buffer slot a buffer is bound in, or -1.
int BoundSlot(ID3D11DeviceContext* ctx, ID3D11Resource* resource) {
    ID3D11Buffer* bound[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ctx->VSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, bound);
    int slot = -1;
    for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i) {
        if (bound[i] && bound[i] == resource)
            slot = static_cast<int>(i);
        if (bound[i])
            bound[i]->Release();
    }
    return slot;
}

// This thread's own constant buffer of that size, made on first use.
ID3D11Buffer* OwnBuffer(ID3D11DeviceContext* ctx, UINT size) {
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
            return nullptr;
        t_shiftSize = size;
    }
    return t_shiftBuffer;
}

// Draws with these bones bound in place of BF3's buffer (in the slot it's in), then puts BF3's
// back. The buffer is refilled with WRITE_DISCARD, so it can serve several draws in a row.
// Cracks in the gloves' mesh (tools/handview.cpp "a=1"): three thin slivers where two edges meet but
// were never joined, their sides on different bones (the middle and ring fingers' webbing, the back
// of the little finger's knuckle, the thumb's web). Hidden in the game's own poses, they opened into
// holes with the fingers spread or curled (user, 2026-10-30). Each is closed with a triangle of the
// same vertices of the gloves' buffer, both ways round (the one facing out is drawn).
constexpr uint16_t kGlovePatch[] = {7, 8, 1723, 7, 1723, 8, 9, 11, 1597, 9, 1597, 11, 13, 14, 1393, 13, 1393, 14};

void DrawGlovePatch(int which, ID3D11DeviceContext* ctx, INT base) {
    static std::once_flag created;
    static ID3D11Buffer* s_patch = nullptr;
    std::call_once(created, [ctx] {
        ID3D11Device* device = nullptr;
        ctx->GetDevice(&device);
        if (!device)
            return;
        D3D11_BUFFER_DESC desc{sizeof(kGlovePatch), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0};
        D3D11_SUBRESOURCE_DATA data{kGlovePatch, 0, 0};
        if (FAILED(device->CreateBuffer(&desc, &data, &s_patch)))
            s_patch = nullptr;
        device->Release();
    });
    if (!s_patch)
        return;
    ID3D11Buffer* indices = nullptr;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT offset = 0;
    ctx->IAGetIndexBuffer(&indices, &format, &offset);
    ctx->IASetIndexBuffer(s_patch, DXGI_FORMAT_R16_UINT, 0);
    g_origDrawIndexed[which](ctx, static_cast<UINT>(std::size(kGlovePatch)), 0, base);
    ctx->IASetIndexBuffer(indices, format, offset);
    if (indices)
        indices->Release();
}

bool DrawWithPalette(int which, ID3D11DeviceContext* ctx, UINT count, UINT start, INT base,
                     ID3D11Resource* original, const uint8_t* bytes, UINT size, bool patchGloves = false) {
    int slot = BoundSlot(ctx, original);
    ID3D11Buffer* own = slot < 0 ? nullptr : OwnBuffer(ctx, size);
    D3D11_MAPPED_SUBRESOURCE mapped;
    // BF3's own Map/Unmap, not the hooked ones: this upload isn't one to inspect.
    if (!own || FAILED(g_origMap[which](ctx, own, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return false;
    std::memcpy(mapped.pData, bytes, size);
    g_origUnmap[which](ctx, own, 0);
    ctx->VSSetConstantBuffers(slot, 1, &own);
    g_origDrawIndexed[which](ctx, count, start, base);
    if (patchGloves)
        DrawGlovePatch(which, ctx, base);
    auto buffer = static_cast<ID3D11Buffer*>(original);
    ctx->VSSetConstantBuffers(slot, 1, &buffer);
    return true;
}

// Draws the gloves (or with weapon set, a pass of the gun, those bones) with a shifted copy of
// their bones: the copy goes into this thread's own constant buffer, bound in the slot where BF3's
// buffer is, then BF3's is put back.
bool DrawShifted(int which, ID3D11DeviceContext* ctx, UINT count, UINT start, INT base, UINT bones,
                 const PassSelection* weapon = nullptr) {
    const Upload& u = t_upload;
    if (!u.resource || u.bytes.size() != kGloveUploadSize) {  // or only the gun's first bones
        ++g_noCopy;
        return false;
    }
    if (weapon && !PlayersOwn(u.bytes)) {
        ++g_weaponFar;
        return false;
    }
    int slot = BoundSlot(ctx, u.resource);
    if (slot < 0) {
        ++g_noSlot;
        return false;
    }

    UINT size = static_cast<UINT>(u.bytes.size());
    if (!OwnBuffer(ctx, size))
        return false;
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
    bool gloves = !weapon && bones == kHandDraws[0].bones;
    std::lock_guard lock(g_selMutex);
    if (gloves)
        g_selValues.clear();
    const float* shift = weapon ? g_weaponOffset : g_shift;
    for (UINT k = 0; k < bones && k * 12 + 12 <= size / 4; ++k) {
        float* bone = m + k * 12;
        bool selected = weapon ? k < 64 && weapon->bones[k]
                               : g_selectAll || (gloves ? k < 64 && g_selected[k] : MatchesSelected(bone));
        if (!selected)
            continue;
        if (weapon && g_weaponHide) {
            std::memset(bone, 0, 12 * sizeof(float));  // its vertices all land on one point
            continue;
        }
        if (gloves && !g_selectAll)
            g_selValues.insert(g_selValues.end(), bone, bone + 12);
        bone[3] += shift[0];  // translation (y is up in character space)
        bone[7] += shift[1];
        bone[11] += shift[2];
    }
    if (weapon)
        ++g_weaponShifted;
    g_origUnmap[which](ctx, t_shiftBuffer, 0);

    ctx->VSSetConstantBuffers(slot, 1, &t_shiftBuffer);
    g_origDrawIndexed[which](ctx, count, start, base);
    auto original = static_cast<ID3D11Buffer*>(u.resource);
    ctx->VSSetConstantBuffers(slot, 1, &original);
    ++g_shifted;
    return true;
}

// ---- Magazine ----
// X ejects the magazine: it vanishes from the gun, falls to the floor and lies there a while. The
// mesh dump showed it's a bone of its own in each of the gun's passes: bone 4 of the colour pass (a
// 21 cm part below the gun), bone 0 of the 312-index draw (the rounds at its top), bones 4 and 5
// of the depth pass that draws the gun whole. The gun's draws use bones 0-5 only.
struct MagBone {
    size_t pass;
    UINT bone;
};
constexpr MagBone kMagazineBones[] = {{0, 4}, {1, 0}, {2, 4}, {2, 5}};
constexpr size_t kMagBones = std::size(kMagazineBones);
// The magazine's box in the gun's model space (from the dump): its centre is what it tumbles
// around, its corners what touches the floor.
constexpr float kMagMin[3] = {-0.011f, -0.304f, 0.038f}, kMagMax[3] = {0.011f, -0.090f, 0.131f};
constexpr float kGravity = 9.81f;
constexpr float kEjectSpeed = 0.8f;  // m/s out of the magazine well
constexpr float kSpin = 4.0f;        // rad/s, end over end
// On the floor before it goes. Lying there it followed the player around (tested 2026-10-30): the
// body's space isn't anchored to the world the way UpdateDrop assumes. Until it is, it vanishes as
// it lands.
constexpr double kLieSeconds = 0.0;

std::atomic<bool> g_magOut{false};       // hidden in the gun
std::atomic<bool> g_dropShown{false};    // the dropped one is drawn
std::atomic<ULONGLONG> g_rifleDrawn{0};  // when the gun with this magazine was last drawn

struct Drop {
    float last[kMagBones][12] = {};   // the magazine's bones as last drawn in the gun
    bool seen[kMagBones] = {};
    float start[kMagBones][12] = {};  // ... when it came out
    bool started[kMagBones] = {};
    float c0[3] = {};                 // its centre then
    float pos[3] = {}, vel[3] = {};   // its centre since, and its speed
    float axis[3] = {1, 0, 0};        // what it tumbles around
    float angle = 0.0f;
    float q[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};  // its turn since it came out
    float centre[3] = {};             // where it's drawn: pos, kept still in the world
    float origin0[3] = {};            // the arms' space's origin when it came out
    bool falling = false;
    double landedAt = 0.0, updatedAt = 0.0;
};
std::mutex g_magMutex;
Drop g_drop;

double Seconds() {
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / frequency;
}

// A turn of angle radians around the unit axis a, as a row-major 3x3 matrix.
void AxisAngle(const float* a, float angle, float* q) {
    float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
    q[0] = t * a[0] * a[0] + c, q[1] = t * a[0] * a[1] - s * a[2], q[2] = t * a[0] * a[2] + s * a[1];
    q[3] = t * a[0] * a[1] + s * a[2], q[4] = t * a[1] * a[1] + c, q[5] = t * a[1] * a[2] - s * a[0];
    q[6] = t * a[0] * a[2] - s * a[1], q[7] = t * a[1] * a[2] + s * a[0], q[8] = t * a[2] * a[2] + c;
}

void Normalize(float* v) {
    float length = Length3(v[0], v[1], v[2]);
    if (length > 1e-6f)
        v[0] /= length, v[1] /= length, v[2] /= length;
}

// A bone matrix (three rows: rotation | translation) moved the way the dropped magazine has moved:
// turned by q around its centre at the start, c0, which is now at centre.
void MoveBone(const float* m, const Drop& d, float* out) {
    for (int i = 0; i < 3; ++i) {
        const float* q = d.q + i * 3;
        for (int j = 0; j < 3; ++j)
            out[i * 4 + j] = q[0] * m[j] + q[1] * m[4 + j] + q[2] * m[8 + j];
        out[i * 4 + 3] = q[0] * (m[3] - d.c0[0]) + q[1] * (m[7] - d.c0[1]) + q[2] * (m[11] - d.c0[2]) + d.centre[i];
    }
}

// ---- Free hands (hands.cpp works out where they go) ----
std::mutex g_handsMutex;
bool g_handsOn = false;
skin::HandTargets g_hands;
// The move found this frame: the gun's (and the right hand's on it), from its body bone, for draws
// that don't have that bone.
float g_gunDelta[12] = {};
bool g_haveGunDelta = false;
constexpr float kEyeHeight = 1.6f;  // a dropped magazine's floor this far below the eye (standing)
// Bones placed from a bone of another draw (the gun's body): their offset from it, learned as
// they're drawn. A move worked out in that other draw and reused here was a frame off whenever the
// game's own gun bobbed (walking): black copies, flicker (user's video).
struct Learned {
    float rel[kPaletteBones][12] = {};
    bool have[kPaletteBones] = {};
};
std::unordered_map<DrawKey, Learned, DrawKeyHash> g_learned;  // under g_handsMutex
// Which frame the gun's body was drawn in (the count of view uploads then: a frame's start uploads
// its views, its draws follow). An offset is only learned from a reference of the same frame:
// across frames the arms' space moves a frame's walk, or a whole metre when its origin jumps, and
// the right glove, scope and stock jumped about while walking (user's video, 2026-10-30).
uint32_t g_gunBodyView = 0;
// Draws of the gun and arms that couldn't be moved, since the last log: no copy of their bones,
// someone else's, or BF3's bone buffer not bound.
std::atomic<uint32_t> g_handsUnmoved{0}, g_handsMovedDraws{0};
// ... by draw and why: 0 no copy of a whole bone buffer, 1 someone else's bones, 2 BF3's buffer not
// bound for the draw (the copy is of another upload).
std::mutex g_unmovedMutex;
std::map<std::tuple<UINT, UINT, int>, uint32_t> g_unmovedWhy;

void NoteUnmoved(const DrawKey& key, int why) {
    ++g_handsUnmoved;
    std::lock_guard lock(g_unmovedMutex);
    ++g_unmovedWhy[{key.indexCount, key.startIndex, why}];
}

// A bone's offset from a reference bone (both as the game drew them in the same frame).
void Learn(Learned& l, size_t k, const float* referenceInverse, const float* bone) {
    Compose(referenceInverse, bone, l.rel[k]);
    l.have[k] = true;
}

// Affine 3x4 matrices: three rows, rotation | translation.
void Compose(const float* a, const float* b, float* out) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j)
            out[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j];
        out[i * 4 + 3] += a[i * 4 + 3];
    }
}

bool Invert(const float* m, float* out) {
    float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], k = m[10];
    float det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
    if (!(std::fabs(det) > 1e-8f))
        return false;
    float r[9] = {(e * k - f * h) / det, (c * h - b * k) / det, (b * f - c * e) / det,
                  (f * g - d * k) / det, (a * k - c * g) / det, (c * d - a * f) / det,
                  (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
    for (int i = 0; i < 3; ++i) {
        out[i * 4] = r[i * 3], out[i * 4 + 1] = r[i * 3 + 1], out[i * 4 + 2] = r[i * 3 + 2];
        out[i * 4 + 3] = -(r[i * 3] * m[3] + r[i * 3 + 1] * m[7] + r[i * 3 + 2] * m[11]);
    }
    return true;
}

// A bone moved by delta (in place).
void MoveBy(const float* delta, float* bone) {
    float moved[12];
    Compose(delta, bone, moved);
    std::memcpy(bone, moved, sizeof(moved));
}

// A bone whose vertices all land on one point: folded away.
void Collapse(float* bone, const float* point) {
    std::memset(bone, 0, 12 * sizeof(float));
    bone[3] = point[0], bone[7] = point[1], bone[11] = point[2];
}

// The first-person arms' draws (the mesh dumps, 2026-10-30), each with a palette of its own, and
// what each palette entry is, from where its vertices sit in the bind pose: L / R a hand's bone,
// moved with that hand; l / r that side's arm or sleeve, folded into its hand (its triangles shrink
// into the glove); c the body and legs, folded onto the line between the hands (zero-area
// triangles, never drawn); '-' no vertices.
struct ArmDraw {
    DrawKey key;
    const char* roles;
    int rightRoot = -1;  // the right hand's root bone, where the draw has it (for the gun's grip)
    // Which of the gloves' left-hand bones (handpose.cpp's 0-22) each 'L' entry is, where the
    // numbering isn't the gloves' own (-1: none, moved with the glove's cuff).
    const int8_t* leftBones = nullptr;
    size_t leftBoneCount = 0;
};
// Matched by vertices at the same place in the gloves' mesh dump and these depth passes', so the
// fingers bend alike in every pass. The bare forearms share no vertices with the gloves: their 'L'
// entries, at the wrist, go with the cuff.
constexpr int8_t kGloveDepthLeft[] = {-1, -1, 14, 13, 16, -1, 8, 9, 19, 15, 18, 17, 2};
constexpr int8_t kBodyDepthLeft[] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 4,
                                     0,  1,  6,  7,  21, 22, 12, 11, 20, 10, 14, 13, 16, 3,  8,  9,  19, 15, 18, 17, 2};
constexpr int8_t kForearmLeft[] = {-1};
// The right hand root's vertices' centre in the gloves' bind pose (the mesh dump).
constexpr float kRightPalm[3] = {-0.505f, 1.129f, 0.150f};
// The gun's body bone as the game animated it, and from it and the right hand, where the game's
// hand holds the gun in the gun's model space (smoothed).
float g_gunBodyOrig[12] = {};
bool g_haveGunBodyOrig = false;
float g_gripModel[3] = {};
bool g_haveGrip = false;
constexpr ArmDraw kArmDraws[] = {
    {{13755, 15051, 0}, "LLLLLLLLLLLLLLLLLLLLLLLRRRRRRRRRRRRRRRRRRRRRR", 37},  // gloves
    {{7617, 49995, 0}, "--LLL-LLLLLLLrrrRRRRRRRRRRRRRRRRRRRRRRr", -1, kGloveDepthLeft,
     std::size(kGloveDepthLeft)},  // their depth
    {{2016, 13035, 0}, "llLlL-l-LLrrRrR-rR", -1, kForearmLeft, std::size(kForearmLeft)},  // bare forearms
    {{8529, 0, 0}, "cllllll-rrrrrr", -1},                                                 // chest, sleeves
    {{21189, 28806, 0}, "cc-ccccccccccccllllLLLLLLLLLLLLLLLLLLLLLLlrrrrrRRRRRRRr", -1, kBodyDepthLeft,
     std::size(kBodyDepthLeft)},              // body's depth
    {{4506, 8529, 0}, "c---ccccccccccccc", -1},  // legs
};

// The gun's other skinned model (the mesh dumps: 7848 + 1440 indices drawn in colour, the 9288 of
// its depth pass; bones 0-2 near the gun's back end, the stock): it goes wherever the gun goes.
// The scope is a rigid model (no bone weights) placed by the first matrix of a bone buffer of its
// own, which has the gun's turn (the scope probe, 2026-10-30): it moves as a one-bone part.
constexpr ArmDraw kGunPartDraws[] = {
    {{7848, 0, 0}, "GG", -1},     {{1440, 7848, 0}, "G", -1},  {{9288, 9288, 0}, "GGG", -1},
    {{9273, 12, 0}, "G", -1},     {{12, 0, 0}, "G", -1},       {{72, 9285, 0}, "G", -1},
    {{129, 9357, 0}, "G", -1},    {{108, 9486, 0}, "G", -1},   {{9273, 9594, 0}, "G", -1},
};

const ArmDraw* FindArmDraw(const DrawKey& key) {
    for (const ArmDraw& d : kGunPartDraws)
        if (d.key == key)
            return &d;
    for (const ArmDraw& d : kArmDraws)
        if (d.key == key)
            return &d;
    return nullptr;
}

// The dropped magazine's fall, once a frame: gravity, a tumble, and a stop at the floor (its box's
// lowest corner). The arms' space the bones are in stays put in the world, except that its origin
// jumps a whole metre now and then as the player walks: that jump is taken off.
void UpdateDrop() {
    float origin[3] = {};
    bool haveOrigin = false;
    float floor = g_floor.load();
    {
        std::lock_guard lock(g_fpMutex);
        if (floor == 0.0f && g_haveViewToArms)
            floor = g_viewToArms[7] - kEyeHeight;  // the eye's height in the arms' space
    }
    {
        std::lock_guard lock(g_fpMutex);
        if (g_haveFpOrigin) {
            std::memcpy(origin, g_fpOrigin, sizeof(origin));
            haveOrigin = true;
        }
    }
    std::lock_guard lock(g_magMutex);
    Drop& d = g_drop;
    if (!g_dropShown.load())
        return;
    double now = Seconds();
    float dt = static_cast<float>(std::min(now - d.updatedAt, 0.05));
    d.updatedAt = now;
    if (d.falling) {
        d.vel[1] -= kGravity * dt;
        for (int i = 0; i < 3; ++i)
            d.pos[i] += d.vel[i] * dt;
        d.angle += kSpin * dt;
        AxisAngle(d.axis, d.angle, d.q);
        const float* m = d.start[0];
        float lowest = 1e9f;
        for (int c = 0; c < 8; ++c) {
            float v[3] = {c & 1 ? kMagMax[0] : kMagMin[0], c & 2 ? kMagMax[1] : kMagMin[1],
                          c & 4 ? kMagMax[2] : kMagMin[2]};
            float p[3];
            for (int i = 0; i < 3; ++i)
                p[i] = m[i * 4] * v[0] + m[i * 4 + 1] * v[1] + m[i * 4 + 2] * v[2] + m[i * 4 + 3] - d.c0[i];
            lowest = std::min(lowest, d.q[3] * p[0] + d.q[4] * p[1] + d.q[5] * p[2] + d.pos[1]);
        }
        if (lowest <= floor) {
            d.pos[1] += floor - lowest;
            d.falling = false;
            d.landedAt = now;
        }
    } else if (now - d.landedAt > kLieSeconds) {
        g_dropShown = false;
        return;
    }
    for (int i = 0; i < 3; ++i)
        d.centre[i] = d.pos[i] - (haveOrigin ? origin[i] - d.origin0[i] : 0.0f);
}

bool StartDrop() {
    float origin[3] = {};
    {
        std::lock_guard lock(g_fpMutex);
        std::memcpy(origin, g_fpOrigin, sizeof(origin));
    }
    std::lock_guard lock(g_magMutex);
    Drop& d = g_drop;
    if (!d.seen[0])
        return false;  // the gun hasn't been drawn
    std::memcpy(d.start, d.last, sizeof(d.start));
    std::memcpy(d.started, d.seen, sizeof(d.started));
    const float* m = d.start[0];
    float cm[3] = {(kMagMin[0] + kMagMax[0]) / 2, (kMagMin[1] + kMagMax[1]) / 2, (kMagMin[2] + kMagMax[2]) / 2};
    for (int i = 0; i < 3; ++i)
        d.c0[i] = d.pos[i] = d.centre[i] = m[i * 4] * cm[0] + m[i * 4 + 1] * cm[1] + m[i * 4 + 2] * cm[2] + m[i * 4 + 3];
    // Out of the well along its own length (the model's -y), tumbling around the gun's sideways
    // axis (the model's x).
    float down[3] = {-m[1], -m[5], -m[9]};
    Normalize(down);
    for (int i = 0; i < 3; ++i)
        d.vel[i] = down[i] * kEjectSpeed;
    d.axis[0] = m[0], d.axis[1] = m[4], d.axis[2] = m[8];
    Normalize(d.axis);
    d.angle = 0.0f;
    AxisAngle(d.axis, 0.0f, d.q);
    std::memcpy(d.origin0, origin, sizeof(origin));
    d.falling = true;
    d.updatedAt = Seconds();
    g_dropShown = true;
    return true;
}

// One of the gun's passes: moved into the right hand (free hands), without its magazine while it's
// out, and drawn again with only the dropped magazine's bones while that falls. Its magazine bones
// are noted, as drawn, for the moment it comes out. False: draw as usual.
bool DrawGun(int which, ID3D11DeviceContext* ctx, UINT count, UINT start, INT base, size_t pass) {
    const Upload& u = t_upload;
    if (!u.resource || u.bytes.size() < kGunPrefix || !PlayersOwn(u.bytes))
        return false;
    if (pass == 0)
        g_rifleDrawn = GetTickCount64();
    bool out = g_magOut.load(std::memory_order_relaxed), shown = g_dropShown.load(std::memory_order_relaxed);
    // Whole palettes as big as BF3's buffer; the gun reads only its first bones.
    thread_local std::vector<uint8_t> t_gun, t_dropped;
    t_gun.assign(kGloveUploadSize, 0);
    std::memcpy(t_gun.data(), u.bytes.data(), kGunPrefix);
    auto* bones = reinterpret_cast<float*>(t_gun.data());
    bool moved = false;
    {
        std::lock_guard lock(g_handsMutex);
        if (pass != 1) {  // bone 2, the gun's body, in the colour and the depth pass
            std::memcpy(g_gunBodyOrig, bones + 2 * 12, sizeof(g_gunBodyOrig));
            g_haveGunBodyOrig = true;
            g_gunBodyView = g_viewUploads.load();
        }
        skin::HandTargets t = g_hands;
        if (g_handsOn && t.gun && ToArms(t, ctx)) {
            // Bone 2 is the gun's body in the colour and depth passes: the move comes from this
            // draw's own. The rounds' draw has none: its bone goes where its learned offset from
            // the gun's body (as last drawn) puts it.
            float inverse[12];
            if (pass != 1 && Invert(bones + 2 * 12, inverse)) {
                Compose(t.gunPose, inverse, g_gunDelta);
                g_haveGunDelta = true;
                for (int k = 0; k < 6; ++k)
                    MoveBy(g_gunDelta, bones + k * 12);
                moved = true;
                if (pass == 0 && g_tracing.load(std::memory_order_relaxed)) {
                    TraceRow r{Seconds(), GetCurrentThreadId(), {}, {g_gunBodyOrig[3], g_gunBodyOrig[7], g_gunBodyOrig[11]},
                               g_viewUploads.load()};
                    {
                        std::lock_guard fp(g_fpMutex);
                        r.eye[0] = g_viewToArms[3], r.eye[1] = g_viewToArms[7], r.eye[2] = g_viewToArms[11];
                    }
                    std::lock_guard trace(g_traceMutex);
                    g_trace.push_back(r);
                    if (g_trace.size() >= 120)
                        g_tracing = false;
                }
            } else if (pass == 1) {
                Learned& l = g_learned[kWeaponPasses[1]];
                if (g_haveGunBodyOrig && g_gunBodyView == g_viewUploads.load() && Invert(g_gunBodyOrig, inverse))
                    Learn(l, 0, inverse, bones);
                if (l.have[0]) {
                    Compose(t.gunPose, l.rel[0], bones);
                    moved = true;
                }
            }
        }
    }
    {
        std::lock_guard lock(g_magMutex);
        Drop& d = g_drop;
        if (!out) {
            for (size_t i = 0; i < kMagBones; ++i) {
                if (kMagazineBones[i].pass != pass)
                    continue;
                std::memcpy(d.last[i], bones + kMagazineBones[i].bone * 12, sizeof(d.last[i]));
                d.seen[i] = true;
            }
        }
        if (!moved && !out && !shown)
            return false;
        if (out)
            for (size_t i = 0; i < kMagBones; ++i)
                if (kMagazineBones[i].pass == pass)
                    std::memset(bones + kMagazineBones[i].bone * 12, 0, 48);  // all on one point
        if (shown) {
            t_dropped.assign(kGloveUploadSize, 0);  // every other bone of the gun on one point
            for (size_t i = 0; i < kMagBones; ++i)
                if (kMagazineBones[i].pass == pass && d.started[i])
                    MoveBone(d.start[i], d, reinterpret_cast<float*>(t_dropped.data() + kMagazineBones[i].bone * 48));
        }
    }
    if (!(moved || out) ||
        !DrawWithPalette(which, ctx, count, start, base, u.resource, t_gun.data(), kGloveUploadSize)) {
        g_origDrawIndexed[which](ctx, count, start, base);
        if (moved || out)
            NoteUnmoved(kWeaponPasses[pass], 2);
    } else if (moved) {
        ++g_handsMovedDraws;
    }
    if (shown)
        DrawWithPalette(which, ctx, count, start, base, u.resource, t_dropped.data(), kGloveUploadSize);
    return true;
}

// One of the first-person arms' draws with free hands on: each hand's bones moved with it, the
// arms, sleeves and body folded away. False: draw as usual.
bool DrawArms(int which, ID3D11DeviceContext* ctx, UINT count, UINT start, INT base, const ArmDraw& draw) {
    const Upload& u = t_upload;
    if (GetTickCount64() - g_rifleDrawn.load() > 500)  // the gun the mod knows isn't in hand
        return false;
    if (!u.resource || u.bytes.size() != kGloveUploadSize || !PlayersOwn(u.bytes)) {
        NoteUnmoved(draw.key, !u.resource || u.bytes.size() != kGloveUploadSize ? 0 : 1);
        return false;
    }
    thread_local std::vector<uint8_t> t_arms;
    t_arms = u.bytes;
    auto* bones = reinterpret_cast<float*>(t_arms.data());
    {
        std::lock_guard lock(g_handsMutex);
        if (!g_handsOn)
            return false;
        skin::HandTargets t = g_hands;
        if (!ToArms(t, ctx))
            return false;
        float inverse[12];
        // Where the game's own right hand holds the gun: its palm, as the game drew it, in the
        // gun's model space.
        uint32_t frame = g_viewUploads.load();
        if (draw.rightRoot >= 0 && g_haveGunBodyOrig && g_gunBodyView == frame && Invert(g_gunBodyOrig, inverse)) {
            const float* m = bones + draw.rightRoot * 12;
            float palm[3], grip[3];
            for (int i = 0; i < 3; ++i)
                palm[i] = m[i * 4] * kRightPalm[0] + m[i * 4 + 1] * kRightPalm[1] + m[i * 4 + 2] * kRightPalm[2] +
                          m[i * 4 + 3];
            for (int i = 0; i < 3; ++i)
                grip[i] = inverse[i * 4] * palm[0] + inverse[i * 4 + 1] * palm[1] + inverse[i * 4 + 2] * palm[2] +
                          inverse[i * 4 + 3];
            if (Length3(grip[0], grip[1], grip[2]) < 1.0f) {  // within the gun's reach: a real hold
                for (int i = 0; i < 3; ++i)
                    g_gripModel[i] = g_haveGrip ? g_gripModel[i] * 0.9f + grip[i] * 0.1f : grip[i];
                g_haveGrip = true;
            }
        }
        // The gun's body drawn earlier in this same frame, to learn offsets from.
        float gunInverse[12];
        bool gunRef = g_haveGunBodyOrig && g_gunBodyView == frame && Invert(g_gunBodyOrig, gunInverse);
        Learned& learned = g_learned[draw.key];
        float mid[3];
        for (int i = 0; i < 3; ++i)
            mid[i] = t.left && t.gun ? (t.leftWrist[i] + t.rightWrist[i]) / 2 : t.left ? t.leftWrist[i] : t.rightWrist[i];
        for (size_t k = 0; draw.roles[k] && k < kPaletteBones; ++k) {
            float* bone = bones + k * 12;
            switch (draw.roles[k]) {
            case 'L':
                // The whole hand placed as one, each bone bent as its finger is held. (Blending the
                // matrices towards the game's own grip and past it folded the hand flat: user,
                // 2026-10-30.)
                if (t.left) {
                    int g = !draw.leftBones ? static_cast<int>(k) : k < draw.leftBoneCount ? draw.leftBones[k] : -1;
                    Compose(t.leftPose, t.leftFingers[g >= 0 && g < handpose::kBones ? g : 0], bone);
                }
                break;
            case 'R':
            case 'G':  // a part of the gun: where its learned offset from the gun's body puts it
                if (t.gun) {
                    if (gunRef)
                        Learn(learned, k, gunInverse, bone);
                    if (learned.have[k])
                        Compose(t.gunPose, learned.rel[k], bone);
                }
                break;
            case 'l':
                if (t.left)
                    Collapse(bone, t.leftWrist);
                break;
            case 'r':
                if (t.gun)
                    Collapse(bone, t.rightWrist);
                break;
            case 'c':
                Collapse(bone, mid);
                break;
            }
        }
    }
    bool drawn = DrawWithPalette(which, ctx, count, start, base, u.resource, t_arms.data(), kGloveUploadSize,
                                 &draw == &kArmDraws[0]);  // the gloves: their cracks closed
    if (drawn)
        ++g_handsMovedDraws;
    else
        NoteUnmoved(draw.key, 2);
    return drawn;
}

// weapon.txt "watch": the scope is a rigid model (no bones: the mesh dump of its 9273 + 12 + 72 +
// 129 + 108 indices has no weights), placed by a matrix in some constant buffer. Each thread's
// latest small uploads are kept, and at the scope's draw those still bound to the vertex shader
// are searched for transforms, to find which one places it.
constexpr DrawKey kScope{9273, 12, 0};
struct RecentUpload {
    ID3D11Resource* resource = nullptr;
    UINT size = 0;
    uint8_t bytes[4096];
};
thread_local RecentUpload t_recent[8];
thread_local int t_recentNext = 0;

void NoteRecentUpload(ID3D11Resource* resource, const uint8_t* data, UINT size) {
    RecentUpload& r = t_recent[t_recentNext];
    t_recentNext = (t_recentNext + 1) % 8;
    r.resource = resource;
    r.size = std::min(size, static_cast<UINT>(sizeof(r.bytes)));
    std::memcpy(r.bytes, data, r.size);
}

void ProbeScope(ID3D11DeviceContext* ctx) {
    static std::atomic<ULONGLONG> lastLog{0};
    ULONGLONG now = GetTickCount64();
    if (now - lastLog.load() < 2000)
        return;
    lastLog = now;
    ID3D11Buffer* bound[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ctx->VSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, bound);
    for (UINT slot = 0; slot < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++slot) {
        if (!bound[slot])
            continue;
        D3D11_BUFFER_DESC desc{};
        bound[slot]->GetDesc(&desc);
        bool recent = false;
        for (const RecentUpload& r : t_recent)
            recent |= r.resource == bound[slot];
        Log("Scope probe: vertex shader constant buffer slot %u: %u bytes, %s", slot, desc.ByteWidth,
            recent ? "uploaded just before" : "not among this thread's latest uploads");
        for (const RecentUpload& r : t_recent) {
            if (r.resource != bound[slot] || !r.size)
                continue;
            const auto* f = reinterpret_cast<const float*>(r.bytes);
            int found = 0;
            for (UINT i = 0; i + 12 <= r.size / 4 && found < 6; i += 4) {
                const float* m = f + i;
                // Three rows with unit-length directions and a translation within 6 m: a placement.
                if (std::fabs(Length3(m[0], m[1], m[2]) - 1) > 0.05f || std::fabs(Length3(m[4], m[5], m[6]) - 1) > 0.05f ||
                    std::fabs(Length3(m[8], m[9], m[10]) - 1) > 0.05f || Length3(m[3], m[7], m[11]) > 6.0f)
                    continue;
                ++found;
                float w[4] = {};  // a fourth row, if there is one (4x4 with the translation there)
                if (i + 16 <= r.size / 4)
                    std::memcpy(w, m + 12, sizeof(w));
                Log("Scope probe: slot %u, %u-byte upload, +%u: (%.3f %.3f %.3f %.3f) (%.3f %.3f %.3f %.3f) (%.3f %.3f %.3f "
                    "%.3f) (%.3f %.3f %.3f %.3f)",
                    slot, r.size, i * 4, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], w[0],
                    w[1], w[2], w[3]);
            }
        }
    }
    for (ID3D11Buffer* b : bound)
        if (b)
            b->Release();
    std::lock_guard lock(g_handsMutex);
    const float* g = g_gunBodyOrig;
    Log("Scope probe: the gun's body for comparison: (%.3f %.3f %.3f %.3f) (%.3f %.3f %.3f %.3f) (%.3f %.3f %.3f %.3f)",
        g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9], g[10], g[11]);
}

// Which of the gun's passes a draw is, or kWeaponPassCount.
size_t WeaponPass(const DrawKey& key) {
    for (size_t i = 0; i < kWeaponPassCount; ++i)
        if (key == kWeaponPasses[i])
            return i;
    return kWeaponPassCount;
}

void WatchDraw(size_t pass) {
    const Upload& u = t_upload;
    if (!u.resource || u.bytes.size() < kPaletteBones * 48 || !PlayersOwn(u.bytes))
        return;
    const auto* m = reinterpret_cast<const float*>(u.bytes.data());
    const float* r = m + (pass == 0 ? kReceiverBone : 0) * 12;
    std::lock_guard lock(g_watchMutex);
    WatchPass& w = g_watch[pass];
    ++w.draws;
    for (UINT k = 0; k < kPaletteBones; ++k) {
        const float* b = m + k * 12;
        float d[3] = {b[3] - r[3], b[7] - r[7], b[11] - r[11]};
        for (int j = 0; j < 3; ++j) {
            float rel = r[j] * d[0] + r[4 + j] * d[1] + r[8 + j] * d[2];  // in the reference's axes
            w.lo[k][j] = w.any ? std::min(w.lo[k][j], rel) : rel;
            w.hi[k][j] = w.any ? std::max(w.hi[k][j], rel) : rel;
        }
    }
    w.any = true;
}

void CaptureMesh(ID3D11DeviceContext* ctx, const DrawKey& key) {
    if (!PlayersOwn(t_upload.bytes) || !g_dumpWanted.exchange(false))
        return;
    auto* d = new MeshDump;
    ctx->IAGetVertexBuffers(0, 4, d->vb, d->stride, d->offset);
    ctx->IAGetIndexBuffer(&d->ib, &d->ibFormat, &d->ibOffset);
    ctx->IAGetPrimitiveTopology(&d->topology);
    d->key = key;
    d->bones = t_upload.bytes;
    std::lock_guard lock(g_dumpMutex);
    std::swap(g_dump, d);
    if (d) {  // an earlier one not written yet
        for (ID3D11Buffer* b : d->vb)
            if (b)
                b->Release();
        if (d->ib)
            d->ib->Release();
        delete d;
    }
}

void DrawIndexed(int which, ID3D11DeviceContext* ctx, UINT count, UINT start, INT base) {
    if (!g_research.load(std::memory_order_relaxed)) {
        // Only the hands' and the gun's own draws matter: a couple of comparisons, no lock.
        if (g_handShift.load(std::memory_order_relaxed)) {
            DrawKey key{count, start, base};
            for (const HandDraw& h : kHandDraws)
                if (h.key == key && DrawShifted(which, ctx, count, start, base, h.bones))
                    return;
        }
        DrawKey key{count, start, base};
        if (g_dumpWanted.load(std::memory_order_acquire) && key == g_dumpKey)
            CaptureMesh(ctx, key);
        if (key == kScope && g_weaponWatch.load(std::memory_order_relaxed))
            ProbeScope(ctx);
        size_t pass = WeaponPass(key);
        if (pass < kWeaponPassCount) {
            if (g_weaponWatch.load(std::memory_order_relaxed))
                WatchDraw(pass);
            if (g_weaponShift.load(std::memory_order_relaxed) && g_weaponSelected[pass].any &&
                DrawShifted(which, ctx, count, start, base, kPaletteBones, &g_weaponSelected[pass]))
                return;
            if (DrawGun(which, ctx, count, start, base, pass))
                return;
        } else if (g_handsActive.load(std::memory_order_relaxed)) {
            if (const ArmDraw* arms = FindArmDraw(key); arms && DrawArms(which, ctx, count, start, base, *arms))
                return;
        }
        g_origDrawIndexed[which](ctx, count, start, base);
        return;
    }
    bool skinned = t_drawsSinceBones < kSkinnedWithin;
    ++t_drawsSinceBones;
    if (g_huntFirstPerson.load(std::memory_order_relaxed)) {
        ID3D11DepthStencilState* state = nullptr;
        UINT ref = 0;
        ctx->OMGetDepthStencilState(&state, &ref);
        if (state)
            state->Release();
        if (ref & kFirstPersonStencil) {
            DrawKey key{count, start, base};
            {
                std::lock_guard lock(g_drawMutex);
                ++g_firstPerson[key];
                if (g_haveFinal && key == g_final)
                    g_finalSeen = true;
            }
            const DrawSet* hidden = g_hidden.load(std::memory_order_acquire);
            if (hidden && hidden->count(key))
                return;
        }
    }
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
        size_t pass = WeaponPass(key);
        if (pass < kWeaponPassCount && g_weaponShift.load(std::memory_order_relaxed) &&
            g_weaponSelected[pass].any &&
            DrawShifted(which, ctx, count, start, base, kPaletteBones, &g_weaponSelected[pass]))
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

// The last second's watch, one line per pass: the bones that moved most against the reference.
void LogWatch() {
    std::lock_guard lock(g_watchMutex);
    for (size_t p = 0; p < kWeaponPassCount; ++p) {
        WatchPass& w = g_watch[p];
        std::vector<std::pair<float, UINT>> moved;
        for (UINT k = 0; w.any && k < kPaletteBones; ++k) {
            float range = Length3(w.hi[k][0] - w.lo[k][0], w.hi[k][1] - w.lo[k][1], w.hi[k][2] - w.lo[k][2]);
            if (range > 0.005f)
                moved.push_back({range, k});
        }
        std::sort(moved.begin(), moved.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        char text[512] = {};
        int used = 0;
        for (size_t i = 0; i < moved.size() && i < 12 && used < 460; ++i) {
            UINT k = moved[i].second;
            used += snprintf(text + used, sizeof(text) - used, " %u: %.1f cm (now %+.2f %+.2f %+.2f);", k,
                             moved[i].first * 100.0f, w.hi[k][0], w.hi[k][1], w.hi[k][2]);
        }
        Log("Weapon watch: draw %u/%u, %u draws; bones moving against bone %u:%s", kWeaponPasses[p].indexCount,
            kWeaponPasses[p].startIndex, w.draws, p == 0 ? kReceiverBone : 0u, moved.empty() ? " none" : text);
        w = WatchPass{};
    }
    if (!g_tracing.load()) {
        std::lock_guard lock(g_traceMutex);
        for (size_t i = 0; i < g_trace.size(); ++i) {
            const TraceRow& r = g_trace[i];
            Log("Trace %3zu: %.4f s, thread %5lu, view %u: eye (%.3f %.3f %.3f) gun (%.3f %.3f %.3f) gap (%.3f %.3f %.3f)", i,
                r.time - g_trace[0].time, r.thread, r.viewUploads, r.eye[0], r.eye[1], r.eye[2], r.gun[0], r.gun[1],
                r.gun[2], r.gun[0] - r.eye[0], r.gun[1] - r.eye[1], r.gun[2] - r.eye[2]);
        }
        g_trace.clear();
    }
    if (g_handsActive.load()) {
        Log("Hands: last second's gun and arm draws: %u moved, %u drawn unmoved", g_handsMovedDraws.exchange(0),
            g_handsUnmoved.exchange(0));
        Log("Hands: placed with the view bound to the draw %u times (it differed from the latest upload %u "
            "times), with the latest %u times",
            g_viewBound.exchange(0), g_viewDiffered.exchange(0), g_viewLatest.exchange(0));
        std::lock_guard lock(g_unmovedMutex);
        static const char* kWhy[] = {"no whole bone buffer copied", "someone else's bones", "BF3's buffer not bound"};
        for (const auto& [key, n] : g_unmovedWhy)
            Log("Hands:   unmoved %u/%u: %u (%s)", std::get<0>(key), std::get<1>(key), n, kWhy[std::get<2>(key)]);
        g_unmovedWhy.clear();
    }
    std::lock_guard projLock(g_projMutex);
    for (const SeenProjection& s : g_seenProjections) {
        const float* m = s.m;
        Log("Weapon watch: matrix with the game's FOV, %u-byte upload at +%u (%s): (%.3f %.3f %.3f %.3f) (%.3f %.3f "
            "%.3f %.3f) (%.3f %.3f %.3f %.3f) (%.3f %.3f %.3f %.3f)",
            s.size, s.offset, s.columns ? "scale down the columns" : "scale along the rows", m[0], m[1], m[2], m[3],
            m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
    }
    g_seenProjections.clear();
}

std::wstring BesideDll(const wchar_t* name) {
    wchar_t path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&BesideDll), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\'))
        slash[1] = 0;
    return std::wstring(path) + name;
}

// Copies a buffer the graphics card holds back to memory and writes it to a file. On the thread
// that presents, which owns the immediate context.
bool SaveBuffer(ID3D11Device* device, ID3D11DeviceContext* imm, ID3D11Buffer* buffer, const wchar_t* name,
                UINT& size) {
    D3D11_BUFFER_DESC desc;
    buffer->GetDesc(&desc);
    size = desc.ByteWidth;
    D3D11_BUFFER_DESC sd{desc.ByteWidth, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
    ID3D11Buffer* staging = nullptr;
    if (FAILED(device->CreateBuffer(&sd, nullptr, &staging)))
        return false;
    imm->CopyResource(staging, buffer);
    D3D11_MAPPED_SUBRESOURCE mapped;
    bool ok = false;
    if (SUCCEEDED(g_origMap[0](imm, staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, BesideDll(name).c_str(), L"wb") == 0 && f) {
            ok = fwrite(mapped.pData, 1, desc.ByteWidth, f) == desc.ByteWidth;
            fclose(f);
        }
        g_origUnmap[0](imm, staging, 0);
    }
    staging->Release();
    return ok;
}

void WriteDump() {
    MeshDump* d = nullptr;
    {
        std::lock_guard lock(g_dumpMutex);
        std::swap(d, g_dump);
    }
    if (!d)
        return;
    ID3D11Device* device = nullptr;
    if (d->ib)
        d->ib->GetDevice(&device);
    ID3D11DeviceContext* imm = nullptr;
    if (device)
        device->GetImmediateContext(&imm);
    FILE* f = nullptr;
    wchar_t prefix[48];  // mesh_<index count>_<start>_
    swprintf_s(prefix, L"mesh_%u_%u_", d->key.indexCount, d->key.startIndex);
    auto name = [&](const wchar_t* what) { return std::wstring(prefix) + what; };
    if (imm && _wfopen_s(&f, BesideDll(name(L"dump.txt").c_str()).c_str(), L"w") == 0 && f) {
        fprintf(f, "draw %u %u %d topology %d\n", d->key.indexCount, d->key.startIndex, d->key.baseVertex,
                static_cast<int>(d->topology));
        UINT size = 0;
        bool ok = SaveBuffer(device, imm, d->ib, name(L"ib.bin").c_str(), size);
        fprintf(f, "ib format %d offset %u size %u %s\n", static_cast<int>(d->ibFormat), d->ibOffset, size,
                ok ? "saved" : "FAILED");
        for (int i = 0; i < 4; ++i) {
            if (!d->vb[i])
                continue;
            wchar_t vb[16];
            swprintf_s(vb, L"vb%d.bin", i);
            ok = SaveBuffer(device, imm, d->vb[i], name(vb).c_str(), size);
            fprintf(f, "vb %d stride %u offset %u size %u %s\n", i, d->stride[i], d->offset[i], size,
                    ok ? "saved" : "FAILED");
        }
        fclose(f);
        if (_wfopen_s(&f, BesideDll(name(L"bones.bin").c_str()).c_str(), L"wb") == 0 && f) {
            fwrite(d->bones.data(), 1, d->bones.size(), f);
            fclose(f);
        }
        Log("Weapon: draw %u/%u's mesh is written to mesh_%u_%u_*.bin", d->key.indexCount, d->key.startIndex,
            d->key.indexCount, d->key.startIndex);
    }
    if (imm)
        imm->Release();
    if (device)
        device->Release();
    for (ID3D11Buffer* b : d->vb)
        if (b)
            b->Release();
    if (d->ib)
        d->ib->Release();
    delete d;
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
    // hunt.txt next to the DLL saying "first person": hunt among the first-person draws instead.
    {
        wchar_t path[MAX_PATH];
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&HuntStart), &self);
        GetModuleFileNameW(self, path, MAX_PATH);
        if (wchar_t* slash = wcsrchr(path, L'\\'))
            wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"hunt.txt");
        char text[64] = {};
        FILE* f = nullptr;
        if (_wfopen_s(&f, path, L"r") == 0 && f) {
            fread(text, 1, sizeof(text) - 1, f);
            fclose(f);
        }
        g_huntFirstPerson = std::strstr(text, "first person") != nullptr;
        g_huntOnly = -1;
        if (const char* s = std::strstr(text, "only"))
            sscanf_s(s + 4, "%d", &g_huntOnly);
    }
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
    const auto& pool = g_huntFirstPerson.load() ? g_firstPerson : g_skinned;
    std::vector<std::pair<DrawKey, UINT>> seen(pool.begin(), pool.end());
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
    // hunt.txt "first person only N": hide just the N-th (from 0, most drawn first), to map each.
    if (g_huntFirstPerson.load() && g_huntOnly >= 0) {
        for (size_t i = 0; i < g_huntRange.size(); ++i)
            Log("Draw hunt: first-person draw %zu: index count %u, start %u, base %d", i, g_huntRange[i].indexCount,
                g_huntRange[i].startIndex, g_huntRange[i].baseVertex);
        size_t n = std::min(static_cast<size_t>(g_huntOnly), g_huntRange.size() - 1);
        PublishHidden(g_huntRange.begin() + n, g_huntRange.begin() + n + 1);
        Log("Draw hunt: hiding only first-person draw %zu", n);
        return;
    }
    PublishHidden(g_huntRange.begin(), g_huntRange.begin() + (g_huntRange.size() + 1) / 2);
    Log("Draw hunt: %zu %s draw kinds; hiding the first %zu", g_huntRange.size(),
        g_huntFirstPerson.load() ? "first-person" : "skinned", (g_huntRange.size() + 1) / 2);
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
        ReadWeaponSelection();
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
        // A size that held a match once was scanned for good: sizes that matched only during a
        // cutscene or a menu went on costing over a millisecond a frame. Those idle 10 s are dropped;
        // the every-two-seconds full scan finds them again if they come back.
        static ULONGLONG lastMatch[kSizeBuckets] = {};
        char sizes[512] = {};
        int used = 0;
        for (int b = 0; b < kSizeBuckets; ++b) {
            uint32_t scanned = g_scannedBySize[b].exchange(0), matched = g_matchedBySize[b].exchange(0);
            if (matched)
                lastMatch[b] = now;
            else if (g_sizeMatched[b].load() && now - lastMatch[b] > 10000)
                g_sizeMatched[b] = false;
            if ((scanned >= 100 || matched) && used < 480)
                used += snprintf(sizes + used, sizeof(sizes) - used, " %u-%uB: %u scanned %u matched;", b * 256,
                                 b * 256 + 255, scanned, matched);
        }
        Log("First-person FOV: uploads by size:%s", sizes);
    }
    if (g_weaponShift.load())
        Log("Weapon: last second's gun draws: %u shifted, %u left alone (another soldier's)",
            g_weaponShifted.exchange(0), g_weaponFar.exchange(0));
    if (g_weaponWatch.load())
        LogWatch();
    WriteDump();
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

bool EjectMagazine() {
    if (g_magOut.load() || !StartDrop())
        return false;
    g_magOut = true;
    Log("Magazine: ejected, falling to the floor at %.2f m", g_floor.load());
    return true;
}

void InsertMagazine() {
    if (g_magOut.exchange(false))
        Log("Magazine: a new one is in the gun");
}

bool MagazineOut() {
    return g_magOut.load();
}

bool RifleInHand() {
    return GetTickCount64() - g_rifleDrawn.load() < 500;
}

void Update() {
    UpdateDrop();
}

bool Watching() {
    return g_weaponWatch.load();
}

bool FirstPersonViews(float (&arms)[16], float (&world)[16]) {
    std::lock_guard lock(g_fpMutex);
    ULONGLONG now = GetTickCount64();
    if (now - g_fpArmsTime > 500 || now - g_fpWorldTime > 500)  // not seen lately
        return false;
    std::memcpy(arms, g_fpArms, sizeof(arms));
    std::memcpy(world, g_fpWorld, sizeof(world));
    return true;
}

bool GunGrip(float* point) {
    std::lock_guard lock(g_handsMutex);
    if (!g_haveGrip)
        return false;
    std::memcpy(point, g_gripModel, sizeof(g_gripModel));
    return true;
}

void SetHands(const HandTargets* targets) {
    std::lock_guard lock(g_handsMutex);
    g_handsOn = targets != nullptr;
    if (targets) {
        g_hands = *targets;
    } else {
        g_haveGunDelta = false;
    }
    g_handsActive = g_handsOn;
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
