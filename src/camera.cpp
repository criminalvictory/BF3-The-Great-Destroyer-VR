// Camera access. Reading follows the pointer chains from bf3vr.ini; writing uses hardware
// breakpoints, because BF3 rewrites the camera every frame and anything written at the wrong
// moment is lost. A breakpoint fires right after the game writes a camera's height, and the
// handler shifts X and Z along that camera's right vector by the eye offset before anything
// renders from it. The camera has no roll, so the right vector has no height component and the
// handler never writes the watched float itself (which would trigger it again).
// With a controller aiming, the render cameras are also turned to the head, and the weapon
// camera the opposite way so the gun stays with the hand. Aiming itself: see aim.cpp.

#include "camera.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

#include "log.h"

namespace {

constexpr float kPi = 3.14159265f;
constexpr uintptr_t kWatchOffset = 0x34;  // translation.y
constexpr int kSlots = 4;                 // x86 debug registers Dr0..Dr3
constexpr int kMaxWriters = 8;

// The last debug register is for the weapon camera (the copy the gun is drawn from) while the
// view is turned; the others hold render cameras, which get the eye shift.
constexpr int kWeaponSlot = kSlots - 1;

// Slot 2 holds the cull camera (the copy BF3 decides what to draw from), or while hunting for
// it, the candidate being tried. It gets the same head turn as the render cameras in slots 0-1.
constexpr int kCullSlot = 2;

// Transform start per debug register, 0 = unused, and where in its block the breakpoint sits
// (relative to the transform). The exception handler reads these.
std::atomic<uintptr_t> g_slots[kSlots];
std::atomic<uintptr_t> g_slotOffsets[kSlots];
std::atomic<float> g_eyeOffset{0.0f};
std::atomic<bool> g_viewsMode{false};  // views.cpp makes the render and cull cameras itself
std::atomic<uintptr_t> g_weaponAddress{0};
// The weapon camera's yaw as the game last wrote it (atan2 of its back row's x and -z, the way the
// look angles store theirs), before the mod turns it, and when (GetTickCount64).
std::atomic<float> g_weaponYaw{0.0f}, g_weaponPitch{0.0f};  // pitch: -asin of the back row's y, as stored
std::atomic<ULONGLONG> g_weaponYawAt{0};
std::atomic<bool> g_gunFlip{false};
// weapon.txt "wcam x y z" (SetWeaponTest): the camera the gun is placed from moved along its own
// axes, to see what follows it: the gun, its muzzle flash, the casings, the eye.
std::atomic<bool> g_weaponTest{false};
std::atomic<float> g_weaponTestOffset[3];
std::atomic<uint32_t> g_hits{0};
std::atomic<uintptr_t> g_writers[kMaxWriters];
int g_writersLogged = 0;

// ---- Render FOV ----
// BF3's render view (the object the render cameras live in: transform at +0x30, FOV in radians
// at +0x78 and +0x7C, near and far at +0x80, aspect at +0x88, projection matrix at +0x2F0) is
// rebuilt every frame. Its FOV comes from the game camera's view at bf3.exe+1F8B480 (same
// layout), itself rewritten every frame: a write test found that changing bf3.exe+1F8B4F8 changes
// the render view's FOV. The campaign renders 55 degrees whatever the menu says.
constexpr uintptr_t kFovSource = 0x1F8B4F8;    // bf3.exe offset
constexpr uintptr_t kFovFromTransform = 0x48;  // a view's FOV pair, relative to its transform
constexpr uintptr_t kProjectionFromTransform = 0x2C0;
std::atomic<float> g_renderFov{0.0f};          // radians, 0 = the game's own
uintptr_t g_fovSource = 0;                     // absolute, set by StartWatch
// A breakpoint on the source never fired (its writer may be a thread hidden from debug
// registers), and the source code is wrapped on disk. The render and cull views, though, are
// filled by one block copy each frame, transform first, FOV after. With the wider view on, their
// breakpoints watch the near plane just past the FOV instead of the height, so they fire once
// both are in place: the turn, eye shift and FOV are all written after the copy.
constexpr uintptr_t kNearFromTransform = 0x50;
std::atomic<uint32_t> g_slotHits[kSlots];
// Main render camera writes since xr_runtime last asked: one per game frame, if every frame
// shown is built exactly once between two Presents (which the alternate-eye scheme assumes).
std::atomic<uint32_t> g_cameraBuilds{0};
// The render cameras' object holds three such views: the main one (+0x0, its transform is render
// camera 1 at +0x30), the cull view (+0x480) and the first-person view (+0x900), which draws the
// gun and arms. Widening only the main view left the gun at 55 degrees: shown life-size, about
// twice too big. Its FOV pair is written from every camera breakpoint, so whichever fires after
// the game has filled that view in makes it stick. That changed the view's own matrices but not
// the gun, which is drawn from a 55-degree projection made elsewhere; skin.cpp rescales that one
// where it is uploaded to the graphics card.
constexpr uintptr_t kMainTransform = 0x30;
constexpr uintptr_t kFirstPersonView = 0x900;
// The block holding a render view's transform, relative to the transform.
constexpr uintptr_t kViewFromTransform = 0x30;

// Where in a slot's block the breakpoint sits, relative to the transform.
uintptr_t WatchOffset(int slot, bool fov) {
    return fov && (slot == 0 || slot == kCullSlot) ? kNearFromTransform : kWatchOffset;
}

bool LooksLikeFov(float v) {
    return v > 0.2f && v < 2.8f;  // radians; also rejects NaN
}

// A view's FOV pair, only where it looks like one.
void WriteFov(uintptr_t address, float fov) {
    auto f = reinterpret_cast<float*>(address);
    if (LooksLikeFov(f[0]) && LooksLikeFov(f[1]))
        f[0] = f[1] = fov;
}

// View turn for the handler: written into the inactive buffer, then published by flipping the index.
struct ViewTurnState {
    bool active;
    camera::ViewTurn turn;
};
ViewTurnState g_view[2] = {};
std::atomic<int> g_viewIndex{0};
std::atomic<ULONGLONG> g_turnLogUntil{0};  // weapon.txt "viewlog N" (TurnToHead)
std::atomic<bool> g_mirrorFix{true};       // weapon.txt "mirrorfix 0": a mirrored camera turned as any (TurnToHead)
std::atomic<int> g_turned{0};              // cameras turned to the head since TakeTurned
std::atomic<uint32_t> g_refusedInactive{0}, g_refusedNotCamera{0};  // TurnToHead's refusals (TakeTurned logs them)
float g_refusedMatrix[16] = {};
// views.cpp's cameras of one frame (both eyes and their cull cameras) turned with one published turn (BeginViews,
// the view builder's thread), and what the first of them showed (ShownTurn): its yaw as built (low half) and the
// yaw taken out of it (high half), two floats in one word so the render thread reads them together.
ViewTurnState g_frameView = {};
bool g_frameLatched = false, g_frameFirst = false;
std::atomic<uint64_t> g_shown{0};
std::atomic<ULONGLONG> g_shownAt{0};  // when it was (GetTickCount64; 0 never): older than kShownFresh, stale
constexpr ULONGLONG kShownFresh = 250;
// A camera the mod had already turned, offered again in a frame's views (TurnToHead): its yaw the last frame's
// shown yaw within kRepeatSame after a jump of more than kRepeatJump, its pitch the last shown pitch within
// kRepeatPitch (the head's, baked in: a game camera whose yaw lands there by chance is still turned). The last
// frame's first camera as built and as shown (radians), and the same latched for this frame by BeginViews, so
// both eyes' copies are judged alike. The view builder's thread only.
constexpr float kRepeatSame = 0.1f / 57.29578f, kRepeatJump = 5.0f / 57.29578f, kRepeatPitch = 0.5f / 57.29578f;
float g_lastCameraYaw = 0.0f, g_lastShownYaw = 0.0f, g_lastShownPitch = 0.0f;
float g_refCameraYaw = 0.0f, g_refShownYaw = 0.0f, g_refShownPitch = 0.0f;
bool g_haveLast = false, g_haveRef = false;
uint32_t g_repeats = 0;  // left as they were since the last log

// Camera copies the finder (F10) saw this session, which the F6 hunt tries one at a time.
std::mutex g_testMutex;
std::vector<uintptr_t> g_testCopies;
std::atomic<int> g_huntIndex{-1};  // -1 = not hunting
ULONGLONG g_huntStepTime = 0;
constexpr ULONGLONG kHuntStepMs = 3000;

// Beep without holding up the render thread it's called from.
void BeepAsync(DWORD frequency, DWORD duration) {
    DWORD packed = (frequency << 16) | duration;
    if (HANDLE t = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
            DWORD v = static_cast<DWORD>(reinterpret_cast<uintptr_t>(p));
            Beep(v >> 16, v & 0xFFFF);
            return 0;
        }, reinterpret_cast<LPVOID>(static_cast<uintptr_t>(packed)), 0, nullptr))
        CloseHandle(t);
}

