#include "views.h"

#include <windows.h>
#include <MinHook.h>

#include <atomic>
#include <cmath>
#include <cstring>

#include "camera.h"
#include "input.h"
#include "log.h"

namespace {

// The function that builds the frame's view descriptions (bf3.exe 1.6, not relocated). It takes
// the game's camera state, and fills a count and an array of descriptions 0x1100 bytes each: a
// name pointer at +4, the viewport at +0x10, then three camera blocks of 0x480 bytes, each starting
// with its transform: the main view +0x30, the one BF3 culls with +0x4B0, the first person +0x930.
// Within a camera block: FOV pair +0x48, aspect +0x58. The first-person views the player's own arms,
// gun and legs are drawn with aren't made from that block (both eyes' come from one camera, the
// weapon camera's; turned, the body still moved with the head): skin.cpp turns those as they're uploaded.
constexpr uintptr_t kBuildViews = 0x01018E70;
constexpr uint8_t kBuildViewsStart[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x81, 0xEC, 0x74, 0x0E, 0x00, 0x00};
constexpr size_t kViewSize = 0x1100;
constexpr size_t kMainCamera = 0x30, kCullCamera = 0x4B0, kFirstPersonCamera = 0x930, kCameraSize = 0x480;
constexpr size_t kFovPair = 0x48, kAspect = 0x58;
constexpr size_t kViewport = 0x10;  // x, y, width, height (int32 each)

// GameRenderSettings, as the view builder reads it: [[bf3.exe+1F84D78]+0x3C], recognised by its
// vtable; SplitScreenTestViewCount at +0x40 (BF3's type tables).
constexpr uintptr_t kSettingsOwner = 0x02384D78;
constexpr uintptr_t kSettingsVtable = 0x02138E50;
constexpr size_t kViewCount = 0x40;

using BuildViewsFn = void(__fastcall*)(void* self, void* edx, void* source, uint32_t* count, uint8_t* views);
BuildViewsFn g_buildViews = nullptr;
bool g_hooked = false;
std::atomic<bool> g_want{false}, g_active{false};
std::atomic<float> g_offset[2] = {0.0f, 0.0f};
std::atomic<float> g_fov{0.0f};  // what the eyes were last built with
std::atomic<int32_t> g_rightAt{0};  // where the game put the second view (x), before MakeEyes moved it

bool LooksLikeFov(float v) { return v > 0.2f && v < 2.8f; }

void WriteFov(uint8_t* camera, float fov) {
    auto f = reinterpret_cast<float*>(camera + kFovPair);
    if (fov > 0.0f && LooksLikeFov(f[0]) && LooksLikeFov(f[1]))
        f[0] = f[1] = fov;
}

// The two views made into the eyes: both start from the first view's cameras (the game's own),
// are turned to the head, and are shifted half the eye distance each way along the head's right.
// The first-person camera is turned only with a scripted main camera (a cutscene's or a ride's), by
// the same turn (camera.h TurnToHead), which also goes to skin.cpp: the views the player's own arms,
// gun and legs are drawn with are turned by it as they're uploaded (the user, 2026-11-01: "Only the
// head moves"). The first-person camera isn't what they're made from, but it's kept in step with them,
// as the cull camera is with the main one. In ordinary play the mod places the gun and arms itself.
// The second is also put at the window's left edge, where the first is (overlay.cpp moves its
// finished picture into the right half): placed at the right, its screen effects worked from that
// place against pictures only an eye wide, and while suppressed the whole right eye was smeared
// into streaks (the user's video; caught in both-eye pictures, 2026-10-30).
void MakeEyes(uint8_t* views) {
    uint8_t* eye[2] = {views, views + kViewSize};
    auto* place = reinterpret_cast<int32_t*>(eye[1] + kViewport);
    if (place[0] != 0) {
        g_rightAt.store(place[0], std::memory_order_relaxed);
        place[0] = 0;
    }
    const float* game = reinterpret_cast<const float*>(eye[0] + kMainCamera);
    input::NoteTurnStep(0, std::atan2(game[8], game[10]));  // the stick turn's evenness, as built
    // The second view is the split-screen test's: the first one's camera turned round.
    std::memcpy(eye[1] + kMainCamera, eye[0] + kMainCamera, kCameraSize);
    std::memcpy(eye[1] + kCullCamera, eye[0] + kCullCamera, kCameraSize);
    std::memcpy(eye[1] + kFirstPersonCamera, eye[0] + kFirstPersonCamera, kCameraSize);
    const float fov = camera::WantedFov();
    camera::BeginViews();  // one turn for both eyes and their cull cameras
    for (int e = 0; e < 2; ++e) {
        auto main = reinterpret_cast<float*>(eye[e] + kMainCamera);
        auto cull = reinterpret_cast<float*>(eye[e] + kCullCamera);
        auto firstPerson = reinterpret_cast<float*>(eye[e] + kFirstPersonCamera);
        camera::TurnToHead(main, firstPerson);
        camera::TurnToHead(cull);
        // The eye's place: along the head's right (the turned main camera's first row).
        const float offset = g_offset[e].load(std::memory_order_relaxed);
        for (float* m : {main, cull, firstPerson})
            for (int i = 0; i < 3; ++i)
                m[12 + i] += main[i] * offset;
        for (float* m : {main, cull, firstPerson})
            WriteFov(reinterpret_cast<uint8_t*>(m), fov);
        // The first-person camera keeps the whole picture's aspect; its half is the main view's.
        reinterpret_cast<float*>(eye[e] + kFirstPersonCamera + kAspect)[0] =
            reinterpret_cast<float*>(eye[e] + kMainCamera + kAspect)[0];
    }
    // Read back: with two views the pointer chain to the render camera leads nowhere, and the
    // headset was told the game's own 55 degrees for a 100-degree picture (a small square, user).
    float drawn = reinterpret_cast<float*>(eye[0] + kMainCamera + kFovPair)[0];
    g_fov.store(LooksLikeFov(drawn) ? drawn : 0.0f, std::memory_order_relaxed);
}

// weapon.txt "viewdiff N": for N seconds, the two views' descriptions compared outside their
// camera blocks (which MakeEyes makes alike), logged when what differs changes (at most 4 times a
// second). The suppression blur stayed in the right eye with the HUD in both (2026-10-30): if the
// game keeps the screen's effects in a view's description, they differ here while suppressed.
std::atomic<ULONGLONG> g_diffUntil{0};

void LogDifferences(const uint8_t* views) {
    if (GetTickCount64() > g_diffUntil.load(std::memory_order_relaxed))
        return;
    static ULONGLONG lastLog = 0;
    static uint32_t last[kViewSize / 4][2] = {};
    static bool lastDiffers[kViewSize / 4] = {};
    if (GetTickCount64() - lastLog < 250)
        return;
    const auto* a = reinterpret_cast<const uint32_t*>(views);
    const auto* b = reinterpret_cast<const uint32_t*>(views + kViewSize);
    char line[900];
    int used = 0;
    bool changed = false;
    for (size_t i = 0; i < kViewSize / 4; ++i) {
        const size_t at = i * 4;
        if (at >= kMainCamera && at < kFirstPersonCamera + kCameraSize)
            continue;
        const bool differs = a[i] != b[i];
        if (differs != lastDiffers[i] || (differs && (a[i] != last[i][0] || b[i] != last[i][1])))
            changed = true;
        lastDiffers[i] = differs, last[i][0] = a[i], last[i][1] = b[i];
        if (differs && used < 820) {
            float fa, fb;
            std::memcpy(&fa, &a[i], 4), std::memcpy(&fb, &b[i], 4);
            used += snprintf(line + used, sizeof(line) - used, " +%03zX %08X|%08X (%g|%g)", at, a[i], b[i], fa, fb);
        }
    }
    if (!changed)
        return;
    lastLog = GetTickCount64();
    Log("Views: the eyes' descriptions differ at%s", used ? line : " nothing");
}

void __fastcall BuildViewsHook(void* self, void* edx, void* source, uint32_t* count, uint8_t* views) {
    g_buildViews(self, edx, source, count, views);
    bool eyes = g_want.load(std::memory_order_relaxed) && count && *count == 2 && views;
    if (eyes) {
        LogDifferences(views);
        MakeEyes(views);
    }
    // The renderer copies these views next; the camera breakpoints there leave them alone now.
    camera::SetViewsMode(eyes);
    g_active.store(eyes, std::memory_order_relaxed);
}

// The view builder hooked once its code is there (bf3.exe decrypts itself at start-up).
void Hook() {
    uint8_t start[sizeof(kBuildViewsStart)];
    if (!camera::SafeRead(kBuildViews, start, sizeof(start)) ||
        std::memcmp(start, kBuildViewsStart, sizeof(start)) != 0)
        return;
    void* target = reinterpret_cast<void*>(kBuildViews);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&BuildViewsHook), reinterpret_cast<void**>(&g_buildViews)) !=
            MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        Log("Views: could not hook the view builder");
        g_hooked = true;  // don't retry every frame
        g_buildViews = nullptr;
        return;
    }
    g_hooked = true;
    Log("Views: view builder hooked");
}

