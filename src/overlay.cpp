#include "overlay.h"

#include <windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "log.h"
#include "views.h"

namespace {

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

void NewFrame() {
    g_eyePictures.store(0, std::memory_order_relaxed);
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

bool Draw(ID3D11DeviceContext* ctx, UINT count, const std::function<void()>& draw) {
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
    // "hudfix": 1 the left eye's copy without the depth-stencil view, 2 no left copy, 3 no right copy.
    const int fix = g_hudFix.load(std::memory_order_relaxed);
    if (fix == 1)
        DrawWithoutDepth(ctx, draw);
    else if (fix != 2)
        draw();
    const bool reads = ReadsScene(ctx, static_cast<UINT>(viewport.Height));
    const bool intoHalf = fix != 3 && (eyeEffect || reads) && IntoRightHalf(ctx, draw);
    if (!intoHalf && fix != 3)
        DrawMovedRight(ctx, draw, viewport);
    if (g_logFrame.load(std::memory_order_relaxed) == 2)
        Log("Overlay: %u, %s viewport, %s%s, right eye's copy %s", count, window ? "window" : "eye",
            eyeEffect ? "the eye effect" : "HUD", reads ? " reading the scene" : "",
            intoHalf ? "drawn into its half" : "moved right");
    if (g_logged.fetch_add(1, std::memory_order_relaxed) == 0)
        Log("Overlay: the right eye's view is drawn as the left eye's and moved into its half; the HUD in both eyes");
    return true;
}

}