uintptr_t DebugRegister(const CONTEXT* c, int i) {
    switch (i) {
    case 0: return c->Dr0;
    case 1: return c->Dr1;
    case 2: return c->Dr2;
    default: return c->Dr3;
    }
}

void SetDebugRegister(CONTEXT* c, int i, uintptr_t value) {
    switch (i) {
    case 0: c->Dr0 = value; break;
    case 1: c->Dr1 = value; break;
    case 2: c->Dr2 = value; break;
    default: c->Dr3 = value; break;
    }
}

// Dr7 bits for breakpoint i: local enable, and its RW/LEN field.
constexpr uintptr_t EnableBits(int i) {
    return (static_cast<uintptr_t>(3) << (2 * i)) | (static_cast<uintptr_t>(0xF) << (16 + 4 * i));
}

void RecordWriter(uintptr_t ip) {
    for (auto& slot : g_writers) {
        uintptr_t current = slot.load(std::memory_order_relaxed);
        if (current == ip)
            return;
        if (current == 0 && slot.compare_exchange_strong(current, ip))
            return;
    }
}

// A LinearTransform's three direction rows are unit length with a zero fourth component;
// arbitrary data almost never is.
bool IsCamera(const float* m) {
    if (m[3] != 0.0f || m[7] != 0.0f || m[11] != 0.0f)
        return false;
    for (int row = 0; row < 3; ++row) {
        const float* v = m + 4 * row;
        float len = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        if (!(std::fabs(len - 1.0f) < 0.05f))  // also rejects NaN
            return false;
    }
    return true;
}

