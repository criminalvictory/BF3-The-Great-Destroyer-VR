#include "streamview.h"

#include <windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

#include "config.h"
#include "depth3d.h"
#include "log.h"

namespace {

constexpr float kPi = 3.14159265f;
// A jump in where the head looks bigger than this from one frame to the next (a recenter, a cut) is gone to at once.
constexpr float kSnap = 0.35f;

std::once_flag g_loaded;
std::atomic<bool> g_on{true};
std::atomic<float> g_fovDeg{85.0f};
// The shape the view is drawn for, width over height: 16:9 squeezed into the 2:1 window, so OBS's "stretch to
// screen" on a 16:9 canvas shows it in proportion (drawn for the window's own 2:1 it was stretched 12% tall: "it's
// too stretched out", the user, 2026-11-01). 0: the window's own shape.
std::atomic<float> g_aspect{16.0f / 9.0f};
// The whole eye's picture fitted into that shape, as tall as the view, black at its sides (as seen in the headset:
// the user, 2026-11-01), instead of the view filled with its middle.
std::atomic<bool> g_fit{false};
std::atomic<bool> g_steady{true};
std::atomic<float> g_still{0.5f};    // Hz
std::atomic<float> g_speedUp{3.0f};  // Hz per radian a second
std::atomic<float> g_tiltDeg{0.0f};
std::atomic<bool> g_restart{false};

// bf3vr.ini's settings, once, before anything changes them (Home, weapon.txt).
void Load() {
    std::call_once(g_loaded, [] {
        const config::Settings& s = config::Get();
        g_on = s.streamView;
        g_fovDeg = s.streamFovDeg;
        g_aspect = s.streamAspect;
        g_fit = s.streamFit;
        g_steady = s.streamSteady;
        g_tiltDeg = s.streamTiltDeg;
    });
}

// Each pixel of the window looks along its ray from the window's camera; turned into the eye's own camera
// (row0-2), the ray lands on the eye's picture, which is read there. Off the picture it's black (the steadying
// keeps the window's corners on it).
const char kShader[] = R"(
cbuffer Stream : register(b0) {
    float4 row0, row1, row2;  // the window camera's ray -> the eye's camera (x right, y up, -z ahead)
    float4 extent;            // xy: the window's half width and height, as tangents from its camera
    float4 edges;             // the eye picture's left, right, top and bottom edges, as tangents
};
Texture2D picture : register(t0);
SamplerState linearClamp : register(s0);

void VS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0) {
    uv = float2((id << 1) & 2, id & 2);
    pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 PS(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 v = float3((uv.x * 2 - 1) * extent.x, (1 - uv.y * 2) * extent.y, -1);
    float3 r = float3(dot(row0.xyz, v), dot(row1.xyz, v), dot(row2.xyz, v));
    if (r.z > -1e-4)
        return float4(0, 0, 0, 1);
    float2 t = r.xy / -r.z;
    float2 p = float2((t.x - edges.x) / (edges.y - edges.x), (edges.z - t.y) / (edges.z - edges.w));
    if (any(p < 0) || any(p > 1))
        return float4(0, 0, 0, 1);
    return float4(picture.SampleLevel(linearClamp, p, 0).rgb, 1);
}
)";

struct Params {
    float rows[3][4];  // the turn into the eye's camera, row by row (the fourth of each unused)
    float extent[4];
    float edges[4];
};

struct Resources {
    bool tried = false, ok = false;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11Buffer* params = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState* noDepth = nullptr;
    // The left eye, copied out of the back buffer each frame to be read while the back buffer is drawn over.
    ID3D11Texture2D* eye = nullptr;
    ID3D11ShaderResourceView* eyeView = nullptr;
    UINT eyeWidth = 0, eyeHeight = 0;
    DXGI_FORMAT eyeFormat = DXGI_FORMAT_UNKNOWN;
} g_res;

template <class T>
void SafeRelease(T*& p) {
    if (p)
        p->Release();
    p = nullptr;
}

