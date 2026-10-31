#include "gunshape.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "log.h"

namespace {

struct Field {
    int n[3] = {};
    float origin[3] = {}, cell = 0.0f;
    std::vector<int16_t> tenths;  // signed tenths of a millimetre, x fastest
};
Field g_field;
bool g_loaded = false;
std::once_flag g_load;

// The field from the file at path (g_loaded when it read).
void LoadFile(const wchar_t* path) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") || !f) {
        Log("Gun shape: guns\\m416.bin isn't there (tools/gunshape.exe makes it); the hand goes through the gun");
        return;
    }
    char magic[4] = {};
    int32_t version = 0;
    Field field;
    // Version 2: tenths of a millimetre in an int16 a cell; version 1 (whole millimetres in a byte)
    // was too coarse: the glove pushed out of the gun by it jittered.
    bool ok = fread(magic, 1, 4, f) == 4 && !std::memcmp(magic, "GSDF", 4) && fread(&version, 4, 1, f) == 1 &&
              version == 2 && fread(field.n, 4, 3, f) == 3 && fread(field.origin, 4, 3, f) == 3 &&
              fread(&field.cell, 4, 1, f) == 1 && field.cell > 0.0005f && field.n[0] > 1 && field.n[1] > 1 &&
              field.n[2] > 1 && field.n[0] < 2000 && field.n[1] < 2000 && field.n[2] < 2000;
    if (ok) {
        field.tenths.resize(static_cast<size_t>(field.n[0]) * field.n[1] * field.n[2]);
        ok = fread(field.tenths.data(), 2, field.tenths.size(), f) == field.tenths.size();
    }
    fclose(f);
    if (!ok) {
        Log("Gun shape: guns\\m416.bin can't be read (version %d; tools/gunshape.exe writes version 2)", version);
        return;
    }
    g_field = std::move(field);
    g_loaded = true;
    Log("Gun shape: the rifle's shape loaded, %d x %d x %d cells of %.0f mm", g_field.n[0], g_field.n[1], g_field.n[2],
        g_field.cell * 1000.0f);
}

// guns\m416.bin next to the module (the mod's DLL).
void LoadBesideModule() {
    wchar_t path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&LoadBesideModule), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\'))
        *(slash + 1) = 0;
    wcscat_s(path, L"guns\\m416.bin");
    LoadFile(path);
}

// The field at p by trilinear interpolation (metres); false off the grid.
bool Sample(const float* p, float& out) {
    float f[3];
    int i[3];
    for (int k = 0; k < 3; ++k) {
        const float c = (p[k] - g_field.origin[k]) / g_field.cell;
        i[k] = static_cast<int>(std::floor(c));
        f[k] = c - static_cast<float>(i[k]);
        if (i[k] < 0 || i[k] >= g_field.n[k] - 1)
            return false;
    }
    auto at = [&](int x, int y, int z) {
        return static_cast<float>(g_field.tenths[(static_cast<size_t>(z) * g_field.n[1] + y) * g_field.n[0] + x]);
    };
    float v = 0.0f;
    for (int dz = 0; dz < 2; ++dz)
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                const float w = (dx ? f[0] : 1 - f[0]) * (dy ? f[1] : 1 - f[1]) * (dz ? f[2] : 1 - f[2]);
                v += w * at(i[0] + dx, i[1] + dy, i[2] + dz);
            }
    out = v * 0.0001f;
    return true;
}

}

namespace gunshape {

bool Load(const wchar_t* path) {
    std::call_once(g_load, [path] { LoadFile(path); });
    return g_loaded;
}

bool Distance(const float* p, float& distance) {
    std::call_once(g_load, LoadBesideModule);
    return g_loaded && Sample(p, distance);
}

bool Distance(const float* p, float& distance, float* normal) {
    std::call_once(g_load, LoadBesideModule);
    if (!g_loaded || !Sample(p, distance))
        return false;
    // The way out: the field's slope over half a cell each way.
    const float h = g_field.cell * 0.5f;
    float slope[3];
    for (int k = 0; k < 3; ++k) {
        float a[3] = {p[0], p[1], p[2]}, b[3] = {p[0], p[1], p[2]};
        a[k] += h, b[k] -= h;
        float va, vb;
        if (!Sample(a, va) || !Sample(b, vb))
            return false;
        slope[k] = va - vb;
    }
    const float length = std::sqrt(slope[0] * slope[0] + slope[1] * slope[1] + slope[2] * slope[2]);
    if (length < 1e-6f)
        return false;
    for (int k = 0; k < 3; ++k)
        normal[k] = slope[k] / length;
    return true;
}

}
