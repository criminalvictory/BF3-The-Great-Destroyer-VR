# Rock and a Hard Place: the weapons, how they really work, and how to bring them to VR

Research for *BF3: The Great Destroyer VR*, written 2026-10-30. Scope: Battlefield 3 campaign mission 10,
*Rock and a Hard Place* (its level file is `SP_Valley`), played as Sgt. Henry Blackburn.

How to read this:

- **What this means for us** is plain English. Everything after it is the detail.
- **(unverified)** marks anything I could not confirm from a source or from the game's own data. Nothing
  here was tried in the game (the rule for this job was not to touch the game).
- Where the facts come from, strongest first:
  1. **The game's own data.** [Venice-EBX](https://github.com/VeniceUnleashed/Venice-EBX) is a public text
     dump of BF3's data files, made by the Venice Unleashed project. It includes the campaign levels. From
     it I read the mission's own lists: Blackburn's loadout, every enemy type and its guns, the pickups, the
     weapon numbers (magazine sizes, reload times, spread), the default PC keys and the HUD. The dump is of
     the final PC release. I assumed, without checking, that it matches the user's install (bf3.exe 1.6).
  2. The [Battlefield Wiki](https://battlefield.fandom.com/wiki/Rock_and_a_Hard_Place) and
     [IMFDB](https://www.imfdb.org/wiki/Battlefield_3) for what players see in the game.
  3. US Army manuals and firearms references for how the real weapons work.
  4. Two VR mods for lessons: FrostbiteVR (MIT licence, BC2) and Overlord (GPL-3.0, so ideas only: no
     code and no text copied).

See also, in this folder: `frostbitevr.md`, `bc2vr.md`, `overlord-lessons.md`, `6dof-head.md`.

---

## What this means for us

1. **Blackburn starts with:** the M416 rifle with an ACOG scope and an M320 grenade launcher under the
   barrel, a SMAW rocket launcher, hand grenades and a knife. That's the whole kit. **There is no pistol**:
   the M9 isn't in this mission.
2. **He picks up a Stinger** (anti-aircraft missile) from a wrecked Growler near the end to shoot down the
   jet. The mission can't be finished without it.
3. **The enemies are Russian paratroopers.** The guns you can take from them are the **AN-94** rifle (most
   of them carry one) and the **RPK-74M** light machine gun (a few). The rocket soldiers carry an RPG-7 and
   an AEK-971, but the game makes them drop nothing. No enemy in this mission carries an AK-74M, SVD or PKP.
4. **Two weapons are needed to finish:** the SMAW, the only weapon that can kill the two BMP armoured
   vehicles (the game even refills it for you), and the Stinger, for the jet.
5. **The Stinger only locks on while zoomed in.** That's what the game data says: zoomed in (right mouse
   button) it locks onto heat; not zoomed, it doesn't lock at all, and it can't fire without a lock. Our
   free-hands mode never zooms, so as things stand the Stinger may never fire. The mod will have to hold the
   zoom while the Stinger is up at the eye. (Not tried in the game yet.)
6. **The same goes for accuracy.** When not zoomed, BF3 throws in random spread: about 2.5° for the rifle
   and 2° for the grenade launcher and the SMAW. Zoomed, it's 0.2° and 0°. Since we never zoom, bullets and
   grenades may land off the spot the scope shows. A quick test: single shots at a wall.
7. **BF3 starts rockets and launcher grenades from the camera (the head), not from the launcher.** That's
   probably why aiming "goes haywire" with the SMAW and the grenade launcher. The start point is a number in
   the weapon's data, so the mod could rewrite it every frame to the drawn launcher's muzzle (to test).
8. **The M320 in BF3 on PC:** it counts as a separate weapon. Key 3 selects it and key 1 goes back to the
   rifle; the switch takes about half a second. It holds 1 grenade with 3 spare and reloads itself in
   2.9 seconds.
9. **The real M320 doesn't load like the old M203.** You press a release and the barrel's back end swings
   out to the left. You pull the empty case out by hand, push a new round in and swing the barrel shut. On
   the M203 the barrel slides forward instead, and the case pops out by itself.
10. **Proposed VR M320, in the spirit of Overlord:** put the left hand on the launcher's grip and squeeze,
    and the mod switches the game to the launcher. The left trigger fires one grenade. Move the hand back to
    the handguard and the mod switches back to the rifle. The right trigger never fires the launcher, and
    the left trigger never fires the rifle. Version 1 keeps the game's own automatic reload; version 2 is a
    hand reload like the real M320.
11. **The HUD's "G×n" number is probably the hand grenades, not the launcher.** The HUD has exactly one
    grenade counter. Also, the mod currently presses G (throw grenade) when the right grip is squeezed, so
    squeezing may be what used them up (to check).
12. **Every gun gets its own profile** (as agreed). The AN-94 and the RPK are AK-family guns: the magazine
    rocks in, a paddle releases it, the cocking handle is on the right, and there's no bolt hold-open (so you
    always pull the handle after an empty magazine). The AN-94 also has odd controls: a push-button safety
    inside the trigger guard and a separate mode switch on the left.

---

## 1. Which weapons are in the mission, and how sure we are

### 1.1 Blackburn's own loadout (high confidence: game data, wiki and IMFDB agree)

The level's player spawn (`Levels/SP_Valley/GlobalLogic`) uses the template
`SpHumanSoldierSpawnTemplateDataValley`, which points to the soldier setup
[`Characters/Soldiers/Customizations/UsHumanSpSoldier_Valley`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Characters/Soldiers/Customizations/UsHumanSpSoldier_Valley.txt).
Its weapon list:

| Game slot | Weapon (data name) | Attachments | Selected with (PC default) |
|---|---|---|---|
| 0 (primary) | `U_M416` | ACOG, 40 mm HE launcher | **1** |
| 1 (secondary) | *empty: no pistol* | (the Stinger goes here when picked up) | 2 |
| 2 (gadget) | `U_M416_M320_HE`: the M320 under the M416 | ACOG, 40 mm HE | **3** |
| 5 (gadget 2) | `U_SMAW` | none | **4** |
| 6 (grenade) | `U_M67` | none | **G** (quick throw) or **5** (hold) |
| 7 (melee) | `U_Knife` | none | **F** (quick) or **6** (hold) |

