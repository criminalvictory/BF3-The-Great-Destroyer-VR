# The train pistol's manual reload (The Great Destroyer): study and design

Studied 2026-10-31 from the game's own reload (recorded with `reloadrec`), the M416's manual reload in
the mod, and the pistol's ammo. Nothing is built yet. The working files (prototype modules, offline
tests, pose matrices and pictures) are in `screenshots\wf_reload\` on the development machine. They
are not in the repository, because the pictures and poses are taken from the game's own meshes and
animations.

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

## The design (its own profile; nothing copied from the M416)

| Control | What it does |
|---|---|
| X tap (right thumb) | Drops the magazine, like the M9's button behind the trigger guard. With one half-way in, drops that. |
| X held 0.6 s | Unchanged: R, the game's reload and pickups (the shotgun at the train's door). |
| Left grip at the pelvis | Takes a magazine (pouch.cpp's zone and squeeze rule). |
| Left grip held | Carries it; let go away from the gun and it falls. |
| Left hand up under the grip | Inserts it: the fingers open 12 cm under the grip (the flat base), the palm pushes it up its line. |
| Left trigger at the slide's rear | Grabs the slide (as the M416's charging handle); pull back fully and let go to rack. |
| Right trigger | Fires. Empty with the slide locked and a new magazine in: the first pull sends the slide home. |

- **Firing to empty locks the slide open**: the mod draws it 4.1 cm back, since the game's own slide
  always goes home (it still has the mod's hidden round).
- **Carry and flat base**: the magazine sits on the palm in one place the whole time, as the game puts
  it at 2.390 s. Only the fingers change: closed round it to carry (C2), open with the magazine standing
  on the palm at the gun (B). So nothing jumps in the hand at the well.
- **The slide by hand**: the palm on the slide's top, the fingers over its right side's serrations, the
  thumb on its left (fitted on the meshes; the game has no rack of its own).
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
