#include "depth3d.h"

#include <windows.h>
#include <d3dcompiler.h>
#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "log.h"

namespace {

using ClearDsvFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8);
ClearDsvFn g_origClear[2] = {};

std::atomic<UINT> g_width{0}, g_height{0};

// Depth buffers the size of the picture, cleared since the last Probe. Each holds a reference
// until then, so none can go away while it is looked at.
struct Candidate {
    ID3D11Texture2D* texture;
    D3D11_TEXTURE2D_DESC desc;
    UINT clears;
    float clearValue;
};
std::mutex g_mutex;
std::vector<Candidate> g_candidates;
ID3D11Texture2D* g_depth = nullptr;  // the one in use, referenced
D3D11_TEXTURE2D_DESC g_depthDesc{};

void OnClear(ID3D11DepthStencilView* dsv, UINT flags, FLOAT value) {
    UINT width = g_width.load(std::memory_order_relaxed);
    if (!dsv || !(flags & D3D11_CLEAR_DEPTH) || !width)
        return;
    ID3D11Resource* resource = nullptr;
    dsv->GetResource(&resource);
    if (!resource)
        return;
    D3D11_RESOURCE_DIMENSION dim;
    resource->GetType(&dim);
    if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        auto texture = static_cast<ID3D11Texture2D*>(resource);
        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);
        if (desc.Width == width && desc.Height == g_height.load(std::memory_order_relaxed) && desc.ArraySize == 1) {
            std::lock_guard lock(g_mutex);
            bool known = false;
            for (Candidate& c : g_candidates) {
                if (c.texture == texture) {
                    ++c.clears;
                    c.clearValue = value;
                    known = true;
                    break;
                }
            }
            if (!known && g_candidates.size() < 16) {
                texture->AddRef();
                g_candidates.push_back({texture, desc, 1, value});
            }
        }
    }
    resource->Release();
}

void STDMETHODCALLTYPE Clear0(ID3D11DeviceContext* c, ID3D11DepthStencilView* v, UINT f, FLOAT d, UINT8 s) {
    OnClear(v, f, d);
    g_origClear[0](c, v, f, d, s);
}
void STDMETHODCALLTYPE Clear1(ID3D11DeviceContext* c, ID3D11DepthStencilView* v, UINT f, FLOAT d, UINT8 s) {
    OnClear(v, f, d);
    g_origClear[1](c, v, f, d, s);
}

