# BC2VR (Bad Company 2 VR): what it is, what works, and what it teaches us

Research note for **BF3: The Great Destroyer VR**, written 2026-10-30.

Sources: the [BC2VR repository][bc2vr] as of its latest commit (2026-10-28, `1a175e0`), and the
[FrostbiteVR][fvr] source it is built from: the `main` branch (`b6b8975`) and the newest
development branch `checkpoint/436-r56-equipment-rendering` (`75dacb4`, pushed 2026-10-30).
Links to FrostbiteVR files are pinned to those commits.

I did not run BC2VR or Bad Company 2. Everything about how BC2VR behaves comes from its author's
own notes, release notes and code. Where I add my own guess, or say something about BF3 that our
project has not confirmed, it is marked **(unverified)**. The claims were checked a second time
against the cited files and our own code on 2026-10-30, and corrected in place.

---

## What this means for us (short version)

- **What it is.** BC2VR is a VR mod for the Bad Company 2 campaign, by the same person who made
  FrostbiteVR. The BC2VR page holds no code at all: it is the shop window (installer, updates,
  feedback forms and release notes). All the code lives in FrostbiteVR, which is MIT licensed, so we
  may learn from it freely and could reuse pieces if we keep its copyright notice. BC2VR itself has
  no license, but there is nothing in it to reuse anyway.
- **How far along it is.** Early "pre-alpha", under two weeks of public releases. Testers can play
  the first fight with knife kills and reloads by hand, grip guns with two hands and swap guns from
  shoulder holsters. Still broken: crashes after dying, a C4 throw that blocks the first mission,
  wrong-looking destroyed buildings, cutscene cameras, vehicles, and poor performance in the headset.
  Nobody has finished the campaign with it.
- **Same basic idea as ours.** It makes the game draw each eye as a separate view from one game
  update, as we now do with BF3's built-in split-screen test. That confirms our choice. Its history
  also warns us: again and again one more effect (clouds, debris, decals) turned out to use the
  wrong eye's camera, and each one needed its own fix. We should look for those on purpose.
- **Ideas worth taking.** (1) Move the gun where the game itself looks for it, so the game's own
  muzzle flash, smoke, shells and bullets come out of the gun in your hand: this is our open
  problem. (BC2VR does this for the bullets' starting point and for the gun's position as the
  game's effect code reads it; its notes do not show the flash following, and spent shells are not
  done.) (2) A fake headset for automatic tests, with pictures checked against known right
  answers. (3) Send controller presses straight into the game's own input system instead of faking
  a keyboard and mouse. (4) Walking in your room by letting the game walk the soldier, only as far
  as walls allow.
- **Mistakes to avoid.** Making a story-critical action work only through a new VR gesture (the
  knife kill, which blocked the first mission for days), or leaving a game action with no button at
  all (the game throws C4 with its "zoom" action, which had no VR button, and that blocked the first
  mission again). Leaving death and checkpoint reloads for later. Sending the pictures to the
  headset through a separate program, with the game drawing eye pairs only on request: headset
  performance stayed poor (the exact cause is not proven). Switching on dozens of guns at once
  without testing each one.
- **Nothing can be pasted in.** BC2 runs on an older engine branch (Frostbite 1.5, BF3 is Frostbite
  2) with different memory layouts and functions. Only ideas, and some game-independent MIT math,
  can carry over.

---

## 1. What BC2VR is

### 1.1 The repository

- **Contents:** a README, two issue forms ([bug report, feedback][bc2vr-templates]) and
  [releases][bc2vr-releases]. No source code; GitHub lists no language and no license.
- **Releases:** 11 public pre-releases, [v0.1.5][rel-0.1.5] (2026-10-18) to [v0.1.15][rel-0.1.15]
  (2026-10-26). Each has a setup zip (16.4 to 16.8 MB), a runtime zip and a small update manifest
  read by the launcher's updater. Download counts are small (highest for a zip on 2026-10-30: 68,
  the 0.1.12 runtime).
- **Issues:** none filed, open or closed, as of 2026-10-30 ([issues][bc2vr-issues]). Tester
  feedback reached the author in other ways: logs and screenshots relayed by "the user" in
  FrostbiteVR's notes ([executable admission][fvr-exe], [0.1.17 bug list][fvr-bugs017]).
- **Commits:** 13, all by the author, 2026-10-18 to 2026-10-28: README and release-note changes,
  plus the two issue forms in the first commit ([history][bc2vr-commits]). The last one shortened
  the README; the longer version is in [13d7574][bc2vr-readme-long].

### 1.2 License

- **BC2VR:** no license file, so by default all rights are reserved; it contains no code.
- **What it ships:** FrostbiteVR's code, MIT licensed ("Copyright (c) 2026 BFVR contributors",
  [LICENSE][fvr-license]). The packages also carry MinHook (BSD), the Khronos OpenXR loader
  (Apache-2.0) and JsonCpp (MIT), plus a portable Python; a Granny animation-curve reader adapts
  LSLib (MIT); on first run the launcher downloads an LGPL FFmpeg build to decode the game's own
  audio ([notices][fvr-notices]; FFmpeg is named in the launcher's
  [runtime notices][fvr-runtime-notices] and the [FFmpeg recipe][fvr-ffmpeg], not in the main
  notices).
- **For us:** we may study FrostbiteVR freely; reusing its code means keeping its MIT notice.
  Our rule stands: ideas in our own words, no pasted code.

### 1.3 How it relates to FrostbiteVR

- **FrostbiteVR is older and is the source.** Its repository was created 2026-10-08, its first
  commit publishing the developer source at "checkpoint 202"; its notes go back to 2026-09-25
  ([BC2 discovery][fvr-discovery]). BC2VR is newer (2026-10-17) and is only the product side.
  Release notes point back to FrostbiteVR source checkpoints, for example v0.1.10 to commit
  [aaad4c7][fvr-aaad4c7] and v0.1.9 to the checkpoint 428 notes ([v0.1.10][rel-0.1.10],
  [v0.1.9][rel-0.1.9]).
- **Shared code: all of it.** The BC2VR packages are FrostbiteVR's BC2 adapter (a 32-bit DLL inside
  the game), its separate 64-bit OpenXR program, and a PowerShell/Python launcher
  ([README][fvr-readme], [OpenXR host][fvr-xrhost], [launcher folder][fvr-launcher]).
- **Lineage.** FrostbiteVR reuses math and policy (not engine addresses) from the same author's
  [BF2142VR][bf2142vr] (Refractor engine) and from [BFVR][bfvr] (Battlefield 1942 VR)
  ([README][fvr-readme]).
- **Public versus private builds.** Public BC2VR 0.1.15 ships the "checkpoint 434" game-side files.
  Newer private test builds 0.1.17 to 0.1.20 (2026-10-29/30) were installed only for the author's
  own tester and are not published; only FrostbiteVR's development branch records them
  ([current state][fvr-current]). FrostbiteVR's `main` still describes 0.1.7.
