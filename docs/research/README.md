# Research notes: index and next steps

## What this means for us (plain English)

- **FrostbiteVR and BC2VR are one project by one author.** BC2VR is only the download and feedback
  page for FrostbiteVR's Bad Company 2 mod. All the code is in FrostbiteVR, which we may learn from
  freely and reuse with credit (MIT licence); BC2VR has no licence and no code. Nothing exists for
  BF3: their plan named BF3 as the next game, but the author went to Battlefield 4 instead.
- **What works for them** *(reported; we did not run it)*: both eyes, aiming with the controller,
  walking around the room, two-handed grips, holsters, the shotgun pump, magazines loaded by hand,
  the opening knife fight. **What doesn't:** a crash after dying or loading a checkpoint (open since
  September), mission 1 blocked because throwing C4 had no button, poor headset speed, cutscene
  cameras facing the wrong way, newly added guns that won't reload. Nobody has finished the campaign.
- **On the big pieces we are level or ahead.** BF3 draws both eyes itself through its split-screen
  test, at 72 fps. FrostbiteVR had to build that by hand for BC2, and the split-screen route crashed
  on BF4 *(reported)*.
- **Worth borrowing:** not their whole system, only ideas and a little game-independent maths (never
  their memory addresses): testing without the headset, walking around the room, their crouch rules
  and numbers, a recipe per gun, magazine insertion numbers, and later real arms up to the shoulder.
- **Overlord** (Call of Duty VR; GPL, so ideas only, never code) shows how to keep fixes fixed: a
  self-test and a pass/fail checklist for every build before you are called. It gets the game's own
  muzzle flash and brass out of the VR gun by asking the game to create them there at the moment of
  the shot, not by moving the pictures afterwards *(reported)*. Its cutscene rules largely match
  yours.
- **Rock and a Hard Place** (read from the game's data files, not yet checked in our game): Blackburn
  carries the M416 with an ACOG and the M320 underneath, a SMAW rocket launcher, hand grenades and a
  knife. **No pistol, and no AK-74 to pick up:** the Russians drop AN-94s and RPK-74Ms, and a Stinger
  waits near the end. The SMAW (for the two BMPs) and the Stinger (for the jet) are needed to finish.
- **Surprises in that data** *(unverified in the game)*: the Stinger only locks on while zoomed, and
  free hands never zoom, so as things stand it may never fire; rockets and launcher grenades start
  from your head, not the tube; shots fired without zoom get extra random spread.
- **Two gaps in our controls** (checked in our code): no controller button selects the SMAW or the
  M320 or uses the knife, and squeezing the right grip presses G, the quick grenade throw.
- **6DoF head** (moving your head moves the view): the camera part is small and sits in one place.
  Walls are the real problem, so the camera stays inside the soldier's body and the view darkens
  near walls. Real crouch: squat about 30 cm to crouch, rise past 20 cm to stand, and the view never
  drops twice.
- **Next:** the ten steps below, in the order of the roadmap you agreed.

*(reported)* = another project's documents say so and we have not tried it. *(unverified)* = nobody
has checked it in our game or our build yet. Nothing was run for these notes.

---

## The ten next steps, ranked

Ranked in the order of the roadmap you agreed (1: self-test and muzzle flash; 2: 6DoF head for
standing play; 3: every weapon of the mission with its own mechanics; 4: the whole mission without a
crash), and by value within each stage. Effort is my rough guess: **Small** = a few hours;
**Medium** = one to three sessions; **Large** = several sessions of reverse engineering that may not
succeed.

| # | Step | Stage | Effort | Main write-ups |
| --- | --- | --- | --- | --- |
| 1 | Make the self-test the gate for every build, with a checkpoint load | 1 | Small | overlord-lessons, bc2vr |
| 2 | Muzzle flash, smoke and brass at the drawn gun (the flame is under way) | 1 | Large (the flame: Small) | overlord-lessons, bc2vr, frostbitevr |
| 3 | The camera follows your head's position | 2 | Medium | 6dof-head |
| 4 | No seeing through walls: a limit and a fade | 2 | Medium | 6dof-head |
| 5 | Real crouch becomes the game's crouch | 2 | Medium | 6dof-head, frostbitevr |
| 6 | A "scripted" mode for the car ride and other camera takeovers | 2 | Medium | 6dof-head, overlord-lessons |
| 7 | Stinger and SMAW basics, and a button for every action the mission needs | 3 | Medium | rock-and-a-hard-place-weapons |
| 8 | One profile per gun, and one referee for the left hand | 3 | Medium | overlord-lessons, frostbitevr |
| 9 | The M320 under the M416, version 1 | 3 | Medium | rock-and-a-hard-place-weapons |
| 10 | A deliberate hunt for things drawn for the wrong eye | 4 | Small | bc2vr, overlord-lessons |

### 1. Make the self-test the gate for every build, with a checkpoint load

- **What:** `tools/selftest.ps1` (in the repository since commit `7332e84`) already checks start-up,
  the look angles, the frame rate, the view staying still while the gun turns, where the game aims,
  the stick's turn direction, the HUD in both eyes and streaks. Add three things:
  1. a round that opens the pause menu, loads the last checkpoint and runs the checks again;
  2. the build's identity (commit, a "modified" flag, the DLL's hash) at the top of `bf3vr.log` and
     of each report;
  3. a table per build with one row per never-break item, marked Pass, Fail or Not covered, and who
     checked it (the self-test or you). Rows the self-test can't cover yet (walking where you look,
     real shots landing, the grip and hand reloads) stay marked for you.

  Nothing goes to the headset, and no milestone is uploaded, until the self-test rows pass.