// A depth format's typeless family, and how to read its depth in a shader.
DXGI_FORMAT TypelessDepth(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24G8_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32G8X24_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_TYPELESS;
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_TYPELESS;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
DXGI_FORMAT DepthReadFormat(DXGI_FORMAT typeless) {
    switch (typeless) {
    case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
DXGI_FORMAT StencilReadFormat(DXGI_FORMAT typeless) {
    switch (typeless) {
    case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_X24_TYPELESS_G8_UINT;
    case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
// A colour format's typeless family, so the copy can be read as sRGB like the headset image.
DXGI_FORMAT TypelessColor(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_R10G10B10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_TYPELESS;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_TYPELESS;
    default: return f;
    }
}

// Each output pixel looks for the picture's pixel that lands on it: x_out = x + shift / distance(x),
// solved by repeating x = x_out - shift / distance(x), which settles wherever depth is smooth.
const char kShader[] = R"(
cbuffer Params : register(b0) {
    float shift;
    float nearPlane;
    float depthScale;
    float minDistance;
    float2 size;
    float debugView;
    float fillMode;  // gaps: 0 the edge's background stretched, 1 mirrored, 2 mirrored and softer
    float gunScale;  // the gun and arms are drawn squeezed towards the eye: their distances times this
    float gunBelow;  // they are what is nearer than this...
    float gunMask;   // ...or, when not 0, what has these stencil bits
    float unused;
};
Texture2D<float4> colorTex : register(t0);
#ifdef MULTISAMPLE
// The nearest of the samples: an edge pixel, whose colour is part object and part background,
// then moves with the object instead of staying behind as a fringe.
Texture2DMS<float> depthTex : register(t1);
Texture2DMS<uint2> stencilTex : register(t2);
float DepthAt(float2 uv) {
    int2 p = int2(saturate(uv) * (size - 1));
    return min(min(depthTex.Load(p, 0), depthTex.Load(p, 1)), min(depthTex.Load(p, 2), depthTex.Load(p, 3)));
}
uint StencilAt(float2 uv) {
    int2 p = int2(saturate(uv) * (size - 1));
    return stencilTex.Load(p, 0).g | stencilTex.Load(p, 1).g | stencilTex.Load(p, 2).g | stencilTex.Load(p, 3).g;
}
#else
Texture2D<float> depthTex : register(t1);
Texture2D<uint2> stencilTex : register(t2);
float DepthAt(float2 uv) { return depthTex.Load(int3(int2(saturate(uv) * (size - 1)), 0)); }
uint StencilAt(float2 uv) { return stencilTex.Load(int3(int2(saturate(uv) * (size - 1)), 0)).g; }
#endif
SamplerState linearSampler : register(s0);
float Distance(float2 uv) {
    float d = nearPlane / max(depthScale - DepthAt(uv), 1e-7);
    bool firstPerson = gunMask > 0 ? (StencilAt(uv) & (uint)gunMask) != 0 : d < gunBelow;
    return max(firstPerson ? d * gunScale : d, minDistance);
}
void VS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0) {
    uv = float2((id << 1) & 2, id & 2);
    pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 PS(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    if (debugView > 2.5) {
        // The stencil value as grey (0 to 255 over black to white), for reading back.
        float s = StencilAt(uv) / 255.0;
        return float4(s, s, s, 1);
    }
    if (debugView > 1.5) {
        // Distance as grey, black at 0 and white at 2 m, for reading back from a saved eye.
        float g = saturate(nearPlane / max(depthScale - DepthAt(uv), 1e-7) / 2);
        return float4(g, g, g, 1);
    }
    if (debugView > 0) {
        // Distance in bands: red under 0.3 m, yellow to 0.6, green to 1.5, cyan to 5, blue beyond;
        // brightness repeats every band so steps show.
        float d = nearPlane / max(depthScale - DepthAt(uv), 1e-7);
        float3 c = d < 0.3 ? float3(1, 0, 0) : d < 0.6 ? float3(1, 1, 0) : d < 1.5 ? float3(0, 1, 0)
                 : d < 5 ? float3(0, 1, 1) : float3(0, 0, 1);
        float3 picture = colorTex.SampleLevel(linearSampler, uv, 0).rgb;
        return float4(lerp(picture, c, 0.6), 1);
    }
    // Walk the picture from the far side of the largest possible shift towards this pixel; the
    // first place whose shifted position reaches it is the nearest surface there, as an eye would
    // see it. Where that place is a jump onto something nearer, the gap behind that nearer thing
    // shows the background just before it. (Solving x = x_out - shift / distance(x) by repeating
    // it, the first version, latched onto either side of an edge and doubled edges.)
    float dir = shift >= 0 ? 1 : -1;
    float maxShift = abs(shift) / minDistance;
    const int steps = 48;
    float stepSize = maxShift / steps;
    float x = uv.x - dir * maxShift;
    float prevX = x, prevF = -1;
    float found = uv.x;
    [loop] for (int i = 0; i <= steps; ++i) {
        float f = (x + shift / Distance(float2(x, uv.y)) - uv.x) * dir;
        if (f >= 0) {
            if (i == 0)
                found = x;
            else if (f - prevF > 3 * stepSize) {
                // A gap: the background mirrored across the edge, as far out as this pixel is into
                // the gap, so each pixel gets its own background (one pixel stretched streaked).
                float into = fillMode > 0 ? max((uv.x - (prevX + shift / Distance(float2(prevX, uv.y)))) * dir, 0) : 0;
                found = -2 - (prevX - dir * (into + 3 / size.x));
            } else
                found = lerp(prevX, x, -prevF / max(f - prevF, 1e-6));
            break;
        }
        prevX = x;
        prevF = f;
        x += dir * stepSize;
    }
    if (found <= -2) {
        // A gap behind something nearer, which the game never drew: a soft mix of the background
        // beside it (several pixels further out, a few rows up and down) instead of one pixel
        // stretched across, which streaked.
        float bx = -2 - found;
        float spread = fillMode > 1 ? 3.0 : 1.5;
        float3 sum = 0;
        [unroll] for (int k = 0; k < 2; ++k)
            [unroll] for (int r = -1; r <= 1; ++r)
                sum += colorTex.SampleLevel(linearSampler, float2(bx - dir * k * spread / size.x, uv.y + r * spread / size.y), 0).rgb;
        return float4(sum / 6, 1);
    }
    // Past the picture's edge (the shifted eye's outer strip): the edge itself, not black.
    return float4(colorTex.SampleLevel(linearSampler, float2(saturate(found), uv.y), 0).rgb, 1);
}
)";

struct Params {
    float shift, nearPlane, depthScale, minDistance;
    float width, height, debugView, fillMode;
    float gunScale, gunBelow, gunMask, unused;
};

// Live tuning from depth3d.txt next to the DLL: "strength 1.0", "min 0.25", "debug 1",
// "fill 1" (gaps: 0 stretched, 1 mirrored, 2 mirrored and softer), "show 1" (the desktop
// window shows the right eye instead of the left).
// "debug 1" colours distance in bands, "debug 2" shows it as grey (black 0, white 2 m).
std::atomic<float> g_strength{1.0f}, g_minDistance{0.25f}, g_fillMode{1.0f}, g_debugView{0.0f};
std::atomic<bool> g_showRight{false};
// "hold 1": the picture and depth stop updating while the eyes keep being made from them, so
// settings can be compared by screenshot on one and the same frame.
std::atomic<bool> g_hold{false};
// "bias": how the eye shift is shared. 0 evenly; -1 the left eye gets the game's own picture and
// the right eye all the shift (so every filled gap is in one eye only), 1 the other way round.
std::atomic<float> g_bias{0.0f};
// "gun 2.4": the gun and arms' distances are multiplied by 2.4. BF3 draws them squeezed towards
// the eye (the grip reads 0.13 m, the front of the gun 0.29 m), so without it they are a flat
// cut-out at the nearest distance allowed. "gun 1" turns it off.
// What counts as the gun and arms: "mask 32", what has these stencil bits (BF3 marks them with 32,
// the world with 2; "debug 3" shows the values), or with "mask 0", what is nearer than "below 0.35"
// (which also catches part of the arm, so the stencil is better).
std::atomic<float> g_gunScale{2.4f}, g_gunBelow{0.35f};
std::atomic<int> g_gunMask{32};
// "save N": when N changes, both eyes are written to eye_left.bmp and eye_right.bmp next to the
// DLL (a window screenshot of the game can mix two frames).
std::atomic<int> g_saveRequest{0};
int g_saved = 0;

void ReadTuning() {
    static FILETIME lastWrite{};
    wchar_t path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ReadTuning), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\'))
        wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"depth3d.txt");
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &info) ||
        CompareFileTime(&info.ftLastWriteTime, &lastWrite) == 0)
        return;
    lastWrite = info.ftLastWriteTime;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"r") != 0 || !f)
        return;
    char text[256] = {};
    fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    float strength = 1.0f, minDistance = 0.25f, debug = 0.0f, fill = 1.0f, show = 0.0f, hold = 0.0f, bias = 0.0f;
    int save = 0;
    if (const char* s = std::strstr(text, "hold"))
        sscanf_s(s + 4, "%f", &hold);
    g_hold = hold > 0.0f;
    if (const char* s = std::strstr(text, "bias"))
        sscanf_s(s + 4, "%f", &bias);
    g_bias = std::clamp(bias, -1.0f, 1.0f);
    if (const char* s = std::strstr(text, "save"))
        sscanf_s(s + 4, "%d", &save);
    g_saveRequest = save;
    float gun = 2.4f, below = 0.35f;
    if (const char* s = std::strstr(text, "gun"))
        sscanf_s(s + 3, "%f", &gun);
    if (const char* s = std::strstr(text, "below"))
        sscanf_s(s + 5, "%f", &below);
    g_gunScale = std::max(gun, 0.1f);
    g_gunBelow = below;
    int mask = 32;
    if (const char* s = std::strstr(text, "mask"))
        sscanf_s(s + 4, "%d", &mask);
    g_gunMask = mask & 0xFF;
    if (const char* s = std::strstr(text, "strength"))
        sscanf_s(s + 8, "%f", &strength);
    if (const char* s = std::strstr(text, "min"))
        sscanf_s(s + 3, "%f", &minDistance);
    if (const char* s = std::strstr(text, "debug"))
        sscanf_s(s + 5, "%f", &debug);
    if (const char* s = std::strstr(text, "fill"))
        sscanf_s(s + 4, "%f", &fill);
    if (const char* s = std::strstr(text, "show"))
        sscanf_s(s + 4, "%f", &show);
    g_strength = strength;
    g_minDistance = minDistance;
    g_debugView = debug;
    g_fillMode = fill;
    g_showRight = show > 0.0f;
    Log("Depth 3D: strength %.2f, nearest %.2f m, gaps %s, depth view %.0f, desktop shows the %s eye, bias %.2f, "
        "gun (%s) distances times %.2f",
        strength, minDistance, fill > 1.0f ? "mirrored and softer" : fill > 0.0f ? "mirrored" : "stretched", debug,
        show > 0.0f ? "right" : "left", g_bias.load(),
        g_gunMask.load() ? "stencil bits" : "nearer than the cut-off", g_gunScale.load());
}

