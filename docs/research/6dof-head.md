# 6DoF head for standing play: research and design

*Research note, 2026-10-30. Nothing here is built yet. I read our code (`src/`), the project memory
notes, [FrostbiteVR](https://github.com/justiceofrip/FrostbiteVR) (MIT; `main` at `b6b8975`, the
[`bf4/vr-adapter`](https://github.com/justiceofrip/FrostbiteVR/tree/bf4/vr-adapter) branch at
`428be84` and the newest BC2 branch `checkpoint/436-r56-equipment-rendering`),
[BC2VR](https://github.com/justiceofrip/BC2VR) (the installer and release notes for FrostbiteVR's
Bad Company 2 mod) and [Overlord](https://github.com/Aeka0/Overlord) (GPL-3.0: **ideas only, in our
own words, no code**). Nothing was run. Every claim I could not check is marked **(unverified)**.
Related notes in this folder: [frostbitevr.md](frostbitevr.md), [bc2vr.md](bc2vr.md),
[overlord-lessons.md](overlord-lessons.md).*

---

## What this means for us (plain English)

- **What "6DoF head" means.** Today the view turns with your head, but moving your head (leaning,
  ducking, a small step) does nothing: the world moves along with you. With 6DoF the game's camera
  moves when your head moves, so you can lean around a corner, duck behind a wall or lean in to look
  closer. It is the biggest missing piece of the alpha ("the head's position moves the view").
- **The camera part is small and sits in one place.** The mod already builds both eyes' cameras
  itself (`views.cpp`). Moving them by your head's position is a few lines there. The gun and the
  left glove are already placed relative to your head, so they stay in your hands when the camera
  moves.
- **Walls are the real problem.** The game doesn't know your head moved, so nothing stops your head
  from going into a wall and seeing through it. Plan, cheapest first:
  1. keep the camera within a short distance of the soldier (about 20 to 25 cm sideways, to be
     measured), which is inside the soldier's own collision body that the game already keeps out of
     walls;
  2. darken the view gently when the camera gets very close to a surface, or when you lean past the
     limit (research found fading less sickening than pushing the view back);
  3. later, ask the game's own physics whether a wall is in the way (that function still has to be
     found in bf3.exe).
- **Crouching for real.** Squat about 30 cm and the mod presses the game's crouch; stand back up past
  20 cm and it stands (the gap stops it flickering). F9 also records your standing height. The view
  always follows your real head: the game's own crouch drop is cancelled out, so the view never sinks
  twice or jumps. Prone stays on a button at first.
- **Walking in your room.** If your head stays more than about 15 cm from where the soldier is, the
  mod walks the soldier there slowly with the stick, and only counts the distance the soldier really
  moved, so walls stay walls. This is FrostbiteVR's method, which a BC2 tester accepted in the
  headset. Your own stick always wins.
- **Cutscenes and the car ride.** The camera follows the game's camera, the horizon stays level, your
  head looks around freely and can move a little (about 10 to 15 cm), and the mod never crouches or
  walks for you then.
- **What the others teach us.** FrostbiteVR has tested room walking and crouch thresholds, but does
  nothing about walls, and its BF4 version lets the view drop twice when crouching (game drop plus
  real drop). BC2VR today crouches with a stick click. Overlord has a good set of rules (bounded head
  movement around the game body, never teleport the soldier, crouch only after the game confirms).
- **Most of it can be checked without you in the headset**: a test line fakes a head position, and
  the log shows where the camera went, whether the crouch key went in and how far the soldier walked.
  What needs you: how it feels (the lean limit, crouch depth, the fade, the walking speed).

---

## 1. Where we start from (our code today)

| What | Where | Fact that matters for 6DoF |
| --- | --- | --- |
| Both eyes' cameras | `src/views.cpp` `MakeEyes` (lines 56-91) | Called from the hooked view builder (0x01018E70). Copies view 1's main, cull and first-person camera blocks to view 2, turns main and cull to the head (`camera::TurnToHead`), shifts all three by ±IPD/2 along the turned camera's right row, sets the FOV. **This is where 6DoF goes.** |
| Head turn maths | `src/camera.cpp` `ViewTurnLocal` (205-227), `TurnLocal` (230-254), `TurnToHead` (631-657) | The game camera is body yaw × hand yaw × game pitch (the look angles follow the right controller). The turn takes the hand's yaw and the game's pitch out and puts the head in. OpenXR axes map x → camera row 0 (right), y → row 1 (up), z → row 2 (back). |
| The weapon camera | `src/camera.cpp` breakpoint handler (317-338) | Turned opposite to the head so the game's gun stays with the hand; never gets an eye shift. 6DoF must not move it either. |
| Head pose and recenter | `src/xr_runtime.cpp` `BuildStereo` (872-1096), `Recenter` (668-687), F9 (543) | Eye poses come from `xrLocateViews` in the recentred space `g_space`. F9 puts that space's origin **at the head** (height included) facing the head's yaw, so right after F9 the eye centre is at (0, 0, 0). The eye centre is computed (938-946) and given to `hands::Update`. Nothing feeds the head's position into the cameras. |
| Poses handed to SteamVR | `src/xr_runtime.cpp` `renderPose` (912-919), `BuildProjection` (857-870) | They already carry the head's real position. Today the image is drawn without it; that mismatch is harmless with rotation-only reprojection (as far as I know, **unverified** for the Steam Frame). With 6DoF the two agree. Only the orientation-valid bit is checked (881), not position-valid or position-tracked. |
| Hands and gun | `src/hands.cpp` `Update` (638-) and `skin.cpp` | Worked out relative to the head (eye centre, left-eye orientation), then placed per first-person draw with that draw's own view-projection, whose eye is the weapon camera's. Where the world camera sits does not matter to them. |
| Crouch, jump, walking | `src/input.cpp` (161, 177-178, 256, 694-711), `src/pad.h` | Right B holds Left Ctrl (scan 0x1D) while held; A = jump; left stick click = sprint. The left stick walks where the head faces through a virtual Xbox left stick (`pad::SetMove`, any direction and speed). |
| Scripted moments | `src/aim.cpp` `Targets` (239-272) | The look angles are dropped after 1 s of not matching the weapon camera (cutscenes, vehicles, loads, per the memory note bf3-look-angles.md). That is our "scripted" signal. |
| Depth | `src/depth3d.cpp`, `camera::ProjectionDepth` | BF3's depth buffer is found by its clears; distance = 0.06 / (1.00001 − depth), so the **near plane is 6 cm**. First-person draws carry stencil 32. Whether the depth finder picks the per-eye (half-width) depth buffer in per-eye mode is **unverified**. |

