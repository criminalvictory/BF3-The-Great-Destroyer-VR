// OpenXR side of the mod. Two output modes:
//
// Theater: the game's finished frame is shown as a large quad floating in front of the player.
//
// Stereo (milestone 2): alternate-eye rendering. Each game frame is assigned to one eye in
// turn and submitted as that eye's projection view, together with the headset pose it was
// shown for. The other eye keeps showing its last image. Until the camera write lands
// (see camera.cpp) both eyes see the same viewpoint, so this exercises the pipeline only.

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "aim.h"
#include "camera.h"
#include "config.h"
#include "depth3d.h"
#include "finder.h"
#include "fov.h"
#include "god.h"
#include "input.h"
#include "log.h"
#include "skin.h"
#include "window.h"
#include "xr_runtime.h"

namespace {

constexpr float kPi = 3.14159265f;

struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D11KHR> images;
};

// Index 0/1 are the eyes in stereo mode, 2 is the theater screen.
constexpr int kLeftEye = 0, kRightEye = 1, kTheater = 2;

XrInstance g_instance = XR_NULL_HANDLE;
XrSystemId g_system = XR_NULL_SYSTEM_ID;
XrSession g_session = XR_NULL_HANDLE;
XrSpace g_localSpace = XR_NULL_HANDLE;  // runtime's LOCAL origin, never moved
XrSpace g_space = XR_NULL_HANDLE;       // LOCAL shifted by recenter; all layers live here
XrSpace g_viewSpace = XR_NULL_HANDLE;   // the headset itself
Swapchain g_swapchains[3];

ID3D11Device* g_device = nullptr;
UINT g_scWidth = 0, g_scHeight = 0;
DXGI_FORMAT g_scFormat = DXGI_FORMAT_UNKNOWN;
DXGI_FORMAT g_scSourceFormat = DXGI_FORMAT_UNKNOWN;

bool g_failed = false;
bool g_running = false;
bool g_resizePending = false;
bool g_recenterPending = true;

// Alternate-eye state: which eye the next game frame goes to, and the pose each eye was last shown for.
int g_nextEye = kLeftEye;
XrPosef g_eyePose[2] = {};
bool g_eyeValid[2] = {};
// The desktop window shows every game frame, so in stereo it would alternate between the eyes
// and fine detail flickers. The last left-eye frame is kept here and shown in place of each
// right-eye frame, once the right eye has gone to the headset.
ID3D11Texture2D* g_mirror = nullptr;
bool g_mirrorValid = false;
// Depth-3D eyes made while the headset is idle, for the desktop (PreviewDepth3D).
ID3D11Texture2D* g_previewEyes[2] = {};

// The eye poses located at each of the last few frames, the newest at g_poseNewest. The game draws
// each frame with the head pose its camera was handed some frames earlier; submitting the image
// with that pose lets the compositor correct for the latency. One frame was assumed; the picture
// doubling on head turns, more so at a lower frame rate, suggests more ([stereo] pose_lag, F11).
constexpr int kPoseHistory = 4;
XrPosef g_poseHistory[kPoseHistory][2] = {};
int g_poseNewest = 0;
int g_posesKept = 0;

bool g_aimingWithHand = false;

// Yaw (turning left positive) and pitch (looking up positive) of where q points: (0, 0, -1) rotated by q.
void LookAngles(const XrQuaternionf& q, float& yaw, float& pitch) {
    float fx = -2.0f * (q.x * q.z + q.w * q.y);
    float fy = -2.0f * (q.y * q.z - q.w * q.x);
    float fz = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));
    yaw = std::atan2(-fx, -fz);
    pitch = std::asin(std::clamp(fy, -1.0f, 1.0f));
}

// Also restarts head tracking, so a mode switch or recenter doesn't jerk the view.
void ResetEyes() {
    g_eyeValid[0] = g_eyeValid[1] = false;
    g_posesKept = 0;
    aim::Reset();
}
// F7: the game may render a frame later than we think, which swaps the eyes and turns depth inside out.
bool g_swapEyes = false;
// How the two eyes are made (Scroll Lock cycles): from each frame's picture and depth (both eyes
// every frame, depth3d.cpp); alternate eyes (each game frame for one eye: the picture doubled on
// head turns, more so at lower frame rates); mono (every frame to both eyes, flat: no doubling,
// which showed the alternating caused it); frozen (nothing new shown: the compositor held the
// picture still like a wall, so it handles each eye's pose).
enum StereoTest { kTestDepth, kTestNone, kTestMono, kTestFreeze, kStereoTests };
int g_stereoTest = kTestDepth;
// The view turn removes the hand's yaw from BF3's camera. BF3 may build that camera from look
// angles written a frame or two earlier, so the view turn uses the yaw from that many frames
// back ([controllers] view_lag, F11 cycles it); a mismatch shakes the picture as the arm moves.
constexpr int kYawHistory = 3;
float g_yawHistory[kYawHistory] = {};
int g_yawHistoryNext = 0;

bool Check(XrResult result, const char* what) {
    if (XR_SUCCEEDED(result))
        return true;
    char name[XR_MAX_RESULT_STRING_SIZE] = "?";
    if (g_instance)
        xrResultToString(g_instance, result, name);
    Log("%s failed: %s (%d)", what, name, static_cast<int>(result));
    return false;
}

