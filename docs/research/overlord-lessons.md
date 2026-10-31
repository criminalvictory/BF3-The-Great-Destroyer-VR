# Lessons from Overlord for BF3: The Great Destroyer VR

*Research note, 30 October 2026. A read-only study of the design documents of [Overlord][repo], a VR
mod for Call of Duty: Modern Warfare 2 Campaign Remastered ("H2"), at commit `2b3f225`
(29 October 2026), read through the GitHub API. Its `docs/` folder holds 139 files; the 55 of them
that touch what we build are cited in this note. Its claims were re-checked against those sources and
our own code on 30 October 2026.*

> **License boundary.** Overlord is GPL-3.0 ([license][license]). This note takes ideas only, in our
> own words. Never copy, translate or closely paraphrase Overlord code into this project. The numbers
> quoted (distances, speeds, times) are design facts from their documents, given so we can compare
> tuning; they are not code.
>
> **How claims are marked.** *They report* = Overlord's documents say so; we have not run Overlord or
> read its source. *(our notes)* = our own project notes or source files. **(unverified)** = anything
> about BF3 or Frostbite 2 internals, or a plan of ours, that nobody has checked yet.

## What this means for us

- **Stop fixed things from breaking again.** Overlord's biggest strength isn't a feature; it's how
  they keep fixes fixed. They treat "it compiled", "the automatic tests pass" and "the person in the
  headset confirmed it" as three different things and never mix them up. Their fixes routinely come
  with an automatic test that would have caught the bug, often built from a recording of the real
  failure, and those tests run on the PC without a headset. We should do the same: a quick self-test
  after every build, and a never-break checklist filled in for every build, before anyone is called
  to the headset.
- **Muzzle flash and casings.** They got the game's own flame and brass to come out of the VR gun by
  asking the game to create those effects at the VR gun's muzzle and ejection port at the moment of
  the shot, instead of moving the drawn pictures afterwards. They then had to stop the casings
  drawing on top of the hands (that fix still awaits their headset check). This is the route to try
  next for BF3: it keeps the vanilla flash and smoke, and short of tracking every particle it is the
  way to get smoke that stays where it was fired and casings that fly from where the port was.
- **Cutscenes and the car ride.** Their rules largely match our decision (section 3.2 has the one
  open point for a car): the game's camera only turns the body; the head stays free; the game
  camera's tilt is dropped; the player's own head movement is damped (to a tenth, at most 5 cm) so
  the view stays in the scene's camera; the view lines up once when the scene starts and hands back
  once when it ends. They recognise a scene from the game's own state, never from a timer or a
  guess.
- **Every gun is its own data.** Each weapon has its own profile (magazine, chamber, bolt, catches,
  sounds, hand poses), a page for the gun or its family, and a line in one spreadsheet of each gun's
  manual of arms (51 weapons). That is our "never copy the M416" rule worked out in detail; we can
  copy the method, not the code.
- **One referee for the hands.** A single piece of logic decides each frame what each hand is doing,
  and writes down why when it says no. Before that, features fought over the off hand and the result
  depended on the order the code ran in.
- **Strict gesture rules.** Slaps and strikes only count after the hand first moved away, then came
  in fast enough, far enough and from the right direction. Debug views draw the real hand next to the
  drawn hand, so tuning is based on what really happened.
- **What does not carry over.** Their engine internals (Call of Duty's models, scripts, settings,
  addresses) are nothing like Frostbite 2's, and much of their work (two guns at once, ladders,
  climbing, a settings launcher) isn't needed for BF3's campaign now.

---

## 1. Keeping fixed things fixed (validation)

This matters most for us: every regression costs a headset session.

### 1.1 What Overlord does

- **Four kinds of "done", never mixed.** Their documentation rules ask every topic to keep apart
  what is only proposed, what is implemented, what passed offline checks and what a person accepted
  in the headset. Offline success is never reported as headset acceptance, and a tester's "it looks
  right" counts only for what was actually tried ([docs index][idx], [diagnostics workflow][diag]).
  Example: one cleanup passed more than 10,000 offline checks with no failure, and every in-game row
  of its checklist still stayed *Pending* until a tester marked it Pass, Fail or Not covered
  ([regression checklist][rrc]).
- **Small test programs, one per area.** The logic that decides things (camera policy, hand
  arbitration, reload mechanics, controller input, grips, underbarrel...) lives in code with no game
  calls, so it runs on the PC with fake or recorded poses. A change runs the tests for its area
  ([development guide][dev], [weapon architecture, sections 4.1 and 9][weaparch],
  [camera architecture][cam]).
- **Bugs become tests built from the real failure.** The headset-standby orientation that
  turned the shoulders 180 degrees is replayed 1,000 times ([HMD resume heading][hmd]); the recorded
  left-hand contacts of a failed SCAR support grip are a test fixture ([underbarrel support][ubsup]);
  a captured arm motion with a 152-degree elbow flip is a test (now under 4 degrees)
  ([interaction comfort][comfort]); a rendering fix's test fails under the old code in all eight
  combinations of holding hand, grab or release, and which of two events arrives first
  ([support-grip render audit][support]).
- **Invariants over long random sequences.** Their weapon design asks for property tests that run
  long random sequences of eject, insert, drop, switch, charge, fire and outside ammo changes and
  check that no round is ever created or destroyed ([weapon architecture, section 11][weaparch]).
- **The shipped file is checked, not just the source.** A script reads the debug symbols of the exact
  executable to prove no feature was compiled out. They hit a compiler optimisation bug that only
  appeared in the optimised build (writes silently lost, a crash on level load), so acceptance
  belongs to the exact binary tested, never to "the same source" or an earlier build
  ([development guide][dev], [release validation][rel], [release preparation][relprep]).
- **Each hook checks the game's bytes and fails closed.** Before patching, a hook compares the game's
  code with what it expects; on a mismatch only that feature turns off, with a logged reason, and the
  game plays normally ([camera bob][bob], [lens flare][flare], [posture controls][stance],
  [gameplay architecture, section 14][gamearch]).
- **Status with reasons, and one-button evidence.** Their features have status commands that print
  their state, and many keep the *last rejection and its reason*. One console command, or one key
  combination (Ctrl+Shift+F8), collects two complete status reports, recent console history, traces,
  the VR settings and the latest crash reports into one zip in about five seconds, so the tester
  hands over one file instead of copying logs ([diagnostics workflow][diag]).
- **Debug views of the real geometry.** Optional overlays draw, in both eyes, the actual capture
  zones and the raw tracked hand next to the drawn hand. Their rule: never retune a threshold from one
  apparent miss; capture the failure and read why it was rejected first ([handle-slap
  diagnostics][hkdiag], [reload diagnostics][reldiag], [interaction diagnostics][intdiag]).
- **Agreed test windows.** Before a capture they get the tester's go-ahead, announce "starting now",
  run one 30-second window (at most 60 seconds, never silently extended), then announce "finished,
  the headset can come off". Outside the window they assume the headset is off
  ([equipment visibility capture][equip]).
- **The build is identified:** every incident record names the commit, uncommitted changes, the
  executable's and symbols' hashes and the process start time, and the status report itself carries
  the Git revision with a modified flag, the build configuration and the compile time
  ([diagnostics workflow][diag]).
- **Changes the headset rejects are withdrawn.** A culling change passed its maths tests, the tester
  saw no improvement, and it was taken out instead of kept "because the tests pass"
  ([equipment visibility capture][equip]).
