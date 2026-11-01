# The train pistol's manual reload (The Great Destroyer): study and design

Studied 2026-10-31 from the game's own reload (recorded with `reloadrec`), the M416's manual reload in
the mod, and the pistol's ammo. Build steps 2-4 are in (behind `build\pistol.txt`, off by default) and had their
first headset test on 2026-11-01 (below); the next round's magnetic magazine and G2 carry, and the magnetic slide with
three grips, are in after it. The working files (prototype modules, offline tests, pose matrices and pictures) are in
`screenshots\wf_reload\` on the development machine. They
stay out of the repository, because the pictures, recordings and pose files are taken from the game's own
meshes and animations; only the finger matrices the mod needs are in `src\pistolposes.h`, as handpose.cpp's are.

## What the game does

- **Both recorded reloads are the same animation.** "reloadrec_empty.bin" was not empty: all 12 shots
  cycled the slide home, and the reload after them only swapped the magazine. The game's own slide
  lock at 0 rounds has never been recorded.
- **The magazine moves along one straight line**, its own length: 21.2 cm long, raked 16 degrees back
  from the gun's vertical. Every frame is within 0.17 mm of it, with no rotation. Seated at
  (0, 0.138, -0.025) in the frame bone's space; the grip's mouth is 11.08 cm below home.
- **The slide** moves only along z, 4.57 cm (from -0.2995 to -0.3453), in about 110-125 ms a shot.
  The hammer and trigger bones never move.
- **The game's left hand** leaves the gun at 1.27 s, goes 64 cm left and about 28 cm down
  (off-screen, not to a pelvis pouch), and comes back with the magazine in a loose pinch (2.218 s).
  It pushes the last 7 mm home with the palm flat under the floorplate (2.34-2.45 s). That is the
  only time the palm is under the floorplate.
- So the game's reload can't be used as an animation with free hands: it takes the hand off-screen,
  tilts the pistol up to 38 degrees, moves the magazine by itself and never touches the slide. Its
  numbers are used: the magazine's path, the seat, the flat-base hand (2.390 s), the seated push
  (2.421 s) and the pinch (2.218 s).

## The pistol's ammo

`ammo::Get` doesn't work for the pistol: no "Ammo:" line in any log from this mission. The server
soldier is found, but the path that works for the M416 (soldier +0x4C0, +0x248, the firing class's
vtable, +0xF4 loaded / +0xF8 spare) fails at its last step. A read-only probe (`ammoprobe.ps1`, which
opens the game for reading only) finds where it breaks and which field drops by one a shot. A
slide-cycle shot detector, run on the game's own pistol bones, found 12 of 12 shots in the recording
and none in the reload; it proves the field before anything is written.

**Found at the train (2026-10-31, read-only, from single shots and a reload: 8/114, 2/62, 1/62, 15/47,
14/47 on the HUD).** Three objects of the rifle's own firing class (vtable bf3+1DAD810) hold the loaded
rounds at +F4 and the spare at +F8, as the M416's does:
- the server's, the one that decides: server soldier +4EC, then +40, then +128;
- two client copies: client soldier +D4, then +C0, then +160 and +164.

Still to check: whether that server path is the pistol's own slot or whichever weapon is in hand
(switch weapons and look again).

## The design (its own profile; nothing copied from the M416)

| Control | What it does |
|---|---|
| X tap (right thumb) | Drops the magazine, like the M9's button behind the trigger guard. With one half-way in, drops that. |
| X held 0.6 s | Unchanged: R, the game's reload and pickups (the shotgun at the train's door). |
| Left grip at the pelvis | Takes a magazine (pouch.cpp's zone and squeeze rule). |
| Left grip held | Carries it; let go away from the gun and it falls. |
| Left hand to the grip | Inserts it: caught within 7.5 cm of the grip's mouth from any direction, it glides in and only then the fingers open (the flat base); the palm pushes it up its line, or held there gripped it goes home by itself. |
| Left trigger at the slide's rear | Grabs the slide (as the M416's charging handle) within 13 cm, in the grip the hand is turned for (overhand, the fingers over its left side, or a pinch from behind); a press just before the hand gets there waits 0.3 s for it. The hand glides on; pull back fully and let go to rack. |
| Right trigger | Fires. Empty with the slide locked and a new magazine in: the first pull sends the slide home. |

- **Firing to empty locks the slide open**: the mod draws it 4.1 cm back, since the game's own slide
  always goes home (it still has the mod's hidden round).
- **Carry and flat base**: the magazine sits on the palm in one place the whole time, as the game puts
  it at 2.390 s. Only the fingers change: closed round it to carry (C2), open with the magazine standing
  on the palm at the gun (B). So nothing jumps in the hand at the well.
- **The slide by hand**: the palm on the slide's top, the fingers over its right side's serrations, the
  thumb on its left (fitted on the meshes; the game has no rack of its own). Since the next round's step 2, two more
  grips beside it: the fingers over its left side with the hand from the right, and a pinch from behind (below).
- Holding X still plays the game's own reload as a fallback.

## Fixes the review asked for before building

1. **The seat is probably out of reach.** Seating needs the left palm 8 cm from the right grip point
   and 1 mm from the right hand, and real controllers can't overlap (the closest two-handed take in the
   logs was 43 mm). Add a tunable snap distance and a closest-approach log, and set it from the first
   headset test.
2. The dropped magazine's box must be in the magazine bone's bind space (x -0.0132 to 0.0130,
   y -0.2041 to -0.0926, z 0.0743 to 0.1298), not gun space: otherwise it tumbles about a point 13.8 cm
   off.
3. X with a magazine half-way in must drop it from the held draw, not a stale pose.
4. The pistol's trigger edge needs its own hysteresis on the raw value (armed below 0.4, pulled above
   0.6): otherwise a trigger resting near 0.6 can fire right after an automatic rack.
5. Draw the locked slide in one place only (HandTargets), not also from the ammo code.

## Build order

1. Read-only ammo diagnosis at the train checkpoint with the pistol in hand (nothing written); record
   the M9's magazine sounds from its own reload.
2. The slide and magazine drawn by the mod (behind `build\pistol.txt`, off by default), tested headless.
3. The slide by hand, visual only.
4. The magazine by hand, a rehearsal with no ammo. **First headset test** (the rounds don't change yet).
5. The ammo proof, logging only.
6. The mechanism with writes, each re-checked before writing. **Second headset test.**
7. The user's answers, the slide sounds, remembering a proven pistol across weapon switches.

## The user's verdict on the pictures (2026-10-31)

- The closed carry (C2), the flat base (B), the push into the grip and the overhand slide rack are
  "great".
- **The game's own pinch (C1) is out as it is:** "Holding it with one finger is not doable." Wanted: a
  carry where all the fingers grasp the magazine, the thumb too. To be studied again (new candidates
  with every finger and the thumb on the magazine's body, pictures from the same four views).

The user's answers to the mechanics:
- After a new magazine in an empty pistol, the first trigger pull only sends the slide home; the next
  one fires.
- The slide is grabbed with the left trigger, as the M416's charging handle.
- Racking with a round chambered throws that round out, lost, like a real gun.
- The magazine may snap home by itself once it's most of the way in (the distance set from the first
  headset test).

## The carry the user chose: G2 (2026-10-31)

Three full-grasp carries were fitted on the meshes (`screenshots\wf_reload\grasp\`, picture 23): G1 a fist round
the magazine, G2 the index straight along its front, G3 C2's place with every finger on it. G1 and G2 pass the
contact checks; G3 fails (from C2's place the little finger can't reach the body). The user chose **G2**. Its place
on the palm is 52 degrees and 1.7 cm from the flat base's, so it meets the gun with a regrip; three ways to do that
were prototyped offline (`screenshots\wf_reload\g2\`).

## The first headset test (2026-11-01, steps 2-4, the closed carry C2, snap 4 cm)

- Works: walking; X drops the magazine and the trigger stays dead while it's out; a magazine taken at the pelvis;
  pushed home it fires again; firing and the two-handed hold as before; X held is the game's reload.
- **The slide is hard to grab:** the left trigger missed at 7.1-9.9 cm from the overhand hold (the reach is 7 cm),
  and only the overhand grip exists. The user: make it "as magnetic as possible", polished, without losing the
  animation, and three grips: fingers over the right side (today's), fingers over the left side, and a pinch with
  the index and thumb from behind.
- **The magazine is hard to get in:** "it takes a couple of attempts". The flat base flickered open and shut, and
  the magazine slipped past the grip's bottom uncaught (the funnel is 5 mm and 3 degrees at the mouth, and a top that
  has gone above the mouth isn't caught). Once caught, every push seated it (2.1-3.5 cm short, inside the 4 cm snap).
  The user: magnetic, from any direction (hand to gun or gun onto the magazine), "make sure there's at least some
  animation to it".
- The test's log: `screenshots\logs\bf3vr_0029_firsttest.log`.

## Next round (Wednesday 2026-11-04, at the user's request)

1. The magazine: a magnetic catch from any direction, gliding onto the grip's line, an animated snap home, no
   flicker; tunable in pistol.txt.
2. G2 as the default carry, in the magnetic catch (the regrip while the well holds the magazine).
3. The slide: a much larger reach whatever the hand's turn, the hand eased onto it, and the three grips (two new
   poses to fit). Built as step 2 (below).
4. Then the ammo proof and the writes (build order 5-6).

## Next round, step 1 built (2026-11-01): the magazine magnetic, G2 the carry

Off by default as before (`build\pistol.txt` "mag 1"); tested offline only (`tools\pistolmagtest.cpp`, 158 checks).
- **The catch**: a magazine in the hand is caught once its top is within 7.5 cm of the grip's mouth, or already in the
  grip, its length within 55 degrees of the grip's line, from any direction (hand to gun or gun onto hand). Not while
  it's plainly going away or swiping past (where it will be 0.08 s on must be in reach too), nor faster than 4 m/s (a
  tracking jump). The funnel and the "from above" rule are gone. A near miss is logged once, with why.
- **The glide**: caught, it glides onto the line over 0.15 s, its top 1.5 cm into the grip (or as deep as it already
  was); the drawn hand goes with it. The push from then on goes by how far the **palm** moves on up the line from where
  it was at the catch, so the hand can stop where the controllers meet and still push. Turning the wrist doesn't count:
  read through the whole hand pose, straightening the magazine after the catch swung its top centimetres away.
- **G2's regrip** (the three prototypes' findings, their judges never ran): the catch and the push go by the carry's own
  hold, what the player lines up (A, C). Once caught, the drawn hand goes over from G2 onto the flat base B about the
  floorplate's middle, the fingers ahead (C's schedule: the thumb at most 2.7 mm into the magazine), and the magazine
  waits 3.1 cm in until it's done (G2's index meets the frame 3.25 cm in: C). So the magazine never turns in the hand.
  Prototype B (G2's place all the way in, with two new finger poses) wasn't used: the user hasn't seen those poses.
  C2 and the game's pinch go the same way (C2 only opens its fingers).
- **The snap**: 6.5 cm by default, eased over 0.15 s from the speed the push had, the hand going with it and then
  easing back to the controller (not 3 cm a frame and 0.35 a frame). A lost frame mid-snap still seats it.
- **The dwell, decided yes** (0.4 s, "dwell 0" turns it off): held at the grip with the grip squeezed, it goes home by
  itself. Why: the controllers can't overlap, so the last centimetres are the hard part; the first test had every caught
  magazine pushed home anyway; the user asked for magnetic. Gripped only, so letting go and dropping away still parks it.
- **No flicker**: the flat base comes only with the catch now, never before it; leaving needs three frames beyond wider
  limits than the catch; a magazine pulled back out isn't caught again until it heads back in or has been out of reach;
  a hand lost a moment (0.25 s) keeps it on the line. Each change is one log line.
- **Tunables** (re-read live, logged once when read): "catch 7.5 55", "glide 0.15", "seat 6.5", "dwell 0.4"; "carry g2"
  is the default, "carry c2" and "carry game" stay.
- **The review's fixes** (`tools\pistolmagtest.cpp`: 195 checks; 29 of the 37 new ones fail on the step as first built,
  and one more needs EasingBack, which is new):
  - Leaving is judged by the hand going away, never by its going on in. A real hand caught 6-7.5 cm off carries on
    towards the grip, and the palm's move since the catch was being read as leaving: in the review's scratch runs every
    catch from the side, the front or a 45-degree diagonal with the hand still moving (0.4-1.5 m/s) was pulled back out
    or parked out of the palm's reach. Now across the line counts only past where the palm was at the catch and 5 cm
    (4 while gliding in) past the nearest it has come since, measured from where the palm would hold the magazine on
    the line; down the line still counts at 2.5 cm (3), but only if the palm is also that much further from where it
    would hold it, so coming down and in from above the grip's mouth isn't leaving either. All of those now go in.
  - Pulled back out, it's armed again once its top heads back in (2 cm nearer home than the furthest it has been):
    pulled out within the grip and pushed straight back up, it's caught again.
  - A near miss is told, and the catch armed again, only 1.5 cm out of reach: a top trembling 1 mm at the edge of reach
    was told 72 times in 3 s.
  - Parked by the hand leaving it, it isn't pushed on again until the hand has been out of a push's reach (a hand in
    from the side above a parked one and lowered was pushed and parked twice).
  - The ease back to the controller no longer waits through frames without Update: Clear (the magazine by hand off, free
    hands off) drops it, and hands.cpp runs Update until it's done, the pistol in hand or not; the hand had been drawn
    9 cm off the controller, with the flat base's fingers, on the next Update.
  - A pass under the grip carried on past at up to about 1 m/s is caught and left part way in (parked: the palm pushes
    it on, X drops it); faster, it's pulled back out into the hand (from 2 m/s or so it isn't caught at all). Before,
    both were pulled back out.
- **How far the drawn hand gets from the controller**: caught 7.5 cm under the grip's mouth and held still, the glide
  takes the magazine 9 cm up to 1.5 cm in and the dwell's snap the 9.6 cm left, the hand drawn on it going with it:
  18.4 cm at the most (measured), turned up to 52 degrees for G2. Pushed, the real hand goes up with it. The step's own
  report put it at about 14 cm. For the headset test to judge.

## Next round, step 2 built (2026-11-01): the slide magnetic, three grips

Off by default as before (`build\pistol.txt` "slide 1"); tested offline only (`tools\pistolslidetest.cpp`, 134 checks).
- **Three grips** (`screenshots\wf_reload\slidegrips\`, picture `design\pictures\21b_three_slide_grips.png`; their
  fingers, palms and hand directions are in `src\pistolposes.h` from `slidegrips\slidegrips.h`): S1 overhand, the
  fingers over the right side (today's, "great"); S2 the fingers over the left side, the hand from the gun's right; S3
  the index and the thumb pinching the rear from behind. **Neither new grip passed its independent check**, and neither
  has been seen in the game: S2's ring finger pad is 1.85 mm off the grooves (the limit was 1.5) and its palm sits about
  2 cm above the slide; S3's thumb base has the hammer's corner 2.46 mm in it for the first 1.6 mm of the pull, and its
  index holds with the side of its tip. Both went in anyway as best efforts, for the user to judge.
- **Which grip**: the one the hand is turned for (out of the palm and along the fingers, as the controller holds it),
  of those whose hold is in reach, so the slide is grabbed however the hand is turned. The fingers tell them apart (S1's
  point to the gun's right, S2's to its left, S3's at the muzzle). The grip it had last is taken again unless another
  fits clearly better (between S2 and S3, 70 degrees of the hand's turn apart, a hand goes over at 35 degrees, or 7
  further on after the other). Once grabbed it's that grip until the hand has eased back off: never a flip mid-pull.
- **Magnetic**: a press grabs with the palm centre within 13 cm of a grip's hold on the slide's rear (it was 7 cm from
  S1's, where the first test's presses missed at 7.1-9.9 cm; every one of them grabs now, whichever way round the hold).
  A press made out of reach waits 0.3 s for the hand, the trigger held, and grabs the moment the palm comes within
  reach; let go first, or too late, it's told once as a miss with the nearest the palm came.
- **The glide**: the hand drawn glides from where it's drawn into the grip, eased over 0.12 s, its fingers with it, the
  palm going straight (the blend turns about the palm's centre; about the glove's anchor, a hand turned the other way
  round swung the palm 3.5 cm a frame). Then it rides the slide back as before. Let go, the slide springs home as before
  (5 frames from fully back) out of the hand, which eases back to the controller over the same 0.12 s. Grabbed again
  while it's still easing back, it glides on from where it's drawn (0.4 mm that frame).
- **The hand going on forward**: a hand still coming in as it takes hold (S3 from behind: grabbed 12.5 cm out, it goes
  on 10 cm) would have had to come all the way back before the slide moved. Now the hand going forward of home (or the
  slide stop) slides forward on it, so the pull counts from where the hand is (a deliberate change to one old check:
  pushed 3 cm forward on the stop, a 3 mm tug is now 3 mm from there, not from where it was grabbed).
- **The right trigger**: no grab while it's pulled past 0.6 (a left trigger squeezed along with a shot; the 13 cm takes
  in the two-handed hold, and a slide held holds the trigger off). A slide already held stays held. Kept: the right
  trigger held off while the slide is held, with its reset; no two-handed hold taken while the slide is held; no grab
  with a magazine in the hand or for 0.3 s after a seat; the M416's charging handle untouched.
- **Tunables**: "grab 13 0.3 0.12" (the reach in cm, the wait and the glide in seconds; kept to 3-25 cm, 0-1 s,
  0.05-0.5 s), logged once when pistol.txt is read. Each grab's log line says which grip, how far, how long after the
  press, and how far the hand's turn was from the grip's.

## Open questions for the user

1. ~~Carry the magazine with the hand closed round its base (C2, recommended), or the game's loose pinch
   (C1)?~~ C1 to be redone as a full grasp (above).
2. The flat base: the game's palm cupped about 45 degrees under the magazine with the fingers up the
   grip, or a flat palm square under it?
3. May the magazine snap home by itself once it's most of the way in (the controllers can't overlap)?
4. After a new magazine in an empty pistol: does the first trigger pull only send the slide home, or fire too?
5. Grab the slide with the left trigger or the left grip; overhand on its rear, or a pinch?
6. Racking with a round chambered: does it fly out and get lost, or go back to the spare ammo?
7. A slide-release on the right stick's click, or is the trigger's automatic rack enough?
