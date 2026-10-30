#pragma once
#include <windows.h>

#include <cstdint>
#include <vector>

namespace config {

enum class Mode { Theater, Stereo };

struct Settings {
    Mode mode = Mode::Theater;

    // Theater screen placement, in metres.
    float screenDistance = 2.5f;
    float screenWidth = 3.2f;

    // Vertical FOV the game renders with (BF3's FOV slider), used to tell the
    // compositor what each eye image covers.
    float gameVerticalFovDeg = 55.0f;

    // Stereo mode: show only the left eye in the desktop window, so it doesn't flicker
    // between the eyes. The headset is unaffected.
    bool mirrorOneEye = true;

    // Stereo mode head tracking: head turns and tilts are added to the player's look angles,
    // so the game looks (and aims) where the head looks. The mouse still turns the body.
    bool headTracking = true;

    // Stereo mode: aim and fire with the right controller instead of the head; the head then
    // only turns the picture.
    bool controllerAim = true;
    // Right stick turning: mouse counts per frame at full deflection.
    float turnSpeed = 15.0f;
    // Which way the gun model is swung to follow the hand (F1 flips it).
    bool gunFlip = false;
    // How far the controller's aim may stray from where the head looks, in degrees. BF3 only
    // draws what's in front of its aim, so further means more missing scenery.
    float maxAimOffsetDeg = 30.0f;
    // Frames by which the view turn lags the hand, to match when BF3 builds its camera (F11).
    int viewLag = 0;

    // Windowed mode: keep BF3's window this many pixels square (0 = leave it alone).
    int windowSize = 1600;
};

// Cheat Engine style pointer chain: address = bf3.exe + base, then for each offset:
// address = [address] + offset. base == 0 means not set.
struct CameraPointer {
    uintptr_t base = 0;
    std::vector<uintptr_t> offsets;
};

// The player's look angles, found by the look-angle finder (F3).
struct Aim {
    // The game's own camera (not the render copies): the finder reads its forward vector to
    // recognise the angles. Defaults to the one found for bf3.exe 1.6.0.0.
    CameraPointer gameCamera;
    CameraPointer yaw;
    CameraPointer pitch;
    bool yawDegrees = false;
    bool pitchDegrees = false;
    // Which way a head turn moves each angle; F4 / F2 flip them in game.
    bool invertYaw = false;
    bool invertPitch = false;
};

// BF3's FOV setting, found by the FOV finder (Home), so the mod can set it past the menu's 90.
struct Fov {
    // How a value is stored: 0 = float degrees, 1 = float radians, 2 = float half-angle degrees,
    // 3 = float half-angle radians, 4 = int degrees.
    struct Target {
        CameraPointer pointer;
        int encoding = 0;
    };
    std::vector<Target> targets;
    // The FOV to set, in the same units as BF3's menu slider. 0 = leave BF3's own value.
    float value = 110.0f;
};

// God mode (campaign only): pointers to the player's health, found by the health finder (Home x3).
struct God {
    std::vector<CameraPointer> health;
    bool enabled = true;
    float value = 100.0f;  // what health is kept at
};

// Reads bf3vr.ini next to the mod DLL, writing a default one if it doesn't exist.
void Load(HMODULE self);
const Settings& Get();
Settings& Mutable();
// Writes the current gameVerticalFovDeg to bf3vr.ini (tuned in game with Page Up / Page Down).
void SaveFov();
// Writes the current gunFlip to bf3vr.ini (F1 in game).
void SaveGunFlip();
// Writes the current viewLag to bf3vr.ini (F11 in game).
void SaveViewLag();

// Up to four camera pointers: BF3 keeps several copies of the view (renderer, weapon, ...) and
// the eye shift goes to all of them. The first is the one the camera probe logs.
// They can be replaced at any time by the finder threads, so they're kept separately.
constexpr size_t kMaxCameras = 4;
std::vector<CameraPointer> GetCameras();
CameraPointer GetCamera();
// Replaces the camera pointers and writes them to bf3vr.ini.
void SetCameras(const std::vector<CameraPointer>& pointers);
void SetCamera(const CameraPointer& pointer);

// The camera copy BF3 decides what to draw from (found with the F6 hunt). It gets the same head
// turn as the render cameras, so scenery isn't skipped when the hand aims away from the gaze.
CameraPointer GetCull();
void SetCull(const CameraPointer& pointer);

// Look angles: read and replace (written to bf3vr.ini).
Aim GetAim();
void SetAim(const Aim& aim);

// God mode: read and replace (written to bf3vr.ini).
God GetGod();
void SetGod(const God& god);

// FOV override: read and replace (written to bf3vr.ini).
constexpr size_t kMaxFovTargets = 8;
Fov GetFov();
void SetFov(const Fov& fov);

}
