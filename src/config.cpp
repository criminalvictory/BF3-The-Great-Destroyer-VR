#include "config.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <string>

#include "log.h"

namespace {

config::Settings g_settings;
std::vector<config::CameraPointer> g_cameras;
config::Aim g_aim;
config::Fov g_fov;
config::CameraPointer g_cull;
config::God g_god;
std::mutex g_pointerMutex;

// Keys for pointer n (0-based): base/offsets, then base2/offsets2, ...
std::wstring KeyName(const wchar_t* key, size_t n) {
    return n ? key + std::to_wstring(n + 1) : key;
}

const char kDefaultIni[] =
    "; bf3vr settings. Changes take effect next time the mod is loaded.\n"
    "\n"
    "[general]\n"
    "; theater = flat floating screen, stereo = per-eye projection (alternate-eye rendering)\n"
    "; F8 switches between them in game, F9 recenters.\n"
    "mode=theater\n"
    "\n"
    "[theater]\n"
    "screen_distance=2.5\n"
    "screen_width=3.2\n"
    "\n"
    "[stereo]\n"
    "; Must match the FOV slider in BF3's video options.\n"
    "game_vertical_fov=55\n"
    "; Field of view the mod makes the game render with, in degrees (0 = the game's own).\n"
    "; Delete turns it on and off in game, Page Up / Page Down change it.\n"
    "render_fov=0\n"
    "render_fov_last=90\n"
    "; 1 = the desktop window shows only the left eye instead of flickering between both.\n"
    "mirror_one_eye=1\n"
    "; 1 = the desktop window (and OBS's game capture) shows one view, the left eye's, filling it, instead of\n"
    "; both eyes side by side. Home switches it in game. stream_fov: degrees across. stream_aspect: the shape\n"
    "; it's drawn for, squeezed into the window: 1.7778 (16:9) looks right with OBS's \"stretch to screen\" on a\n"
    "; 16:9 canvas; 0 = the window's own shape. stream_fit: 1 = the whole eye's picture as the headset shows it,\n"
    "; black at its sides. stream_steady: 1 = the head's small shakes and tilt taken out, the horizon level.\n"
    "; stream_tilt: degrees below where you look.\n"
    "stream_view=1\n"
    "stream_fov=85\n"
    "stream_aspect=1.7778\n"
    "stream_fit=0\n"
    "stream_steady=1\n"
    "stream_tilt=0\n"
    "; 1 = hand frames to SteamVR on a thread of their own. Keep 0: it puts the graphics device in\n"
    "; its locked mode, which goes round the mod's draw hooks (the gun's FOV fix and free hands stop).\n"
    "async_handover=0\n"
    "\n"
    "[head]\n"
    "; Stereo mode: head turns and tilts steer the game's view and aim; the mouse turns your body.\n"
    "tracking=1\n"
    "\n"
    "[controllers]\n"
    "; 1 = aim and fire with the right controller; the head then only turns the picture.\n"
    "aim=1\n"
    "; Right stick turning speed at full deflection, in stereo (degrees a second).\n"
    "turn_degrees=266\n"
    "; The same in theater mode, where the stick moves the mouse (counts per 72nd of a second).\n"
    "turn_speed=15\n"
    "; 1 = the left stick walks as a controller's does, in any direction and as fast as it's pushed,\n"
    "; where the head faces; 0 = it presses W/A/S/D.\n"
    "analog_move=1\n"
    "; 1 = swing the gun model the other way (F1 in game) if it moves away from your hand.\n"
    "gun_flip=0\n"
    "; How far (degrees) the aim may stray from where you look. BF3 only draws what's in front\n"
    "; of its aim, so a bigger value means more missing scenery at the edges of your view.\n"
    "max_aim_offset=30\n"
    "; Frames (0-2) the head's view turn lags the hand, to stop the picture shaking while the arm\n"
    "; moves. F11 cycles it in game.\n"
    "view_lag=0\n"
    "\n"
    "[aim]\n"
    "; The player's look angles, found by the look-angle finder (F3). Same pointer format as [camera].\n"
    "; game_camera is the game's own camera, which the finder compares the angles against.\n"
    "game_camera_base=1F8AADC\n"
    "game_camera_offsets=60\n"
    "yaw_base=\n"
    "yaw_offsets=\n"
    "pitch_base=\n"
    "pitch_offsets=\n"
    "; 1 = the angle is stored in degrees rather than radians (the finder sets these).\n"
    "yaw_degrees=0\n"
    "pitch_degrees=0\n"
    "; 1 = flip the direction a head turn/tilt moves the view (F4 = yaw, F2 = pitch in game).\n"
    "invert_yaw=0\n"
    "invert_pitch=0\n"
    "\n"
    "[window]\n"
    "; In windowed mode, keep BF3's window this many pixels square. Square gives the most view\n"
    "; top to bottom; bigger is sharper in the headset. It may be bigger than the monitor.\n"
    "; 0 = leave the window alone.\n"
    "size=1600\n"
    "; 1 = the window fills the monitor (borderless, in the shape of the picture it shows) and the game still\n"
    "; draws at size for the headset, scaled into it: the desktop, Discord and window captures show the whole\n"
    "; picture. 0 = the window is as big as the game draws, bigger than most monitors (the desktop shows its\n"
    "; top left only).\n"
    "fit=1\n"
    "; 1 = the desktop window waits for the monitor's refresh as the game asks; 0 = it doesn't\n"
    "; (the headset sets the pace in VR, and waiting capped the game near 48 fps).\n"
    "vsync=0\n"
    "\n"
    "[god]\n"
    "; Campaign only. Keeps the player's health and maximum health at value; End toggles.\n"
    "enabled=1\n"
    "value=100000\n"
    "\n"
    "[world]\n"
    "; Campaign: seconds a dead soldier's body stays (0 = the game's own, 15 for most soldiers).\n"
    "corpse_seconds=0\n"
    "\n"
    "[fov]\n"
    "; BF3's FOV setting, found by the FOV finder (Home in game), so it can go past the menu's 90.\n"
    "; value is in the menu slider's units; End cycles it 90-130 in game. 0 = don't override.\n"
    "value=110\n"
    "\n"
    "[camera]\n"
    "; Pointers to the render camera, which gets the per-eye shift. The camera finder (F10) and\n"
    "; copy test (F6/F11/F5) fill these in; see CAMERA_GUIDE.md.\n"
    "; base is the hex offset from bf3.exe; offsets are hex values separated by commas.\n"
    "; base2/offsets2 .. base4/offsets4 are extra copies of the camera that get the same eye shift.\n"
    "base=\n"
    "offsets=\n";

std::wstring g_path;

std::wstring ReadString(const wchar_t* section, const wchar_t* key) {
    wchar_t buf[512] = {};
    GetPrivateProfileStringW(section, key, L"", buf, 512, g_path.c_str());
    return buf;
}

float ReadFloat(const wchar_t* section, const wchar_t* key, float fallback) {
    std::wstring s = ReadString(section, key);
    return s.empty() ? fallback : static_cast<float>(_wtof(s.c_str()));
}

bool ReadBool(const wchar_t* section, const wchar_t* key, bool fallback) {
    return ReadFloat(section, key, fallback ? 1.0f : 0.0f) != 0.0f;
}

void WriteString(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(section, key, value.c_str(), g_path.c_str());
}

config::CameraPointer ReadPointer(const wchar_t* section, const std::wstring& baseKey,
                                  const std::wstring& offsetsKey) {
    config::CameraPointer pointer;
    pointer.base = wcstoul(ReadString(section, baseKey.c_str()).c_str(), nullptr, 16);
    if (!pointer.base)
        return pointer;
    std::wstring offsets = ReadString(section, offsetsKey.c_str());
    for (const wchar_t* p = offsets.c_str(); *p;) {
        wchar_t* end = nullptr;
        unsigned long v = wcstoul(p, &end, 16);
        if (end == p)
            break;
        pointer.offsets.push_back(v);
        p = end;
        while (*p == L',' || *p == L' ')
            ++p;
    }
    return pointer;
}

// An unset pointer (base 0) is written as empty keys.
void WritePointer(const wchar_t* section, const std::wstring& baseKey, const std::wstring& offsetsKey,
                  const config::CameraPointer& pointer) {
    std::wstring base, offsets;
    if (pointer.base) {
        wchar_t buf[32];
        swprintf_s(buf, L"%X", static_cast<unsigned>(pointer.base));
        base = buf;
        for (uintptr_t o : pointer.offsets) {
            swprintf_s(buf, L"%X", static_cast<unsigned>(o));
            if (!offsets.empty())
                offsets += L",";
            offsets += buf;
        }
    }
    WriteString(section, baseKey.c_str(), base);
    WriteString(section, offsetsKey.c_str(), offsets);
}

}

