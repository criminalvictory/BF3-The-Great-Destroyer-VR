#include "effects.h"

#include <windows.h>
#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "ammo.h"
#include "log.h"
#include "overlay.h"
#include "skin.h"

namespace {

using DrawFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
using DrawInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
using DrawIndirectFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);

DrawFn g_origDraw[2] = {};
DrawInstancedFn g_origDrawInstanced[2] = {};
DrawIndexedInstancedFn g_origDrawIndexedInstanced[2] = {};
DrawIndirectFn g_origDrawIndexedIndirect[2] = {}, g_origDrawIndirect[2] = {};

// ---- Census ----
// Which draws come with firing: every draw for a few seconds, tallied by what it's drawn with (the
// kind of call, the shaders, the stencil reference: BF3 marks first-person draws with 32), first
// standing still and then firing. An effect's draw keeps its shaders while its counts change with
// how many particles there are.
enum DrawType : uint8_t { kIndexed, kPlain, kIndexedInstanced, kInstanced, kIndexedIndirect, kIndirect, kTypes };
const char* const kTypeNames[kTypes] = {"DrawIndexed",          "Draw",
                                        "DrawIndexedInstanced", "DrawInstanced",
                                        "DrawIndexedInstancedIndirect", "DrawInstancedIndirect"};

struct Kind {
    uint8_t type;
    const void* vs;
    const void* ps;
    UINT stencilRef;
    bool operator==(const Kind& o) const {
        return type == o.type && vs == o.vs && ps == o.ps && stencilRef == o.stencilRef;
    }
};
struct KindHash {
    size_t operator()(const Kind& k) const {
        return std::hash<const void*>()(k.vs) ^ (std::hash<const void*>()(k.ps) << 1) ^ (size_t{k.type} << 3) ^
               (size_t{k.stencilRef} << 7);
    }
};
// Draws of a kind in the baseline and after it, the frames it was drawn in (a muzzle flash shows
// in some frames only, a draw or two each: too few on average to stand out), their counts' and
// instances' range, and the state the first one was drawn with.
struct Tally {
    uint32_t before = 0, after = 0;
    uint32_t framesBefore = 0, framesAfter = 0, lastFrame = UINT32_MAX;
    UINT minCount = UINT_MAX, maxCount = 0, minInstances = UINT_MAX, maxInstances = 0;
    bool described = false;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    UINT stride = 0;
    bool blend = false, depthTest = false, depthWrite = false;
    D3D11_BLEND src = D3D11_BLEND_ONE, dst = D3D11_BLEND_ZERO;
    char buffers[160] = "";  // the vertex shader's constant buffers: slot:size, "a" the arms' view, "w" the world's
};

constexpr ULONGLONG kBaseline = 2000, kCensus = 6000;  // ms
// weapon.txt "hidefx": indexed draws with these index counts are skipped, to tell in the headset
// which are the muzzle flash and the casings (0 = unused).
std::atomic<UINT> g_hideFx[4];

// weapon.txt "hideps": draws with these pixel shaders are skipped (pointers as the census logs
// them; they change with every start of the game), to find the muzzle flash's.
std::atomic<uintptr_t> g_hidePs[4];
std::atomic<bool> g_anyHidePs{false};

bool HiddenPs(ID3D11DeviceContext* ctx) {
    if (!g_anyHidePs.load(std::memory_order_relaxed))
        return false;
    ID3D11PixelShader* ps = nullptr;
    ctx->PSGetShader(&ps, nullptr, nullptr);
    if (!ps)
        return false;
    ps->Release();
    for (const auto& h : g_hidePs)
        if (h.load(std::memory_order_relaxed) == reinterpret_cast<uintptr_t>(ps))
            return true;
    return false;
}

bool Hidden(UINT count) {
    for (const auto& h : g_hideFx)
        if (UINT c = h.load(std::memory_order_relaxed); c && c == count)
            return true;
    return false;
}

// weapon.txt "movefx": indexed draws with these index counts are drawn where the gun in the right
// hand is, not where the game holds its own (free hands): the view-projection they're drawn with
// (the per-view constants at +176) times the gun's move. The muzzle flash is drawn in the world
// pass at the game's own muzzle, at the chest.
std::atomic<UINT> g_moveFx[4];

bool Moved(UINT count) {
    for (const auto& m : g_moveFx)
        if (UINT c = m.load(std::memory_order_relaxed); c && c == count)
            return true;
    return false;
}

// False when it can't be moved: the gun isn't being moved, or no view constants are known.
// Row-major 4x4 times 4x4 (column vectors): out = a b.
void Mul4(const float* a, const float* b, float* out) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            out[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c] + a[r * 4 + 3] * b[12 + c];
}

// Which of the view constants a moved draw gets changed (weapon.txt "flashmats", a sum): 1 the view
// (+48), 2 the view-projection (+176), 4 the one without translation that takes points less the eye
// (+240, the eye at +320), 8 the projection (+112) as if the view-space points were moved in the
// world. Changing one a shader doesn't use does nothing; changing two it chains moves it twice.
std::atomic<int> g_moveMatrices{2 | 4};
std::atomic<float> g_lift{0.0f};  // weapon.txt "flashlift": metres up on top of the move, to see it plainly
// What became of the draws there were to move, since the last log.
std::atomic<uint32_t> g_movedDraws{0}, g_noDelta{0}, g_noView{0}, g_noSlot{0}, g_shots{0}, g_flashSeen{0};

// Rigid 4x4 inverse (rotation transposed, translation turned back).
void InvertRigid4(const float* m, float* out) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c)
            out[r * 4 + c] = m[c * 4 + r];
        out[r * 4 + 3] = -(m[r] * m[3] + m[4 + r] * m[7] + m[8 + r] * m[11]);
    }
    out[12] = out[13] = out[14] = 0.0f;
    out[15] = 1.0f;
}