- **Why:** regressions are what frustrate you most, and each one costs a headset session. Overlord
  keeps its fixes fixed this way. BC2VR left deaths and checkpoint loads for later, and its crash
  there has been open since September *(reported)*.
- **Write-ups:** [overlord-lessons.md](overlord-lessons.md) §1 and lessons 1, 8 and 10;
  [bc2vr.md](bc2vr.md) §5 pitfalls 2 and 10; [frostbitevr.md](frostbitevr.md) §3.17-3.18.
- **Effort:** Small.
- **Test (no headset):** run it on today's build: every row passes, including after the checkpoint
  load. Then run it on a throwaway build with one fault put in on purpose (for example the yaw sign
  flipped): the matching row must say FAIL.

### 2. Muzzle flash, smoke and brass created at the drawn gun

- **Under way for the flame** (uncommitted work seen in the working tree on 2026-10-31, not yet
  confirmed in the headset): the matrix that places the flame was found (the first matrix of its
  vertex-shader constant block, in the arms' space, at the game's muzzle), and `src/effects.cpp`
  draws the flame with a moved copy of that block. This "draw route" suits the flame, which lives
  only a few frames.
- **What, for smoke and brass:** find where BF3 decides where a shot's smoke and spent cases appear,
  and hand it the drawn gun's muzzle and ejection port at the moment of the shot. First tool: a
  survey of the effect shaders that records which constants each one reads (by name if BF3 kept the
  names, by disassembly if not) and recognises each shader by a fingerprint of its code, so a shader
  found once is found again after a restart (the census keyed them by a pointer that changes every
  launch). Then trace back from their positions to whatever writes them.
- **Why:** you asked for the game's own flash, smoke and brass at the gun. Moving drawn pictures
  every frame drags smoke along with the gun and makes brass already in the air jump when the hand
  moves; short of tracking every particle, only creating them at the source keeps smoke where it
  was fired and lets brass fly from where the port was. Overlord did it this way *(reported working
  in their headset)*; BC2VR moved the gun where the game's effect and shot code read it *(reported;
  its notes don't show the flash following)*.
- **Write-ups:** [overlord-lessons.md](overlord-lessons.md) §2; [bc2vr.md](bc2vr.md) §3.5 and
  lesson 1; [frostbitevr.md](frostbitevr.md) §3.10 and borrow 5.
