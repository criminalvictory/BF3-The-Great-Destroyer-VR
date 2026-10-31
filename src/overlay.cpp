#include "overlay.h"

#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "log.h"
#include "views.h"

namespace {

// ---- The enemies' orange markers, taken off ----
// BF3's HUD marks enemies with orange diamonds (squadmates and objectives blue); in VR they're drawn
// at the screen's distance, not the enemy's, and the user wants them gone (2026-10-31: they were the
// "crosshairs"). Their draws can't be told from the blue ones (the same icon sheet and shaders, the
// colour put on inside the game's own shader), so the window's picture is copied just before the
// HUD is drawn, and at Present, wherever the HUD made a pixel more orange, it goes back to what it
// was. Blue markers, white text and red damage marks are left. weapon.txt "orangemarkers 1": keep
// them.
std::atomic<bool> g_keepOrange{false};
std::atomic<bool> g_keepCrosshair{false};  // weapon.txt "crosshair 1": the game's crosshair left on (Draw)

// Two passes: where the HUD made a pixel more orange (a mask, its mips made by the hardware), then
// everything the HUD drew near that put back as it was: the markers' white rim and dark shadow
// aren't orange themselves and stayed as a ghost ring when only the orange was taken off.
constexpr char kKeyShader[] = R"(
Texture2D<float4> before : register(t0);
Texture2D<float4> after : register(t1);
Texture2D<float> mask : register(t2);
SamplerState linearClamp : register(s0);
void VS(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD0) {
    uv = float2((id << 1) & 2, id & 2);
    pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
// How orange a colour is: green between red and blue, as far from both as it can be.
float Orange(float3 c) { return min(c.r - c.g, c.g - c.b); }
float Mark(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    int3 p = int3(pos.xy, 0);
    float4 b = before.Load(p), a = after.Load(p);
    // The HUD's own pixel orange too: a white or grey icon over the teal scene is "more orange" than the scene
    // and was cut out as a square (the blur study, 2026-10-31).
    return Orange(a.rgb) > 0.05 && Orange(a.rgb) - Orange(b.rgb) > 0.06 && a.r > b.r + 0.03 ? 1 : 0;
}
float4 PutBack(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    int3 p = int3(pos.xy, 0);
    // A 32-pixel mip, sampled between texels: any orange within about 40 pixels.
    return mask.SampleLevel(linearClamp, uv, 5) > 0.0005 ? before.Load(p) : after.Load(p);
}
)";

struct KeyResources {
    ID3D11Texture2D* before = nullptr;
    ID3D11Texture2D* after = nullptr;
    ID3D11Texture2D* mask = nullptr;            // where the HUD put orange, with mips
    ID3D11ShaderResourceView* views[3] = {};    // before, after, mask
    ID3D11RenderTargetView* maskTarget = nullptr;
    UINT width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* mark = nullptr;
    ID3D11PixelShader* putBack = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    bool failed = false;
} g_key;
// This frame: the window's target as the HUD was drawn into it (held), its picture before the HUD.
ID3D11RenderTargetView* g_keyTarget = nullptr;
ID3D11Texture2D* g_keyTexture = nullptr;
std::atomic<uint32_t> g_keyFrames{0};

template <class T> void Drop(T*& p) {
    if (p)
        p->Release();
    p = nullptr;
}

// The shaders and states once, the two copies of the window's size and format as it is.
bool EnsureKey(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& target) {
    if (g_key.failed)
        return false;
    if (!g_key.mark) {
        auto compile = [&](const char* entry, const char* profile, ID3DBlob** out) {
            ID3DBlob* errors = nullptr;
            const bool ok = SUCCEEDED(D3DCompile(kKeyShader, sizeof(kKeyShader) - 1, "orange", nullptr, nullptr, entry,
                                                 profile, 0, 0, out, &errors));
            if (errors) {
                Log("Overlay: orange markers' shader %s: %s", entry, static_cast<const char*>(errors->GetBufferPointer()));
                errors->Release();
            }
            return ok;
        };
        ID3DBlob* vs = nullptr;
        ID3DBlob* mark = nullptr;
        ID3DBlob* putBack = nullptr;
        const bool ok =
            compile("VS", "vs_4_0", &vs) && compile("Mark", "ps_4_0", &mark) && compile("PutBack", "ps_4_0", &putBack) &&
            SUCCEEDED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_key.vs)) &&
            SUCCEEDED(device->CreatePixelShader(mark->GetBufferPointer(), mark->GetBufferSize(), nullptr, &g_key.mark)) &&
            SUCCEEDED(device->CreatePixelShader(putBack->GetBufferPointer(), putBack->GetBufferSize(), nullptr,
                                                &g_key.putBack));
        for (ID3DBlob* b : {vs, mark, putBack})
            if (b)
                b->Release();
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_DEPTH_STENCIL_DESC dd{};
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (!ok || FAILED(device->CreateBlendState(&bd, &g_key.blend)) ||
            FAILED(device->CreateDepthStencilState(&dd, &g_key.depth)) ||
            FAILED(device->CreateRasterizerState(&rd, &g_key.raster)) || FAILED(device->CreateSamplerState(&sd, &g_key.sampler))) {
            g_key.failed = true;
            Log("Overlay: the orange markers can't be taken off (no shader)");
            return false;
        }
    }
    if (g_key.before && g_key.width == target.Width && g_key.height == target.Height && g_key.format == target.Format)
        return true;
    for (ID3D11ShaderResourceView*& v : g_key.views)
        Drop(v);
    Drop(g_key.maskTarget);
    Drop(g_key.before);
    Drop(g_key.after);
    Drop(g_key.mask);
    D3D11_TEXTURE2D_DESC d = target;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    d.CPUAccessFlags = 0;
    d.MiscFlags = 0;
    D3D11_TEXTURE2D_DESC m = d;
    m.Format = DXGI_FORMAT_R16_FLOAT;  // a lone orange pixel survives five mips (in 8 bits it rounds away)
    m.MipLevels = 0;  // all of them
    m.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    m.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    D3D11_RENDER_TARGET_VIEW_DESC mv{};
    mv.Format = DXGI_FORMAT_R16_FLOAT;
    mv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    if (FAILED(device->CreateTexture2D(&d, nullptr, &g_key.before)) || FAILED(device->CreateTexture2D(&d, nullptr, &g_key.after)) ||
        FAILED(device->CreateTexture2D(&m, nullptr, &g_key.mask)) ||
        FAILED(device->CreateShaderResourceView(g_key.before, nullptr, &g_key.views[0])) ||
        FAILED(device->CreateShaderResourceView(g_key.after, nullptr, &g_key.views[1])) ||
        FAILED(device->CreateShaderResourceView(g_key.mask, nullptr, &g_key.views[2])) ||
        FAILED(device->CreateRenderTargetView(g_key.mask, &mv, &g_key.maskTarget))) {
        for (ID3D11ShaderResourceView*& v : g_key.views)
            Drop(v);
        Drop(g_key.maskTarget);
        Drop(g_key.before);
        Drop(g_key.after);
        Drop(g_key.mask);
        g_key.failed = true;
        Log("Overlay: the orange markers can't be taken off (no copy of the window, format %d)", target.Format);
        return false;
    }
    g_key.width = target.Width, g_key.height = target.Height, g_key.format = target.Format;
    return true;
}