// Drawn with the per-view constants changed so whatever it draws moves with the gun: whichever of
// them g_moveMatrices says. False when it can't be: the gun isn't being moved, no view constants
// are known, or they aren't bound to the vertex shader.
bool DrawMoved(int which, ID3D11DeviceContext* ctx, const std::function<void()>& draw) {
    float d[16];
    if (!skin::GunDeltaWorld(d)) {
        ++g_noDelta;
        return false;
    }
    d[7] += g_lift.load(std::memory_order_relaxed);  // the world's up is y
    ID3D11Resource* resource = nullptr;
    const uint8_t* view = skin::LatestViewConstants(resource);
    if (!view) {
        ++g_noView;
        return false;
    }
    uint8_t copy[352];
    std::memcpy(copy, view, sizeof(copy));
    auto at = [&](size_t offset) { return reinterpret_cast<float*>(copy + offset); };
    const int mats = g_moveMatrices.load(std::memory_order_relaxed);
    float moved[16];
    if (mats & 8) {
        // View-space points moved in the world: P (V D V^-1), the view as it came.
        float inverse[16], vd[16], vdv[16];
        InvertRigid4(at(48), inverse);
        Mul4(at(48), d, vd);
        Mul4(vd, inverse, vdv);
        Mul4(at(112), vdv, moved);
        std::memcpy(at(112), moved, sizeof(moved));
    }
    if (mats & 4) {
        // p - eye moved: R (p - eye) + (R eye + t - eye).
        const float* eye = at(320);
        float relative[16];
        std::memcpy(relative, d, sizeof(relative));
        for (int r = 0; r < 3; ++r)
            relative[r * 4 + 3] = d[r * 4] * eye[0] + d[r * 4 + 1] * eye[1] + d[r * 4 + 2] * eye[2] + d[r * 4 + 3] - eye[r];
        Mul4(at(240), relative, moved);
        std::memcpy(at(240), moved, sizeof(moved));
    }
    for (int bit : {1, 2}) {
        if (!(mats & bit))
            continue;
        const size_t offset = bit == 1 ? 48 : 176;
        Mul4(at(offset), d, moved);
        std::memcpy(at(offset), moved, sizeof(moved));
    }
    if (!skin::DrawWithCopy(which, ctx, resource, copy, sizeof(copy), draw)) {
        ++g_noSlot;
        return false;
    }
    ++g_movedDraws;
    return true;
}

// ---- The muzzle flash ----
// As the census found it firing at the sky (2026-10-30): five layers of sprites (Draw, 6 vertices of
// 24 bytes, blended one / one minus source alpha, stencil 0) and a small mesh (36 indices of 32-byte
// vertices, blended, stencil 2), drawn in two thirds of the frames while firing and next to none
// otherwise. The game draws it at its own muzzle, at the chest; with free hands it's moved with the
// gun, for draws like these in the moments after a shot.
std::atomic<ULONGLONG> g_lastShot{0};
int g_lastLoaded = -1;

bool FlashLike(ID3D11DeviceContext* ctx, UINT stride, UINT stencil) {
    if (GetTickCount64() - g_lastShot.load(std::memory_order_relaxed) > 300)
        return false;
    ID3D11Buffer* vb = nullptr;
    UINT vbStride = 0, offset = 0;
    ctx->IAGetVertexBuffers(0, 1, &vb, &vbStride, &offset);
    if (vb)
        vb->Release();
    if (vbStride != stride)
        return false;
    ID3D11DepthStencilState* depth = nullptr;
    UINT ref = 0;
    ctx->OMGetDepthStencilState(&depth, &ref);
    if (depth)
        depth->Release();
    if (ref != stencil)
        return false;
    ID3D11BlendState* blend = nullptr;
    FLOAT factor[4];
    UINT mask = 0;
    ctx->OMGetBlendState(&blend, factor, &mask);
    bool premultiplied = false;
    if (blend) {
        D3D11_BLEND_DESC b;
        blend->GetDesc(&b);
        premultiplied = b.RenderTarget[0].BlendEnable && b.RenderTarget[0].SrcBlend == D3D11_BLEND_ONE &&
                        b.RenderTarget[0].DestBlend == D3D11_BLEND_INV_SRC_ALPHA;
        blend->Release();
    }
    return premultiplied;
}

bool FlashSprite(ID3D11DeviceContext* ctx, UINT vertices) {
    if (vertices != 6 || !FlashLike(ctx, 24, 0))
        return false;
    ++g_flashSeen;
    return true;
}

bool FlashMesh(ID3D11DeviceContext* ctx, UINT indices) {
    return indices == 36 && FlashLike(ctx, 32, 2);
}

// ---- The casings ----
// A small mesh (234 indices; the census, 2026-10-30) drawn once per casing in each pass (colour,
// depth, shadows), flying from where the game's gun throws them, at the chest. With free hands,
// while firing they're drawn moved with the gun, so they come out of its ejection port; half a
// second after the last shot they're hidden: moved with the hand they'd land above or below the
// ground, and the game's own lie at the chest.
constexpr UINT kCasingIndices = 234;
// Off until the casings really move with the gun (2026-10-30: changing the view constants moved
// neither them nor the flash): hidden after half a second, the game's own would only vanish early.
constexpr bool kMoveCasings = false;

int CasingDraw(UINT count) {
    if (!kMoveCasings || count != kCasingIndices)
        return effects::kNormal;
    float d[16];
    if (!skin::GunDeltaWorld(d))
        return effects::kNormal;  // the game's own gun: its casings are where they belong
    return GetTickCount64() - g_lastShot.load(std::memory_order_relaxed) < 600 ? effects::kMove : effects::kHide;
}

// weapon.txt "dumpfx 234": the next three indexed instanced draws with that index count log the
// start of the 4096-byte block they're drawn with (whether each casing's own place is in it).
std::atomic<UINT> g_dumpFx{0};
std::atomic<int> g_dumpFxLeft{0};

float Length3(const float* v);
float Distance3(const float* a, const float* b);

// General 4x4 inverse (row-major); false when singular.
bool Invert4(const float* m, float* out) {
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (!(std::fabs(det) > 1e-12f))
        return false;
    for (int i = 0; i < 16; ++i)
        out[i] = inv[i] / det;
    return true;
}

