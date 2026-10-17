# Escalation (SV) as the original runs it (RE, 2026-10-18)

Labels: C = TransContent / TransGame bytecode (`work/script/decomp_tc/MP_ESC_Remnant_BASE_m.TransContent.txt`, decompiled this
item) + authored data; H = inference.
Data export: `notes/data/escalation/{MP_ESC_BrokenHope,MP_ESC_Remnant,_class_defaults}.json` (`tools/escalation_export.py`). It
holds the spawn lists, all generators, and every generator / round event with its downstream Kismet and targets.
Maps: only **MP_ESC_BrokenHope** and **MP_ESC_Remnant** are SV (MP_TRANSFORMING_ACTORS_RE §4).

## 1. Match flow (C, `TnSurvivalGame`)

- **Players:**
  - up to **4**, all on the map's CoopInfo faction (Autobots on BrokenHope, where the horde is Decepticon; the reverse on
    Remnant);
  - every TnTeamPlayerStart is set up for attack / defend;
  - classes: TnPlayerControllerSurvival, TnPlayerReplicationInfoSurvival, TnPlayerPawnSurvival.
- **Loadout:** `SetPlayerDefaults` removes the primary and secondary weapons and gives a **TnWeaponAssaultRifle**. Weapon data uses
  **SurvivalData** (`DesiredWeaponDataType` 4); skill index fixed at 1 (**no difficulty setting**).
- **States:**
  1. **StartMatch** → "SV" game-type message → **PreGame**.
  2. **PreGame:** wait until a player has spawned and `ElapsedTime ≥ PreGameTime` (BrokenHope 30 s, Remnant 10 s) → **InWave**.
  3. **InWave.BeginState:**
     - `IncrementWave` (CurrentWave / DisplayWave++; **CurrentWave clamps to the last wave entry**, so waves past the table repeat
       the last row; HUD shows DisplayWave + 1; **wave 16 displayed → achievement 45**);
     - fire `TnSeqEvent_SurvivalEvents` **"Round Begin"**;
     - TotalUsedThisWave = 0; spawn the first enemy (FillOutWave).
  4. **InWave.Update (each tick):**
     - **CheckLivingPlayers:** if every player controller IsDead → `EndGame(…, "AllDead")`. Being downed doesn't count as dead.
     - Once TotalUsedThisWave ≥ wave **Budget** and no enemy is left alive → **BetweenWaves**.
     - Else: when the alive enemies' cost ≤ **BudgetDeficitThreshold** → start filling. While filling, spawn one enemy every
       **FillWaveReCheckTime** (BrokenHope 0.5 s default, Remnant 3.0 s) until the alive cost ≥ **BudgetAlive**.
  5. **BetweenWaves:**
     - RoundEnded: the respawn helper respawns every dead player; **"Round End"** event fires; **every living player is revived**
       (`GetRevive`); GC;
     - then GRI NextWaveTime counts down **TimeBetweenWaves (25 s on both)** → **InWave**.
  6. **Game over:** `TnSurvivalGameOverMessage(DisplayWave + 1)`; every PC → `OnCampaignGameOver`. **There is no win
     condition: it's endless, and you lose when all players are dead at once.**

## 2. Enemy spawning (C)

- **Choosing a type** (`GenerateListOfPossibleEnemies`):
  - Enemy entries are eligible while `MinWave ≤ CurrentWave ≤ MaxWave` (MaxWave 0 = no cap) and living count < MaxCount.
  - **If any eligible type is below its MinCount, that type is chosen exclusively.** Otherwise it's a uniform random choice
    among the eligible types.
  - Cost is added to TotalUsedThisWave and the alive cost.
- **Placement** (`TnSurvivalSpawnFinder`):
  - each TnSurvivalZoneVolume has a score per spawn type (Standard / ShortRange / LongRange / Air / Boss);
  - **a zone with a player in it gets −999** (OccupiedZoneScore), and zone "ZoneChanges" adjust other zones' scores while a
    player is inside;
  - pick a random zone among the **top 3** (NumTopZonesToConsider) weighted by |score|, take its spawn point, and check the
    capsule fits (up to 5 tries).
  - Kismet turns zones on and off as doors open (§5).
- **On spawn:** team = horde; it **sees every player immediately** (`SeeEnemy`), and the wave's buffs are applied.
- **On kill** (`Killed`):
  - loot via the PickupRewardManager (loot table `TR_Escalation_m.Loot_Table`; classifications per enemy + AbilityResource,
    with Headshot / Melee / Explosion variants);
  - **killer PRI gains PowerReward**;
  - type count and alive cost go down.

