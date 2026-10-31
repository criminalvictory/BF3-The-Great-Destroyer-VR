# Changelog

Every upload to GitHub gets an entry here: what changed since the last one, and the stage the
project is at. Stages: pre-alpha, alpha, beta, release. A change of stage is also marked as a
GitHub release.

## 2026-10-31, morning: the gun stays in your hands, no lens-flare ring, a steady grip (stage: pre-alpha, experimental)

From the user's headset session and videos this morning. The grip and wrist fixes were seen in the
headset ("both fixed"); the rest is checked by the self-test, an offline test and the mod's own
pictures, not yet in the headset.

### Fixed
- **The gun jumping to wrong places** (the user's video: the rifle lying across the body, swinging
  round at the face, dropping out of view). It happened only while the left hand "held" the handguard
  (the grip squeezed for 38 s): the gun then pointed from the right hand to the left wherever the left
  hand went, and switched in one jump. Now the left hand steers the gun only while the line between the
  hands is near where the right hand points it (fully within 35 degrees, not at all past 65) and the
  hands are more than 10-15 cm apart; the change eases in and out over a tenth of a second, a tracking
  jump of the left controller can't throw the gun across the view, a short loss of it changes nothing,
  and a hold taken on the way in follows the hand onto the handguard. The hold itself still lasts until
  the grip is let go. `src/steer.h`, with an offline test (`steertest.exe`, 19 checks, in the
  self-test). weapon.txt `steer 35 65` tries other angles; the log says once a second how far the hands
  strayed and what the left grip read, to tune them from.
- **The "crosshair" seen when hit** was the sun's lens flare: an aperture-shaped ring the game draws
  near the middle of the view when the sun is in sight (it came with the sun while being shot at). It's
  taken off; the dirty-lens blobs from the same flare stay (the user likes them). The game's shaders
  are now known by their code from one start to the next (`src/shaders.cpp`). weapon.txt `lensflare 1`
  keeps the ring.
- **A stretched, spiky glove while gripping**: the glove's blend onto the handguard (and the handle and
  magazine holds) turned about each joint, not about a point 1.2 m away.
- **Black patches on the left wrist**: the bare forearm's bones now move with the glove's own.
- **The camera following the right controller**: a stray F4 had flipped the aim's direction (saved).
  F2, F3, F4 and Scroll Lock now need Ctrl, and the log warns if the direction is flipped.

### Notes
- The orange markers the user saw in a recording are only in recordings: OBS takes each frame before
  the mod takes them off. The headset's picture doesn't have them.
- weapon.txt `turnby 30` turns the view at once (finding the sun with nobody in the headset).

## 2026-10-31, overnight: a self-test, casings out of the port, the flap with the handle, a smooth glove, no crash in the aim search (stage: pre-alpha, experimental)

Since the last upload. Worked on overnight with nobody in the headset: a stand-in hand holds the
rifle while the controllers sleep, and a self-test checks what broke before after every build. The
flap was tested in the headset by the user before bed ("the flaps are great"); the rest by the
self-test and the mod's own pictures, not yet in the headset.

### New
- **Self-test** (`tools\selftest.ps1`), run after every build: the mod's parts came up, the look
  angles were found and held, the frame rate, the view staying still while the gun turns 20
  degrees, the game aiming where the gun points (its weapon camera against the yaw written), the
  stick turning the way the user confirmed, both eyes' HUD corners alike, no streaks, errors in the
  log, and the glove collision test. PASS/FAIL per line.
- **Glove collision test without the game** (`collidetest.exe`): 25 scripted hand paths at 72 Hz
  (resting, soft and hard slaps, sliding, turning the wrist, through the gun, tracking jumps)
  against the rifle's shape, checking penetration, passing through, jerks and felt slaps.
- **Stand-in hand** (weapon.txt `standin 1`): holds the rifle out in front while the right
  controller sleeps, so the gun, aim and effects can be tested with nobody in the headset; with the
  headset off the frame's work goes on headless. `standin 1 45` looks 45 degrees up (test shots into
  the sky only), more numbers turn the aim and move the hold. While the left controller sleeps too,
  a stand-in left hand rests under the handguard, palm up, to see the left glove and forearm.
- weapon.txt `hudskip 10-19 40`: those HUD draws of each frame left out, to tell which is which;
  `hudskiptex 64x32` those with a texture that size. `tail` now also logs each HUD draw's
  constants, textures and vertices.