bool CreateInstance() {
    const char* extensions[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(ci.applicationInfo.applicationName, "BF3 VR");
    ci.applicationInfo.applicationVersion = 2;
    strcpy_s(ci.applicationInfo.engineName, "bf3vr");
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = extensions;
    if (!Check(xrCreateInstance(&ci, &g_instance), "xrCreateInstance"))
        return false;

    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(g_instance, &props)))
        Log("OpenXR runtime: %s %u.%u.%u", props.runtimeName, XR_VERSION_MAJOR(props.runtimeVersion),
            XR_VERSION_MINOR(props.runtimeVersion), XR_VERSION_PATCH(props.runtimeVersion));

    XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!Check(xrGetSystem(g_instance, &si, &g_system), "xrGetSystem (is the headset connected?)"))
        return false;

    XrSystemProperties sys{XR_TYPE_SYSTEM_PROPERTIES};
    if (XR_SUCCEEDED(xrGetSystemProperties(g_instance, g_system, &sys)))
        Log("Headset: %s", sys.systemName);
    return true;
}

// The runtime must composite on the same GPU the game renders on.
bool CheckGraphicsRequirements(ID3D11Device* device) {
    PFN_xrGetD3D11GraphicsRequirementsKHR getRequirements = nullptr;
    if (!Check(xrGetInstanceProcAddr(g_instance, "xrGetD3D11GraphicsRequirementsKHR",
                                     reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements)),
               "xrGetInstanceProcAddr"))
        return false;
    XrGraphicsRequirementsD3D11KHR reqs{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    if (!Check(getRequirements(g_instance, g_system, &reqs), "xrGetD3D11GraphicsRequirementsKHR"))
        return false;

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    DXGI_ADAPTER_DESC desc{};
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)))) {
        if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
            adapter->GetDesc(&desc);
            adapter->Release();
        }
        dxgiDevice->Release();
    }
    Log("Game GPU: %ls", desc.Description);
    // Retrying can't fix either of these.
    if (std::memcmp(&desc.AdapterLuid, &reqs.adapterLuid, sizeof(LUID)) != 0) {
        Log("Game is rendering on a different GPU than the headset runtime wants; VR inactive");
        g_failed = true;
        return false;
    }
    if (device->GetFeatureLevel() < reqs.minFeatureLevel) {
        Log("Game D3D feature level is below the runtime minimum; VR inactive");
        g_failed = true;
        return false;
    }
    return true;
}

bool CreateReferenceSpace(XrReferenceSpaceType type, const XrPosef& pose, XrSpace& out) {
    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.referenceSpaceType = type;
    rs.poseInReferenceSpace = pose;
    return Check(xrCreateReferenceSpace(g_session, &rs, &out), "xrCreateReferenceSpace");
}

bool CreateSession(ID3D11Device* device) {
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    binding.device = device;
    XrSessionCreateInfo ci{XR_TYPE_SESSION_CREATE_INFO};
    ci.next = &binding;
    ci.systemId = g_system;
    if (!Check(xrCreateSession(g_instance, &ci, &g_session), "xrCreateSession"))
        return false;

    const XrPosef identity{{0, 0, 0, 1}, {0, 0, 0}};
    return CreateReferenceSpace(XR_REFERENCE_SPACE_TYPE_LOCAL, identity, g_localSpace) &&
           CreateReferenceSpace(XR_REFERENCE_SPACE_TYPE_LOCAL, identity, g_space) &&
           CreateReferenceSpace(XR_REFERENCE_SPACE_TYPE_VIEW, identity, g_viewSpace);
}

bool Init(ID3D11Device* device) {
    if (!g_device) {
        g_device = device;
        g_device->AddRef();
    }
    return CreateInstance() && CheckGraphicsRequirements(device) && CreateSession(device);
}

// The OpenXR loader explains why it can't find or load a runtime, but only on stderr.
// Send that to bf3vr_xr.log, and note any OpenXR/VR settings the game inherited from its launcher.
void EnableLoaderDiagnostics() {
    wchar_t path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&EnableLoaderDiagnostics), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\'))
        wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"bf3vr_xr.log");
    FILE* f = nullptr;
    if (_wfreopen_s(&f, path, L"w", stderr) == 0)
        SetEnvironmentVariableA("XR_LOADER_DEBUG", "all");

    if (wchar_t* env = GetEnvironmentStringsW()) {
        for (const wchar_t* var = env; *var; var += wcslen(var) + 1) {
            if (_wcsnicmp(var, L"XR_", 3) == 0 || _wcsnicmp(var, L"VR_", 3) == 0 || wcsstr(var, L"OPENXR") ||
                wcsstr(var, L"OpenXR") || _wcsnicmp(var, L"STEAMVR", 7) == 0)
                Log("Inherited env: %ls", var);
        }
        FreeEnvironmentStringsW(env);
    }
}

// Steam hands games it launches while SteamVR is running an XR_RUNTIME_JSON that names the
// 64-bit runtime (steamxr_win64.json). A 32-bit game like BF3 can't load it, and the loader
// gives up with "runtime unavailable". Point it at the 32-bit runtime instead.
void FixRuntimeOverride() {
    if (sizeof(void*) != 4)
        return;
    wchar_t value[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"XR_RUNTIME_JSON", value, MAX_PATH))
        return;
    std::wstring path(value);
    size_t pos = path.rfind(L"win64");
    if (pos == std::wstring::npos)
        return;
    path.replace(pos, 5, L"win32");
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        SetEnvironmentVariableW(L"XR_RUNTIME_JSON", path.c_str());
        Log("XR_RUNTIME_JSON pointed at a 64-bit runtime; using %ls", path.c_str());
    } else {
        // No 32-bit file next to it: fall back to the registered 32-bit runtime.
        SetEnvironmentVariableW(L"XR_RUNTIME_JSON", nullptr);
        Log("XR_RUNTIME_JSON pointed at a 64-bit runtime; cleared it to use the registered 32-bit one");
    }
}