// Quaternions are (x, y, z, w), in OpenXR's axes: x right, y up, -z forward.
void QuatMul(const float* a, const float* b, float* out) {
    out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

void QuatRotate(const float* q, const float* v, float* out) {
    float t[3] = {2 * (q[1] * v[2] - q[2] * v[1]), 2 * (q[2] * v[0] - q[0] * v[2]), 2 * (q[0] * v[1] - q[1] * v[0])};
    out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

// BF3's camera rows are right (row0), up (row1) and backwards (row2): the true view direction
// is -row2. That's a left-handed basis, a mirror image of OpenXR's right-handed axes; mapping
// through it keeps turning, up/down and tilt all the right way round (confirmed in game: with
// row0 taken as left, both turning and tilt came out mirrored).

// The turn, in the camera's own frame, from where the hand aims to where the head looks.
// The game camera is body yaw * controller yaw * game pitch (the look angles follow the
// controller), so removing the controller yaw and the game's own pitch and applying the head
// leaves body yaw * head. taken: the yaw it removed (the picture shows the camera's + taken - head).
void ViewTurnLocal(const float* m, const camera::ViewTurn& turn, float* local, float* taken = nullptr) {
    float pitch = std::asin(std::clamp(-m[9], -1.0f, 1.0f));  // true forward = -row2
    float yaw = turn.controllerYaw;
    if (turn.haveLook) {
        // The look yaw this camera was built from, by BF3's own formula (the angle finder found
        // atan2(x, -z) of row 2), against the one written this frame: the difference is how far
        // the hand has turned since, so the hand's turn in this camera needs no lag guess. A
        // fixed frame lag (view_lag) was a frame too short or too long at a lower frame rate:
        // the picture swayed against or with the hand.
        float behind = std::atan2(m[8], -m[10]) - turn.lookYaw;
        if (behind > kPi)
            behind -= 2 * kPi;
        if (behind < -kPi)
            behind += 2 * kPi;
        yaw = turn.currentYaw + turn.lookSign * behind;
    }
    if (taken)
        *taken = yaw;
    float qYaw[4] = {0.0f, std::sin(yaw / 2), 0.0f, std::cos(yaw / 2)};
    float qPitch[4] = {std::sin(pitch / 2), 0.0f, 0.0f, std::cos(pitch / 2)};
    float aim[4];
    QuatMul(qYaw, qPitch, aim);
    float aimInverse[4] = {-aim[0], -aim[1], -aim[2], aim[3]};
    QuatMul(aimInverse, turn.head, local);
}

// Rotates camera m by local, a quaternion in its own (OpenXR-style) frame.
void TurnLocal(float* m, const float* local) {
    float right[3] = {m[0], m[1], m[2]};
    float up[3] = {m[4], m[5], m[6]};
    float forward[3] = {-m[8], -m[9], -m[10]};

    // Local OpenXR axes onto the game camera's true axes.
    auto toWorld = [&](const float* v, float* out) {
        for (int i = 0; i < 3; ++i)
            out[i] = v[0] * right[i] + v[1] * up[i] - v[2] * forward[i];
    };
    const float x[3] = {1, 0, 0}, y[3] = {0, 1, 0}, minusZ[3] = {0, 0, -1};
    float axis[3], newRight[3], newUp[3], newForward[3];
    QuatRotate(local, x, axis);
    toWorld(axis, newRight);
    QuatRotate(local, y, axis);
    toWorld(axis, newUp);
    QuatRotate(local, minusZ, axis);
    toWorld(axis, newForward);

    for (int i = 0; i < 3; ++i) {
        m[i] = newRight[i];
        m[4 + i] = newUp[i];
        m[8 + i] = -newForward[i];
    }
}

// The handler wrote a watched float itself (a camera's translation y, where the breakpoint sits):
// the breakpoint fires again inside it. Writing it unguarded recursed until the stack overflowed
// (the weapon camera test, 2026-10-30), so that write is let through.
thread_local bool t_inHandler = false;

// Runs on the game thread that just wrote a camera. Must stay tiny: no locks, no logging.
LONG CALLBACK OnException(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = info->ContextRecord;
    if (!(c->Dr6 & 0xF))
        return EXCEPTION_CONTINUE_SEARCH;
    if (t_inHandler) {
        c->Dr6 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    t_inHandler = true;

    float offset = g_eyeOffset.load(std::memory_order_relaxed);
    for (int i = 0; i < kSlots; ++i) {
        if (!(c->Dr6 & (1u << i)))
            continue;
        uintptr_t watched = g_slots[i].load(std::memory_order_relaxed);
        uintptr_t watchOffset = g_slotOffsets[i].load(std::memory_order_relaxed);
        if (!watched || DebugRegister(c, i) != watched + watchOffset) {
            // Left over from a camera address that is no longer current: disarm it in this thread.
            c->Dr7 &= ~EnableBits(i);
            continue;
        }
#ifdef _WIN64
        RecordWriter(c->Rip);
#else
        RecordWriter(c->Eip);
#endif
        g_hits.fetch_add(1, std::memory_order_relaxed);
        g_slotHits[i].fetch_add(1, std::memory_order_relaxed);
        auto m = reinterpret_cast<float*>(watched);
        // Only touch memory that holds a camera right now: BF3 reuses these blocks for other
        // data, and shifting anything else corrupts it.
        if (!IsCamera(m))
            continue;
        if (i == 0)
            g_cameraBuilds.fetch_add(1, std::memory_order_relaxed);
        if (i != kWeaponSlot && g_viewsMode.load(std::memory_order_relaxed))
            continue;  // already turned, shifted and widened where the views were built
        // Wider FOV: the render view's and the cull view's own pair, once the copy has put the
        // game's there (the breakpoint then sits past it, on the near plane), and the
        // first-person view's.
        if (float fov = g_renderFov.load(std::memory_order_relaxed); fov > 0.0f && i != kWeaponSlot) {
            if (watchOffset == kNearFromTransform)
                WriteFov(watched + kFovFromTransform, fov);
            if (uintptr_t main = g_slots[0].load(std::memory_order_relaxed)) {
                uintptr_t firstPerson = main - kMainTransform + kFirstPersonView + kViewFromTransform;
                if (IsCamera(reinterpret_cast<const float*>(firstPerson)))
                    WriteFov(firstPerson + kFovFromTransform, fov);
            }
        }
        const ViewTurnState& view = g_view[g_viewIndex.load(std::memory_order_acquire)];
        float local[4];
        if (view.active)
            ViewTurnLocal(m, view.turn, local);
        if (watched == g_weaponAddress.load(std::memory_order_relaxed)) {
            // As the game wrote it, built from the real look angles (aim.cpp tells them by it).
            g_weaponYaw.store(std::atan2(m[8], -m[10]), std::memory_order_relaxed);
            g_weaponPitch.store(-std::asin(std::clamp(m[9], -1.0f, 1.0f)), std::memory_order_relaxed);
            g_weaponYawAt.store(GetTickCount64(), std::memory_order_relaxed);
            // BF3 places the gun from the weapon camera relative to the picture's camera, so
            // turning the weapon camera the opposite way keeps the gun with the hand. Which way
            // is "opposite" is confirmed in game (F1 flips it).
            if (view.active) {
                if (!g_gunFlip.load(std::memory_order_relaxed))
                    local[0] = -local[0], local[1] = -local[1], local[2] = -local[2];
                TurnLocal(m, local);
            }
            if (g_weaponTest.load(std::memory_order_relaxed)) {
                const float o[3] = {g_weaponTestOffset[0].load(std::memory_order_relaxed),
                                    g_weaponTestOffset[1].load(std::memory_order_relaxed),
                                    g_weaponTestOffset[2].load(std::memory_order_relaxed)};
                for (int k = 0; k < 3; ++k)
                    m[12 + k] += m[k] * o[0] + m[4 + k] * o[1] + m[8 + k] * o[2];
            }
            continue;  // the weapon camera never gets the eye shift
        }
        if (view.active)
            TurnLocal(m, local);
        if (offset != 0.0f) {
            m[12] += m[0] * offset;
            m[14] += m[2] * offset;
        }
    }
    t_inHandler = false;
    c->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Data breakpoints after any write to the 4 bytes at each target plus its offset (0 = slot unused).
bool ArmThread(DWORD threadId, const uintptr_t (&targets)[kSlots], const uintptr_t (&offsets)[kSlots]) {
    HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, threadId);
    if (!thread)
        return false;
    bool ok = false;
    if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
        CONTEXT c{};
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(thread, &c)) {
            for (int i = 0; i < kSlots; ++i) {
                c.Dr7 &= ~EnableBits(i);
                SetDebugRegister(&c, i, targets[i] ? targets[i] + offsets[i] : 0);
                if (targets[i])  // L_i, RW_i = write, LEN_i = 4 bytes
                    c.Dr7 |= (static_cast<uintptr_t>(1) << (2 * i)) | (static_cast<uintptr_t>(1) << (16 + 4 * i)) |
                             (static_cast<uintptr_t>(3) << (18 + 4 * i));
            }
            ok = SetThreadContext(thread, &c) != 0;
        }
        ResumeThread(thread);
    }
    CloseHandle(thread);
    return ok;
}

// While BF3 rebuilds objects (level loads, video settings) a chain can briefly land somewhere
// else, even on a misaligned address. Only watch, and so only write to, a real camera transform.
bool LooksValid(uintptr_t address) {
    if (address & 0xF)
        return false;
    float m[16];
    return camera::SafeRead(address, m, sizeof(m)) && IsCamera(m);
}

// What should be watched right now: render cameras, the cull camera (or hunt candidate), and
// the weapon camera while the view is turned.
void DesiredSlots(uintptr_t (&out)[kSlots]) {
    std::fill(std::begin(out), std::end(out), 0);
    bool weapon = g_view[g_viewIndex.load()].active || g_weaponTest.load();

    uintptr_t cull = 0;
    int hunt = g_huntIndex.load();
    if (hunt >= 0) {
        std::lock_guard lock(g_testMutex);
        if (hunt < static_cast<int>(g_testCopies.size()))
            cull = g_testCopies[hunt];
    } else if (!camera::Resolve(config::GetCull(), cull)) {
        cull = 0;
    }
    if (cull && LooksValid(cull))
        out[kCullSlot] = cull;

    int renderSlots = out[kCullSlot] ? kCullSlot : weapon ? kWeaponSlot : kSlots;
    std::vector<config::CameraPointer> cameras = config::GetCameras();
    for (size_t i = 0; i < cameras.size() && i < static_cast<size_t>(renderSlots); ++i) {
        uintptr_t address = 0;
        if (camera::Resolve(cameras[i], address) && LooksValid(address))
            out[i] = address;
    }
    uintptr_t weaponAddress = 0;
    if (weapon && camera::Resolve(config::GetAim().gameCamera, weaponAddress) && LooksValid(weaponAddress))
        out[kWeaponSlot] = weaponAddress;
    g_weaponAddress = out[kWeaponSlot] ? weaponAddress : 0;
}

// The game's own threads. (A Toolhelp snapshot lists every thread on the PC: 11 ms with SteamVR
// and browsers running, and this thread took one about every 50 ms.)
std::vector<DWORD> GameThreads() {
    using NextThread = LONG(NTAPI*)(HANDLE, HANDLE, ACCESS_MASK, ULONG, ULONG, PHANDLE);
    static const auto next =
        reinterpret_cast<NextThread>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtGetNextThread"));
    std::vector<DWORD> ids;
    if (!next)
        return ids;
    HANDLE thread = nullptr, following = nullptr;
    while (next(GetCurrentProcess(), thread, THREAD_QUERY_LIMITED_INFORMATION, 0, 0, &following) >= 0) {
        if (thread)
            CloseHandle(thread);
        thread = following;
        ids.push_back(GetThreadId(thread));
    }
    if (thread)
        CloseHandle(thread);
    return ids;
}

// Every 50 ms: re-resolve what to watch, since BF3 moves the render camera to new memory every
// few seconds and each moment unwatched is a frame without the eye shift. Game threads are
// re-armed straight away when it moves, and new threads are picked up twice a second.
DWORD WINAPI WatchThread(LPVOID) {
    std::vector<DWORD> armed;
    uintptr_t current[kSlots] = {}, currentOffsets[kSlots] = {};
    int missing[kSlots] = {};
    for (int tick = 0;; ++tick) {
        Sleep(50);
        uintptr_t desired[kSlots], offsets[kSlots];
        DesiredSlots(desired);
        bool fov = g_renderFov.load() > 0.0f;
        for (int i = 0; i < kSlots; ++i)
            offsets[i] = WatchOffset(i, fov);
        // A camera block often fails validation for a moment while BF3 rewrites it. Keep the
        // breakpoint on it for up to a second rather than re-arming every thread each flicker;
        // the handler checks the block again before every write anyway.
        // The render and cull cameras' chain goes through memory BF3 reuses every frame: it
        // resolves to something else about one read in eight (sampled 2026-10-30), and once,
        // after the opening ride, for good, which lost the wide view (the game drew 55 degrees).
        // The cameras themselves stay put for minutes, so those keep their breakpoint for as long
        // as the block still holds a camera transform.
        for (int i = 0; i < kSlots; ++i) {
            if (desired[i] || !current[i]) {
                missing[i] = 0;
            } else if (i != kWeaponSlot && LooksValid(current[i])) {
                desired[i] = current[i];
            } else if (++missing[i] < 20) {
                desired[i] = current[i];
            } else if (missing[i] == 20) {
                // Dropped once (logged once): which part of the camera check its block failed (The Great
                // Destroyer's train checkpoint drops the weapon slot for its 7.4 s opening sequence).
                float m[16] = {};
                const bool read = camera::SafeRead(current[i], m, sizeof(m));
                auto row = [&m](int r) {
                    return std::sqrt(m[r * 4] * m[r * 4] + m[r * 4 + 1] * m[r * 4 + 1] + m[r * 4 + 2] * m[r * 4 + 2]);
                };
                Log("Camera watch: slot %d dropped (%08llX %s): rows %.3f %.3f %.3f, w %.3f %.3f %.3f", i,
                    static_cast<unsigned long long>(current[i]), read ? "readable" : "unreadable", row(0), row(1), row(2),
                    m[3], m[7], m[11]);
            }
        }
        bool moved = !std::equal(std::begin(desired), std::end(desired), std::begin(current)) ||
                     !std::equal(std::begin(offsets), std::end(offsets), std::begin(currentOffsets));
        if (moved) {
            armed.clear();
            std::copy(std::begin(desired), std::end(desired), std::begin(current));
            std::copy(std::begin(offsets), std::end(offsets), std::begin(currentOffsets));
            // Address first and offset second could briefly pair a new address with an old
            // breakpoint; the handler then just disarms that thread until it is re-armed below.
            for (int i = 0; i < kSlots; ++i) {
                g_slots[i] = desired[i];
                g_slotOffsets[i] = offsets[i];
            }
            Log("Camera watch: watching %08X %08X %08X %08X%s", static_cast<unsigned>(desired[0]),
                static_cast<unsigned>(desired[1]), static_cast<unsigned>(desired[2]),
                static_cast<unsigned>(desired[3]), fov ? " (render and cull views past their FOV)" : "");
        }
        if (!moved && tick % 10 != 0)
            continue;

        DWORD self = GetCurrentThreadId();
        for (DWORD id : GameThreads()) {
            if (id == self || std::find(armed.begin(), armed.end(), id) != armed.end())
                continue;
            if (ArmThread(id, current, currentOffsets))
                armed.push_back(id);
        }
    }
}

void LogWatch() {
    bool any = false;
    for (auto& slot : g_slots)
        any |= slot.load() != 0;
    if (!any)
        return;
    uint32_t hits = g_hits.exchange(0);
    Log("Camera watch: %u writes in the last second, eye offset %.3f m", hits, g_eyeOffset.load());
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    for (; g_writersLogged < kMaxWriters; ++g_writersLogged) {
        uintptr_t ip = g_writers[g_writersLogged].load();
        if (!ip)
            break;
        Log("Camera watch: written by the instruction before bf3.exe+%X", static_cast<unsigned>(ip - base));
    }

    Log("Camera watch: per slot %u %u %u %u", g_slotHits[0].exchange(0), g_slotHits[1].exchange(0),
        g_slotHits[2].exchange(0), g_slotHits[3].exchange(0));
    // What the game actually renders with: the render view's FOV and the one its projection
    // matrix was built from (scale = 1 / tan(FOV / 2)).
    constexpr float kDeg = 57.29578f;
    auto fromScale = [](float scale) { return scale > 0.0f ? 2.0f * std::atan(1.0f / scale) * kDeg : 0.0f; };
    float source = 0.0f, view[2] = {}, cull[2] = {}, scale = 0.0f, firstPerson[2] = {}, firstScale = 0.0f;
    camera::SafeRead(g_fovSource, &source, sizeof(source));
    if (uintptr_t render = g_slots[0].load()) {
        camera::SafeRead(render + kFovFromTransform, view, sizeof(view));
        camera::SafeRead(render + kProjectionFromTransform, &scale, sizeof(scale));
        uintptr_t fp = render - kMainTransform + kFirstPersonView + kViewFromTransform;
        camera::SafeRead(fp + kFovFromTransform, firstPerson, sizeof(firstPerson));
        camera::SafeRead(fp + kProjectionFromTransform, &firstScale, sizeof(firstScale));
    }
    if (uintptr_t cullView = g_slots[kCullSlot].load())
        camera::SafeRead(cullView + kFovFromTransform, cull, sizeof(cull));
    Log("FOV: source %.1f, render view %.1f (its projection %.1f), cull view %.1f, first person %.1f (its "
        "projection %.1f); wanted %.1f degrees",
        source * kDeg, view[0] * kDeg, fromScale(scale), cull[0] * kDeg, firstPerson[0] * kDeg, fromScale(firstScale),
        g_renderFov.load() * kDeg);
}

}