- **The right glove's trigger finger pulls with the trigger**: the game's own grip curls the index
  further, up to 22 degrees with the trigger all the way in, in the gloves' colour pass and both
  depth passes. The right hand's finger bones were mapped from the game's meshes (`handpose`:
  `BendRightIndex`, checked offline with `handview hand=r`). weapon.txt `fingertest 1` shows it
  without firing. Not yet seen in the headset.
- **The enemies' orange markers are gone** (the user: they were the "crosshairs"; there is no centre
  crosshair in the game). Their draws can't be told from the blue squad and objective markers (one
  icon sheet, coloured inside the game's shader), so the picture is copied before the HUD is drawn,
  and at the end of the frame everything the HUD drew within about 40 pixels of a pixel it made more
  orange is put back as it was. The blue markers stay. It costs no measurable frame time (13.9 ms a
  frame with it and without). weapon.txt `orangemarkers 1` keeps them.
- **Spent casings come out of the gun's ejection port** (the user: "every time I shoot"): each
  casing is its own small draw whose world placement is in its instance data, so while firing that
  placement is copied on the graphics card and moved by a small compute shader the way the gun was
  moved from where the game holds it to the hand. Fully moved near the port, less as it flies off,
  not at all 0.8 m out, so casings lying on the ground stay where they fell. In the pictures the
  casings now start 3-4 cm from the port; before, they started about 20 cm off, where the game's
  own gun is. No measurable frame time (bursts at 15.4 and 15.7 ms a frame with it, 15.1 and 16.2
  without). weapon.txt `casings 0` leaves them where the game throws them; `casings 1 0.2 0.8` sets
  how near the port they're fully moved and how far out not at all. Not yet seen in the headset.