// weapon.txt "dumpfx N" (N = 6: the flash's sprites, 234: the casings): the next three draws with
// that count log their 4096-byte block, and any 4x4 in it that is the view-projection times a
// rigid placement (each row of the view-projection's inverse times it, rows or columns: a world-
// view-projection), with where that placement is against the game's muzzle.
void DumpFx(UINT count) {
    if (count != g_dumpFx.load(std::memory_order_relaxed) || g_dumpFxLeft.fetch_sub(1) <= 0)
        return;
    ID3D11Resource* resource = nullptr;
    std::vector<uint8_t> block;
    ID3D11Resource* viewBuffer = nullptr;
    const uint8_t* view = skin::LatestViewConstants(viewBuffer);
    float body[12], world[3], arms[3];
    const bool gun = skin::GameGunBody(body) && skin::GameGunPlaces(world, arms);
    float muzzle[3] = {};
    for (int r = 0; r < 3 && gun; ++r)
        muzzle[r] = body[r * 4 + 1] * 0.04f + body[r * 4 + 2] * 0.78f + body[r * 4 + 3];
    if (!skin::LatestBlock(resource, block) || block.size() < 128 || !view) {
        Log("Effects: dump of a %u-count draw: no block or view", count);
        return;
    }
    const auto* f = reinterpret_cast<const float*>(block.data());
    Log("Effects: dump of a %u-count draw (the game's muzzle at %.3f %.3f %.3f in the world):", count, muzzle[0],
        muzzle[1], muzzle[2]);
    for (int r = 0; r < 8; ++r)
        Log("Effects:   +%3d: %12.4f %12.4f %12.4f %12.4f", r * 16, f[r * 4], f[r * 4 + 1], f[r * 4 + 2], f[r * 4 + 3]);
    float vpInverse[16];
    if (!Invert4(reinterpret_cast<const float*>(view + 176), vpInverse))
        return;
    int found = 0;
    for (size_t k = 0; k + 16 <= block.size() / 4 && found < 6; k += 4) {
        for (int transposed = 0; transposed < 2; ++transposed) {
            float m[16], w[16];
            for (int i = 0; i < 16; ++i)
                m[i] = transposed ? f[k + (i % 4) * 4 + i / 4] : f[k + i];
            Mul4(vpInverse, m, w);
            // Rigid: orthonormal rows, nothing in the last row but (0 0 0 1).
            bool rigid = std::fabs(w[12]) < 0.02f && std::fabs(w[13]) < 0.02f && std::fabs(w[14]) < 0.02f &&
                         std::fabs(w[15] - 1.0f) < 0.02f;
            for (int r = 0; r < 3 && rigid; ++r)
                rigid = std::fabs(Length3(w + r * 4) - 1.0f) < 0.05f;
            if (!rigid)
                continue;
            const float at[3] = {w[3], w[7], w[11]};
            ++found;
            Log("Effects:   +%zu%s is the view-projection times a placement at %.3f %.3f %.3f, %.2f m from the muzzle",
                k * 4, transposed ? " (columns)" : "", at[0], at[1], at[2], Distance3(at, muzzle));
        }
    }
    if (!found)
        Log("Effects:   no world-view-projection in the block");
}

std::atomic<bool> g_census{false};
std::mutex g_censusMutex;
std::unordered_map<Kind, Tally, KindHash> g_tally;
ULONGLONG g_censusStart = 0;
uint32_t g_framesBefore = 0, g_framesAfter = 0;
std::atomic<uint32_t> g_censusFrame{0};

void Describe(ID3D11DeviceContext* ctx, ID3D11DepthStencilState* depth, Tally& t) {
    t.described = true;
    ctx->IAGetPrimitiveTopology(&t.topology);
    ID3D11Buffer* vb = nullptr;
    UINT offset = 0;
    ctx->IAGetVertexBuffers(0, 1, &vb, &t.stride, &offset);
    if (vb)
        vb->Release();
    ID3D11BlendState* blend = nullptr;
    FLOAT factor[4];
    UINT mask = 0;
    ctx->OMGetBlendState(&blend, factor, &mask);
    if (blend) {
        D3D11_BLEND_DESC d;
        blend->GetDesc(&d);
        t.blend = d.RenderTarget[0].BlendEnable != FALSE;
        t.src = d.RenderTarget[0].SrcBlend;
        t.dst = d.RenderTarget[0].DestBlend;
        blend->Release();
    }
    if (depth) {
        D3D11_DEPTH_STENCIL_DESC d;
        depth->GetDesc(&d);
        t.depthTest = d.DepthEnable != FALSE;
        t.depthWrite = d.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL;
    }
    ID3D11Buffer* cb[8] = {};
    ctx->VSGetConstantBuffers(0, 8, cb);
    int used = 0;
    for (int i = 0; i < 8; ++i) {
        if (!cb[i])
            continue;
        D3D11_BUFFER_DESC d;
        cb[i]->GetDesc(&d);
        int view = skin::ViewBufferKind(cb[i]);
        used += snprintf(t.buffers + used, sizeof(t.buffers) - used, "%d:%u%s ", i, d.ByteWidth,
                         view == 1 ? "a" : view == 2 ? "w" : "");
        cb[i]->Release();
        if (used >= static_cast<int>(sizeof(t.buffers)) - 1)
            break;
    }
}

// ---- Frame tail ----
// weapon.txt "tail": one frame's draws to targets the window's size, in order, with their shaders,
// blending and viewport. The HUD (and with it the screen's blood and suppression) is drawn into the
// right eye's half only (2026-10-30); this tells its draws from the rest.
std::atomic<int> g_tailState{0};  // 0 off, 1 from the next frame, 2 recording this one
struct TailDraw {
    DrawType type;
    UINT count, instances;
    const void *ctx, *vs, *ps, *target;
    UINT width, height, format, stencil;
    D3D11_VIEWPORT viewport;
    bool blend;
    D3D11_BLEND src, dst;
};
std::mutex g_tailMutex;
std::vector<TailDraw> g_tail;
UINT g_tailAll = 0;  // every draw of the frame