// Writes a 4-byte-per-pixel texture to a top-down 32-bit BMP (blue, green, red, alpha).
bool SaveBmp(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture, const wchar_t* name) {
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    bool rgba;
    switch (desc.Format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: rgba = true; break;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: rgba = false; break;
    default: return false;
    }
    D3D11_TEXTURE2D_DESC sd = desc;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    sd.MipLevels = 1;
    sd.ArraySize = 1;
    sd.SampleDesc = {1, 0};
    ID3D11Texture2D* staging = nullptr;
    if (desc.SampleDesc.Count > 1 || FAILED(device->CreateTexture2D(&sd, nullptr, &staging)))
        return false;
    context->CopySubresourceRegion(staging, 0, 0, 0, 0, texture, 0, nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped;
    bool ok = false;
    if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        wchar_t path[MAX_PATH];
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&SaveBmp), &self);
        GetModuleFileNameW(self, path, MAX_PATH);
        if (wchar_t* slash = wcsrchr(path, L'\\'))
            wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), name);
        FILE* f = nullptr;
        if (_wfopen_s(&f, path, L"wb") == 0 && f) {
            const uint32_t rowBytes = desc.Width * 4, imageBytes = rowBytes * desc.Height;
            BITMAPFILEHEADER fh{};
            BITMAPINFOHEADER ih{};
            fh.bfType = 0x4D42;
            fh.bfOffBits = sizeof(fh) + sizeof(ih);
            fh.bfSize = fh.bfOffBits + imageBytes;
            ih.biSize = sizeof(ih);
            ih.biWidth = static_cast<LONG>(desc.Width);
            ih.biHeight = -static_cast<LONG>(desc.Height);
            ih.biPlanes = 1;
            ih.biBitCount = 32;
            ih.biSizeImage = imageBytes;
            fwrite(&fh, sizeof(fh), 1, f);
            fwrite(&ih, sizeof(ih), 1, f);
            std::vector<uint8_t> row(rowBytes);
            for (UINT y = 0; y < desc.Height; ++y) {
                std::memcpy(row.data(), static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch, rowBytes);
                if (rgba)
                    for (UINT x = 0; x < rowBytes; x += 4)
                        std::swap(row[x], row[x + 2]);
                fwrite(row.data(), 1, rowBytes, f);
            }
            fclose(f);
            ok = true;
        }
        context->Unmap(staging, 0);
    }
    staging->Release();
    return ok;
}