**Enemy table (BrokenHope, Decepticon horde):**

| Enemy | Waves (0-based) | Min–Max alive | Cost | Spawn type | Power |
|---|---|---|---|---|---|
| Spider | 0–4 | 2–8 | 5 | Standard | 10 |
| Soldier MachineGun | 0+ | 0–8 | 10 | Standard | 25 |
| Soldier Rocket | 2+ | 0–4 | 20 | LongRange | 30 |
| CQC Specialist | 2–3 / 4+ | 0–2 / 0–6 | 20 | Standard | 50 |
| Soldier Jet | 4+ | 0–4 | 20 | Air | 75 |
| Soldier Shotgun | 5–6 / 7+ | 0–2 / 0–6 | 30 | Standard | 75 |
| Brute | 7–8 / 11+ | 0–1 | 60 | Standard | 100 |
| Titan | 9–10 / 10+ (max 2) | 0–1 / 0–2 | 60 | Standard | 100 |
| **Soldier Tank** | **14+** | 0–1 | 60 | **Boss** | 300 |

**Remnant (Autobot horde):**

| Enemy | Waves | Min–Max | Cost | Power |
|---|---|---|---|---|
| HoloBrute | 0–5 | 3–6 | 10 | 25 |
| Soldier MachineGun | 0+ | 2–6 | 10 | 25 |
| Soldier Rocket | 2+ | 0–2 | 20 | 30 |
| CQC | 4+ | 0–3 | 20 | 50 |
| Shotgun | 6+ | 0–2 | 25 | 75 |
| Titan | 7–9; 11+ (1–2) | | 45 | 100 |
| Brute | 9–10; 12+ (1) | | 45 | 100 |

Spawn types are mostly ShortRange.

**Waves** (Budget / BudgetAlive / DeficitThreshold; full rows in the json):
- **BrokenHope:** 100 / 50 / 20 → 150 → 200 → 300 → 400 → 500 → 600 → 800 → 1000 → 1200 (waves 9–13) → 1300 (14–16).
  Buffs: wave 9 onward `TnBuffSurvivalDecreaseDamageTaken` level 0..4 (**damage taken × 0.9 / 0.8 / 0.7 / 0.6 / 0.5**); from wave 13
  `TnBuffSurvivalIncreaseDamage` level 0..3 (**damage dealt × 1.25 / 1.5 / 1.75 / 2.0**, level 4 = 2.25).
- **Remnant:** 100 → … → 1200 from wave 9 onward, 20 rows. DecreaseDamageTaken from wave 5; IncreaseDamage from wave 10, reaching
  level 4 (×2.25) at 14+.
- Past the last row the last row repeats (clamped CurrentWave), so endless waves keep the top budget and buffs.

## 3. Energon economy and generators ("shop") (C)

- **Currency:**
  - `TnPlayerReplicationInfoSurvival._CurrentPower`, per player, **clamped 0..9999**;
  - earned only from kills (PowerReward above), plus whatever the loot drops give (H: ability-resource pickups refill the
    ability, not power);
  - reset to 0 on PRI Reset.