ID3DBlob* Compile(const char* entry, const char* profile) {
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    if (FAILED(D3DCompile(kShader, sizeof(kShader) - 1, "streamview", nullptr, nullptr, entry, profile,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors))) {
        Log("Stream view: shader %s failed: %s", entry,
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no message");
        SafeRelease(code);
    }
    SafeRelease(errors);
    return code;
}

bool CreateFixed(ID3D11Device* device) {
    if (g_res.tried)
        return g_res.ok;
    g_res.tried = true;
    ID3DBlob* vs = Compile("VS", "vs_5_0");
    ID3DBlob* ps = Compile("PS", "ps_5_0");
    bool ok = vs && ps &&
              SUCCEEDED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_res.vs)) &&
              SUCCEEDED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_res.ps));
    SafeRelease(vs);
    SafeRelease(ps);

    D3D11_BUFFER_DESC cb{sizeof(Params), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = FALSE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ok = ok && SUCCEEDED(device->CreateBuffer(&cb, nullptr, &g_res.params)) &&
         SUCCEEDED(device->CreateSamplerState(&sd, &g_res.sampler)) &&
         SUCCEEDED(device->CreateRasterizerState(&rd, &g_res.raster)) &&
         SUCCEEDED(device->CreateBlendState(&bd, &g_res.blend)) &&
         SUCCEEDED(device->CreateDepthStencilState(&dd, &g_res.noDepth));
    g_res.ok = ok;
    if (!ok)
        Log("Stream view: its shaders could not be made; the window shows both eyes");
    return ok;
}

// The texture the left eye is copied into: half the back buffer's width, in its format.
bool PrepareEye(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& back) {
    const UINT width = back.Width / 2;
    if (g_res.eye && g_res.eyeWidth == width && g_res.eyeHeight == back.Height && g_res.eyeFormat == back.Format)
        return true;
    SafeRelease(g_res.eyeView);
    SafeRelease(g_res.eye);
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = back.Height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = back.Format;
    td.SampleDesc = {1, 0};
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &g_res.eye)) ||
        FAILED(device->CreateShaderResourceView(g_res.eye, nullptr, &g_res.eyeView))) {
        SafeRelease(g_res.eyeView);
        SafeRelease(g_res.eye);
        return false;
    }
    g_res.eyeWidth = width;
    g_res.eyeHeight = back.Height;
    g_res.eyeFormat = back.Format;
    return true;
}

// The pipeline state the pass changes, put back afterwards: this runs inside the game's Present. As depth3d.cpp's,
// with the hull and domain shaders too (a full-screen triangle can't be drawn with tessellation on).
struct SavedState {
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D11VertexShader* vs = nullptr;
    ID3D11HullShader* hs = nullptr;
    ID3D11DomainShader* ds = nullptr;
    ID3D11GeometryShader* gs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11Buffer* cb = nullptr;
    ID3D11BlendState* blend = nullptr;
    FLOAT blendFactor[4] = {};
    UINT sampleMask = 0;
    ID3D11DepthStencilState* depth = nullptr;
    UINT stencilRef = 0;
    ID3D11RasterizerState* raster = nullptr;

    void Save(ID3D11DeviceContext* c) {
        c->OMGetRenderTargets(1, &rtv, &dsv);
        c->RSGetViewports(&viewportCount, viewports);
        c->VSGetShader(&vs, nullptr, nullptr);
        c->HSGetShader(&hs, nullptr, nullptr);
        c->DSGetShader(&ds, nullptr, nullptr);
        c->GSGetShader(&gs, nullptr, nullptr);
        c->PSGetShader(&ps, nullptr, nullptr);
        c->IAGetInputLayout(&layout);
        c->IAGetPrimitiveTopology(&topology);
        c->PSGetShaderResources(0, 1, &srv);
        c->PSGetSamplers(0, 1, &sampler);
        c->PSGetConstantBuffers(0, 1, &cb);
        c->OMGetBlendState(&blend, blendFactor, &sampleMask);
        c->OMGetDepthStencilState(&depth, &stencilRef);
        c->RSGetState(&raster);
    }
    void Restore(ID3D11DeviceContext* c) {
        c->OMSetRenderTargets(1, &rtv, dsv);
        c->RSSetViewports(viewportCount, viewports);
        c->VSSetShader(vs, nullptr, 0);
        c->HSSetShader(hs, nullptr, 0);
        c->DSSetShader(ds, nullptr, 0);
        c->GSSetShader(gs, nullptr, 0);
        c->PSSetShader(ps, nullptr, 0);
        c->IASetInputLayout(layout);
        c->IASetPrimitiveTopology(topology);
        c->PSSetShaderResources(0, 1, &srv);
        c->PSSetSamplers(0, 1, &sampler);
        c->PSSetConstantBuffers(0, 1, &cb);
        c->OMSetBlendState(blend, blendFactor, sampleMask);
        c->OMSetDepthStencilState(depth, stencilRef);
        c->RSSetState(raster);
        SafeRelease(rtv);
        SafeRelease(dsv);
        SafeRelease(vs);
        SafeRelease(hs);
        SafeRelease(ds);
        SafeRelease(gs);
        SafeRelease(ps);
        SafeRelease(layout);
        SafeRelease(srv);
        SafeRelease(sampler);
        SafeRelease(cb);
        SafeRelease(blend);
        SafeRelease(depth);
        SafeRelease(raster);
    }
};

