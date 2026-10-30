# BF3: The Great Destroyer VR

An OpenXR VR mod for the **Battlefield 3 campaign** (PC, 32-bit `bf3.exe` 1.6.0.0, Steam version).
It injects into the running game and turns it into stereo 3D with head tracking and motion controllers.

**Campaign only.** Never use it on online servers: injecting into the game there can get your
account banned. This project contains only its own code; it ships no EA files.

## What works

- **Stereo 3D** by alternate-eye rendering: each game frame is drawn for one eye in turn, with the
  camera shifted by your real eye distance.
- **Head tracking**: the head turns the picture, including tilt.
- **Motion controller aiming**: the right controller aims; bullets, muzzle flash and the gun model
  follow it. Buttons and sticks are sent to the game as keyboard and mouse.
- **Square window**: in windowed mode the game is kept square (default 1600×1600), which gives
  more view top to bottom and a sharper headset image. It may be bigger than the monitor.
- **Floating screen** (theater) mode as an alternative to stereo.

Tested on a Steam Frame through SteamVR's OpenXR runtime with an RTX 3080.

## Known issues

- Scenery can disappear at the edges when the controller aims far away from where you look:
  BF3 only draws what's in front of its aim. `max_aim_offset` limits this; a cull-camera hunt
  (F6) is in progress to fix it properly.
- The crosshair stays in the centre of the view.

## Building

Needs Visual Studio with the C++ workload (it brings CMake and Ninja). Then run:

```
build.bat
```

This fetches the OpenXR SDK and MinHook and produces `build\bf3vr.dll` and `build\injector.exe`.

## Running

1. Start SteamVR.
2. Run `build\injector.exe`. It waits for the game.
3. Start the campaign from Battlelog. The mod loads a few seconds after `bf3.exe` starts.
4. Settings live in `build\bf3vr.ini` (created on first run); the log is `build\bf3vr.log`.

The mod fixes up a Steam quirk on its own: games launched while SteamVR runs get the 64-bit
runtime path, which a 32-bit game can't load.

## Controls

| Controller | Action |
|---|---|
| Right trigger | Fire |
| Left trigger | Aim down sights |
| Left stick / click | Move / sprint |
| Right stick | Turn |
| A / B | Jump / crouch |
| X / Y | Reload / use |
| Right grip | Grenade |

| Key | Action |
|---|---|
| F8 | Switch floating screen / stereo |
| F9 | Recenter |
| Page Up / Page Down | Zoom of the headset image (make things life-size) |
| F1 | Flip which way the gun model follows the hand |
| F4 / F2 | Flip head yaw / pitch direction |
| F7 | Swap eyes |

Finders and tests (needed once per setup; see the log for results):

| Key | Action |
|---|---|
| F10 | Camera finder (look around, turn and walk after the beeps) |
| F6 / F5 | Hunt for the cull camera / save the one being tried |
| F3 | Look-angle finder |
| F11 | Shift test: moves the camera 1 m sideways |
| Home / End | FOV finder (slider 90 → 60 → 90) / step the FOV override |

`CAMERA_GUIDE.md` describes finding the camera by hand with Cheat Engine instead.

## How it works

- `src/d3d11_hook.cpp`: hooks `IDXGISwapChain::Present` to grab each finished frame.
- `src/xr_runtime.cpp`: OpenXR session on the game's own D3D11 device; submits frames as a
  floating quad or as per-eye projection views.
- `src/camera.cpp`: hardware write breakpoints on BF3's camera copies. Right after the game writes
  one, the mod shifts it per eye and turns it to the head.
- `src/aim.cpp`: writes the player's look angles, so the game aims where the controller points.
- `src/input.cpp`: OpenXR actions for the controllers, injected as keyboard and mouse.
- `src/finder.cpp`: in-game memory finders for the camera, look angles and settings.
- `src/window.cpp`: keeps the game window square, past the screen size limit.
- `injector/main.cpp`: loads `bf3vr.dll` into `bf3.exe`.