So today we have rotation (3DoF) plus the eye distance. The position of the head is known every frame
but thrown away.

---

## 2. Moving the render camera by the head's position

### 2.1 The rule

For each eye, every frame:

> eye position = game eye position + body frame applied to (head − neutral − walked) + the head-turned
> half eye distance (already there)

- **game eye position**: the translation of the main camera BF3 built this frame (the soldier's eye).
- **head**: the eye centre in the recentred space (metres; x right, y up, z back).
- **neutral**: where the eye centre was at F9, which is (0, 0, 0) by construction (section 1).
- **walked**: how far room walking has really moved the soldier (section 7), in the same space.
- **body frame**: the soldier's level, yaw-only heading in the world.

### 2.2 Why a body frame, and how to get it

The offset must be turned by the **body's** heading, not the head's. Using the head-turned camera's
axes would swing the offset when you turn your head (looking left would carry you left).

The body frame is exactly what `TurnToHead` produces with "no head rotation": it takes the hand's yaw
and the game's pitch out of the game camera, and putting in an identity head leaves the body's yaw.
A new `camera::BodyFrame()` can run the same `ViewTurnLocal` maths with the head quaternion set to
(0, 0, 0, 1). Three cautions:

- `MakeEyes` turns `eye[0]`'s main camera in place, and the `game` pointer (line 63) points at it:
  copy the game camera **before** the eyes are turned.
- `ViewTurnLocal` removes the game's pitch but not a roll. The normal game camera has no roll (the
  camera.cpp comments rely on that), but a scripted or vehicle camera may roll **(unverified)**. Make
  the frame level by rebuilding it from its yaw alone (BF3's look-yaw formula, atan2 of the back
  row's x and −z, memory note bf3-look-angles.md), and self-test that the rebuilt frame equals the
  unflattened one in normal play.
- Without a tracked controller (head aiming) there is no view turn; the body is then the game camera
  with the head's yaw and pitch taken out.

The axis mapping is the one `TurnLocal` already uses for rotations: OpenXR x → row 0, y → world up,
z → row 2. FrostbiteVR's BF4 adapter found the same for Frostbite 3: the camera rows are right, up and
the axis pointing behind the camera, the same as OpenXR's, so the headset pose goes in without
mirroring ([Bf4AdapterCore.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/src/games/bf4/Bf4AdapterCore.cpp),
`XrPoseToBf4Camera`). The sign is still proven by self-test (section 11, test 1.1), because yaw signs
have bitten us before (memory note bf3-look-angles.md).

### 2.3 Where in the code

In `MakeEyes`, after `TurnToHead` and the eye shift, add the world offset to the main, cull and
first-person blocks of both eyes (the cull camera moves too, so what BF3 decides to draw matches
what each eye sees, as with the head turn today). A sketch, in our own code:

```cpp
// MakeEyes: body is a copy of the game's camera taken before the eyes were turned
camera::BodyFrame(body);                 // hand yaw and game pitch out: level, yaw only
const Offset o = LoadHeadOffset();       // metres, recentred space: x right, y up, z back
for (float* m : {main, cull, firstPerson})
    for (int i = 0; i < 3; ++i)
        m[12 + i] += body[i] * o.x + body[4 + i] * o.y + body[8 + i] * o.z;
```

Never move the weapon camera: the game places its own gun from it and aims with it (camera.cpp
comments, look angles in memory note bf3-look-angles.md). Moving it was tried only as research for
the muzzle flash (weapon.txt `wcam`, memory note bf3-gun-effects.md); 6DoF leaves it as the game
builds it.

### 2.4 One pose for the picture and for SteamVR

- Publish the head's position **together with** its rotation, in one record (extend
  `camera::ViewTurn`, which is already published through a double buffer), so a frame never mixes a
  new rotation with an old position.
- Keep the rendered (clamped, section 3) eye positions in the pose history and submit those in
  `renderPose`, not the raw head positions. Then SteamVR is told exactly what was drawn, including at
  the limit. The existing pose lag (`[stereo] pose_lag`, F11) applies to both alike.
- Require `XR_VIEW_STATE_POSITION_VALID_BIT` and `XR_VIEW_STATE_POSITION_TRACKED_BIT` for the
  position. When tracking of the position is lost, hold the last offset; if it stays lost, ease it
  back to zero over about a second; never jump. FrostbiteVR learned the same: it tells a truly
  tracked head pose apart from one that is only marked valid but stale, and recenters once after
  750 ms of sustained loss
  ([HEADSET-CALIBRATION-20261001.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/HEADSET-CALIBRATION-20261001.md),
  [RecenterPolicy.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/interaction/RecenterPolicy.h)).

### 2.5 Eye centre for the picture, neck for decisions

- Render from the true eye centre. Turning your head moves your eyes (they sit about 8 cm in front of
  the neck), and drawing that motion gives real parallax.