namespace camera {

// Pointer chains go stale between levels, so every read must survive bad memory.
bool SafeRead(uintptr_t address, void* out, size_t size) {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Resolve(const config::CameraPointer& pointer, uintptr_t& address) {
    if (!pointer.base)
        return false;
    address = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + pointer.base;
    for (uintptr_t offset : pointer.offsets) {
        uintptr_t next = 0;
        if (!SafeRead(address, &next, sizeof(next)) || !next)
            return false;
        address = next + offset;
    }
    return true;
}

bool Read(Transform& out) {
    uintptr_t address = 0;
    return Resolve(config::GetCamera(), address) && SafeRead(address, &out, sizeof(out));
}

void Probe() {
    static ULONGLONG lastLog = 0;
    ULONGLONG now = GetTickCount64();
    if (now - lastLog < 1000)
        return;
    lastLog = now;
    LogWatch();
    if (!config::GetCamera().base)
        return;

    Transform t;
    if (!Read(t)) {
        Log("Camera probe: pointer chain did not resolve");
        return;
    }
    Log("Camera probe: pos (%.2f, %.2f, %.2f) forward (%.2f, %.2f, %.2f)", t.trans[0], t.trans[1], t.trans[2],
        t.forward[0], t.forward[1], t.forward[2]);
}

void StartWatch() {
    g_fovSource = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + kFovSource;
    AddVectoredExceptionHandler(1, OnException);
    if (HANDLE t = CreateThread(nullptr, 0, WatchThread, nullptr, 0, nullptr))
        CloseHandle(t);
}

void SetEyeOffset(float metres) {
    g_eyeOffset = metres;
}

void SetViewTurn(const ViewTurn* turn) {
    int next = 1 - g_viewIndex.load(std::memory_order_relaxed);
    g_view[next].active = turn != nullptr;
    if (turn)
        g_view[next].turn = *turn;
    g_viewIndex.store(next, std::memory_order_release);
}

void SetWeaponTest(const float* offset) {
    if (offset)
        for (int k = 0; k < 3; ++k)
            g_weaponTestOffset[k] = offset[k];
    if (g_weaponTest.exchange(offset != nullptr) != (offset != nullptr) || offset)
        Log("Camera: the weapon camera test is %s (%.2f %.2f %.2f along its right, up and back)",
            offset ? "on" : "off", offset ? offset[0] : 0.0f, offset ? offset[1] : 0.0f, offset ? offset[2] : 0.0f);
}

void SetGunFlip(bool flip) {
    g_gunFlip = flip;
}

void SetRenderFov(float radians) {
    g_renderFov = radians;
}

float WantedFov() {
    return g_renderFov.load(std::memory_order_relaxed);
}

void SetViewsMode(bool on) {
    g_viewsMode = on;
}

bool TurnToHead(float* transform) {
    // The frame's turn (BeginViews): both eyes and their cull cameras turned alike.
    const ViewTurnState& view = g_frameLatched ? g_frameView : g_view[g_viewIndex.load(std::memory_order_acquire)];
    // Why a camera wasn't turned, counted for TakeTurned's log: after a load the game's own arms showed
    // 6.5 s with no camera turned at all (the user: "it could be quicker", 16:28).
    if (!view.active) {
        g_refusedInactive.fetch_add(1, std::memory_order_relaxed);
        g_haveLast = false;  // nothing turned: no last picture to tell a repeated camera by
        return false;
    }
    if (!IsCamera(transform)) {
        if (g_refusedNotCamera.fetch_add(1, std::memory_order_relaxed) == 0)
            std::memcpy(g_refusedMatrix, transform, sizeof(g_refusedMatrix));
        return false;
    }
    float before[16];
    std::memcpy(before, transform, sizeof(before));
    // weapon.txt "viewlog N": for N seconds, each frame's first camera turned: the hand's yaw, the
    // look yaw written, the game camera's yaw and place, the head's yaw and the yaw shown. The user
    // saw the view move with the gun though the look yaw checked out (2026-10-30): this shows
    // whether it turns or the game's camera slides. Marked " | nudge" (mouse aim holding its corrections for the
    // search's nudge) or " | again" (a camera already turned, left as it was: below).
    static ULONGLONG lastLog = 0;
    auto logView = [&](const char* mark) {
        const ULONGLONG now = GetTickCount64();
        if (now >= g_turnLogUntil.load(std::memory_order_relaxed) || now - lastLog < 10)
            return;
        lastLog = now;
        const float* q = view.turn.head;
        const float forward[3] = {-2 * (q[0] * q[2] + q[3] * q[1]), -2 * (q[1] * q[2] - q[3] * q[0]),
                                  -(1 - 2 * (q[0] * q[0] + q[1] * q[1]))};
        const float deg = 57.29578f;
        Log("View: hand %.2f written %.2f | camera %.2f at %.3f %.3f %.3f | head %.2f shown %.2f%s",
            view.turn.currentYaw * deg, view.turn.lookYaw * deg, std::atan2(before[8], -before[10]) * deg, before[12],
            before[13], before[14], std::atan2(-forward[0], -forward[2]) * deg,
            std::atan2(transform[8], -transform[10]) * deg, mark);
    };
    float local[4], taken = 0.0f;
    ViewTurnLocal(transform, view.turn, local, &taken);
    // A cutscene's camera can come mirrored (the user, The Great Destroyer 2026-10-31: as Kovac walks
    // in, turning the head left turned the view right; the look angles weren't even known then, so
    // nothing fought the game): its rows the other handedness from the player's own camera, learned
    // while the look angles are written (that camera follows them). The turn is then mirrored the
    // same way, across its right row: yaw and roll the other way, pitch as it is.
    static std::atomic<int> s_normalHand{0};
    static int s_lastHand = 0;
    const float* r0 = transform;
    const float* r1 = transform + 4;
    const float* r2 = transform + 8;
    const float det = r0[0] * (r1[1] * r2[2] - r1[2] * r2[1]) - r0[1] * (r1[0] * r2[2] - r1[2] * r2[0]) +
                      r0[2] * (r1[0] * r2[1] - r1[1] * r2[0]);
    const int hand = det < 0.0f ? -1 : 1;
    if (view.turn.haveLook && s_normalHand.exchange(hand) != hand)
        Log("Camera: the player's camera has %s rows", hand > 0 ? "right-handed" : "left-handed");
    const int normal = s_normalHand.load(std::memory_order_relaxed);
    const bool mirrored = normal != 0 && hand != normal && g_mirrorFix.load(std::memory_order_relaxed);
    static ULONGLONG s_handLogged = 0;
    if (hand != s_lastHand && GetTickCount64() - s_handLogged >= 1000) {
        s_lastHand = hand;
        s_handLogged = GetTickCount64();
        Log("Camera: the camera turned to the head has %s rows (det %.3f)%s", hand > 0 ? "right-handed" : "left-handed",
            det, mirrored ? ": mirrored from the player's own, the head's turn mirrored to match" : "");
    }
    if (mirrored) {
        local[1] = -local[1];
        local[2] = -local[2];
    }
    // A camera the mod had already turned, offered again in this frame's views: its yaw the last frame's shown yaw
    // within 0.04 degrees after a jump of 5 to 75 degrees (19 frames in The Great Destroyer, 18:30-18:44; at the
    // game's own place, no eye shift: the weapon camera as OnException turned it). With a look yaw the turn reads
    // the camera's own yaw and comes out the same; without one the yaw was taken out of it a second time and the
    // picture flicked for a frame by the hand's angle from the head (-75 degrees at 18:42:40.994, 18:42:45.660,
    // 18:43:03.694 and 18:44:27.259, standing still on the train). It's left as it was turned.
    const float cameraYaw = std::atan2(before[8], -before[10]);
    if (!view.turn.haveLook && !mirrored && g_haveRef &&
        std::fabs(std::remainder(cameraYaw - g_refShownYaw, 2 * kPi)) < kRepeatSame &&
        std::fabs(std::remainder(cameraYaw - g_refCameraYaw, 2 * kPi)) > kRepeatJump &&
        std::fabs(std::asin(std::clamp(-before[9], -1.0f, 1.0f)) - g_refShownPitch) < kRepeatPitch) {
        g_turned.fetch_add(1, std::memory_order_relaxed);  // turned (before): no cutscene
        static ULONGLONG s_repeatLogged = 0;
        ++g_repeats;
        if (GetTickCount64() - s_repeatLogged >= 1000) {
            s_repeatLogged = GetTickCount64();
            Log("Camera: %u camera%s the mod had turned came again (the last picture's yaw, %.1f degrees from the game's): "
                "left as turned",
                g_repeats, g_repeats == 1 ? "" : "s", std::fabs(std::remainder(cameraYaw - g_refCameraYaw, 2 * kPi)) * 57.29578f);
            g_repeats = 0;
        }
        logView(" | again");
        return true;
    }
    const bool first = g_frameFirst;
    g_frameFirst = false;
    TurnLocal(transform, local);
    g_turned.fetch_add(1, std::memory_order_relaxed);
    // What the frame's first camera showed, for xr_runtime.cpp's picture body (not a mirrored one: its turn is
    // the other way round), and for the next frame's check above.
    if (first && !mirrored) {
        uint32_t low = 0, high = 0;
        std::memcpy(&low, &cameraYaw, sizeof(low));
        std::memcpy(&high, &taken, sizeof(high));
        g_shown.store(static_cast<uint64_t>(high) << 32 | low, std::memory_order_release);
        g_shownAt.store(GetTickCount64(), std::memory_order_release);
        g_lastCameraYaw = cameraYaw;
        g_lastShownYaw = std::atan2(transform[8], -transform[10]);
        g_lastShownPitch = std::asin(std::clamp(-transform[9], -1.0f, 1.0f));
        g_haveLast = true;
    }
    logView(view.turn.nudging ? " | nudge" : "");
    return true;
}

void BeginViews() {
    g_frameView = g_view[g_viewIndex.load(std::memory_order_acquire)];
    g_frameLatched = true;
    g_frameFirst = true;
    g_refCameraYaw = g_lastCameraYaw;
    g_refShownYaw = g_lastShownYaw;
    g_refShownPitch = g_lastShownPitch;
    g_haveRef = g_haveLast;
}

bool ShownTurn(float& cameraYaw, float& takenYaw) {
    const ULONGLONG at = g_shownAt.load(std::memory_order_acquire);
    if (!at || GetTickCount64() - at > kShownFresh)
        return false;
    const uint64_t v = g_shown.load(std::memory_order_acquire);
    const uint32_t low = static_cast<uint32_t>(v), high = static_cast<uint32_t>(v >> 32);
    std::memcpy(&cameraYaw, &low, sizeof(low));
    std::memcpy(&takenYaw, &high, sizeof(high));
    return std::isfinite(cameraYaw) && std::isfinite(takenYaw);
}

int TakeTurned() {
    const int turned = g_turned.exchange(0, std::memory_order_relaxed);
    // While none are turned, once a second: why (the view turn off, or the matrix not a camera: then its
    // translation and its rows' lengths, the first refused one), and whether the views were built at all.
    static ULONGLONG s_logged = 0;
    const ULONGLONG now = GetTickCount64();
    const uint32_t inactive = g_refusedInactive.exchange(0), notCamera = g_refusedNotCamera.exchange(0);
    if (turned == 0 && now - s_logged >= 1000) {
        s_logged = now;
        const float* m = g_refusedMatrix;
        auto row = [m](int r) { return std::sqrt(m[r * 4] * m[r * 4] + m[r * 4 + 1] * m[r * 4 + 1] + m[r * 4 + 2] * m[r * 4 + 2]); };
        Log("Camera: no camera turned this picture: %u refused with the view turn off, %u not a camera%s", inactive,
            notCamera, notCamera ? "" : " (none offered: the views not built?)");
        if (notCamera)
            Log("Camera: the first refused: rows %.3f %.3f %.3f, w %.3f %.3f %.3f, at (%.2f %.2f %.2f)", row(0), row(1),
                row(2), m[3], m[7], m[11], m[12], m[13], m[14]);
    }
    return turned;
}

void SetMirrorFix(bool on) {
    if (g_mirrorFix.exchange(on) != on)
        Log("Camera: a mirrored camera's turn is %s", on ? "mirrored to match" : "left as any other");
}

void LogTurns(int seconds) {
    g_turnLogUntil.store(GetTickCount64() + 1000ull * seconds, std::memory_order_relaxed);
    Log("View: logging the view's turn each frame for %d s", seconds);
}

bool GameWeaponAngles(float& yaw, float& pitch) {
    if (GetTickCount64() - g_weaponYawAt.load(std::memory_order_relaxed) > 200)
        return false;
    yaw = g_weaponYaw.load(std::memory_order_relaxed);
    pitch = g_weaponPitch.load(std::memory_order_relaxed);
    return std::isfinite(yaw) && std::isfinite(pitch);
}

uint32_t TakeCameraBuilds() {
    return g_cameraBuilds.exchange(0);
}

bool ProjectionDepth(float& nearPlane, float& depthScale) {
    // Seen in game: rows (s 0 0 0) (0 s 0 0) (0 0 -1.00001 -1) (0 0 -0.06 0): depth = 1.00001 - 0.06/distance.
    uintptr_t render = g_slots[0].load();
    float m[16];
    if (!render || !SafeRead(render + kProjectionFromTransform, m, sizeof(m)))
        return false;
    float scale = -m[10], n = -m[14];
    if (!(scale > 0.9f && scale < 1.1f && n > 0.001f && n < 1.0f))
        return false;
    nearPlane = n;
    depthScale = scale;
    return true;
}

float SourceFov() {
    float fov = 0.0f;
    return g_fovSource && SafeRead(g_fovSource, &fov, sizeof(fov)) && LooksLikeFov(fov) ? fov : 0.0f;
}

float RenderedFov() {
    // The render view's projection says what the last frame was drawn with. With the render view
    // not watched, nothing widens it: the game draws its own FOV.
    if (uintptr_t render = g_slots[0].load()) {
        float scale = 0.0f;
        if (SafeRead(render + kProjectionFromTransform, &scale, sizeof(scale)) && scale > 0.05f && scale < 20.0f)
            return 2.0f * std::atan(1.0f / scale);
    }
    return SourceFov();
}

void SetTestCopies(const std::vector<uintptr_t>& copies) {
    std::lock_guard lock(g_testMutex);
    g_testCopies = copies;
    g_huntIndex = -1;
}

void ToggleHunt() {
    if (g_huntIndex >= 0) {
        g_huntIndex = -1;
        Log("Cull hunt: stopped without saving");
        BeepAsync(440, 300);
        return;
    }
    size_t count;
    {
        std::lock_guard lock(g_testMutex);
        count = g_testCopies.size();
    }
    if (!count) {
        Log("Cull hunt: no camera copies yet. Press F10 first to run the camera finder.");
        BeepAsync(300, 700);
        return;
    }
    g_huntIndex = 0;
    g_huntStepTime = GetTickCount64();
    Log("Cull hunt: trying copy 1 of %zu", count);
    BeepAsync(1000, 80);
}

void UpdateHunt() {
    int index = g_huntIndex.load();
    if (index < 0 || GetTickCount64() - g_huntStepTime < kHuntStepMs)
        return;
    size_t count;
    {
        std::lock_guard lock(g_testMutex);
        count = g_testCopies.size();
    }
    g_huntStepTime = GetTickCount64();
    if (++index >= static_cast<int>(count)) {
        g_huntIndex = -1;
        Log("Cull hunt: tried all %zu copies without a save", count);
        BeepAsync(300, 700);
        return;
    }
    g_huntIndex = index;
    Log("Cull hunt: trying copy %d of %zu", index + 1, count);
    BeepAsync(1000, 80);
}

uintptr_t HuntCandidate() {
    int index = g_huntIndex.load();
    std::lock_guard lock(g_testMutex);
    return index >= 0 && index < static_cast<int>(g_testCopies.size()) ? g_testCopies[index] : 0;
}

void StopHunt() {
    g_huntIndex = -1;
}

}