- **Magazines from a pouch at the pelvis, pushed into the M416 by hand** (the user: "physically
  picking up a magazine ... from the pelvis area and manually inserting it into the gun"). With the
  magazine out (X ejects it), or the bolt locked back on an empty one, a fresh squeeze of the left
  grip in front of the lower belly puts the gun's own magazine in the left glove. That's below the
  belly button and above the groin, placed from the headset, and nothing is drawn there.
  - Brought top first up under the magazine well, it's caught in a funnel below the well's mouth that
    straightens it as it's pushed. It slides only along the well, the glove with it.
  - It seats when pushed home (within 5 mm, two frames), and only then do the rounds go in, with the
    magazine's sound.
  - Let go of before that, it falls, or goes back in the pouch if let go there.
  - The hold in the glove was fitted against the meshes so no finger goes into the magazine.
  - Tested in the game with a scripted hand (pouch.txt `test 1`, `test 2`): taken, caught, seated,
    30 rounds in. Not yet in the headset.
  - X with the magazine out still puts one in for now; `xinsert 0` in build\pouch.txt turns that off.
    Holding X is still the game's reload.
  - Any other gun reloads with the game's own reload when the left hand squeezes at the pelvis
    (`anygun 0` turns that off).
  - `zone` in pouch.txt moves and sizes the pouch. Every squeeze near it is logged with where the hand
    was, to place it from the first session.
  - The design came from three independent designs and a judge; a rifle magazine never goes into the
    grenade launcher's firing state.
  - An offline test drives it with scripted hands at 72 Hz (`magtest.exe`, 50 checks, now in the
    self-test). It and a three-way review found 15 problems, all fixed:
    - a 6-degree turn of the wrist knocked the magazine off the well;
    - a one-frame tracking jump threw it out;
    - a one-frame loss of the right controller made it vanish;
    - it could be caught coming down through the receiver;
    - the other guns' pelvis reload could take the squeeze meant for the handguard;
    - a dropped magazine fell from rest instead of at the hand's speed;
    - a few smaller ones.
    These fixes are tested offline only: the game couldn't be restarted (Battlelog had signed out).
- `tools\launch.ps1` clicks Battlelog's LAUNCH CAMPAIGN when PLAY CAMPAIGN opens the campaign page.
- **The ACOG's reticle and red cable stand out more**: they're see-through, so they're drawn again on
  themselves (the reticle five more times, the cable three), the strongest of three levels the user
  compared in the headset. weapon.txt `scopeboost 12 2 129 1` tries other levels.
- weapon.txt `scopetrace`: three frames of the scope's draws logged, each with the bones it was
  drawn with, whether it moved with the gun, and once per part how it's drawn (blending, its pixel
  shader's constants and textures). `zoom 1` holds the game's zoom (to test the
  Stinger's lock-on, which only locks zoomed).
- Research notes in `docs\research\`: FrostbiteVR and BC2VR (what works there, what applies to BF3),
  Overlord's lessons (ideas only), the mission's weapons, a 6DoF head plan, ten ranked next steps.
- **Wrist movement, step 1 (the left hand)**: the left forearm is solved from the glove's hand and a
  shoulder estimated from the headset, so the glove bends at the wrist while the forearm keeps
  pointing at the elbow, and the forearm takes two thirds of the wrist's roll at the watch
  (`src/armik.h`; three designs and a judge). **Off until it has been seen in the headset**:
  `on` in build\arms.txt turns it on; no file, or `off`, draws the forearm exactly as before. `log 1`
  logs the wrist's readings once a second; `test move`, `test neutral`, `test bend` and `test drop`
  move, straighten, bend or remove the stand-in's left hand. Tested offline only (`armtest.exe`, 14
  checks, now in the self-test with an in-game row too); not yet run in the game.

### Changed
- **The ejection port's flap follows the charging handle**: pulled slowly it opens slowly, fully
  open 4 cm back, and it shuts with the handle as the spring takes it home.
- **The free left glove no longer jerks against the gun**: it was kept from a point 1.2 m from the
  hand, so small turns of the wrist threw it by centimetres, and its slide flipped direction on the
  bolt catch's edges. Now it's kept by the hand's middle, pushed out by all the spheres in the gun at
  once, and slides smoothly (turning the wrist while touching: 35 mm jerks before, about 1 mm now).
  A hard slap can't pass through the gun (its path is swept, stopping at the first touch).
- Muzzle flash research: its sprites were read back (`flashprobe`): flat quads placed through their
  own block of constants, not the view as tried before. Moving that block's first matrix with the
  gun (`flashmove 1`, off by default) applied only some of the time and the flash wasn't seen to
  move, so the flash still comes from where the game holds its gun.

### Fixed
- **The ACOG's dark lens circle, red cable and red dot floated in the sky**, moving with the gun
  (the user's video): they're drawn later in the frame than the rest of the gun, where the mod
  never learned where they sit on it, so they stayed where the game holds its own gun. They now
  take the scope body's place. Confirmed in the headset by the user ("it seems to work just fine
  now"). Two earlier attempts at it were taken out again: one moved world-space scope parts near
  the gun, which could have taken a squadmate's scope onto the player's gun; one kept a copy of
  every bone upload, which the scope never needed.
- **The hands slid about on the gun while sprinting**, and the gun could flip between two holds
  twice a second (the user's videos): the mod copied how the game's hands hold its gun every frame,
  sprint animation and all. Now it copies that only from the game's normal hold: not while
  sprinting (nor for a second after), and not while the game's hands are more than 3-4 cm off
  their usual grip (its other moves), unless both stay put elsewhere for 5 s of the game running (a
  pause or the rifle put away doesn't count; only the hand that's off takes the new hold). The
  first hold after loading is followed until both palms stay still for a second, and every draw of
  a frame copies the game's hands or keeps its own together. A log line every 10 s says how the
  hold was judged. Not yet tested with a sprint (the keyboard's didn't move the player there).
- **In some launches the right glove stayed at the game's hip instead of on the grip**: where the
  game's hands hold its gun was only learned when the gun was drawn before the arms in a view, and
  which comes first differs from launch to launch (in one tonight it learned only after a reload,
  2.5 minutes in). The arms' bones are now kept until the gun is drawn and learned then: in a launch
  where every view draws the arms first, the grip was learned 0.1 s after stereo started.
- **Black slivers could float between the hands** with the left hand off the gun (found by a code
  review): 15 small collar triangles mix the body and both arms, and the arms now fold into their
  forearms' middles while the body still folded between the wrists. The body folds onto the line
  between the arms' fold points again, where those triangles have no area.
- **The stand-in's left hand acted as a real one** (found by the review): with the headset off it
  could take a hold on the handguard or chamber a round with the controllers' last buttons. It's
  now only drawn. If the controllers failed to start, its first pose also stayed in the room.
- **Black spikes on the left forearm** below the watch (the user's picture): the bare forearm's far
  end was folded into the palm with an all-zero matrix, which made its stretched triangles black,
  and the depth passes' differently cut copy of them left more. The bare forearms now move whole
  with their hands, and the arms' hidden parts fold into the middle of the forearm, inside it.
  Checked with the stand-in's left hand; not yet seen in the headset.
- **Squeezing the right grip threw a grenade** (it was bound to the game's G): the right grip is for
  holding the gun; the grenade is unbound until it gets its own gesture.
- **The game could crash right after a load**: the look-angle search tried 24 of 163 matching
  places in memory when the game's yaw happened to be near zero. It now never tries when that many
  match, and turns the view a little to look again.
- The stand-in hand and the self-test: controllers aren't taken as tracked while input isn't
  focused (their old poses stayed).

### Known
- In the car ride at the mission's start the game turns the look itself (the seat's limits, the
  vehicle turning) and the mod writes its own back each frame: the picture follows the head, but
  the body doesn't turn with the vehicle yet, and turning the head turned the view the wrong way
  (the user). Needs a test in the headset before changing.
- The ammo counter reads one more than the gun holds.
- Looking along a forearm into its open elbow end shows a black disc (the depth pass's folded arm
  inside it); from the side nothing shows.
- To come (the user's list): the muzzle flash at the gun, wrist movement for the gloves, a pouch
  reload of their own for the other guns and the launchers.

## 2026-10-30, late night: aim, both hands on the gun, collisions, both eyes alike (stage: pre-alpha, experimental)

Since the last upload. The game aims where the gun points and the view stays put however the gun
moves, the left hand holds the gun, the free hand rests against the gun instead of passing through
it, and everything drawn over the picture is the same in both eyes. Tested in the headset by the
user ("they land there": the shots; the view no longer moves with the gun; the smooth turn the
right way) and by the mod's own pictures of both eyes in combat.

### New
- **Both hands on the gun**: squeeze the left grip near the handguard and the left hand holds it
  until the grip is let go (not on sway, not on a far swing). The gun points from the right hand to
  the left, and the game aims along it. The glove is the game's own left hand as its animation
  holds this gun (made for it, the grenade launcher included, so it doesn't clip), its fingers
  moving a little with the controller's, easing on and off the gun.
- **Collisions with the rifle**: the free left hand rests against the gun and slides along it
  instead of passing through, and slapping the bolt release counts when the glove touches it. The
  rifle's shape is a distance field built from its mesh dump by `gunshape.exe` (into
  `build\guns\m416.bin`; it's the game's model, so it isn't in the repository). The launcher, the
  stock and the scope aren't in it yet.
- **Walking with the left stick as an analog stick** (the game reads a virtual controller), where
  the head faces, not where the gun points.
- **Sharper picture**: 2160 pixels per eye.
- **God mode keeps the spare rounds up**, for testing the reloads over and over.
- Tools: `gunshape.exe` (the rifle's collision shape and the glove's collision spheres),
  `gripfit.exe` and `leverfit.exe` (the glove fitted on the gun from the mesh dumps),
  `tools\gunammo.ps1` (the gun's ammo state from outside), `tools\make.bat` (build only some
  targets, e.g. the tools while the game runs). weapon.txt: `tail` (one frame's draws onto the
  window, in order), `viewdiff`, `viewlog` (the view's turn each frame), `aimtest`.

### Fixed
- **Every shot froze the game**: the window is bigger than the monitor, so the mouse pointer sat on
  the taskbar's "Show desktop" corner and each fire click minimised everything. The pointer is now
  kept on the game's window.
- **The view moved with the gun, and the game aimed the other way**: BF3's look angle grows turning
  right, the headset's turning left. The old angle finder flipped it; the search that replaced it
  didn't. The flip is now built into the aim itself (the gun's turn and the stick's), the angle is
  set outright each frame, and the picture goes by the angle written.
- **Shots didn't go where the gun pointed**: recoil or a turn during the check that proves the
  look angles let a wrong pair through. The check now waits for a still camera, nudges both the
  yaw and the pitch each its own way, and needs the camera to follow both and come back.
- **The right eye smeared into streaks when suppressed** (seen as a blinding blur): BF3 drew the
  second eye as a split screen's right half, and its screen effects read the scene past its edge.
  The right eye is now drawn as the left one is and moved into place; the player's blood, damage
  tint and HUD go into both eyes.
- **Magazines held 59 or 88 rounds**: the game reloaded by itself on top of a hand reload. The
  game's count keeps one hidden round so it never runs dry, and the rifle's magazine is 30.
- The grip let go when the gun was swung far right or left, the free hand jittered against the
  gun, the gun shook aiming up past the game's pitch limit, and the reload sounds were off to the
  right: all fixed. The ejection port's cover swings shut between shots, and the black layer round
  the arms, glove and gun is gone.

### Known
- The ammo counter on the HUD shows one round more than the gun holds (the hidden round).
- The lever hand still clips the charging handle's shaft.
- The ACOG's reticle can float away from the scope.

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