// An eye's picture going onto the window (unblended, a target twice as wide as high): its pixel
// shader's constants (the first four buffers, 64 floats each at most), read back through a staging
// copy, logged with the tail. The suppression blur stayed in the right eye (2026-10-30): if it is
// a constant of this pass, the two eyes' values differ while suppressed.
void LogPassConstants(ID3D11DeviceContext* ctx, size_t index) {
    ID3D11Buffer* buffers[4] = {};
    ctx->PSGetConstantBuffers(0, 4, buffers);
    for (int slot = 0; slot < 4; ++slot) {
        ID3D11Buffer* buffer = buffers[slot];
        if (!buffer)
            continue;
        D3D11_BUFFER_DESC desc;
        buffer->GetDesc(&desc);
        ID3D11Device* device = nullptr;
        buffer->GetDevice(&device);
        D3D11_BUFFER_DESC staging = desc;
        staging.Usage = D3D11_USAGE_STAGING;
        staging.BindFlags = 0;
        staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging.MiscFlags = 0;
        ID3D11Buffer* copy = nullptr;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (device && SUCCEEDED(device->CreateBuffer(&staging, nullptr, &copy))) {
            ctx->CopyResource(copy, buffer);
            if (SUCCEEDED(ctx->Map(copy, 0, D3D11_MAP_READ, 0, &mapped))) {
                const auto* f = static_cast<const float*>(mapped.pData);
                const UINT count = std::min<UINT>(desc.ByteWidth / 4, 64);
                char line[1400];
                int used = 0;
                for (UINT i = 0; i < count && used < 1360; ++i)
                    used += snprintf(line + used, sizeof(line) - used, " %g", f[i]);
                Log("Tail %zu: PS constants %d (%u bytes):%s", index, slot, desc.ByteWidth, line);
                ctx->Unmap(copy, 0);
            }
            copy->Release();
        }
        if (device)
            device->Release();
        buffer->Release();
    }
}

void Tail(ID3D11DeviceContext* ctx, DrawType type, UINT count, UINT instances) {
    TailDraw d{type, count, instances, ctx};
    ID3D11RenderTargetView* rtv = nullptr;
    ctx->OMGetRenderTargets(1, &rtv, nullptr);
    if (rtv) {
        ID3D11Resource* resource = nullptr;
        rtv->GetResource(&resource);
        if (resource) {
            d.target = resource;
            ID3D11Texture2D* texture = nullptr;
            if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)))) {
                D3D11_TEXTURE2D_DESC desc;
                texture->GetDesc(&desc);
                d.width = desc.Width, d.height = desc.Height, d.format = desc.Format;
                texture->Release();
            }
            resource->Release();
        }
        rtv->Release();
    }
    {
        std::lock_guard lock(g_tailMutex);
        ++g_tailAll;
    }
    if (d.width < 1000 || g_tail.size() >= 20000)
        return;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    ctx->VSGetShader(&vs, nullptr, nullptr);
    ctx->PSGetShader(&ps, nullptr, nullptr);
    ctx->OMGetDepthStencilState(&depth, &d.stencil);
    d.vs = vs, d.ps = ps;
    if (vs)
        vs->Release();
    if (ps)
        ps->Release();
    if (depth)
        depth->Release();
    UINT viewports = 1;
    ctx->RSGetViewports(&viewports, &d.viewport);
    if (!viewports)
        d.viewport = {};
    ID3D11BlendState* blend = nullptr;
    FLOAT factor[4];
    UINT mask = 0;
    ctx->OMGetBlendState(&blend, factor, &mask);
    d.src = D3D11_BLEND_ONE, d.dst = D3D11_BLEND_ZERO;
    if (blend) {
        D3D11_BLEND_DESC desc;
        blend->GetDesc(&desc);
        d.blend = desc.RenderTarget[0].BlendEnable != FALSE;
        d.src = desc.RenderTarget[0].SrcBlend, d.dst = desc.RenderTarget[0].DestBlend;
        blend->Release();
    }
    size_t index;
    {
        std::lock_guard lock(g_tailMutex);
        index = g_tail.size();
        g_tail.push_back(d);
    }
    if (!d.blend && d.width == 2 * d.height)
        LogPassConstants(ctx, index);
}

const char* BlendName(D3D11_BLEND b);

void LogTail() {
    std::lock_guard lock(g_tailMutex);
    Log("Tail: %u draws in the frame, %zu of them to targets 1000 or more wide; in runs to one target:", g_tailAll,
        g_tail.size());
    // Runs: the same target, viewport and blending in a row, with their first pixel shaders.
    for (size_t i = 0; i < g_tail.size();) {
        const TailDraw& a = g_tail[i];
        size_t j = i + 1;
        while (j < g_tail.size() && g_tail[j].target == a.target && g_tail[j].blend == a.blend &&
               g_tail[j].viewport.TopLeftX == a.viewport.TopLeftX && g_tail[j].viewport.Width == a.viewport.Width)
            ++j;
        char shaders[200] = "";
        int used = 0;
        for (size_t k = i; k < j && used < 180; ++k) {
            bool seen = false;
            for (size_t m = i; m < k && !seen; ++m)
                seen = g_tail[m].ps == g_tail[k].ps;
            if (!seen)
                used += snprintf(shaders + used, sizeof(shaders) - used, "%p ", g_tail[k].ps);
        }
        Log("Tail run %zu-%zu (%zu): target %p %ux%u format %u, viewport %.0f,%.0f %.0fx%.0f, blend %s; PS %s", i, j - 1,
            j - i, a.target, a.width, a.height, a.format, a.viewport.TopLeftX, a.viewport.TopLeftY, a.viewport.Width,
            a.viewport.Height, a.blend ? "on" : "off", shaders);
        i = j;
    }
    // The last of them one by one.
    for (size_t i = g_tail.size() > 150 ? g_tail.size() - 150 : 0; i < g_tail.size(); ++i) {
        const TailDraw& d = g_tail[i];
        Log("Tail %zu: %s %u x%u, context %p, VS %p PS %p, target %p %ux%u format %u, viewport %.0f,%.0f %.0fx%.0f, "
            "blend %s (%s, %s), stencil %u",
            i, kTypeNames[d.type], d.count, d.instances, d.ctx, d.vs, d.ps, d.target, d.width, d.height, d.format,
            d.viewport.TopLeftX, d.viewport.TopLeftY, d.viewport.Width, d.viewport.Height, d.blend ? "on" : "off",
            BlendName(d.src), BlendName(d.dst), d.stencil);
    }
    g_tail.clear();
}

void ProbeDraw(ID3D11DeviceContext* ctx, DrawType type, UINT count, UINT instances);
thread_local UINT t_start = 0;  // the indexed draw being noted: its first index (the probe logs it)