// Just before the frame's first HUD draw: the window's picture as it is.
void TakeBefore(ID3D11DeviceContext* ctx) {
    if (g_keepOrange.load(std::memory_order_relaxed) || g_keyTarget || g_key.failed)
        return;
    ID3D11RenderTargetView* target = nullptr;
    ctx->OMGetRenderTargets(1, &target, nullptr);
    if (!target)
        return;
    ID3D11Resource* resource = nullptr;
    target->GetResource(&resource);
    ID3D11Texture2D* texture = nullptr;
    if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)))) {
        D3D11_TEXTURE2D_DESC d;
        texture->GetDesc(&d);
        ID3D11Device* device = nullptr;
        ctx->GetDevice(&device);
        if (device && d.SampleDesc.Count == 1 && d.MipLevels == 1 && d.ArraySize == 1 && EnsureKey(device, d)) {
            ctx->CopyResource(g_key.before, texture);
            g_keyTarget = target;  // held until Present
            target = nullptr;
            g_keyTexture = texture;
            texture = nullptr;
        }
        if (device)
            device->Release();
    }
    Drop(texture);
    Drop(resource);
    Drop(target);
}

// At Present: the HUD's orange put back to what was under it.
void TakeOffOrange(ID3D11DeviceContext* ctx, IDXGISwapChain* swap) {
    if (!g_keyTarget)
        return;
    ctx->CopyResource(g_key.after, g_keyTexture);
    // The pipeline state this changes, put back afterwards (this runs inside the game's Present).
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    ctx->OMGetRenderTargets(1, &rtv, &dsv);
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ctx->RSGetViewports(&viewportCount, viewports);
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11GeometryShader* gs = nullptr;
    ctx->VSGetShader(&vs, nullptr, nullptr);
    ctx->PSGetShader(&ps, nullptr, nullptr);
    ctx->GSGetShader(&gs, nullptr, nullptr);
    ID3D11InputLayout* layout = nullptr;
    ctx->IAGetInputLayout(&layout);
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ctx->IAGetPrimitiveTopology(&topology);
    ID3D11ShaderResourceView* srvs[3] = {};
    ctx->PSGetShaderResources(0, 3, srvs);
    ID3D11SamplerState* sampler = nullptr;
    ctx->PSGetSamplers(0, 1, &sampler);
    ID3D11BlendState* blend = nullptr;
    FLOAT factor[4] = {};
    UINT mask = 0;
    ctx->OMGetBlendState(&blend, factor, &mask);
    ID3D11DepthStencilState* depth = nullptr;
    UINT ref = 0;
    ctx->OMGetDepthStencilState(&depth, &ref);
    ID3D11RasterizerState* raster = nullptr;
    ctx->RSGetState(&raster);

    const D3D11_VIEWPORT full{0.0f, 0.0f, static_cast<float>(g_key.width), static_cast<float>(g_key.height), 0.0f, 1.0f};
    ctx->RSSetViewports(1, &full);
    ctx->VSSetShader(g_key.vs, nullptr, 0);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->OMSetBlendState(g_key.blend, nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(g_key.depth, 0);
    ctx->RSSetState(g_key.raster);
    ctx->PSSetSamplers(0, 1, &g_key.sampler);
    // Where the HUD put orange, spread by the mask's mips...
    ID3D11ShaderResourceView* none[3] = {};
    ctx->PSSetShaderResources(0, 3, none);
    ctx->OMSetRenderTargets(1, &g_key.maskTarget, nullptr);
    ctx->PSSetShaderResources(0, 2, g_key.views);
    ctx->PSSetShader(g_key.mark, nullptr, 0);
    ctx->Draw(3, 0);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->GenerateMips(g_key.views[2]);
    // ...and everything the HUD drew near it put back as it was.
    ctx->OMSetRenderTargets(1, &g_keyTarget, nullptr);
    ctx->PSSetShaderResources(0, 3, g_key.views);
    ctx->PSSetShader(g_key.putBack, nullptr, 0);
    ctx->Draw(3, 0);

    ctx->PSSetShaderResources(0, 3, none);
    ctx->OMSetRenderTargets(1, &rtv, dsv);
    ctx->RSSetViewports(viewportCount, viewports);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    ctx->GSSetShader(gs, nullptr, 0);
    ctx->IASetInputLayout(layout);
    ctx->IASetPrimitiveTopology(topology);
    ctx->PSSetShaderResources(0, 3, srvs);
    ctx->PSSetSamplers(0, 1, &sampler);
    ctx->OMSetBlendState(blend, factor, mask);
    ctx->OMSetDepthStencilState(depth, ref);
    ctx->RSSetState(raster);
    Drop(rtv);
    Drop(dsv);
    Drop(vs);
    Drop(ps);
    Drop(gs);
    Drop(layout);
    for (ID3D11ShaderResourceView*& v : srvs)
        Drop(v);
    Drop(sampler);
    Drop(blend);
    Drop(depth);
    Drop(raster);

    if (g_keyFrames.fetch_add(1, std::memory_order_relaxed) == 0) {
        ID3D11Texture2D* back = nullptr;
        const bool same = swap && SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(&back))) && back == g_keyTexture;
        Drop(back);
        Log("Overlay: the enemies' orange markers are taken off (the HUD drawn %s)",
            same ? "into the window's own picture" : "into a picture of its own: CHECK it reaches the window");
    }
    Drop(g_keyTarget);
    Drop(g_keyTexture);
}