// The 1-euro filter (Casiez, Roussel and Vogel, 2012) on one angle: held still it follows slowly, so the head's
// small shakes are taken out; turning, it follows closely, so the view doesn't trail the turn.
struct OneEuro {
    float value = 0.0f, speed = 0.0f;
    float raw = 0.0f;  // the last angle given: the speed is measured from it, as in the filter's own code
    static float Alpha(float cutoff, float dt) { return 1.0f / (1.0f + 1.0f / (2.0f * kPi * cutoff * dt)); }
    void Start(float x) {
        value = raw = x;
        speed = 0.0f;
    }
    void Step(float x, float dt) {
        speed += Alpha(1.0f, dt) * ((x - raw) / dt - speed);
        raw = x;
        value += Alpha(g_still.load() + g_speedUp.load() * std::fabs(speed), dt) * (x - value);
    }
};

// The window's camera: its yaw and pitch (LookAngles' sense in xr_runtime.cpp, the yaw not wrapped round), level.
struct Steadied {
    bool started = false;
    OneEuro yaw, pitch;
} g_cam;

// A quaternion's turn as a matrix, m[row][column]: its columns are the turned x, y and z axes.
void Matrix(const XrQuaternionf& q, float m[3][3]) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    m[0][0] = 1 - 2 * (y * y + z * z), m[0][1] = 2 * (x * y - w * z), m[0][2] = 2 * (x * z + w * y);
    m[1][0] = 2 * (x * y + w * z), m[1][1] = 1 - 2 * (x * x + z * z), m[1][2] = 2 * (y * z - w * x);
    m[2][0] = 2 * (x * z - w * y), m[2][1] = 2 * (y * z + w * x), m[2][2] = 1 - 2 * (x * x + y * y);
}

// A level camera looking along yaw (left positive) and pitch (up positive): its right, up and back as columns.
void LevelMatrix(float yaw, float pitch, float m[3][3]) {
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const float f[3] = {-sy * cp, sp, -cy * cp};
    const float r[3] = {cy, 0.0f, -sy};
    const float u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    for (int i = 0; i < 3; ++i) {
        m[i][0] = r[i];
        m[i][1] = u[i];
        m[i][2] = -f[i];
    }
}

// A ray from the window's camera (stream) turned into the eye's camera (eye): eye's transpose times stream.
void Between(const float eye[3][3], const float stream[3][3], float out[3][3]) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out[i][j] = eye[0][i] * stream[0][j] + eye[1][i] * stream[1][j] + eye[2][i] * stream[2][j];
}

// Whether the window's four corners, seen from a camera turned by toImage from the eye's, land on the eye's
// picture within inside (its edges a little in, so the reads keep off them). The window's edges land as straight
// lines between them, so the corners are enough.
bool Fits(const float toImage[3][3], const float extent[2], const float inside[4]) {
    for (int c = 0; c < 4; ++c) {
        const float v[3] = {c & 1 ? extent[0] : -extent[0], c & 2 ? extent[1] : -extent[1], -1.0f};
        float r[3];
        for (int i = 0; i < 3; ++i)
            r[i] = toImage[i][0] * v[0] + toImage[i][1] * v[1] + toImage[i][2] * v[2];
        if (r[2] > -1e-3f)
            return false;
        const float x = r[0] / -r[2], y = r[1] / -r[2];
        if (x < inside[0] || x > inside[1] || y > inside[2] || y < inside[3])
            return false;
    }
    return true;
}