namespace config {

void Load(HMODULE self) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(self, path, MAX_PATH);
    g_path = path;
    g_path = g_path.substr(0, g_path.find_last_of(L"\\/") + 1) + L"bf3vr.ini";

    if (GetFileAttributesW(g_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, g_path.c_str(), L"w") == 0 && f) {
            fputs(kDefaultIni, f);
            fclose(f);
            Log("Wrote default bf3vr.ini");
        }
    }

    Settings& s = g_settings;
    s.mode = _wcsicmp(ReadString(L"general", L"mode").c_str(), L"stereo") == 0 ? Mode::Stereo : Mode::Theater;
    s.screenDistance = ReadFloat(L"theater", L"screen_distance", s.screenDistance);
    s.screenWidth = ReadFloat(L"theater", L"screen_width", s.screenWidth);
    s.gameVerticalFovDeg = ReadFloat(L"stereo", L"game_vertical_fov", s.gameVerticalFovDeg);
    s.renderFovDeg = ReadFloat(L"stereo", L"render_fov", s.renderFovDeg);
    s.renderFovLastDeg = ReadFloat(L"stereo", L"render_fov_last", s.renderFovLastDeg);
    s.headTracking = ReadBool(L"head", L"tracking", true);
    s.mirrorOneEye = ReadBool(L"stereo", L"mirror_one_eye", true);
    s.streamView = ReadBool(L"stereo", L"stream_view", s.streamView);
    s.streamFovDeg = ReadFloat(L"stereo", L"stream_fov", s.streamFovDeg);
    s.streamAspect = ReadFloat(L"stereo", L"stream_aspect", s.streamAspect);
    s.streamFit = ReadBool(L"stereo", L"stream_fit", s.streamFit);
    s.streamSteady = ReadBool(L"stereo", L"stream_steady", s.streamSteady);
    s.streamTiltDeg = ReadFloat(L"stereo", L"stream_tilt", s.streamTiltDeg);
    s.controllerAim = ReadBool(L"controllers", L"aim", true);
    s.turnDegrees = ReadFloat(L"controllers", L"turn_degrees", s.turnDegrees);
    s.analogMove = ReadFloat(L"controllers", L"analog_move", s.analogMove ? 1.0f : 0.0f) != 0.0f;
    s.turnSpeed = ReadFloat(L"controllers", L"turn_speed", s.turnSpeed);
    s.gunFlip = ReadBool(L"controllers", L"gun_flip", false);
    s.maxAimOffsetDeg = ReadFloat(L"controllers", L"max_aim_offset", s.maxAimOffsetDeg);
    s.viewLag = std::clamp(static_cast<int>(ReadFloat(L"controllers", L"view_lag", 0.0f)), 0, 2);
    s.poseLag = std::clamp(static_cast<int>(ReadFloat(L"stereo", L"pose_lag", 1.0f)), 0, 3);
    s.corpseSeconds = std::max(ReadFloat(L"world", L"corpse_seconds", 0.0f), 0.0f);
    s.asyncHandover = ReadBool(L"stereo", L"async_handover", false);
    s.desktopVsync = ReadBool(L"window", L"vsync", false);
    s.windowSize = static_cast<int>(ReadFloat(L"window", L"size", static_cast<float>(s.windowSize)));
    s.windowFit = ReadBool(L"window", L"fit", s.windowFit);

    std::vector<CameraPointer> cameras;
    for (size_t n = 0; n < kMaxCameras; ++n) {
        CameraPointer camera = ReadPointer(L"camera", KeyName(L"base", n), KeyName(L"offsets", n));
        if (camera.base)
            cameras.push_back(camera);
    }

    Aim aim;
    aim.gameCamera = ReadPointer(L"aim", L"game_camera_base", L"game_camera_offsets");
    if (!aim.gameCamera.base)  // ini from before [aim] existed
        aim.gameCamera = {0x1F8AADC, {0x60}};
    aim.yaw = ReadPointer(L"aim", L"yaw_base", L"yaw_offsets");
    aim.pitch = ReadPointer(L"aim", L"pitch_base", L"pitch_offsets");
    aim.yawDegrees = ReadBool(L"aim", L"yaw_degrees", false);
    aim.pitchDegrees = ReadBool(L"aim", L"pitch_degrees", false);
    aim.invertYaw = ReadBool(L"aim", L"invert_yaw", false);
    aim.invertPitch = ReadBool(L"aim", L"invert_pitch", false);
    if (aim.invertYaw)  // a stray key once saved it so: the game aims opposite the gun, the view swings with it
        Log("Config: [aim] invert_yaw=1: the hand's yaw goes in as it came, not negated: the game aims the other "
            "way from the gun and the view turns with the gun. Ctrl + F4 flips it back");

    Fov fov;
    fov.value = ReadFloat(L"fov", L"value", fov.value);
    for (size_t n = 0; n < kMaxFovTargets; ++n) {
        Fov::Target target;
        target.pointer = ReadPointer(L"fov", KeyName(L"base", n), KeyName(L"offsets", n));
        target.encoding = static_cast<int>(ReadFloat(L"fov", KeyName(L"encoding", n).c_str(), 0.0f));
        if (target.pointer.base)
            fov.targets.push_back(target);
    }

    CameraPointer cull = ReadPointer(L"camera", L"cull_base", L"cull_offsets");

    God god;
    god.enabled = ReadBool(L"god", L"enabled", true);
    god.value = ReadFloat(L"god", L"value", god.value);

    {
        std::lock_guard lock(g_pointerMutex);
        g_cameras = cameras;
        g_aim = aim;
        g_fov = fov;
        g_cull = cull;
        g_god = god;
    }

    Log("Config: mode=%s fov=%.1f camera pointers=%u aim=%s", s.mode == Mode::Stereo ? "stereo" : "theater",
        s.gameVerticalFovDeg, static_cast<unsigned>(cameras.size()),
        aim.yaw.base && aim.pitch.base ? "set" : "not found yet (F3)");
}