- **A fixed shape for writing up a fix:** status, build, how to reproduce, evidence, cause (facts
  versus guesses), constraints, validation done, what is still not covered. Also: no frame-rate gains
  reported without a measurement, and when several changes ship together, no claim about which one
  helped ([diagnostics workflow][diag]).
- **Big rewrites go through an "observation mode".** Their migration plan for the hand rewrite runs
  the new logic beside the old one, only logging what it *would* decide; the two are compared; then
  the new logic takes over, and the old code is deleted after headset acceptance. Both systems never
  act at the same time ([hand-interaction redesign, section 11][handre]). Whether each step was
  carried out as planned is not recorded there (**unverified**).

### 1.2 What we already have (our notes)

- A never-break list: aim follows the gun and the view stays still (one and two hands), stick-turn
  direction, walking where you look, shots landing where the gun points, both eyes matching (HUD,
  blood, blur), 72 fps, the grip holding until release, reloading by hand. It lives in our notes, not
  as a record per build.
- [tools/selfcheck.ps1](../../tools/selfcheck.ps1): reads the log and prints ok/FAIL for six parts
  coming up (hooks, OpenXR session, wide FOV, god mode, bodies, depth buffer).
- [tools/weapontest.ps1](../../tools/weapontest.ps1): saves the game's own right-eye picture for a
  list of `weapon.txt` states into one contact sheet. It was written for the older depth-3D mode: it
  reads `build\eye_right.bmp`, while today's per-eye mode saves both eyes as `eye_both.bmp`
  ([src/xr_runtime.cpp](../../src/xr_runtime.cpp)), so it needs updating before it can be trusted
  (checked in the code, not run).
- In-game test switches: `weapon.txt` `viewlog N` (per frame: hand, written, game camera, head and
  shown yaw), `aimtest 20` (a fake 20-degree gun turn), F17 (a 3-second test turn), `eyetrace`,
  `tail`; a both-eyes picture test that measured the right-eye streaks; `tools/sampler` for CPU time.
- One fail-closed rule already in the Overlord spirit: [src/aim.cpp](../../src/aim.cpp) never writes
  through a look-angle pair it hasn't proved (the camera must be still, then follow a yaw and a pitch
  nudge, then come back).
- A CHANGELOG entry per upload; spoken test instructions and an alarm when a headset test is ready.
- New while this note was being checked (uncommitted): a first
  [tools/selftest.ps1](../../tools/selftest.ps1) that prints PASS/FAIL/SKIP lines (see 1.3, step 3).

Missing compared with Overlord: tests of the logic that run without the game; recordings of real
sessions to replay; a pass/fail record per build; the build's identity in the log (start-up logs
only "bf3vr loaded (x86 build)", [src/dllmain.cpp](../../src/dllmain.cpp)); features that switch
themselves off, with a logged reason, on a mismatch (a few checks exist: views.cpp hooks the view
builder only when its first bytes match, and the split-screen setting and god mode check an object's
type before writing; most hooks have none, and the view-builder check fails silently); a "last
rejection reason" for grabs, slaps and pulls; a one-key snapshot.

### 1.3 What to adopt: a check after every build (plan)

1. **`bf3vr_tests` (seconds, no game).** Move the pure maths out of the hook code into functions
   with no game or D3D calls, and test them with fixed numbers: yaw signs and view composition (hand,
   head, stick and game camera in; written look yaw and shown yaw out), the look-angle proof's
   accept/reject rule, the reload state machine (dry, locked back, magazine in, slap or pull,
   chambered), the ammo rule (magazine + chamber), grip catch and release, slap detection. Build it
   with CMake beside the DLL and make `build.bat` fail when a test fails.
2. **A session recorder and replay.** One line per frame into a file: head and controller poses,
   buttons and sticks, written and read look angles, shown yaw, scripted state, grip state, frame
   time. Each bug the tester reports becomes a recording with an expected result, replayed through the
   same functions as step 1. (Needs the refactor in step 1.)
3. **`tools/selftest.ps1` (about a minute; game running, headset not needed).** Selfcheck plus
   in-game checks that print PASS/FAIL: the F17 turn (the shown turn equals the request),
   `aimtest 20` (the view stays still, the game camera turns 20 degrees the right way), look angles
   found and proved, both-eye pictures of one frame (similar brightness, no streaks), frame time at
   72 Hz, the same number of free-hand draws in both eyes (the right eye once got about 6 more a
   frame, our notes). The switches exist, and a first version of this script appeared in the working
   tree (uncommitted) while this note was being checked: it prints PASS/FAIL/SKIP for the start-up
   parts, frame rate, the view staying still under `aimtest 20`, the weapon camera against the
   written yaw, the F17 turn's direction, the HUD in both eyes, streaks and log errors. Not yet in it:
   the free-hand draw count per eye, and the build's identity in its report.
4. **The tester checks only what a machine can't** (feel, comfort, reach) from a short spoken list,
   and the results go into a table per build: one row per never-break item, marked Pass, Fail or Not
   covered, and who checked it (self-test or tester). A build isn't called good, uploaded as a
   milestone, or rung through to the tester until its self-test rows pass.
5. **Smaller habits:** log the commit, a "modified" flag and the DLL's hash at start-up; give every
   hook a byte check that switches off only that feature; log a reason every time a grab, slap or
   pull is refused; one key that writes every subsystem's status and both eyes' pictures into one
   folder; when a fix doesn't help in the headset, take it out.

The three regressions our notes record, and the offline test that would have caught each:

| Regression (our notes) | Test that fails on the broken code |
| --- | --- |
| The camera moved with the gun again (the hand's yaw went in with the wrong sign) | Turn the hand 20 degrees with the head still: the shown view must not move, and the written look yaw must move 20 degrees the BF3 way. The broken code turns the view by twice the hand's turn. |
| A fix inverted the stick turn | Hold the stick right for one second at full: the view turns right by `turn_degrees` (266 degrees a second at full deflection). |
| Shots stopped landing where the gun points (a wrong look-angle pair was accepted while recoil or the smooth turn moved the camera) | Feed the proof's decision rule recorded camera readings in which recoil moves the camera during the nudge: it must answer "busy" or "reject", never "accept". |

---

## 2. Muzzle flash and spent casings at the VR gun

### 2.1 What Overlord did

- **They create the game's own effects at the VR gun, at the moment of the shot.** When a shot is
  accepted, they freeze two frames of the gun as it is drawn: its real muzzle and its ejection marker.
  The main thread then asks the game's effect system for that weapon's muzzle effect and its normal
  or last-shot shell effect at those frames, and the game owns the particles' lifetime and the shell
  physics. They report that the headset showed the flame and the ejected shells at the VR gun (tested
  before a later refactor that, they say, didn't touch this path) ([independent weapon fire][fire]).
- **Casings drew in front of the hands and gun.** The shell effect is flagged in the game's data to
  draw with the first-person model, which the engine turns into a special depth treatment, so even
  spawned in the world the casings drew in front of the hands and gun. They give the shell effect
  private copies of its data with only that flag cleared, leaving the shared game assets untouched.
  The muzzle flash kept its normal path. The headset check of this casing fix is still pending on
  their side ([ejection reference audit][eject]).
- **Find the port on the gun you hold.** Across 34 receiver models (skins counted separately), the
  third-person model's port marker and the first-person one disagree by up to about 19 cm after
  lining up the muzzles. So each adapted gun publishes its ejection point in its own receiver space,
  taken from the held gun's own skeleton, and nothing (left-hand use, a suppressor, another selected
  weapon) may move it to the other side. Several first-person markers sit on the gun's centre line
  while the visible port is on the side or top, so the final point has to be calibrated against the
  textured receiver with the bolt open: no blanket offset, no automatic snapping to a surface
  ([ejection reference audit][eject], [audit data][ejectjson]).
- **Pulling the bolt on a live round ejects a cosmetic cartridge**, only after the ammo change has
  been committed; short pulls, an empty chamber or holding the handle back do not
  ([ejection reference audit][eject]).
- **Everything the mod drops shares one world-space flight.** Dropped magazines, live rounds pulled
  from the chamber, shotgun shells, revolver cases, the M203's spent case and discarded launcher
  rounds use one presenter: a simple ballistic flight in world space (gravity, a fixed tumble), at
  most 1.2 seconds, ended by a collision test against the world, one flight shared by both eyes;
  letting go of the gun doesn't erase a round already in the air ([falling reload items][fall],
  [ejection reference audit][eject], [underbarrel runtime][ubrt]). The brass from ordinary shots is
  not part of it: that is the game's own shell effect with the game's own physics (above).