// The eye's own camera, turned down by tilt about its right, or as much of the tilt as the window fits with.
void Plain(float tilt, const float extent[2], const float inside[4], float out[3][3]) {
    auto tilted = [](float t, float m[3][3]) {
        const float c = std::cos(t), s = std::sin(t);
        const float turn[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, c, s}, {0.0f, -s, c}};
        std::memcpy(m, turn, sizeof(turn));
    };
    tilted(tilt, out);
    if (tilt == 0.0f || Fits(out, extent, inside))
        return;
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 12; ++i) {
        const float mid = 0.5f * (lo + hi);
        float m[3][3];
        tilted(tilt * mid, m);
        (Fits(m, extent, inside) ? lo : hi) = mid;
    }
    tilted(tilt * lo, out);
}

// The turn from the window's camera into the eye's for this frame's picture, drawn for image.
void Aim(const XrQuaternionf* image, float dt, const float extent[2], const float inside[4], float out[3][3]) {
    const float tilt = std::clamp(g_tiltDeg.load(), -30.0f, 30.0f) * kPi / 180.0f;
    if (g_restart.exchange(false))
        g_cam.started = false;
    if (!image || !g_steady.load()) {
        g_cam.started = false;
        Plain(tilt, extent, inside, out);
        return;
    }
    float eye[3][3];
    Matrix(*image, eye);
    // Where the eye looks, (0, 0, -1) turned, as yaw and pitch.
    const float f[3] = {-eye[0][2], -eye[1][2], -eye[2][2]};
    const float yaw = std::atan2(-f[0], -f[2]);
    const float eyePitch = std::asin(std::clamp(f[1], -1.0f, 1.0f));
    // Whether a level window looking `down` below where the eye looks fits on the picture.
    auto levelFits = [&](float down) {
        float stream[3][3], m[3][3];
        LevelMatrix(yaw, std::clamp(eyePitch - down, -1.45f, 1.45f), stream);
        Between(eye, stream, m);
        return Fits(m, extent, inside);
    };
    // As much of the tilt as a level window fits with (all of it unless the head is rolled far): taken all or
    // nothing, the horizon jumped to the head's whole roll at some roll.
    float fitTilt = tilt;
    if (tilt != 0.0f && !levelFits(tilt) && levelFits(0.0f)) {
        float lo = 0.0f, hi = 1.0f;
        for (int i = 0; i < 12; ++i) {
            const float mid = 0.5f * (lo + hi);
            (levelFits(tilt * mid) ? lo : hi) = mid;
        }
        fitTilt = tilt * lo;
    }
    const float pitch = std::clamp(eyePitch - fitTilt, -1.45f, 1.45f);
    // A jump from the last frame's angles (a recenter, a cut) is gone to at once. Measured against the steadied
    // angles instead, which trail a turn, a quick look at a low frame rate passed for one.
    const bool jump = g_cam.started && (std::fabs(std::remainder(yaw - g_cam.yaw.raw, 2.0f * kPi)) > kSnap ||
                                        std::fabs(pitch - g_cam.pitch.raw) > kSnap);
    const float unwrapped = g_cam.started ? g_cam.yaw.raw + std::remainder(yaw - g_cam.yaw.raw, 2.0f * kPi) : yaw;
    if (!g_cam.started || jump) {
        g_cam.yaw.Start(unwrapped);
        g_cam.pitch.Start(pitch);
        g_cam.started = true;
    } else {
        g_cam.yaw.Step(unwrapped, dt);
        g_cam.pitch.Step(pitch, dt);
    }
    // t = 1: the steadied camera; t = 0: level where the eye looks (the yaw unwrapped next to the steadied one).
    const float aimYaw = g_cam.yaw.value + std::remainder(yaw - g_cam.yaw.value, 2.0f * kPi);
    auto build = [&](float t, float m[3][3]) {
        float stream[3][3];
        LevelMatrix(aimYaw + t * (g_cam.yaw.value - aimYaw), pitch + t * (g_cam.pitch.value - pitch), stream);
        Between(eye, stream, m);
    };
    build(1.0f, out);
    if (Fits(out, extent, inside))
        return;
    build(0.0f, out);
    if (!Fits(out, extent, inside)) {
        // The head tilted too far for a level window to fit on the picture: the eye's own camera.
        Plain(tilt, extent, inside, out);
        return;
    }
    // The steadied camera pulled toward where the eye looks only as far as the window needs to stay on the
    // picture; the filter carries on from there.
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 12; ++i) {
        const float mid = 0.5f * (lo + hi);
        float m[3][3];
        build(mid, m);
        (Fits(m, extent, inside) ? lo : hi) = mid;
    }
    build(lo, out);
    g_cam.yaw.value = aimYaw + lo * (g_cam.yaw.value - aimYaw);
    g_cam.pitch.value = pitch + lo * (g_cam.pitch.value - pitch);
}

}

