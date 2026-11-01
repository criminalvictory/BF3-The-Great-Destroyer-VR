# BF3: The Great Destroyer VR

An OpenXR VR mod for the **Battlefield 3 campaign** (PC, 32-bit `bf3.exe` 1.6.0.0, Steam version).
It injects into the running game and turns it into stereo 3D with head tracking and motion controllers.

**Status: pre-alpha (experimental).** The campaign is playable in true stereo (BF3 draws each eye
itself) with head tracking, the gun in your right hand and a free left hand, but more of what makes
it feel like VR is still being built: moving your head (lean, duck, peek), reloading by hand,
two-handed aiming. It's tested on one setup (Steam `bf3.exe` 1.6.0.0, SteamVR, Steam Frame, RTX
3080); expect rough edges. Every upload is logged in [CHANGELOG.md](CHANGELOG.md).

It becomes **alpha** when: the head's position moves the view, the gun sits in the right hand and
the left hand is free, every campaign weapon aims correctly, it installs from the quick start
without the finder steps, and a whole mission plays through without a crash.

**Campaign only.** Never use it on online servers: injecting into the game there can get your
account banned. This project contains only its own code; it ships no EA files.

## Quick start

1. Start SteamVR and put the headset on its stand (or on).
2. Run `build\injector.exe` and leave it open. It waits for the game.
3. In Battlelog, press **PLAY CAMPAIGN**, then **Resume campaign** in the game's menu.
4. Put the headset on and press **F8** for 3D. **F9** recenters the view.
5. In VR: aim and fire with the right controller, move with the left stick, turn with the right
   stick. God mode is on (End turns it off).

If the picture looks wrong:

- **Ctrl + Scroll Lock** cycles how the 3D is made: per-eye views (normal), depth 3D, hybrid, alternate
  eyes, mono, frozen. If one looks better to you, that's useful to know.
- **Delete** turns the wide view off and on; **Page Up / Page Down** make it wider or narrower.
- The settings files are in the `build` folder: `bf3vr.ini` (general), `depth3d.txt` (the 3D
  tuning, changes apply at once).

## What works