- **For draws they couldn't fix at the source** (lens flares, which the game builds already projected
  onto the screen) they lift each flare's corners back to the light's depth, using the camera that
  created it, inside the game's upload before the GPU reads it; then each eye's flare draw uses that
  eye's matrices, which are restored afterwards ([lens flare][flare]).

### 2.2 Where we are (our notes)

A draw census found the flash is five sprite layers (`Draw`, 6 vertices of 24 bytes, premultiplied
blending, stencil 0) plus a small mesh (36 indices, 32-byte vertices, stencil 2), and the casings an
instanced mesh (234 indices, stencil 2), all drawn where the game holds its own gun, at the chest.
Stencil 2 is the value BF3 gives world geometry; its first-person gun and arms get 32 (our notes),
and effects.cpp records the flash as drawn in the world pass. Moving the weapon camera and changing
the per-view constants at +48, +176 and +240 did not move them (+112 untested; effects.cpp has a
switch for it, `weapon.txt` `flashmats 8`, with no result recorded). The candidate next steps were
the sprite vertices in the dynamic vertex buffer, or the per-draw matrices at +0/+64. The aim is to
move the vanilla flash and smoke, not to replace them ([src/effects.cpp](../../src/effects.cpp)).

### 2.3 What to try, in order

Why the source matters: if we move the drawn effects every frame by "where our gun is minus where
the game's gun is", smoke is dragged along with the gun and casings already in the air jump whenever
the hand moves. Short of tracking every particle from its birth, only creating the effects at the VR
gun gives smoke that stays where it was fired and brass that flies from where the port was at that
instant (our reasoning). That is the approach Overlord took.

1. **Source route (best; feasibility unverified).** Find where BF3's client builds the transform for
   a shot's first-person effects. Frostbite 2 weapons most likely carry first-person fire effects with
   offsets relative to the weapon, and shell ejection may come from a socket on the first-person
   weapon (both **unverified**). If at that moment we can hand in the VR gun's muzzle and port frames
   in world space, the game creates its own flash, smoke and brass there. We already have the pieces
   for those frames: skin.cpp compares the first-person view-projection in arms space (the
   352-byte upload) with the one in world space (the 80-byte upload) to find the arms space's origin
   in the world, and `skin::GunDeltaWorld` gives the move from the game's gun to the drawn gun in
   world space (effects.cpp uses it). Whether that is precise enough at the instant of a shot is
   **unverified**. Start from the frame where the census sees the flash appear and trace back to
   whatever wrote its transform. Byte-check the hook; on a mismatch, fall back to today's behaviour.
2. **Then check which pass draws them.** The census points to the world pass: the flash mesh and the
   casings use stencil 2, BF3's world value (first-person draws use 32), so unlike Overlord's casings
   they should get world depth and each eye's own view once created at the VR gun (**unverified**;
   the sprites at stencil 0 are not classified yet). Only if some part turns out to be drawn in the
   first-person pass (stencil 32) do we need Overlord's kind of fix (clearing whatever marks it as
   first-person; BF3's equivalent is **unverified**) or skin.cpp's per-eye first-person shift.
   Accept it with a both-eye picture: the flame at the muzzle in both eyes, and casings hidden behind
   a wall when they fall behind it.
3. **Draw route as the fallback, for the flash only.** Apply the gun's rigid move to the sprite
   vertices inside the game's upload (Map/Unmap), per eye if needed, the lens-flare way. Fine for a
   flame that lives a few frames; wrong for smoke and casings.
4. **Take the port from our own model.** The dust cover (bone 3) spans x -0.024 to -0.010, y 0.030
   to 0.056, z 0.364 to 0.456 in gun model space, where x points left, so the port is on the right
   (our notes). Use the open port's centre and an exit direction outward to the right and a little up
   (the real AR-15 family's behaviour; **unverified** for BF3's effect). The muzzle is at the barrel's
   end, z about 0.78 (our notes; the bore's height is still to be measured). Each gun's profile
   carries its own port and muzzle frames; the AK-74 has no port cover (our notes) and, like the real
   gun, ejects to the right (**unverified** in BF3).
5. **Dropped magazines in world space.** Our dropped magazine, lying on the floor, followed the
   player because its anchoring (camera deltas applied to the arms space) was wrong, so it now
   vanishes on landing (our notes); that space's origin also jumps a metre at a time as the player
   walks (README). Overlord's rule suggests converting it to world space once, when it is released,
   and flying it in world space for both eyes from then on. A floor height taken from the soldier's
   position would do until we can query BF3's collision (**unverified**).

---

## 3. Scripted camera moments and cutscenes

### 3.1 Their camera policy

They split "who controls the view" into independent knobs, chosen per scene
([camera architecture][cam]):

- **Head rotation:** ignored / only tilt kept / limited / free.
- **Head movement:** none / damped with a spherical limit / full.
- **The game camera's rotation:** used as the base / its changes since the scene began are added /
  ignored.
- **Which of its axes:** yaw only / yaw and roll / all three.
- **Source:** the game's final view, or a named attachment point on the animated rig.
- **Entry:** keep the player's heading, or line up once with the game camera.

Rotations are combined as matrices, not by adding angles; a pitch passing straight up can't flip the
yaw by 180 degrees; recentering only resets the heading. One priority order (fixed thermal scopes,
then mounted guns, then scripted scenes, then normal play) picks the policy, and input and rendering
use the same choice.

### 3.2 Their rules for scripted scenes

- **Recognise a scene from the game's own state**: the player attached to a particular animated rig
  or vehicle, plus the mission script's own flags. Never from a map name alone, a timer, an angle
  threshold or a single "in a cutscene" switch ([scripted sequences][seq],
  [scripted free look][freelook], [camera architecture][cam]).
- **Keep separate decisions** for the camera, the arms (the game's scripted arms or the VR hands,
  never both), whether weapons are suspended, movement and turning, and story actions
  ([scripted sequences][seq], [scripted body arms][sarms]).
- **Line up once.** On entry the view aligns once with the scene camera; if that camera isn't ready
  yet, the alignment waits; a new rig inside the same scene re-bases without a jump; invalid data
  holds the current heading and recovers without a catch-up snap; on exit, control is handed back
  once, without a double turn ([camera architecture][cam], [scripted free look][freelook]).
- **Follow the scene's turns, not its tilt.** "Authored yaw": the view takes the game camera's
  horizontal turns and drops its pitch; roll is kept only where the shot really banks (a
  helicopter). The yaw is measured on a stable axis that switches to another axis near vertical, so it
  never makes a false half turn ([scripted free look][freelook]).
- **Damp the player's own head movement.** While attached to a scripted camera, physical head
  movement is scaled to 10% (adjustable from 0 to 25%) with a 5 cm limit; the game camera's own travel
  stays at full size ([estate scripted scenes][estate], [scripted free look][freelook]).
- **After a checkpoint load** into a scripted state, one tracking and camera reset happens as soon as
  valid data arrives; a load into ordinary play uses up that chance without a reset
  ([camera architecture][cam], [scripted free look][freelook]).
- **Inputs can't leak across a scene.** A trigger held through a scene can't fire when it ends; a
  fresh press is needed ([scripted weapon control][sctl]).
- **Death** releases both grips, hides the interactive hands, frees the head (lined up once) with
  damped movement, and stops stick movement and turning ([player death][death]).

**The closest match to our car ride** is MW2's *Takedown* opening, where the player rides as a
passenger: a "seeded" free head (lined up once on entry, as [player death][death] defines it), head
movement damped to 10% (5 cm limit), the scripted camera still following the car and the duck and
get-out animations, normal head movement back once the player is detached, and the duck prompt
rewritten for VR controls ([scripted sequences, Takedown section][seq]). Their documents list the
seeded free head apart from the policies that add the scene camera's yaw, so whether the car's own
turns also turn the view in Takedown is not stated (**unverified**). For the vehicles the player
drives, the vehicle's yaw does turn the player's heading while the head stays free
([driver vehicles][vehicles]).

### 3.3 How it maps onto our plan

Our decision (our notes): in cutscenes and camera takeovers, like the car ride at the start of *Rock
and a Hard Place*, lock only the body to the game camera (its yaw and position), keep the head free,
keep the horizon level. In Overlord's terms: free head, authored yaw with pitch **and** roll dropped,
damped head movement, one alignment on entry.

Today (our notes, [src/aim.cpp](../../src/aim.cpp)): when the found look angles stop matching the
weapon camera (a vehicle, a cutscene, a load), the mod still writes through them for up to one
second, then drops them and searches again; nothing is written while none are found, and the hand's
yaw is frozen in the view turn ([src/xr_runtime.cpp](../../src/xr_runtime.cpp)).

To adopt:

- **A "scripted" state with explicit enter and exit**, each logged with its reason. Key it to a game
  signal; the candidates are all **unverified**: the soldier entering a vehicle seat, the active
  camera no longer being the soldier's, player input being disabled. Keep the look-angle mismatch
  only as a fallback.
- **The view during a scene:** the game camera's position, plus its horizontal heading (pitch and roll
  dropped), plus the alignment made at entry, plus the head's full rotation and damped head movement
  (10%, 5 cm).