namespace streamview {

void Draw(ID3D11Device* device, ID3D11Texture2D* back, const XrQuaternionf* image, const XrFovf& fov) {
    Load();
    static bool s_drawing = false;
    if (!g_on.load()) {
        if (s_drawing) {
            s_drawing = false;
            Log("Stream view: off, the window shows both eyes side by side");
        }
        g_cam.started = false;
        return;
    }
    D3D11_TEXTURE2D_DESC desc;
    back->GetDesc(&desc);
    if (desc.SampleDesc.Count != 1 || desc.Width != 2 * desc.Height || !CreateFixed(device))
        return;
    ID3D11RenderTargetView* target = nullptr;
    if (!PrepareEye(device, desc) || FAILED(device->CreateRenderTargetView(back, nullptr, &target))) {
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            Log("Stream view: the window's picture (format %d) can't be drawn over; it shows both eyes", desc.Format);
        }
        return;
    }

    static LARGE_INTEGER s_frequency{}, s_last{};
    LARGE_INTEGER now;
    if (!s_frequency.QuadPart)
        QueryPerformanceFrequency(&s_frequency);
    QueryPerformanceCounter(&now);
    const float dt = s_last.QuadPart ? std::clamp(static_cast<float>(now.QuadPart - s_last.QuadPart) /
                                                      static_cast<float>(s_frequency.QuadPart),
                                                  0.002f, 0.1f)
                                     : 1.0f / 72.0f;
    s_last = now;