// Brings up OpenXR on the game's device. If SteamVR isn't running yet, tries again every
// few seconds, so it can be started after the game.
bool TryInit(IDXGISwapChain* swap) {
    if (g_session)
        return true;
    static bool diagnosticsOn = false;
    if (!diagnosticsOn) {
        EnableLoaderDiagnostics();
        FixRuntimeOverride();
        diagnosticsOn = true;
    }
    static ULONGLONG nextAttempt = 0;
    ULONGLONG now = GetTickCount64();
    if (now < nextAttempt)
        return false;
    nextAttempt = now + 5000;

    ID3D11Device* device = nullptr;
    if (FAILED(swap->GetDevice(IID_PPV_ARGS(&device)))) {
        Log("Swap chain is not D3D11; VR inactive");
        g_failed = true;
        return false;
    }
    bool ok = Init(device);
    device->Release();
    if (ok) {
        Log("OpenXR session created");
        input::Init(g_instance, g_session);  // the mod still works without controllers
        return true;
    }

    // Destroying the instance destroys everything created from it.
    if (g_instance)
        xrDestroyInstance(g_instance);
    g_instance = XR_NULL_HANDLE;
    g_system = XR_NULL_SYSTEM_ID;
    g_session = XR_NULL_HANDLE;
    g_localSpace = g_space = g_viewSpace = XR_NULL_HANDLE;
    if (!g_failed)
        Log("OpenXR not available yet (is SteamVR running and the headset on?); retrying in 5 s");
    return false;
}

