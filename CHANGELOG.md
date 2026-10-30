# Changelog

Every upload to GitHub gets an entry here: what changed since the last one, and the stage the
project is at. Stages: pre-alpha, alpha, beta, release. A change of stage is also marked as a
GitHub release.

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