- **Generators** (`TnSurvivalGenerator`, "vending machines"):
  - Standing in the generator's area makes it usable (contextual command shows the name key and cost `` `n ``).
  - **Hold X** (or the keyboard interact key) to set `_AllowDraining`. While held, each tick moves `max(1, dt × DrainRate)` power
    (capped at what's still needed) from the player into the generator, with a repair-beam tracer FX, a fill loop sound, and a
    fail sound if you run out.
  - **Shared pool:** any number of players (up to 4 tracked) can feed the same generator; progress is kept.
  - On reaching `PowerLevels[level]`:
    - `OnPowerLevelChange(+)`: **FullSound**, and **TnSeqEvent_SurvivalGenerator** fires with the new level;
    - every MP generator has **one level**, so it's done after one purchase. The fill-anim position shows progress.
- **What each generator does** (via Kismet, C): **unhide a pickup factory** (and turn off the generator's own trigger volume):

| Generator | Cost (BrokenHope / Remnant) | Gives |
|---|---|---|
| Ammo | 50 | TnAmmoCratePickupFactory |
| Flak grenade / K-mine | 75 | weapon pickup |
| Health | 100 | TnHealthPickupFactory |
| Flashbang | 125 | weapon pickup |
| BurstRifle / HeavyPistol / Shotgun | 200 | weapon pickup |
| Overshield | 250 | TnOverShieldPickupFactory |
| Plasma / Scatter / Sniper | 300 | weapon pickup |
| GrenadeLauncher / RocketLauncher | 500 | weapon pickup |
| HeavyMG | 600 | weapon pickup |
| HealGrenade | 800 | weapon pickup |
| Ion turret / Rocket turret | 1200 | (mounted) weapon pickup factory |
| Repair ray | 1250 | weapon pickup |
| **Door** | 225–2500 | §5 |

DrainRate is roughly cost / 4 per second, so a full purchase takes about 2–5 s of holding.

## 4. Respawn, downed, revive (C)

- `AllowDowned` true: players get downed and can be revived by teammates (stock co-op revive).
- **Death:** the respawn helper is **TnRespawnHelperRoundEnd**: a dead player waits (time −1, no timer) and **respawns only when
  the wave ends**.
- **Wave end:** every living player is revived.
- **Lose:** all players dead (not downed) at the same moment.

## 5. Map Kismet: doors, bridges, zones (C, from the generator events)

**BrokenHope** (12 DoorGenerator events; door pairs have a generator on each side opening the same doors):

| Generator(s) | Cost | Opens |
|---|---|---|
| 13243 / 12789 | **2500** | doors 14299 + 2661 |
| 12564 | 1000 | doors 16265 + 4577 |
| 866 | 700 | door 5499; enables zone 3931; drops DynamicBlockingVolumes |
| 7839 / 11282 | 400 | doors 13872 + 3347 (zone 16285) |
| 13385 / 4199 | 300 | doors 11450 + 9615 (zone 10523) |
| 8025 | 400 | door 1531 + **bridges 1476 + 7490** |
| 2813 / 8540 | 400 | doors 12155 + 12616 + **bridge 5091** |
| 14319 | 400 | door 11211 + **bridges 3522 + 5711** |

- **Bridges:** hidden with no collision until bought; the purchase does `ToggleHidden` UnHide + `ChangeCollision` + Transform.
- **Doors:** each purchase also turns on the NavigationCells through the doorway and turns off the TriggerVolumes /
  DynamicBlockingVolumes that sealed it. Some purchases switch on new TnSurvivalZoneVolumes (new spawn areas).
- **Round counters ("Wall_Crashers"):** "Round Begin" counters enable four player-presence triggers after **rounds 3, 4, 4 and 6**.
  Those triggers toggle specific TnSurvivalZoneVolumes on / off while a player stands in them. In other words, extra spawn areas
  near the walls unlock as waves progress.
- Round events also drive announcer dialog (random line per round end) and AI-dialog mute groups.
- One "Round Begin" (first round only) transforms an object variable in the Setup sequence (target not resolved in the export, H).

**Remnant** (8 door generators, each with a separate zone-activation chain):

| Generator | Cost | Opens | Zones enabled |
|---|---|---|---|
| 1189 "HEALTH DOOR" | 225 | 7351 + 9081 | 2 |
| 8829 "W/A DOOR" | 225 | 12943 + 6670 | 3 |
| 9849 "AI LEFT" | 500 | 7404 + 797 | 5 |
| 10858 "AI RIGHT" | 500 | 15255 + 3920 | 5 |
| 11149 "REPAIR GUN DOOR" | 700 | 1373 | 3 |
| 9533 "FUEL PUMP" | 700 | 13617 | 3 |
| 9525 "WEAPONS RIGHT" | 1000 | 8277 (+ Interp) | 5 |
| 8219 "WEAPONS LEFT" | 1000 | 8437 (+ Interp) | 5 |

Doors 797 / 3920 are chained from another door's TransformWorldProp "Finished" (MP_TRANSFORMING_ACTORS_RE).

## 6. Not covered / H

- Per-enemy AI behaviour (the AICD templates; see the campaign AI docs).
- The loot table contents (`TR_Escalation_m.Loot_Table`, in authored.db).
- The co-op revive timings (stock co-op path).
- Easter egg (C, not needed): `TnPlayerReplicationInfoSurvival.CheckForUpdate` grants XP / unlocks to specific developer player
  names using Barricade at generator TnSurvivalGenerator_2812. Don't port it.
