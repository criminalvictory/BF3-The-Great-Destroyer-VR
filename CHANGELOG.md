# Changelog

Every upload to GitHub gets an entry here: what changed since the last one, and the stage the
project is at. Stages: pre-alpha, alpha, beta, release. A change of stage is also marked as a
GitHub release.

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