- **Per-eye stereo** (the default): BF3 draws both eyes itself, every frame, from the same game
  state, each from its own camera half your eye distance from the middle of your head: real depth
  everywhere and no halo. It uses a developer setting the retail game still has,
  `GameRenderSettings.SplitScreenTestViewCount`: set to 2 the game draws a second complete view
  beside the first. `src/views.cpp` hooks the function that builds the frame's views and makes them
  the two eyes (the left eye in the left half, the right eye in the right half of a window twice as
  wide), and each eye is shown its half. This is how FrostbiteVR renders Bad Company 2 (a native
  second view), found in BF3's own code. BF3 draws the gun and arms of both views from one camera,
  so the free hands and gun are shifted per eye by the mod (the game's own arms, with free hands
  off, have no depth of their own). Two full views hold the headset's 72 fps with about 6 ms to
  spare a frame (tested in the campaign's opening level).
- **A steady frame rate**: every frame, BF3 asks Windows for the PC's memory statistics
  (`GetPerformanceInfo`), and Windows counts every process and thread on the computer to answer:
  11 ms of the 13.9 ms a frame has at 72 Hz, with SteamVR, a browser and the usual apps open
  (380 processes, 7,800 threads). Made in 2011, it took microseconds then. The mod answers it from
  a copy refreshed once a second (`src/perfinfo.cpp`); with two views the frames no longer missed
  the headset and turning with the stick stopped stuttering.
- **Stereo 3D from depth** (Ctrl + Scroll Lock): every game frame becomes both eyes. Each pixel is shifted
  sideways by the eye's offset over its distance, read from BF3's own depth buffer, so both eyes
  update every frame and head turns no longer double the picture (alternate-eye rendering, one
  frame per eye in turn, did). The gun and arms get their real depth too: BF3 draws them squeezed
  towards the eye and marks them in the stencil buffer, so the mod un-squeezes exactly those
  pixels. Cheaper than per-eye views, but around near things against far ones it shows a soft halo.
  Ctrl + Scroll Lock cycles per-eye views / depth 3D / hybrid / alternate eyes / mono / frozen; tuning is
  in `build\depth3d.txt` (see below).
- **Head tracking**: the head turns the picture, including tilt.
- **Motion controller aiming**: the right controller aims; bullets, muzzle flash and the gun model
  follow it. Buttons and sticks are sent to the game as keyboard and mouse, except the smooth turn:
  in stereo the right stick's turn goes into the look angle the mod writes each frame, which the
  picture follows, so it turns in step with the frames (`[controllers] turn_degrees`, 266° a
  second at full deflection). As a mouse move once a frame it stuttered: BF3 reads the mouse on a
  rhythm of its own, and about 6 times a second a frame barely turned and the next turned double.
  In theater mode the stick still moves the mouse, a little every millisecond.
- **Square window**: in windowed mode the game draws square (default 1600×1600), which gives
  more view top to bottom and a sharper headset image; with per-eye views twice as wide (3200×1600:
  each eye a square half). That may be bigger than the monitor, so the window on the desktop is fitted
  to the monitor instead (borderless, in the shape of the picture it shows) while the game still draws
  at that size: see [Streaming and recording](#streaming-and-recording).
- **Floating screen** (theater) mode as an alternative to stereo.
- **Wide field of view** (Delete on/off, Page Up / Page Down to tune, default 100°): the campaign
  renders a fixed 55° whatever the menu says, which in a headset looks like a square window. The
  mod widens the render, cull and first-person views each frame, and rescales the gun and arms'
  projection where it is uploaded to the graphics card, so they keep their real size. The
  headset image is shown at the same angle, so the world stays life-size.
- **Free hands** (Insert on/off, on by default): the gun is drawn in your right hand, pointing
  where the controller points, held where the game's own hand holds it, with its scope and stock;
  the left glove follows the left controller, its fingers following yours: the left trigger
  curls the index finger, the left grip the other three, and resting your thumb on the stick folds
  the thumb in (all three: a fist; the grip alone: pointing). Relaxed, the fingers are a little
  curved and apart. Each finger bends at its own joints (`src/handpose.cpp`), and three small
  cracks in the glove model that bending opens are filled. The arms, sleeves and body are folded
  away (floating hands). On the Steam Frame, the left View button lines the glove up: the first
  press holds it still, put your hand into it and press again (kept in `build\grip.txt`, with
  "spread 0.7" for how far apart the relaxed fingers are and "leftwidth 1.15" for the palm's
  width). Without one, the line-up made on the test setup's Steam Frame is used. BF3 draws the first-person arms in a space of their own whose origin jumps a metre at a
  time as you walk; each draw places the hands with the eye and axes of the view-projection it is
  drawn with, so they stay steady while moving.
- **Both hands on the rifle**: squeeze the left grip with the left hand at the handguard and the
  glove takes hold there (the game's own grip for this gun) until you let go of the grip. While it
  holds, the gun points from your right hand towards your left, and the game aims along it, as long
  as your left hand is where a handguard could be: with it beside, below or behind the right hand,
  or the hands closer than 10-15 cm, the right hand alone points the gun. The change eases in and
  out over about a tenth of a second, letting go too, and a sudden jump of the left controller's
  tracking can't throw the gun across the view. (Before, the rifle followed the left hand wherever
  it went while the grip was squeezed, and swung round in one jump.)
- **God mode** (End on/off, on by default): the campaign runs the game's own server inside
  `bf3.exe`, and the health that decides deaths is the server's copy of your soldier. The mod finds
  it every spawn and keeps health and maximum health at `[god] value` (100000), so hits are undone
  the next frame and the screen stays clean. Scripted deaths still happen (leaving the area of
  operation fails the mission).
- **Bodies stay** (`[world] corpse_seconds`, 600 here; 0 = the game's own 15 s): raises how long
  dead soldiers lie before the game removes them (tested: none of 12 removed in 3 minutes).
- **Magazine eject** (X, the campaign's M16-type rifle): a tap drops the magazine out of the gun; it
  falls, tumbling, and vanishes at the floor. Its rounds go back to the spare ones and one stays
  in the chamber. A second tap puts a new magazine in; holding X is the game's own reload. The gun
  doesn't fire with the magazine out or one round left (run dry, the game would reload by
  itself). With any other gun, a tap is the game's reload. The ammo is the count in the game's own
  server, which decides firing.
- **Magazines from the pelvis** (the rifle): with the magazine out, or the bolt locked back on an
  empty one, squeeze the left grip with the hand in front of the lower belly (below the belly
  button, above the groin; nothing is drawn there). The gun's own magazine appears in the left
  glove. Bring it up under the magazine well, top first: it's caught below the well's mouth,
  straightened as you push, slides only along the well, and seats when pushed home. Only then do
  its rounds go in. Let go of it on the way and it falls; let go of it at the pelvis and it goes back.
  With any other gun, squeezing there is the game's own reload. `build\pouch.txt` moves and sizes
  the pouch (`zone`), turns it off (`pouch off`), stops X from putting a magazine in (`xinsert 0`)
  or other guns from reloading there (`anygun 0`), and runs a scripted hand for pictures (`test 1`,
  `test 2 -0.05`).
- **No orange enemy markers**: the HUD's orange markers over enemies and targets are taken off at
  the end of each frame (they can't be told from the blue squad markers by their draws, so the
  picture under the HUD is kept and put back wherever the HUD made it more orange); the blue
  markers stay. `build\weapon.txt` `orangemarkers 1` keeps them.
- **The right glove's trigger finger** pulls with the trigger; **the ACOG's reticle and red cable**
  are drawn brighter (`scopeboost`).
- **Spent casings** come out of the drawn gun's ejection port on every shot: each casing's world
  placement is moved on the graphics card with the gun near the port, less as it flies, not at all
  once it's 0.8 m out, so the ones on the ground stay where they fell (`build\weapon.txt`
  `casings 0` leaves them where the game throws them).
- **Reloading by hand** (the rifle): run dry, the bolt locks back and the gun won't fire. With a
  full magazine in, either slap the bolt release on the left of the receiver, behind the magazine
  well, with the left hand (the flat-screen game's empty reload), or put the left hand on the
  charging handle at the back of the receiver's top, hold the left trigger, pull back at least
  4.5 cm and let go. The handle slides back with the hand and springs home. The ejection port's
  cover swings open on its hinge when the bolt moves (a shot, the bolt locked back, the handle
  drawn) and shuts again once it has been still for a moment.
- **Reload sounds**: the magazine coming out, going in, and the bolt going home are the game's own
  sounds, recorded from its empty reload (`capture.exe`) and played by the mod at the gun when the
  hands do the same (`build\sounds\*.wav`). They follow bf3.exe's volume in Windows like the game.
  The clips are the game's, so they aren't in this repository: record your own with the rifle in
  hand at a quiet spot (`capture record 9 take.wav` while `build\weapon.txt` holds `empty`, which
  empties the gun so the game does its empty reload), find the three clicks with `capture loud`
  (about 0.6, 1.5 and 2.2 s after emptying), and cut each with `capture cut` into
  `build\sounds\mag_out.wav`, `mag_in.wav` and `bolt.wav`.

Tested on a Steam Frame through SteamVR's OpenXR runtime with an RTX 3080.

## Known issues

- Per-eye views: the HUD is drawn once across both halves, so the minimap is only in the left eye
  and the ammo count only in the right. Keep `[stereo] async_handover=0`: handing frames to
  SteamVR on a thread of its own switches the graphics device to its locked functions, which go
  round the mod's draw hooks (the gun's FOV fix and free hands stop).
- Depth 3D: around near things against far ones (the gun, a soldier against the sky) the gap the
  other eye would see behind them is filled from the background beside it, which can look like a
  soft halo. `bias -1` / `bias 1` in `depth3d.txt` keeps one eye exactly as the game drew it and
  puts all the filling in the other; hybrid mode (Ctrl + Scroll Lock) alternates which eye is drawn.
- With the wide view, the sleeve near the shoulder can look stretched: that part of BF3's arm
  model is normally off screen.
- Free hands: only the campaign's M16-type rifle is known (other guns get the game's own arms);
  bullets still leave from where the game holds its gun, not the drawn muzzle. A short piece of
  bare forearm shows under the left glove's cuff.
- The muzzle flash and its smoke are drawn where the game holds its own gun (at the chest), not at
  the drawn gun.
- A dark layer shows around the arms, glove and gun, plainest against bright light (being looked
  into: one pass of the arms placed for the wrong eye is the suspect).
- Looking along a forearm into its open elbow end shows a black disc.
- The charging handle has no sound of its own yet (the M16's reloads never use it); letting it
  go plays the bolt going home.

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

## Streaming and recording

In 3D the game draws both eyes side by side, twice as wide as it is tall, at the headset's size
(`[window] size` per eye). Once the headset has its frame, the mod draws over that picture with one
view, the left eye's, so whatever records the game gets a normal-looking picture: 85° across, and
steadied while you wear the headset (small head shakes taken out, the horizon kept level). The
headset's own picture isn't touched. **Home** switches between this view and both eyes (saved; only
while the game is in front).

The game's window on the desktop fills the monitor, borderless, in the view's shape (16:9; 2:1 with
both eyes), and shows the same view: the game still draws at the headset's size, told its window is
that big, and Windows scales the picture into the window. `[window] fit=0` puts back the window as big
as the game draws, bigger than most monitors (the desktop then shows only its top left).

- **OBS and Streamlabs:** a *Game Capture* source, mode *Capture specific window*, window
  *[bf3.exe]: Battlefield 3*, then right-click it → *Transform* → *Stretch to screen* on a 16:9
  canvas. The view is drawn 16:9 into the 2:1 picture, so stretching it puts it back in proportion.
  The mod finishes each frame before any capture hook sees it, so no capture setting needs changing.
- **Discord** (*Go Live*, streaming Battlefield 3): Discord copies the window as Windows shows it, so
  it gets the whole view, 16:9. (With the window bigger than the screen, Windows kept only its part on
  the monitor up to date, and Discord showed that part moving in a corner of an old, frozen picture.)
- **Other recorders that hook the game** (Fraps, Bandicam's game mode) should get the same view. One
  that saves the game's own frame at its size records it 2:1: stretch it to 16:9, or set
  `stream_aspect=0` for a picture drawn in its own shape.
- **Screen, display or window capture** sees the window as it is on the monitor: the whole view.

`[stereo]` in `build\bf3vr.ini`: `stream_view` (1 = on), `stream_fov` (degrees across),
`stream_aspect` (the shape it's drawn for; 0 = the window's own), `stream_fit` (1 = the whole eye's
picture, black at its sides), `stream_steady` (1 = steadied), `stream_tilt` (degrees below where you
look). The subtitles and the HUD's corners sit at the eye's outer edge, so the cropped view leaves
them out; the pause menu is cropped too (Home shows both eyes).

## Controls

| Controller | Action |
|---|---|
| Right trigger | Fire |
| Left trigger | Grabs the rifle's charging handle when the hand is on it (pull back, let go); curls the left index finger. Without free hands: aim down sights (not while the left grip is held) |
| Left stick / click | Move / sprint |
| Right stick | Turn |
| A / B | Jump / crouch |
| X | Magazine: tap to eject / insert, hold to reload |
| Left grip | Curl the left hand's middle, ring and little fingers; near the handguard, hold it; at the pelvis, take a magazine |
| Left thumb on the stick | Fold the left thumb in |
| View (left, Steam Frame) | Line up the left glove with your hand |
| Y | Use |
| Right grip | Holds the gun (nothing in the game; the grenade has no gesture yet) |

On the Steam Frame, A, B, X and Y are all on the right controller; the mod uses SteamVR's own
Frame controller profile (`XR_VALVE_frame_controller_interaction`). On Touch-style controllers X
and Y are on the left.

| Key | Action |
|---|---|
| F8 | Switch floating screen / stereo |
| F9 | Recenter |
| Delete | Wide field of view on / off (saved) |
| Page Up / Page Down | With the wide view on: its angle, 5° a step. Otherwise: zoom of the headset image |
| F1 | Flip which way the gun model follows the hand |
| Ctrl + F4 / Ctrl + F2 | Flip head yaw / pitch direction (saved; not needed: the direction is BF3's) |
| F7 | Swap eyes |
| F11 | How many frames old the head pose each eye image is shown with (0-3, default 1) |
| Insert | Free hands on / off (the game's own arms) |
| End | God mode on / off (saved) |
| Home | Stream view on / off: one eye in the window and OBS, or both eyes (saved) |
| Ctrl + Scroll Lock | 3D method: depth 3D / hybrid (each frame from one eye, the other from its depth) / alternate eyes / mono / frozen |
| Pause | Skinning research hooks on / off (off by default: they cost frame rate) |

`build\hands.txt`: gloves palette numbers ("0-23" left hand, "24-54" right hand, or "all") and
"shift x y z" in metres. On foot the bones' axes are x right, y up, z back towards you.

`build\weapon.txt` (research on the rifle's three draws): "4 shift 0 -0.3 0" moves bones of the
colour pass ("hide" instead of the shift hides them), "pass 1: 0" / "pass 2: 4-5" names the bones
of its other draws (the magazine's rounds, the depth pass), "watch" logs which bones move, "dump"
writes the gun's mesh to `build\gun_*.bin` once, "floor 0.05" sets where a dropped magazine stops.

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
| Ctrl + F3 | Look-angle finder |
| F12 | Gameplay skeleton finder (research: it turned out not to drive the drawn arms) |
| F15, F13 / F14 | Draw hunt, driven by `tools/drawhunt.ps1` (keys no keyboard has): hides half the skinned draws at a time until the one asked about is found |

`tools/selfcheck.ps1` confirms from the log, after a launch, that the hooks, OpenXR session, wide
view, god mode, bodies and the 3D's depth buffer all came up.
`tools/weapontest.ps1` tries weapon.txt settings in turn and puts the game's own picture for each
side by side (saved by the mod, so no screenshot shows an old frame). `tools/videoframes.ps1` pulls
frames out of a recording (Windows' own media components, no ffmpeg) into one sheet.
`handview.exe` (built with the mod, from `tools/handview.cpp`) draws the left glove from its mesh
dump in finger poses, each from four sides, without the game: `handview build out.bmp
0,1,1,1,1,0.7` (index, middle, ring, little, thumb, spread). It reports how deep fingers sink into
each other, whether the depth passes stand out of the glove, and with "a=1" the glove's pieces
and openings; "c=-1" culls back faces as the game does.
`sampler.exe` (built with the mod, from `tools/sampler.cpp`) shows where BF3's threads spend their
time, from outside the game: `sampler 5 10` pauses the 10 busiest a thousand times a second for
5 s and lists, per thread, the code it ran and the system calls it waited in, with the game code
behind them (it found the 11 ms memory statistics call). Thread CPU is read from cycle counts:
Windows' thread times are only charged at clock ticks and make bursty threads look idle.
`capture.exe` records what the PC plays (`capture record 7 take.wav`), shows how loud each 10 ms
was (`loud`), cuts mono clips (`cut take.wav 3.19 0.28 bolt.wav 4.17`, the gain undoing bf3.exe's
volume slider at the time) and lists each app's volume (`sessions`); `playtest.exe bolt` plays a
clip as the mod does. `anglescan.exe` finds the look angles from outside the game (pairs that
match the game camera; `test` nudges each), what points at an address (`refs`), pointer paths
from the soldier (`path`), and the level's ambient occlusion settings (`ao`).
`tools/drawhunt.ps1` presses the hunt keys and game keys (by scan code: the game ignores keys
sent by virtual-key code alone; `-Action fire` holds the mouse button across frames, `f16` is
the magazine button, `f17` turns as the right stick does for 3 s, with the turn's evenness frame
to frame in the log) and takes screenshots while BF3 is in front; `tools/say.ps1`
reads test instructions aloud. Memory tools for campaign research, run with the 32-bit
PowerShell: `peek.ps1`/`peekabs.ps1` (read), `poke.ps1`/`pokeabs.ps1` (write, refusing unless the
expected value is there), `watch.ps1`/`watchabs.ps1`/`treewatch.ps1`/`treediff.ps1` (watch values
change), `findref.ps1`/`findtext.ps1`/`findfloat.ps1` (search), `fields.ps1`/`owner.ps1` (read
BF3's own type tables), `soldiers.ps1`/`corpses.ps1` (the server's soldiers and their bodies),
`findints.ps1`/`pokeint.ps1` (find and set whole numbers such as the ammo), `pathto.ps1` (pointer
paths from an object to an address), `dwords.ps1` (memory as values, pointers into bf3.exe named).
Code research: `dumpimage.ps1` saves bf3.exe as it is in memory (its code is only decrypted there)
to `build\bf3_mem.exe`, which Visual Studio's `dumpbin /disasm /range:...` disassembles;
`findcode.ps1` lists where the code uses an address (a global, a function, a string), with the
instructions around it. `tools/alarm.ps1` rings on every speaker at once (with SteamVR running,
Windows' default output is the headset) to call the tester back when a test is ready.

`CAMERA_GUIDE.md` describes finding the camera by hand with Cheat Engine instead.

## How it works

- `src/d3d11_hook.cpp`: hooks `IDXGISwapChain::Present` to grab each finished frame.
- `src/xr_runtime.cpp`: OpenXR session on the game's own D3D11 device; submits frames as a
  floating quad or as per-eye projection views.
- `src/camera.cpp`: hardware write breakpoints on BF3's camera copies. Right after the game writes
  one, the mod shifts it per eye and turns it to the head.
- `src/aim.cpp`: writes the player's look angles, so the game aims where the controller points.
  It finds them itself from the player's soldier at each life or checkpoint, writes only to an
  object of their type, and of those only to the one that keeps a small nudge (the others are
  copies the game rebuilds every frame).
- `src/input.cpp`: OpenXR actions for the controllers, injected as keyboard and mouse.
- `src/finder.cpp`: in-game memory finders for the camera, look angles and settings.
- `src/window.cpp`: keeps the game drawing square (twice as wide in stereo), past the screen size
  limit, with its window fitted to the monitor and the game told it's still that size.
- `src/skin.cpp`: hooks the bone uploads and draws of skinned meshes; finds the hands' draws and
  draws them with changed bones.
- `src/views.cpp`: per-eye views: BF3's split-screen test turned on, its view builder hooked so
  the two views are the eyes.
- `src/depth3d.cpp`: finds BF3's depth buffer (by its clears) and makes both eyes from the frame
  and its depth in a pixel shader.
- `src/god.cpp`: keeps the health of the server's and the client's copy of the player's soldier.
- `src/bodies.cpp`: keeps dead soldiers' bodies longer.
- `src/ammo.cpp`: reads and sets the server's ammo count for the magazine eject (`skin.cpp` draws
  the magazine falling).
- `src/hands.cpp`: free hands: where the gun and the left glove go relative to the head, from the
  controllers (`skin.cpp` redraws the gun and arms there).
- `src/handpose.cpp`: the left glove's fingers, bent joint by joint (joints and finger directions
  measured from the mesh dump).
- `src/perfinfo.cpp`: answers the game's once-a-frame memory statistics call from a copy.
- `src/reload.cpp`: the rifle's bolt, charging handle, bolt release and ejection port cover, worked
  by the left hand.
- `src/sounds.cpp`: plays the recorded reload sounds (XAudio2, on a thread of its own).
- `src/effects.cpp`: research into the muzzle flash and the casings (drawn where the game holds
  its own gun).
- `injector/main.cpp`: loads `bf3vr.dll` into `bf3.exe`.