void NoteDraw(ID3D11DeviceContext* ctx, DrawType type, UINT count, UINT instances) {
    if (g_tailState.load(std::memory_order_relaxed) == 2)
        Tail(ctx, type, count, instances);
    ProbeDraw(ctx, type, count, instances);
    if (!g_census.load(std::memory_order_relaxed))
        return;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    UINT ref = 0;
    ctx->VSGetShader(&vs, nullptr, nullptr);
    ctx->PSGetShader(&ps, nullptr, nullptr);
    ctx->OMGetDepthStencilState(&depth, &ref);
    {
        std::lock_guard lock(g_censusMutex);
        Tally& t = g_tally[Kind{type, vs, ps, ref}];
        const bool before = GetTickCount64() - g_censusStart < kBaseline;
        ++(before ? t.before : t.after);
        if (uint32_t frame = g_censusFrame.load(std::memory_order_relaxed); frame != t.lastFrame) {
            t.lastFrame = frame;
            ++(before ? t.framesBefore : t.framesAfter);
        }
        t.minCount = std::min(t.minCount, count);
        t.maxCount = std::max(t.maxCount, count);
        t.minInstances = std::min(t.minInstances, instances);
        t.maxInstances = std::max(t.maxInstances, instances);
        if (!t.described)
            Describe(ctx, depth, t);
    }
    if (vs)
        vs->Release();
    if (ps)
        ps->Release();
    if (depth)
        depth->Release();
}

const char* BlendName(D3D11_BLEND b) {
    switch (b) {
    case D3D11_BLEND_ZERO: return "0";
    case D3D11_BLEND_ONE: return "1";
    case D3D11_BLEND_SRC_ALPHA: return "srcA";
    case D3D11_BLEND_INV_SRC_ALPHA: return "1-srcA";
    case D3D11_BLEND_DEST_ALPHA: return "dstA";
    case D3D11_BLEND_INV_DEST_ALPHA: return "1-dstA";
    case D3D11_BLEND_SRC_COLOR: return "srcC";
    case D3D11_BLEND_INV_SRC_COLOR: return "1-srcC";
    case D3D11_BLEND_DEST_COLOR: return "dstC";
    default: return "other";
    }
}

// The kinds that came with firing: new after the baseline, or in clearly more of the frames.
void Report() {
    struct Row {
        Kind kind;
        const Tally* tally;
        double before, after;  // the share of frames drawn in
    };
    std::vector<Row> rows;
    const double framesBefore = std::max<uint32_t>(g_framesBefore, 1), framesAfter = std::max<uint32_t>(g_framesAfter, 1);
    for (const auto& [kind, t] : g_tally) {
        double before = t.framesBefore / framesBefore, after = t.framesAfter / framesAfter;
        if ((t.before == 0 && t.after > 0) || (after >= 0.05 && after >= 2.0 * before + 0.03))
            rows.push_back({kind, &t, before, after});
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b) { return a.after - a.before > b.after - b.before; });
    Log("Census: %zu kinds of draw, %u frames of baseline and %u after; %zu came with what happened after:",
        g_tally.size(), g_framesBefore, g_framesAfter, rows.size());
    for (size_t i = 0; i < rows.size() && i < 40; ++i) {
        const Row& r = rows[i];
        const Tally& t = *r.tally;
        Log("Census: %s VS %p PS %p, stencil %u%s: in %.0f%% of frames before, %.0f%% after (%.2f draws a frame "
            "after); counts %u-%u, instances %u-%u; topology %d, stride %u, blend %s (%s, %s), depth %s%s; VS "
            "buffers %s",
            kTypeNames[r.kind.type], r.kind.vs, r.kind.ps, r.kind.stencilRef, r.kind.stencilRef & 32 ? " (first person)" : "",
            100.0 * r.before, 100.0 * r.after, t.after / framesAfter, t.minCount, t.maxCount, t.minInstances,
            t.maxInstances, static_cast<int>(t.topology), t.stride, t.blend ? "on" : "off", BlendName(t.src),
            BlendName(t.dst), t.depthTest ? "tested" : "off", t.depthWrite ? ", written" : "", t.buffers);
    }
}

// ---- Gun probe ----
// F19: for 10 s (fire meanwhile), every draw is checked for something placed at the game's own
// gun: a placement in its vertex shader's constant buffers (three unit-length rows with the
// translation in their fourth components, the rows in any order: BF3 keeps some as y, z, x) or
// vertices in its vertex buffer (the first three floats of each), within 1 m of the gun body, in
// the world or in the arms' space. Each kind of draw found so is logged once, with where. The
// muzzle flash and the casings start where the game's gun is: this finds their draws and where
// their placements are. Uploads are seen only with weapon.txt "watch".
std::atomic<ULONGLONG> g_probeUntil{0};
float g_gunWorld[3] = {}, g_gunArms[3] = {};
std::atomic<bool> g_haveGun{false};
std::mutex g_probeMutex;
std::vector<Kind> g_probeLogged;

bool Blended(ID3D11DeviceContext* ctx) {
    ID3D11BlendState* blend = nullptr;
    FLOAT factor[4];
    UINT mask = 0;
    ctx->OMGetBlendState(&blend, factor, &mask);
    bool on = false;
    if (blend) {
        D3D11_BLEND_DESC d;
        blend->GetDesc(&d);
        on = d.RenderTarget[0].BlendEnable != FALSE;
        blend->Release();
    }
    return on;
}

float Length3(const float* v) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

float Distance3(const float* a, const float* b) {
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return Length3(d);
}

// The nearest anything in some data comes to the gun: where, in which space, and for placements
// which order of rows (0: x y z; 1: y z x; 2: z x y), for vertices how many lie within 1 m.
struct Hit {
    float distance = 1e9f;
    size_t offset = 0;
    bool arms = false;
    int order = -1;  // -1: vertices
    int within = 0;
};

void ScanPlacements(const uint8_t* data, UINT size, Hit& hit) {
    const auto* f = reinterpret_cast<const float*>(data);
    const size_t floats = size / 4;
    for (size_t k = 0; k + 12 <= floats; k += 4) {
        const float* m = f + k;
        if (std::fabs(Length3(m) - 1) > 0.05f || std::fabs(Length3(m + 4) - 1) > 0.05f ||
            std::fabs(Length3(m + 8) - 1) > 0.05f)
            continue;
        const float w[3] = {m[3], m[7], m[11]};
        for (int order = 0; order < 3; ++order) {
            const float t[3] = {w[(3 - order) % 3], w[(4 - order) % 3], w[(5 - order) % 3]};
            for (int space = 0; space < 2; ++space) {
                const float d = Distance3(t, space ? g_gunArms : g_gunWorld);
                if (d < hit.distance)
                    hit = Hit{d, k * 4, space == 1, order, 0};
            }
        }
    }
}

