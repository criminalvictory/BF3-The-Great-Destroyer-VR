#include "shaders.h"

#include <windows.h>
#include <d3d11.h>
#include <MinHook.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <unordered_map>

#include "log.h"

namespace {

using CreatePixelShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*,
                                                        ID3D11PixelShader**);
CreatePixelShaderFn g_origCreate = nullptr;

std::mutex g_mutex;
std::unordered_map<ID3D11PixelShader*, uint64_t> g_checksums;

// The lens flare's ghost: an aperture-shaped ring with a small one inside, drawn at the view's middle
// opposite the sun (found 2026-10-31 by hiding the flare's quads in turn with the sun in view, at three
// angles to it). Its other quads stay: the dirty lens's soft blobs below (c201dc354194ebac: the user
// likes those) and three that showed nothing. weapon.txt "flareps" adds more.
constexpr uint64_t kLensFlare[] = {0xb2c7fb2369dbfa58ull};
constexpr int kMostExtra = 8;
std::atomic<uint64_t> g_extra[kMostExtra] = {};
std::atomic<bool> g_keepFlare{false};

// Compiled shaders begin "DXBC" and a 16-byte checksum of the rest: its first 8 bytes do. Anything
// else (never seen) gets an FNV-1a hash of all of it.
uint64_t Checksum(const void* code, SIZE_T size) {
    const auto* p = static_cast<const uint8_t*>(code);
    uint64_t c = 0;
    if (size >= 20 && std::memcmp(p, "DXBC", 4) == 0) {
        std::memcpy(&c, p + 4, sizeof(c));
        return c ? c : 1;
    }
    c = 1469598103934665603ull;
    for (SIZE_T i = 0; i < size; ++i)
        c = (c ^ p[i]) * 1099511628211ull;
    return c ? c : 1;
}

// build\psdump.txt present at start: each pixel shader's bytecode is written to build\ps\<checksum>.dxbc as
// the game creates it (once each), to read what a post-process pass does (fxc /dumpbin): the suppression
// blur's square patches (2026-10-31).
bool g_dumpCode = false;
wchar_t g_dumpDir[MAX_PATH] = {};

void SaveCode(uint64_t checksum, const void* code, SIZE_T size) {
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s%016llx.dxbc", g_dumpDir, static_cast<unsigned long long>(checksum));
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") != 0 || !f)
        return;
    fwrite(code, 1, size, f);
    fclose(f);
}

HRESULT STDMETHODCALLTYPE HookedCreatePixelShader(ID3D11Device* device, const void* code, SIZE_T size,
                                                  ID3D11ClassLinkage* linkage, ID3D11PixelShader** out) {
    const HRESULT hr = g_origCreate(device, code, size, linkage, out);
    if (SUCCEEDED(hr) && out && *out && code && size) {
        const uint64_t c = Checksum(code, size);
        {
            std::lock_guard lock(g_mutex);
            g_checksums[*out] = c;  // a pointer the driver hands out again gets its new shader's
        }
        if (g_dumpCode)
            SaveCode(c, code, size);
    }
    return hr;
}

}  // namespace

namespace shaders {

bool InstallHook(void* createPixelShader) {
    {
        wchar_t path[MAX_PATH] = {};
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&InstallHook), &self);
        GetModuleFileNameW(self, path, MAX_PATH);
        if (wchar_t* slash = wcsrchr(path, L'\\')) {
            slash[1] = 0;
            wchar_t flag[MAX_PATH];
            swprintf_s(flag, L"%spsdump.txt", path);
            if (GetFileAttributesW(flag) != INVALID_FILE_ATTRIBUTES) {
                swprintf_s(g_dumpDir, L"%sps\\", path);
                CreateDirectoryW(g_dumpDir, nullptr);
                g_dumpCode = true;
                Log("Shaders: each pixel shader's bytecode is saved to build\\ps (psdump.txt)");
            }
        }
    }
    if (!createPixelShader ||
        MH_CreateHook(createPixelShader, &HookedCreatePixelShader, reinterpret_cast<void**>(&g_origCreate)) != MH_OK) {
        Log("Shaders: CreatePixelShader could not be hooked (the lens flare stays)");
        return false;
    }
    return true;
}

uint64_t ChecksumOf(ID3D11PixelShader* ps) {
    if (!ps)
        return 0;
    std::lock_guard lock(g_mutex);
    const auto it = g_checksums.find(ps);
    return it == g_checksums.end() ? 0 : it->second;
}

bool IsLensFlare(ID3D11PixelShader* ps) {
    if (g_keepFlare.load(std::memory_order_relaxed))
        return false;
    const uint64_t c = ChecksumOf(ps);
    if (!c)
        return false;
    for (uint64_t f : kLensFlare)
        if (f && f == c)
            return true;
    for (const auto& e : g_extra)
        if (const uint64_t f = e.load(std::memory_order_relaxed); f && f == c)
            return true;
    return false;
}

void SetLensFlare(bool keep, const uint64_t* extra, int extraCount) {
    const bool was = g_keepFlare.exchange(keep);
    bool changed = was != keep;
    for (int i = 0; i < kMostExtra; ++i) {
        const uint64_t v = i < extraCount && extra ? extra[i] : 0;
        changed |= g_extra[i].exchange(v) != v;
    }
    if (changed)
        Log("Shaders: the sun's lens flare %s%s", keep ? "kept (lensflare 1)" : "taken off",
            extraCount > 0 ? " (with weapon.txt's flareps)" : "");
}

}  // namespace shaders