- **Effort:** Small to finish the flame; Large for smoke and brass. Time-box the source route: if
  two sessions don't find it, park it behind steps 3 to 5.
- **Test (no headset):** god mode on, a short burst, both eyes saved: the flame at the muzzle in
  both eyes, brass leaving the port on the right, smoke staying where it was fired as the gun moves
  away, brass hidden behind a wall when it falls behind one. **Then you:** the same in the headset.
- **Unverified:** everything about BF3's effect code for smoke and brass, including whether its
  shaders kept their names; the flame's draw route until it is seen in both eyes.

### 3. The camera follows your head's position (6DoF, part 1)

- **What:** first the test lines: `headtest x y z` fakes a head position and `sixlog` logs where
  each eye went. Then, in `views.cpp` `MakeEyes`, move both eyes' main, cull and first-person
  cameras by the head's offset, turned by the soldier's level heading (not the head's); never move
  the weapon camera. Publish the head's position together with its rotation, tell SteamVR the pose
  that was really drawn, use the position only while OpenXR marks it valid and tracked, and cap it
  at 0.5 m until step 4 adds the real limit.
- **Why:** it is the biggest missing piece of the alpha. The head's position is known every frame
  and thrown away; the change is small and sits in one place; the gun and the glove are already
  placed relative to the head, so they stay in your hands.
- **Write-ups:** [6dof-head.md](6dof-head.md) §1-2 and §11 steps 0-1;
  [frostbitevr.md](frostbitevr.md) §3.7.
- **Effort:** Medium.
- **Test (no headset):** 6dof-head tests 1.1-1.7: `headtest 0.2 0 0` moves both eyes 0.200 m along
  the body's right (within 2 mm); the offset doesn't turn when the head turns; the gun keeps its
  pixels in eye dumps while the world shifts; 72 fps; the never-break list with 6DoF on and off.
  **Then you:** lean, duck and step sideways: the world stays still and nothing doubles.

### 4. No seeing through walls: a limit and a fade (6DoF, part 2)

- **What:** measure by script how close the soldier's eye gets to a wall (walk into one and read the
  depth; fake head turns for the sides and back; look down for the eye's height above the floor).
  Keep the camera within that distance minus 10 cm (start: 25 cm sideways, eased in from 18 cm, and
  10 cm up). Past the limit, darken the view with a dark layer fixed to the headset (fast in, slow
  out). Then add a fade from the depth buffer when a surface comes within 12 cm.
- **Why:** the game doesn't know your head moved, so nothing stops it entering a wall. FrostbiteVR
  and BC2VR say plainly that they don't handle this *(reported)*. Research found fading less
  sickening than pushing the view back.
- **Write-ups:** [6dof-head.md](6dof-head.md) §3-4 and §11 steps 2-3;
  [overlord-lessons.md](overlord-lessons.md) §6.
- **Effort:** Medium.
- **Test (no headset):** 6dof-head tests 2.1-2.4 and 3.1-3.2: `headtest 1.0 0 0` stops exactly at
  the limit with the fade nearly full; a sweep from 0 to 0.4 m rises smoothly and never passes it;
  the eye dump beside the wall shows nothing behind it; the depth fade costs under 0.2 ms.
  **Then you:** the limit feels natural and the fade is a cue, not a shock.
- **Unverified:** the size of BF3's soldier collision body; whether SteamVR fades a layer fixed to
  the headset as expected; whether each eye's depth buffer is found in per-eye mode.

### 5. Real crouch becomes the game's crouch (6DoF, part 3)

- **What:** first a `crouchtest` line finds out whether BF3's crouch key is hold or toggle and how
  far the game's eye drops. Then crouch when your neck point drops more than 30 cm for 200 ms, and
  stand again when it is less than 20 cm down. Recentering stores your standing height, except when
  the new height is far below the one learned so far (a recenter while squatting). Give recentering
  a controller chord, since F9 is a keyboard key and you play standing. Add the game's own crouch
  drop back so the view never sinks twice. Prone gets a button rather than lying down.
- **Why:** real crouch is alpha criterion 1. FrostbiteVR's BF4 version lets the view drop twice
  *(reported)*, and none of the three projects reports a physical crouch accepted in a real headset.
- **Write-ups:** [6dof-head.md](6dof-head.md) §6 and §11 step 4; [frostbitevr.md](frostbitevr.md)
  §3.7 and §3.14; [overlord-lessons.md](overlord-lessons.md) §6.
- **Effort:** Medium.
- **Test (no headset):** 6dof-head tests 4.1-4.6: a faked 25 cm drop gives no crouch; 32 cm crouches
  after 200 ms while the view stays exactly 32 cm down; 22 cm stays crouched and 18 cm stands; the B
  button still crouches with the game's own drop; a recenter at -40 cm keeps the old standing height
  and logs a warning. **Then you:** no crouch from looking down, picking something up or a quick
  duck.
- **Unverified:** whether BF3's crouch is hold or toggle, and how far its eye drops.

### 6. A "scripted" mode for the car ride and other camera takeovers

- **What:** an explicit scripted state with a logged reason on entry and exit, keyed to a signal
  from the game (candidates, all *unverified*: the soldier in a vehicle seat, the active camera no
  longer the soldier's, player input switched off), with today's look-angle mismatch kept as the
  fallback. While it's on: the body follows the game camera's position and level heading (its tilt
  and roll dropped), your head turns freely and moves within about 10-15 cm, the view lines up once
  on entry and hands back once on exit, there is no crouching or room walking, the trigger must be
  let go before the first shot afterwards, and only one pair of arms is drawn (ours or the game's).
- **Why:** it is your decision for cutscenes, and the mission opens with the Growler ride.
  Overlord's scripted-scene rules largely match it; BC2VR's cutscene cameras still face the wrong way
  after days of study *(reported)*.
- **Write-ups:** [6dof-head.md](6dof-head.md) §9 and §11 step 6;
  [overlord-lessons.md](overlord-lessons.md) §3 and lesson 4; [bc2vr.md](bc2vr.md) §3.7 and
  pitfall 6.
- **Effort:** Medium.
- **Test (no headset):** 6dof-head tests 6.1-6.3: load the opening checkpoint with `sixlog` on:
  scripted during the ride, off on foot; the picture's roll equals your head's (within 0.5°); no
  jump over 2 cm or 2° entering or leaving except at a camera cut. Plus an offline check that
  entering and leaving never turns the view twice. **Then you:** the ride is comfortable and the
  action stays in front.

### 7. Stinger and SMAW basics, and a button for every action the mission needs

- **What:**
  1. controller buttons for the SMAW (key 4), back to the rifle (1) and the knife (F), for example
     on the left D-pad, which has no job yet (its OpenXR names are *unverified*); the
     over-the-shoulder grab for the SMAW can come later;
  2. decide what the right grip should do: today it presses G, the quick grenade throw (checked in
     `input.cpp`). Whether that is what emptied your "G" counter is *unverified*;
  3. hold the game's zoom while the Stinger is at your eye: the game's data says it only locks on
     when zoomed, and only fires once locked;
  4. find out why SMAW and launcher aiming goes "haywire" before fixing it. The data says rockets
     and launcher grenades start from the camera (your head) plus a fixed offset, and that shots
     without zoom get extra spread (2° for the launchers, 2.5° for the rifle). If that is the cause,
     rewrite the live start point to the drawn tube's muzzle each frame, and hold the zoom or copy
     the zoomed spread.
- **Why:** the mission can't be finished without the SMAW (the two BMPs) and the Stinger (the jet).
  BC2VR's first mission was blocked twice by an action without a button: the knife worked only as a
  gesture, and the C4 throw had no button at all *(reported)*.
- **Write-ups:** [rock-and-a-hard-place-weapons.md](rock-and-a-hard-place-weapons.md) §2.3,
  §3.3-3.6, §5 and §6; [bc2vr.md](bc2vr.md) §5 pitfall 1.
- **Effort:** Medium (the buttons and the zoom are small; the start point needs a memory search).
- **Test:** load the tank-battle checkpoint, switch to the SMAW from the controller, fire at a wall
  with head and tube pointing different ways, save both eyes at launch (does the rocket leave the
  tube or your face?) and mark where it lands; fire single rifle shots without zoom at a wall 30 m
  away (do they scatter?). Load the jet-run checkpoint, pick up the Stinger and raise it: the log
  shows the zoom held, the lock tone sounds, it fires. **You** are needed for the lock itself (the
  jet within 5° for 2 seconds).
- **Hint:** an earlier scripted test found that key 2 didn't switch weapons. Key 2 selects slot 1,
  which is empty in this mission (no pistol), which may explain it *(unverified)*. Check that 1, 3
  and 4 reach the game before building on them.
- **Unverified:** all of this comes from the game's data files and has not been tried in the game.

### 8. One profile per gun, and one referee for the left hand

- **What:** move the M416's specifics (charging-handle and bolt-release points, magazine size,
  handguard line, collision shape, dust cover, muzzle and port positions) into a profile that
  describes each gun by its feed, what happens after its last round, and whether a quick reload
  keeps the chambered round. A gun without a profile falls back to the game's own reload on purpose.
  Then:
  1. one decision per frame for the left hand (support grip, charging handle, bolt slap, magazine,
     launcher) that logs why it says no;
  2. a stricter slap: the hand must first move away, then come in fast enough, far enough and from
     the right direction;
  3. an answer to "the ammo HUD reads one high", for example by treating BF3's extra round as the
     chambered round (a suggestion, *unverified*).
- **Why:** never an M416 copy. FrostbiteVR and Overlord both arrived at shared mechanisms plus data
  per gun (FrostbiteVR's recipe model is still a design no game of theirs uses; only its pump and
  bolt part runs in BC2). Every new gun adds jobs for the left hand; in Overlord, until one referee
  decided, the result depended on the order the code ran in *(reported)*.
- **Write-ups:** [frostbitevr.md](frostbitevr.md) §3.8-3.9;
  [overlord-lessons.md](overlord-lessons.md) §4-5 and lessons 5-7;
  [rock-and-a-hard-place-weapons.md](rock-and-a-hard-place-weapons.md) §3; memory note
  `weapons-own-mechanics`.
- **Effort:** Medium.
- **Test (no headset):** the self-test passes and the M416's draws and counts are unchanged; offline
  tests of the reload steps (dry, locked back, magazine in, slap or pull, chambered) and a long
  random run of ejects, inserts, shots and reloads that must never create or lose a round.
  **Then you:** both hand reloads, the slap, the handle and the grip until release, as before.

### 9. The M320 under the M416, version 1

- **What:** dump the launcher's own mesh and add it to the gun's collision shape (today the hand goes
  through it). Left hand on the launcher's pistol grip with a squeeze: the mod sends 3 and waits
  until the game confirms the launcher is in hand (a loaded count of 1 or less, the launcher's own
  spare count). The left trigger fires one grenade; the right trigger does nothing in launcher mode.
  Hand back to the handguard: the mod sends 1. Respect the game's 0.5 s gadget cooldown, keep its
  own 2.9 s reload, and never let a press made during the switch fire later.
- **Why:** you asked for it, Overlord-style. BF3 treats the launcher as a weapon of its own, so using
  the game's own switch keeps ammo, damage and the HUD right, and switching on the grip hides the
  half-second switch.
- **Write-ups:** [rock-and-a-hard-place-weapons.md](rock-and-a-hard-place-weapons.md) §3.2 and §4;
  [overlord-lessons.md](overlord-lessons.md) §4.3; memory note `underbarrel-launcher`.
- **Effort:** Medium.
- **Test:** first by script from the log: 3 and 1 sent, the counts confirmed. **Then you**, following
  rock-and-a-hard-place-weapons §4.10: after the grip the HUD reads 1/3; exactly one grenade per
  pull, none while the trigger is held; the right trigger does nothing in launcher mode; letting go
  brings the rifle back; ten grenades at a marked wall 30 m away; an ammo crate, a checkpoint load
  and a cutscene leave no stuck switch.
- **Unverified:** the switch times and counts are from the game's data, not seen in our game.

### 10. A deliberate hunt for things drawn for the wrong eye

- **What:** at five fixed places in the mission, save both eyes and compare them. Far things (sky,
  clouds, distant hills) must line up in both eyes; decals, debris, particles, water, shadows and
  light shafts must sit at the same depth in each. Also check whether the dark layer around the
  arms, glove and gun is shifted the opposite way in each eye (Overlord's sign of a per-view light
  list left at the centre camera). Anything found gets its own row in the self-test.
- **Why:** BC2VR's testers found wrong-eye effects one at a time over a month (the camera, the gun,
  clouds 245 to 280 pixels apart against 0 to 8 for a distant hill, debris, decals) *(reported)*. We
  have the eye dumps to find ours first, and it may explain our dark layer *(unverified)*.
- **Write-ups:** [bc2vr.md](bc2vr.md) §3.1 and lesson 4; [overlord-lessons.md](overlord-lessons.md)
  §7; [frostbitevr.md](frostbitevr.md) §3.1.
- **Effort:** Small (fixing what it finds is extra).
- **Test:** it is the test. Pass: far features no more than a few pixels apart between the eyes, and
  no effect at a different depth in each eye.

### Next in line

- Walking around the room: the soldier follows when you step away (6dof-head §7; frostbitevr §3.7).
- Offline tests of the turn and aim maths, built from the three recorded regressions
  (overlord-lessons §1.3).
- A heading that stays defined when the gun or the head points straight up or down
  (overlord-lessons §6).
- Real arms from the shoulder to the hand, to replace the bare forearm (frostbitevr §3.5).
- A controller buzz on each shot and when a magazine seats; there is none today (frostbitevr §3.12).
- The HUD at about 2 m instead of at infinity, and the centre crosshair hidden (frostbitevr §3.11).
- Hand grenades from the chest; AN-94 and RPK-74M profiles (rock-and-a-hard-place-weapons §3.5,
  §3.7-3.8 and §5).
- Bullets starting at the drawn muzzle, which matters once you lean (6dof-head §5; bc2vr §3.5).
- Controls through BF3's own input actions instead of fake keys (bc2vr §3.4; large, *unverified*
  for BF3).