const Settings& Get() {
    return g_settings;
}

Settings& Mutable() {
    return g_settings;
}

void SaveFov() {
    wchar_t value[32];
    swprintf_s(value, L"%.0f", g_settings.gameVerticalFovDeg);
    WriteString(L"stereo", L"game_vertical_fov", value);
}

void SaveRenderFov() {
    wchar_t value[32];
    swprintf_s(value, L"%.0f", g_settings.renderFovDeg);
    WriteString(L"stereo", L"render_fov", value);
    swprintf_s(value, L"%.0f", g_settings.renderFovLastDeg);
    WriteString(L"stereo", L"render_fov_last", value);
}

std::vector<CameraPointer> GetCameras() {
    std::lock_guard lock(g_pointerMutex);
    return g_cameras;
}

CameraPointer GetCamera() {
    std::lock_guard lock(g_pointerMutex);
    return g_cameras.empty() ? CameraPointer{} : g_cameras[0];
}

void SetCameras(const std::vector<CameraPointer>& pointers) {
    {
        std::lock_guard lock(g_pointerMutex);
        g_cameras = pointers;
        if (g_cameras.size() > kMaxCameras)
            g_cameras.resize(kMaxCameras);
    }
    for (size_t n = 0; n < kMaxCameras; ++n)
        WritePointer(L"camera", KeyName(L"base", n), KeyName(L"offsets", n),
                     n < pointers.size() ? pointers[n] : CameraPointer{});
}