struct Resources {
    bool tried = false, ok = false;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps[2] = {};  // single-sample and multisample depth
    ID3D11Buffer* params = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState* noDepth = nullptr;
    // The picture, copied each frame so it can be read as sRGB.
    ID3D11Texture2D* color = nullptr;
    ID3D11ShaderResourceView* colorView = nullptr;
    D3D11_TEXTURE2D_DESC colorFor{};
    DXGI_FORMAT colorViewFormat = DXGI_FORMAT_UNKNOWN;
    // The depth: read in place when the game allows it, else from a copy.
    ID3D11Texture2D* depthFor = nullptr;  // not referenced; compared only
    ID3D11Texture2D* depthCopy = nullptr;
    ID3D11ShaderResourceView* depthView = nullptr;
    ID3D11ShaderResourceView* stencilView = nullptr;  // null when the depth has no stencil
    std::unordered_map<ID3D11Texture2D*, ID3D11RenderTargetView*> targets;
    bool held = false;  // the last frame was made from held copies
} g_res;

template <class T>
void SafeRelease(T*& p) {
    if (p)
        p->Release();
    p = nullptr;
}

ID3DBlob* Compile(const char* entry, const char* profile, bool multisample) {
    D3D_SHADER_MACRO defines[] = {{"MULTISAMPLE", "1"}, {nullptr, nullptr}};
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile(kShader, sizeof(kShader) - 1, "depth3d", multisample ? defines : nullptr, nullptr, entry,
                            profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        Log("Depth 3D: shader %s failed: %s", entry,
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
    ID3DBlob* vs = Compile("VS", "vs_5_0", false);
    ID3DBlob* ps = Compile("PS", "ps_5_0", false);
    ID3DBlob* psMs = Compile("PS", "ps_5_0", true);
    bool ok = vs && ps && psMs &&
              SUCCEEDED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_res.vs)) &&
              SUCCEEDED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_res.ps[0])) &&
              SUCCEEDED(device->CreatePixelShader(psMs->GetBufferPointer(), psMs->GetBufferSize(), nullptr,
                                                  &g_res.ps[1]));
    SafeRelease(vs);
    SafeRelease(ps);
    SafeRelease(psMs);

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
    Log("Depth 3D: shaders %s", ok ? "ready" : "could not be made; staying with the old 3D");
    return ok;
}