// Sets GameRenderSettings.SplitScreenTestViewCount, only where that object really is.
void SetViewCount(uint32_t count) {
    uintptr_t owner = 0, settings = 0, vtable = 0;
    if (!camera::SafeRead(kSettingsOwner, &owner, sizeof(owner)) || !owner ||
        !camera::SafeRead(owner + 0x3C, &settings, sizeof(settings)) || !settings ||
        !camera::SafeRead(settings, &vtable, sizeof(vtable)) || vtable != kSettingsVtable)
        return;
    auto field = reinterpret_cast<volatile uint32_t*>(settings + kViewCount);
    if (*field != count) {
        *field = count;
        Log("Views: %u view%s (GameRenderSettings at %08X)", count, count == 1 ? "" : "s",
            static_cast<unsigned>(settings));
    }
}

}

namespace views {

void Update(bool want) {
    if (!g_hooked)
        Hook();
    want = want && g_buildViews;
    g_want.store(want, std::memory_order_relaxed);
    static ULONGLONG lastSet = 0;
    static bool lastWant = false;
    ULONGLONG now = GetTickCount64();
    if (want != lastWant || now - lastSet >= 1000) {  // settings are rebuilt on level loads
        lastSet = now;
        lastWant = want;
        SetViewCount(want ? 2 : 1);
    }
    if (!want) {
        g_active.store(false, std::memory_order_relaxed);
        camera::SetViewsMode(false);
    }
}

bool Active() { return g_active.load(std::memory_order_relaxed); }

void SetEyeOffsets(float left, float right) {
    g_offset[0].store(left, std::memory_order_relaxed);
    g_offset[1].store(right, std::memory_order_relaxed);
}

float EyeOffset(int eye) { return eye == 0 || eye == 1 ? g_offset[eye].load(std::memory_order_relaxed) : 0.0f; }

float RenderedFov() { return Active() ? g_fov.load(std::memory_order_relaxed) : 0.0f; }

bool RightEyeAtLeft() { return Active() && g_rightAt.load(std::memory_order_relaxed) != 0; }

void WatchDifferences(int seconds) {
    g_diffUntil.store(GetTickCount64() + 1000ull * seconds, std::memory_order_relaxed);
    Log("Views: comparing the eyes' descriptions for %d s", seconds);
}

}