void SetCamera(const CameraPointer& pointer) {
    SetCameras({pointer});
}

Aim GetAim() {
    std::lock_guard lock(g_pointerMutex);
    return g_aim;
}

void SetAim(const Aim& aim) {
    {
        std::lock_guard lock(g_pointerMutex);
        g_aim = aim;
    }
    WritePointer(L"aim", L"game_camera_base", L"game_camera_offsets", aim.gameCamera);
    WritePointer(L"aim", L"yaw_base", L"yaw_offsets", aim.yaw);
    WritePointer(L"aim", L"pitch_base", L"pitch_offsets", aim.pitch);
    WriteString(L"aim", L"yaw_degrees", aim.yawDegrees ? L"1" : L"0");
    WriteString(L"aim", L"pitch_degrees", aim.pitchDegrees ? L"1" : L"0");
    WriteString(L"aim", L"invert_yaw", aim.invertYaw ? L"1" : L"0");
    WriteString(L"aim", L"invert_pitch", aim.invertPitch ? L"1" : L"0");
}

God GetGod() {
    std::lock_guard lock(g_pointerMutex);
    return g_god;
}

void SetGod(const God& god) {
    {
        std::lock_guard lock(g_pointerMutex);
        g_god = god;
    }
    WriteString(L"god", L"enabled", god.enabled ? L"1" : L"0");
    wchar_t buf[32];
    swprintf_s(buf, L"%.0f", god.value);
    WriteString(L"god", L"value", buf);
}