---

## What does not apply to BF3, and why

- **Their memory addresses, offsets and object layouts.** BC2 runs an older engine (Frostbite 1.5)
  and BF4 is a 64-bit Frostbite 3 build, so nothing of theirs fits bf3.exe 1.6. The same goes for
  BC2's menu hooks, ammo receipts and animation decoding.
- **Building the second eye by hand** (BC2's extra view and enlarged work list) **or drawing the
  world twice** (BF4): BF3's split-screen test already draws two views from one game update. BF4's
  split-screen route crashed because its renderer has room for only one view *(reported)*; BF3's
  has room for two.
- **NVIDIA 3D Vision tricks:** BF3's own built-in stereo branch needs a 3D Vision driver, and their
  BF4 driver shim never got BF4 into stereo *(reported; parked, not disproved)*. We need neither.
- **A separate 64-bit headset program** passing the pictures across: it adds copies and delay, and
  their headset speed stayed poor (the cause is unproven). Ours runs inside the game at 72 fps.
- **Aiming with fake mouse movement** (BF4): we write the game's real look angles, which is exact
  and proven by the nudge test.
- **Their controller layouts** (Quest and Index, two face buttons on each hand): the Steam Frame has
  all four on the right and a D-pad on the left, so button plans such as Overlord's M9 need our own.
- **BC2VR's cutscene code:** unsolved there; only its pitfalls help.
- **Overlord's engine and code:** Call of Duty's models, scripts, flags and settings are nothing
  like Frostbite's, and its code is GPL, so ideas only. It also renders the game once per eye, which
  BF3's split screen makes unnecessary.
- **Things this mission doesn't need:** two guns at once, ladders, climbing, a riot shield, BC2's
  fold-up launcher sight (BF3's underslung M320 has none), the M203's forward-sliding reload (the
  M320 swings sideways), and the M9 and AK-74 (not in this mission).
- **FrostbiteVR's 28 cm support-grip release:** you want the grip held until you let go.

---

## Where the notes disagree or have gone out of date

- `tools/selftest.ps1` and the "headless" mode (frames kept going while the headset sleeps) are in
  the repository since commit `7332e84` (2026-10-30, 23:50). frostbitevr.md, bc2vr.md and
  overlord-lessons.md still call them uncommitted work in progress.
- **The flame's placement:** overlord-lessons.md §2.2 and frostbitevr.md §3.10 treat it as unknown.
  Uncommitted work in `src/effects.cpp` (2026-10-31) found the matrix that places it and draws the
  flame moved to the drawn gun (step 2); not yet confirmed in the headset *(unverified)*.
- **Which guns are in the mission:** rock-and-a-hard-place-weapons.md, from the game's data, finds no
  M9 and no AK-74 to pick up (the AN-94 and RPK-74M instead, plus the Stinger). overlord-lessons.md
  §4.4, frostbitevr.md §3.9 and our memory note `weapons-own-mechanics` still list the M9 and the
  AK-74 for it. Their AK-74 notes still fit the AN-94 and RPK-74M (the same family: magazine rocked
  in, paddle release, handle on the right, no hold-open), and their M9 notes fit *Semper Fidelis*
  and *The Great Destroyer*.
- **Physical crouch elsewhere:** the summary of 6dof-head.md says FrostbiteVR "has tested" crouch
  thresholds; its own §6.2 and frostbitevr.md §3.7 show that only BF4's version ran, and only on a
  simulated headset.

---

## The write-ups

| Write-up | In one line |
| --- | --- |
| [frostbitevr.md](frostbitevr.md) | FrostbiteVR (BC2 and BF4): what works, each technique against ours, the 5 best borrows |
| [bc2vr.md](bc2vr.md) | BC2VR, FrostbiteVR's Bad Company 2 release: its state, how it works, 5 lessons and 10 pitfalls |
| [overlord-lessons.md](overlord-lessons.md) | Overlord (Call of Duty VR; GPL, ideas only): keeping fixes fixed, flash, cutscenes, guns, hands; 10 lessons |
| [rock-and-a-hard-place-weapons.md](rock-and-a-hard-place-weapons.md) | Every weapon in the mission from BF3's own data, how the real guns work, and the M320 design |
| [6dof-head.md](6dof-head.md) | Head position, walls, real crouch, room walking and cutscenes: the design, the plan and its self-tests |

Licences: FrostbiteVR is MIT (ideas freely; code only with its notice in
`licenses/FrostbiteVR-MIT.txt` and a CHANGELOG credit); BC2VR has no licence (take nothing from
it); Overlord is GPL-3.0 (ideas only, in our own words, never code).

## Checked for this index

Written 2026-10-31 from the five notes above (all dated 2026-10-30); nothing was run and the game
was not touched. Checked in our own code and history: `tools/selftest.ps1` and the headless mode
are in commit `7332e84`; in `src/input.cpp` the right grip is bound to the grenade action, which
presses G; no binding sends the keys 1 to 4, F or V, or turns the mouse wheel; the Steam Frame's
left D-pad has no binding; zoom (the left trigger) is switched off while free hands are on; and no
file in `src/` calls `xrApplyHapticFeedback`. The working tree also holds another session's
uncommitted work (the flash probe and flash move in `src/effects.cpp`, a stand-in head pitch in
`src/input.cpp`); the binding facts above hold in both the committed and the working-tree
`input.cpp`. Everything else is carried over from the notes with their own marks.