void ScanVertices(const uint8_t* data, UINT size, UINT stride, Hit& hit) {
    if (stride < 12)
        return;
    Hit best;
    int within = 0;
    for (UINT o = 0; o + 12 <= size; o += stride) {
        const float* p = reinterpret_cast<const float*>(data + o);
        for (int space = 0; space < 2; ++space) {
            const float d = Distance3(p, space ? g_gunArms : g_gunWorld);
            within += d < 1.0f;
            if (d < best.distance)
                best = Hit{d, o, space == 1, -1, 0};
        }
    }
    best.within = within;
    if (best.distance < hit.distance)
        hit = best;
}

// Uploads made on this thread up to its last draw: older data a draw has bound was some earlier
// draw's (one buffer is refilled for draw after draw), so only newer data counts as its own.
thread_local uint32_t t_uploadsAtLastDraw = 0;

void ProbeDraw(ID3D11DeviceContext* ctx, DrawType type, UINT count, UINT instances) {
    const uint32_t before = t_uploadsAtLastDraw;
    t_uploadsAtLastDraw = skin::UploadSerial();
    if (GetTickCount64() > g_probeUntil.load(std::memory_order_relaxed) || !g_haveGun.load(std::memory_order_relaxed))
        return;
    ID3D11DepthStencilState* depth = nullptr;
    UINT ref = 0;
    ctx->OMGetDepthStencilState(&depth, &ref);
    if (depth)
        depth->Release();
    if ((ref & 32) || !Blended(ctx))
        return;  // effects only: blended, and not the first-person gun and arms
    // Every placement within 1.2 m of the game's muzzle in the data bound: where, which order of
    // rows, in which space, how far, and whether it was uploaded for this draw ("new").
    static const char* const kOrders[] = {"xyz", "yzx", "zxy"};
    char found[512] = "";
    int used = 0, matches = 0;
    ID3D11Buffer* cb[4] = {};
    ctx->VSGetConstantBuffers(0, 4, cb);
    for (int i = 0; i < 4; ++i) {
        if (!cb[i])
            continue;
        UINT size = 0;
        uint32_t serial = 0;
        const uint8_t* data = skin::LatestBlockOf(cb[i], size, serial);
        if (!data)
            data = skin::RecentUploadOf(cb[i], size, serial);
        cb[i]->Release();
        if (!data)
            continue;
        const auto* f = reinterpret_cast<const float*>(data);
        // Plain points (a particle's centre): any register's x, y, z within 25 cm of the muzzle.
        for (size_t k = 0; k + 3 <= size / 4 && used < 440; k += 4) {
            for (int space = 0; space < 2; ++space) {
                const float d = Distance3(f + k, space ? g_gunArms : g_gunWorld);
                if (d < 0.25f && used < 440) {
                    used += snprintf(found + used, sizeof(found) - used, " cb%d+%zu point %s %.2fm%s;", i, k * 4,
                                     space ? "arms" : "world", d, serial > before ? " new" : "");
                    ++matches;
                }
            }
        }
        for (size_t k = 0; k + 12 <= size / 4 && used < 440; k += 4) {
            const float* m = f + k;
            if (std::fabs(Length3(m) - 1) > 0.05f || std::fabs(Length3(m + 4) - 1) > 0.05f ||
                std::fabs(Length3(m + 8) - 1) > 0.05f)
                continue;
            const float w[3] = {m[3], m[7], m[11]};
            for (int order = 0; order < 3; ++order) {
                const float t[3] = {w[(3 - order) % 3], w[(4 - order) % 3], w[(5 - order) % 3]};
                for (int space = 0; space < 2; ++space) {
                    const float d = Distance3(t, space ? g_gunArms : g_gunWorld);
                    if (d < 0.3f && used < 440) {
                        used += snprintf(found + used, sizeof(found) - used, " cb%d+%zu %s %s %.2fm%s;", i, k * 4,
                                         kOrders[order], space ? "arms" : "world", d, serial > before ? " new" : "");
                        ++matches;
                    }
                }
            }
        }
    }
    {
        // Vertices uploaded for it (batched particles): how many lie within 25 cm of the muzzle.
        ID3D11Buffer* vb = nullptr;
        UINT vbStride = 0, vbOffset = 0;
        ctx->IAGetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset);
        if (vb) {
            UINT size = 0;
            uint32_t serial = 0;
            const uint8_t* data = skin::RecentUploadOf(vb, size, serial);
            vb->Release();
            int close[2] = {};
            for (UINT o = 0; data && vbStride >= 12 && o + 12 <= size; o += vbStride)
                for (int space = 0; space < 2; ++space)
                    close[space] += Distance3(reinterpret_cast<const float*>(data + o), space ? g_gunArms : g_gunWorld) < 0.25f;
            if ((close[0] || close[1]) && used < 440) {
                used += snprintf(found + used, sizeof(found) - used, " vertices near the muzzle: %d world, %d arms%s;",
                                 close[0], close[1], serial > before ? " new" : "");
                ++matches;
            }
        }
    }
    if (!matches)
        return;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ctx->VSGetShader(&vs, nullptr, nullptr);
    ctx->PSGetShader(&ps, nullptr, nullptr);
    const Kind kind{static_cast<uint8_t>(type), vs, ps, ref};
    if (vs)
        vs->Release();
    if (ps)
        ps->Release();
    ID3D11Buffer* vb = nullptr;
    UINT stride = 0, offset = 0;
    ctx->IAGetVertexBuffers(0, 1, &vb, &stride, &offset);
    if (vb)
        vb->Release();
    std::lock_guard lock(g_probeMutex);
    if (g_probeLogged.size() >= 40 ||
        std::find(g_probeLogged.begin(), g_probeLogged.end(), kind) != g_probeLogged.end())
        return;
    g_probeLogged.push_back(kind);
    Log("Muzzle probe: %s count %u (from index %u), %u instances, stride %u, stencil %u, VS %p PS %p:%s",
        kTypeNames[type], count, type == kIndexed || type == kIndexedInstanced ? t_start : 0u, instances, stride, ref,
        kind.vs, kind.ps, found);
}

