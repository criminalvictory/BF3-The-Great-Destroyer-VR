# FrostbiteVR: what works, what doesn't, and what BF3 VR can borrow

Research note, 2026-10-30. Source: [justiceofrip/FrostbiteVR](https://github.com/justiceofrip/FrostbiteVR),
read through GitHub only (nothing was built or run). Three snapshots were read:

- `main` at `b6b8975` (2026-10-19), the published Bad Company 2 (BC2) framework;
- [`bf4/vr-adapter`](https://github.com/justiceofrip/FrostbiteVR/tree/bf4/vr-adapter) at `428be84` (2026-10-21), a Battlefield 4 adapter;
- [`checkpoint/436-r56-equipment-rendering`](https://github.com/justiceofrip/FrostbiteVR/tree/checkpoint/436-r56-equipment-rendering) at `75dacb4` (2026-10-30), the newest BC2 work.

Each technique was compared with our own code (`src/`) and the project memory notes.

**Checked a second time (2026-10-30, late).** Every claim below was re-checked against the
files it cites. To see which of their shared modules the BC2 and BF4 code really calls, a shallow
copy of the three snapshots was searched in a scratch folder outside the project (nothing was
built or run). Several shared modules turned out to be unused by any game (sections 3.5 to 3.7
and 3.13), and a few sources and numbers were corrected.

**How to read the marks.** *(reported)* means the FrostbiteVR documents say so and we have not
tested it. *(unverified)* means it is my own inference for BF3 and has not been tried in the game.
Statements about code without a mark are things I read in their source or ours.

## What this means for us

- FrostbiteVR is free to reuse (MIT licence). Its only working game is Bad Company 2, on an older
  Frostbite engine. **Nothing exists for BF3.** In its October plan BF3 was the next game, but the
  author went to Battlefield 4 instead (a branch from 18 to 21 October).
- On the big pieces we are level or ahead. Our mod already draws both eyes natively through BF3's
  own split-screen test. That is the result FrostbiteVR built by hand for BC2. When they tried the
  same split-screen trick on BF4 it crashed, because BF4 only has room for one view. BF3's
  split-screen test still draws two views without crashing, which is why ours works.
- In BC2 these work in the headset *(reported)*: both eyes, aiming with the controller, walking
  around the room, gun grips, two-handed grips, holsters, and the shotgun pump with shell loading.
  These are still broken there *(reported)*: a crash after deaths/reloads, a mission-1 blocker
  (the C4 can't be thrown), poor headset performance and choppy automatic fire, cutscene cameras
  "all over the place", and reloads that won't start on several newly tested guns.
- What is worth borrowing are ideas and small pieces of game-independent maths:
  1. a **fake headset** for testing the mod without wearing the real one;
  2. **walking around the room and crouching for real** (their walking works in BC2; their
     shared "real crouch" code is used by no game, and BF4's own simpler version has run on a
     simulated headset only);
  3. **real arms** from the shoulders to the hands (this should fix our bare forearm stub);
  4. a **per-gun recipe** for how each gun loads and cycles, plus magazine insertion that snaps in;
  5. a **shader survey** to find exactly which setting moves the muzzle flash and casings.
- Cheap extras: the controller buzzes on each shot and when a magazine seats, the HUD sits about
  2 m away instead of at infinity, and the meaningless centre crosshair is hidden.
- None of their BC2 or BF4 memory addresses carry over to BF3.

---

## 1. The project in brief

### 1.1 Licence and what we may take

- **MIT**, "Copyright (c) 2026 BFVR contributors" ([LICENSE](https://github.com/justiceofrip/FrostbiteVR/blob/main/LICENSE)).
  The lineage is MIT too: [BFVR](https://github.com/JayBiggsGMG/BFVR-Battlefield-1942-VR-Mod) (Battlefield 1942 VR),
  then [BF2142VR](https://github.com/justiceofrip/BF2142VR), then FrostbiteVR
  ([THIRD_PARTY_NOTICES.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/THIRD_PARTY_NOTICES.md)).
- Bundled third-party code: MinHook 1.3.4 (BSD), OpenXR 1.1.61 headers and loader (Apache-2.0),
  JsonCpp inside the loader (MIT), and a Granny-curve decoder adapted from LSLib (MIT). The BF4
  branch's NVAPI shim takes its function IDs from NVIDIA's MIT NVAPI headers
  ([BF4-RESEARCH.md](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/docs/BF4-RESEARCH.md)).
- No GPL licence appears in FrostbiteVR, unlike Overlord: not in the notices, not in `licenses/`,
  and a text search of all three snapshots finds no GPL text. The only "GPL" in the repository is a
  download list for an LGPL build of FFmpeg that the BC2VR launcher fetches on first run; it is not
  FrostbiteVR code and nothing we would take. We may copy FrostbiteVR's own code if we keep the MIT
  notice. If we ever do, add `licenses/FrostbiteVR-MIT.txt` with their copyright line and credit
  them in the CHANGELOG. Ideas need no notice, but we credit them anyway.
- The separate [BC2VR](https://github.com/justiceofrip/BC2VR) repository (the installer and feedback
  tracker) has **no licence file**, so by default all rights are reserved. Don't copy from it. It
  holds only a README and issue templates; the installers are release downloads.

### 1.2 How it is built

- It is large: 2,278 files on `main`, including 337 files under `docs/` and 469 under `tests/`.
  The BC2 adapter alone has 413 files, two of them over 330 KB each (`NativeProbe.cpp`,
  `Bc2Gameplay.cpp`). There are 19 branches, 0 issues, 4 pull requests (docs, build and CI only)
  and 1 GitHub release
  ([checkpoint-436-r56](https://github.com/justiceofrip/FrostbiteVR/releases/tag/checkpoint-436-r56), 2026-10-28, pre-release).
- It appears to be written mostly by AI coding agents under written rules
  ([AGENTS.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/AGENTS.md)). That is my
  inference: the BF4 handoff says it was "written ... at the end of a claude.ai chat session",
  the newest branch adds a `CLAUDE.md`, and the task branches are numbered by chat. All commits are
  under the owner's account. Each checkpoint rewrites a single "current state" file and logs a
  great deal of evidence. The documents are long and heavily hedged. The code packs many
  statements onto each line, so its ideas port more easily than the code itself.
- **Architecture** ([ARCHITECTURE.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/ARCHITECTURE.md)):
  - an x86 DLL inside BC2 holds the native hooks (MinHook);
  - a **separate x64 OpenXR host process** talks to the headset;
  - eye images cross between the processes as shared D3D11 textures, and tracking and input go
    through shared memory with deadlines;
  - game-independent maths and policy live in `include/fvr/{interaction,math}` and
    `src/{interaction,math}` (their architecture note says this shared core has no Windows,
    DirectX or OpenXR dependency; the rest of `include/fvr` holds the D3D11, IPC and XR pieces),
    and everything BC2-specific lives in `src/games/bc2`.
  - A shared module existing is not the same as a game using it: several of the modules this note
    describes have no caller in any game (marked where they come up).
- Ours, for contrast: one 32-bit DLL inside `bf3.exe`, OpenXR in-process (SteamVR's 32-bit runtime),
  hooks mostly at the D3D11 level, one hooked game function (the view builder), hardware
  breakpoints on the game's camera writes, and direct writes to values found in memory (the look
  angles, health, ammo).

### 1.3 Its plans for Frostbite 2 / BF3

- The [porting roadmap](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/FROSTBITE-PORTING-ROADMAP.md)
  (2026-10-01) names BF3 the "proposed second adapter". It plans gates from 0 (environment) to 6
  (developer release) and advises "bring up one ordinary weapon and one verified reload family end
  to end". It states plainly: "No BF3, BF4, Hardline, BF1, BFV, 2042, or newer native binding is
  verified in this repository."
- In practice the author started BF4 on 2026-10-18 (by its handoff note; the branch's commits run
  from 19 to 21 October), not BF3. None of the three snapshots contains anything BF3-specific: a
  text search of all of them for "BF3", "Battlefield 3" and "Frostbite 2" finds only "a future
  BF3/BF4 port must ..." remarks and a BF4-branch driver probe noting that BF3 shipped AMD's HD3D
  stereo.
- The BF4 notes record that DICE added stereo 3D to Frostbite 2 for BF3 by rendering each frame
  twice (3D Vision / HD3D), citing an EA article
  ([BF4-RESEARCH.md](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/docs/BF4-RESEARCH.md)).
  That fits our finding that BF3's split-screen test and its own stereo branch both still build
  two views (memory note `bf3-per-eye-views`).

---

## 2. What works and what doesn't (state on 2026-10-30)

### 2.1 Bad Company 2, the working game *(all reported)*

| | What | Where it is stated |
| --- | --- | --- |
| Works, user in headset | Native two-eye picture (2026-09-27: "amazing! it's perfect except ... black dots"). The image is described as sharp in 0.1.17 | [RENDERING-REPAIR-20260927.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/RENDERING-REPAIR-20260927.md), [0.1.17 bug list](https://github.com/justiceofrip/FrostbiteVR/blob/checkpoint/436-r56-equipment-rendering/docs/archive/2026-10-29-bc2vr-0.1.17-buglist.md) |
| Works, user in headset | Controller aiming and horizontal roomscale "body follow" (2026-09-30) | [CONTROLS-20260930.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/CONTROLS-20260930.md) |
| Works, user in headset | Free left hand, selected gun grips, the launcher's flip-up sight | [FROSTBITE-PORTING-ROADMAP.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/FROSTBITE-PORTING-ROADMAP.md) |
| Works, user in headset | Holsters reliable, SPAS pump "felt great", pump audio "PERFECT" (2026-10-17) | [WEAPON-COVERAGE-CURRENT.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/WEAPON-COVERAGE-CURRENT.md) |
| Works, user in headset | Opening knife kills, progress to the scientist, manual magazine loading (external tester) | [PREALPHA-BACKLOG.md](https://github.com/justiceofrip/FrostbiteVR/blob/checkpoint/436-r56-equipment-rendering/docs/PREALPHA-BACKLOG.md) |
| Works, user in headset | Two-hand grips on several guns (shotgun, LMG), reliable holster swaps (0.1.18, 2026-10-30) | [0.1.18 feedback](https://github.com/justiceofrip/FrostbiteVR/blob/checkpoint/436-r56-equipment-rendering/docs/archive/2026-10-30-bc2vr-0.1.18-buglist.md) |
| Works in scripted game runs only (no human) | Nambu manual magazine and firing, five M95 bolt cycles, SPAS pump and shell sequences | [CURRENT.md (main)](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/CURRENT.md), [release notes](https://github.com/justiceofrip/FrostbiteVR/releases/tag/checkpoint-436-r56) |
| Broken | A crash after deaths/reloads, still unresolved (in main's checkpoint-426 state file; the BC2VR README still lists death/reload crashes with no accepted fix, and the 436 release notes still list death/checkpoint recovery as open) | [CURRENT.md (main)](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/CURRENT.md), [BC2VR README](https://github.com/justiceofrip/BC2VR/blob/main/README.md) |
| Broken | Mission 1: the C4 can't be thrown, which blocks progress | 0.1.17 bug list |
| Broken | Performance "horrid" in the headset; automatic fire feels bursty (also in 0.1.19) | 0.1.17 bug list, 0.1.18 feedback |
| Broken | Cutscene cameras "all over the place", the ammo counter flashing in cutscenes, a cutscene seen through a scope | 0.1.17 bug list |
| Broken | Building-destruction rendering, black dots / LOD artifacts, a floating ACOG dot | 0.1.17 bug list, PREALPHA-BACKLOG.md |
| Broken | Reloads won't start on newly tested guns (a shotgun, a probable PKM, the AN-94; still failing in 0.1.19); LMG bullets don't follow the barrel (also in 0.1.19); the LMG can't be stowed for empty hands; the XM8 launcher sight won't flip; the AN-94 underbarrel is dead | 0.1.18 feedback |
| Unfinished or unverified | Belt-fed LMG reload, spent-shell ejection, some magnified scopes, full-body IK, multiplayer, most vehicles | CURRENT.md, WEAPON-COVERAGE-CURRENT.md |

The public installer is BC2VR 0.1.15, and privately installed builds reach 0.1.20 *(reported)*.

### 2.2 Battlefield 4 branch *(all reported)*

Source: [CURRENT-BF4.md](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/docs/CURRENT-BF4.md)
and [BF4-STEREO.md](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/docs/BF4-STEREO.md).

- **Seen in a real headset:**
  - the flat "theater" screen with laser-pointer menus;
  - head tracking in three sessions with builds 0.4.13 to 0.4.15 (rotation was inverted and the
    gun oversized; both were fixed in 0.4.16).
  - Those fixes, and every build after them, have not been seen in a headset.
- **Simulated headset only** (their fake OpenXR runtime, section 3.17):
  - BF4 draws each eye itself: 86 fps in stereo against 115 mono at 1080p;
  - the gun is in the hand, with arms from the body (arm IK);
  - no centre crosshair, and the HUD at 2 m;
  - holster and magazine gestures, physical crouch, and controller aim through mouse counts.
- **Built but not yet seen running in the game:** hiding BF4's own body, which had swung into view
  with the gun (checked on saved captures and in their emulator only).
- **Dead ends or parked:**
  - BF4's split-screen stereo crashes, because its world renderer holds exactly one view;
  - an NVAPI "3D Vision" shim: the shim answered every query BF4 made, but BF4 never entered its
    stereo path, because stereo is off when its graphics device is created. Trying again would need
    a launch option the player sets, which has not been done, so this route is parked, not
    disproved;
  - 3D-from-depth and alternate-eye rendering (both retired).
- Not released: there are no `bf4-v*` tags.

### 2.3 BF3

Nothing has been built (section 1.3).

---

## 3. The techniques, one by one

Each item says what it does, how it works, what our mod does today, and a verdict: **as-is**,
**with changes**, or **does not apply**.

### 3.1 Two eyes from one game frame

- **BC2:** they create one extra native world view through BC2's own view factory (it stays
  registered while stereo runs and is rebuilt after a checkpoint restart), initialise its child
  views, and swap BC2's fixed 32-entry renderer work list for an owned 128-entry pool (two views
  had overflowed it and crashed). Both eyes then came out with the right eye's camera constants:
  both draws read the camera binding the last (right) view had left. An earlier guess, that
  shared per-frame memory was being overwritten, was ruled out. So they correct the per-eye camera
  constants during Prepare and Draw. One simulation update feeds both views
  ([CRASH-20260925.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/CRASH-20260925.md),
  [RENDERING-REPAIR-20260927.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/RENDERING-REPAIR-20260927.md),
  [BC2_NATIVE_DISCOVERY.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/BC2_NATIVE_DISCOVERY.md),
  [NativeWorkPool.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/games/bc2/NativeWorkPool.h)).
- **BF4:**
  - the split-screen route crashed, because BF4's world renderer allocates one view block and a
    one-entry array of per-view renderers;
  - what works on the simulated headset is calling the world renderer's *draw* twice per frame
    (render thread), moving the main view constants to each eye, and putting back a verified list
    of per-frame fields the first draw "used up". Which used-up fields must *not* be put back
    (thread stacks, job lists, a pool's free list) they learned the hard way, one crash at a time
    (CURRENT-BF4.md, sessions 105-111;
    [BF4-STEREO.md](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/docs/BF4-STEREO.md)
    for the one-view crash).
- **Ours:** `GameRenderSettings.SplitScreenTestViewCount = 2` plus the hooked view builder
  `0x01018E70`, which rewrites both views' cameras (`src/views.cpp`). It holds 72 fps at
  2 x 1600^2 (memory note `bf3-per-eye-views`).
- **Verdict: does not apply.** BF3 already has the two-view path that FrostbiteVR had to build by
  hand. Three lessons to keep:
  1. state left behind by the last view can give both eyes that view's constants. We already
     split the first-person uploads by order in `skin.cpp`, so watch for it in any new per-eye
     pass;
  2. BC2's frustum builder caps the vertical half-angle at about 57.3 degrees (about 114.6 degrees
     full). Whether BF3 has a similar cap is *(unverified)*: check before pushing the render FOV
     past about 110 degrees;
  3. BF3's own 3D Vision branch ("RootLeft/RootRight") would need an NVAPI shim, and BF4's shim
     never got the game into its stereo path (section 2.2), so it is a long shot. We don't need it.

### 3.2 Eye shape (FOV) and culling ("stereo culling")

- **Theirs:** each eye gets an off-centre projection built from the headset's FOV tangents
  ([StereoMath.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/math/StereoMath.h),
  `MakeLhProjectionFromFovTangents`). BC2 maps the tangents onto its vertical FOV, aspect and
  "crop" offsets. BC2's culling ignores the crop offsets, so they cull with one symmetric cone that
  encloses both eye frusta
  ([StereoCulling.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/math/StereoCulling.cpp)).
  It puts the 8 corners of each eye frustum into the centre camera's space, takes the widest x/z
  and y/z ratios, and adds a 0.01 % margin.
- **Ours:** one symmetric FOV for both eyes (`camera::WantedFov`), parallel eyes offset by half the
  IPD, and the cull camera turned and offset with each eye (`src/views.cpp`). OpenXR is told a
  symmetric `XrFovf` (`GameFov` in `src/xr_runtime.cpp`) and the compositor reprojects to the
  real lens.
- **Verdict: with changes, later.** If we ever render the Steam Frame's real per-eye FOV (sharper,
  fewer wasted pixels), BF3's cull camera will need exactly this enclosing symmetric cone. Whether
  BF3's camera block has off-centre parameters is *(unverified)*. If it doesn't, we could patch the
  projection matrices in the uploads, as `RescaleProjections` already does for the FOV.

### 3.3 The first-person gun and arms in each eye

- **BC2:** `RetargetFirstPersonProjection` keeps the weapon pass's own depth column and takes x, y
  and w from the eye
  ([ProjectionOverride.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/math/ProjectionOverride.h)).
- **BF4:** the first-person meshes have their own view constants, which they identified through
  shader reflection. When BF4 binds them, the mod binds its own constant buffer instead: the
  render's projection and a view moved by hand x grip^-1. The whole rig then sits at the controller,
  first-person muzzle flash included. To read BF4's mapped ring buffer they use SSE4.1 streaming
  loads, which cut the time for a 2.9 KB palette from 61.9 to 3.1 microseconds *(reported, AMD GPU)*
  (CURRENT-BF4.md, "Gun in the hand" and "Arms from the body").
- **Ours:**
  - `skin.cpp` `RescaleProjections` widens the 55 degree first-person projections in constant-buffer
    uploads (x and y only, depth kept);
  - the gun and gloves are placed per draw through their bone matrices;
  - the eyes are told apart by upload order and eye position.
  - Uploads are read and edited straight through the mapped pointer (`mapped->pData`, read at
    Unmap in `HandleUpload`).
- **Verdict:** the core idea is already done the same way. Two details apply with changes:
  1. **streaming loads**. If the NVIDIA driver also hands back write-combined memory for these
     dynamic buffers *(unverified)*, plain reads of it are very slow, and BF3 is CPU-bound. Our own
     `g_rescaleTicks` counter can measure part of this before we change anything: it times only
     the projection scan, not the copies of the glove and gun bone uploads that `HandleUpload`
     also reads from the same mapped memory;
  2. binding a constant buffer of our own for one draw, instead of editing the game's buffer in
     place. Their BF4 mod does this at the moment BF4 binds its first-person constants: the bind
     goes ahead with the mod's buffer, so BF4's own ring buffer is never edited.

### 3.4 Tracked rig: where the hands, gun and shoulders go

- **Theirs** ([TrackedRig.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/TrackedRig.cpp)):
  - controller grips are expressed relative to a reference head pose, then in the body frame;
  - each wrist's *orientation* offset is learned once per soldier, never its position (where the
    hands started must not become an offset);
  - the gun-in-hand attachment is learned from the game's own animation once it has settled (at
    least 600 ms after equipping and 150 ms stable), then the gun stays locked to the wrist through
    recoil;
  - shoulders and elbow directions are anchored in an upright body frame, not to the gun.
  - `PhysicalTorsoFrame` gets the head's heading by keeping only the y and w parts of its quaternion
    and renormalising. That stays well defined when looking straight up or down.
- **Their hard lesson** ([HAND-COUPLING-20260930.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/HAND-COUPLING-20260930.md)):
  turning the gun swung BC2's native shoulders about 38 cm and dragged the free hand 24 cm off its
  controller. Anchoring the shoulders to the body brought the hand's drift down to 0.2 mm
  *(reported, a scripted test in the game with no headset)*.
- **Ours:** `hands.cpp` puts the gun on the right controller's grip with the aim orientation, and the
  anchor is the game's own hold (`skin::GunGrip`). The left glove follows a lined-up offset (the
  View button). There are no shoulders or arms yet.
- **Verdict: with changes.** We need the body-anchored shoulders when we add arms (3.5). The
  heading trick drops straight into anything that needs a "body heading": pouches, holsters, and the
  cutscene body lock.

### 3.5 Arm IK

- **Main branch, game-independent:**
  - [ArmIk.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/ArmIk.cpp)
    solves a two-bone arm by the law of cosines with an elbow pole direction;
  - each segment gets the smallest rotation from its old direction to its new one, so the forearm's
    twist survives, and fingers and twist bones ride along with their segment;
  - an optional mode stretches both segments when the hand is out of reach (BC2 saw up to 1.46x
    *(reported)*);
  - [ArmPoleVectorMath.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/math/ArmPoleVectorMath.cpp)
    chooses where the elbow points from the hand's position (out, down and back) and blends toward
    a fixed safe direction when the hand is straight above or below the shoulder, or behind it. It
    cites Parger et al., which I take to be the 2018 VR upper-body IK paper (my identification). It
    is limited to 12 degrees of change per sample. It came over from BFVR (the Battlefield 1942
    mod has the same file), and **no BC2 or BF4 code calls it**: BC2's arms keep the game's own
    elbow directions, anchored to the body (3.4).
- **BF4 branch, the closest match to our setup**
  ([Bf4ArmIk.h](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/src/games/bf4/Bf4ArmIk.h)):
  it works on the GPU bone palettes that the first-person draws upload, as our `skin.cpp` does,
  and writes nothing into game memory. One difference: it binds its own corrected copy of each
  palette, where `skin.cpp` edits the game's upload in place.
  1. Each arm is found by its fixed joints: the shoulder, elbow and wrist points that two bones map
     to the same place in every pose.
  2. Every palette entry in every first-person draw is labelled by which joints it maps: upper arm
     (shoulder and elbow), forearm (elbow and wrist), clavicle (shoulder only), hand (wrist only), or
     finger (turns about an axis within 18 cm of the wrist).
  3. The solve runs once per pass. The shoulders sit on a body frame and follow 35 % of the hand's
     move, up to 8 cm. The elbow stays on the game's side.
  4. Each entry S becomes C x S, where C = headFromPass^-1 x D x headFromPass.
  - This copes with palettes that order bones differently per mesh and with stale copies of the arm
    bones. It costs about 0.2 ms per pass with no change in frame rate *(reported, simulated headset only)*.
  - It also hides BF4's own body mesh in the first-person pass (an all-zero palette). That part is
    built but not yet seen in the running game (saved captures and the emulator only).
- **Ours:** gloves at the controllers, while a bare forearm (draw 2016/13035) and the sleeve stay
  behind under the left glove (memory notes `bf3-vr-findings-2026-10-30`, `bf3-per-eye-views`).
- **Verdict: with changes. This is the most valuable code-level borrow for the picture.** We must
  measure BF3's arm joints from our own mesh dumps and list BF3's arm and sleeve draws
  (`tools/fpmap.ps1` already found 18 first-person draw kinds). Whether BF3's arm palettes hold
  clavicle, upper-arm and forearm bones the same way is *(unverified)*.

### 3.6 Weapon grip and support grip

- **Theirs:**
  - [WeaponGrip.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/WeaponGrip.cpp)
    keeps a held / not-held state: a grip press at a holster slot takes the gun, a press anywhere
    else lets it go, and picking a weapon by wheel or keyboard counts as held. It came over from
    BF2142VR (which has its own `WeaponGrip.cpp`), and no BC2 or BF4 code calls it.
  - [SupportGrip.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/SupportGrip.cpp)
    is two-hand aiming. It grabs when the squeeze reaches 0.7 after a neutral sample (0.35 or less),
    within 16 cm, and holds out to 28 cm. On the grab it stores the hand-to-hand direction in the
    right hand's frame. Each frame it turns the right grip and aim by the smallest rotation that
    points that direction at the left hand.
  - It lets go if the hands cross (dot product below -0.5), if they are under 10 cm or over 1.2 m
    apart, or if tracking is lost.
  - A pistol mode ("PrimaryHand") keeps the support hold without changing the aim.
  - BF4 uses 12 to 80 cm in front and within 35 degrees of the aim (60 to keep holding)
    ([TWO-HAND-SUPPORT-20261001.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/TWO-HAND-SUPPORT-20261001.md), CURRENT-BF4.md).
- **Ours:** `hands.cpp` `Support()` uses the same smallest-rotation idea. It catches within 25 cm and
  holds until the grip is released (the user's request), and the glove blends onto the game's own
  left-hand grip.
- **Verdict:** we already have this. Borrow only two things:
  1. the **pistol mode** for the M9 (support hand cupping the firing hand: hold without steering the
     aim);
  2. the **crossed-hands guard**.
  Do not take their 28 cm release: our user explicitly wants "locked no matter what until I let go".

### 3.7 Roomscale follow and real crouching (our roadmap step 2)

- **RoomscaleFollow**
  ([RoomscaleFollow.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/RoomscaleFollow.cpp)):
  - the head may lean freely up to 12 cm from the body;
  - beyond that the mod asks the game to walk toward the head at min(0.8, 2.4 x (distance - 0.12))
    m/s;
  - it then measures how far the soldier *actually* moved, collisions included, and removes only
    that from the camera offset (the "consumed" displacement);
  - jumps over 25 cm and gaps over 150 ms are ignored (teleports, snap turns, recentering), and it
    never fights the player's own stick.
  - Accepted in the headset on 2026-09-30 *(reported)*. It does not stop the head going through
    walls *(reported, CONTROLS-20260930.md)*.
- **Stance:**
  - The shared core's `PhysicalStance` crouches when the head drops more than 30 cm and stands
    again under 20 cm. Prone is over 88 cm (back under 72 cm). There is a 180 ms dwell
    ([ComfortControls.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/ComfortControls.cpp)).
    The standing height is set when recentering. **No BC2 or BF4 code calls it** (nor
    `StandingHeightReference`): it came over from BF2142VR's comfort code. BC2 players crouch by
    clicking the left stick (BC2VR README); nothing says BC2's physical crouch was ever tried.
  - BF4's `PhysicalCrouch` crouches at 35 cm and stands at 20 cm. Its standing height is the
    highest head height seen, sinking 1 cm/s while standing. BF4's crouch is a toggle, so the mod
    tracks what the game believes: its own presses, the player's crouch button, and BF4 standing
    the player up on sprint or jump
    ([Bf4PhysicalCrouch.h](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/src/games/bf4/Bf4PhysicalCrouch.h)).
  - `PhysicalCameraHeight` learns the eye height above the soldier's root while standing
    (0.15 to 1.1 m, stable for 300 ms) and then holds the view at that height above the root
    ([ComfortCamera.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/ComfortCamera.cpp)).
    That this is meant to stop the game's own camera bob and stance moves from moving the view is
    my reading; the code gives no reason. It is also from BF2142VR and also has no caller in BC2
    or BF4.
  - So the only physical crouch actually wired into a game is BF4's (above), tried on the
    simulated headset only.
- **Ours:** head rotation only, with the eyes offset by half the IPD each side (`views.cpp`,
  `camera.cpp` `TurnToHead`, as read in code). F9 recenters. Crouch is the right B button, sent as
  Ctrl (`input.cpp`).
- **Verdict: with changes. This is the top borrow for roadmap step 2.** BF3 needs:
  - the soldier's position each frame (we have the client soldier chain; the position offset is
    *(unverified)*);
  - a way to request movement (we already inject keys and sticks);
  - knowing whether the user's crouch key is hold or toggle *(unverified)*.
  Our "lean / duck / peek with a limit" is their lean radius. Wall clipping still needs an answer of
  our own.

### 3.8 Magazines: pulling out, pouches, inserting

- **Theirs:**
  - [DetachableMagazine.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/interaction/DetachableMagazine.h),
    [ReloadInsertion.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/interaction/ReloadInsertion.h),
    `AmmoSupply`, and BF4's body frame
    ([Holsters.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/src/interaction/Holsters.cpp)).
  - Grab the magazine within 9 cm and pull it along the well to remove it: 6 cm by default, 9 cm
    for the XM8.
  - Take a new one from a body pouch. In BF4 that is the front left of the belt, a 10 cm sphere; the
    body's heading follows the head only past 37 degrees, at 4 rad/s.
  - Near the well it **captures**. For the XM8 that means within 7 cm and 45 degrees, after which the
    magazine slides along a 10 cm rail and aligns over 120 ms. It needs 3.5 cm of real push after
    capture and seats when within 4 mm and held for 60 ms. It lets go beyond 15 cm or 75 degrees
    ([Bc2MagazineGeometryProfile.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/games/bc2/Bc2MagazineGeometryProfile.cpp)).
  - **Custody rules:** a partly used magazine that is pulled out keeps its own rounds and never
    counts as fresh reserve (DetachableMagazine.h); the state is kept until the action really
    finishes, never reset because a controller sample repeats, and it survives tracking or focus
    loss ([AGENTS.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/AGENTS.md), the first
    section of [ARCHITECTURE.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/ARCHITECTURE.md),
    DetachableMagazine.h; BF4's notes add menus). Whether a tactical reload keeps the round in the
    chamber is a per-gun setting in their mechanism data (3.9), not a fixed rule.
- **Ours:**
  - X ejects the magazine, which falls and vanishes on landing;
  - chambering is by charging handle or bolt-release slap;
  - the round count is written into the server's object (`reload.cpp`, `ammo.cpp`);
  - grab-from-pouch and insert are on the roadmap.
- **Verdict: with changes.** Take their geometry ideas and numbers as starting values, and their
  custody rules for our ammo bookkeeping. Skip their native "receipt" machinery: we write the
  server count directly. A pouch magazine could perhaps be drawn by issuing BF3's own magazine draw
  again with our own bone matrix *(unverified, our idea, not theirs)*.

### 3.9 Per-gun mechanism recipes (our roadmap step 3)

- **Theirs:**
  - [WeaponMechanism.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/interaction/WeaponMechanism.h)
    describes a gun by its feed (detachable magazine, tube, belt, clip, single round), the action
    after an empty feed and the action after each shot (automatic, pump, bolt, charging handle,
    slide), and whether a tactical reload keeps the chamber;
  - [WeaponCycle.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/interaction/WeaponCycle.h)
    describes a moving part by its rail axis, stroke, contact point and radius, tolerances, unlock
    rotation (a bolt's lift), dwell and top speed, with phases Unlock, Rear, Forward and Lock;
  - [WeaponProfile.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/interaction/WeaponProfile.h)
    gives each feature its own evidence state: unverified, game-verified or headset-accepted. It
    covers three features only: aim alignment, the support grip and the moved muzzle.
  - Their own header says no running code uses `WeaponMechanism.h` yet (in all three snapshots;
    the BF4 branch's reload classifier fills in descriptions, but nothing calls its plan
    selector). It is a plan, not a proven system.
  - `WeaponCycle.h` is used: BC2's SPAS pump and M95 bolt run on it. It covers pumps and bolts
    only, and their list of actions has no bolt-release button.
- **Ours:** `reload.cpp` hard-codes the M416. The memory note `weapons-own-mechanics` asks for
  per-gun profiles.
- **How our guns would map** *(my mapping, unverified against BF3's animations)*:

  | Gun | Feed | After an empty feed | Tactical reload |
  | --- | --- | --- | --- |
  | M416 | magazine | bolt release or charging handle | keeps chamber |
  | AK-74 | magazine, rocked in | charging handle (no hold-open) | keeps chamber |
  | M9 | magazine | slide or slide release | keeps chamber |
  | M320 | single round (open, insert, close) | nothing more: closing the breech chambers it | - |

- **Verdict: as-is, as a data model.** We write our own code around it.

### 3.10 Muzzle, shots and effects (our roadmap step 1)

- **BC2:** `PlaceFireAtMuzzle` moves the shot's origin to the drawn muzzle after the game's own
  offsets. That needs a native firing hook. It is enabled for SPAS and XM8, and only as a candidate
  for the Nambu, whose impact accuracy is not confirmed *(reported)*. The LMG is still off its
  barrel (0.1.18, 0.1.19).
- **BF4** (CURRENT-BF4.md):
  - the first-person flash moves with the first-person view because it is part of that pass;
  - casings are world draws and stay at the game's own gun. Their proposed fix, not built, is to
    give exactly those draws a moved main view, recognised by a stable shader identity (a bytecode
    hash);
  - a **shader census** runs D3DReflect on every `CreateVertexShader` and lists each shader's
    constant buffers and variable names. BF4's shaders keep their names
    ([Bf4ShaderCensus.h](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/src/games/bf4/Bf4ShaderCensus.h));
  - **converged aim**: the game fires from the eye while the barrel is drawn at the hand, so they
    aim from the eye at a point 20 m along the barrel. Shots then cross the barrel's line there
    (under 1 degree off either way).
- **Ours:**
  - `effects.cpp` has a draw census keyed by shader *pointer*, which changes every launch, plus
    `hidefx`/`movefx`;
  - moving the per-view constants (+48/+176/+240) did not move the flash (memory note
    `bf3-gun-effects`);
  - BF3's flash sprites are not marked as first-person draws (stencil reference 0, where the gun
    and arms use 32), so they are not part of the first-person pass we move.
- **Verdict:** their casing idea is what we already tried, so it does not apply as-is. What applies
  with changes:
  1. **the census tool and a stable identity.** If BF3's shaders keep their names
     *(unverified)*, we learn which buffer and variable each effect shader really reads. Even
     without names, disassembling the bytecode (D3DDisassemble) shows which constant registers it
     reads (my addition, untested on BF3);
  2. **converged aim**, where it is not yet known *(unverified)* whether BF3's own bullets start at
     the eye or at the gun.

### 3.11 HUD and crosshair

- **BF4:**
  - the HUD shows in both eyes at about 2 m: one eye's copy is shifted by the disparity of a point
    2 m ahead (13 px in their setup);
  - the centre crosshair is hidden by restoring a plus-shaped patch at the centre from a copy taken
    just before the HUD draws. The patch is a 10 % square plus 4 % bands reaching 15 %. Otherwise
    BF4's crosshair sits where the head looks, not where the gun aims (CURRENT-BF4.md).
- **Ours:** `overlay.cpp` puts the game's HUD into each eye at the same spot, which places it at
  infinity. The crosshair is still listed as "set aside" (memory note `vr-roadmap`).
- **Verdict: with changes. Small and cheap.** Whether BF3's crosshair stays inside such a patch
  under fire spread is *(unverified)*.

### 3.12 Controller buzz (haptics)

- **Theirs**
  ([Feedback.h](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/include/fvr/interaction/Feedback.h)):
  - recoil: amplitude 0.75 for 25 ms, at most 20 a second (BF4 branch only; "not felt in a
    headset" by their notes);
  - a shell or magazine captured: 0.55 for 35 ms;
  - seated: 0.9 for 75 ms;
  - nothing while focus or tracking is lost.
  - BF4 also sends a light pulse when an empty hand reaches a holster it could use
    (CURRENT-BF4.md, holsters; not in Feedback.h).
  - BF4 detects shots from the hand bone's kick.
- **Ours:** none (no `xrApplyHapticFeedback` call in `src/`).
- **Verdict: with changes. Cheap.** We can detect shots from the round count (`ammo.cpp`) and
  touches from the gun-shape contacts (`hands.cpp`).

### 3.13 Recoil

- **Theirs:** BFVR-heritage maths
  ([WeaponPoseMath.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/math/WeaponPoseMath.h))
  adds the game's recoil kicks as a tilt of the drawn gun about the grip, so the player fights the
  kick with the controller. `ComfortYaw` (from BF2142VR) keeps that recoil out of the view's
  heading. Neither the recoil functions nor `ComfortYaw` is called by any BC2 or BF4 code, so
  this is untested in either game (their architecture rules leave the game's own recoil in the
  game's simulation).
- **Ours:** `aim.cpp` writes yaw and pitch outright every frame, and its own comment notes that
  recoil moves the pitch. So our writes may be cancelling BF3's kick *(unverified)*.
- **Verdict: with changes.** First test whether recoil is felt at all now.

### 3.14 Recentering, turning, comfort

- **Theirs:**
  - [RecenterPolicy.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/interaction/RecenterPolicy.h)
    recenters once after 750 ms or more of lost tracking or focus, never on a single glitch;
  - BF4 recenters on both stick clicks held for 1 s, keeping the clicks from the game while both
    are held (a lone click still reaches it after 80 ms);
  - snap turn is 30 degrees by default;
  - a comfort vignette darkens the edges while the stick moves the player.
- **Ours:** F9, the View-button line-up, and smooth turning through the look yaw.
- **Verdict: small optional borrows.** A recenter on the controllers suits standing play better
  than a keyboard key.

### 3.15 Menus, pause and death screens

- **Theirs:** BC2 has a laser pointer on its native menus. BF4 switches to the flat screen when the
  game shows its mouse cursor for 4 frames, and back when the cursor is hidden.
- **Ours:** theater mode, with the stick moving the mouse.
- **Verdict: with changes.** It matters for alpha (death, checkpoint and pause screens). Whether BF3
  shows a system cursor in its menus is *(unverified)*.

### 3.16 A separate headset process

- **Theirs:** an x64 host process. Their architecture note gives the reason: the BF2142 design
  "used an x64 OpenXR presenter to avoid 32-bit runtime limitations". It reuses the last complete
  pair of images, with their original poses, for up to 250 ms. BF4 adds three request channels
  for a higher pair rate.
- **Verdict: does not apply now.** Our in-process runtime holds 72 fps since the
  `GetPerformanceInfo` fix (memory note `bf3-per-eye-views`).

### 3.17 Testing without a headset (our roadmap step 1)

- **Theirs:** the BF4 branch's
  [SimOpenXrRuntime.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/tools/simxr/SimOpenXrRuntime.cpp)
  (873 lines) is an OpenXR runtime that pretends to be a headset:
  - 90 Hz, two asymmetric eyes, a 64 mm IPD;
  - head and hand poses, triggers and buttons read from a text file;
  - the submitted eyes written out as BMP files;
  - **`flip`/`sig`**, which alternates the head between two poses every frame and checks that each
    submitted image really shows the pose it was labelled with.
  - [GestureSweep.ps1](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/tools/simxr/GestureSweep.ps1)
    drives every gesture in a running level, and WARP-based emulated tests cover the rest.
  - How it is loaded: it is an x64 DLL that their XR host loads directly, in place of the OpenXR
    loader, through a developer `xr_loader` setting; it is never registered as a system runtime.
    It exports only `xrGetInstanceProcAddr`, offers only the D3D11 extension, ignores binding
    suggestions, and answers inputs by action name ("trigger", "squeeze", "primary" and so on).
- **Ours:**
  - `tools/selfcheck.ps1` (committed) only checks that the log shows each part started;
  - `weapon.txt` "standin" gives a stand-in right hand;
  - SteamVR asks for no frames while the headset sleeps (comment in `xr_runtime.cpp`).
  - Work in progress, not committed (seen in the working tree during this check): a fuller
    `tools/selftest.ps1` with never-break checks taken from the game itself, and a "headless" mode
    in `xr_runtime.cpp` that keeps each frame's work going while the headset sleeps, with the
    stand-in hand on.
- **Verdict: with changes.** Build it for 32-bit and map our action names and button layout onto
  its inputs. Selecting it is not as simple as setting `XR_RUNTIME_JSON`: our DLL links the
  standard OpenXR loader statically, and that loader expects a runtime manifest and the loader
  negotiation entry point, which this DLL lacks *(my reading of the OpenXR loader rules,
  unverified here)*. Either add that entry point plus a manifest, or let our code load the DLL
  directly as their host does. Note that `FixRuntimeOverride` in `xr_runtime.cpp` only rewrites
  `XR_RUNTIME_JSON` when its path contains "win64", so a 32-bit test manifest set there would be
  left alone.

### 3.18 Ways of working worth copying

- Give each feature its own status (unverified / game-checked / headset-accepted) instead of "it
  builds, so it works".
- Never reset held equipment state just because a controller sample repeats.
- Restore exactly what you wrote and nothing else.

These come from their AGENTS.md and support our never-break rule (memory note `never-break`).

---

## 4. What does not carry over

- Every BC2 address, RVA, vtable and object layout:
  - the native view factory and its 32-entry work pool;
  - the HaloAimer angle setters;
  - the padded right-handed camera rows (`FrostbiteCamera`);
  - the menu (APT) bindings;
  - native ammunition receipts and client-prediction handling;
  - Granny animation decoding and the body-asset extraction pipeline;
  - the launcher, Steam and EA specifics.
- Every BF4 offset: it is a 64-bit, Frostbite 3 build with a different renderer.
- The NVAPI 3D Vision shim, BF4's double world draw with field re-arming, 3D-from-depth and
  alternate eyes. BF3's split-screen views already do this job better.
- Aiming through injected mouse counts with learned gain (BF4). We write the real look angles,
  which is exact (memory note `bf3-look-angles`).
- Their controller bindings: Touch, Index and the simple profile only. We already have the Steam
  Frame profile (`input.cpp`).
- Cutscenes: BC2's cutscene cameras are an open bug there, so there is nothing to borrow. Our own
  plan (body locked to the game camera, head free, level horizon) stands (memory note
  `cutscenes-and-qte`).

---

## 5. The five most valuable things to borrow (ranked)

1. **A fake headset for automatic testing**
   ([SimOpenXrRuntime.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/tools/simxr/SimOpenXrRuntime.cpp),
   `flip`/`sig` pose check, GestureSweep).
   - *Why:* regressions are what frustrates the user most, and the self-test pipeline is roadmap
     step 1. It is a standalone MIT tool that lets every never-break item be checked with nobody in
     the headset: aim follows the gun, the view stays still, stick-turn direction, both eyes match,
     frame rate, reload gestures.
   - *Take:* the design (a runtime driven by a text file, eye snapshots, the pose-label check),
     re-implemented or adapted with their MIT notice.
   - *Risk:* it must be 32-bit, answer our action set and be loadable by our statically linked
     OpenXR loader (3.17); real SteamVR timing is not tested by it.
2. **Walking around the room and real crouching**
   ([RoomscaleFollow.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/RoomscaleFollow.cpp),
   `PhysicalStance` / `StandingHeightReference` in
   [ComfortControls.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/ComfortControls.cpp),
   [Bf4PhysicalCrouch.h](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/src/games/bf4/Bf4PhysicalCrouch.h),
   `PhysicalCameraHeight`).
   - *Why:* this is roadmap step 2 and "real crouch" is alpha criterion 1. The pieces are small
     and pure. The walking part is headset-accepted in BC2 *(reported)*, and its "consume only the
     movement that really happened" rule keeps the body from walking through walls. The crouch and
     camera-height pieces are not: the shared ones are used by no game, and BF4's own crouch has
     run on the simulated headset only.
   - *Take:* the 12 cm lean radius, the speed law, consumed-displacement accounting, crouch at 30
     to 35 cm and stand at 20 cm with a dwell, and a self-calibrating standing height.
   - *Risk:* BF3's soldier position and crouch key behaviour are *(unverified)*; the head can still
     lean through walls; the crouch thresholds are untried in a real headset in any of their games.
3. **Real arms from the shoulders to the hands**
   ([Bf4ArmIk.h](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/src/games/bf4/Bf4ArmIk.h)
   with [ArmIk.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/ArmIk.cpp)
   and [ArmPoleVectorMath.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/math/ArmPoleVectorMath.cpp)).
   - *Why:* it should fix the bare forearm stub and sleeve *(unverified for BF3)*, and it works at
     the same layer as our `skin.cpp` (rewriting the uploaded bone palettes, no game memory). The reaching hand on the
     charging handle, the pouch and the handguard all need arms that follow.
   - *Take:* finding the joints from bind-space fixed points, labelling bones by the joints they
     map, solving once per pass with body-anchored shoulders, then applying the correction to every
     palette.
   - *Risk:* BF3's arm rig and draws must be measured *(unverified)*, and BF4 has it on a simulated
     headset only. `ArmPoleVectorMath.cpp` is not used by BC2 or BF4 (3.5), so its elbow choice is
     untested in their games.
4. **Per-gun mechanism recipes and magazine insertion**
   ([WeaponMechanism.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/interaction/WeaponMechanism.h),
   [WeaponCycle.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/interaction/WeaponCycle.h),
   [ReloadInsertion.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/interaction/ReloadInsertion.h),
   [DetachableMagazine.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/interaction/DetachableMagazine.h),
   the BF4 body frame in [Holsters.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/src/interaction/Holsters.cpp)).
   - *Why:* roadmap step 3 says every gun gets its own mechanics, never an M416 copy. Their split
     into a shared mechanism family plus small per-gun data is exactly that, and the insertion numbers
     (capture 7 cm / 45 degrees, 10 cm rail, 4 mm seat, 60 ms dwell) are good starting values for
     grab-from-pouch.
   - *Take:* the data model and the custody rules (a partial magazine keeps its rounds; whether a
     tactical reload keeps the chamber is set per gun).
   - *Risk:* small, since we write our own code around it, but their mechanism model has no
     running user yet (3.9), so it is a design to learn from, not proven code.
5. **A shader survey with stable shader identities**
   ([Bf4ShaderCensus.h](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/src/games/bf4/Bf4ShaderCensus.h)).
   - *Why:* the muzzle flash and casings at the chest are roadmap step 1 and stuck after several
     ruled-out routes. Knowing which constant buffer and variable each effect shader reads (names
     through D3DReflect, or registers through disassembly) replaces guessing. A bytecode hash keeps
     a found shader recognisable across launches; today we key by pointer.
   - *Risk:* BF3 may have stripped shader names *(unverified)*; disassembly still works.

**Honourable mentions, all cheap:**

- haptics (3.12);
- the HUD at about 2 m plus the crosshair patch (3.11);
- streaming loads for reading mapped memory, measured first (3.3);
- converged aim at 20 m (3.10);
- pistol support mode and the crossed-hands guard (3.6);
- a controller recenter chord (3.14);
- switching to the flat screen on game menus (3.15);
- recoil as a gun tilt about the grip (3.13; inherited maths that no game of theirs uses yet).

---

## 6. Claims I could not verify

- Every FrostbiteVR status claim (what works in BC2 or BF4, frame rates, costs, user quotes) comes
  from their documents. None was reproduced.
- For BF3:
  - a frustum-angle cap like BC2's;
  - off-centre fields in BF3's camera block;
  - write-combined memory behind `mapped->pData` on our NVIDIA driver;
  - the soldier position offset and whether the crouch key is hold or toggle;
  - arm palettes laid out like BF4's;
  - whether BF3's shaders keep their reflection names;
  - where BF3's bullets start (eye or gun);
  - whether our look-angle writes cancel BF3's recoil;
  - whether BF3 shows a system cursor in its menus;
  - whether a second magazine draw can be issued;
  - whether BF3's crosshair stays inside a centre patch.
- My mapping of our guns to mechanism recipes (3.9).
- That their "Parger" reference is the 2018 VR upper-body IK paper.
- That the standard OpenXR loader we link needs a negotiation entry point and a manifest before it
  can load their fake runtime (3.17).
- What `PhysicalCameraHeight` is meant to fix (3.7); the code gives no reason.
- That the project is written mostly by AI agents (1.2): an inference from its files and branch
  names.
- "No BC2 or BF4 code calls it" (3.5 to 3.7, 3.13) comes from a text search of the three
  snapshots for each module's name. A call made in some indirect way would not show up, so read
  it as "no caller found".
- Our own code was read, not run, for this note. In particular "no head translation yet" is from
  `views.cpp`, `camera.cpp` and `xr_runtime.cpp` as they stood on 2026-10-30 (committed code at
  `1ef5910`; the working tree also held the uncommitted self-test work noted in 3.17).

## 7. Sources

- Repository and history: [FrostbiteVR](https://github.com/justiceofrip/FrostbiteVR),
  [branches](https://github.com/justiceofrip/FrostbiteVR/branches),
  [release checkpoint-436-r56](https://github.com/justiceofrip/FrostbiteVR/releases/tag/checkpoint-436-r56).
- Main-branch documents:
  [README](https://github.com/justiceofrip/FrostbiteVR/blob/main/README.md),
  [CURRENT](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/CURRENT.md),
  [ARCHITECTURE](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/ARCHITECTURE.md),
  [FROSTBITE-PORTING-ROADMAP](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/FROSTBITE-PORTING-ROADMAP.md),
  [BC2_NATIVE_DISCOVERY](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/BC2_NATIVE_DISCOVERY.md),
  [NATIVE_TRACE](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/NATIVE_TRACE.md),
  [CRASH-20260925](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/CRASH-20260925.md),
  [RENDERING-REPAIR-20260927](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/RENDERING-REPAIR-20260927.md),
  [CONTROLS-20260930](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/CONTROLS-20260930.md),
  [HAND-COUPLING-20260930](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/HAND-COUPLING-20260930.md),
  [TWO-HAND-SUPPORT-20261001](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/TWO-HAND-SUPPORT-20261001.md),
  [WEAPON-COVERAGE-CURRENT](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/WEAPON-COVERAGE-CURRENT.md).
- BF4-branch documents:
  [HANDOFF-BF4](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/HANDOFF-BF4.md),
  [CURRENT-BF4](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/docs/CURRENT-BF4.md),
  [BF4-STEREO](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/docs/BF4-STEREO.md),
  [BF4-RESEARCH](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/docs/BF4-RESEARCH.md),
  [BF4-ROADMAP](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/docs/BF4-ROADMAP.md).
- Newest BC2-branch documents:
  [CURRENT](https://github.com/justiceofrip/FrostbiteVR/blob/checkpoint/436-r56-equipment-rendering/docs/CURRENT.md),
  [PREALPHA-BACKLOG](https://github.com/justiceofrip/FrostbiteVR/blob/checkpoint/436-r56-equipment-rendering/docs/PREALPHA-BACKLOG.md),
  [0.1.17 bug list](https://github.com/justiceofrip/FrostbiteVR/blob/checkpoint/436-r56-equipment-rendering/docs/archive/2026-10-29-bc2vr-0.1.17-buglist.md),
  [0.1.18 feedback](https://github.com/justiceofrip/FrostbiteVR/blob/checkpoint/436-r56-equipment-rendering/docs/archive/2026-10-30-bc2vr-0.1.18-buglist.md).
- Source files: linked in each section above.
- Related repositories, for the licence and lineage checks:
  [BC2VR README](https://github.com/justiceofrip/BC2VR/blob/main/README.md) (no licence file),
  [BF2142VR](https://github.com/justiceofrip/BF2142VR) (MIT; source of `WeaponGrip`, `RecenterPolicy`
  and the comfort code) and
  [BFVR](https://github.com/JayBiggsGMG/BFVR-Battlefield-1942-VR-Mod) (MIT; source of
  `WeaponPoseMath` and `ArmPoleVectorMath`).
- Ours: `src/views.cpp`, `src/camera.cpp`, `src/hands.cpp`, `src/skin.cpp`, `src/aim.cpp`,
  `src/reload.cpp`, `src/ammo.cpp`, `src/effects.cpp`, `src/overlay.cpp`, `src/xr_runtime.cpp`,
  `src/input.cpp`, `tools/selfcheck.ps1`, `tools/fpmap.ps1`, `CMakeLists.txt` (the OpenXR loader
  link), and the memory notes in `C:\Users\richy\.claude\projects\E--BF3-CLAUDE-VR-PROJECT\memory\`.
