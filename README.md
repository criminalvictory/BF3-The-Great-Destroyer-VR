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
- **Wide field of view** (Delete on/off, Page Up / Page Down to tune, default 100°): the campaign
  renders a fixed 55° whatever the menu says, which in a headset looks like a square window. The
  mod widens the render, cull and first-person views each frame, and rescales the gun and arms'
  projection where it is uploaded to the graphics card, so they keep their real size. The
  headset image is shown at the same angle, so the world stays life-size.
- **Moving the hands** (work in progress, toward free hands and a shoulder holster): the gloves can
  be drawn moved away from the gun (Insert lifts them 20 cm as a test), without touching what BF3
  uploads.
- **God mode** for testing (End), once the health finder (Home) has run.

Tested on a Steam Frame through SteamVR's OpenXR runtime with an RTX 3080.

## Known issues

- The gun moves in small steps while the arm swings. Each eye only sees every second frame
  (about 43 updates a second per eye at 85-90 fps), and SteamVR can smooth head motion but not
  what the hand moves inside the game. A higher frame rate reduces it.
- The crosshair stays in the centre of the view.
- God mode doesn't hold yet: the health value the finder picked isn't the one that counts.
- Turning the head can show a slight double image, more at the wide field of view (lower frame
  rate). Not from the eye order (one camera per frame, measured) or the pose timing (pose lag made
  no difference); Scroll Lock cycles a mono and a frozen test to narrow it down.
- With the wide view, the sleeve near the shoulder can look stretched: that part of BF3's arm
  model is normally off screen.
- With the hands test on, a small dark patch can stay where the left glove was (likely lighting
  or shadow).

Scenery no longer disappears when the controller aims away from where you look: the camera BF3
decides what to draw from is found once with the cull hunt (F10, then F6/F5) and turned with the
head like the render cameras.

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
| Delete | Wide field of view on / off (saved) |
| Page Up / Page Down | With the wide view on: its angle, 5° a step. Otherwise: zoom of the headset image |
| F1 | Flip which way the gun model follows the hand |
| F4 / F2 | Flip head yaw / pitch direction |
| F7 | Swap eyes |
| F11 | How many frames old the head pose each eye image is shown with (0-3, default 1) |
| Insert | Hands test: draw the gloves 20 cm higher / normally (bones chosen in `build\hands.txt`) |
| End | God mode on / off (saved; needs the health finder first) |
| Scroll Lock | Doubling test: normal / mono (same picture to both eyes) / frozen |
| Pause | Skinning research hooks on / off (off by default: they cost frame rate) |

The hand's turn is now read from each camera the game builds, so the picture no longer sways as
the arm moves at any frame rate (the old fixed frame lag is only a fallback).

Finders and tests (needed once per setup; see the log for results):

| Key | Action |
|---|---|
| F10 | Camera finder (look around, turn and walk after the beeps) |
| F6 / F5 | Cull hunt: with the arm held out so scenery is missing, tries one camera copy every 3 s / F5 saves the one that brings it back |
| F3 | Look-angle finder |
| F12 | Gameplay skeleton finder (research: it turned out not to drive the drawn arms) |
| Home | Health finder: press at full health, again after taking damage, again once healed |
| F15, F13 / F14 | Draw hunt, driven by `tools/drawhunt.ps1` (keys no keyboard has): hides half the skinned draws at a time until the one asked about is found |

`tools/drawhunt.ps1` presses the hunt keys and takes screenshots while BF3 is in front;
`tools/say.ps1` reads test instructions aloud.

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
- `src/skin.cpp`: hooks the bone uploads and draws of skinned meshes; finds the hands' draws and
  draws them with changed bones.
- `src/god.cpp`: keeps the player's health topped up.
- `injector/main.cpp`: loads `bf3vr.dll` into `bf3.exe`.