- **Exit:** write the heading the player ends up facing into the look angles once. aim.cpp already
  sets the yaw outright each frame instead of adding steps (our notes), which is the right base. Add
  an offline test that entering and leaving a scene doesn't turn the view twice.
- **One pair of arms.** If BF3 shows its own scripted first-person arms in a scene (**unverified** for
  the car ride), our free hands and gun step aside. Overlord had the opposite bug at explosions and
  breaches: the game's own flat first-person gun and hands showed for moments, because their mod
  handed drawing back to the game whenever a script disabled weapons (or its own hand resources were
  missing) ([estate scripted scenes][estate]). Because we re-place the game's own first-person draws,
  our risk is pulling scripted arms or props to the controllers (**unverified**), so the scripted
  state must say which draws we may touch.
- **Weapons in scenes:** no firing while suspended, and the trigger must be released before the first
  shot afterwards.
- **QTEs (later missions).** Overlord's rule for story actions: each scene declares what the game is
  waiting for (a held button, a command, a weapon state) and the mod sends exactly that; an early
  press that is refused may be retried, holding never repeats, and an input held across a phase
  change never turns into an action; they never force mission flags, and avoid fake keyboard keys
  where the game's own input path exists ([scripted sequences][seq], [Cliffhanger props][cliff]). For
  our right-grip-for-every-prompt plan: inject the prompt's key (our existing scan-code path, a
  compatibility route) only while a prompt is detected, one press per squeeze, never auto-repeated.

---

## 4. Per-weapon mechanics

### 4.1 Their architecture rules ([weapon architecture][weaparch])

This page is their design; it still calls its full mechanical model a design target, while the
weapon pages in section 4.3 describe what is built.

- **The hands decide, not the animation.** Stock reload animations supply hand poses, sounds and
  effects, but their timing has no authority; the player's hands decide when a magazine leaves or the
  bolt closes.
- **A gun is an assembly of modules:** one body with a primary module and an optional underbarrel
  module. Each module has its own feed, chamber and action, ammo, muzzle, loading points and moving
  parts. Switching modes never resets either module.
- **Separate state axes instead of one reload enum:** feed (inserted, detached, built in), chamber
  (loaded, empty, spent), action (closed, open, cycling, locked open, held open), plus interaction
  and sync states.
- **One ammo rule:** rounds loaded = magazine + chamber. Splitting a game total (15 into 14 + 1) must
  never create a round; a full magazine plus a chambered round (16) can only come from reserve.
- **Pooled magazines:** taking a magazine out returns its rounds to reserve; drawing a fresh one
  reserves up to its capacity; inserting transfers them; cancelling refunds exactly once.
- **Each gun says what happens after the last shot:** locks open, stays closed, open bolt, or manual
  cycle. A gun locked open can't fire after a new magazine until its release or charging handle is
  worked.
- **Move first, commit second.** A mechanical change asks the game to apply it and only counts once
  the game confirms (compare, then write); there is never a second writable copy of the ammo.
- **Sounds, haptics and effects only follow committed changes** and never create them; if the game's
  own path already plays a sound, the VR path doesn't play a second copy.
- **Left-handed use swaps roles, not the model:** ports, catches and handles stay on their real side.
- **Unsupported guns fall back on purpose** to the game's own reload, never to half-working
  mechanics.
- **Tested off the headset first** with synthetic and recorded hand paths; the headset judges reach,
  comfort and feel.

### 4.2 How they add a gun

- **One registry and one folder per gun family.** The dispatcher has no weapon-name checks; each
  gun's data says which exact game model and capacity it accepts, and tests cover accepted and
  rejected setups ([weapon registration][reg]).
- **Evidence first, then a reviewed profile.** External tools dump each gun's models, animations and
  sound keys; analysis proposes moving bones, muzzle tags and hand-pose frames, all marked
  *candidate*. A person checks every bone in a viewer, records its axis, travel and rest position,
  and writes an explicit profile. The states are captured, candidate, desktop-verified,
  headset-pending, headset-verified (or fallback); a gun never jumps from candidate to
  headset-verified ([weapon asset workflow][assets]).
- **A one-sheet manual of arms** ([weapon mechanics CSV][csv]): for each weapon, its release
  mechanism (button, paddle, rock-and-lock, cylinder...), open or closed bolt, whether it has a
  separate hold-open release, fire modes, support-hand position, review status (confirmed,
  provisional, headset pending) and notes; for example, the AK's magazine is rock-and-lock and a
  controller button alone must not drop it.
- **One page per gun or family.** The M9 page records 15 + 1, which buttons do what, which strokes
  count, and what is still pending in the headset; the MP5/AUG/UMP page records the handles' travel
  (65, 120 and 85 mm) and which frame of the game's own reload animation each hand pose came from
  ([M9][m9], [MP5, AUG and UMP][handle]).

### 4.3 The weapon documents, briefly