void STDMETHODCALLTYPE Draw0(ID3D11DeviceContext* c, UINT count, UINT start) {
    NoteDraw(c, kPlain, count, 1);
    if (HiddenPs(c))
        return;
    if (overlay::Draw(c, count, [&] { g_origDraw[0](c, count, start); }))
        return;
    if (FlashSprite(c, count)) {
        DumpFx(count);
        if (DrawMoved(0, c, [&] { g_origDraw[0](c, count, start); }))
            return;
    }
    g_origDraw[0](c, count, start);
}
void STDMETHODCALLTYPE Draw1(ID3D11DeviceContext* c, UINT count, UINT start) {
    NoteDraw(c, kPlain, count, 1);
    if (HiddenPs(c))
        return;
    if (overlay::Draw(c, count, [&] { g_origDraw[1](c, count, start); }))
        return;
    if (FlashSprite(c, count)) {
        DumpFx(count);
        if (DrawMoved(1, c, [&] { g_origDraw[1](c, count, start); }))
            return;
    }
    g_origDraw[1](c, count, start);
}
void STDMETHODCALLTYPE DrawInstanced0(ID3D11DeviceContext* c, UINT count, UINT instances, UINT start, UINT first) {
    NoteDraw(c, kInstanced, count, instances);
    if (HiddenPs(c))
        return;
    g_origDrawInstanced[0](c, count, instances, start, first);
}
void STDMETHODCALLTYPE DrawInstanced1(ID3D11DeviceContext* c, UINT count, UINT instances, UINT start, UINT first) {
    NoteDraw(c, kInstanced, count, instances);
    if (HiddenPs(c))
        return;
    g_origDrawInstanced[1](c, count, instances, start, first);
}
void STDMETHODCALLTYPE DrawIndexedInstanced0(ID3D11DeviceContext* c, UINT count, UINT instances, UINT start, INT base,
                                             UINT first) {
    t_start = start;
    NoteDraw(c, kIndexedInstanced, count, instances);
    DumpFx(count);
    if (Hidden(count) || HiddenPs(c))
        return;
    const int casing = CasingDraw(count);
    if (casing == effects::kHide)
        return;
    if ((casing == effects::kMove || Moved(count) || FlashMesh(c, count)) && DrawMoved(0, c, [&] { g_origDrawIndexedInstanced[0](c, count, instances, start, base, first); }))
        return;
    g_origDrawIndexedInstanced[0](c, count, instances, start, base, first);
}
void STDMETHODCALLTYPE DrawIndexedInstanced1(ID3D11DeviceContext* c, UINT count, UINT instances, UINT start, INT base,
                                             UINT first) {
    t_start = start;
    NoteDraw(c, kIndexedInstanced, count, instances);
    DumpFx(count);
    if (Hidden(count) || HiddenPs(c))
        return;
    const int casing = CasingDraw(count);
    if (casing == effects::kHide)
        return;
    if ((casing == effects::kMove || Moved(count) || FlashMesh(c, count)) && DrawMoved(1, c, [&] { g_origDrawIndexedInstanced[1](c, count, instances, start, base, first); }))
        return;
    g_origDrawIndexedInstanced[1](c, count, instances, start, base, first);
}
void STDMETHODCALLTYPE DrawIndexedIndirect0(ID3D11DeviceContext* c, ID3D11Buffer* args, UINT offset) {
    NoteDraw(c, kIndexedIndirect, 0, 0);
    g_origDrawIndexedIndirect[0](c, args, offset);
}
void STDMETHODCALLTYPE DrawIndexedIndirect1(ID3D11DeviceContext* c, ID3D11Buffer* args, UINT offset) {
    NoteDraw(c, kIndexedIndirect, 0, 0);
    g_origDrawIndexedIndirect[1](c, args, offset);
}
void STDMETHODCALLTYPE DrawIndirect0(ID3D11DeviceContext* c, ID3D11Buffer* args, UINT offset) {
    NoteDraw(c, kIndirect, 0, 0);
    g_origDrawIndirect[0](c, args, offset);
}
void STDMETHODCALLTYPE DrawIndirect1(ID3D11DeviceContext* c, ID3D11Buffer* args, UINT offset) {
    NoteDraw(c, kIndirect, 0, 0);
    g_origDrawIndirect[1](c, args, offset);
}

// Hooks one function of both kinds of context (one hook when they share it).
template <typename Fn>
bool HookPair(void* const (&targets)[2], Fn hook0, Fn hook1, Fn (&original)[2]) {
    if (MH_CreateHook(targets[0], reinterpret_cast<void*>(hook0), reinterpret_cast<void**>(&original[0])) != MH_OK)
        return false;
    if (targets[1] == targets[0]) {
        original[1] = original[0];
        return true;
    }
    return MH_CreateHook(targets[1], reinterpret_cast<void*>(hook1), reinterpret_cast<void**>(&original[1])) == MH_OK;
}

}