// The window's pictures and overlays are small draws: a quad or two each (6 to 36 vertices or
// indices in the frames logged with weapon.txt "tail", 2026-10-30).
constexpr UINT kMostVertices = 600;

// This frame so far: how many eyes' pictures have gone onto the window (the left eye's first).
std::atomic<int> g_eyePictures{0};

// Each eye's own effects on the window this frame (the damage tint, drawn right after each eye's
// picture), as the left eye's came: by pixel shader and vertex count. The right eye's are the same
// draws after its picture; anything else then is the HUD. (By shader alone, the screen's blood
// spatter, drawn once for the HUD with the tint's shader, went to the right eye only.)
struct EyeEffect {
    ID3D11PixelShader* ps;
    UINT count;
};
std::mutex g_shadersMutex;
std::vector<EyeEffect> g_eyeEffects;
// Pixel shaders of the HUD that read a picture of an eye's scene (an eye's size, a half or a
// quarter of it; the HUD's own pictures are 1024 square): true for those, asked once each.
std::unordered_map<ID3D11PixelShader*, bool> g_readsScene;

std::atomic<int> g_logged{0};
// weapon.txt "tail": the next frame's choices logged (1 asked, 2 this frame). "hudfix N": an
// experiment for the ammo counter that came out in the right eye only (2026-10-30): 1 = the left
// eye's copy drawn without the depth-stencil view.
std::atomic<int> g_logFrame{0};
std::atomic<int> g_hudFix{0};
// HUD draws after the right eye's picture, and the right eye's copies of them, counted for a log
// line once a second (tools/selftest.ps1 checks they match: the HUD in both eyes).
std::atomic<uint32_t> g_hudDraws{0}, g_hudCopies{0};
// weapon.txt "hudskip 10-19 40": the HUD's draws with those numbers in the frame (from 0, in the
// order the game draws them) left out, to tell which is which (the crosshair, 2026-10-31).
constexpr int kSkippable = 256;
std::atomic<uint64_t> g_hudSkip[kSkippable / 64] = {};
std::atomic<int> g_hudIndex{0};
// weapon.txt "hudskiptex 64x32 4096x128": the HUD's draws whose first texture is that size left out
// (sizes stay from run to run; the pointers don't).
std::atomic<uint32_t> g_hudSkipTex[4] = {};  // width << 16 | height, 0 unused

// The size of the pixel shader's first texture (width << 16 | height), 0 none.
uint32_t FirstTextureSize(ID3D11DeviceContext* ctx, DXGI_FORMAT* format = nullptr) {
    if (format)
        *format = DXGI_FORMAT_UNKNOWN;
    ID3D11ShaderResourceView* view = nullptr;
    ctx->PSGetShaderResources(0, 1, &view);
    if (!view)
        return 0;
    uint32_t size = 0;
    ID3D11Resource* resource = nullptr;
    view->GetResource(&resource);
    ID3D11Texture2D* texture = nullptr;
    if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)))) {
        D3D11_TEXTURE2D_DESC td;
        texture->GetDesc(&td);
        size = td.Width << 16 | (td.Height & 0xFFFF);
        if (format)
            *format = td.Format;
        texture->Release();
    }
    if (resource)
        resource->Release();
    view->Release();
    return size;
}

