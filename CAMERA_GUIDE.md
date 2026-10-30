# Finding BF3's camera with Cheat Engine

Real 3D needs the mod to move the game camera a few centimetres left or right for each eye.
For that we need two things from BF3's memory:

1. **A stable pointer to the camera transform**, so the mod can find it every time the game runs.
2. **The instruction that writes it**, so the mod can add its eye offset right after the game does.

Only do this in the campaign. Never attach Cheat Engine to BF3 while it's connected to an online server.

## What you're looking for

Frostbite stores a camera as a `LinearTransform`: 16 floats (64 bytes) in four rows of four.

| Offset | Row         | What it looks like |
|--------|-------------|--------------------|
| +0x00  | right       | values between -1 and 1, changes when you turn |
| +0x10  | up          | values between -1 and 1, changes when you look up and down |
| +0x20  | forward     | values between -1 and 1, changes when you turn or look |
| +0x30  | translation | world position in metres: X, Y (height), Z |

The fourth float of each row is usually 0, or 1 for the translation row.

## 1. Find the camera height

1. Load a campaign level and attach Cheat Engine to `bf3.exe`.
2. New scan: **Value type: Float**, **Scan type: Unknown initial value**.
3. Walk up a slope or stairs → **Increased value**. Walk down → **Decreased value**.
   Stand still → **Unchanged value**. Crouch → **Decreased value**.
4. Repeat until a handful of addresses are left.

## 2. Check that it's the camera transform

For each remaining address, right-click → **Browse this memory region**, and set the
display type to Float (right-click in the hex view → Display Type → Float).

The height you found is **Y of the translation row**, at transform start + 0x34. So look
0x34 bytes back: 12 floats between -1 and 1 that change when you move the mouse, followed by X, Y, Z.
Pick the copy where all of this matches and the values update as you look around.

**Transform start = the height address − 0x34.** Add that start address to your cheat table.

## 3. Pointer scan for the transform start

The address changes every time the game runs, so we need a pointer chain to it:

1. Right-click the transform start address → **Pointer scan for this address**. Defaults are fine; max level 5.
2. Restart the level (or the game), find the transform again (steps 1–2), and do
   **Pointer scanner → Rescan memory** with the new address. Repeat once or twice.
3. Pick a result whose base is `"bf3.exe"+something`, ideally one with few offsets.

## 4. Put it in bf3vr.ini

`build\bf3vr.ini` is created the first time the mod runs. Copy the pointer into it. Cheat Engine shows
the base and then Offset 0, Offset 1, ... from left to right; list them in that same order:

```ini
[camera]
base=01EF2A3C
offsets=10,4C,120
```

All values are hex, without `0x`. (Those numbers are only an example.)

Start the game with the injector and look at `build\bf3vr.log`. Every second it logs:

```
Camera probe: pos (12.31, 104.55, -87.02) forward (0.71, -0.05, 0.70)
```

If `pos` follows you as you walk and `forward` changes as you turn, the pointer is right.
If it says `pointer chain did not resolve`, try another pointer scan result.

## 5. Find what writes it

Back in Cheat Engine, right-click the **X of the translation row** (transform start + 0x30) →
**Find out what writes to this address**. Move around for a second, then copy the instruction
line(s) into `bf3 codes.txt`, the same way you noted the ammo instructions:

```
bf3.exe+XXXXXX  movaps [ecx+30],xmm0
```

Send me the pointer and that instruction. The next step is to hook there and shift the camera per eye.