- **M9** ([M9 reload][m9]). *Problem:* a pistol's slide lock, magazine and chamber without the stock
  animation. *Rule:* 15 in the magazine + 1 chambered; a full rearward slide stroke extracts once and
  the return feeds once, while short or reversed strokes do nothing; inserting a magazine never
  chambers by itself; the release button closes a locked slide, chambering only from a magazine with
  rounds (with an empty magazine inserted, that press drops the magazine instead and the slide stays
  locked); the grasp style is chosen at grab time and kept for the stroke. *For us:* this matches our
  notes on the M9 (slide locks back empty, slide release lever, magazine button on the grip). Their
  buttons (B/Y, A/X) assume two face buttons on each controller, like the Quest that their pages
  name; on the Steam Frame all four face buttons are on the right controller and the left has a
  D-pad (our notes), so our button choices need their own design.
- **Receiver bolt-catch slap** ([receiver bolt release][boltrel]). *Problem:* a bolt release needs a
  deliberate slap, not a touch. *Rule:* only with a free off hand, a loaded magazine fully inserted
  and let go of, and the gun locked open; the target stays on the gun's physical left whichever hand
  holds it; a 2.5 cm target radius; the hand must first be 7 cm away, then come in at least 2.5 cm,
  at 0.5 to 8 m/s, inside a 60-degree inward cone (their wording); one approach is one attempt, even
  if refused; the whole palm (heel to knuckles, thumb pad, edges), not just sample points on the
  fingers, counts as a striking surface. *For us:* our slap is a fresh touch on the receiver's left
  side above 0.25 m/s ([src/reload.cpp](../../src/reload.cpp), our notes). Adding move-away-first, a
  minimum travel, a direction cone and a speed ceiling would stop accidental releases, for example
  when reaching for the handguard.
- **Manual handle catch, MP5/AUG/UMP** ([manual handle catch][handle]). *Rule:* a full stroke is
  needed (MP5: 60 of 65 mm); lifting about 21 mm or turning about 19.5 degrees at the rear catches the
  handle open; a downward slap releases it (at least 0.5 m/s and 25 mm of travel; rearm after 100 mm
  away); gaps of more than 150 ms between samples reset the approach; travel counts from the hand's
  highest point, so lifting first doesn't spoil the slap. *For us:* our charging handle counts a 4.5 cm
  pull of its 7 cm travel ([src/reload.cpp](../../src/reload.cpp)), while their M16 bolt is drawn
  travelling up to about 81 mm and resting at 78 mm when locked back (authored visual values,
  [bolt presentation][bolt]). A shorter pull can be a comfort choice, but decide it per gun.
- **Magazine latch contact** ([magazine latch contact][latch]). *Problem:* striking a release paddle
  with a spare magazine, the AK way. *Rule:* the whole spare magazine is one box swept against the
  latch point between samples, so fast hits aren't missed; a contact uses up the approach even if it
  is refused; the box must leave the rearm radius (AK: 4 cm latch, 7.5 cm rearm) before another try;
  after a successful strike the new magazine can't seat for 300 ms; the old magazine flies off with
  80% of the hit's speed (at most 3 m/s) and a limited tumble. *For us:* this is the AK-74's paddle
  release (our notes), done physically.
- **Launchers** ([launchers][launch]). *Rule:* single-shot launchers are their own family, not
  borrowed magazines; an RPG rocket seats when the last 6 cm of its tail meets the muzzle ring
  (65-degree and 7.5 cm tolerances); disposable tubes stay spent; the Javelin's muzzle marker is
  tilted about 10 degrees from its body, so it got its own reviewed muzzle definition instead of a
  looser general check, and the Stinger's sight check uses its own offset tube sight; the game still
  creates the projectile, from the physical muzzle. *For us:* the SMAW and the rifle's launcher mode
  make aiming go wrong (our notes); a per-launcher muzzle and sight axis is the first thing to check
  (the cause is **unverified**).
- **Underbarrel** ([underbarrel runtime][ubrt], [underbarrel support][ubsup]). Already in our notes:
  the launcher is a module with its own ammo, a firing grip at its own trigger (grip + trigger), one
  trigger press at most one shot, and the spent case dropping once the moving tube's rear edge passes
  it (about 40% of the stroke). New details: the support grip's capture was widened after recorded
  left-hand failures (75 degrees from palm-up; release at 22 cm); the fired case falls from rest,
  with no push or spin, through the shared falling-item presenter; letting go of the rifle's grip
  while the other hand holds the launcher keeps that hand's grasp. *For us:* the M320 differs from
  the M203 they modelled, because its breech swings open to the side instead of sliding forward (true
  of the real gun; **unverified** in BF3; check its reload animation). A mode switch should wait for
  the game to confirm the launcher is selected before firing, rather than relying on timed key
  presses ([weapon architecture, section 4.5][weaparch]).
- **Independent weapon fire** ([independent weapon fire][fire]). *Problem:* firing two held guns
  separately, without the game's akimbo mode. *Rules worth keeping for one gun:* fire rate, burst and
  cooldown run on the gun's own clock; after a hand-over, a focus loss or a stall the trigger must
  return to neutral, and no overdue shots are fired; every accepted shot also sends the game's own
  "player fired" event, because mission scripts wait for it; the effects come from the gun's real
  muzzle and port (see section 2). *For us:* we fire through the game's own input (an injected click
  while the pointer is on the game, our notes), so the game's own fire path and events run (by
  design; not separately tested); add the neutral-trigger rule after pauses, menus and headset
  sleep.
- **Chambering guide** ([chambering guide][chamber]). An optional hint, off by default: when live
  rounds are in the gun but the chamber is empty, the part to work (charging handle, slide, bolt)
  pulses yellow, in step in both eyes; it never changes ammo or input. A nice optional help once our
  reloads are all physical.
- **Ammunition presentation** ([ammunition presentation][ammo]). Magazines show 0, 1, 2 or 3 visible
  rounds (more than three look like three), a held spare shows its own count, and on some rifles the
  chambered round is drawn and moves with the bolt; each skin needs its own geometry, never face lists
  borrowed from another skin. Polish for later.
- **Bolt presentation** ([bolt presentation][bolt]). The bolt visibly cycles when firing and stays at
  its catch after the last round, split out of the receiver mesh where it shares a bone; handles that
  don't move with the bolt (HK) don't drag it. Our dust cover already swings on its hinge (our
  notes); a bolt visible through the open port is later polish.

### 4.4 Our guns for *Rock and a Hard Place* (from our notes; to confirm in game)

| Gun | Magazine / feed | After the last shot | Handles and catches | Port cover | Status |
| --- | --- | --- | --- | --- | --- |
| M416, the campaign rifle ("M16-type" in older notes) | 30-round box + 1 chambered | locks open (done) | charging handle at the rear top, bone 0 (done); bolt-release slap on the left (done) | yes, on a hinge, bone 3 (done) | in progress |
| M320 under the M416 | one 40 mm round | the spent case stays until the breech opens (real gun; **unverified** in BF3) | breech opens to the side (**unverified**) | none | designed |
| M9 | box magazine, button on the grip; 15 + 1 if like the real gun (**unverified**) | slide locks back | slide racks over the top; slide release lever | none | not started |
| AK-74 (picked up) | box magazine rocked in (front lug, then back), paddle behind it | stays closed: always rack after a new magazine | charging handle on the right, moves when firing; big safety lever on the right | none | not started |
| SMAW (named in our notes; whether it is in this mission is **unverified**) | one rocket (rear-loaded on the real gun; **unverified** in BF3) | n/a | n/a | n/a | aiming problem noted |