// A small RGBA texture's first mip read back and written as a 32-bit .bmp beside the DLL
// (hudtex_<draw>.bmp), with its average colour where it isn't see-through logged.
void SaveSmallTexture(ID3D11DeviceContext* ctx, ID3D11Texture2D* texture, const D3D11_TEXTURE2D_DESC& td, int index) {
    ID3D11Device* device = nullptr;
    texture->GetDevice(&device);
    D3D11_TEXTURE2D_DESC sd = td;
    sd.MipLevels = 1;
    sd.ArraySize = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    ID3D11Texture2D* copy = nullptr;
    if (device && SUCCEEDED(device->CreateTexture2D(&sd, nullptr, &copy))) {
        ctx->CopySubresourceRegion(copy, 0, 0, 0, 0, texture, 0, nullptr);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx->Map(copy, 0, D3D11_MAP_READ, 0, &mapped))) {
            std::vector<uint8_t> pixels(td.Width * td.Height * 4);
            double sum[3] = {}, weight = 0;
            for (UINT y = 0; y < td.Height; ++y)
                for (UINT x = 0; x < td.Width; ++x) {
                    const uint8_t* p = static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch + x * 4;
                    uint8_t* q = &pixels[((td.Height - 1 - y) * td.Width + x) * 4];  // bottom-up, BGRA
                    q[0] = p[2], q[1] = p[1], q[2] = p[0], q[3] = p[3];
                    for (int c = 0; c < 3; ++c)
                        sum[c] += p[c] * (p[3] / 255.0);
                    weight += p[3] / 255.0;
                }
            ctx->Unmap(copy, 0);
            wchar_t path[MAX_PATH];
            HMODULE self = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&SaveSmallTexture), &self);
            GetModuleFileNameW(self, path, MAX_PATH);
            if (wchar_t* slash = wcsrchr(path, L'\\'))
                swprintf_s(slash + 1, MAX_PATH - (slash + 1 - path), L"hudtex_%d.bmp", index);
            FILE* f = nullptr;
            if (_wfopen_s(&f, path, L"wb") == 0 && f) {
                BITMAPFILEHEADER fh{};
                BITMAPINFOHEADER ih{};
                ih.biSize = sizeof(ih);
                ih.biWidth = static_cast<LONG>(td.Width);
                ih.biHeight = static_cast<LONG>(td.Height);
                ih.biPlanes = 1;
                ih.biBitCount = 32;
                fh.bfType = 0x4D42;
                fh.bfOffBits = sizeof(fh) + sizeof(ih);
                fh.bfSize = fh.bfOffBits + static_cast<DWORD>(pixels.size());
                fwrite(&fh, sizeof(fh), 1, f);
                fwrite(&ih, sizeof(ih), 1, f);
                fwrite(pixels.data(), 1, pixels.size(), f);
                fclose(f);
            }
            Log("Overlay: HUD draw %d texture saved (hudtex_%d.bmp); its colour where drawn: %.0f %.0f %.0f", index,
                index, weight > 0 ? sum[0] / weight : 0.0, weight > 0 ? sum[1] / weight : 0.0,
                weight > 0 ? sum[2] / weight : 0.0);
        }
        copy->Release();
    }
    if (device)
        device->Release();
}

// A buffer's bytes [offset, offset + size) read back through a staging copy.
bool ReadBuffer(ID3D11DeviceContext* ctx, ID3D11Buffer* buffer, UINT offset, UINT size, void* out) {
    D3D11_BUFFER_DESC desc;
    buffer->GetDesc(&desc);
    if (!size || offset + size > desc.ByteWidth)
        return false;
    ID3D11Device* device = nullptr;
    buffer->GetDevice(&device);
    D3D11_BUFFER_DESC sd{};
    sd.ByteWidth = (size + 15) & ~15u;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* copy = nullptr;
    bool ok = false;
    if (device && SUCCEEDED(device->CreateBuffer(&sd, nullptr, &copy))) {
        const D3D11_BOX box{offset, 0, 0, offset + size, 1, 1};
        ctx->CopySubresourceRegion(copy, 0, 0, 0, 0, buffer, 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx->Map(copy, 0, D3D11_MAP_READ, 0, &mapped))) {
            std::memcpy(out, mapped.pData, size);
            ctx->Unmap(copy, 0);
            ok = true;
        }
        copy->Release();
    }
    if (device)
        device->Release();
    return ok;
}

// weapon.txt "tail": a small HUD draw's vertices, raw (a vertex colour would tell the markers apart).
void LogHudVertices(ID3D11DeviceContext* ctx, int index, UINT count, UINT start, INT base) {
    ID3D11Buffer* ib = nullptr;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT ibOffset = 0;
    ctx->IAGetIndexBuffer(&ib, &format, &ibOffset);
    ID3D11Buffer* vbs[4] = {};
    UINT strides[4] = {}, offsets[4] = {};
    ctx->IAGetVertexBuffers(0, 4, vbs, strides, offsets);
    const UINT indexSize = format == DXGI_FORMAT_R32_UINT ? 4 : 2;
    uint8_t raw[48] = {};
    if (ib && ReadBuffer(ctx, ib, ibOffset + start * indexSize, count * indexSize, raw)) {
        UINT first = indexSize == 4 ? *reinterpret_cast<uint32_t*>(raw) : *reinterpret_cast<uint16_t*>(raw);
        for (int s = 0; s < 4; ++s) {  // each stream the draw reads
            const UINT stride = strides[s];
            uint8_t v[64] = {};
            if (!vbs[s] || !stride || stride > 64 || !ReadBuffer(ctx, vbs[s], offsets[s] + (first + base) * stride, stride, v))
                continue;
            char line[400];
            int used = 0;
            for (UINT i = 0; i < stride / 4 && used < 360; ++i) {
                float f;
                std::memcpy(&f, v + i * 4, 4);
                used += snprintf(line + used, sizeof(line) - used, " %08X(%.3g)", *reinterpret_cast<uint32_t*>(v + i * 4), f);
            }
            Log("Overlay: HUD draw %d first vertex, stream %d (stride %u):%s", index, s, stride, line);
        }
    }
    if (ib)
        ib->Release();
    for (ID3D11Buffer* vb : vbs)
        if (vb)
            vb->Release();
}

