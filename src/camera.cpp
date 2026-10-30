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
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

#include "log.h"

namespace {

constexpr uintptr_t kWatchOffset = 0x34;  // translation.y
constexpr int kSlots = 4;                 // x86 debug registers Dr0..Dr3
constexpr int kMaxWriters = 8;

// The last debug register is for the weapon camera (the copy the gun is drawn from) while the
// view is turned; the others hold render cameras, which get the eye shift.
constexpr int kWeaponSlot = kSlots - 1;

// Transform start per debug register, 0 = unused. The exception handler reads these.
std::atomic<uintptr_t> g_slots[kSlots];
std::atomic<float> g_eyeOffset{0.0f};
std::atomic<uintptr_t> g_weaponAddress{0};
std::atomic<bool> g_gunFlip{false};
std::atomic<uint32_t> g_hits{0};
std::atomic<uintptr_t> g_writers[kMaxWriters];
int g_writersLogged = 0;

// View turn for the handler: written into the inactive buffer, then published by flipping the index.
struct ViewTurnState {
    bool active;
    camera::ViewTurn turn;
};
ViewTurnState g_view[2] = {};
std::atomic<int> g_viewIndex{0};

// Slot 2 holds the cull camera (the copy BF3 decides what to draw from), or while hunting for
// it, the candidate being tried. It gets the same head turn as the render cameras in slots 0-1.
constexpr int kCullSlot = 2;

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
// leaves body yaw * head.
void ViewTurnLocal(const float* m, const camera::ViewTurn& turn, float* local) {
    float pitch = std::asin(std::clamp(-m[9], -1.0f, 1.0f));  // true forward = -row2
    float yaw = turn.controllerYaw;
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

// Runs on the game thread that just wrote a camera. Must stay tiny: no locks, no logging.
LONG CALLBACK OnException(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = info->ContextRecord;
    if (!(c->Dr6 & 0xF))
        return EXCEPTION_CONTINUE_SEARCH;

    float offset = g_eyeOffset.load(std::memory_order_relaxed);
    for (int i = 0; i < kSlots; ++i) {
        if (!(c->Dr6 & (1u << i)))
            continue;
        uintptr_t watched = g_slots[i].load(std::memory_order_relaxed);
        if (!watched || DebugRegister(c, i) != watched + kWatchOffset) {
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
        auto m = reinterpret_cast<float*>(watched);
        // Only touch memory that holds a camera right now: BF3 reuses these blocks for other
        // data, and shifting anything else corrupts it.
        if (!IsCamera(m))
            continue;
        const ViewTurnState& view = g_view[g_viewIndex.load(std::memory_order_acquire)];
        float local[4];
        if (view.active)
            ViewTurnLocal(m, view.turn, local);
        if (watched == g_weaponAddress.load(std::memory_order_relaxed)) {
            // BF3 places the gun from the weapon camera relative to the picture's camera, so
            // turning the weapon camera the opposite way keeps the gun with the hand. Which way
            // is "opposite" is confirmed in game (F1 flips it).
            if (view.active) {
                if (!g_gunFlip.load(std::memory_order_relaxed))
                    local[0] = -local[0], local[1] = -local[1], local[2] = -local[2];
                TurnLocal(m, local);
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
    c->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Data breakpoints after any write to the 4 bytes at each target (0 = slot unused).
bool ArmThread(DWORD threadId, const uintptr_t (&targets)[kSlots]) {
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
                SetDebugRegister(&c, i, targets[i] ? targets[i] + kWatchOffset : 0);
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
    bool weapon = g_view[g_viewIndex.load()].active;

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

// Every 50 ms: re-resolve what to watch, since BF3 moves the render camera to new memory every
// few seconds and each moment unwatched is a frame without the eye shift. Game threads are
// re-armed straight away when it moves, and new threads are picked up twice a second.
DWORD WINAPI WatchThread(LPVOID) {
    std::vector<DWORD> armed;
    uintptr_t current[kSlots] = {};
    int missing[kSlots] = {};
    for (int tick = 0;; ++tick) {
        Sleep(50);
        uintptr_t desired[kSlots];
        DesiredSlots(desired);
        // A camera block often fails validation for a moment while BF3 rewrites it. Keep the
        // breakpoint on it for up to a second rather than re-arming every thread each flicker;
        // the handler checks the block again before every write anyway.
        for (int i = 0; i < kSlots; ++i) {
            if (desired[i] || !current[i]) {
                missing[i] = 0;
            } else if (++missing[i] < 20) {
                desired[i] = current[i];
            }
        }
        bool moved = !std::equal(std::begin(desired), std::end(desired), std::begin(current));
        if (moved) {
            armed.clear();
            std::copy(std::begin(desired), std::end(desired), std::begin(current));
            for (int i = 0; i < kSlots; ++i)
                g_slots[i] = desired[i];
            Log("Camera watch: watching %08X %08X %08X %08X", static_cast<unsigned>(desired[0]),
                static_cast<unsigned>(desired[1]), static_cast<unsigned>(desired[2]),
                static_cast<unsigned>(desired[3]));
        }
        if (!moved && tick % 10 != 0)
            continue;

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE)
            continue;
        THREADENTRY32 entry{sizeof(entry)};
        DWORD self = GetCurrentThreadId(), process = GetCurrentProcessId();
        for (BOOL ok = Thread32First(snap, &entry); ok; ok = Thread32Next(snap, &entry)) {
            if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self ||
                std::find(armed.begin(), armed.end(), entry.th32ThreadID) != armed.end())
                continue;
            if (ArmThread(entry.th32ThreadID, current))
                armed.push_back(entry.th32ThreadID);
        }
        CloseHandle(snap);
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

void SetGunFlip(bool flip) {
    g_gunFlip = flip;
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