bool PrepareColor(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* back, DXGI_FORMAT viewFormat) {
    D3D11_TEXTURE2D_DESC desc;
    back->GetDesc(&desc);
    if (!g_res.color || desc.Width != g_res.colorFor.Width || desc.Height != g_res.colorFor.Height ||
        desc.Format != g_res.colorFor.Format || viewFormat != g_res.colorViewFormat) {
        SafeRelease(g_res.colorView);
        SafeRelease(g_res.color);
        D3D11_TEXTURE2D_DESC copy{};
        copy.Width = desc.Width;
        copy.Height = desc.Height;
        copy.MipLevels = 1;
        copy.ArraySize = 1;
        copy.Format = TypelessColor(desc.Format);
        copy.SampleDesc.Count = 1;
        copy.Usage = D3D11_USAGE_DEFAULT;
        copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
        // Read it as the headset image is written (sRGB when that is sRGB), so colours pass through.
        vd.Format = TypelessColor(viewFormat) == copy.Format ? viewFormat : desc.Format;
        vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        vd.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateTexture2D(&copy, nullptr, &g_res.color)) ||
            FAILED(device->CreateShaderResourceView(g_res.color, &vd, &g_res.colorView))) {
            SafeRelease(g_res.color);
            return false;
        }
        g_res.colorFor = desc;
        g_res.colorViewFormat = viewFormat;
    }
    if (g_hold.load() && g_res.held)
        return true;  // keep the frame from when hold began, as the depth does
    if (desc.SampleDesc.Count > 1)
        context->ResolveSubresource(g_res.color, 0, back, 0, desc.Format);
    else
        context->CopyResource(g_res.color, back);
    return true;
}