// weapon.txt "tail": a HUD draw's constants, read back through a staging copy (a stall: the one
// frame logged only): the first floats of the pixel and vertex shaders' first two buffers, and the
// first texture's size. To tell the HUD's pieces apart by their colour (the orange enemy markers,
// 2026-10-31).
void LogHudConstants(ID3D11DeviceContext* ctx, int index) {
    // Direct3D 11.1 binds a part of a constant buffer (the HUD's draws: the start of their 12800-byte
    // buffer held the same post-processing numbers for every one of them): where each slot's starts.
    UINT psFirst[2] = {}, vsFirst[2] = {};
    ID3D11DeviceContext1* ctx1 = nullptr;
    if (SUCCEEDED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&ctx1)))) {
        ID3D11Buffer* b[2] = {};
        UINT count[2] = {};
        ctx1->PSGetConstantBuffers1(0, 2, b, psFirst, count);
        for (ID3D11Buffer* x : b)
            if (x)
                x->Release();
        ctx1->VSGetConstantBuffers1(0, 2, b, vsFirst, count);
        for (ID3D11Buffer* x : b)
            if (x)
                x->Release();
        ctx1->Release();
    }
    auto dump = [&](ID3D11Buffer* buffer, const char* stage, int slot) {
        const UINT first = (stage[0] == 'P' ? psFirst : vsFirst)[slot] * 16;  // bytes
        if (!buffer)
            return;
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
        if (device && SUCCEEDED(device->CreateBuffer(&staging, nullptr, &copy))) {
            ctx->CopyResource(copy, buffer);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(ctx->Map(copy, 0, D3D11_MAP_READ, 0, &mapped))) {
                const UINT at = first < desc.ByteWidth ? first : 0;
                const auto* f = reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped.pData) + at);
                char line[400];
                int used = 0;
                for (UINT i = 0; i < std::min<UINT>((desc.ByteWidth - at) / 4, 24) && used < 360; ++i)
                    used += snprintf(line + used, sizeof(line) - used, " %.3g", f[i]);
                Log("Overlay: HUD draw %d %s constants %d (%u bytes, from %u):%s", index, stage, slot, desc.ByteWidth, at,
                    line);
                ctx->Unmap(copy, 0);
            }
            copy->Release();
        }
        if (device)
            device->Release();
    };
    ID3D11Buffer* ps[2] = {};
    ID3D11Buffer* vs[2] = {};
    ctx->PSGetConstantBuffers(0, 2, ps);
    ctx->VSGetConstantBuffers(0, 2, vs);
    for (int i = 0; i < 2; ++i) {
        dump(ps[i], "PS", i);
        dump(vs[i], "VS", i);
        if (ps[i])
            ps[i]->Release();
        if (vs[i])
            vs[i]->Release();
    }
    ID3D11ShaderResourceView* view = nullptr;
    ctx->PSGetShaderResources(0, 1, &view);
    if (view) {
        ID3D11Resource* resource = nullptr;
        view->GetResource(&resource);
        D3D11_TEXTURE2D_DESC td{};
        ID3D11Texture2D* texture = nullptr;
        if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)))) {
            texture->GetDesc(&td);
            // A small uncompressed picture (the markers' are 128x64) is saved beside the DLL as
            // hudtex_<draw>.bmp, to see which marker it is, and its average colour logged.
            if (td.Format == DXGI_FORMAT_R8G8B8A8_UNORM && td.Width <= 256 && td.Height <= 256)
                SaveSmallTexture(ctx, texture, td, index);
            texture->Release();
        }
        if (resource)
            resource->Release();
        view->Release();
        Log("Overlay: HUD draw %d texture %p %ux%u format %d", index, static_cast<void*>(resource), td.Width, td.Height,
            td.Format);
    }
}

// The draw with no depth-stencil view bound (no depth or stencil test).
void DrawWithoutDepth(ID3D11DeviceContext* ctx, const std::function<void()>& draw) {
    ID3D11RenderTargetView* target = nullptr;
    ID3D11DepthStencilView* depth = nullptr;
    ctx->OMGetRenderTargets(1, &target, &depth);
    if (!depth) {
        draw();
    } else {
        ctx->OMSetRenderTargets(1, &target, nullptr);
        draw();
        ctx->OMSetRenderTargets(1, &target, depth);
        depth->Release();
    }
    if (target)
        target->Release();
}

bool Blended(ID3D11DeviceContext* ctx) {
    ID3D11BlendState* blend = nullptr;
    FLOAT factor[4];
    UINT mask = 0;
    ctx->OMGetBlendState(&blend, factor, &mask);
    bool blended = false;
    if (blend) {
        D3D11_BLEND_DESC desc;
        blend->GetDesc(&desc);
        blended = desc.RenderTarget[0].BlendEnable != FALSE;
        blend->Release();
    }
    return blended;
}