The game starts you on slot 0. The wiki's mission page lists the same weapons (M416 with an underbarrel
M320, SMAW, Stinger) ([Battlefield Wiki](https://battlefield.fandom.com/wiki/Rock_and_a_Hard_Place)). Its
M416 page says the M416 is Blackburn's starting weapon here with an ACOG and the M320
([wiki](https://battlefield.fandom.com/wiki/M416/Battlefield_3)), and its SMAW page says you carry the SMAW
from the start ([wiki](https://battlefield.fandom.com/wiki/SMAW)). IMFDB has a picture of Blackburn running
with the HK416 and M320 under the mission's An-22 flyover ([IMFDB](https://www.imfdb.org/wiki/Battlefield_3)).

**No M9.** Slot 1 is empty in the data. IMFDB says the player has a sidearm only in *Semper Fidelis* and
*The Great Destroyer* ([IMFDB](https://www.imfdb.org/wiki/Battlefield_3)). The M9 is in the level's asset
list only because Campo and a jet-section extra carry one (section 1.3).

### 1.2 Picked up during the mission

| Weapon | Where | How sure | Notes from the data |
|---|---|---|---|
| **FIM-92 Stinger** (this level's own version, `U_FIM92_SP_Valley`) | In the Growler wreck during the jet run ("Grab Stinger and take out Su-25") | High | A pickup in [`Valley_JetLogic`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Levels/SP_Valley/Valley_JetLogic.txt): it goes into **slot 1 (key 2)** and is selected automatically, with 3 to 5 missiles. An invisible ammo crate there (10 m radius, refills every 10 s) explains IMFDB's "infinite reloads" |
| **AN-94** | Dropped by Russian riflemen, everywhere | High | IMFDB shows Blackburn holding one with a PKS-07 scope in this mission |
| **RPK-74M** | Dropped by Russian machine gunners (forest and the two valley fields) | High | Always with a bipod; sometimes with a scope or foregrip |
| M16A4 / M4A1 / M240B | From dead Marines? | **Low (unverified)** | The Marines' setups don't block death drops, but no source mentions picking these up |

### 1.3 Enemy weapons (from the level's spawn lists)

I counted every soldier spawn template in the level's files (forest, briefing hill, run down the hill, the
valley fields, artillery camp, tank battle, jet run, plus the respawn prefabs):

| Enemy type (template, spawners) | Carries | Drops a pickup? |
|---|---|---|
| Russian rifleman, `Template_RU_Assault` (32 spawn entries, 5 of them respawn prefabs reused many times) | **AN-94**, randomly bare or with a Kobra, PK-A, PK-AS, PKS-07 or PSO-1 sight, some with a foregrip; M67 grenades; knife | **Yes** |
| Russian machine gunner, `Template_RU_LMG` (7: forest 2, field 1 2, field 2 3) | **RPK-74M** with bipod, bare or with a PSO-1, PK-AS, PK-A or Kobra sight, or a foregrip; grenades; knife | **Yes** |
| Russian rocket soldier, `Template_RU_RPG` (4: forest 2, artillery camp 1, field 2 1) | AEK-971 (bare), RPG-7 (AI version), knife | **No**: `DisableDeathPickup True` |
| Vehicle crews, `Template_Enemy_TankDriver` | Knife only | No gun |

Sources: the soldier setups
[`RuSpSoldier`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Characters/Soldiers/Customizations/RuSpSoldier.txt),
[`RuSpSoldier_LMG`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Characters/Soldiers/Customizations/RuSpSoldier_LMG.txt)
and
[`RuSpSoldier_RPG`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Characters/Soldiers/Customizations/RuSpSoldier_RPG.txt).
The wiki's mission page lists the enemy weapons as AN-94, RPK-74M and RPG-7V2, which matches. It leaves
out the AEK-971, which makes sense: nobody ever sees one dropped.

Two odd ones, probably scripted extras rather than enemies to fight (unverified):

- `Template_SP007_LongRange_Enemy_Soldier` is used once in the forest as an "actor" on the player's team.
  Its setup is the PLR one, with a random AK-74M, KH2002 or G3A3.
- `Template_SP_Jet_AA` is used once in the jet section on the enemy team. Its setup is the carrier crew
  one: M16A4, M9 and grenades. Its purpose is unknown; perhaps a target for the jet.

The friendly Marines carry: Campo an M4A1, an M9 and grenades; Cole an M4A1; Montes an M240B; the other
Marines M16A4s.

### 1.4 Not in this mission (high confidence)

- **M9**: not in Blackburn's kit (above).
- **SVD, PKP Pecheneg**: they appear nowhere in the level's asset list or in any soldier setup it uses.
  IMFDB places the PKP in *Operation Guillotine*.
- **AK-74M**: only through the one scripted extra above. Not a normal pickup.

### 1.5 Where each weapon matters (mission flow)

From the wiki's synopsis and the level's file names
([wiki](https://battlefield.fandom.com/wiki/Rock_and_a_Hard_Place)):

1. **The forest:** you ride in the back of a Growler, get ambushed, and fight riflemen, machine gunners and
   rocket soldiers. The knife counts here too: knifing a Russian earns the console achievement "What the
   hell *are* you?".
2. **The hill briefing and the paradrop:** An-22s drop paratroopers, BM-21 rockets and D-30 howitzers
   (scenery, not usable).
3. **Down the hill and the valley fields:** riflemen and machine gunners. There's an ammo crate on the run
   down the hill (`spammocrate` in
   [`RunDownHill_Logic`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Levels/SP_Valley/RunDownHill_Logic.txt)).
4. **The tank battle:** two BMP-2Ms. A script ([`UseRPGLogic`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Levels/SP_Valley/Prefabs/UseRPGLogic.txt))
   watches the SMAW. It shows a tooltip to switch to it (the message is still named "AT4", on the select-
   slot-5 key) and another to resupply at an ammo depot. It can also set the SMAW's ammo (`AmmoToSet 75`).
   The wiki adds that it refills the SMAW while a tank is alive and no crate is near.
5. **The jet run:** you run from cover to cover under an Su-25, pick up the Stinger from the Growler wreck,
   lock on and shoot the jet down. Campo and Matkovic die.

### 1.6 How picking up a gun works in BF3 (from the data)

Dead AI drop
[`AI/Pickups/DefaultAIDynamicPickup`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/AI/Pickups/DefaultAIDynamicPickup.txt):

- You take it with **E** from up to **2.5 m** away. It stays on the ground for **180 s** and shows on the
  minimap.
- Ammo comes randomized: **16 to 33 % of a magazine loaded and 33 to 66 % of the spare rounds**. Walking
  over the same kind of gun tops your ammo up.
- The gun goes into slot 0, and slot 2 is "linked" to slot 0. So taking an AN-94 or RPK most likely throws
  away the M416 **and the M320 with it**, and the Russians have nothing in slot 2 (unverified, but it's what
  the data describes). The dropped M416 should stay on the ground to take back, since the data says the
  weapon isn't removed when dropped (unverified).

---

## 2. Rules every BF3 weapon follows

### 2.1 Keys (PC defaults, from the game's input data)

From [`Input/VeniceSoldierInputConcepts`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Input/VeniceSoldierInputConcepts.txt),
[`VeniceSoldierInputMapping`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Input/VeniceSoldierInputMapping.txt)
and the switching map
[`SoldierWeaponSwitching`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Input/SoldierWeaponSwitching.txt).
These are the defaults: if the user has rebound keys, the mod's key presses must follow those (the mod
sends scan codes).

| Key | Game action | In this mission |
|---|---|---|
| 1 | select slot 0 | M416 |
| 2 | select slot 1 | nothing, later the Stinger |
| 3 | "static gadget" (slot 2 or 3); also select slot 4 | the M320. Pressing 3 again stays on the M320 |
| 4 | select slot 5 | SMAW |
| 5 | select slot 6 | hand grenade held ready (pin pulled; throw on release) |
| 6 | select the knife | knife in hand |
| G | quick-throw a grenade, then switch back | |
| F | quick knife attack, then switch back | takedowns |
| V | fire mode | |
| R | reload | |
| E | interact or pick up | |
| mouse wheel | next or previous weapon | |
| left / right mouse button | fire / zoom (ADS) | |
| no PC key by default | "switch to grenade launcher" (toggles rifle and launcher); "switch to primary" | consoles use these |

The game also waits **0.5 s** before it lets you switch to a gadget again (`SwitchToGadgetCooldownTime`).

### 2.2 Weapon numbers (from each weapon's data file)

"Holds" is BF3's own magazine size, including the round in the chamber. BF3 counts the chambered round (30
+ 1): IMFDB notes that BF3, unlike earlier DICE games, tracks the extra chambered round and animates
chambering on empty reloads ([IMFDB](https://www.imfdb.org/wiki/Battlefield_3)). The spare figure follows
from `NumberOfMagazines`, which counts the loaded magazine too: the M416's 5 gives 124 + 31, as the wiki
says.

| Weapon | Holds | Spare at start | Fire modes (V) | Reload: partial / empty | Time to bring up | Shot starts from | Start speed |
|---|---|---|---|---|---|---|---|
| M416 | 31 (30 + 1) | 124 | auto, single | 1.75 s / 2.5 s | 0.67 s | the gun | 600 m/s |
| M320 under the M416 | 1 | 3 | single | 2.9 s, starts by itself | 0.55 s | **the camera**, offset (−0.10, −0.30, +0.86) m | 45 m/s forward, 1.5 m/s up |
| SMAW | 1 | 3 (the wiki says 4) | single | 5.8 s | 1.0 s (a 0.2 s setting also exists) | **the camera**, offset (−0.30, −0.10, +2.0) m | 40 m/s, speeding up |
| Stinger, this level's version | 1 | the pickup gives 3 to 5 | lock, then fire | 4.3 s | 1.9 s | **the camera**, offset (−0.20, −0.065, +0.35) m | 16 m/s, speeding up |
| M67 grenade | 1 at a time; the data counts 1, IMFDB says 3 in the campaign | – | hold, then release | 1.0 s until the next is ready | 0.2 s | **the camera**; drawn leaving the hand | 18 m/s forward, 3 m/s up |
| AN-94 | 31 | 124 | 2-round burst (default), auto, single | 2.45 s / 3.3 s | – | the gun | 600 m/s |
| RPK-74M | 46 (45 + 1) | 138 | auto, single | 3.0 s / 4.0 s | – | the gun | 600 m/s |
| M9 (not in this mission) | 16 (15 + 1) | 48 | single | 1.35 s / 1.6 s | – | the gun | – |

The files: [`Weapons/M416/M416`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/M416/M416.txt)
(it also holds the underslung launcher's own firing settings, with the 0.55 s switch time),
[`Weapons/Gadgets/M320/40mm_HE_Firing`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/Gadgets/M320/40mm_HE_Firing.txt),
[`Weapons/SMAW/SMAW`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/SMAW/SMAW.txt),
[`Weapons/FIM92A/FIM92A_SP_Valley`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/FIM92A/FIM92A_SP_Valley.txt),
[`Weapons/M67/M67`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/M67/M67.txt),
[`Weapons/AN94/AN94`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/AN94/AN94.txt),
[`Weapons/RPK/RPK`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/RPK/RPK.txt) and
[`Weapons/M9/M9`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/M9/M9.txt). The
wiki's figures agree: M416 124 + 31, 1.75 s and 2.5 s; RPK 138 + 46, 3 s and 4 s; SMAW 5.8 s; the
launcher's grenade at 45 m/s.

The reading of the fire-mode lists is mine. Each gun has a default mode and a list of extra modes, and the
mode numbers aren't labelled in the dump, so the reading is inferred. It agrees with the wiki (M416: auto
and single) and IMFDB (the AN-94 starts in burst).

Each weapon file has a second set of firing numbers with double-size magazines (62 for the M416 and the
AN-94, 92 for the RPK). That second set is most likely the AI's (unverified). It could matter if a dropped
gun keeps the AI's settings; nobody has reported that.

The wiki says the campaign raises the most ammo you can carry (M416: up to 372 + 31 in the campaign
against 217 + 31 online; M320: 9 + 1). So ammo crates can fill you past the starting amount.

### 2.3 Where shots start, and the spread when not zoomed (important for VR)

**The start point.** Rifle bullets leave from the gun (`ForceSpawnToCamera False`). Grenades, the
launcher's grenade, the SMAW rocket and the Stinger missile leave from **the camera plus a fixed offset**,
measured in the camera's frame (`ForceSpawnToCamera True`). For the M67 the game draws the grenade leaving
the hand, but its flight starts from the camera.

In VR the camera is the head, while the launcher is in the hand. So a rocket starts in front of your face
and flies the way the game's look direction points. The mod writes that look direction from the gun
(`aim.cpp`), so the direction should be right but the start point is wrong. When head and tube point
different ways this would look like "aiming haywire" (likely cause; unverified).

The offset is plain data (`ShotConfigData.InitialPosition`, and `InitialDirection`), not code. So the mod
could rewrite it every frame to "where the drawn muzzle is, seen from the camera". The launched object
would then really leave the drawn barrel. To find the live data: the launcher's settings start with the
floats −0.1, −0.3, 0.86, then 0, 0, 1, then 0, 1.5, 45. (unverified: Frostbite usually stores a 3-number
vector padded to 16 bytes.)

**Spread.** BF3 adds random spread that depends on whether you're zoomed. These are the standing-still
values; moving and firing add more:

| Weapon | Not zoomed | Zoomed |
|---|---|---|
| M416 | 2.5° (grows 0.1° a shot, up to 7°) | 0.2° (up to 3°) |
| M320, SMAW, Stinger (all three use `40mm_GunSway`) | 2.0° (grows 1.8° a shot, up to 5.5°) | 0° |

Sources: the M416's own sway settings in `M416.txt`, and
[`40mm_GunSway`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/Gadgets/M320/40mm_GunSway.txt).
The wiki gives the same M416 figures (2.5 not zoomed, 0.2 zoomed). With free hands the left trigger works
the charging handle instead of zooming (`hands.h`), so BF3 probably treats every shot as a hip shot
(unverified in the game).

There are two ways to fix it:

- **(a) Hold the zoom** (right mouse button) while the gun is up at the eye. But zooming also changes the
  game's FOV and camera, which the mod already fights.
- **(b) Copy the zoomed spread over the not-zoomed spread** in the live data of the gun in hand. This one
  is less visible (unverified).

The Stinger is the exception: it needs the real zoom to lock at all (section 3.4).

### 2.4 The HUD's ammo box

The single-player HUD screen
([`UI/Flow/Screen/HudSPScreen`](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/UI/Flow/Screen/HudSPScreen.txt))
feeds its bottom-right ammo box from the weapon with these values: loaded rounds, total rounds, **one
grenade count**, low ammo, show ammo, fire mode, the list of fire modes, and the reload button (plus a few
general ones such as visibility). There's only one grenade count,
so the "G×n" the user saw is most likely the **hand grenades**. The launcher's own rounds should show in
the main loaded and total numbers while it's selected (unverified). To check: throw one grenade (G) and
see whether the number drops, then press 3 and read the main counter.

---

## 3. Weapon by weapon: the real gun, BF3's version, and what VR needs

### 3.1 M416 (Heckler & Koch HK416) with ACOG: Blackburn's rifle

**The real gun.** The HK416 is an AR-15-pattern carbine built from the M4. Its main difference is a short-
stroke gas piston taken from HK's G36, which keeps the gas out of the receiver
([IMFDB](https://www.imfdb.org/wiki/HK416), [Wikipedia](https://en.wikipedia.org/wiki/Heckler_%26_Koch_HK416)).
It takes STANAG magazines, and BF3's model is the 10.4-inch-barrel version. Because it's an AR, the manual
of arms is the M4's (US Army [TC 3-22.9](https://www.armywriter.com/board/references/TC3-22x9.pdf)):

- **Hold:** the firing hand holds the pistol grip high in the web between thumb and index finger, with the
  trigger finger along the receiver until firing and the thumb on the selector. The support hand goes on
  the handguard, as far forward as is comfortable (TC 3-22.9).
- **Magazine:** pushed straight up into the magazine well until the catch clicks. It's released by the
  push-button magazine catch on the right side, which the firing hand's index finger presses (TC 3-22.9).
  The HK416 A5 adds a release on the other side too
  ([Wikipedia](https://en.wikipedia.org/wiki/Heckler_%26_Koch_HK416)); BF3's older model has the plain AR
  button (unverified on the model).
- **Bolt hold-open:** the bolt catch on the left side locks the bolt open by itself when the last round is
  fired, or when the user sets it (TC 3-22.9). To close the bolt, press the catch's upper part (the "slap"
  the mod already does), or pull the charging handle a little and let go.
- **Charging handle:** a T-handle at the rear of the upper receiver, with its latch on the left. It doesn't
  move when the gun fires. You pull it fully back and let it snap forward without riding it.
- **Safety and selector:** on the left above the grip, worked by the right thumb: SAFE, SEMI, AUTO. The A5
  has ambidextrous fire controls ([Wikipedia](https://en.wikipedia.org/wiki/Heckler_%26_Koch_HK416)).
- **Dust cover:** a spring-loaded flap over the ejection port that keeps dirt out (TC 3-22.9). It pops open
  when the bolt carrier moves (the first shot, or pulling the handle) and stays open until you close it by
  hand.
- **Forward assist:** a plunger behind the ejection port that pushes the bolt home (TC 3-22.9). HK sells one
  as an HK416 part ([Midwest Gun Works](https://www.midwestgunworks.com/page/mgwi/prod/203599)); whether
  BF3's model has one is unverified, and it plays no part in the game.

**BF3's version** ([IMFDB](https://www.imfdb.org/wiki/Battlefield_3),
[wiki](https://battlefield.fandom.com/wiki/M416/Battlefield_3)):

- Modes: auto and single. A December 2011 patch removed burst on PC.
- The partial (tactical) reload is a plain magazine swap with the rifle still held pointing forward:
  1.75 s.
- The empty reload tilts the rifle more, swaps the magazine and ends by **smacking the bolt catch**: 2.5 s.
  IMFDB notes, of the M4 in the early trailers, that DICE first showed racking the charging handle on empty
  reloads and changed it to the bolt catch before release.
- The wiki says the M416 shares its reload animations with the M27 IAR and the M417.
- Model quirks (IMFDB): the dust cover is drawn as a thin flap left half open; the back of the charging
  handle is modelled solid.
- The gun's first-person view uses its own 55° FOV (`RenderFov 55`) and 20° when zoomed. That matches what
  the mod found for the gun's projection.

**VR profile (M416).** Most of this is built (charging handle, bolt release slap, magazine eject). For the
per-gun profile:

- Magazine: straight in and out, with the release button on the right side.
- Bolt: locks open on empty; close it with the slap or a slingshot pull.
- Dust cover: swings open on the first shot or a handle pull; close it with a finger.
- Selector: a thumb flick on the left side sends V (optional; BF3 has no SAFE, so the mod could add one).
- **"The ammo HUD reads one high":** BF3's own magazine size is 31 (30 + 1 in the chamber). After a partial
  reload the game really holds 31. And `ammo.cpp` deliberately keeps one hidden round in the game's count.
  It might be cleaner to treat the game's extra round as the chambered round, which is how the real rifle
  works (suggestion; unverified).
- The M320 belongs to this profile as a module (section 4).

### 3.2 M320 under the M416: the real thing, and BF3's version

**The real M320** (Heckler & Koch's grenade launcher module, the replacement for the M203). Sources: the US
Army's training deck for the M320 and M320A1
([Slideshare copy](https://www.slideshare.net/slideshow/m320m320a1-grenade-launcher-pmi/69634635)),
[Wikipedia](https://en.wikipedia.org/wiki/M320_Grenade_Launcher_Module),
[army.mil](https://www.army.mil/article/180665/fort_jackson_soldiers_train_on_newest_grenade_launcher),
[Modern Firearms (AG36 family)](https://modernfirearms.net/?p=5443),
[IMFDB (AG series)](https://www.imfdb.org/wiki/Heckler_%26_Koch_AG_grenade_launcher_series) and a
civilian review at [TTAG](https://thetruthaboutguns.com/gun-review-hk-m320-grenade-launcher).

- **Loading: yes, the barrel swings sideways, to the left.** You press the barrel release and the barrel
  swings out to the left side of the receiver under spring pressure (Army deck). Modern Firearms describes
  the same family as unlocking and swinging its breech to the left. IMFDB notes a later HK model that can
  open to either side, which means the standard one opens to one side only.
  - The Army deck's wording varies: one slide says "to the left", another "from right to left". Both mean
    the breech ends up on the left.
  - The barrel turns on an axle with a spring (Army deck), so the pivot is at the front and the breech end
    swings out. That pivot position is my inference.
- **The M203 is different:** you press its barrel latch (on the left side of the barrel) and **slide the
  barrel forward**. The empty case or round is pulled clear and **thrown out automatically**. Sliding the
  barrel forward and back also **cocks** the launcher, and closing it locks with a click
  ([FM 3-22.31, chapter 2](https://www.globalsecurity.org/military/library/policy/army/fm/3-22-31/chap2.htm)).
  Army users found the M320 easier: one press and the breech swings out the side (army.mil).
- **The M320's empty case doesn't jump out.** The deck's unloading steps remove the round or case by hand,
  gripping its rim through cut-outs in the barrel and pulling it back. Then the barrel is closed until it
  locks.
- **Loading steps** (Army deck): safety to S; press the barrel release and let the barrel swing out; push
  the round fully into the chamber; close the barrel and make sure it's locked.
- **Trigger:** double-action only: a long, heavy pull (11.25 to 15.75 lb) and no cocking. After a misfire
  you just pull again (Wikipedia, Army deck, Modern Firearms).
- **Safety:** an ambidextrous lever with two positions, S and F, that clicks into each one (Army deck).
- **Barrel release:** a lever on the left that you push up (Army deck). The TTAG review calls it a trigger-
  like button just in front of the trigger, inside the guard. Either way it sits by the front of the
  trigger guard, where the left hand can reach it.
- **Holding it:** the soldier fires the M320 by its own pistol grip; the M203 was held by the rifle's
  magazine (army.mil). Army doctrine on a rifle: fold the launcher's grip away, put the firing hand on the
  M320's pistol grip and the other hand on the rifle's pistol grip (Army deck). Games, and our plan, keep
  the right hand on the rifle and put the left hand on the launcher's grip.
- **Sight:** a fold-up leaf sight on the side of the launcher, left or right, marked 50 to 350 m in 50 m
  steps (Wikipedia; the Army deck says it can go on either side).
- **Mounting:** it fits the rail under the rifle's handguard. HK made versions for the HK416
  ([IMFDB](https://www.imfdb.org/wiki/Heckler_%26_Koch_AG_grenade_launcher_series)).

**BF3's M320 under the M416** (game data; [IMFDB](https://www.imfdb.org/wiki/Battlefield_3); [wiki](https://battlefield.fandom.com/wiki/M320)):

- **It's a separate weapon in slot 2.** It uses the M416's model and the 40 mm HE firing settings. On PC,
  **key 3** switches to it, **key 1** goes back, and the mouse wheel cycles. The game's own toggle action
  ("switch to grenade launcher") has **no PC key** by default. Pressing 3 again while on the launcher stays
  on the launcher.
- **Switching** takes **0.55 s** to the launcher; back to the rifle is 0.5 to 0.67 s (two switch times are
  stored, 0.5 and 0.67). The wiki says the switch is much faster when the launcher is mounted than when it's
  carried on its own.
- **Firing:** the left mouse button, single shots. BF3's underbarrel launchers have **no sight to fold up**;
  zooming just zooms in (IMFDB; the separate standalone M320 does have its leaf sight). The 40 mm round
  leaves the camera at 45 m/s; the wiki says it has a small kill radius (0.84 m).
- **Ammo:** 1 loaded, 3 spare. The wiki says up to 9 + 1 in the campaign.
- **Reload:** starts by itself after the shot if there are spares, once the fire button is let go. It takes
  2.9 s. IMFDB's picture shows Blackburn pulling the "spent" round out (the game has one round model, so it
  looks unfired) before loading a new one. Whether the barrel visibly swings out in BF3's animation is
  unverified.
- **Model:** a separate skinned mesh attached to the M416 (`m320_underslung_1p_Mesh`, with three materials,
  one of them the round's). So it's its own draw call, as the mod's notes already suspected.
- **Sound:** the underslung reload sound is one 3.3 s recording (`M320_Underslung_Reload_Wave`). The mod
  could record it once (as it did for the rifle) and cut it into open, case out, round in and close.
- **Its GP-30 cousin:** IMFDB says an M320 fitted to a Russian rifle becomes a GP-30, drawn with caseless
  rounds. Not used in this mission.

### 3.3 SMAW (Mk 153 Shoulder-launched Multipurpose Assault Weapon)

**The real one** ([IMFDB](https://www.imfdb.org/wiki/Mk_153_SMAW),
[Wikipedia](https://en.wikipedia.org/wiki/Shoulder-launched_Multipurpose_Assault_Weapon),
[FAS](https://man.fas.org/dod-101/sys/land/smaw.htm), [Modern Firearms](https://modernfirearms.net/?p=5456)):

- **Build:** a reusable launcher (tube, sights, two grips with the trigger), plus factory-sealed rockets,
  each in its own container.
- **Loading:** the rocket's container is **pushed onto the rear of the launcher** and clips in place. After
  firing, the empty container is taken off and thrown away, and a new one goes on.
- **Spotting rifle:** a 9 mm one on the right side of the tube, matched to the rocket's flight so it
  raises the chance of a first-round hit (FAS). Its 6-round magazine is stored in each rocket's end cap.
- **Controls:** one trigger does both. It fires the spotting rifle mechanically and the rocket through a
  magneto. A switch just above the pistol grip chooses which one fires. There's a manual safety.
- **Sights:** open iron sights, plus mounts for a day optic and night sights.
- **Hold:** on the shoulder, with the firing hand on the rear pistol grip and the support hand on the front
  grip.

**BF3's version:**

- 1 loaded, 3 spare in the data; the wiki says 4 + 1.
- **Key 4.** 5.8 s reload, starting by itself. It uses the launchers' spread (2° not zoomed, 0° zoomed), and
  its zoomed view is 25°.
- The rocket starts **2 m in front of the camera** at 40 m/s and speeds up (the wiki says it reaches full
  speed after 6 s).
- No working spotting rifle.
- The level's script refills it for the two BMPs, and ammo crates refill it too.
- What BF3's reload animation shows (a rocket pushed into the back, or a whole container swapped) is
  unverified: watch it once. The game's own description says the rockets load one at a time at the back.

**VR notes:**

- Carry it slung on the back. Reaching over the right shoulder and gripping sends 4 (switch it in);
  letting go of it sends 1.
- Hold it on the right shoulder: the right hand on the rear grip, the left on the front grip. The right
  trigger fires.
- Hold the zoom, or copy the zoomed spread (2.3), so the rocket goes where the tube points. Move the
  rocket's start point to the tube's front (2.3).
- Version 1 keeps the game's own reload. Version 2: pull the empty container off the back, take a new one
  from behind the shoulder, push it on until it clicks.

### 3.4 FIM-92 Stinger (picked up)

**The real one** ([IMFDB](https://www.imfdb.org/wiki/FIM-92_Stinger), [FAS](https://man.fas.org/dod-101/sys/land/stinger.htm),
[Wikipedia](https://en.wikipedia.org/wiki/FIM-92_Stinger)):

- **Two main parts:** a throw-away launch tube holding the missile, with the sight on it, and a reusable
  gripstock with the trigger, the controls and a fold-out IFF antenna (friend-or-foe radio).
- **The battery coolant unit (BCU)** twists into the bottom of the gripstock with a quarter turn. Once
  started it gives about 45 s of power, plus gas to cool the missile's seeker.
- **Firing:** the gunner powers it up, hears the seeker's tone when it has the target, and fires. It's
  fire-and-forget. (The exact switch order is from general knowledge; unverified.)
- **Reload:** in the field it's rarely "reloaded". The gripstock slides onto a new tube and latches.
- BF3's model is a Stinger *training set* (it even reads "Tracking Rainer") (IMFDB).

**BF3's version (this level's own copy):**

- Picked up into **slot 1 (key 2)** and selected automatically, with 3 to 5 missiles. A hidden ammo crate
  near the wreck keeps it topped up.
- **Lock-on:** the lock settings say no lock when not zoomed and **heat lock when zoomed**. It takes
  **2.0 s** with the jet within **5°**, and the Stinger fires only once locked (`FireOnlyWhenLockedOn True`).
- Reload 4.3 s, draw 1.9 s, zoomed view 15°. The missile starts from the camera.

**VR notes:**

- **This one is a must for the alpha:** the mod has to send the zoom (right mouse) while the Stinger is up
  to the eye. Otherwise it may never lock (unverified in the game, but clear in the data).
- Hold: on the right shoulder, the right hand on the gripstock, the left under the front of the tube. The
  look direction must follow the tube, which `aim.cpp` already does for guns. The user has to keep the jet
  inside 5° for 2 s.
- The game makes its own lock tone.
- Reload: version 1 is the game's own. A hand reload isn't worth it here (it's a short scripted scene).
- Overlord's choice for its Stinger fits too: keep the game's own lock workflow and let raising the launcher
  to the eye request zoom (ideas only, from Overlord's
  [launcher notes](https://github.com/Aeka0/Overlord/blob/main/docs/vr-launchers.md)).

### 3.5 M67 hand grenade

**The real one** ([Wikipedia](https://en.wikipedia.org/wiki/M67_grenade), [IMFDB](https://www.imfdb.org/wiki/M67_Hand_Grenade)):

- Held with the spoon (the safety lever) pressed against the body.
- The safety clip (the "jungle clip") comes off, then the pin is pulled by its ring, usually with the other
  hand.
- When it's thrown the spoon flies off, and it goes off **4 to 5 seconds** later. It weighs about 400 g,
  and a soldier can throw it about 35 m.

**BF3's version** (IMFDB; game data):

- IMFDB says you carry three in the campaign; the grenade's own file says 1 (the online default). The
  campaign's bigger carrying limits may explain the difference: check the HUD number at the mission start.
- **G** throws one at once and switches back. **5** puts one in your hand with the pin pulled, and you
  throw on release; holding it doesn't start the fuse (IMFDB). In the data the hold can last practically
  forever.
- It flies from the camera at 18 m/s forward and 3 m/s up, at a **fixed** speed. Your throw doesn't change
  it.

**VR notes:**

- **Version 1:** grab a grenade from the chest. The mod sends 5 and holds the left mouse button (the
  game's "hold" state). Let go of the grip and the mod releases the button, which throws it. The pin pull
  with the other hand can be shown for looks only.
- **Version 2:** set the grenade's start speed (`InitialSpeed`, live data) from the hand's real speed at
  release, so a lob and a hard throw differ (unverified).
- Today the right grip sends G (`input.cpp`). That probably needs to change once grenades come off the
  chest.

### 3.6 Knife

BF3: **F** is a quick knife attack, which near an enemy plays the canned takedown. **6** holds the knife.
VR: a "stab" gesture or a button press sends F. The takedown runs like the other locked scenes (see the
project's memory note on cutscenes and QTEs: body locked to the game camera, prompts on the right grip).

### 3.7 AN-94 (picked up from Russian riflemen)

**The real one** ([Firearms News review](https://www.firearmsnews.com/editorial/russian-545x39mm-an94-nikonov-review/383282),
[Modern Firearms](https://modernfirearms.net/en/assault-rifles/russia-assault-rifles/an-94-abakan-eng/),
[Wikipedia](https://en.wikipedia.org/wiki/AN-94)):

- **Safety:** a **cross-bolt button at the front of the trigger guard**. You push it to the left for fire.
  It's not the usual AK lever.
- **Fire selector:** a **separate small lever on the left rear of the receiver**, above the grip, worked
  by the right thumb. Forward is single, middle is the 2-round burst, rear is full auto (Firearms News).
  Modern Firearms orders them single, 2, auto.
- **Charging handle:** on the **right side, fixed to the bolt carrier**. It moves with every shot.
- **Magazine:** takes AK-74 magazines, but the magazine **leans to the right**, because the recoil spring
  sits on the left. It's **rocked in** while leaning right, and released by the same paddle as the AK-74M.
- **No bolt hold-open.** **The stock folds to the right.**
- The barrel and receiver recoil together inside the plastic housing. The 2-round burst (about 1,800 rpm)
  is felt as one kick after the second round.

**BF3's version:**

- 30 + 1 rounds. **2-round burst by default**, then auto and single (V).
- Reloads 2.45 s partial, 3.3 s empty. IMFDB notes that in the campaign the burst fires at the same rate
  as full auto. The data's burst rate (1,200 rpm) is for the player's version; an AI version has its own.
- IMFDB:
  - BF3 uses the AK **speed reload**: the new magazine knocks the old one out while the thumb pushes the
    paddle.
  - The magazine is drawn **straight down**, not leaning.
  - Empty reloads end by letting go of the charging handle.
  - It has green magazines.
- Enemy rifles come bare, with a foregrip, or with a side-mounted sight (Kobra, PK-A, PK-AS, PKS-07,
  PSO-1). Each sight is another rigid mesh for the mod. IMFDB also notes that BF3 holds guns lower with
  optics fitted.

**VR profile (AN-94):**

- **Magazine:** rock it in (front hook first, then swing the back up until it clicks), tilted as BF3 draws
  it (straight). Release it with the paddle behind the magazine, pushed by the left thumb.
- **After an empty magazine always rack the handle**, reaching to the right side, because there's no hold-
  open.
- **Modes:** a thumb flick on the left rear selector sends V.
- **Safety:** optional, the button in the trigger guard.
- **No dust-cover animation:** the AK cover is fixed.

### 3.8 RPK-74M (picked up from Russian machine gunners)

**The real one** ([Wikipedia RPK-74](https://en.wikipedia.org/wiki/RPK-74), [Wikipedia AK-47](https://en.wikipedia.org/wiki/AK-47),
[IMFDB](https://www.imfdb.org/wiki/Battlefield_3)):

- It's an AK-74 with a longer, heavier barrel, a **45-round magazine** (it takes AK-74 magazines), a
  **folding bipod** and, on the -74M, a side-folding polymer stock and a rail bracket for optics.
- **The AK selector is a large lever on the right side:** up is safe, middle is full auto, down is single.
  On safe it also covers the charging handle's slot and stops it coming fully back.
- **Charging handle:** on the right, fixed to the bolt carrier, moving with each shot.
- **Magazine:** rocked in, released by the paddle (common AK knowledge, as in the user's own AK
  description in the project's memory note on per-gun mechanics).
- **No hold-open:** only some old Yugoslav magazines hold the bolt open when empty.
- **Bipod:** on the real gun it's fixed just behind the front sight and folds back. BF3 instead fits a
  Harris-style bipod that's shorter than the magazine (IMFDB).

**BF3's version:**

- 45 + 1 rounds, auto and single, reloads 3.0 s and 4.0 s.
- It's named RPK-74M, but the model has a 7.62 mm-style 40-round magazine and no flash hider (IMFDB).
- IMFDB's pictures: pulling the long magazine out, the rounds visible in the new magazine, and the empty
  reload **pulling the charging handle from underneath** with the left hand.
- Enemy guns always have the bipod, and sometimes a sight or a foregrip.

**VR profile (RPK-74M):** the same AK-family rules as the AN-94: rock in, paddle out, always rack after
empty. Its own differences:

- A vertical, longer magazine: the hand target sits lower, and the magazine is heavier.
- The selector is on the right side, so it's worked by the trigger finger or the left hand.
- The handguard is longer.
- The bipod is ignored in version 1. BF3's rested shooting would need research.

### 3.9 RPG-7 and AEK-971 (enemy only)

Carried by the 4 rocket soldiers, who drop nothing (`DisableDeathPickup True`). In this mission they only
matter as incoming fire. IMFDB notes that BF3's RPG-7 is the older model, not the V2 its name says.

### 3.10 M16A4 (maybe, from fallen Marines; unverified)

The ordinary Marines carry M16A4s, and their setups don't switch off death drops. Whether BF3 lets you pick
up a fallen Marine's rifle isn't documented anywhere I found. If it does, it's another AR-family profile:
close to the M416, but with the M16's own controls (BF3's M16A4 has a SAFE and FIRE selector and fires
3-round bursts online, per IMFDB).

### 3.11 M9 (not in this mission; for *Semper Fidelis* and *The Great Destroyer*)

**The real one** ([Wikipedia](https://en.wikipedia.org/wiki/Beretta_M9)):

- Single and double action, with an ambidextrous safety (it also lowers the hammer) on the slide.
- The magazine release button can be swapped to either side and sits behind the trigger guard. The
  15-round magazine goes straight up into the grip.
- The slide locks back after the last round. It's released by the slide stop on the left of the frame, or
  by pulling the slide back over the top and letting go. The slide is open-topped. (These last points are
  from common knowledge of the 92FS; Wikipedia doesn't cover them, so they're unverified.)
- Hold: the support hand cups the firing hand (as the user described it; project memory note on per-gun
  mechanics).

**BF3:** 15 + 1 rounds, reloads 1.35 s and 1.6 s. IMFDB notes that the slide doesn't lock quite as far back
as it should, and that the sights correctly show two dots.

---

## 4. The underslung M320 in VR: proposed design

### 4.1 What the player does (plain English)

- **To fire a grenade:**
  1. Keep the right hand on the rifle.
  2. Slide the left hand down and back from the handguard to the launcher's little pistol grip, and
     squeeze. The rifle "switches" to the launcher; there's about half a second of the game's own switch.
  3. Pull the left trigger: one grenade.
  4. Move the left hand back up to the handguard: the rifle is back.
- **To reload by hand (version 2):**
  1. With the left thumb, push the barrel release by the front of the launcher's trigger guard. The
     barrel's back end swings out to the left.
  2. Pull the empty case out (or tip the gun so it drops).
  3. Take a new round from the chest.
  4. Push it in and swing the barrel shut until it clicks.
- **Rules that stop accidents:**
  - One trigger pull fires one thing.
  - The right trigger never fires a grenade.
  - Nothing fires while the barrel is open.
  - Squeezing the handguard is only a support grip, never "launcher mode".

### 4.2 What we take from BF3, FrostbiteVR and Overlord (and what doesn't fit)

- **From BF3:** the launcher is a real weapon of its own (slot 2), with its own ammo, its own 2.9 s
  automatic reload, its own projectile, and the switch keys (3 to the launcher, 1 back). We **use the
  game's own switch** rather than faking anything, so ammo, damage and the HUD stay right.
- **From FrostbiteVR** (MIT, by justiceofrip; BC2's XM8 and its 40 mm launcher):
  - **What worked** (tested by script, not yet accepted in a headset): a physical gesture triggered the
    game's *own* switch action, and the mod changed mode only after the game confirmed the switch (about
    16 ms later). Firing was blocked while the hand worked the part. Letting go, losing tracking, reloading
    or changing weapon cancelled the gesture
    ([LAUNCHER-BINDINGS](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/LAUNCHER-BINDINGS-20261001.md),
    [LAUNCHER-PILOT](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/LAUNCHER-PILOT-20261001.md)).
  - **What failed first:**
    - Grabs that never sent a switch: repeated input reads cleared the "armed" state, and the hand was
      checked against an older frame's gun position.
    - A hinge read while the gun was still settling after a weapon switch
      ([LAUNCHER-INPUT-TIMING](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/LAUNCHER-INPUT-TIMING-20261001.md),
      [LAUNCHER-GEOMETRY](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/LAUNCHER-GEOMETRY-20261001.md)).
  - **What they left out on purpose:**
    - Moving the grenade's start point to the launcher (the game's muzzle point stays at the rifle's
      muzzle in both modes).
    - Hand reloads of the launcher: their user put them off on October 3
      ([LAUNCHER-RELOAD-PLAN](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/LAUNCHER-RELOAD-PLAN.md)).
  - **Their user rejected** the launcher as "just another weapon slot" and asked for a physical action.
  - **For BF3 the gesture changes:** BC2 has fold-up launcher sights to flip, and BF3's underbarrel
    launcher has none (IMFDB). The trigger grip below fits instead.
  - **BF3's switch map is also simpler:** the toggle action works both ways, but it has no PC key, so we
    use 3 and 1.
- **From Overlord** (GPL-3.0; ideas only, in our own words;
  [underbarrel notes](https://github.com/Aeka0/Overlord/blob/main/docs/vr-underbarrel-runtime.md),
  [native contracts](https://github.com/Aeka0/Overlord/blob/main/docs/vr-underbarrel-native-contracts.md)):
  - The launcher is a *module of the rifle* with its own ammo ledger and mode. The rifle's magazine is
    never touched.
  - The firing grip needs a deliberate grip plus trigger at the launcher's trigger area; a squeeze alone is
    only support. The hand contact is checked strictly, and support can't slide into firing by rolling the
    wrist.
  - One trigger press makes at most one shot of one weapon. A press that was blocked doesn't fire later
    while still held.
  - Holding the module also carries the rifle.
  - Nothing fires while the action is open or moving.
  - The muzzle point comes from the visible launcher's barrel.
  - Overlord's M203 reload slides forward and drops the case part-way through the stroke. The M320 needs
    its own sideways swing and a case pulled by hand (3.2), so we copy the *rules*, not the motion.

### 4.3 Hand zones on the launcher (to measure from the mesh dump)

The launcher is its own skinned draw (`m320_underslung_1p_Mesh`). It isn't in the gun dump or in
`m416.bin`, which is why the hand went through it. Dump it with weapon.txt "dump", find its bones (the
barrel that swings, the round, the trigger) as was done for the rifle, and **add it to the gun's collision
shape**. Then mark, in gun-model space:

| Zone | What it is | Used for |
|---|---|---|
| Launcher grip | The M320's pistol grip and trigger, under the front half of the handguard, ahead of the magazine | Firing grip |
| Barrel release | By the front of the trigger guard, left side (3.2) | Opening the barrel (thumb or finger push) |
| Breech and chamber | The back end of the launcher barrel | Pulling the case out, pushing a round in |
| Barrel body | Its side, with the hinge axis near its front (inferred) | Swinging it open and shut by hand |
| Handguard | The existing support-grip line | Support only (unchanged) |

### 4.4 States

```
RIFLE ──left Grip on launcher grip──▶ SWITCHING_IN ──game says launcher is in hand──▶ LAUNCHER_READY
  ▲                                                                                      │ left Trigger
  │                                                                                      ▼
  └──(left hand leaves grip / tracking lost) SWITCHING_OUT ◀── FIRED (spent case in chamber)
                                                        │ v1: game reloads by itself (2.9 s) → LAUNCHER_READY
                                                        │ v2: OPEN → CASE_OUT → ROUND_IN → CLOSED → LAUNCHER_READY
```

### 4.5 Input rules

- **The left trigger's job depends on where the left hand is,** as already agreed (project memory note
  on the underbarrel launcher): on the launcher's grip it fires; on the charging handle it pulls; on the
  handguard it does nothing.
- **Firing grip** = the left squeeze held with the palm in the launcher-grip zone, with the hand roughly
  at the grip's angle. A squeeze on the handguard stays a support grip.
- **In LAUNCHER_READY**, the left trigger becomes the left mouse button and the **right trigger is
  ignored** (a short buzz tells the user). This keeps "one pull, one weapon".
- **A left-trigger press made while SWITCHING_IN, or while the barrel is open, is dropped.** It doesn't
  fire later even if still held (Overlord's rule).
- **Scripts must hold the left mouse button over several frames** for BF3 to see a shot (the mod already
  found this). Hold it until the game's loaded count drops, or about 150 ms, then let go.

### 4.6 Switching and timing

- **Switch on the grip, not on the trigger.** That hides the 0.55 s switch while the hand is still
  settling on the grip. The user's earlier idea was to switch, fire and switch back on each trigger pull;
  that would make every grenade lag by about half a second.
- **To switch in:** send 3, then wait until the game confirms. When the gun in hand changes, its loaded
  count should become ≤ 1 and its spare count the launcher's. This uses the same object `ammo.cpp` reads:
  the server soldier's +0x4C0, then +0x248. Only then go to LAUNCHER_READY.
- **To switch out:** send 1 when the left hand leaves the grip zone (with a little delay so a wobble doesn't
  bounce), on tracking loss, in a cutscene or locked scene, or when sprinting.
- Respect the game's 0.5 s gadget cooldown, and never send 3 and 1 in the same frame.

### 4.7 Aim, and where the grenade starts

- **Direction:** the look direction already follows the gun (`aim.cpp`). The M320's bore runs parallel to
  the rifle's, a few centimetres lower, so no correction is needed at first.
- **Start point:** BF3 launches the grenade 0.86 m in front of the camera, 0.3 m down and 0.1 m to the side
  (2.3).
  - **Version 1:** accept it.
  - **Version 2:** rewrite the live `InitialPosition` (and maybe `InitialDirection`) of the 40 mm settings
    every frame from the drawn launcher's muzzle (2.3).
- **Spread:** 2° not zoomed and 0° zoomed (2.3). Copy the zoomed values over while the launcher is in hand,
  or send the zoom.
- **Sight:** BF3's underbarrel M320 has none.
  - **Version 1:** no sight. Point and shoot, maybe with an optional landing marker as a training aid.
  - **Later:** the mod draws its own fold-up leaf sight on the left side (50 to 350 m marks, 3.2).

### 4.8 Ammo and reload

- **Version 1, the game's own reload:**
  - After the shot BF3 reloads by itself in 2.9 s (it waits for the left trigger to be let go).
  - The mod just keeps firing blocked until the loaded count is 1 again.
  - It's simple and safe. FrostbiteVR chose the same for BC2.
- **Version 2, the hand reload:** use the same method as the rifle's manual reload.
  - After the shot, write the game's loaded count back to 1, so the game doesn't start its own reload.
  - Keep the real state in the mod's own launcher ledger: spent case, open, empty, round in, closed.
  - Block the left trigger until the breech is closed with a round in, then take 1 off the game's spare
    count.
  - Only write the **server** copy of the counts (the mod's own lesson).
  - The ledger carries over when you switch back to the rifle. An open barrel can stay open, which is
    realistic. It's matched up again when the launcher comes back in hand.
  - God mode can optionally keep the 40 mm spare count up.
- **Version 2 motions** (real M320, 3.2): push the release, and the barrel's back end swings left. Pull the
  case back by its rim; it falls, reusing the falling-magazine code. Take a round from a chest pouch and
  push it into the chamber along the bore (a sideways error of a few cm and about 20° is fine). Swing the
  barrel shut until it latches, with the recorded sound and a buzz. The right hand never leaves the rifle.

### 4.9 Other cases

- **Out of spare rounds:** the left trigger gives a dry click and a buzz. BF3 may switch back to the rifle
  by itself (`SwitchToPrimaryWhenOutOfAmmo True` on the M416's data).
- **Ammo crates:** they raise the spare count. The ledger must read the new count, not fight it.
- **Picking up an AN-94 or RPK:** the M320 probably goes away with the M416 (1.6). The profile must check
  that slot 2 really holds the launcher before offering the grip.
- **The *Kaffarov* mission:** the M416 comes back there with the M320 *unusable* (wiki). Same check.
- **Death or loading a checkpoint:** rebuild the ledger from the game's counts.
- **Cutscenes and locked scenes:** force the rifle and leave the barrel as it is.
- **Sprinting:** BF3 doesn't block reloading while sprinting (data). The mod can do what feels best.

### 4.10 Test plan (in the headset, one item at a time)

1. Grip the launcher, then wait: the HUD's main counter shows "1 / 3"? Note how long the switch takes.
2. Left trigger: exactly one grenade. Holding the trigger fires nothing more. The right trigger in launcher
   mode does nothing.
3. Let go: the rifle is back, and the right trigger fires bullets.
4. Ten grenades at a marked wall at 30 m: where do they land against the aim point (not zoomed)? Repeat
   with the spread copy, or the zoom.
5. Watch where the grenade appears: from the face or from the launcher (2.3).
6. Version 1: after a shot, the automatic reload ends and the next shot works. Version 2: each reload step
   works, and nothing fires while open.
7. Ammo crate, pickup, checkpoint load, cutscene: no stuck switch and no wrong count.

---

## 5. Suggested order of work for this mission's weapons

1. **Stinger and SMAW basics** (both needed to finish the mission):
   - A way to select them (a button or the over-the-shoulder grab).
   - The zoom held at the eye (the Stinger can't lock without it).
   - The start-point and spread fixes (2.3).
   - The game's own reloads.
2. **The M320, version 1** (4.1 to 4.7 with the game's own reload), as a module of the M416 profile. It
   needs the launcher's mesh dump and collision shape.
3. **Spread for the rifle:** measure first (2.3). If the shots scatter, it touches every gun.
4. **Hand grenades:** from the chest, with hold and release (3.5).
5. **AN-94 and RPK profiles:** optional pickups. The mission can be played on the M416 alone, with ammo
   crates.
6. **M320 hand reload (version 2)**, the SMAW's container swap, and grenade throw speed: polish.

---

## 6. Things to check in the game

- [ ] The HUD's "G×n": hand grenades or launcher rounds? (Throw one with G and watch.)
- [ ] The M67 count at the mission start: 1 or 3?
- [ ] The SMAW's starting ammo: 1 + 3 (data) or 4 + 1 (wiki)?
- [ ] Does the Stinger lock only when zoomed? (Data says yes.)
- [ ] Do rifle shots scatter when not zoomed? (Data says 2.5°.)
- [ ] Does the launcher grenade start in front of the face? (Data says it starts from the camera.)
- [ ] The real switch time to the launcher and back (data: 0.55 s, and 0.5 to 0.67 s).
- [ ] Does taking an AN-94 throw away the M320? Can you take back the dropped M416?
- [ ] Does BF3's launcher reload animation swing the barrel out? (Then the mesh has a hinge bone.)
- [ ] Which hand fires the launcher in BF3's own animation: the left on the launcher grip? (Expected; not
  seen.)
- [ ] Can you take a fallen Marine's M16A4?
- [ ] Do the user's key bindings match the defaults above?

---

## 7. Sources

**The game's data** (Venice-EBX, a text dump of BF3's data files):

- [Player's loadout](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Characters/Soldiers/Customizations/UsHumanSpSoldier_Valley.txt)
- [Player's spawn template](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Gameplay/Soldiers/SoldierSpawnTemplates/SpHumanSoldierSpawnTemplateDataValley.txt)
- Level logic: [GlobalLogic](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Levels/SP_Valley/GlobalLogic.txt),
  [ForestLogic](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Levels/SP_Valley/ForestLogic.txt),
  [Valley_JetLogic](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Levels/SP_Valley/Valley_JetLogic.txt),
  [UseRPGLogic](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Levels/SP_Valley/Prefabs/UseRPGLogic.txt)
- Enemy setups: [RuSpSoldier](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Characters/Soldiers/Customizations/RuSpSoldier.txt),
  [RuSpSoldier_LMG](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Characters/Soldiers/Customizations/RuSpSoldier_LMG.txt),
  [RuSpSoldier_RPG](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Characters/Soldiers/Customizations/RuSpSoldier_RPG.txt)
- [AI weapon drop](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/AI/Pickups/DefaultAIDynamicPickup.txt)
- Weapons: [M416](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/M416/M416.txt),
  [40 mm HE firing](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/Gadgets/M320/40mm_HE_Firing.txt),
  [40 mm sway](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/Gadgets/M320/40mm_GunSway.txt),
  [SMAW](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/SMAW/SMAW.txt),
  [Stinger (this level)](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/FIM92A/FIM92A_SP_Valley.txt),
  [M67](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/M67/M67.txt),
  [AN-94](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/AN94/AN94.txt),
  [RPK](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/RPK/RPK.txt),
  [M9](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Weapons/M9/M9.txt)
- Input: [weapon switching](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Input/SoldierWeaponSwitching.txt),
  [input concepts](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Input/VeniceSoldierInputConcepts.txt),
  [input mapping](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/Input/VeniceSoldierInputMapping.txt)
- [Single-player HUD](https://github.com/VeniceUnleashed/Venice-EBX/blob/master/UI/Flow/Screen/HudSPScreen.txt)

**About the game:**

- Battlefield Wiki: [Rock and a Hard Place](https://battlefield.fandom.com/wiki/Rock_and_a_Hard_Place),
  [M416](https://battlefield.fandom.com/wiki/M416/Battlefield_3),
  [M320](https://battlefield.fandom.com/wiki/M320), [SMAW](https://battlefield.fandom.com/wiki/SMAW),
  [Stinger](https://battlefield.fandom.com/wiki/FIM-92_Stinger/Battlefield_3),
  [RPK-74M](https://battlefield.fandom.com/wiki/RPK/Battlefield_3)
- IMFDB: [Battlefield 3](https://www.imfdb.org/wiki/Battlefield_3)

**The real weapons:**

- US Army [TC 3-22.9 Rifle and Carbine (2016)](https://www.armywriter.com/board/references/TC3-22x9.pdf);
  [FM 3-22.31 chapter 2 (M203)](https://www.globalsecurity.org/military/library/policy/army/fm/3-22-31/chap2.htm);
  [M320/M320A1 training deck (Slideshare copy)](https://www.slideshare.net/slideshow/m320m320a1-grenade-launcher-pmi/69634635);
  [army.mil on the M320](https://www.army.mil/article/180665/fort_jackson_soldiers_train_on_newest_grenade_launcher)
- Wikipedia: [M320](https://en.wikipedia.org/wiki/M320_Grenade_Launcher_Module),
  [HK416](https://en.wikipedia.org/wiki/Heckler_%26_Koch_HK416), [AN-94](https://en.wikipedia.org/wiki/AN-94),
  [RPK-74](https://en.wikipedia.org/wiki/RPK-74), [AK-47](https://en.wikipedia.org/wiki/AK-47),
  [AK-74](https://en.wikipedia.org/wiki/AK-74),
  [SMAW](https://en.wikipedia.org/wiki/Shoulder-launched_Multipurpose_Assault_Weapon),
  [Stinger](https://en.wikipedia.org/wiki/FIM-92_Stinger), [M67](https://en.wikipedia.org/wiki/M67_grenade),
  [Beretta M9](https://en.wikipedia.org/wiki/Beretta_M9)
- IMFDB: [HK416](https://www.imfdb.org/wiki/HK416),
  [AG / M320 series](https://www.imfdb.org/wiki/Heckler_%26_Koch_AG_grenade_launcher_series),
  [SMAW](https://www.imfdb.org/wiki/Mk_153_SMAW), [Stinger](https://www.imfdb.org/wiki/FIM-92_Stinger),
  [M67](https://www.imfdb.org/wiki/M67_Hand_Grenade)
- Modern Firearms: [AG36 / AG-C](https://modernfirearms.net/?p=5443),
  [AN-94](https://modernfirearms.net/en/assault-rifles/russia-assault-rifles/an-94-abakan-eng/),
  [SMAW](https://modernfirearms.net/?p=5456)
- FAS: [SMAW](https://man.fas.org/dod-101/sys/land/smaw.htm),
  [Stinger](https://man.fas.org/dod-101/sys/land/stinger.htm)
- [Firearms News AN-94 review](https://www.firearmsnews.com/editorial/russian-545x39mm-an94-nikonov-review/383282);
  [TTAG M320 review](https://thetruthaboutguns.com/gun-review-hk-m320-grenade-launcher);
  [Midwest Gun Works (HK forward assist part)](https://www.midwestgunworks.com/page/mgwi/prod/203599)

**VR mods:**

- FrostbiteVR (MIT): [GRENADE-LAUNCHER-INTERACTION](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/GRENADE-LAUNCHER-INTERACTION.md),
  [LAUNCHER-BINDINGS](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/LAUNCHER-BINDINGS-20261001.md),
  [LAUNCHER-PILOT](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/LAUNCHER-PILOT-20261001.md),
  [LAUNCHER-INPUT-TIMING](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/LAUNCHER-INPUT-TIMING-20261001.md),
  [LAUNCHER-GEOMETRY](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/LAUNCHER-GEOMETRY-20261001.md),
  [LAUNCHER-RELOAD-PLAN](https://github.com/justiceofrip/FrostbiteVR/blob/main/docs/LAUNCHER-RELOAD-PLAN.md)
- Overlord (GPL-3.0, ideas only): [underbarrel runtime](https://github.com/Aeka0/Overlord/blob/main/docs/vr-underbarrel-runtime.md),
  [underbarrel native contracts](https://github.com/Aeka0/Overlord/blob/main/docs/vr-underbarrel-native-contracts.md),
  [launchers](https://github.com/Aeka0/Overlord/blob/main/docs/vr-launchers.md),
  [grenades](https://github.com/Aeka0/Overlord/blob/main/docs/vr-grenades.md)