bool PrepareDepth(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* depth,
                  const D3D11_TEXTURE2D_DESC& desc) {
    DXGI_FORMAT typeless = TypelessDepth(desc.Format);
    DXGI_FORMAT read = DepthReadFormat(typeless);
    if (read == DXGI_FORMAT_UNKNOWN)
        return false;
    // Read in place only from a typeless texture the game made readable; otherwise copy it. While
    // held, always from a copy, which then stops being refreshed.
    bool hold = g_hold.load();
    bool inPlace = (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) && desc.Format == typeless && !hold;
    if (hold && g_res.depthFor == depth && g_res.depthCopy)
        return true;  // held: keep the copy as it is
    if (depth != g_res.depthFor || (inPlace == (g_res.depthCopy != nullptr))) {
        SafeRelease(g_res.depthView);
        SafeRelease(g_res.stencilView);
        SafeRelease(g_res.depthCopy);
        g_res.depthFor = nullptr;
        ID3D11Texture2D* source = depth;
        if (!inPlace) {
            D3D11_TEXTURE2D_DESC copy = desc;
            copy.Format = typeless;
            copy.Usage = D3D11_USAGE_DEFAULT;
            copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            copy.CPUAccessFlags = 0;
            copy.MiscFlags = 0;
            if (FAILED(device->CreateTexture2D(&copy, nullptr, &g_res.depthCopy)))
                return false;
            source = g_res.depthCopy;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
        vd.Format = read;
        if (desc.SampleDesc.Count > 1) {
            vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        } else {
            vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            vd.Texture2D.MipLevels = 1;
        }
        if (FAILED(device->CreateShaderResourceView(source, &vd, &g_res.depthView))) {
            SafeRelease(g_res.depthCopy);
            return false;
        }
        // The stencil too, where there is one: BF3 may mark what it draws in it.
        vd.Format = StencilReadFormat(typeless);
        if (vd.Format == DXGI_FORMAT_UNKNOWN || FAILED(device->CreateShaderResourceView(source, &vd, &g_res.stencilView)))
            g_res.stencilView = nullptr;
        g_res.depthFor = depth;
        Log("Depth 3D: reading depth %ux%u format %d, %u samples, %s%s", desc.Width, desc.Height, desc.Format,
            desc.SampleDesc.Count, inPlace ? "in place" : "from a copy", g_res.stencilView ? ", with stencil" : "");
    }
    if (g_res.depthCopy)
        context->CopyResource(g_res.depthCopy, depth);
    return true;
}

ID3D11RenderTargetView* TargetView(ID3D11Device* device, ID3D11Texture2D* target, DXGI_FORMAT format) {
    auto it = g_res.targets.find(target);
    if (it != g_res.targets.end())
        return it->second;
    D3D11_RENDER_TARGET_VIEW_DESC rd{};
    rd.Format = format;
    rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ID3D11RenderTargetView* view = nullptr;
    if (FAILED(device->CreateRenderTargetView(target, &rd, &view)))
        return nullptr;
    g_res.targets[target] = view;
    return view;
}

// How long the graphics card spends making both eyes, logged every few seconds. Read a few frames
// late, never waiting for the card.
struct GpuTimer {
    static constexpr int kSlots = 4;
    ID3D11Query* disjoint[kSlots] = {};
    ID3D11Query* begin[kSlots] = {};
    ID3D11Query* end[kSlots] = {};
    bool pending[kSlots] = {};
    bool tried = false, ok = false, running = false;
    int next = 0;
    double totalMs = 0.0;
    int samples = 0;
    ULONGLONG lastLog = 0;

    bool Create(ID3D11Device* device) {
        if (tried)
            return ok;
        tried = true;
        D3D11_QUERY_DESC dd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, td{D3D11_QUERY_TIMESTAMP, 0};
        ok = true;
        for (int i = 0; i < kSlots && ok; ++i)
            ok = SUCCEEDED(device->CreateQuery(&dd, &disjoint[i])) && SUCCEEDED(device->CreateQuery(&td, &begin[i])) &&
                 SUCCEEDED(device->CreateQuery(&td, &end[i]));
        return ok;
    }
    void Begin(ID3D11DeviceContext* c) {
        running = ok && !pending[next];
        if (running) {
            c->Begin(disjoint[next]);
            c->End(begin[next]);
        }
    }
    void End(ID3D11DeviceContext* c) {
        if (!running)
            return;
        c->End(end[next]);
        c->End(disjoint[next]);
        pending[next] = true;
        next = (next + 1) % kSlots;
    }
    void Collect(ID3D11DeviceContext* c) {
        for (int i = 0; i < kSlots; ++i) {
            if (!pending[i])
                continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d;
            UINT64 t0 = 0, t1 = 0;
            if (c->GetData(disjoint[i], &d, sizeof(d), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
                c->GetData(begin[i], &t0, sizeof(t0), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
                c->GetData(end[i], &t1, sizeof(t1), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
                continue;
            pending[i] = false;
            if (!d.Disjoint && d.Frequency && t1 > t0) {
                totalMs += 1000.0 * static_cast<double>(t1 - t0) / static_cast<double>(d.Frequency);
                ++samples;
            }
        }
        ULONGLONG now = GetTickCount64();
        if (now - lastLog >= 5000 && samples) {
            Log("Depth 3D: making both eyes takes %.2f ms on the graphics card per frame", totalMs / samples);
            totalMs = 0.0;
            samples = 0;
            lastLog = now;
        }
    }
} g_timer;

// The pipeline state Render changes, put back afterwards: this runs inside the game's Present.
struct SavedState {
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11GeometryShader* gs = nullptr;
    ID3D11InputLayout* layout = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11ShaderResourceView* srvs[3] = {};
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
        c->PSGetShader(&ps, nullptr, nullptr);
        c->GSGetShader(&gs, nullptr, nullptr);
        c->IAGetInputLayout(&layout);
        c->IAGetPrimitiveTopology(&topology);
        c->PSGetShaderResources(0, 3, srvs);
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
        c->PSSetShader(ps, nullptr, 0);
        c->GSSetShader(gs, nullptr, 0);
        c->IASetInputLayout(layout);
        c->IASetPrimitiveTopology(topology);
        c->PSSetShaderResources(0, 3, srvs);
        c->PSSetSamplers(0, 1, &sampler);
        c->PSSetConstantBuffers(0, 1, &cb);
        c->OMSetBlendState(blend, blendFactor, sampleMask);
        c->OMSetDepthStencilState(depth, stencilRef);
        c->RSSetState(raster);
        SafeRelease(rtv);
        SafeRelease(dsv);
        SafeRelease(vs);
        SafeRelease(ps);
        SafeRelease(gs);
        SafeRelease(layout);
        for (ID3D11ShaderResourceView*& v : srvs)
            SafeRelease(v);
        SafeRelease(sampler);
        SafeRelease(cb);
        SafeRelease(blend);
        SafeRelease(depth);
        SafeRelease(raster);
    }
};

}

namespace depth3d {

bool InstallHooks(void* const (&clearDepthStencilView)[2]) {
    void* hooks[2] = {reinterpret_cast<void*>(&Clear0), reinterpret_cast<void*>(&Clear1)};
    for (int i = 0; i < 2; ++i) {
        if (i == 1 && clearDepthStencilView[1] == clearDepthStencilView[0]) {
            g_origClear[1] = g_origClear[0];
            break;
        }
        if (MH_CreateHook(clearDepthStencilView[i], hooks[i], reinterpret_cast<void**>(&g_origClear[i])) != MH_OK) {
            Log("Depth 3D: hooking ClearDepthStencilView failed");
            return false;
        }
    }
    return true;
}

int MirrorEye() {
    return g_showRight.load() ? 1 : 0;
}

float Strength() {
    return g_strength.load();
}

void SetPictureSize(UINT width, UINT height) {
    g_width = width;
    g_height = height;
}

bool Render(ID3D11Device* device, ID3D11Texture2D* back, DXGI_FORMAT targetFormat, const Eye (&eyes)[2],
            const DepthModel& model) {
    ID3D11Texture2D* depth = nullptr;
    D3D11_TEXTURE2D_DESC depthDesc{};
    {
        std::lock_guard lock(g_mutex);
        depth = g_depth;
        depthDesc = g_depthDesc;
        if (depth)
            depth->AddRef();
    }
    if (!depth)
        return false;
    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    bool ok = CreateFixed(device) && PrepareColor(device, context, back, targetFormat) &&
              PrepareDepth(device, context, depth, depthDesc);
    g_res.held = ok && g_hold.load();
    if (ok) {
        g_timer.Create(device);
        g_timer.Collect(context);
        g_timer.Begin(context);
        SavedState saved;
        saved.Save(context);
        D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(g_res.colorFor.Width),
                                static_cast<float>(g_res.colorFor.Height), 0.0f, 1.0f};
        ID3D11ShaderResourceView* views[3] = {g_res.colorView, g_res.depthView, g_res.stencilView};
        context->RSSetViewports(1, &viewport);
        context->VSSetShader(g_res.vs, nullptr, 0);
        context->PSSetShader(g_res.ps[depthDesc.SampleDesc.Count > 1 ? 1 : 0], nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->PSSetShaderResources(0, 3, views);
        context->PSSetSamplers(0, 1, &g_res.sampler);
        context->PSSetConstantBuffers(0, 1, &g_res.params);
        context->OMSetBlendState(g_res.blend, nullptr, 0xFFFFFFFF);
        context->OMSetDepthStencilState(g_res.noDepth, 0);
        context->RSSetState(g_res.raster);
        for (int e = 0; e < 2; ++e) {
            const Eye& eye = eyes[e];
            ID3D11RenderTargetView* target = TargetView(device, eye.target, targetFormat);
            if (!target) {
                ok = false;
                break;
            }
            const float bias = model.bias >= -1.0f && model.bias <= 1.0f ? model.bias : g_bias.load();
            const float share = e == 0 ? 1.0f + bias : 1.0f - bias;
            Params p{eye.shift * share * g_strength.load(), model.nearPlane, model.depthScale, g_minDistance.load(),
                     static_cast<float>(depthDesc.Width), static_cast<float>(depthDesc.Height),
                     g_debugView.load(), g_fillMode.load(), g_gunScale.load(), g_gunBelow.load(),
                     static_cast<float>(g_gunMask.load())};
            context->UpdateSubresource(g_res.params, 0, nullptr, &p, 0, 0);
            context->OMSetRenderTargets(1, &target, nullptr);
            context->Draw(3, 0);
        }
        ID3D11ShaderResourceView* none[3] = {};
        context->PSSetShaderResources(0, 3, none);
        saved.Restore(context);
        g_timer.End(context);
        if (ok && g_saveRequest.load() != g_saved) {
            g_saved = g_saveRequest.load();
            bool written = SaveBmp(device, context, eyes[0].target, L"eye_left.bmp") &&
                           SaveBmp(device, context, eyes[1].target, L"eye_right.bmp");
            Log("Depth 3D: eyes %s (save %d)",
                written ? "written to eye_left.bmp and eye_right.bmp" : "could not be written", g_saved);
        }
    }
    context->Release();
    depth->Release();
    return ok;
}

void Reset() {
    for (auto& [texture, view] : g_res.targets)
        view->Release();
    g_res.targets.clear();
}

bool SaveIfAsked(ID3D11Device* device, ID3D11Texture2D* picture, const wchar_t* name) {
    if (g_saveRequest.load() == g_saved)
        return false;
    g_saved = g_saveRequest.load();
    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    bool written = SaveBmp(device, context, picture, name);
    context->Release();
    Log("Depth 3D: picture %s (save %d)", written ? "written" : "could not be written", g_saved);
    return written;
}

void Probe() {
    static ULONGLONG lastLog = 0, lastRead = 0;
    ULONGLONG now = GetTickCount64();
    if (now - lastRead >= 300) {
        lastRead = now;
        ReadTuning();
    }
    if (now - lastLog < 1000)
        return;
    lastLog = now;
    std::lock_guard lock(g_mutex);
    // The depth buffer the picture's size that is cleared most: the scene's.
    Candidate* best = nullptr;
    for (Candidate& c : g_candidates)
        if (!best || c.clears > best->clears)
            best = &c;
    if (best && best->texture != g_depth) {
        SafeRelease(g_depth);
        g_depth = best->texture;
        g_depth->AddRef();
        g_depthDesc = best->desc;
    }
    static int logged = 0;
    if (logged < 5 && !g_candidates.empty()) {
        ++logged;
        for (const Candidate& c : g_candidates)
            Log("Depth 3D: depth buffer %p %ux%u format %d, %u samples, bind %X, cleared %u times to %.3f%s",
                static_cast<void*>(c.texture), c.desc.Width, c.desc.Height, c.desc.Format, c.desc.SampleDesc.Count,
                c.desc.BindFlags, c.clears, c.clearValue, c.texture == g_depth ? " (used)" : "");
    }
    for (Candidate& c : g_candidates)
        c.texture->Release();
    g_candidates.clear();
}

}
