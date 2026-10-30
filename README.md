# BF3: The Great Destroyer VR

An OpenXR VR mod for the **Battlefield 3 campaign** (PC, 32-bit `bf3.exe` 1.6.0.0, Steam version).
It injects into the running game and turns it into stereo 3D with head tracking and motion controllers.

**Campaign only.** Never use it on online servers: injecting into the game there can get your
account banned. This project contains only its own code; it ships no EA files.

## What works

- **Stereo 3D from depth** (the default): every game frame becomes both eyes. Each pixel is shifted
  sideways by the eye's offset over its distance, read from BF3's own depth buffer, so both eyes
  update every frame and head turns no longer double the picture (alternate-eye rendering, one
  frame per eye in turn, did). The gun and arms get their real depth too: BF3 draws them squeezed
  towards the eye and marks them in the stencil buffer, so the mod un-squeezes exactly those
  pixels. Scroll Lock cycles depth 3D / hybrid / alternate eyes / mono / frozen; tuning is in
  `build\depth3d.txt` (see below).
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
- **God mode** (End on/off, on by default): the campaign runs the game's own server inside
  `bf3.exe`, and the health that decides deaths is the server's copy of your soldier. The mod finds
  it every spawn and keeps health and maximum health at `[god] value` (100000), so hits are undone
  the next frame and the screen stays clean. Scripted deaths still happen (leaving the area of
  operation fails the mission).
- **Bodies stay** (`[world] corpse_seconds`, 600 here; 0 = the game's own 15 s): raises how long
  dead soldiers lie before the game removes them.

Tested on a Steam Frame through SteamVR's OpenXR runtime with an RTX 3080.

## Known issues

- Depth 3D: around near things against far ones (the gun, a soldier against the sky) the gap the
  other eye would see behind them is filled from the background beside it, which can look like a
  soft halo. `bias -1` / `bias 1` in `depth3d.txt` keeps one eye exactly as the game drew it and
  puts all the filling in the other; hybrid mode (Scroll Lock) alternates which eye is drawn.
- The crosshair stays in the centre of the view (its settings are known: each weapon's HudData,
  CrosshairOpacityMin/Max; reaching them in memory is not done yet).
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
| Insert | Hands test: draw the gloves moved (bones and direction in `build\hands.txt`) / normally |
| End | God mode on / off (saved) |
| Scroll Lock | 3D method: depth 3D / hybrid (each frame from one eye, the other from its depth) / alternate eyes / mono / frozen |
| Pause | Skinning research hooks on / off (off by default: they cost frame rate) |

`build\hands.txt`: gloves palette numbers ("0-23" left hand, "24-54" right hand, or "all") and
"shift x y z" in metres. On foot the bones' axes are x right, y up, z back towards you.

### Depth 3D tuning: `build\depth3d.txt`

Read a few times a second while the game runs, e.g.
`strength 0.7 min 0.3 gun 2.4 mask 32 bias 0 fill 1 debug 0 show 0 hold 0`:

| Word | Meaning |
|---|---|
| strength | share of your real eye distance used (1 = full) |
| min | nearest distance in metres anything counts as |
| gun | the gun and arms' distances are multiplied by this (1 = off) |
| mask | stencil bits marking the gun and arms (BF3 uses 32; 0 = use "below" metres instead) |
| bias | -1 / 1: the left / right eye gets the game's own picture, the other eye all the shift |
| fill | gaps behind near things: 0 stretched, 1 mirrored, 2 mirrored and softer |
| debug | 1 distance in coloured bands, 2 distance as grey (white = 2 m), 3 the stencil |
| show | 1 = the desktop window shows the right eye |
| hold | 1 = freeze the picture (both eyes keep being made from it) to compare settings |
| save N | when N changes, both eyes are written to `build\eye_left.bmp` / `eye_right.bmp` |

`tools\eyebmp.ps1` turns the saved eyes into PNGs and reads distances or stencil values from them.

The hand's turn is now read from each camera the game builds, so the picture no longer sways as
the arm moves at any frame rate (the old fixed frame lag is only a fallback).

Finders and tests (needed once per setup; see the log for results):

| Key | Action |
|---|---|
| F10 | Camera finder (look around, turn and walk after the beeps) |
| F6 / F5 | Cull hunt: with the arm held out so scenery is missing, tries one camera copy every 3 s / F5 saves the one that brings it back |
| F3 | Look-angle finder |
| F12 | Gameplay skeleton finder (research: it turned out not to drive the drawn arms) |
| Home / F16 | Old health finders (superseded by the server-side god mode) |
| F15, F13 / F14 | Draw hunt, driven by `tools/drawhunt.ps1` (keys no keyboard has): hides half the skinned draws at a time until the one asked about is found |

`tools/drawhunt.ps1` presses the hunt keys and game keys (by scan code: the game ignores keys
sent by virtual-key code alone) and takes screenshots while BF3 is in front; `tools/say.ps1`
reads test instructions aloud. Memory tools for campaign research, run with the 32-bit
PowerShell: `peek.ps1`/`peekabs.ps1` (read), `poke.ps1`/`pokeabs.ps1` (write, refusing unless the
expected value is there), `watch.ps1`/`watchabs.ps1`/`treewatch.ps1`/`treediff.ps1` (watch values
change), `findref.ps1`/`findtext.ps1`/`findfloat.ps1` (search), `fields.ps1`/`owner.ps1` (read
BF3's own type tables), `soldiers.ps1`/`corpses.ps1` (the server's soldiers and their bodies).

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
- `src/depth3d.cpp`: finds BF3's depth buffer (by its clears) and makes both eyes from the frame
  and its depth in a pixel shader.
- `src/god.cpp`: keeps the health of the server's and the client's copy of the player's soldier.
- `src/bodies.cpp`: keeps dead soldiers' bodies longer.
- `injector/main.cpp`: loads `bf3vr.dll` into `bf3.exe`.