// The render target's size: the window is twice as wide as high.
bool OnWindow(ID3D11DeviceContext* ctx) {
    ID3D11RenderTargetView* target = nullptr;
    ctx->OMGetRenderTargets(1, &target, nullptr);
    if (!target)
        return false;
    bool window = false;
    ID3D11Resource* resource = nullptr;
    target->GetResource(&resource);
    ID3D11Texture2D* texture = nullptr;
    if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)))) {
        D3D11_TEXTURE2D_DESC desc;
        texture->GetDesc(&desc);
        window = desc.Width == 2 * desc.Height;
        texture->Release();
    }
    if (resource)
        resource->Release();
    target->Release();
    return window;
}

ID3D11PixelShader* PixelShader(ID3D11DeviceContext* ctx) {
    ID3D11PixelShader* ps = nullptr;
    ctx->PSGetShader(&ps, nullptr, nullptr);
    if (ps)
        ps->Release();  // only its address is kept, to compare
    return ps;
}

bool ReadsScene(ID3D11DeviceContext* ctx, UINT eye) {
    ID3D11PixelShader* ps = PixelShader(ctx);
    if (!ps)
        return false;
    {
        std::lock_guard lock(g_shadersMutex);
        auto it = g_readsScene.find(ps);
        if (it != g_readsScene.end())
            return it->second;
    }
    bool scene = false;
    ID3D11ShaderResourceView* views[8] = {};
    ctx->PSGetShaderResources(0, 8, views);
    for (ID3D11ShaderResourceView* v : views) {
        if (!v)
            continue;
        ID3D11Resource* resource = nullptr;
        v->GetResource(&resource);
        ID3D11Texture2D* texture = nullptr;
        if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)))) {
            D3D11_TEXTURE2D_DESC desc;
            texture->GetDesc(&desc);
            for (UINT part = 1; part <= 4; part *= 2)
                scene = scene || (desc.Width == eye / part && desc.Height == eye / part);
            texture->Release();
        }
        if (resource)
            resource->Release();
        v->Release();
    }
    std::lock_guard lock(g_shadersMutex);
    g_readsScene[ps] = scene;
    return scene;
}

// A picture an eye wide to draw the right eye's things in, as they come out at the window's left
// edge, before they go into its right half.
ID3D11Texture2D* g_half = nullptr;
ID3D11RenderTargetView* g_halfView = nullptr;
UINT g_halfWidth = 0, g_halfHeight = 0;
DXGI_FORMAT g_halfFormat = DXGI_FORMAT_UNKNOWN;

bool HalfTarget(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& window) {
    const UINT width = window.Width / 2;
    if (g_half && g_halfWidth == width && g_halfHeight == window.Height && g_halfFormat == window.Format)
        return true;
    if (g_halfView)
        g_halfView->Release(), g_halfView = nullptr;
    if (g_half)
        g_half->Release(), g_half = nullptr;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = window.Height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = window.Format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &g_half)) ||
        FAILED(device->CreateRenderTargetView(g_half, nullptr, &g_halfView))) {
        if (g_half)
            g_half->Release(), g_half = nullptr;
        Log("Overlay: no %ux%u picture for the right eye", width, window.Height);
        return false;
    }
    g_halfWidth = width, g_halfHeight = window.Height, g_halfFormat = window.Format;
    return true;
}

// Draws into the window's right half what the draw puts at its left edge: that half into the half
// picture, the draw there (blending onto it as onto the window), and back. Things that read the
// scene by where on the window they are read it right only at the left edge. False when it can't
// (a depth or stencil test wants the window's own depth).
bool IntoRightHalf(ID3D11DeviceContext* ctx, const std::function<void()>& draw) {
    ID3D11DepthStencilState* depthState = nullptr;
    UINT ref = 0;
    ctx->OMGetDepthStencilState(&depthState, &ref);
    bool needsDepth = false;
    if (depthState) {
        D3D11_DEPTH_STENCIL_DESC d;
        depthState->GetDesc(&d);
        needsDepth = d.DepthEnable || d.StencilEnable;
        depthState->Release();
    }
    ID3D11RenderTargetView* target = nullptr;
    ID3D11DepthStencilView* depth = nullptr;
    ctx->OMGetRenderTargets(1, &target, &depth);
    ID3D11Resource* window = nullptr;
    ID3D11Texture2D* windowTexture = nullptr;
    ID3D11Device* device = nullptr;
    bool done = false;
    if (target && !(needsDepth && depth)) {
        target->GetResource(&window);
        if (window && SUCCEEDED(window->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&windowTexture)))) {
            D3D11_TEXTURE2D_DESC desc;
            windowTexture->GetDesc(&desc);
            ctx->GetDevice(&device);
            if (device && desc.SampleDesc.Count == 1 && desc.Width == 2 * desc.Height && HalfTarget(device, desc)) {
                const UINT half = desc.Width / 2;
                const D3D11_BOX right{half, 0, 0, desc.Width, desc.Height, 1};
                ctx->CopySubresourceRegion(g_half, 0, 0, 0, 0, windowTexture, 0, &right);
                ctx->OMSetRenderTargets(1, &g_halfView, nullptr);
                draw();
                ctx->OMSetRenderTargets(1, &target, depth);
                const D3D11_BOX back{0, 0, 0, half, desc.Height, 1};
                ctx->CopySubresourceRegion(windowTexture, 0, half, 0, 0, g_half, 0, &back);
                done = true;
            }
        }
    }
    if (device)
        device->Release();
    if (windowTexture)
        windowTexture->Release();
    if (window)
        window->Release();
    if (depth)
        depth->Release();
    if (target)
        target->Release();
    static std::atomic<bool> s_told{false};
    if (!done && !s_told.exchange(true))
        Log("Overlay: something for the right eye couldn't be drawn into its half (%s)",
            needsDepth ? "it tests depth or stencil" : "no window picture");
    return done;
}