- Use a **neck point** for decisions (crouch, room walking, limits on body movement): about 8 cm
  behind and 7.5 cm below the eye centre, turning with the head. Those are the old Oculus SDK's
  default neck-to-eye distances, 0.0805 m and 0.075 m
  ([ovr-sys bindings](https://docs.rs/ovr-sys/0.3.0/src/ovr_sys/lib.rs.html); not checked for the
  Steam Frame). Without it, looking at the floor lowers the eyes by several centimetres and turning
  the head moves them sideways, which could be read as a crouch or a step.

### 2.6 What the others do

- **FrostbiteVR, BC2 adapter.** The view base is the soldier's collision position (horizontal), the
  game's own eye height (vertical) and the body yaw, minus the distance room walking has consumed;
  the head pose, relative to an upright reference taken at recenter, is composed on top
  ([Bc2TrackedBodyBase.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/games/bc2/Bc2TrackedBodyBase.h),
  [Bc2TrackedViews.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/games/bc2/Bc2TrackedViews.h),
  `ComposeRuntimeHeadWithLhCamera` in
  [StereoMath.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/math/StereoMath.h)).
  Its cull view is one wider frustum around both eyes
  ([StereoCulling.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/math/StereoCulling.cpp));
  we move each eye's cull camera instead, which is simpler and fine. There is **no limit** on how
  far the camera follows the head (only room walking gives up past 3 m, as bad data), and their docs
  say plainly that it doesn't stop the head clipping through walls
  ([CONTROLS-20260930.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/CONTROLS-20260930.md)).
  The recenter reference is made upright (yaw only), and a recenter while looking straight down
  keeps the old yaw
  ([ControllerInput.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/ControllerInput.cpp),
  `UprightReference`, `ExplicitRecenterReference`). Our `Recenter` has no such guard: worth adding.
- **FrostbiteVR, BF4 branch.** The head pose is composed one to one onto the game camera with a
  yaw-only parent ([Bf4ViewOverride.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/src/games/bf4/Bf4ViewOverride.cpp),
  `HeadTrackedView`). Listed there as a 6DoF head, seen in a headset only in an earlier build
  ([CURRENT-BF4.md](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/docs/CURRENT-BF4.md)).
  I found no position limit in the files I read **(unverified for the whole branch)**.
- **Overlord.** Head rotation and head movement are separate rules chosen per scene: movement can be
  fixed, scaled down with a small spherical limit, or fully tracked; normal play is fully tracked
  ([vr-camera-architecture.md](https://github.com/Aeka0/Overlord/blob/main/docs/vr-camera-architecture.md)).
  Its design rules keep the head inside a bounded space around the game body
  ([vr-gameplay-interaction-architecture.md](https://github.com/Aeka0/Overlord/blob/main/docs/vr-gameplay-interaction-architecture.md),
  section 8; a design document, I did not check how much of it is built).
- **Not a shortcut: the compositor.** SteamVR's reprojection fixes the last few milliseconds of head
  motion; it cannot invent what is behind a corner when you lean. The camera itself has to move.

---

## 3. The limit: how far the camera may leave the soldier

### 3.1 Why a limit, and what shape

The only thing the game keeps out of walls is the soldier's collision body (a capsule, believed;
**unverified** for BF3). If the camera stays inside it, it cannot reach a wall the soldier is
touching. So the camera is kept within:

- a **horizontal disc** of radius *r* around the game eye (lean and peek),
- a **vertical range**: a little above standing (tiptoe) down to a safe height above the floor
  (duck, crouch).

Inside, it moves one to one with your head. Near the edge the extra movement is squeezed smoothly
(for example: one to one up to 18 cm, then a curve that approaches 25 cm and never passes it), so
there is no hard wall in the view.

### 3.2 Starting numbers (all to be measured and tuned)

| Setting | Start value | Why |
| --- | --- | --- |
| Horizontal limit *r* | 25 cm, squeezed from 18 cm | to replace by the measured clearance minus 10 cm (below) |
| Up | +10 cm above the F9 height | tiptoe |
| Down | to 35 cm above the floor | needs the eye height above the floor (below) |
| Near plane | 6 cm (the game's) | the camera must stay further than this from any surface |

BF3's capsule size and where the eye sits in it are unknown **(unverified)**. They can be measured
without a person in the headset, using the depth buffer we already read (section 11, test 2.1):
walk the soldier into a wall by script, read the distance from the eye to the wall at the centre of
the view, then turn only the view (a faked head yaw of 90° and 180°) and read again. That gives the
clearance in front, at the side and behind; the limit is that minus about 10 cm. Looking straight
down with a faked head pitch gives the eye height above the floor in the same way.

### 3.3 What the limit does not cover

Thin or low things the capsule doesn't fill: a table edge when you duck, a low ceiling when you
stand up under it, the top of a low wall. Those need the fade (section 4).

---

## 4. When the head goes into a wall

### 4.1 What research and practice say

- Ziegler et al., *Simulator Sick but Still Immersed* (IEEE VR 2018,
  [DOI 10.1109/VR.2018.8446221](https://doi.org/10.1109/VR.2018.8446221),
  [summary](https://bibsonomy.org/bibtex/22097cb8e354799d0f38280b7d7cb04c5/hci-uwb)) compared doing
  nothing, fading to black, and stopping the camera ("push-back") when the head enters an object.
  Push-back felt the most immersive but caused the most simulator sickness; the fade hides the
  clipping. A later paper, Kopel and Stanasiuk, *How to Handle Head Collisions in VR* (2020,
  [Springer](https://link.springer.com/chapter/10.1007/978-3-030-55789-8_54)), looks at the same
  problem; I could not read it, so which methods it compares and what it found are **unverified**.
- The Oculus best-practices guide advises fading or dimming the picture *before* positional
  tracking runs out, rather than letting the view jerk
  ([Oculus Best Practices](https://www.cs.sjtu.edu.cn/~shengbin/course/vr/misc/oculus_bestpractices.pdf)).
- Overlord's rules: never silently teleport the player; resolve with a bounded pose, a limited fade or
  a recenter prompt; never allow seeing or firing through a wall while the game body stays outside
  ([vr-gameplay-interaction-architecture.md](https://github.com/Aeka0/Overlord/blob/main/docs/vr-gameplay-interaction-architecture.md),
  section 8).

### 4.2 Our approach, in three tiers

| Tier | How it knows | What it does | Status |
| --- | --- | --- | --- |
| 1. The limit | the capsule clearance (section 3) | stops the camera at the limit (a small push-back) and darkens in proportion to how far you are past it: none at 5 cm past, nearly black at 20 cm past | needs only measured numbers |
| 2. Depth proximity | the last frame's depth: the nearest scene distance over the view, first-person pixels (stencil 32) left out, per edge (left, right, top, bottom) | darkens as the nearest surface goes from 12 cm to 5 cm (the near plane is 6 cm); can also stop the camera from moving further toward a surface it sees | the depth buffer of each eye must be found in per-eye mode (**unverified**) |
| 3. Game physics | a ray from the game eye to where the camera wants to go | stops the camera 10 cm before the hit and fades | Frostbite has ray casts for bullets and AI, but where it is in bf3.exe 1.6 and how to call it is **unverified**; a reverse-engineering task like the view builder was |

Depth only sees what is in view: a wall right beside you while you look ahead is invisible to it. That
is why tier 1 must hold the line at the side. Tier 3 would cover every direction.

### 4.3 How to darken

- Simplest: a head-locked dark quad layer in OpenXR (in `g_viewSpace`, 0.3 m in front, large enough
  to cover the view), its alpha set each frame, submitted after the projection layer. It does not
  touch the game's rendering. Whether SteamVR blends a view-space quad by its alpha as expected is
  **unverified**.
- Fallback: a full-screen darkening pass on the eye pictures before they are copied to the headset.
- Fade in fast (0.1 s) and out slower (0.3 s). FrostbiteVR's BF4 comfort vignette uses the same
  fast-in, slow-out idea for walking and smooth turning
  ([CURRENT-BF4.md](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/docs/CURRENT-BF4.md)).

---

## 5. Leaning and peeking around cover

- **Inside the lean radius (about 15 cm, section 7): the camera moves, the soldier doesn't.** This
  gives duck and short peeks for free.
- **The game's soldier stays where it is.** Its hitbox does not lean, and neither does the place its
  bullets start from. Today the look angles follow the gun, so bullets fly parallel to the drawn
  barrel but start at the soldier's eye. Leaning 25 cm adds up to a quarter metre to the gap between
  the drawn barrel and the bullet line (the gap is the same at every distance, since the two lines
  are parallel). Peeking around a corner, the bullets start behind the corner and hit it.
- **The real fix is to start the shot at the drawn muzzle.** FrostbiteVR does that in BC2: it
  replaces only the origin of the game's final shot (client and server) with the tracked muzzle
  position, within a maximum distance, and keeps the game's direction, recoil and spread
  ([FIRING-ORIGIN-20260930.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/FIRING-ORIGIN-20260930.md),
  `PlaceFireAtMuzzle` in
  [TrackingMath.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/TrackingMath.cpp)).
  Overlord's rules add that a muzzle behind a wall must not hit what the eyes can see. For BF3 the
  shot functions are unknown: a separate research item (step 7), **unverified**.
- **Beyond the lean radius, room walking moves the soldier** (section 7). Stepping out from cover
  then exposes the soldier, as it would in life, and the bullets come from close to your head again.
- **Idea to try in the headset:** people tilt their head (roll) when they lean. A head rolled toward
  the side it moved to could mean "lean, don't walk", and widen the lean radius on that side. Not
  tested anywhere.
- **Turning while leaning.** The room turns around the soldier when the stick turns, so a head 25 cm
  to the side swings in an arc. Overlord turns around the current head instead (it shifts the room's
  origin so the head stays put), section 7.4.

---

## 6. Physical crouch to the game's crouch (and prone?)

### 6.1 Standing height

- **F9 stores it.** F9 already puts the room's origin at your head; the neck point's height at F9 is
  the standing height (stand upright when you press it).
- **Fallback, from BF4:** the standing height is the highest neck height seen, sinking 1 cm a second
  while standing and held while crouched
  ([Bf4PhysicalCrouch.h](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/src/games/bf4/Bf4PhysicalCrouch.h)).
  This also catches an F9 pressed while bent over.
- **Guard:** FrostbiteVR's core treats whatever posture you recenter in as upright
  ([ComfortControls.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/interaction/ComfortControls.h)),
  which goes wrong if you press it while squatting. Better: F9 updates the standing height only if
  the new one is no more than 15 cm below the learned highest; otherwise keep it and log a warning.
  BC2VR's recenter keeps the crouch state
  ([BC2VR v0.1.12](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.12)); ours should too.
- **Optional:** a floor-based space (STAGE, or LOCAL_FLOOR with `XR_EXT_local_floor`) would give the
  height above the floor directly. Whether the Steam Frame offers them is **unverified**; the design
  doesn't need them.

### 6.2 Thresholds and hysteresis

| | Crouch when the drop is over | Stand again under | Held for | Prone |
| --- | --- | --- | --- | --- |
| FrostbiteVR core `PhysicalStance` ([ComfortControls.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/ComfortControls.cpp)) | 30 cm | 20 cm | 180 ms; a sample gap over 250 ms resets | over 88 cm, back under 72 cm |
| FrostbiteVR BF4 `PhysicalCrouch` | 35 cm | 20 cm | immediate, 0.12 s key pulse | none |
| **Ours (start)** | **30 cm** | **20 cm** | **200 ms** | **button only** |

Rules taken from Overlord's design (in our words): a physical crouch is a *request* that the game
must confirm; leaning, a quick duck, picking something up or losing tracking must not toggle it; it
is an explicit option; a button stays available
([vr-gameplay-interaction-architecture.md](https://github.com/Aeka0/Overlord/blob/main/docs/vr-gameplay-interaction-architecture.md),
section 12.2; [vr-stance-controls.md](https://github.com/Aeka0/Overlord/blob/main/docs/vr-stance-controls.md)).
The 200 ms hold handles the quick duck; the neck point (2.5) handles looking down.

FrostbiteVR's shared stance class has these thresholds, but BC2VR ships crouch as a left-stick click
toggle ([BC2VR README](https://github.com/justiceofrip/BC2VR)); I found no proof that physical
crouch was headset-tested there **(unverified)**. BF4's is off by default, tested only on a
simulated headset ([CURRENT-BF4.md](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/docs/CURRENT-BF4.md)).

### 6.3 Pressing the game's crouch

- Whether BF3's crouch key is "hold" or "toggle" in the user's settings is **unverified** (our B
  button holds Left Ctrl while pressed; how the game treats that hasn't been checked). A self-test
  finds out (test 4.1): tap the key, watch the game camera's height; hold it, watch again.
- If it is a toggle, keep a belief of the game's stance like BF4 does: it follows our own presses,
  the player's own B presses, and the game's own stand-ups (sprint and jump stand you up in BF4;
  for BF3 **unverified**). A stance the player chose with B stays until the body really changes
  posture.
- Confirm by the game camera: a crouch shows up as the game eye dropping within about half a second.
  If it doesn't drop (blocked, in a vehicle, scripted), the request failed.
- Later and better: read the stance itself. Community reverse-engineering of BF3's multiplayer client
  describes a stance field (stand, crouch, prone) and a soldier position on the soldier; offsets for
  this build are unknown **(unverified)**. They can be found the way `aim.cpp` found the look angles:
  search near the client soldier, press crouch, see what changes.

### 6.4 The height of the view: don't drop twice

- When the game crouches, its eye drops by its own amount. In BC2 that was 1.548 m to 0.943 m, about
  60 cm ([checkpoint 433 notes](https://github.com/justiceofrip/FrostbiteVR/blob/checkpoint/436-r56-equipment-rendering/docs/history/2026-10-20-checkpoint433-crouch-and-native-jab.md));
  BF3's is **unverified**, to measure. If we add the real head drop on top, a 35 cm squat shows as
  about 95 cm. FrostbiteVR's BF4 notes show exactly that: with the head 0.4 m low, the view was lower
  with physical crouch on than with it off.
- **Our rule:** the view follows the real head one to one. When *our* physical crouch made the game
  crouch, add the game's own drop back. Measure it from the game camera during the change: at the
  moment of the crouch press note the game eye height; for the next 0.8 s the compensation is how far
  the game eye has fallen since; then hold that value while crouched; on standing, shrink it as the
  game eye rises. Walking crouched over uneven ground then stays right, because the compensation is a
  constant and the game eye follows the ground.
- A crouch from the **B button** keeps the game's drop: you asked for a game crouch without moving.
- FrostbiteVR has a related helper (`PhysicalCameraHeight`, a fixed eye height above the body,
  calibrated while standing) in
  [ComfortCamera.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/ComfortCamera.cpp);
  I found no caller in the files I searched.
- **What remains:** while crouched in the game but only squatting 30 to 35 cm, your eyes are about
  25 cm above the game's eye **(the 25 cm is unverified until BF3's crouch drop is measured)**. You
  may see over a low wall that the game's bullets hit. The muzzle shot origin (step 7) fixes that; for
  alpha it is acceptable.
- **Low ceiling:** if we ask the game to stand and its eye does not rise within 0.5 s, it is blocked:
  limit the view to the game eye plus 10 cm and darken above that.

### 6.5 Prone, jump

- **Prone by button**, not by lying down, at first. Lying on the floor with a headset and controllers
  is awkward, the game's own drop is large (so any error shows), and BF3 limits turning while prone
  **(unverified)**. FrostbiteVR's numbers (88 / 72 cm) are there if the user wants it later. Overlord's
  rule: prone needs explicit intent; a headset put on the floor must never make the soldier lie down.
- **Never map physical jumping** to the game's jump.

---

## 7. Walking in the room: roomscale follow

### 7.1 FrostbiteVR's method (MIT)

From [RoomscaleFollow.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/RoomscaleFollow.cpp)
and [RoomscaleFollow.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/interaction/RoomscaleFollow.h),
described, not copied:

- Inside a **lean radius of 12 cm** only the camera moves.
- Beyond it, the mod asks the game for ordinary walking toward the head at
  **min(0.8 m/s, 2.4 × (distance − 0.12 m))**, through the game's own movement input (BC2: about
  6 m/s at a full stick).
- On the next update it reads where the soldier really is and **"consumes" only the distance it
  really moved** (turned into room coordinates with the body's yaw). Blocked by a wall, nothing is
  consumed and the offset stays.
- Your own stick wins: while you move, it doesn't drive and doesn't consume.
- Safety: displacement steps over 25 cm or update gaps over 150 ms are not consumed (teleports,
  snap turns, recenters); head offsets over 3 m are invalid; a new soldier or a recenter starts
  over.
- Evidence ([CONTROLS-20260930.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/CONTROLS-20260930.md)):
  a 0.4 m physical step consumed 0.28 m and stopped inside the lean radius; blocked beside a boat,
  nearly nothing was consumed; the user accepted the controls and body-follow headset test on
  2026-09-30. Their own stated limits: no wall-safe head motion, no collider resize for crouch, no
  slopes or vehicles ([CONTROLS-20260930.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/CONTROLS-20260930.md),
  [ARCHITECTURE.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/ARCHITECTURE.md)).

Overlord's design goes the other way: leaning in the room never moves the game body by itself, and
the head stays in a bounded space around it. We combine both: the soldier follows beyond the lean
radius, and the camera never leaves the envelope of section 3.

### 7.2 Doing it in BF3

- **Output:** `pad::SetMove` (the virtual Xbox left stick) when the player's stick is in its dead
  zone. The follow vector is in room coordinates; turn it into the game's facing by the hand's yaw
  (the game walks along its look yaw, which follows the gun), the same kind of turn `input.cpp`
  already does for "walk where the head faces".
- **Unknown:** how fast BF3 walks for a given stick deflection, its dead zone and its acceleration
  **(unverified)**. If the dead zone is large, slow speeds need short bursts above it. A self-test
  measures the curve (test 5.1).
- **Where the soldier is:** at first the game eye's horizontal position (the main camera's
  translation, or the weapon camera's). Walking bob adds noise **(unverified how much)**; filter it.
  Later the soldier's own position, once found (6.3).

### 7.3 Defaults

Lean radius 15 cm (a little more than FrostbiteVR's 12 cm, for peeking), speed as FrostbiteVR, a
setting to turn it off.

### 7.4 Turn around the head

Our right stick turns smoothly (266°/s). With the head 25 cm to the side, a 90° turn swings the
camera about 35 cm. Overlord turns about the current head by shifting the room's origin
([vr-camera-architecture.md](https://github.com/Aeka0/Overlord/blob/main/docs/vr-camera-architecture.md)).
For us: each frame the stick turns, re-express the remaining head offset so the camera stays where it
is in the world; the soldier stays put and follow walks it under the head again if needed.

### 7.5 Safety

- **Ledges.** Rock and a Hard Place has cliffs. Leaning out over an edge makes follow walk the soldier
  toward it. Keep the speed low; consider no follow while the head is low and pitched down (looking
  over an edge); an idea to try: check with the depth buffer that there is ground where the step
  would go (when that spot is in view). **Untested ideas.**
- **Stuck against a wall:** the stick keeps pushing; harmless (footsteps at most). The limit and fade
  take over.
- **Off** while scripted, in vehicles, in menus, dead, sprinting, and for a second after a load.
- **Reset** the walked distance on F9, a new soldier or checkpoint, a scripted moment starting or
  ending, and any game camera jump over 1 m in a frame.

---

## 8. The gun and hands with a moving camera

- **Already consistent.** The hands are computed relative to the head and drawn with each
  first-person draw's own view-projection (the weapon camera's eye). Moving the world camera changes
  nothing about where they appear relative to you; they stay where your hands are in the world. The
  per-eye shift of the gun (skin.cpp) is the same ±IPD/2.
- **To check by self-test:** the gun-to-eye distance in the first-person uploads stays constant
  (the existing trace showed 2 mm) with and without a faked head offset; in eye dumps the gun stays on
  the same pixels while the world shifts.
- **Known side effects:** the game's own muzzle flash and casings are still drawn at the soldier's
  chest (open issue), now further away when you lean. With free hands off (Insert), the game's own gun
  stays fixed to the view, as in the flat game.
- **The bullet line** moves further from the drawn barrel when you lean (section 5).
- **Believed, not checked:** BF3 draws no body for the player in first person **(unverified)**; if it
  did, 6DoF would pull the camera away from it.

---

## 9. Cutscenes and scripted moments

The user's decision (memory note cutscenes-and-qte.md): keep 3D, lock the body to the game camera,
head free, level horizon.

| | Normal play | Scripted moment (cutscene, car ride, death camera) |
| --- | --- | --- |
| Body heading | game camera minus the hand's yaw | the game camera's yaw only (pitch and roll dropped) |
| Head rotation | free | free |
| Head movement | one to one, limit of section 3 | one to one within 10 cm, squeezed to 15 cm, measured from where the head was when the moment began |
| Room walking, physical crouch | on | off; the stance stays as it is |

- **Detection:** the look angles stopped matching the weapon camera for 1 s (aim.cpp), a camera cut
  (a jump of over 1 m or 30° in one frame without a stick turn), or the opening car ride.
- **Level horizon:** today the turn removes the game's pitch but not a roll; in scripted moments
  build the body frame from the game camera's yaw alone (2.2). Optional later: smooth the game
  camera's vertical bumps in the car.
- **Entry without a jump:** today the frozen hand yaw is still taken out of a scripted camera, so the
  action can sit off to one side. Drop it at the first camera cut (a cut already jumps), or blend it
  out over half a second under a short fade. Overlord aligns once on entry
  ([vr-camera-architecture.md](https://github.com/Aeka0/Overlord/blob/main/docs/vr-camera-architecture.md));
  FrostbiteVR's vehicle anchor takes the head pose at entry so the view starts exactly at the game's
  seat camera ([VehicleTracking.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/interaction/VehicleTracking.h),
  `VehicleCameraAnchor`).
- **Head movement in scripted moments:** Overlord scales it down to 10% with a 5 cm limit
  ([vr-scripted-free-look.md](https://github.com/Aeka0/Overlord/blob/main/docs/vr-scripted-free-look.md)).
  Standing players lean more, so start with one to one inside 10 cm and tune.
- **Exit:** take the current head position as the new neutral for the walked distance (no jump),
  short fade if the camera position jumps.

---

## 10. The three projects at a glance

| Topic | FrostbiteVR (BC2, BF4 branch) | BC2VR (release) | Overlord (ideas only) | Ours (planned) |
| --- | --- | --- | --- | --- |
| Head position | composed one to one on the body or game camera, no limit | room-scale movement listed as included ([README](https://github.com/justiceofrip/BC2VR)) | bounded envelope; per-scene rules | one to one inside the capsule limit |
| Walls | not handled (said so) | same as FrostbiteVR | prevent or resolve: bounded pose, limited fade, prompt, never teleport | limit + depth fade, physics ray later |
| Room walking | follow beyond 12 cm, consume only real motion | included | lean does not move the body | follow beyond about 15 cm |
| Crouch | core class 30/20 cm; BF4 35/20 cm toggle pulses, no height compensation | stick-click toggle; recenter keeps it | request confirmed by the game; right-stick backup | 30/20 cm, 200 ms, compensated height |
| Prone | core class 88/72 cm | not mentioned | explicit intent only | button |
| Scripted scenes | vehicle seat anchor at entry | not mentioned | align once, 10% movement, 5 cm | yaw-only body, 10-15 cm, level horizon |
| Shot origin | moved to the tracked muzzle (BC2, opt-in prototype) | whether releases turn it on is unknown | at the physical muzzle | later research |
| Recenter | upright, keeps yaw when looking down; once after 750 ms of lost tracking | both stick clicks 1 s | resets heading, keeps pitch/roll | F9, plus these guards |

---

## 11. Implementation plan, step by step, with self-tests

Each step is switchable (`bf3vr.ini` `[head]`, and the free **Home** key toggles 6DoF on and off).
With 6DoF off the mod must behave exactly as today. "Self" means it can be checked from the log or
eye dumps without a person in the headset; "User" means it needs the headset. The game runs with god
mode on for the scripted tests.

### Step 0: test hooks first (no behaviour change)

`build\weapon.txt` lines, in the existing style:

- `headtest x y z [yaw pitch]`: a fake head position (metres, room space) and turn, used instead of
  the real head until `headtest off`. It must work while SteamVR asks for no frames: apply it at
  `views` level and through `camera::SetViewTurn` from `OnPresent`, not only inside `BuildStereo`.
  (Earlier self-tests ran with the headset asleep; whether `BuildStereo` runs then is **unverified**.)
- `sixlog N`: for N seconds, per frame: game eye (world), body yaw, raw and limited offset, rendered
  minus game eye along the body's right, up and back, clearance, fade, follow command, walked
  distance, stance (believed and confirmed), crouch compensation.
- `clearance`: the scene distance at the centre and the nearest per edge, per eye.
- `movetest`, `crouchtest`: tests 5.1 and 4.1 below.
- Reuse: `standin 1` (a fake right hand), `eyetrace`, `viewlog`, depth3d `save N` (both-eye dumps),
  F17 (3 s stick turn), `tools/drawhunt.ps1` scan-code keys, `tools/selfcheck.ps1`.

**Self:** with no test line set, `sixlog` shows a zero offset and nothing else changes.

### Step 1: the camera follows the head (no limit yet but a 0.5 m safety cap)

Code: `camera::BodyFrame`; head position in `ViewTurn`; the offset in `MakeEyes` (2.3); rendered
poses in the pose history (2.4); position-valid and tracked checks.

| Test | Pass if |
| --- | --- |
| 1.1 Self: `headtest 0.2 0 0` | rendered minus game eye = +0.200 m along the body's right (±2 mm), 0 along up and back (±2 mm); same for both eyes |
| 1.2 Self: `headtest 0 0.1 0`, then `0 0 -0.2` | +0.100 along up; +0.200 along forward |
| 1.3 Self: facing a wall, `headtest 0 0 -0.2` | the centre clearance drops by 0.20 m (±1 cm) |
| 1.4 Self: `standin 1`, offset 0 then 0.2 | `eyetrace` gun-to-eye gap unchanged (±2 mm); in eye dumps the gun keeps its pixels, the world shifts |
| 1.5 Self: offset 0.2 with a fake head yaw of 0°, 45°, 90° | the offset stays along the body's right (it must not turn with the head) |
| 1.6 Self: frame timing | still 72 fps; no added time in Present worth measuring |
| 1.7 Self: never-break list with 6DoF on and off | aim follows the gun, the view is still when the head is still (no drift), stick-turn direction, walking where you look, both eyes alike |
| 1.8 User | lean, duck, side to side: the world stays still (no swimming); nothing doubles |

### Step 2: the limit and the out-of-range fade

Measure first (test 2.1), then code the limit (3.1) and the dark layer (4.3).

| Test | Pass if |
| --- | --- |
| 2.1 Self: walk into a wall by script (W until the centre clearance stops shrinking for 1 s); read the clearance; fake head yaw 90°, −90°, 180°; fake pitch −90° in the open | front, side and back clearances and the eye height are logged; the limit is set from them |
| 2.2 Self: `headtest 1.0 0 0` | rendered offset = the limit (±1 mm), fade near full |
| 2.3 Self: sweep `headtest` from 0 to 0.4 m in 1 cm steps | rendered offset rises smoothly and never passes the limit; no jumps |
| 2.4 Self: offset at the limit beside the wall from 2.1 | the side eye dump shows no see-through at the wall |
| 2.5 User | the limit feels natural; the fade is a cue, not a shock |

### Step 3: the depth proximity fade

Check that the per-eye depth buffer is found in per-eye mode (the "Depth 3D: depth buffer" log line,
its size); add a small minimum-distance pass with a delayed read-back (no stall), skipping stencil 32.

| Test | Pass if |
| --- | --- |
| 3.1 Self: at the wall from 2.1, `headtest` forward 0 to 0.25 m | the clearance falls; the fade goes from 0 at 12 cm to full at 5 cm; eye dumps darken |
| 3.2 Self: frame timing | the pass costs under 0.2 ms **(target, unverified)** |
| 3.3 User | ducking under a table edge or into a doorframe darkens before the view cuts |

### Step 4: physical crouch

| Test | Pass if |
| --- | --- |
| 4.1 Self: `crouchtest`: Left Ctrl tapped 0.1 s, then held 1.5 s; Z tapped (prone, **unverified** key) | logs whether crouch is hold or toggle, the game eye drop and how long it takes |
| 4.2 Self: `headtest 0 -0.25 0` held 1 s | no crouch press |
| 4.3 Self: `headtest 0 -0.32 0` held | a crouch after 200 ms; the game eye drops; the rendered eye stays at standing − 0.32 m the whole time (no step over 1 cm per frame beyond the faked motion) |
| 4.4 Self: then −0.22, then −0.18 | still crouched at −0.22; stands at −0.18; the view rises one to one |
| 4.5 Self: B pressed while the faked head is standing | the game crouches and the view drops by the game's amount (button crouch, no compensation) |
| 4.6 Self: F9 with the faked head at −0.4 | standing height not overwritten; warning logged |
| 4.7 User | thresholds feel right; no crouch from looking down, picking things up or a quick duck |

### Step 5: room walking and turning around the head

| Test | Pass if |
| --- | --- |
| 5.1 Self: `movetest`: stick deflection 0.1 to 1.0, 1.5 s each, standing and crouched | a table of speed against deflection; the dead zone is known |
| 5.2 Self: open ground, `headtest 0.5 0 0` | the soldier strafes about 0.35 m within 2 s and stops; the walked distance matches the soldier's movement (±3 cm); the rendered camera stays within ±3 cm in the world |
| 5.3 Self: then `headtest 0 0 0` | the soldier comes back; no drift after 30 s |
| 5.4 Self: at the wall from 2.1, offset toward the wall | nearly nothing walked; the limit and fade take over |
| 5.5 Self: stick pushed during 5.2 | follow output zero; the stick moves the soldier (manual wins) |
| 5.6 Self: offset 0.25 to the side, F17 turn | the rendered camera stays within 2 cm in the world (turn around the head) |
| 5.7 User | small steps feel natural; no unwanted walking while leaning; walking where you look still works |

### Step 6: scripted moments

| Test | Pass if |
| --- | --- |
| 6.1 Self: load the checkpoint (the opening ride), `sixlog` | scripted on during the ride, off on foot; no crouch or follow during it; rendered roll = head roll (±0.5°), the game camera's roll and pitch not shown |
| 6.2 Self: `headtest 0.3 0 0` during the ride | limited to 15 cm |
| 6.3 Self: entering and leaving | no camera jump over 2 cm or 2° except at a game camera cut |
| 6.4 User | the car ride is comfortable; the action stays in front |

### Step 7: research, when the above works

1. The soldier's own position and stance field (search near the client soldier, crouch test): exact
   height compensation, no walking bob, better follow measurement.
2. The physics ray cast in bf3.exe 1.6: tier-3 wall test.
3. The shot origin at the drawn muzzle (FrostbiteVR's BC2 method as the model): bullets from the gun
   you see.
4. Camera bob from the game while walking: measure; remove if it shows (Overlord makes it an option,
   [vr-camera-bob.md](https://github.com/Aeka0/Overlord/blob/main/docs/vr-camera-bob.md)).
5. Physical prone, if the user wants it.

### Additions to the never-break list

6DoF off behaves as before; the view doesn't drift with the head still; follow never overrides the
stick; no crouch from looking down; F9 doesn't lose the crouch state; both eyes get the same offset;
72 fps.

---

## 12. Open questions (all unverified)

- BF3's soldier capsule size and where the eye sits in it (test 2.1).
- BF3's standing, crouched and prone eye heights; how long its crouch takes (test 4.1).
- Crouch key hold or toggle in the user's settings; prone key; whether sprint and jump stand you up.
- Walking speed against stick deflection, the dead zone and acceleration (test 5.1).
- Whether the per-eye depth buffer is found in per-eye mode.
- Whether the game camera rolls in vehicles or cutscenes.
- Whether SteamVR blends a head-locked quad by its alpha as needed for the fade.
- Whether the Steam Frame offers STAGE or LOCAL_FLOOR spaces.
- Where BF3's ray cast, soldier position, stance and shot origin are in bf3.exe 1.6.
- Whether BF3 draws any first-person body.
- How much walking bob BF3's camera has at slow speeds.

## Sources

- FrostbiteVR: [repository](https://github.com/justiceofrip/FrostbiteVR),
  [RoomscaleFollow.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/RoomscaleFollow.cpp),
  [ComfortControls.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/ComfortControls.cpp),
  [ComfortCamera.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/ComfortCamera.cpp),
  [Bc2TrackedBodyBase.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/games/bc2/Bc2TrackedBodyBase.h),
  [Bc2TrackedViews.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/games/bc2/Bc2TrackedViews.h),
  [StereoMath.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/math/StereoMath.h),
  [StereoCulling.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/math/StereoCulling.cpp),
  [ControllerInput.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/ControllerInput.cpp),
  [RecenterPolicy.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/interaction/RecenterPolicy.h),
  [VehicleTracking.h](https://github.com/justiceofrip/FrostbiteVR/blob/main/include/fvr/interaction/VehicleTracking.h),
  [TrackingMath.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/main/src/interaction/TrackingMath.cpp),
  [CONTROLS-20260930.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/CONTROLS-20260930.md),
  [HEADSET-CALIBRATION-20261001.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/HEADSET-CALIBRATION-20261001.md),
  [FIRING-ORIGIN-20260930.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/FIRING-ORIGIN-20260930.md),
  [ARCHITECTURE.md](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/ARCHITECTURE.md),
  BF4 branch: [Bf4PhysicalCrouch.h](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/src/games/bf4/Bf4PhysicalCrouch.h),
  [Bf4ViewOverride.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/src/games/bf4/Bf4ViewOverride.cpp),
  [Bf4AdapterCore.cpp](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/src/games/bf4/Bf4AdapterCore.cpp),
  [CURRENT-BF4.md](https://github.com/justiceofrip/FrostbiteVR/blob/bf4/vr-adapter/docs/CURRENT-BF4.md);
  newest BC2 branch: [checkpoint 433 crouch notes](https://github.com/justiceofrip/FrostbiteVR/blob/checkpoint/436-r56-equipment-rendering/docs/history/2026-10-20-checkpoint433-crouch-and-native-jab.md).
- BC2VR: [README](https://github.com/justiceofrip/BC2VR),
  [release 0.1.12](https://github.com/justiceofrip/BC2VR/releases/tag/v0.1.12).
- Overlord (GPL-3.0, ideas only):
  [vr-camera-architecture.md](https://github.com/Aeka0/Overlord/blob/main/docs/vr-camera-architecture.md),
  [vr-gameplay-interaction-architecture.md](https://github.com/Aeka0/Overlord/blob/main/docs/vr-gameplay-interaction-architecture.md),
  [vr-scripted-free-look.md](https://github.com/Aeka0/Overlord/blob/main/docs/vr-scripted-free-look.md),
  [vr-stance-controls.md](https://github.com/Aeka0/Overlord/blob/main/docs/vr-stance-controls.md),
  [vr-camera-bob.md](https://github.com/Aeka0/Overlord/blob/main/docs/vr-camera-bob.md).
- Research and guidelines: Ziegler et al. 2018,
  [DOI 10.1109/VR.2018.8446221](https://doi.org/10.1109/VR.2018.8446221) and
  [summary](https://bibsonomy.org/bibtex/22097cb8e354799d0f38280b7d7cb04c5/hci-uwb);
  Kopel and Stanasiuk 2020, [Springer](https://link.springer.com/chapter/10.1007/978-3-030-55789-8_54);
  [Oculus Best Practices](https://www.cs.sjtu.edu.cn/~shengbin/course/vr/misc/oculus_bestpractices.pdf);
  neck model constants, [ovr-sys](https://docs.rs/ovr-sys/0.3.0/src/ovr_sys/lib.rs.html);
  UEVR also offers optional room-scale movement per game ([UEVR](https://github.com/praydog/UEVR)).
- Our own: `src/views.cpp`, `src/camera.cpp`, `src/xr_runtime.cpp`, `src/hands.cpp`,
  `src/input.cpp`, `src/aim.cpp`, `src/depth3d.cpp`; memory notes bf3-per-eye-views.md,
  bf3-look-angles.md, bf3-gun-effects.md, cutscenes-and-qte.md, vr-roadmap.md, never-break.md.