    // The eye's picture as tangents of its camera, and a little in from them; the view as wide as asked within it,
    // as tall as its shape makes it (and no taller than the picture).
    const float picture[4] = {std::tan(fov.angleLeft), std::tan(fov.angleRight), std::tan(fov.angleUp),
                              std::tan(fov.angleDown)};
    if (!(picture[1] - picture[0] > 0.01f) || !(picture[2] - picture[3] > 0.01f)) {
        target->Release();
        return;  // no field of view known (nothing to map the window onto)
    }
    const float inside[4] = {picture[0] * 0.99f, picture[1] * 0.99f, picture[2] * 0.99f, picture[3] * 0.99f};
    const float asked = std::tan(0.5f * std::clamp(g_fovDeg.load(), 20.0f, 170.0f) * kPi / 180.0f);
    const float shape = g_aspect.load() > 0.0f ? std::clamp(g_aspect.load(), 0.5f, 4.0f)
                                               : static_cast<float>(desc.Width) / static_cast<float>(desc.Height);
    const bool fit = g_fit.load();
    // Fitted: as tall as the picture, the shape's width; what's off the picture to the sides is black. Steadied, the
    // window turns on the picture to keep the horizon level as the head rolls, so the circle its corners turn on
    // must fit in the picture (with an eye picture no wider than the window, it could be level at no roll at all).
    const float tall = std::min(picture[2], -picture[3]);
    const float smallest = std::min({-inside[0], inside[1], inside[2], -inside[3]});
    const float room = g_steady.load() ? std::sqrt(1.0f + 1.0f / (shape * shape)) : 1.0f;
    const float across = fit ? tall * shape
                             : std::min({asked, -inside[0], inside[1], shape * inside[2], -shape * inside[3],
                                         smallest / room});
    const float extent[2] = {across, across / shape};
    float toImage[3][3];
    if (fit) {
        // The picture as the headset shows it, not turned (turned, its edges would swing into view).
        const float straight[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
        std::memcpy(toImage, straight, sizeof(straight));
        g_cam.started = false;
    } else {
        Aim(image, dt, extent, inside, toImage);
    }
    Params p{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            p.rows[i][j] = toImage[i][j];
    p.extent[0] = extent[0];
    p.extent[1] = extent[1];
    std::memcpy(p.edges, picture, sizeof(picture));

    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    const D3D11_BOX left{0, 0, 0, desc.Width / 2, desc.Height, 1};
    context->CopySubresourceRegion(g_res.eye, 0, 0, 0, 0, back, 0, &left);
    SavedState saved;
    saved.Save(context);
    const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(desc.Width), static_cast<float>(desc.Height),
                                  0.0f, 1.0f};
    context->RSSetViewports(1, &viewport);
    context->VSSetShader(g_res.vs, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    context->PSSetShader(g_res.ps, nullptr, 0);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->PSSetShaderResources(0, 1, &g_res.eyeView);
    context->PSSetSamplers(0, 1, &g_res.sampler);
    context->PSSetConstantBuffers(0, 1, &g_res.params);
    context->OMSetBlendState(g_res.blend, nullptr, 0xFFFFFFFF);
    context->OMSetDepthStencilState(g_res.noDepth, 0);
    context->RSSetState(g_res.raster);
    context->UpdateSubresource(g_res.params, 0, nullptr, &p, 0, 0);
    context->OMSetRenderTargets(1, &target, nullptr);
    // DrawInstanced, not Draw: the mod's own Draw hook would take a draw over the whole window for an eye's
    // picture and send the next frame's first one to the right eye (overlay.cpp).
    context->DrawInstanced(3, 1, 0, 0);
    ID3D11ShaderResourceView* none = nullptr;
    context->PSSetShaderResources(0, 1, &none);
    saved.Restore(context);
    target->Release();
    context->Release();
    depth3d::SaveFinalIfAsked(device, back, L"stream_view.bmp");  // depth3d.txt "save N"

    static bool s_fitShown = false;
    if (!s_drawing || s_fitShown != fit) {
        s_drawing = true;
        s_fitShown = fit;
        if (fit)
            Log("Stream view: the window (and OBS's game capture) shows the left eye's whole picture fitted in, "
                "drawn %.2f:1, black at its sides; Home switches it",
                shape);
        else
            Log("Stream view: the window (and OBS's game capture) shows the left eye alone, %.0f degrees across, "
                "drawn %.2f:1%s%s; Home switches it",
                2.0f * std::atan(across) * 180.0f / kPi, shape,
                !g_steady.load() ? ", as the head moves" : image ? ", steadied with the horizon level" : "",
                image ? "" : " (no headset frame yet: the middle of the eye's picture)");
    }
}

void SetOn(bool on) {
    Load();
    g_on = on;
}

bool On() {
    Load();
    return g_on.load();
}

void SetFov(float degrees) {
    Load();
    degrees = std::clamp(degrees, 20.0f, 170.0f);
    if (g_fovDeg.exchange(degrees) != degrees)
        Log("Stream view: %.0f degrees across asked (at most the eye's own)", degrees);
}

void SetAspect(float aspect) {
    Load();
    aspect = aspect > 0.0f ? std::clamp(aspect, 0.5f, 4.0f) : 0.0f;
    if (g_aspect.exchange(aspect) != aspect)
        Log("Stream view: drawn %s", aspect > 0.0f ? "for a shape of its own (OBS stretches it to the canvas)"
                                                  : "in the window's own shape");
}

void SetFit(bool on) {
    Load();
    g_fit = on;
}

void SetSteady(bool on, float still, float speedUp) {
    Load();
    if (still > 0.0f)
        g_still = still;
    if (speedUp >= 0.0f)
        g_speedUp = speedUp;
    g_steady = on;
    Log("Stream view: %s", on ? "steadied with the horizon level" : "as the head moves");
    if (on)
        Log("Stream view: steadying held still %.2f Hz, %.2f Hz faster per radian a second", g_still.load(),
            g_speedUp.load());
}

void SetTilt(float degrees) {
    Load();
    degrees = std::clamp(degrees, -30.0f, 30.0f);
    if (g_tiltDeg.exchange(degrees) != degrees)
        Log("Stream view: looking %.0f degrees %s where the head looks", std::fabs(degrees),
            degrees >= 0.0f ? "below" : "above");
}

void Reset() {
    g_restart = true;
}

}