// The draw again a half to the right (viewport and scissor), as a copy for the right eye.
void DrawMovedRight(ID3D11DeviceContext* ctx, const std::function<void()>& draw, const D3D11_VIEWPORT& viewport) {
    const float half = viewport.Height;  // an eye is as wide as the window is high
    D3D11_VIEWPORT moved = viewport;
    moved.TopLeftX += half;
    D3D11_RECT scissor[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE], shifted[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT scissors = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ctx->RSGetScissorRects(&scissors, scissor);
    for (UINT i = 0; i < scissors; ++i) {
        shifted[i] = scissor[i];
        shifted[i].left += static_cast<LONG>(half);
        shifted[i].right += static_cast<LONG>(half);
    }
    ctx->RSSetViewports(1, &moved);
    if (scissors)
        ctx->RSSetScissorRects(scissors, shifted);
    draw();
    ctx->RSSetViewports(1, &viewport);
    if (scissors)
        ctx->RSSetScissorRects(scissors, scissor);
}

}

namespace overlay {

void FinishFrame(IDXGISwapChain* swap) {
    if (!g_keyTarget)
        return;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    g_keyTarget->GetDevice(&device);
    if (device)
        device->GetImmediateContext(&ctx);
    if (ctx)
        TakeOffOrange(ctx, swap);
    else
        Drop(g_keyTarget), Drop(g_keyTexture);
    Drop(ctx);
    Drop(device);
}

void SetKeepOrange(bool keep) {
    if (g_keepOrange.exchange(keep) != keep)
        Log("Overlay: the enemies' orange markers %s", keep ? "shown (orangemarkers 1)" : "taken off");
}

void SetKeepCrosshair(bool keep) {
    if (g_keepCrosshair.exchange(keep) != keep)
        Log("Overlay: the game's crosshair %s", keep ? "shown (crosshair 1)" : "taken off");
}

void NewFrame() {
    g_eyePictures.store(0, std::memory_order_relaxed);
    g_hudIndex.store(0, std::memory_order_relaxed);
    static ULONGLONG lastLog = 0;
    if (const ULONGLONG now = GetTickCount64(); now - lastLog >= 1000) {
        lastLog = now;
        if (const uint32_t draws = g_hudDraws.exchange(0), copies = g_hudCopies.exchange(0); draws || copies)
            Log("Overlay: last second %u HUD draws in the left eye, %u copied into the right", draws, copies);
    }
    if (int asked = 1; !g_logFrame.compare_exchange_strong(asked, 2))
        g_logFrame.store(0);
    std::lock_guard lock(g_shadersMutex);
    g_eyeEffects.clear();
}

void LogNextFrame() {
    g_logFrame = 1;
}

void SetHudFix(int mode) {
    if (g_hudFix.exchange(mode) != mode)
        Log("Overlay: HUD experiment %d", mode);
}

void SetHudSkipTextures(const uint32_t (&sizes)[4]) {
    bool changed = false;
    for (int i = 0; i < 4; ++i)
        changed |= g_hudSkipTex[i].exchange(sizes[i]) != sizes[i];
    if (changed)
        Log("Overlay: HUD draws with textures %ux%u %ux%u left out (hudskiptex)", sizes[0] >> 16, sizes[0] & 0xFFFF,
            sizes[1] >> 16, sizes[1] & 0xFFFF);
}

void SetHudSkip(const uint64_t (&bits)[4]) {
    bool changed = false;
    for (int i = 0; i < kSkippable / 64; ++i)
        changed |= g_hudSkip[i].exchange(bits[i]) != bits[i];
    if (changed)
        Log("Overlay: HUD draws left out: %s", bits[0] | bits[1] | bits[2] | bits[3] ? "some (hudskip)" : "none");
}

bool Draw(ID3D11DeviceContext* ctx, UINT count, const std::function<void()>& draw, bool indexed, UINT start,
          INT base) {
    // Only with the eyes side by side, the right eye's view placed at the window's left edge like
    // the left eye's (views.cpp), and only small draws at that edge: an eye's frame there, or the
    // whole window's.
    if (count > kMostVertices || !views::RightEyeAtLeft())
        return false;
    D3D11_VIEWPORT viewport;
    UINT viewports = 1;
    ctx->RSGetViewports(&viewports, &viewport);
    if (viewports != 1 || viewport.TopLeftX != 0.0f || viewport.TopLeftY != 0.0f || viewport.Height < 1000.0f)
        return false;
    const bool window = viewport.Width == 2.0f * viewport.Height, eye = viewport.Width == viewport.Height;
    if (!window && !eye)
        return false;
    if (!Blended(ctx)) {
        // An eye's picture onto the window (unblended, over the whole window): the left eye's comes
        // out where it belongs; the right eye's comes out at the left edge too and goes into the
        // right half. (Placed at the right by the game, its screen effects worked from that place
        // against pictures only an eye wide: while suppressed, the whole right eye was smeared into
        // streaks, the user's right eye blinded: their video, 2026-10-30.)
        if (!window || g_eyePictures.fetch_add(1, std::memory_order_relaxed) != 1)
            return false;
        return IntoRightHalf(ctx, draw);
    }
    const int pictures = g_eyePictures.load(std::memory_order_relaxed);
    if (pictures == 1) {
        // Between the eyes' pictures: the first view's own effects onto the window (its damage
        // effect pass). The game keeps the player's damage for the second view, the right eye's
        // (the screen's blood came in its pass, not the first's): the first's is left out, the
        // second's goes into both eyes. Its shaders are noted to draw the second's alike.
        if (eye && OnWindow(ctx)) {
            if (ID3D11PixelShader* ps = PixelShader(ctx)) {
                std::lock_guard lock(g_shadersMutex);
                g_eyeEffects.push_back({ps, count});
            }
            return true;
        }
        return false;
    }
    if (pictures != 2)
        return false;
    // After the right eye's picture, all onto the window, the player's damage effects and the HUD,
    // into both eyes: as the game draws it (at the left, with the right eye's view placed there),
    // and for the right eye, a copy a half to the right, or for what reads the scene by where on the
    // window it is (the damage effect pass, a part of the HUD reading an eye's scene), into the
    // right half as it comes out at the left.
    bool eyeEffect = false;
    if (eye)
        if (ID3D11PixelShader* ps = PixelShader(ctx)) {
            std::lock_guard lock(g_shadersMutex);
            for (auto it = g_eyeEffects.begin(); it != g_eyeEffects.end(); ++it)
                if (it->ps == ps && it->count == count) {
                    eyeEffect = true;
                    g_eyeEffects.erase(it);
                    break;
                }
        }
    const int index = g_hudIndex.fetch_add(1, std::memory_order_relaxed);
    if (index < kSkippable && (g_hudSkip[index / 64].load(std::memory_order_relaxed) >> (index % 64) & 1)) {
        if (g_logFrame.load(std::memory_order_relaxed) == 2)
            Log("Overlay: HUD draw %d (%u) left out (hudskip)", index, count);
        return true;
    }
    if (g_hudSkipTex[0].load(std::memory_order_relaxed)) {
        const uint32_t size = FirstTextureSize(ctx);
        for (const auto& skip : g_hudSkipTex)
            if (size && skip.load(std::memory_order_relaxed) == size)
                return true;
    }
    // The game's own crosshair: four ticks round the middle of the view, each a 6-index quad on its
    // 128x64 compressed sheet (found 2026-10-31 in the headset's own pictures while the user was shot
    // at: faint against the sky, so hard to see in the mirror; the user's "crosshair when I take
    // damage"). In 30 frames nothing else of the HUD drew with a texture that size and kind.
    if (count == 6 && !g_keepCrosshair.load(std::memory_order_relaxed)) {
        DXGI_FORMAT format;
        if (FirstTextureSize(ctx, &format) == (128u << 16 | 64u) && format == DXGI_FORMAT_BC3_UNORM)
            return true;
    }
    const bool reads = ReadsScene(ctx, static_cast<UINT>(viewport.Height));
    // The second view's own pass over its whole picture: the depth of field, the suppression blur (a
    // window-wide quad reading its eye's blurred scene, PS checksum 2b2ca40a51aa1b4d). The first view drew its
    // own over the left eye between the pictures (the blur study: with all of this phase left out the left eye
    // is blurred, fine detail 0.02-0.12 against the sharp right eye's 1.7-33). Drawn at the left too, under fire
    // the left eye showed the right eye's blurred picture (correlation 0.98-1.00 at no shift: no depth): only
    // into the right half.
    const bool viewPass = window && reads && !eyeEffect;
    // The picture before the HUD, once a frame (the orange markers), after the blur and the damage passes:
    // taken before them, the blur's spreading of orange sparks and warm light counted as the HUD's orange
    // and the sharp picture went back round it, square patches in the blur (the user, 17:20).
    if (!eyeEffect && !viewPass)
        TakeBefore(ctx);
    // "hudfix": 1 the left eye's copy without the depth-stencil view, 2 no left copy, 3 no right copy.
    const int fix = g_hudFix.load(std::memory_order_relaxed);
    if (fix == 1)
        DrawWithoutDepth(ctx, draw);
    else if (fix != 2 && !viewPass)
        draw();
    const bool intoHalf = fix != 3 && (eyeEffect || reads) && IntoRightHalf(ctx, draw);
    if (!intoHalf && fix != 3)
        DrawMovedRight(ctx, draw, viewport);
    if (fix != 2)
        ++g_hudDraws;
    if (fix != 3)
        ++g_hudCopies;
    if (g_logFrame.load(std::memory_order_relaxed) == 2) {
        Log("Overlay: HUD draw %d: %u, %s viewport, %s%s, right eye's copy %s; PS %p", index, count,
            window ? "window" : "eye", eyeEffect ? "the eye effect" : "HUD", reads ? " reading the scene" : "",
            intoHalf ? "drawn into its half" : "moved right", static_cast<void*>(PixelShader(ctx)));
        LogHudConstants(ctx, index);
        if (indexed && count <= 12)
            LogHudVertices(ctx, index, count, start, base);
    }
    if (g_logged.fetch_add(1, std::memory_order_relaxed) == 0)
        Log("Overlay: the right eye's view is drawn as the left eye's and moved into its half; the HUD in both eyes");
    return true;
}

}