- **A BF4 port exists.** Branch [bf4/vr-adapter][fvr-bf4] (2026-10-18 to 21). BF4 is a closer
  relative of BF3 than BC2 is. One notable fact: BF4's own split-screen stereo switch is read-only
  on PC and forcing that route crashed BF4, so they draw each eye another way
  ([BF4 state][fvr-bf4-current], [BF4 stereo][fvr-bf4-stereo]). That belongs in the FrostbiteVR
  write-up; it is outside this note.
- **How it is built.** The notes read like ours: AI coding agents ("Root", numbered "CHAT" agents,
  a coordinator) write the code and "the user" tests in the headset; the agents' rules are in
  [AGENTS.md][fvr-agents], and the development branch also has a [CLAUDE.md][fvr-claude] for
  Claude Code (the workflow itself is inferred from the documents).
- **Size.** GitHub counts about 11 MB of C++ and 2 MB of Python in FrostbiteVR, and each build runs
  291 (0.1.5) to 337 (0.1.20) C++ test suites per architecture ([README][fvr-readme],
  [current state][fvr-current]). Our mod is about 13,600 lines of C++ in src/*.cpp at our last
  commit (about 14,600 with the headers).

### 1.4 Hardware it has been tried on

- The author's tester: Quest 3 through Steam Link and SteamVR ([OpenXR host][fvr-xrhost]).
- Others: RTX 5080 with a Ryzen 9 9900X and a Quest 3 at 90 Hz through Virtual Desktop (VDXR)
  ([performance][fvr-perf436]); a Pimax user; a Steam-version user ([admission][fvr-exe]).
  None of their notes mentions a Steam Frame.
- BC2VR needs BC2 on its DirectX 11 renderer; the game is a 32-bit program, like our bf3.exe
  ([features][fvr-features], [discovery][fvr-discovery]).

---

## 2. What works today and what doesn't

"Headset-confirmed" means a tester reported it working in a headset, as recorded by the author.

| Area | Status | Source |
| --- | --- | --- |
| Stereo picture | Headset-confirmed (accepted late September; called sharp on 2026-10-29) | [rendering repair][fvr-render-repair], [0.1.17 bugs][fvr-bugs017] |
| Aiming with the controller, horizontal room-scale walking | Headset-confirmed 2026-09-30 | [controls][fvr-controls] |
| Recenter, pause-menu laser pointer | Headset-confirmed | [features][fvr-features] |
| Opening fight: knife kills, story text cards, magazine reloads by hand | Headset-confirmed; one tester reached the scientist without a game-breaking problem | [long README][bc2vr-readme-long] |
| Shoulder holsters, gun swaps, two-hand grips on several guns | Headset-confirmed (shoulder holstering earlier; swaps and grips in 0.1.18) | [0.1.18 feedback][fvr-bugs018], [backlog][fvr-backlog] |
| SPAS shell loading, XM8 grenade-launcher sight flip, boat gun aimed by head | Accepted by early October; the XM8 sight flip then failed in the 0.1.18 test | [features][fvr-features], [0.1.18 feedback][fvr-bugs018] |
| Cloud depth in the opening cutscene | Fixed, headset-accepted (checkpoint 425) | [README][fvr-readme] |
| Crashes after dying / reloading a checkpoint | **Open since late September**, no accepted fix | [README][bc2vr-readme], [crash notes][fvr-crash] |
| Mission 1 C4 throw | **Blocks progress** (0.1.17) | [0.1.17 bugs][fvr-bugs017] |
| Destroyed buildings | Draw wrongly (an earlier report: left eye only); partly fixed, still reported | [0.1.17 bugs][fvr-bugs017], [backlog][fvr-backlog], [current state][fvr-current] |
| Cutscenes | Camera framing and facing wrong, a cutscene seen through a scope, ammo counter flashing | [0.1.17 bugs][fvr-bugs017], [backlog][fvr-backlog] |
| Performance in the headset | Described as "horrid" in 0.1.17; poor on an RTX 5080; low through VDXR | [0.1.17 bugs][fvr-bugs017], [performance 435][fvr-perf435], [README][bc2vr-readme] |
| Automatic fire | Feels bursty (XM8) since 0.1.17, still in 0.1.19 | [0.1.17 bugs][fvr-bugs017], [0.1.18 feedback][fvr-bugs018] |
| Newly added guns | No reload or ammo counter, LMG bullets off the barrel line, AN-94 underbarrel dead, LMG can't be stowed to empty hands | [0.1.18 feedback][fvr-bugs018] |
| Vehicles (jeep), the rest of the campaign, LMG reloads by hand, some scopes, reconnects | Not finished | [README][bc2vr-readme], [backlog][fvr-backlog] |
| Steam version of BC2 | Rejected by the executable check; experimental option added, gameplay untested | [v0.1.15][rel-0.1.15] |
| Multiplayer, full-body IK, VR settings menu, a VR-first main menu, other Frostbite games | Not built; later scope (the game's own menus already work in the headset with a laser) | [features][fvr-features], [backlog][fvr-backlog], [porting roadmap][fvr-porting] |

---

## 3. How it works, area by area, and what applies to BF3

Each part gives BC2VR's way, ours, and a verdict: **as is** (the idea carries over directly),
**with changes**, or **not at all**.

### 3.1 Drawing each eye (rendering)

**BC2VR**

- One game update feeds both eyes; the author's rules forbid running the game's frame a second
  time for the second eye ([AGENTS.md][fvr-agents], invariant 1 in [architecture][fvr-arch]).
- The second eye is a second "world view" that the mod creates with BC2's own view-making function
  and adds to the game's render request. On a VR frame the game's main view becomes the left eye
  and the added view the right eye ([NativeProbe.cpp][fvr-native-probe],
  [discovery][fvr-discovery], [performance][fvr-perf436]).
- Each eye gets the headset's real, lopsided field of view (OpenXR eye views are asymmetric). BC2's
  view has "crop" fields that shift the picture centre, and the mod turns the four FOV angles into
  BC2's vertical FOV, aspect and crop ([discovery][fvr-discovery]). Because BC2's culling ignores
  the crop, both eyes share one wider culling cone that encloses both eyes, so nothing is culled at
  the edges ([Bc2TrackedViews.h][fvr-tracked-views], [graphics 416][fvr-gfx416]). BC2 caps that
  culling cone at about 114.6 degrees.
- The hard part was state that the game keeps once per frame instead of once per view. The bugs
  came one at a time:
  1. 2026-09-25, first headset try: double images and severe flicker ([OpenXR host][fvr-xrhost]).
  2. 2026-09-27: both eyes were drawn with the right eye's camera, because some camera constants
     are shared between views. Fix: re-apply each eye's constants inside the game's own per-view
     prepare and draw steps, then restore them exactly. They noted that two eye pictures merely
     looking different proved nothing; only a check against a known expected pixel shift did. The tester
     then liked the picture but saw black dots near the centre (no cause established in the notes
     I read) ([rendering repair][fvr-render-repair]).
  3. 2026-09-30: with the added view switched on, the gun vanished from both eyes (the added view's
     first-person transform was left at a blank default). Fix: give each eye's gun pass its own
     camera and replace the gun's projection with the headset's, keeping the game's own depth
     values ([controls][fvr-controls]). This is the same idea as our skin.cpp gun-FOV rescale and
     per-eye split.
  4. 2026-10-17/18: clouds in the opening cutscene at the wrong depth. The sky's cloud pieces were
     positioned for the right eye; matching cloud features were 245 to 280 pixels apart between the
     eyes, against 0 to 8 for a distant hill. Fix: move those pieces to the left eye's position
     during the left draw; accepted in the headset ([weather 422][fvr-weather422],
     [423][fvr-weather423], [425][fvr-weather425], [README][fvr-readme]).
  5. 2026-10-27: a ring of bricks around a destroyed house floated in the left eye; one more game
     function built its own projection. Fixed for one test house; the tester still reports
     destruction artifacts ([current state][fvr-current], [0.1.17 bugs][fvr-bugs017]).
  6. 2026-10-30: skin decals and soot (destruction effects) used the wrong projection; a candidate
     fix is in [89c1e2d][fvr-89c1e2d], not yet checked in a headset
     ([destruction note][fvr-destruction]).
  7. Still open: a disappearing boulder (culling or level-of-detail suspected), a floating dot in
     the ACOG sight, HUD flicker ([backlog][fvr-backlog], [graphics 416][fvr-gfx416],
     [current state][fvr-current]).
- Render size: they set BC2's render size to the headset runtime's recommended size per eye
  (2784 x 3024 in their tests) by calling the game's own options "Apply" code. Writing the settings
  alone did not resize, and the game later reset itself (back to 1920 x 1080, and once to
  1024 x 768 through its own resolution-menu handler) ([native resolution][fvr-res436]).
- The desktop window shows a copy of the left eye ([graphics 416][fvr-gfx416]).

**Ours.** BF3's built-in split-screen test (`SplitScreenTestViewCount = 2`) makes the retail
renderer draw two complete views from one game state; our hook on the view builder writes each
eye's camera (src/views.cpp). The gun and arms are split per eye in skin.cpp, the second view sits
at x = 0 to avoid the streak bug, and the HUD is duplicated by overlay.cpp (our notes
bf3-per-eye-views.md, bf3-overlay-and-pointer.md). We render a symmetric wide FOV and give OpenXR
that same FOV (src/xr_runtime.cpp).

**Verdict**

- *Second view:* we already have the BF3 equivalent, and ours is simpler because the engine itself
  creates and owns both views. That probably spares us BC2VR's view-lifetime problems (their added
  view must be retired and rebuilt whenever the game replaces its world, at a level load or a
  checkpoint reload, and an early experiment with it was followed by a game exit, see
  [crash notes][fvr-crash], [architecture log][fvr-archlog])
  **(unverified until we test deaths and checkpoint loads)**.
- *Wrong-eye audit:* **as is.** Expect more effects in BF3 that use one view's camera for both eyes.
  Check sky and clouds, decals, destruction debris, particles, water, shadows and light shafts with
  both-eye pictures in Rock and a Hard Place **(unverified which ones, if any, are wrong in BF3)**.
- *Lopsided per-eye FOV:* **with changes.** It needs BF3's view description to accept an
  off-centre projection, which we have not looked for yet **(unverified)**. The gain: no pixels
  wasted on the inner side of each eye, and exactly the headset's FOV.
- *Shared culling cone:* only needed if we go lopsided; our cull camera is already per eye.
- *Runtime-recommended render size:* later, once 72 fps is solid.

### 3.2 Getting the pictures to the headset

**BC2VR**

- The game-side DLL is 32-bit; the OpenXR part runs in a separate 64-bit program
  (`BC2XrHost.exe`), a design carried over from BF2142VR to get around limits of 32-bit OpenXR
  runtimes ([architecture][fvr-arch]).
- Each eye pair is copied into GPU textures shared between the two programs, then copied again
  into OpenXR's swapchains. The bridge was built for the modern kind of shared texture (NT handles
  with keyed mutexes), but BC2's process refused to create those, so with BC2 it falls back to the
  older kind of shared texture with GPU completion checks ([D3D11 bridge][fvr-bridge]).
- The host runs the OpenXR frame loop by itself. When no new pair is ready it shows the last
  complete pair again, with the head pose it was drawn for, for up to 250 ms
  ([OpenXR host][fvr-xrhost]).
- Pairs are drawn on request. When the host has no request open, the game draws only its normal
  desktop frame; in one measured emulated run the game's frame counter moved on 4 to 7 frames
  (average 5.6) between finished VR pairs, which the author says does not prove that each of those
  frames drew the whole scene ([performance][fvr-perf436]). Later work added look-ahead requests
  and a third texture set: in an emulated test (fake headset) fresh pairs per second went from 44.6
  to 89.1 at 2784 x 3024 per eye ([v0.1.13][rel-0.1.13], [performance][fvr-perf436]).
- The host sends projection layers plus quad layers for the ammo counter and the menus; it sends no
  depth layer (a search of the whole development branch finds no OpenXR depth extension). It uses
  the "user presence" extension to notice the headset being taken off
  ([OpenXrHost.cpp][fvr-xrhost-src], [OpenXrAmmoHud.cpp][fvr-ammo-hud]).

**Ours.** OpenXR runs inside bf3.exe; every game frame yields both eyes and is submitted with the
pose it was drawn for (src/xr_runtime.cpp). `xrEndFrame` costs about 7 ms on the game's thread
(measured with the headset idle), and moving it to another thread did not raise the frame rate and
broke our D3D11 hooks (our notes bf3-vr-findings-2026-10-30.md, bf3-per-eye-views.md).

**Verdict:** **not at all** for the separate program. It adds copies, delay and complexity, and
their headset results stayed poor. Keep "one game frame = one stereo pair". One idea, **with
changes**: if the 7 ms `xrEndFrame` becomes our limit, their bridge shows that a second D3D11 device
can take finished eye pictures through a shared texture; inside our own process such a device could
submit on its own thread without switching the game's device into multithread mode
**(unverified for us)**; BC2 refused the modern kind of shared texture, so BF3 may too
**(unverified)**. If we ever want runtimes without 32-bit support (possibly Virtual
Desktop's VDXR or Meta's own runtime, **unverified**), their 64-bit host is the known fallback.

### 3.3 Head tracking, recentering and walking around

**BC2VR**

- The view sits on an upright "body base" taken from the game's own yaw; head pitch, roll and
  position go on top; 1 game unit is taken as 1 metre by default, not calibrated
  ([controls][fvr-controls]).
- Recenter: both stick clicks held for one second, keeping the crouch state. Each recenter starts a
  new "reference generation", so poses from before it never reach the game
  ([architecture][fvr-arch]). A bug where recentering added old head yaw a second time was found
  and fixed, but the fix still awaits a headset check ([main CURRENT][fvr-current-main],
  [README][bc2vr-readme]).
- Room-scale ("RoomscaleFollow"): uses the STAGE space if the runtime has one. Inside a 12 cm
  "lean radius" only the camera moves. Beyond it, the mod pushes the game's own movement input and
  subtracts only the distance the soldier really moved, so walls and collisions stay the game's
  ([controls][fvr-controls], [RoomscaleFollow.h][fvr-roomscale]).
- Crouch is a toggle (click the left stick) ([README][bc2vr-readme]). The shared core has a small
  "posture from height drop" class ([ComfortControls.h][fvr-comfort-controls]), but a text search
  of the development branch finds it used only by its own tests, not by the BC2 code.

**Ours.** Head rotation and eye offsets today; 6DoF with lean, duck and peek and a physical crouch
is roadmap item 2 (our note vr-roadmap.md).

**Verdict**

- *RoomscaleFollow:* **with changes**, directly useful for item 2; 12 cm is a sensible starting
  value for our lean limit.
- *Recenter generations:* **as is.** Cheap protection against the kind of "turn applied twice" bug
  we have had.
- *Physical crouch:* BC2VR has no proven version; their stance class is only a starting point.

### 3.4 Controllers and game input

**BC2VR**

- OpenXR bindings for Touch, Index and simple controllers ([controls][fvr-controls]).
- Controls: left stick moves; right stick snap-turns 30 degrees; right trigger fires; the grips
  handle weapons; clicking the left stick toggles crouch and holding Y sprints (so say the README
  and the 0.1.12 notes; FrostbiteVR's controls note and part of its input code have these two the
  other way round, **unverified** which one ships); X interacts and enters vehicles; both stick
  clicks for one second recenter; the left menu button (or X+Y for half a second) pauses
  ([README][bc2vr-readme], [v0.1.12][rel-0.1.12], [controls][fvr-controls]).
- Presses do not reach the game as fake keys. After BC2's own per-tick input step fills its table
  of 49 actions, the mod writes its values with the game's own setter functions; the next tick
  rebuilds the table. The game's per-action "allowed" bits are kept, so menus and cutscenes still
  block actions ([controls][fvr-controls], [vehicles][fvr-vehicles]).
- Aim and turning go through the game's aimer setter functions; a snap turn changes the game's
  aimer yaw by exactly 30 degrees ([controls][fvr-controls]).
- Menus: a laser pointer that uses BC2's own screen-coordinate conversion, so it works at any
  desktop size ([v0.1.10][rel-0.1.10]).

**Ours.** `SendInput` with scan codes; firing is an injected left click (which once minimised the
game); the pointer is kept on the window; look angles are found by a pair search and proved by a
nudge test (src/input.cpp, src/window.cpp, src/aim.cpp, our notes bf3-overlay-and-pointer.md,
bf3-look-angles.md).

**Verdict:** **with changes, high value.** BF3 also has a Frostbite input layer with named actions;
community reverse-engineering of BF3 and BF4 describes an input cache with "Concept..." entries
(fire, crouch and so on) that trainers write to **(unverified in our 1.6 campaign build)**. Writing
there would remove the window-focus, pointer and scan-code problems and let the game's own blocking
apply; later, "every QTE prompt on the right grip" becomes a single write. For aiming, look for
BF3's aiming object with its yaw and pitch (community notes name `ClientSoldierAimingSimulation` and
its aimer, **unverified**; BC2 has such an object, a soldier aiming simulation holding yaw and
pitch, per their [controls][fvr-controls] note) as a steadier route than our pair search, and keep
our nudge proof as the safety check. BC2VR has no Steam Frame profile; ours
(`XR_VALVE_frame_controller_interaction`) is ahead there.

### 3.5 Weapons and hands

**BC2VR**

- Gun and arms: at the moment BC2 copies the soldier's bone matrices for skinning, the mod hands
  that copy step its own edited set (the game's own animation data is never overwritten). Two-bone
  arm IK reaches the controllers, the gun's bone branch moves to the hand, and fingers and twist
  bones keep the game's animation. One change feeds the drawing of both eyes ([hands][fvr-hands]).
- The gun's world transform, as handed to the game's own attachment and effect code, is replaced
  too (the whole transform, position and orientation) ([hands][fvr-hands]). Their notes show the
  shot origin moving to the tracked muzzle; I found no statement that the muzzle flash was checked
  visually **(unverified)**, and spent-shell ejection is listed as not implemented
  ([checkpoint 412][fvr-cp412], [weapon coverage][fvr-coverage]).
- Bullets: at the final shot matrix (both the client "effects" shot and the local server shot) only
  the origin is replaced, with the tracked muzzle taken from the gun's flash bone; direction, spread
  and recoil stay the game's. Client and server shots are paired by their shared spread seed so
  both use the same pose ([firing origin][fvr-firing]). It started with two guns (XM8, SPAS-12),
  checked by the values written, not by where bullets land; 0.1.19 has measured muzzle points for
  181 configurations, yet the LMG's bullets were still off the barrel line
  ([0.1.18 feedback][fvr-bugs018]).
- Two-hand support grip; per-weapon profiles; offline Python tools read the game's own files
  (meshes, animations, weapon configurations) to measure grip points, magazines and sights
  ([porting roadmap][fvr-porting], their [tools folder][fvr-tools]).
- Reloads by hand move rounds through the game's own ammo transactions (their rule: the mod never
  invents rounds); SPAS shell loading and pump (headset feedback), bolt action (M95, emulated
  controllers only); pistol slide and LMG belt in progress ([current state][fvr-current],
  [backlog][fvr-backlog], [porting roadmap][fvr-porting]).
- Holsters: the intended layout is long guns on the back and shoulders, sidearms on the hip and
  gadgets on the chest (shoulder holstering has headset feedback); chest ammo pouches drawn by the
  host from models generated on the player's PC from their own game files; short sounds decoded
  from the game's audio ([runtime notices][fvr-runtime-notices], [features][fvr-features]).
- Results: in 0.1.18 two-hand grips and holster swaps worked on several guns, but on newly added
  guns the reload and ammo counter didn't start, LMG bullets didn't line up with the barrel, and an
  underbarrel and a sight flip failed. 77 weapon names (181 configurations) had been switched on in
  one go ([0.1.18 feedback][fvr-bugs018], [current state][fvr-current]).

**Ours.** skin.cpp and hands.cpp place the gun and gloves per draw; a collision field from the
gun's mesh (gunshape); charging handle and bolt-release slap; magazine eject; sounds recorded with
WASAPI loopback; the muzzle flash and casings still appear at the chest (our notes
bf3-gun-effects.md, bf3-hands-collision.md).

**Verdict**

- *Muzzle flash, smoke, casings and bullet origin:* **with changes, highest value.** The principle:
  change the gun's position where the game's own effect and shot code read it, not the sprites. For
  BF3 we need the transform the weapon's firing effects use (community SDK dumps of BF3 and BF4
  mention a weapon "shoot space" transform, **unverified**) and the final shot origin.
- *Rewriting the skeleton instead of placing per draw:* **with changes**, and a big change of
  approach. Our per-draw placement works and the user likes it; consider it only if effects and
  attachments can't be moved any other way. Even with skeleton edits they still needed per-eye gun
  projection fixes.
- *Weapon data from the game's own files:* **with changes**; fits our per-gun profiles (we already
  dump meshes).
- *Ammo through the game's own counters:* **as is**; we already write only the server ammo object.
- *Holsters and body inventory:* later.

### 3.6 HUD, menus and comfort overlays

**BC2VR**

- Ammo counter: its own OpenXR quad layer next to the gun, 20 x 3.75 cm, offset from the right
  controller's aim pose, hidden when nearer than 20 cm or farther than 1.8 m
  ([AmmoCounterPlacement.h][fvr-ammo-place], [OpenXrAmmoHud.cpp][fvr-ammo-hud]).
- Comfort guard: a GPU check samples a 16 x 16 grid in both eyes; if one eye is extremely black or
  white compared with the other, both eyes get the same treatment
  ([D3D11StereoComfort.h][fvr-comfort]).
- Body props (chest ammo, holstered guns) are composited by the host against the scene's depth,
  which the game side sends along with each eye. A small crosshair for the boat's grenade launcher
  (a candidate) is drawn after that on purpose, so nearby terrain cannot hide it
  ([graphics 416][fvr-gfx416], [handoff log][fvr-handoff]).
- How the game's own 2D HUD is shown is not documented **(unverified)**. Open: HUD flicker, the ammo
  counter flashing in cutscenes, missing vehicle crosshairs ([current state][fvr-current],
  [backlog][fvr-backlog], [0.1.17 bugs][fvr-bugs017]).

**Ours.** The game's HUD is drawn once and copied into the right eye; damage effects are routed per
eye; the ammo HUD reads one high (our note bf3-overlay-and-pointer.md).

**Verdict:** a counter beside the gun is **optional** (the user's call; nice for standing play).
The comfort guard is worth having **with changes**, as a cheap safety net for any future
one-eye-only flash or fade like our blood-in-one-eye bug. And hide our own overlays in cutscenes
(their bug).

### 3.7 Cutscenes and scripted cameras

**BC2VR.** Cutscenes are drawn in stereo: world pictures are allowed whenever the game declares
normal play, which includes cutscenes ([architecture log][fvr-archlog]). But in testers' reports
up to 0.1.17 the cameras were unreliable and the player faced away from the action, and in 0.1.17 a
cutscene on the second level looked through a scope and their ammo counter flashed
([0.1.17 bugs][fvr-bugs017], [backlog][fvr-backlog], [performance 435][fvr-perf435]). Days of
static analysis of BC2's cutscene classes (cutscene camera entities; data with a camera, a sequence
and "peek around" limits: maximum pitch, maximum yaw, snap back) produced no fix as of 2026-10-30
([camera 33][fvr-cine33], [cutscene metadata][fvr-cine-meta]).

**Ours.** Planned: the body locked to the game camera's yaw and position, horizon kept level, head
free; a scripted moment is detected when the game ignores the look angles we write (our note
cutscenes-and-qte.md).

**Verdict:** nothing to copy; the pitfalls are in section 5. One hint: BC2's cutscene camera data
carries designer limits on how far the player may look around. BF3 may have similar fields
**(unverified)** that could tell us when a scripted camera is active and how much free look the
designers allowed.

### 3.8 Performance tricks

**BC2VR.** Look-ahead requests, three texture sets, asynchronous GPU copies instead of blocking waits,
reading the skeleton in blocks (2,484 memory reads per snapshot down to about 191), no logging or
scanning every frame (diagnostics run only when asked for), an option to present the desktop
window without vsync, and the shared culling cone. Profiling found a possible slow spot inside BC2
itself (a helper that reads back from write-only mapped GPU memory; a lead, its cost not measured),
left unpatched ([performance][fvr-perf436], [architecture][fvr-arch]). Outcome: 89 pairs per second
in emulation, but poor in real headsets ([0.1.17 bugs][fvr-bugs017], [performance 435][fvr-perf435],
[backlog][fvr-backlog]).

**Ours.** The GetPerformanceInfo cache (it cost 11 ms a frame), research hooks off by default, the
sampler tool, 72 fps; the game is CPU-bound (our notes bf3-per-eye-views.md,
bf3-vr-findings-2026-10-30.md).

**Verdict:** **mostly not applicable**; their tricks serve their two-program design. Two
confirmations: never scan or log every frame (we learned the same with Toolhelp snapshots), and a
strong GPU alone did not give good headset performance (their RTX 5080 tester; the bottleneck there
was never identified, so it is **unverified** that it was the CPU). Our own frame is CPU-bound, so
measure the CPU first, as our sampler does.

### 3.9 Testing, packaging and support

**BC2VR.** A test-only OpenXR runtime (a DLL loaded by explicit path, never registered for the
whole PC) and scripted controllers drive the real game without a headset; eye pictures are checked
against known expected pixel shifts; long automated test suites; an installer with updater,
rollback and a diagnostics ZIP; an executable fingerprint check before any hook (the Steam BC2 build
was rejected, so an experimental option followed); menu automation by calling the game's own
"Continue" on its UI thread instead of clicking ([rendering repair][fvr-render-repair],
[firing origin][fvr-firing], [admission][fvr-exe], [monitor 209][fvr-monitor209],
[README][bc2vr-readme]).

**Ours.** tools/selfcheck.ps1 (committed) reads our log to check that each part of the mod started.
The never-break self-test of roadmap item 1 is being written now (tools/selftest.ps1: both-eye
pictures, stick turn, frame rate; not yet committed when this was checked). We also have the
drawhunt scripts, the Battlelog relaunch and a stand-in right hand for tests without controllers
(weapon.txt "standin 1").

**Verdict:** a fake headset with known-answer picture checks: **with changes**, very useful for
item 1. The OpenXR loader lets one process pick a runtime through the `XR_RUNTIME_JSON`
environment variable, but BF3 starts through Battlelog, so a "synthetic pose" mode inside our own
xr_runtime.cpp may be simpler (a "headless" mode that keeps the frame's work going from the
last-seen or a default head is being added there, uncommitted when checked)
**(unverified which works better)**. Packaging and fingerprint checks: later, at alpha.

### 3.10 Summary table

| Area | BC2VR | Ours | Verdict |
| --- | --- | --- | --- |
| Second eye | One added native view (right eye; the game's main view is the left), one game update | BF3 split-screen test views | Already done; ours likely sturdier (unverified) |
| Wrong-eye effects | Fixed one by one (camera, gun, clouds, debris, decals) | Gun, HUD, damage found so far | **As is**: audit on purpose |
| Eye FOV | Lopsided, exact headset FOV | Symmetric wide FOV | **With changes** (needs BF3 support, unverified) |
| Culling | One cone around both eyes | Per-eye cull camera | Only if we go lopsided |
| Render size | Runtime's recommended size via game's Apply | Fixed window size | Later |
| Headset delivery | Separate 64-bit program, shared textures, on-request pairs | In-process, every frame | **Not at all** |
| Recenter | Generations, keeps crouch | LOCAL space recenter | **As is** (generations) |
| Room-scale | 12 cm lean radius, then game movement | Planned (item 2) | **With changes** |
| Physical crouch | None (toggle) | Planned | No model to copy |
| Input | Game's own input actions | SendInput keys and clicks | **With changes, high value** |
| Aim | Game's aimer setters | Pair search plus nudge proof | **With changes** (unverified BF3 object) |
| Hands and gun | Skeleton rewrite with arm IK | Per-draw placement | Optional, big change |
| Muzzle and bullets | Gun transform and shot origin moved to the tracked muzzle (flash not shown following, no casings) | Flash and casings at the chest | **With changes, highest value** |
| Weapon data | Measured from the game's files | Per-gun dumps | **With changes** |
| Ammo honesty | Game's own transactions | Server ammo object | **As is** (already) |
| Ammo counter | Quad layer beside the gun | Game HUD in both eyes | Optional |
| One-eye flash guard | GPU check on both eyes | Per-eye routing | **With changes** |
| Cutscenes | Unsolved | Body lock planned | Pitfalls only |
| Performance | Pipeline tricks, emulated 89 pairs/s, poor in headsets | 72 fps, CPU-bound | Mostly not |
| Testing | Fake runtime, scripted controllers, known-answer pixel checks | Start-up log check; self-test being written | **With changes** |
| Packaging | Installer, updater, fingerprint check | None yet | Later |

---

## 4. The five most valuable lessons for us (ranked)

1. **Move the gun where the game itself reads it, so the game's own effects can follow.** BC2VR
   replaces the gun's transform as the game's attachment and effect code receives it, and the
   origin of the final shot (client effects and server shot alike), using the muzzle bone of the
   posed gun ([hands][fvr-hands], [firing origin][fvr-firing]). Their proof is mechanical so far:
   no note shows the flash following, spent shells are not done, and the LMG's bullets were still
   off in 0.1.18 and 0.1.19 ([0.1.18 feedback][fvr-bugs018]). For us this is the likeliest route to
   the vanilla muzzle flash, smoke and casings at the drawn gun, and to bullets leaving the real
   muzzle: the open item on roadmap step 1. Our work is to find BF3's matching transform and shot
   origin **(unverified)**; the sprite and constant-buffer routes in bf3-gun-effects.md stay
   ruled out.
2. **A fake headset and pictures with known right answers make the self-test trustworthy.** They
   drive the real game with scripted head and hand poses and check each eye against an expected
   pixel shift; two eyes merely looking different was not accepted as proof
   ([rendering repair][fvr-render-repair]). For our roadmap step 1 and the never-break list: script
   a head turn, a gun turn and a sidestep, then check both eyes, aim and frame rate automatically.
3. **Send controls through the game's own input actions, not a fake keyboard and mouse.** BC2VR
   writes its fire, move and jump values into BC2's per-tick action table and keeps the game's own
   "allowed" bits ([controls][fvr-controls]). For us this removes a whole family of bugs (the click
   that minimised the game, scan codes, focus) and makes the later QTE plan a single write
   **(BF3 equivalent unverified)**.
4. **Hunt wrong-eye effects on purpose, before testers find them.** Their second eye exposed
   wrong-eye drawing one effect at a time over a month (camera, gun, clouds, debris, decals). We
   should run a both-eye check of sky and clouds, decals, debris, particles, water and shadows in
   Rock and a Hard Place now, starting with the sky.
5. **Room-scale walking that lets the game move the soldier.** Inside a small lean radius (they
   use 12 cm) only the view moves; beyond it, push the game's normal movement and subtract only
   what the soldier really moved, so walls stay walls ([RoomscaleFollow.h][fvr-roomscale]). This is
   a ready design for the lean, duck and peek limit in roadmap step 2.

---

## 5. Pitfalls BC2VR ran into that we should avoid

1. **A story-required action had only a VR gesture, or no control at all.** The physical knife
   (there is no melee button) did no damage from 2026-10-17 on, which blocked the first mission; a
   first fix (0.1.9) still failed in the headset, a second shipped in 0.1.12 on 2026-10-20, and an
   outside tester confirmed it the next day ([v0.1.7][rel-0.1.7], [v0.1.12][rel-0.1.12],
   [knife status][fvr-knife417], [knife failure][fvr-knife430], [performance 435][fvr-perf435]).
   Then the C4 throw blocked mission 1 in 0.1.17 ([0.1.17 bugs][fvr-bugs017]): the game throws C4
   with its "zoom" action, which the mod had taken off the left trigger and given no other button
   (a fix is prepared but untested, [C4 handoff][fvr-c4]). Keep a plain button for every game action
   a mission can require (melee, grenade or throw, interact, plant or detonate, and any action a
   special item reuses), alongside any gesture.
2. **Death and checkpoint reloads were left for later.** Crashes after dying (first reported
   2026-09-25) are still open, and black screens after a restart, since recovered in emulated
   tests, are not yet accepted ([crash notes][fvr-crash], [restart test][fvr-restart],
   [features][fvr-features], [current state][fvr-current]). Test dying and "load last checkpoint" in
   every build from now on; it is part of our alpha goal anyway.
3. **A separate headset program with pictures made on request.** With it, testers reported poor
   performance in real headsets and a choppy feel when firing, even on a fast PC
   ([0.1.17 bugs][fvr-bugs017], [performance 435][fvr-perf435]); the author has not proven what causes
   either. Stay at one stereo pair per game frame and judge speed in the headset.
4. **Dozens of guns switched on from data in one go.** The newly tried guns (a pump shotgun, an LMG,
   an AN-94) then would not reload in the headset, and the LMG's bullets and the AN-94's underbarrel
   failed too ([0.1.18 feedback][fvr-bugs018]). One gun at a time, end to end, exactly as our weapons
   rule says.
5. **Wrong-eye effects found by testers one by one** (section 3.1). Audit first (lesson 4).
6. **Cutscene problems: wrong facing, a scope view, a flashing ammo counter**
   ([0.1.17 bugs][fvr-bugs017], [backlog][fvr-backlog]).
   When a scripted camera is active: hide gun overlays, scope effects and our own HUD props, take
   the body's yaw from the scripted camera (not from old player yaw plus a recenter offset), hand
   control back cleanly, and don't sink days into static analysis.
7. **A tug-of-war over the render size.** The game reset its own size through its menu code
   ([native resolution][fvr-res436]). Check our window and render size after every load and every
   menu visit.
8. **Small timing and ownership slips.** Recentering added head yaw twice
   ([main CURRENT][fvr-current-main]); one 59.6 ms frame hitch cancelled a magazine hold that had a
   50 ms limit ([monitor 209][fvr-monitor209]). Keep one owner for composing yaw, and make grip,
   slap and handle timings tolerate frame hitches.
9. **Different game builds.** The Steam build of BC2 was rejected by the fingerprint check, and
   testers used different stores and runtimes ([admission][fvr-exe], [v0.1.15][rel-0.1.15]). When we
   publish, check which bf3.exe build is running (Steam or EA app) and say clearly what is supported.
10. **Process weight.** Hundreds of checkpoint documents and 300+ test suites, yet many results were
    only "emulated", headset checks were comparatively rare (my reading of their notes), and
    user-visible bugs stayed open for weeks ([current state][fvr-current]). Automate what protects
    the never-break list, and keep headset tests frequent and short.

---

## 6. Unverified and open points

- That BF3 has an input-action table we can write like BC2's (community notes suggest so; not
  checked in our build).
- That BF3 has a stable aiming object with yaw and pitch (community name
  `ClientSoldierAimingSimulation`; not checked).
- Which BF3 transform the weapon's firing effects read, and where the final shot origin is formed
  (community "shoot space"; not checked).
- Whether BF3's view description supports an off-centre (lopsided) projection.
- Whether BF3's sky, clouds, decals, debris, particles, water or shadows use the wrong eye under the
  split-screen test (none reported by our user yet).
- Whether the split-screen route really avoids BC2VR's view-lifetime crashes on death and
  checkpoint loads (no death/reload test of ours yet).
- Which OpenXR runtimes lack 32-bit support. BC2VR only says 32-bit runtimes have limits; naming
  Virtual Desktop's VDXR or Meta's runtime is my guess.
- That BC2VR's room-scale and recenter designs feel good in practice: they have early headset
  acceptance only ([controls][fvr-controls]).
- Which crouch and sprint buttons BC2VR really ships: its README and its own controls note
  disagree.
- Everything above about BC2VR's internals is as its author documents it; I did not run it.

---

## Sources

BC2VR

- [BC2VR repository][bc2vr], [README][bc2vr-readme], [long README (13d7574)][bc2vr-readme-long],
  [releases][bc2vr-releases], [issues][bc2vr-issues], [commits][bc2vr-commits],
  [issue templates][bc2vr-templates]
- Release notes: [v0.1.5][rel-0.1.5], [v0.1.7][rel-0.1.7], [v0.1.9][rel-0.1.9],
  [v0.1.10][rel-0.1.10], [v0.1.12][rel-0.1.12], [v0.1.13][rel-0.1.13], [v0.1.15][rel-0.1.15]

FrostbiteVR (main, b6b8975)

- [README][fvr-readme], [LICENSE][fvr-license], [third-party notices][fvr-notices],
  [AGENTS.md][fvr-agents], [features][fvr-features], [CURRENT (main)][fvr-current-main]

FrostbiteVR (development branch, 75dacb4)

- State and plans: [CURRENT][fvr-current], [architecture][fvr-arch],
  [architecture log][fvr-archlog], [pre-alpha backlog][fvr-backlog],
  [porting roadmap][fvr-porting], [0.1.17 bug list][fvr-bugs017], [0.1.18 feedback][fvr-bugs018],
  [CLAUDE.md][fvr-claude], [runtime notices][fvr-runtime-notices], [tools folder][fvr-tools],
  [launcher folder][fvr-launcher]
- Rendering: [BC2 discovery][fvr-discovery], [rendering repair][fvr-render-repair],
  [Bc2TrackedViews.h][fvr-tracked-views], [NativeProbe.cpp][fvr-native-probe],
  [weather 422][fvr-weather422], [weather 423][fvr-weather423], [weather 425][fvr-weather425],
  [graphics 416][fvr-gfx416], [decal/soot fix 89c1e2d][fvr-89c1e2d],
  [destruction note][fvr-destruction], [native resolution][fvr-res436]
- Delivery and performance: [OpenXR host][fvr-xrhost], [OpenXrHost.cpp][fvr-xrhost-src],
  [D3D11 bridge][fvr-bridge], [performance 436][fvr-perf436], [performance 435][fvr-perf435]
- Input, tracking, weapons: [controls][fvr-controls], [vehicles][fvr-vehicles],
  [RoomscaleFollow.h][fvr-roomscale], [ComfortControls.h][fvr-comfort-controls],
  [hands][fvr-hands], [firing origin][fvr-firing], [knife status][fvr-knife417],
  [knife failure][fvr-knife430], [C4 handoff][fvr-c4], [checkpoint 412][fvr-cp412],
  [weapon coverage][fvr-coverage]
- HUD and comfort: [AmmoCounterPlacement.h][fvr-ammo-place], [OpenXrAmmoHud.cpp][fvr-ammo-hud],
  [D3D11StereoComfort.h][fvr-comfort], [handoff log][fvr-handoff]
- Cutscenes: [camera 33 handoff][fvr-cine33], [cutscene metadata][fvr-cine-meta]
- Lifecycle and support: [crash notes][fvr-crash], [checkpoint restart][fvr-restart],
  [executable admission][fvr-exe], [monitor 209][fvr-monitor209], [FFmpeg recipe][fvr-ffmpeg],
  [aaad4c7][fvr-aaad4c7]
- BF4 branch (428be84): [branch][fvr-bf4], [BF4 state][fvr-bf4-current],
  [BF4 stereo][fvr-bf4-stereo]

Lineage: [BF2142VR][bf2142vr], [BFVR][bfvr]

Our own project (local): src/views.cpp, src/xr_runtime.cpp, src/input.cpp, src/window.cpp,
src/aim.cpp, src/camera.cpp, src/skin.cpp, src/hands.cpp, tools/selfcheck.ps1,
tools/selftest.ps1 (uncommitted), and the memory notes bf3-per-eye-views.md,
bf3-overlay-and-pointer.md, bf3-vr-findings-2026-10-30.md, bf3-gun-effects.md,
bf3-hands-collision.md, bf3-look-angles.md, cutscenes-and-qte.md, vr-roadmap.md,
weapons-own-mechanics.md, bf3-relaunch.md.

[bc2vr]: https://github.com/justiceofrip/BC2VR
[bc2vr-readme]: https://github.com/justiceofrip/BC2VR/blob/1a175e0094ceb522441865544a2d2af592da6646/README.md
[bc2vr-readme-long]: https://github.com/justiceofrip/BC2VR/blob/13d7574bd682ae883a935a9adae52a59409b576f/README.md
[bc2vr-releases]: https://github.com/justiceofrip/BC2VR/releases
[bc2vr-issues]: https://github.com/justiceofrip/BC2VR/issues?q=is%3Aissue
[bc2vr-commits]: https://github.com/justiceofrip/BC2VR/commits/main
[bc2vr-templates]: https://github.com/justiceofrip/BC2VR/tree/1a175e0094ceb522441865544a2d2af592da6646/.github/ISSUE_TEMPLATE
[rel-0.1.5]: https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.5
[rel-0.1.7]: https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.7
[rel-0.1.9]: https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.9
[rel-0.1.10]: https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.10
[rel-0.1.12]: https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.12
[rel-0.1.13]: https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.13
[rel-0.1.15]: https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.15
[fvr]: https://github.com/justiceofrip/FrostbiteVR
[fvr-readme]: https://github.com/justiceofrip/FrostbiteVR/blob/b6b89755e2947d953ecc39796a29d722dc74468c/README.md
[fvr-license]: https://github.com/justiceofrip/FrostbiteVR/blob/b6b89755e2947d953ecc39796a29d722dc74468c/LICENSE
[fvr-notices]: https://github.com/justiceofrip/FrostbiteVR/blob/b6b89755e2947d953ecc39796a29d722dc74468c/THIRD_PARTY_NOTICES.md
[fvr-agents]: https://github.com/justiceofrip/FrostbiteVR/blob/b6b89755e2947d953ecc39796a29d722dc74468c/AGENTS.md
[fvr-features]: https://github.com/justiceofrip/FrostbiteVR/blob/b6b89755e2947d953ecc39796a29d722dc74468c/release/FEATURES.json
[fvr-current-main]: https://github.com/justiceofrip/FrostbiteVR/blob/b6b89755e2947d953ecc39796a29d722dc74468c/docs/CURRENT.md
[fvr-aaad4c7]: https://github.com/justiceofrip/FrostbiteVR/commit/aaad4c76eb062224f923cb3d05534019825b181a
[fvr-89c1e2d]: https://github.com/justiceofrip/FrostbiteVR/commit/89c1e2d72bf4e190740671adb657cec72a27ce99
[fvr-current]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/CURRENT.md
[fvr-arch]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/ARCHITECTURE.md
[fvr-archlog]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/history/2026-10-28-architecture-log.md
[fvr-backlog]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/PREALPHA-BACKLOG.md
[fvr-porting]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/FROSTBITE-PORTING-ROADMAP.md
[fvr-bugs017]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/archive/2026-10-29-bc2vr-0.1.17-buglist.md
[fvr-bugs018]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/archive/2026-10-30-bc2vr-0.1.18-buglist.md
[fvr-discovery]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/BC2_NATIVE_DISCOVERY.md
[fvr-render-repair]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/archive/RENDERING-REPAIR-20260927.md
[fvr-tracked-views]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/src/games/bc2/Bc2TrackedViews.h
[fvr-native-probe]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/src/games/bc2/NativeProbe.cpp
[fvr-weather422]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/archive/WEATHER-OBSERVATION-422.md
[fvr-weather423]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/archive/WEATHER-TRANSLATION-EXPERIMENT-423.md
[fvr-weather425]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/archive/WEATHER-COMPLETION-425.md
[fvr-gfx416]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/archive/GRAPHICS-FOLLOWUP-416.md
[fvr-res436]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/NATIVE-RESOLUTION-436.md
[fvr-xrhost]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/OPENXR_HOST.md
[fvr-xrhost-src]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/src/xr/OpenXrHost.cpp
[fvr-bridge]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/D3D11_BRIDGE.md
[fvr-perf436]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/PERFORMANCE-436.md
[fvr-controls]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/archive/CONTROLS-20260930.md
[fvr-vehicles]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/BC2-VEHICLES-CAMPAIGN.md
[fvr-roomscale]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/include/fvr/interaction/RoomscaleFollow.h
[fvr-comfort-controls]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/include/fvr/interaction/ComfortControls.h
[fvr-hands]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/archive/HANDS-20260930.md
[fvr-firing]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/FIRING-ORIGIN-20260930.md
[fvr-knife417]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/archive/KNIFE-PLAYER-STATUS-417.md
[fvr-knife430]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/history/2026-10-20-knife430-human-failure.md
[fvr-ammo-place]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/include/fvr/graphics/AmmoCounterPlacement.h
[fvr-ammo-hud]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/src/xr/OpenXrAmmoHud.cpp
[fvr-comfort]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/include/fvr/graphics/D3D11StereoComfort.h
[fvr-cine33]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/handoff/integrations/coordinator-20261030-camera33/README.txt
[fvr-cine-meta]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/handoff/tasks/BUG017-02-04/20261029-work13/HANDOFF.txt
[fvr-crash]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/CRASH-20260925.md
[fvr-restart]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/history/2026-10-26-r46-checkpoint-restart.md
[fvr-exe]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/EXECUTABLE-ADMISSION-436.md
[fvr-monitor209]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/archive/MONITOR-PRESENTATION-209.md
[fvr-handoff]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/history/HANDOFF.md
[fvr-ffmpeg]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/config/ffmpeg-download.json
[fvr-runtime-notices]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/launcher/runtime/THIRD_PARTY_NOTICES.md
[fvr-claude]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/CLAUDE.md
[fvr-tools]: https://github.com/justiceofrip/FrostbiteVR/tree/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/tools
[fvr-launcher]: https://github.com/justiceofrip/FrostbiteVR/tree/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/launcher
[fvr-perf435]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/history/2026-10-21-performance435.md
[fvr-destruction]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/handoff/integrations/coordinator-20261030-feedback56/DESTRUCTION.txt
[fvr-c4]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/handoff/tasks/BUG017-05/20261029-polish11/HANDOFF.txt
[fvr-cp412]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/archive/CHECKPOINT-412.md
[fvr-coverage]: https://github.com/justiceofrip/FrostbiteVR/blob/75dacb494bb41e41b9ff0340984d6f3812ed1ef9/docs/WEAPON-COVERAGE-CURRENT.md
[fvr-bf4]: https://github.com/justiceofrip/FrostbiteVR/tree/bf4/vr-adapter
[fvr-bf4-current]: https://github.com/justiceofrip/FrostbiteVR/blob/428be84fff6c0ed2384b911a6e9efbc667397479/docs/CURRENT-BF4.md
[fvr-bf4-stereo]: https://github.com/justiceofrip/FrostbiteVR/blob/428be84fff6c0ed2384b911a6e9efbc667397479/docs/BF4-STEREO.md
[bf2142vr]: https://github.com/justiceofrip/BF2142VR
[bfvr]: https://github.com/JayBiggsGMG/BFVR-Battlefield-1942-VR-Mod