namespace effects {

bool InstallHooks(const DrawFunctions& f) {
    bool ok = HookPair(f.draw, &Draw0, &Draw1, g_origDraw) &&
              HookPair(f.drawInstanced, &DrawInstanced0, &DrawInstanced1, g_origDrawInstanced) &&
              HookPair(f.drawIndexedInstanced, &DrawIndexedInstanced0, &DrawIndexedInstanced1,
                       g_origDrawIndexedInstanced) &&
              HookPair(f.drawIndexedIndirect, &DrawIndexedIndirect0, &DrawIndexedIndirect1, g_origDrawIndexedIndirect) &&
              HookPair(f.drawIndirect, &DrawIndirect0, &DrawIndirect1, g_origDrawIndirect);
    if (!ok)
        Log("Effects: hooking the other draw calls failed");
    return ok;
}

bool DrawWithGun(int which, ID3D11DeviceContext* ctx, const std::function<void()>& draw) {
    return DrawMoved(which, ctx, draw);
}

int NoteDrawIndexed(ID3D11DeviceContext* ctx, UINT count, UINT start, INT) {
    t_start = start;
    NoteDraw(ctx, kIndexed, count, 1);
    if (Hidden(count) || HiddenPs(ctx))
        return kHide;
    if (const int casing = CasingDraw(count); casing != kNormal)
        return casing;
    return FlashMesh(ctx, count) ? kMove : kNormal;
}

void SetLift(float metres) {
    if (g_lift.exchange(metres) != metres)
        Log("Effects: what's moved with the gun is lifted %.2f m more", metres);
}

ULONGLONG MsSinceShot() {
    return GetTickCount64() - g_lastShot.load(std::memory_order_relaxed);
}

ULONGLONG LastShot() {
    return g_lastShot.load(std::memory_order_relaxed);
}

void SetMoveMatrices(int which) {
    if (which > 0 && g_moveMatrices.exchange(which) != which)
        Log("Effects: moved draws change view constants %d (1 view, 2 view-projection, 4 from the eye, 8 projection)",
            which);
}

void SetHiddenShaders(const uintptr_t* shaders, int n) {
    bool changed = false;
    for (int i = 0; i < 4; ++i)
        changed |= g_hidePs[i].exchange(i < n ? shaders[i] : 0) != (i < n ? shaders[i] : 0);
    g_anyHidePs = n > 0;
    if (changed)
        Log("Effects: hiding draws with %d pixel shaders: %08X %08X %08X %08X", n,
            static_cast<unsigned>(g_hidePs[0].load()), static_cast<unsigned>(g_hidePs[1].load()),
            static_cast<unsigned>(g_hidePs[2].load()), static_cast<unsigned>(g_hidePs[3].load()));
}

void SetMoved(const UINT* counts, int n) {
    bool changed = false;
    for (int i = 0; i < 4; ++i)
        changed |= g_moveFx[i].exchange(i < n ? counts[i] : 0) != (i < n ? counts[i] : 0);
    if (changed)
        Log("Effects: drawing draws of %d index counts at the gun in the hand: %u %u %u %u", n, g_moveFx[0].load(),
            g_moveFx[1].load(), g_moveFx[2].load(), g_moveFx[3].load());
}

void DumpNext(UINT count) {
    g_dumpFx = count;
    g_dumpFxLeft = count ? 3 : 0;
}

void SetHidden(const UINT* counts, int n) {
    bool changed = false;
    for (int i = 0; i < 4; ++i)
        changed |= g_hideFx[i].exchange(i < n ? counts[i] : 0) != (i < n ? counts[i] : 0);
    if (changed)
        Log("Effects: hiding draws of %d index counts: %u %u %u %u", n, g_hideFx[0].load(), g_hideFx[1].load(),
            g_hideFx[2].load(), g_hideFx[3].load());
}

void StartFlashProbe() {
    g_probeUntil = GetTickCount64() + 10000;
    {
        std::lock_guard lock(g_probeMutex);
        g_probeLogged.clear();
    }
    Log("Muzzle probe: the next 10 s (fire, with weapon.txt \"watch\")");
}

void LogFrameTail() {
    g_tailState = 1;
}

void StartCensus() {
    std::lock_guard lock(g_censusMutex);
    g_tally.clear();
    g_framesBefore = g_framesAfter = 0;
    g_censusStart = GetTickCount64();
    g_census = true;
    Log("Census: every draw for 6 s; the first 2 s are the baseline (fire after them)");
}

void Update() {
    // Shots: the rounds in the gun going down by one (the flash comes with them).
    int loaded = 0, spare = 0;
    if (ammo::Get(loaded, spare)) {
        if (g_lastLoaded >= 0 && loaded == g_lastLoaded - 1)
            g_lastShot = GetTickCount64(), ++g_shots;
        g_lastLoaded = loaded;
    }
    // Once a second while anything happened: shots, and what became of the flash's draws.
    static ULONGLONG lastLog = 0;
    if (const ULONGLONG now = GetTickCount64(); now - lastLog >= 1000) {
        lastLog = now;
        const uint32_t shots = g_shots.exchange(0), seen = g_flashSeen.exchange(0), moved = g_movedDraws.exchange(0),
                       noDelta = g_noDelta.exchange(0), noView = g_noView.exchange(0), noSlot = g_noSlot.exchange(0);
        if (shots || seen || moved || noDelta || noView || noSlot)
            Log("Effects: last second %u shots; flash sprites %u; draws moved with the gun %u, not moved: %u no gun "
                "move, %u no view constants, %u not bound (matrices %d)",
                shots, seen, moved, noDelta, noView, noSlot, g_moveMatrices.load());
    }
    if (GetTickCount64() <= g_probeUntil.load(std::memory_order_relaxed)) {
    // Once a second while anything happened: shots, and what became of the flash's draws.
    static ULONGLONG lastLog = 0;
    if (const ULONGLONG now = GetTickCount64(); now - lastLog >= 1000) {
        lastLog = now;
        const uint32_t shots = g_shots.exchange(0), seen = g_flashSeen.exchange(0), moved = g_movedDraws.exchange(0),
                       noDelta = g_noDelta.exchange(0), noView = g_noView.exchange(0), noSlot = g_noSlot.exchange(0);
        if (shots || seen || moved || noDelta || noView || noSlot)
            Log("Effects: last second %u shots; flash sprites %u; draws moved with the gun %u, not moved: %u no gun "
                "move, %u no view constants, %u not bound (matrices %d)",
                shots, seen, moved, noDelta, noView, noSlot, g_moveMatrices.load());
    }
        // The game's muzzle: the end of the barrel in the gun's model space (the mesh dump).
        constexpr float kMuzzle[3] = {0.0f, 0.04f, 0.78f};
        float body[12], world[3], arms[3];
        bool have = skin::GameGunBody(body) && skin::GameGunPlaces(world, arms);
        if (have)
            for (int r = 0; r < 3; ++r) {
                g_gunWorld[r] = body[r * 4] * kMuzzle[0] + body[r * 4 + 1] * kMuzzle[1] + body[r * 4 + 2] * kMuzzle[2] +
                                body[r * 4 + 3];
                g_gunArms[r] = g_gunWorld[r] - (world[r] - arms[r]);
            }
        g_haveGun = have;
    }
    if (const int tail = g_tailState.load(std::memory_order_relaxed); tail == 1) {
        std::lock_guard lock(g_tailMutex);
        g_tail.clear();
        g_tailAll = 0;
        g_tailState = 2;
    } else if (tail == 2) {
        g_tailState = 0;
        LogTail();
    }
    if (!g_census.load(std::memory_order_relaxed))
        return;
    std::lock_guard lock(g_censusMutex);
    g_censusFrame.fetch_add(1, std::memory_order_relaxed);
    ULONGLONG elapsed = GetTickCount64() - g_censusStart;
    if (elapsed < kBaseline) {
        ++g_framesBefore;
    } else if (elapsed < kCensus) {
        ++g_framesAfter;
    } else {
        g_census = false;
        Report();
        g_tally.clear();
    }
}

}