Fields to add per gun, after Overlord's sheet: release mechanism, open or closed bolt, separate
hold-open release, fire modes, support-hand position, muzzle and port frames, mesh dumped, collision
shape built, sounds recorded, review status (from notes / seen in game / accepted in the headset).

---

## 5. Hands and interaction

- **Hand alignment** ([hand alignment][align]). *Problem:* hands sat in the wrong place; a trial that
  guessed the palm from the glove model's knuckles was rejected in the headset. *Rule:* one explicit
  wrist point in the controller's grip frame; moving it never changes the aim angle; the defaults are
  zero, which the tester preferred; two parts of the code agreeing on the same guessed point only
  proves they are consistent, not that the point is right; to check a gun, compare its muzzle with
  the controller's aim ray (angle and sideways offset), and never bend bullets to hide a mismatch.
  *For us:* our left-glove line-up with the Steam Frame's View button (README) is that kind of
  explicit calibration. Keep it per controller model, and log the angle and sideways offset between
  the drawn muzzle and the controller's aim ray, to catch drift.
- **Unified hand interaction** ([hand interaction runtime][handrt], [redesign][handre]). *Problem:*
  features asked each other "are you busy?", so results depended on the order the code ran in; a
  script notice with no real object blocked the first magazine draw; several parts wrote the same
  hand bones one after another. *Rule:* one decision step per frame: read the input once, keep the
  current grasps, collect candidates, decide centrally, commit, publish; a better candidate can't
  steal a grasp already held; grabbing and keeping have separate distances (never widen the grab
  range to stop drops); one final pose per hand; shuffling the candidates must not change the result;
  every refusal is recorded with its reason. Empty hands get four poses from grip and trigger (open,
  pointing, pinch, fist), smoothed with a 45 ms half-life (about 90% of the way in 150 ms), and two
  eyes or repeated draws must not advance the smoothing twice. *For us:* before the M320, AK-74 and
  M9 add more left-hand uses, put one referee in charge of the left hand (support grip, charging
  handle, bolt release, magazine, launcher). Our skin.cpp splits each frame's uploads into the two
  eyes (our notes): keep finger smoothing and any state change once per frame, never once per eye.
- **Interaction comfort** ([interaction comfort][comfort]). *Rule:* when a hand attaches to or leaves
  a part, its position blends over 90 ms, computed relative to the controller so walking and turning
  still respond at once; the grasp style is chosen at grab time from the wrist's angle and kept until
  release; the arm's twist comes from the plane of shoulder, elbow and wrist, so elbows don't flip.
  *For us:* our grip settles and closes on contact (our notes); a short blend would hide the jump.
- **Foregrip reload** ([foregrip reload][foregrip]). *Problem:* a spare magazine vanished while the
  gun was held only by the foregrip, because the drawing code used the "may fire" check. *Rule:* "is
  this pose valid to draw" and "may this hand fire" are separate checks. *For us:* keep drawing checks
  and firing checks apart in hands.cpp and reload.cpp.
- **Falling reload items** ([falling reload items][fall]). Catching a falling magazine with the
  trigger held (65 mm of slack, a swept box for fast crossings), and the rule that a dropped
  magazine's rounds go back to reserve unless a penalty option is on. *For us:* our eject already
  returns the rounds to the spares (README); catching is polish for later.

---

## 6. Camera, tracking and standing play

- **Turning around the head** ([camera architecture][cam]). Normal play turns around the head's
  current tracked position, not the recentre point, and records which turn has been applied so none
  is counted twice; recentering and loads clear it. *For us:* once the head moves in 6DoF, the stick
  turn (written into the look yaw, our notes) must pivot around the head, or the world swings around
  the play area's centre whenever the player has stepped aside.
- **Camera bob** ([camera bob][bob]). *Problem:* walking with empty hands had no bob (the game
  multiplied it by zero), and switching bob off still left the aiming bob (the fix brings all seven
  call sites under one rule). *Rule:* one saved option, on by default, covering the camera's walking
  bob, positional and aiming, applied only to the VR camera; head tracking unaffected; the gun
  model's own bob, idle sway and recoil are left alone. *For us:* whether BF3's first-person camera
  bobs while walking or sprinting is **unverified**; since our view starts from the game camera, any
  bob reaches the eyes. Make it a setting and let the tester choose.
- **Headset wake-up heading** ([HMD resume heading][hmd]). *Problem:* after headset standby the
  shoulders faced 180 degrees the wrong way, because the Euler angles had jumped to an equivalent set
  (pitch 162, yaw 74, roll -177 is the same as pitch 18, yaw -106, roll 3). *Rule:* take the heading
  from the rotation matrix with a tilt-free formula that stays defined even looking straight up or
  down, keeping the last value only when nearly upside down; the game's input angles are kept
  separately. *For us:* our hand and head yaws come from the horizontal part of the forward direction
  (`LookAngles` in [src/xr_runtime.cpp](../../src/xr_runtime.cpp)) and the camera's from its back
  row (our notes); neither has a heading when that direction points straight up or down, for
  example the gun pointed at the ground while reloading. Use a tilt-free heading, or hold the last
  heading near vertical, and test it with recorded poses.