// Pick a runtime swapchain format that CopyResource accepts from the game's back buffer.
// The sRGB variant is preferred: the game writes gamma-encoded values, so the
// compositor should read them as sRGB.
DXGI_FORMAT PickSwapchainFormat(DXGI_FORMAT source) {
    DXGI_FORMAT wanted[2] = {};
    switch (source) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        wanted[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        wanted[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        wanted[0] = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        wanted[1] = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }

    uint32_t count = 0;
    xrEnumerateSwapchainFormats(g_session, 0, &count, nullptr);
    std::vector<int64_t> formats(count);
    xrEnumerateSwapchainFormats(g_session, count, &count, formats.data());
    for (DXGI_FORMAT f : wanted)
        if (std::find(formats.begin(), formats.end(), static_cast<int64_t>(f)) != formats.end())
            return f;
    return DXGI_FORMAT_UNKNOWN;
}

void DestroySwapchains() {
    for (Swapchain& sc : g_swapchains) {
        if (sc.handle)
            xrDestroySwapchain(sc.handle);
        sc = {};
    }
    g_scWidth = g_scHeight = 0;
    g_scSourceFormat = DXGI_FORMAT_UNKNOWN;
    depth3d::Reset();
    ResetEyes();
    if (g_mirror)
        g_mirror->Release();
    g_mirror = nullptr;
    g_mirrorValid = false;
    for (ID3D11Texture2D*& eye : g_previewEyes) {
        if (eye)
            eye->Release();
        eye = nullptr;
    }
}

void UpdateMirror(ID3D11Texture2D* back, const D3D11_TEXTURE2D_DESC& desc, int eye) {
    if (!config::Get().mirrorOneEye)
        return;
    if (!g_mirror) {
        D3D11_TEXTURE2D_DESC copy = desc;
        copy.Usage = D3D11_USAGE_DEFAULT;
        copy.BindFlags = 0;
        copy.CPUAccessFlags = 0;
        copy.MiscFlags = 0;
        if (FAILED(g_device->CreateTexture2D(&copy, nullptr, &g_mirror))) {
            g_mirror = nullptr;
            return;
        }
    }
    ID3D11DeviceContext* context = nullptr;
    g_device->GetImmediateContext(&context);
    if (eye == kLeftEye) {
        context->CopyResource(g_mirror, back);
        g_mirrorValid = true;
    } else if (g_mirrorValid) {
        context->CopyResource(back, g_mirror);
    }
    context->Release();
}

bool CreateSwapchain(UINT width, UINT height, Swapchain& sc) {
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format = g_scFormat;
    ci.sampleCount = 1;
    ci.width = width;
    ci.height = height;
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    if (!Check(xrCreateSwapchain(g_session, &ci, &sc.handle), "xrCreateSwapchain"))
        return false;

    uint32_t count = 0;
    xrEnumerateSwapchainImages(sc.handle, 0, &count, nullptr);
    sc.images.assign(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    return Check(xrEnumerateSwapchainImages(sc.handle, count, &count,
                                            reinterpret_cast<XrSwapchainImageBaseHeader*>(sc.images.data())),
                 "xrEnumerateSwapchainImages");
}

// One swapchain per eye plus one for the theater screen, all the size of the game's back buffer.
bool CreateSwapchains(const D3D11_TEXTURE2D_DESC& back) {
    g_scFormat = PickSwapchainFormat(back.Format);
    if (g_scFormat == DXGI_FORMAT_UNKNOWN) {
        Log("No compatible swapchain format for back buffer format %d", back.Format);
        return false;
    }
    for (Swapchain& sc : g_swapchains)
        if (!CreateSwapchain(back.Width, back.Height, sc))
            return false;

    g_scWidth = back.Width;
    g_scHeight = back.Height;
    g_scSourceFormat = back.Format;
    depth3d::SetPictureSize(back.Width, back.Height);
    Log("Swapchains %ux%u format %d (back buffer format %d, %u samples)", back.Width, back.Height, g_scFormat,
        back.Format, back.SampleDesc.Count);
    return true;
}

void HandleSessionState(XrSessionState state) {
    Log("Session state -> %d", state);
    switch (state) {
    case XR_SESSION_STATE_READY: {
        XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
        bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        g_running = Check(xrBeginSession(g_session, &bi), "xrBeginSession");
        break;
    }
    case XR_SESSION_STATE_STOPPING:
        xrEndSession(g_session);
        g_running = false;
        break;
    case XR_SESSION_STATE_EXITING:
    case XR_SESSION_STATE_LOSS_PENDING:
        // Headset went away (SteamVR closed, etc). Stay out of the way for the rest of this run.
        g_running = false;
        g_failed = true;
        break;
    default:
        break;
    }
}

void PollEvents() {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(g_instance, &event) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
            HandleSessionState(reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state);
        else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
            g_running = false, g_failed = true;
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

bool KeyPressed(int vk) {
    static bool wasDown[256] = {};
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool pressed = down && !wasDown[vk];
    wasDown[vk] = down;
    return pressed;
}

void PollHotkeys() {
    if (KeyPressed(VK_F8)) {
        config::Settings& s = config::Mutable();
        s.mode = s.mode == config::Mode::Stereo ? config::Mode::Theater : config::Mode::Stereo;
        ResetEyes();
        Log("Mode -> %s", s.mode == config::Mode::Stereo ? "stereo" : "theater");
    }
    if (KeyPressed(VK_F9))
        g_recenterPending = true;
    if (KeyPressed(VK_F10))
        finder::Start();
    if (KeyPressed(VK_F7)) {
        g_swapEyes = !g_swapEyes;
        Log("Eyes %s", g_swapEyes ? "swapped" : "normal");
    }
    // F11: how many frames old the head pose each eye image is submitted with (0-3). The view
    // turn no longer needs its own lag (camera.cpp reads it from each camera), so F11 moved here.
    if (KeyPressed(VK_F11)) {
        config::Settings& s = config::Mutable();
        s.poseLag = (s.poseLag + 1) % kPoseHistory;
        config::SavePoseLag();
        Log("Pose lag %d frame%s", s.poseLag, s.poseLag == 1 ? "" : "s");
    }
    // F6: hunt for the cull camera (after F10); F5: save the copy being tried.
    if (KeyPressed(VK_F6))
        camera::ToggleHunt();
    if (KeyPressed(VK_F5))
        finder::SaveCull();
    camera::UpdateHunt();
    // F1: swing the gun model the other way, if it moves away from the hand. Saved to bf3vr.ini.
    if (KeyPressed(VK_F1)) {
        config::Mutable().gunFlip = !config::Get().gunFlip;
        config::SaveGunFlip();
        Log("Gun direction %s", config::Get().gunFlip ? "flipped" : "normal");
    }
    camera::SetGunFlip(config::Get().gunFlip);
    // Delete: make the game render a wider field of view, or its own again. Saved to bf3vr.ini.
    if (KeyPressed(VK_DELETE)) {
        config::Settings& s = config::Mutable();
        s.renderFovDeg = s.renderFovDeg > 0.0f ? 0.0f : s.renderFovLastDeg;
        config::SaveRenderFov();
        Log("Render FOV %s", s.renderFovDeg > 0.0f ? "on" : "off (the game's own)");
    }
    // Page Up / Page Down: with the wider view on, change it (the headset image follows, so the
    // world stays life-size). Otherwise tune the FOV the eye images are shown with until the
    // world looks life-size: showing an image over a wider angle than BF3 drew it stretches it,
    // so a smaller value = less zoomed. Saved to bf3vr.ini.
    bool moreZoom = KeyPressed(VK_PRIOR);
    bool lessZoom = KeyPressed(VK_NEXT);
    if (moreZoom || lessZoom) {
        config::Settings& s = config::Mutable();
        float step = lessZoom ? -5.0f : 5.0f;
        if (s.renderFovDeg > 0.0f) {
            s.renderFovDeg = s.renderFovLastDeg = std::clamp(s.renderFovDeg + step, 40.0f, 140.0f);
            config::SaveRenderFov();
            Log("Render FOV %.0f deg", s.renderFovDeg);
        } else {
            s.gameVerticalFovDeg = std::clamp(s.gameVerticalFovDeg + step, 20.0f, 150.0f);
            config::SaveFov();
            Log("Display FOV %.0f deg", s.gameVerticalFovDeg);
        }
    }
    float wideFov = config::Get().renderFovDeg * kPi / 180.0f;
    camera::SetRenderFov(wideFov);
    skin::SetProjectionFix(wideFov > 0.0f ? camera::SourceFov() : 0.0f, wideFov);
    // Home: health finder step (full health, after damage, after refill). End: god mode on/off.
    if (KeyPressed(VK_HOME))
        finder::HealthStep();
    if (KeyPressed(VK_F16))
        finder::HealthAuto();
    if (KeyPressed(VK_END))
        god::Toggle();
    // Draw hunt, sent by tools/drawhunt.ps1 on keys no real keyboard has.
    if (KeyPressed(VK_F15))
        skin::HuntStart();
    if (KeyPressed(VK_F13))
        skin::HuntAnswer(true);
    if (KeyPressed(VK_F14))
        skin::HuntAnswer(false);
    if (KeyPressed(VK_F3))
        finder::StartAngles();
    if (KeyPressed(VK_F12))
        finder::StartSkeleton();
    // Insert: draw the gloves 20 cm higher, the first test of moving the hands.
    if (KeyPressed(VK_INSERT))
        skin::ToggleHandShift();
    // Pause: the skinning research hooks on/off (off by default: they cost frame rate).
    if (KeyPressed(VK_PAUSE))
        skin::ToggleResearch();
    if (KeyPressed(VK_SCROLL)) {
        static const char* kNames[] = {"depth 3D (both eyes from each frame and its depth)", "alternate eyes",
                                       "mono (the same picture to both eyes)", "frozen"};
        g_stereoTest = (g_stereoTest + 1) % kStereoTests;
        Log("Stereo: %s", kNames[g_stereoTest]);
    }
    // The weapon camera sits at a fixed place in bf3.exe, unlike the render cameras, which move
    // in memory and can be stale for a moment.
    {
        uintptr_t address = 0;
        camera::Transform cam;
        if (camera::Resolve(config::GetAim().gameCamera, address) && camera::SafeRead(address, &cam, sizeof(cam)))
        {
            const float forward[3] = {-cam.forward[0], -cam.forward[1], -cam.forward[2]};  // true view direction
            skin::SetCamera(cam.trans, cam.right, cam.up, forward);
        }
    }
    if (KeyPressed(VK_F4))
        aim::FlipYaw();
    if (KeyPressed(VK_F2))
        aim::FlipPitch();
    // Stereo mode sets the offset and view turn per frame; in theater it's the shift test or nothing.
    if (config::Get().mode == config::Mode::Theater) {
        camera::SetEyeOffset(0.0f);
        camera::SetViewTurn(nullptr);
    }
}

// Moves the layer space so its origin is under the headset, facing where the player looks (yaw only).
void Recenter(XrTime time) {
    XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(g_viewSpace, g_localSpace, time, &head)) ||
        !(head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) ||
        !(head.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
        return;

    const XrQuaternionf& q = head.pose.orientation;
    float yaw = std::atan2(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
    XrPosef origin{{0.0f, std::sin(yaw / 2), 0.0f, std::cos(yaw / 2)}, head.pose.position};

    XrSpace recentered = XR_NULL_HANDLE;
    if (!CreateReferenceSpace(XR_REFERENCE_SPACE_TYPE_LOCAL, origin, recentered))
        return;
    xrDestroySpace(g_space);
    g_space = recentered;
    ResetEyes();
    g_recenterPending = false;
    Log("Recentered (yaw %.0f deg)", yaw * 180.0f / kPi);
}

// Copies the game's back buffer into the next image of a swapchain.
bool SubmitImage(ID3D11Texture2D* back, const D3D11_TEXTURE2D_DESC& desc, Swapchain& sc) {
    uint32_t index = 0;
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (!Check(xrAcquireSwapchainImage(sc.handle, &acquire, &index), "xrAcquireSwapchainImage"))
        return false;
    if (!Check(xrWaitSwapchainImage(sc.handle, &wait), "xrWaitSwapchainImage")) {
        xrReleaseSwapchainImage(sc.handle, &release);
        return false;
    }

    ID3D11DeviceContext* context = nullptr;
    g_device->GetImmediateContext(&context);
    if (desc.SampleDesc.Count > 1)
        context->ResolveSubresource(sc.images[index].texture, 0, back, 0, g_scFormat);
    else
        context->CopyResource(sc.images[index].texture, back);
    context->Release();

    return Check(xrReleaseSwapchainImage(sc.handle, &release), "xrReleaseSwapchainImage");
}

XrFovf GameFov();

// Depth 3D: both eyes from this frame's picture and BF3's depth (depth3d.cpp). Until a depth
// buffer has been found, the picture goes to both eyes as it is (flat).
bool SubmitDepth3D(ID3D11Texture2D* back, const D3D11_TEXTURE2D_DESC& desc, float ipd) {
    uint32_t index[2] = {};
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    int acquired = 0;
    for (; acquired < 2; ++acquired) {
        Swapchain& sc = g_swapchains[acquired];
        if (!Check(xrAcquireSwapchainImage(sc.handle, &acquire, &index[acquired]), "xrAcquireSwapchainImage"))
            break;
        if (!Check(xrWaitSwapchainImage(sc.handle, &wait), "xrWaitSwapchainImage")) {
            xrReleaseSwapchainImage(sc.handle, &release);
            break;
        }
    }
    bool ok = acquired == 2;
    if (ok) {
        // A point at distance d moves sideways by (eye offset) * scale / d in clip space, half
        // that in picture widths; the left eye sees it moved right. scale = 1 / tan(half width).
        float scale = 1.0f / std::tan(GameFov().angleRight);
        float shift = 0.25f * ipd * scale * (g_swapEyes ? -1.0f : 1.0f);
        depth3d::Eye eyes[2] = {{g_swapchains[kLeftEye].images[index[kLeftEye]].texture, shift},
                                {g_swapchains[kRightEye].images[index[kRightEye]].texture, -shift}};
        depth3d::DepthModel model;
        camera::ProjectionDepth(model.nearPlane, model.depthScale);
        ID3D11DeviceContext* context = nullptr;
        g_device->GetImmediateContext(&context);
        if (depth3d::Render(g_device, back, g_scFormat, eyes, model)) {
            // The desktop window shows the left eye as the headset gets it.
            if (config::Get().mirrorOneEye && desc.SampleDesc.Count == 1)
                context->CopyResource(back, eyes[depth3d::MirrorEye()].target);
        } else {
            for (const depth3d::Eye& e : eyes) {
                if (desc.SampleDesc.Count > 1)
                    context->ResolveSubresource(e.target, 0, back, 0, g_scFormat);
                else
                    context->CopyResource(e.target, back);
            }
        }
        context->Release();
    }
    for (int e = 0; e < acquired; ++e)
        Check(xrReleaseSwapchainImage(g_swapchains[e].handle, &release), "xrReleaseSwapchainImage");
    return ok;
}

// With the headset idle SteamVR asks for no frames, so nothing is drawn for it. The depth-3D eyes
// are then still made, into textures of our own (g_previewEyes), and one is shown on the desktop:
// the 3D can be checked by screenshot with nobody wearing the headset.
void PreviewDepth3D(ID3D11Texture2D* back, const D3D11_TEXTURE2D_DESC& desc) {
    if (desc.SampleDesc.Count > 1)
        return;
    for (ID3D11Texture2D*& eye : g_previewEyes) {
        if (eye)
            continue;
        D3D11_TEXTURE2D_DESC td = desc;
        td.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        td.MiscFlags = 0;
        td.CPUAccessFlags = 0;
        td.Usage = D3D11_USAGE_DEFAULT;
        if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM || FAILED(g_device->CreateTexture2D(&td, nullptr, &eye))) {
            eye = nullptr;
            return;
        }
    }
    float scale = 1.0f / std::tan(GameFov().angleRight);
    float shift = 0.25f * 0.064f * scale * (g_swapEyes ? -1.0f : 1.0f);  // a typical eye distance
    depth3d::Eye eyes[2] = {{g_previewEyes[kLeftEye], shift}, {g_previewEyes[kRightEye], -shift}};
    depth3d::DepthModel model;
    camera::ProjectionDepth(model.nearPlane, model.depthScale);
    if (!depth3d::Render(g_device, back, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, eyes, model))
        return;
    ID3D11DeviceContext* context = nullptr;
    g_device->GetImmediateContext(&context);
    context->CopyResource(back, g_previewEyes[depth3d::MirrorEye()]);
    context->Release();
}

XrSwapchainSubImage FullImage(const Swapchain& sc) {
    XrSwapchainSubImage sub{};
    sub.swapchain = sc.handle;
    sub.imageRect = {{0, 0}, {static_cast<int32_t>(g_scWidth), static_cast<int32_t>(g_scHeight)}};
    return sub;
}

// The game renders a symmetric frustum from its own FOV setting, not the headset's.
// Telling the compositor exactly what was rendered lets it place the image correctly.
XrFovf GameFov() {
    const config::Settings& s = config::Get();
    float halfV = (s.renderFovDeg > 0.0f ? s.renderFovDeg : s.gameVerticalFovDeg) * 0.5f * kPi / 180.0f;
    float halfH = std::atan(std::tan(halfV) * g_scWidth / g_scHeight);
    return {-halfH, halfH, halfV, -halfV};
}

struct FrameLayers {
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerProjectionView views[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                                 {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    const XrCompositionLayerBaseHeader* list[1] = {};
    uint32_t count = 0;
};

void BuildTheater(ID3D11Texture2D* back, const D3D11_TEXTURE2D_DESC& desc, FrameLayers& out) {
    Swapchain& sc = g_swapchains[kTheater];
    if (!SubmitImage(back, desc, sc))
        return;
    const config::Settings& s = config::Get();
    out.quad.space = g_space;
    out.quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    out.quad.subImage = FullImage(sc);
    out.quad.pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -s.screenDistance}};
    out.quad.size = {s.screenWidth, s.screenWidth * g_scHeight / g_scWidth};
    out.list[0] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&out.quad);
    out.count = 1;
}

// The projection layer: each eye's latest image with the pose it was rendered for.
void BuildProjection(FrameLayers& out) {
    XrFovf fov = GameFov();
    for (int i = 0; i < 2; ++i) {
        out.views[i].pose = g_eyePose[i];
        out.views[i].fov = fov;
        out.views[i].subImage = FullImage(g_swapchains[i]);
    }
    out.projection.space = g_space;
    out.projection.viewCount = 2;
    out.projection.views = out.views;
    out.list[0] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&out.projection);
    out.count = 1;
}

void BuildStereo(ID3D11Texture2D* back, const D3D11_TEXTURE2D_DESC& desc, XrTime displayTime, FrameLayers& out) {
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = displayTime;
    locate.space = g_space;
    XrViewState viewState{XR_TYPE_VIEW_STATE};
    XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    uint32_t viewCount = 0;
    if (!Check(xrLocateViews(g_session, &locate, &viewState, 2, &viewCount, views), "xrLocateViews") ||
        !(viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT))
        return;

    // Alternate eyes only work if the game builds exactly one camera between two Presents: with
    // none, this frame was built for the eye before; with two, one frame was never shown. Count.
    {
        static uint32_t builds[4] = {};
        static ULONGLONG lastLog = 0;
        ++builds[std::min(camera::TakeCameraBuilds(), 3u)];
        ULONGLONG now = GetTickCount64();
        if (now - lastLog >= 1000) {
            lastLog = now;
            Log("Frames: camera builds between two Presents: none %u, one %u, two %u, more %u", builds[0], builds[1],
                builds[2], builds[3]);
            std::fill(std::begin(builds), std::end(builds), 0u);
        }
    }

    // Doubling test (Scroll Lock): frozen, nothing new is shown, so turning the head shows
    // whether the compositor holds a still picture steady; mono, every frame goes to both eyes
    // from between them, so there is no alternating.
    if (g_stereoTest == kTestFreeze) {
        if (g_eyeValid[kLeftEye] && g_eyeValid[kRightEye])
            BuildProjection(out);
        return;
    }
    // This frame belongs to one eye (in the mono test, both); remember the pose it is shown for.
    // The camera is turned with the left eye's orientation for either eye, so both images are
    // handed over with that orientation and their own eye's position: if the headset's eyes look
    // in slightly different directions (angled displays), the right image would otherwise sit off.
    int lag = config::Get().poseLag;
    auto renderPose = [&](int e) {
        const XrPosef* poses = lag > 0 && g_posesKept >= lag
                                   ? g_poseHistory[(g_poseNewest - (lag - 1) + kPoseHistory) % kPoseHistory]
                                   : nullptr;
        XrPosef pose = poses ? poses[e] : views[e].pose;  // lag 0, or not enough frames yet
        pose.orientation = poses ? poses[kLeftEye].orientation : views[kLeftEye].pose.orientation;
        return pose;
    };
    {
        static ULONGLONG lastLog = 0;
        ULONGLONG now = GetTickCount64();
        if (now - lastLog >= 10000) {
            lastLog = now;
            const XrQuaternionf& l = views[kLeftEye].pose.orientation;
            const XrQuaternionf& r = views[kRightEye].pose.orientation;
            float dot = std::fabs(l.x * r.x + l.y * r.y + l.z * r.z + l.w * r.w);
            float angle = 2.0f * std::acos(std::min(dot, 1.0f)) * 180.0f / kPi;
            const XrFovf& fl = views[kLeftEye].fov;
            const XrFovf& fr = views[kRightEye].fov;
            Log("Eyes: %.2f degrees apart in direction; left fov %.1f/%.1f/%.1f/%.1f, right %.1f/%.1f/%.1f/%.1f "
                "(left/right/up/down, degrees)",
                angle, fl.angleLeft * 180.0f / kPi, fl.angleRight * 180.0f / kPi, fl.angleUp * 180.0f / kPi,
                fl.angleDown * 180.0f / kPi, fr.angleLeft * 180.0f / kPi, fr.angleRight * 180.0f / kPi,
                fr.angleUp * 180.0f / kPi, fr.angleDown * 180.0f / kPi);
        }
    }
    const XrVector3f& a = views[kLeftEye].pose.position;
    const XrVector3f& b = views[kRightEye].pose.position;
    float ipd = std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    int eye = g_nextEye;
    bool mono = g_stereoTest == kTestMono;
    bool fromDepth = g_stereoTest == kTestDepth;
    if (fromDepth) {
        if (!SubmitDepth3D(back, desc, ipd))
            return;
        for (int e = 0; e < 2; ++e) {
            g_eyePose[e] = renderPose(e);
            g_eyeValid[e] = true;
        }
    } else {
        for (int e = 0; e < 2; ++e) {
            if (e != eye && !mono)
                continue;
            if (!SubmitImage(back, desc, g_swapchains[e]))
                return;
            g_eyePose[e] = renderPose(e);
            g_eyeValid[e] = true;
        }
        UpdateMirror(back, desc, mono ? kLeftEye : eye);
    }
    g_nextEye = 1 - eye;

    // Alternating: the game's next frame is for the other eye, so its camera is shifted half the
    // eye distance that way. Mono and depth 3D render from between the eyes.
    float side = (g_nextEye == kLeftEye ? -1.0f : 1.0f) * (g_swapEyes ? -1.0f : 1.0f);
    camera::SetEyeOffset(mono || fromDepth ? 0.0f : 0.5f * ipd * side);

    // The look angles (what BF3 aims and fires along) follow the right controller when it's
    // tracked, otherwise the head. With the controller aiming, the render cameras are turned
    // so the picture still shows where the head looks.
    if (config::Get().headTracking) {
        const XrQuaternionf& head = views[kLeftEye].pose.orientation;
        XrPosef hand;
        bool useHand = config::Get().controllerAim && input::AimPose(hand);
        if (useHand != g_aimingWithHand) {
            aim::Reset();  // switching source: don't jump by the difference between them
            g_aimingWithHand = useHand;
            Log("Aiming with %s", useHand ? "the right controller" : "the head");
        }
        float yaw = 0.0f, pitch = 0.0f;
        LookAngles(useHand ? hand.orientation : head, yaw, pitch);
        if (useHand) {
            // BF3 only draws what's in front of where it aims. With the hand far off to one side,
            // what the head looks at isn't drawn at all, so the aim is kept within a limit of the
            // view, like a shoulder that can't swing a rifle behind the player.
            float headYaw = 0.0f, headPitch = 0.0f;
            LookAngles(head, headYaw, headPitch);
            float limit = config::Get().maxAimOffsetDeg * kPi / 180.0f;
            float offset = yaw - headYaw;
            if (offset > kPi)
                offset -= 2 * kPi;
            if (offset < -kPi)
                offset += 2 * kPi;
            yaw = headYaw + std::clamp(offset, -limit, limit);
            pitch = headPitch + std::clamp(pitch - headPitch, -limit, limit);
        }
        aim::Update(yaw, pitch);
        g_yawHistory[g_yawHistoryNext] = yaw;
        int lagged = (g_yawHistoryNext - config::Get().viewLag + kYawHistory) % kYawHistory;
        g_yawHistoryNext = (g_yawHistoryNext + 1) % kYawHistory;
        if (useHand) {
            camera::ViewTurn turn{{head.x, head.y, head.z, head.w}, g_yawHistory[lagged]};
            turn.currentYaw = yaw;
            turn.haveLook = aim::LookYaw(turn.lookYaw, turn.lookSign);
            camera::SetViewTurn(&turn);
        } else {
            camera::SetViewTurn(nullptr);
        }
    }
    g_poseNewest = (g_poseNewest + 1) % kPoseHistory;
    g_poseHistory[g_poseNewest][kLeftEye] = views[kLeftEye].pose;
    g_poseHistory[g_poseNewest][kRightEye] = views[kRightEye].pose;
    g_posesKept = std::min(g_posesKept + 1, kPoseHistory);

    // Both eyes are needed for a projection layer; the very first frame shows nothing.
    if (!g_eyeValid[kLeftEye] || !g_eyeValid[kRightEye])
        return;
    BuildProjection(out);
}

// Where each frame's time goes, logged once a second: the graphics card sat at 37% while the game
// ran at 55 fps, so the frame is waiting somewhere else.
struct FrameTiming {
    LARGE_INTEGER frequency{}, lastFrame{};
    double frame = 0, wait = 0, build = 0, end = 0;
    int frames = 0;
    ULONGLONG lastLog = 0;
    double Ms(const LARGE_INTEGER& a, const LARGE_INTEGER& b) const {
        return 1000.0 * static_cast<double>(b.QuadPart - a.QuadPart) / static_cast<double>(frequency.QuadPart);
    }
} g_timing;

void RenderFrame(IDXGISwapChain* swap) {
    LARGE_INTEGER t0, t1, t2, t3;
    if (!g_timing.frequency.QuadPart)
        QueryPerformanceFrequency(&g_timing.frequency);
    QueryPerformanceCounter(&t0);
    if (g_timing.lastFrame.QuadPart)
        g_timing.frame += g_timing.Ms(g_timing.lastFrame, t0);
    g_timing.lastFrame = t0;

    ID3D11Texture2D* back = nullptr;
    if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&back))))
        return;
    D3D11_TEXTURE2D_DESC desc;
    back->GetDesc(&desc);

    if (g_resizePending || desc.Width != g_scWidth || desc.Height != g_scHeight || desc.Format != g_scSourceFormat) {
        DestroySwapchains();
        CreateSwapchains(desc);
        g_resizePending = false;
    }

    // Blocks until the headset wants a new frame, so the game is paced to the display.
    XrFrameState frame{XR_TYPE_FRAME_STATE};
    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (!Check(xrWaitFrame(g_session, &waitInfo, &frame), "xrWaitFrame") ||
        !Check(xrBeginFrame(g_session, &beginInfo), "xrBeginFrame")) {
        back->Release();
        return;
    }
    QueryPerformanceCounter(&t1);

    if (g_recenterPending)
        Recenter(frame.predictedDisplayTime);
    input::Sync(g_session, g_space, frame.predictedDisplayTime);
    depth3d::Probe();

    FrameLayers layers;
    if (frame.shouldRender && g_scWidth) {
        if (config::Get().mode == config::Mode::Stereo)
            BuildStereo(back, desc, frame.predictedDisplayTime, layers);
        else
            BuildTheater(back, desc, layers);
    } else if (g_scWidth && config::Get().mode == config::Mode::Stereo && g_stereoTest == kTestDepth) {
        PreviewDepth3D(back, desc);
    }

    QueryPerformanceCounter(&t2);
    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frame.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = layers.count;
    endInfo.layers = layers.list;
    Check(xrEndFrame(g_session, &endInfo), "xrEndFrame");

    back->Release();
    QueryPerformanceCounter(&t3);
    g_timing.wait += g_timing.Ms(t0, t1);
    g_timing.build += g_timing.Ms(t1, t2);
    g_timing.end += g_timing.Ms(t2, t3);
    ++g_timing.frames;
    ULONGLONG now = GetTickCount64();
    if (now - g_timing.lastLog >= 1000 && g_timing.frames) {
        double n = g_timing.frames;
        Log("Frame timing (ms per frame): whole frame %.1f, of it waiting for the headset %.1f, our copy %.1f, "
            "handing over %.1f; %d frames",
            g_timing.frame / n, g_timing.wait / n, g_timing.build / n, g_timing.end / n, g_timing.frames);
        g_timing = FrameTiming{g_timing.frequency, g_timing.lastFrame};
        g_timing.lastLog = now;
    }
}

}

namespace vr {

void OnPresent(IDXGISwapChain* swap) {
    // Hotkeys and the camera tools work without a headset.
    PollHotkeys();
    window::Maintain();
    fov::Apply();
    god::Apply();
    skin::Probe();
    camera::Probe();
    aim::Probe();

    if (g_failed || !TryInit(swap))
        return;
    PollEvents();
    if (g_running)
        RenderFrame(swap);
}

void OnResize() {
    g_resizePending = true;
}

}
