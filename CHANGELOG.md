# Changelog

Every upload to GitHub gets an entry here: what changed since the last one, and the stage the
project is at. Stages: pre-alpha, alpha, beta, release. A change of stage is also marked as a
GitHub release.

## 2026-10-30, night: smooth turning and reloading by hand (stage: pre-alpha, experimental)

4 commits since the last upload. Turning is smooth, the rifle is reloaded by hand with the game's
own sounds, and the left glove keeps the user's line-up. Tested in the headset by the user
(turning: "super smooth"; the reloads, the cover and walking: "it seems okay").

### New
- **Reloading by hand** (the rifle): run dry, the bolt locks back and the gun won't fire. With a
  full magazine in, slap the bolt release on the left of the receiver (the flat-screen game's empty
  reload), or put the left hand on the charging handle, hold the left trigger, pull back and let
  go: the handle slides back with the hand and springs home. The ejection port's cover swings open
  on its hinge when the bolt moves. With free hands the left trigger works the handle instead of
  aiming down sights (the scope is looked through by holding the gun up to the eye).
- **The game's own reload sounds**: the magazine coming out, going in, and the bolt going home,
  recorded from the game's empty reload and played at the gun when the hands do the same. They
  follow bf3.exe's volume in Windows like the game. They aren't in the repository (they're the
  game's): `capture.exe` records them; the README says how.
- **The left glove's line-up** made on the test setup is built in, so a new install starts with
  the glove where the hand is.
- Tools: `sampler.exe` (where the game's threads spend their time, from outside), `capture.exe`
  (records what the PC plays, finds and cuts clips, lists app volumes), `playtest.exe` (plays a
  reload sound as the mod does), `anglescan.exe` (finds the look angles and pointer paths from
  outside the game).

### Fixed
- **Smooth turning**: every frame BF3 asked Windows for memory statistics, which took 11 ms with
  hundreds of processes running and dropped the game below the headset's 72 fps. The mod answers
  from a copy refreshed once a second: a steady 72 fps. The right stick's turn now goes into the
  look angle the mod writes each frame instead of the mouse, so each frame turns the same amount.
- **Walking backwards**: at a new checkpoint the pointer to the player's look angles led to other
  memory (the game's facing stopped following the head, so forward could walk backwards, and the
  mod wrote over a pointer there). The mod now finds the angles from the player's soldier at every
  life or checkpoint, writes only to an object of their type, and of those only to the one that
  keeps a small nudge.

### Known issues
- The ejection port's cover is slow between shots.
- The ACOG's reticle and lens tint sometimes vanish from the scope and float in the distance.
- A dark layer shows around the arms, glove and gun against bright light.
- The muzzle flash, its smoke and the casings come from where the game holds its own gun (the
  chest), not the drawn gun.
- The charging handle has no sound of its own (the M16's reloads never use it).
- The HUD is drawn once across both eyes (the minimap in the left eye, the ammo count in the right).

## 2026-10-30, evening: per-eye rendering and a real left hand (stage: pre-alpha, experimental)

2 commits since the last upload. The 3D is now made by the game itself, and the left glove has
working fingers. Tested in the headset by the user ("this is amazing... beautiful").

### New
- **Per-eye stereo** (the new default): BF3 draws both eyes itself every frame, from the same game
  state, each from its own camera half the eye distance from the middle of the head, so there is
  real depth everywhere and no halo. It uses a developer setting the retail game still has,
  `GameRenderSettings.SplitScreenTestViewCount`: set to 2, the game draws a second complete view
  beside the first. The mod hooks the function that builds the frame's views (found in BF3's
  decrypted code) and makes the two views the eyes, in a window twice as wide (3200×1600, each eye
  a square 1600×1600 half); each eye is shown its half. The approach FrostbiteVR takes for Bad
  Company 2, found already built into BF3. Depth 3D and the other methods stay on Scroll Lock.
- **The left glove's fingers** follow the controller: the left trigger curls the index finger, the
  grip the other three, the thumb on the stick folds the thumb in. All three make a fist; the
  grip alone, a pointing hand. Each finger bends at its own joints, measured from the glove's mesh;
  relaxed, the fingers are a little curved and apart. Three small cracks in the glove model that
  bending opened are closed.
- Tools: `handview.exe` (the glove's finger poses drawn without the game, with checks for
  clipping and the mesh's openings), `dumpimage.ps1` and `findcode.ps1` (BF3's decrypted code saved
  and searched, for `dumpbin` disassembly), `dwords.ps1` (memory as values), `alarm.ps1` (rings
  every speaker when a test is ready).

### Fixed
- In per-eye views the hands and gun looked giant: BF3 draws the first-person parts of both views
  from one camera with no eye offset, which reads as far away. The free hands and gun are now
  shifted per eye (the two views' first-person draws come in order, the left eye's first).
- In per-eye views the picture showed as a small square: the headset was told the game's own 55°
  for a 100° picture. It now gets the angle the views were really built with.

### Known issues
- Smooth turning with the stick stutters a little: about 70 fps against the headset's 72.
- The HUD is drawn once across both eyes (the minimap in the left eye, the ammo count in the right).
- With free hands off, the game's own arms and gun have no depth of their own.
- `[stereo] async_handover=1` silently stops the gun's FOV fix and free hands: keep it 0.
- A short piece of bare forearm shows under the left glove's cuff.

## 2026-10-30, later: free hands (stage: pre-alpha, experimental)

2 commits since the last upload. First of the milestones that make it feel like VR.

### New
- **Free hands** (Insert on/off, on by default): the gun is drawn in your right hand, pointing
  where the controller points and held where the game's own hand holds it, scope and stock
  included; the left glove follows the left controller, open when empty, closing as you squeeze
  the left grip; arms, sleeves and body are folded away. On the Steam Frame the left View button
  lines the glove up with your hand. Steady while walking and strafing (tested in the headset).
- `tools\videoframes.ps1`: frames from a screen recording, without ffmpeg.

### Fixed
- After the opening ride the mod could lose the game's camera: the game then drew its own 55°
  and the headset stretched it over 100° (zoomed, blurry, big gun and arms). The camera now stays
  found, and if it is ever lost the headset shows the angle the game really drew.
- A dropped magazine no longer follows you along the floor (it vanishes as it lands).

### Known issues
- Free hands know only the campaign's M16-type rifle; bullets leave from where the game holds
  its gun, not the drawn muzzle; no two-handed aiming yet.

## 2026-10-30: v0.1.0-pre-alpha (stage: pre-alpha, experimental)

First upload marked with a stage. 14 commits since the last upload.

### New
- **Depth 3D** (the default stereo): each game frame becomes both eyes, every pixel shifted by its
  distance from the game's depth buffer, so both eyes update every frame and head turns no longer
  double the picture. The gun and arms get their real depth back (the game draws them squeezed
  towards the eye and marks them in the stencil buffer). Tuning in `build\depth3d.txt`; Scroll
  Lock cycles depth 3D / hybrid / alternate eyes / mono / frozen.
- **Wide field of view** (100° by default, Delete / Page Up / Page Down): the campaign renders a
  fixed 55° whatever the menu says. The render and cull views are widened, and the gun and arms'
  own projection is rescaled so they keep their real size.
- **God mode** (End, on by default): keeps the health of the soldier in the game's own server,
  which decides deaths. Scripted deaths still happen.
- **Bodies stay** (`[world] corpse_seconds`, 600 on the test setup): dead soldiers lie as long as
  that instead of the game's 15 s.
- **Magazine eject** on X (the campaign's M16-type rifle): the magazine drops out of the gun,
  tumbles and vanishes at the floor; its rounds go back to the spare ones, one stays in the
  chamber. X again puts a new one in; holding X is the game's own reload. The gun doesn't fire
  with the magazine out or one round left.
- **Steam Frame controllers**: their own SteamVR profile is used, so the buttons are where the
  Frame has them (A, B, X, Y on the right hand). Before, SteamVR guessed and put crouch on X.
- **Quick start** in the README, and `tools\selfcheck.ps1`, which confirms from the log that every
  part of the mod came up.

### Changed
- The picture no longer sways with the hand: the view turn is read from each camera the game
  builds.
- Faster: the research hooks are off by default (about +11 fps), the scan for the gun's projection
  skips upload sizes that stopped matching (about 1 ms a frame), the desktop window no longer
  waits for the monitor.
- The hands test reads its direction from `build\hands.txt`.

### Removed
- The old health finders (god mode finds the health itself now).

### Tools added
Memory research for the campaign (run with the 32-bit PowerShell): find and set values
(`findints`, `pokeint`, `pokeabs`, `findfloat`, `findhud`), pointer paths (`pathto`, `findref`),
the game's type tables (`fields`, `owner`), the server's soldiers and bodies (`soldiers`,
`corpses`). Pictures: `eyebmp` (saved eyes), `weapontest`, `fpmap`, `stencilbox`, `huntstep`.

### Known issues
- The head's position doesn't move the view yet (no lean or duck), and the gun is fixed to the
  view: it only points where the controller points.
- Only the M16-type rifle's magazine is known; the SMAW and the grenade launcher make aiming go
  wrong.
- The crosshair stays in the middle of the view, and HUD text takes the depth of what's behind it.
- Around near things against far ones the 3D can show a soft halo.