CameraPointer GetCull() {
    std::lock_guard lock(g_pointerMutex);
    return g_cull;
}

void SetCull(const CameraPointer& pointer) {
    {
        std::lock_guard lock(g_pointerMutex);
        g_cull = pointer;
    }
    WritePointer(L"camera", L"cull_base", L"cull_offsets", pointer);
}

Fov GetFov() {
    std::lock_guard lock(g_pointerMutex);
    return g_fov;
}

void SetFov(const Fov& fov) {
    {
        std::lock_guard lock(g_pointerMutex);
        g_fov = fov;
    }
    wchar_t buf[32];
    swprintf_s(buf, L"%.0f", fov.value);
    WriteString(L"fov", L"value", buf);
    for (size_t n = 0; n < kMaxFovTargets; ++n) {
        bool set = n < fov.targets.size();
        WritePointer(L"fov", KeyName(L"base", n), KeyName(L"offsets", n),
                     set ? fov.targets[n].pointer : CameraPointer{});
        swprintf_s(buf, L"%d", set ? fov.targets[n].encoding : 0);
        WriteString(L"fov", KeyName(L"encoding", n).c_str(), set ? buf : L"");
    }
}

void SavePoseLag() {
    WriteString(L"stereo", L"pose_lag", std::to_wstring(g_settings.poseLag));
}

void SaveStreamView() {
    WriteString(L"stereo", L"stream_view", g_settings.streamView ? L"1" : L"0");
}

void SaveViewLag() {
    WriteString(L"controllers", L"view_lag", std::to_wstring(g_settings.viewLag));
}

void SaveGunFlip() {
    WriteString(L"controllers", L"gun_flip", g_settings.gunFlip ? L"1" : L"0");
}

}