- **Standing play and crouch** ([gameplay architecture, sections 8 and 12][gamearch],
  [posture controls][stance]). *Rules:* leaning doesn't move the game's capsule; the head's offset is
  bounded; seeing or shooting through a wall must be prevented (pull the gun back, fade, or prompt,
  never a silent teleport); standing height is calibrated; a physical crouch needs that calibration,
  hysteresis, a minimum time held, an explicit option, and the game's own confirmation; leaning, a
  quick duck, picking something up or lost tracking must not toggle crouch. Backup on the right stick:
  push down to crouch (again for prone), up to stand; it acts at 0.90 and rearms below 0.30. *For
  us:* these are ready-made acceptance rules for roadmap item 2 (lean, duck and peek with a limit; F9
  to store the neutral height, as our notes plan it (today F9 recenters, which already puts the
  origin at the head's current position); a real crouch becomes a game crouch). Their
  physical-crouch rules are design rules; the stick control is built, with its headset check
  pending. How BF3 takes a crouch request is **unverified**.

---

## 7. Rendering in both eyes

- **First-person size** ([first-person size][fpsize]). An option from 20% to 100% (default 100%)
  scales the hands and personal weapons, separately from the world's scale; arms keep their real
  reach; weapons scale around the receiver; every contact point uses the same scale; it is fixed at
  start-up. *For us:* we already rewrite the gun and arm matrices per draw (our notes), so a uniform
  scale should be cheap (**unverified**); the collision field and grab points must scale with it.
  Polish.
- **Damage screen** ([damage screen][dmg]). The game's blood and low-health layers are captured and
  shown as if infinitely far away: the same picture by direction, through each eye's own projection,
  so there is no depth difference; captures expire after 250 ms and are hidden in menus. *For us:*
  overlay.cpp draws the damage pass into both halves (our notes), which also has no depth difference;
  if our two eyes' projections are ever asymmetric, a straight pixel copy no longer points the same
  way in both eyes (**unverified** whether ours are symmetric).
- **Dynamic-light stereo** ([dynamic light stereo][dynlight]). *Problem:* a see-through lighting
  layer slid outward in opposite directions in the two eyes (the light of a muzzle flash, a
  helicopter's spotlight). *Cause:* a second list of per-view data (up to eight lights) still held the
  centre camera's position. *Fix:* put each eye's position there only for that eye's scene, then
  restore it (their headset check is still pending). *For us:* that is the signature to look for in
  our dark layer around the arms, glove and gun (our notes suspect one first-person pass placed for
  the wrong eye; the gun's depth-only pass was once the "black copy"). In both-eye pictures, check
  whether the dark layer's offset points opposite ways in the two eyes; if it does, look for a
  first-person pass or per-view constant that isn't shifted per eye (**unverified**).
- **Lens flares** ([lens flare][flare]). Covered in section 2.1; also: sun flares and ghost
  reflections need their own rules, never a fixed pixel shift and never an invented sun depth. If
  BF3's flares look wrong in VR (**unverified**), the same per-eye lift applies. Low priority.

---

## 8. Other mission systems

- **Mounted turret** ([mounted turret][turret]). *Problems:* the visible gun and the shots disagreed
  because the firing code read the player's view angles again; feeding the game's own solved angle
  back into the next request piled up error; the game's eye was attached to the moving gun; the turret
  drew with the first-person depth trick. *Rules:* keep the requested aim separate from the game's
  result and use the result only for drawing; take the shot's geometry from the turret; seat the
  camera on a steady part of the vehicle, never on the gun; clear the depth trick for that model;
  taking the handles needs a full grip press and release; triggers rearm at neutral. *For us:* the
  first rule is what our look-angle bug taught us (set the yaw outright each frame; our notes). For
  BF3 vehicles, lock the body to the seat or the vehicle, not to a moving gun (how BF3 attaches its
  vehicle cameras is **unverified**).
- **Ladders** ([ladders][ladder]). Physical rung-by-rung climbing through a script-owned carrier
  that the game moves with collision checks. Deeply tied to their engine; not needed now (whether our
  mission has ladders is **unverified**).
- **Cliffhanger story props** ([Cliffhanger][cliff]). Mission items (ice picks, a C4 detonator) are
  shown as separate hand props while the script keeps control; the detonator only fires when the
  script allows it, a VR hand holds it, and the trigger was released after permission was given; it
  sends the game's normal fire input, never a fake key or a mission flag; boarding the snowmobile
  starts after standing within 1.6 m of it for 250 ms. *For us:* the pattern for mission items in
  later missions (**unverified** which ones).
- **Estate scripted scenes** ([estate scripted scenes][estate]). *Problem:* flat first-person gun
  hands flashed at explosions and breaches, because the mod handed drawing back to the game whenever a
  script disabled weapons. *Rule:* while VR hands are on, always hide the game's own personal
  first-person gun draws, through every transition; the game's hints are reworded for VR controls
  ("right stick down" to go prone) while keeping the game's own timing. *For us:* see section 3.3 on
  which draws we may touch in scripted states.

---

## 9. Performance, development and release

- **Performance** ([performance optimizations][perf]). No frame-rate claims without a measurement;
  comparisons use the same source, configuration, place and route; optional diagnostics are off by
  default and chosen at start-up; hot paths check "is anyone listening?" before doing any work; static
  data is cached; redundant operating-system memory queries were cut from render hot paths; the
  per-frame uploads advance once, after both eyes. *For us:* this matches our GetPerformanceInfo fix
  and keeping research hooks off by default (about +11 fps) (our notes). Add a fixed-route frame-time
  capture (stand at one spot, run the F17 turn) to the 72 fps row of the per-build table.
- **Development guide** ([development guide][dev]). Run the focused tests for the area changed; build
  both the optimised and the debug configuration (a passing debug build doesn't prove the optimised
  one); deploy with a helper that copies and hashes the files and keeps the replaced ones for
  restoring. *For us:* our rename-and-rebuild while the game runs (our notes) already keeps one
  previous copy (`bf3vr.old.dll`, overwritten each time); it could keep each replaced DLL under its
  own name and log the new one's hash.
- **Release validation** ([release validation][rel], [release preparation][relprep]). Before a
  release: audit the exact file; start it fresh, load a campaign level and play the affected part; for
  a regression, repeat the exact level and path that failed; record desktop and headset results
  separately; anything else stays a "candidate". *For us:* the same gate before the alpha tag.
- **Sounds and provenance** ([sound cues][sound], [source provenance][prov]). For weapon-handling
  sounds they play the game's own sound entries through the game's sound system and don't distribute
  extracted audio (they do ship a small compiled bundle of hit, kill and punch feedback recordings,
  some taken from later Call of Duty games, [release preparation][relprep]). Our reload sounds are
  recorded from the game and played by the mod (our notes), and are already kept out of the
  repository (README). Playing BF3's own sound assets through its engine would be cleaner, but
  whether that is feasible is **unverified**.

---

## 10. What does not apply to Frostbite 2

- **Their engine mechanisms**: Call of Duty's animated model objects, mission scripts and flags,
  console variables, command packets, asset zones, weapon definitions and animation notes, and every
  address and offset. The ideas carry over; none of the mechanisms do. Frostbite 2 works through
  entities, components and data containers, with its own effect and sound systems (details
  **unverified**).
- **Their stereo method.** They run the game's renderer once per eye and isolate per-eye resources
  themselves (the dynamic-light layer in section 7 was one they had missed). BF3's split-screen test
  already renders two full views (our notes); our matching risk is the per-view data the split
  screen doesn't make per eye, above all the first-person pass, which we shift ourselves.
- **Two guns at once.** A large weapon project of theirs (separate firing for two held guns) isn't
  needed for BF3's campaign. One lesson from it still applies: they had to re-send the game's
  "player fired" event because mission scripts wait for it. We fire through the game's own input, so
  BF3's scripts should see our shots (by design; not separately tested); keep it that way.
- **Ladders, ice-axe climbing, driving with handlebars, the riot shield, the settings launcher,
  translations, the updater**: not needed for *Rock and a Hard Place* now.
- **Their audio slicing through the engine's seek, their compiler-specific optimiser bug and their
  64-bit code relays** belong to their setup (we are 32-bit). The general lessons (test the exact
  binary; don't distribute extracted weapon-handling audio) still apply.

---

## 11. The 10 most valuable lessons, ranked

1. **Keep "built", "self-tested" and "confirmed in the headset" apart, with a pass/fail never-break
   table for every build.** Nothing reaches the tester until the self-test rows pass. *First step:*
   `tools/selftest.ps1` writing PASS/FAIL lines (a first version now exists, uncommitted), and a
   table per build.
2. **Turn every fixed bug into a test that runs without the game, built from a recording of the real
   failure.** *First step:* move the yaw, stick and view-composition maths into pure functions and
   test the three recorded regressions (section 1.3).
3. **Fix effects at their source.** Create the vanilla muzzle flash, smoke and casings at the drawn
   gun's muzzle and port at the moment of the shot, then make sure they draw with world depth in both
   eyes; move drawn vertices only as a fallback, and only for the flash. *First step:* trace where
   BF3 builds the first-person fire effect's transform (section 2.3).
4. **Treat scripted moments as an explicit policy recognised from the game's own state:** the body
   follows the game camera's heading (level horizon), the head stays free, head movement is damped to
   10% (5 cm), one alignment on entry, one hand-back on exit, one pair of arms. *First step:* a logged
   scripted state for the car ride, keeping the current look-angle mismatch only as a fallback.
5. **Every gun is data, with its own page and a shared manual-of-arms sheet:** separate magazine,
   chamber and action states; loaded = magazine + chamber; the hands decide, the animation doesn't;
   ammo is written only after a compare; unsupported guns fall back to the game's reload on purpose.
   *First step:* the per-gun profile, with the M416's specifics moved into it (our notes).
6. **One referee per frame for the hands**, with a written reason for every refusal and one final
   pose per hand, updated once per frame and never once per eye. *First step:* route the left hand's
   support grip, charging handle and bolt-release slap through one decision before adding the M320.
7. **Gestures need strict, evidence-based rules:** move away first, then enough speed, enough travel
   and the right direction; reset on gaps between samples; one approach is one attempt; use the
   tracked hand, not the drawn one; tune only from recorded failures shown in a debug view. *First
   step:* add these rules to the bolt-release slap.
8. **Fail closed, feature by feature:** byte-check every hook and switch off only that feature on a
   mismatch; keep the last rejection reason for each feature; one key collects all the evidence.
   *First step:* a build-identity line and one status line per subsystem in `bf3vr.log`.
9. **Standing-play rules before standing-play code:** leaning doesn't move the game body; a crouch
   needs calibration, hysteresis, a minimum hold and the game's confirmation; the stick turn pivots
   around the head; headings come from a tilt-free matrix formula. *First step:* write these as the
   acceptance rows of roadmap item 2.
10. **Agree test windows with the tester, record the build in every report, and withdraw a change
    the headset rejects** instead of stacking fixes on top of it. *First step:* add "starting now" and
    "finished, headset off" to our spoken test instructions, and log the DLL's hash at start-up.

---

## Appendix: verdict per Overlord document

| Overlord document | Section | Verdict for us |
| --- | --- | --- |
| [Camera architecture][cam] | 3, 6 | Adopt now: scripted policy knobs; turn around the head |
| [Camera bob][bob] | 6 | Later: a setting, once we know whether BF3 bobs |
| [HMD resume heading][hmd] | 6 | Adopt now: tilt-free heading, pole guard |
| [First-person size][fpsize] | 7 | Later polish |
| [Damage screen][dmg] | 7 | Already similar; check the eyes' projections |
| [Dynamic light stereo][dynlight] | 7 | Idea: diagnostic for the dark layer |
| [Lens flare rendering][flare] | 2, 7 | Idea: the per-eye vertex lift as a fallback |
| [Ejection reference audit][eject] | 2 | Adopt now: port frame from our own gun model; world-depth casings |
| [Falling reload items][fall] | 2, 5 | Adopt now: world-space flight; catching later |
| [Foregrip reload][foregrip] | 5 | Adopt: separate drawing and firing checks |
| [Hand alignment][align] | 5 | Already similar; add a muzzle-versus-aim-ray log |
| [Hand interaction runtime][handrt] / [redesign][handre] | 5 | Adopt before more left-hand uses |
| [Interaction comfort][comfort] | 5 | Later: 90 ms attach blend, latched grasp styles |
| [Independent weapon fire][fire] | 2, 4, 10 | Adopt: effects at the real muzzle, neutral trigger; dual wield does not apply |
| [Launchers][launch] | 4 | Adopt: per-launcher muzzle and sight axis |
| [Underbarrel runtime][ubrt] / [support][ubsup] | 4 | Adopt for the M320 (on the real gun its breech differs from the M203's) |
| [Magazine latch contact][latch] | 4 | Adopt for the AK-74 |
| [Manual handle catch][handle] | 4 | Idea: gesture thresholds; the HK catch itself doesn't apply |
| [M9 reload][m9] | 4 | Adopt for the M9 (buttons differ on the Steam Frame) |
| [Chambering guide][chamber] | 4 | Later: optional hint |
| [Ammunition presentation][ammo] | 4 | Later polish |
| [Bolt presentation][bolt] | 4 | Later polish |
| [Mounted turret][turret] | 8 | Idea: requested versus solved aim; seat-based camera |
| [Estate scripted scenes][estate] | 3, 8 | Adopt: one owner for the arms; damped head |
| [Cliffhanger][cliff] | 8 | Idea: story props and story actions |
| [Ladders][ladder] | 8 | Does not apply now |
| [Performance optimizations][perf] | 9 | Already similar; add a fixed-route capture |
| [Diagnostics workflow][diag] | 1 | Adopt now |
| [Release validation][rel] | 1, 9 | Adopt before the alpha tag |
| [Development guide][dev] | 1, 9 | Adopt: focused tests; check the optimised build |
| Also read: [scripted sequences][seq], [scripted free look][freelook], [scripted control][sctl], [scripted arms][sarms], [player death][death], [driver vehicles][vehicles] | 3 | Adopt now |
| Also read: [weapon architecture][weaparch], [registration][reg], [asset workflow][assets], [mechanics CSV][csv], [receiver bolt release][boltrel] | 4 | Adopt now |
| Also read: [regression checklist][rrc], [equipment capture][equip], [handle-slap][hkdiag], [reload][reldiag] and [interaction][intdiag] diagnostics, [support-grip render audit][support], [gameplay architecture][gamearch], [posture controls][stance], [sound cues][sound], [provenance][prov], [release preparation][relprep] | 1, 6, 9 | Adopt the practices |

## Sources

All Overlord links point at commit `2b3f225080916f58e294f53c698ca6c15727f483` (29 October 2026).

[repo]: https://github.com/Aeka0/Overlord
[license]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/LICENSE
[idx]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/README.md
[dev]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/development.md
[rel]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/release-validation.md
[relprep]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/releasing.md
[prov]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/source-provenance.md
[perf]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/performance-optimizations.md
[diag]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-diagnostics-workflow.md
[rrc]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-readability-regression-checklist.md
[equip]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-equipment-visibility-capture.md
[reldiag]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-reload-diagnostics.md
[intdiag]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-interaction-diagnostics.md
[hkdiag]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-hk-slap-diagnostics.md
[cam]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-camera-architecture.md
[bob]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-camera-bob.md
[hmd]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-hmd-resume-heading.md
[fpsize]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-first-person-size.md
[dmg]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-damage-screen.md
[dynlight]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-dynamic-light-stereo.md
[flare]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-lens-flare-rendering.md
[eject]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-ejection-reference-audit.md
[ejectjson]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-ejection-reference-audit.json
[fall]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-falling-reload-items.md
[foregrip]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-foregrip-reload.md
[align]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-hand-alignment.md
[handrt]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-hand-interaction-runtime.md
[handre]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-hand-interaction-redesign.md
[comfort]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-interaction-comfort.md
[fire]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-independent-weapon-fire.md
[launch]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-launchers.md
[ubrt]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-underbarrel-runtime.md
[ubsup]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-underbarrel-support.md
[latch]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-magazine-latch-contact.md
[handle]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-manual-handle-catch.md
[m9]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-m9-reload.md
[chamber]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-chambering-guide.md
[ammo]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-ammunition-presentation.md
[bolt]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-bolt-presentation.md
[turret]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-mounted-turret.md
[estate]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-estate-scripted-scenes.md
[cliff]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-cliffhanger.md
[ladder]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-ladders.md
[seq]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-scripted-sequences.md
[freelook]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-scripted-free-look.md
[sctl]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-scripted-control.md
[sarms]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-scripted-arms.md
[death]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-player-death.md
[vehicles]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-vehicles.md
[weaparch]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-weapon-interaction-architecture.md
[gamearch]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-gameplay-interaction-architecture.md
[reg]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-weapon-registration.md
[assets]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-weapon-asset-workflow.md
[csv]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-weapon-mechanics.csv
[boltrel]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-receiver-bolt-release.md
[sound]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-weapon-sound-cues.md
[stance]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-stance-controls.md
[support]: https://github.com/Aeka0/Overlord/blob/2b3f225080916f58e294f53c698ca6c15727f483/docs/vr-support-render-audit.md
