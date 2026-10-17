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
- **Round counters ("Wall_Crashers"):** "Round Begin" counters enable four touch triggers after **rounds 3, 4, 4 and 6**.
  Those triggers toggle specific TnSurvivalZoneVolumes on / off while a pawn is inside. In other words, extra spawn areas
  near the walls unlock as waves progress. Details in §5a.
- Round events also drive announcer dialog (random line per round end) and AI-dialog mute groups.
- One "Round Begin" (first round only) transforms an object variable in the Setup sequence (target not resolved in the export, H).

### 5a. BrokenHope complete (C; added 2026-10-18)

**Script:**
- `MP_ESC_BrokenHope_BASE_m`'s TransContent is decompiled to `work/script/decomp_tc/MP_ESC_BrokenHope_BASE_m.TransContent.txt`.
- Every TnSurvival* / generator / spawn / wave / buff / respawn function is **byte-identical to Remnant's** (diffed). So
  §1–§4 apply unchanged, and the maps differ only in data.
- Export: `notes/data/escalation/MP_ESC_BrokenHope.json`. It now also carries `zones`, and the CDO rows are dropped: **35
  generators per map**, not 36.

**Spawn list:**
- PreGameTime **30 s**, TimeBetweenWaves **25 s**, FillWaveReCheckTime **0.5 s** (CDO).
- **Waves** (Budget / BudgetAlive / BudgetDeficitThreshold, 0-based):

| W | Budget | Alive | Deficit | Buffs |
|---|---|---|---|---|
| 0 | 100 | 50 | 20 | – |
| 1 | 150 | 80 | 40 | – |
| 2 | 200 | 100 | 50 | – |
| 3 | 300 | 125 | 100 | – |
| 4 | 400 | 150 | 120 | – |
| 5 | 500 | 200 | 170 | – |
| 6 | 600 | 200 | 180 | – |
| 7 | 800 | 230 | 200 | – |
| 8 | 1000 | 230 | 200 | – |
| 9 | 1200 | 230 | 220 | DecreaseDamageTaken L0 (×0.9) |
| 10 | 1200 | 230 | 220 | DDT L1 (×0.8) |
| 11 | 1200 | 230 | 220 | DDT L2 (×0.7) |
| 12 | 1200 | 230 | 220 | DDT L3 (×0.6) |
| 13 | 1200 | 230 | 220 | DDT L4 (×0.5) + IncreaseDamage L0 (×1.25) |
| 14 | 1300 | 230 | 220 | DDT L4 + ID L1 (×1.5) |
| 15 | 1300 | 230 | 220 | DDT L4 + ID L2 (×1.75) |
| 16+ | 1300 | 230 | 220 | DDT L4 + ID L3 (×2.0); repeats (CurrentWave clamps at 16) |

On BrokenHope IncreaseDamage never reaches L4 (×2.25). That level is Remnant-only.

**Enemies** (13 entries; MaxWave 0 = open):

| Entry | Waves | Min–Max | Cost | Type | Loot class | Power |
|---|---|---|---|---|---|---|
| Spider | 0–4 | 2–8 | 5 | Standard | GENERIC | 10 |
| Soldier MG | 0+ | 0–8 | 10 | Standard | SoldierDec | 25 |
| Soldier Rocket | 2+ | 0–4 | 20 | LongRange | SoldierDec | 30 |
| CQC Specialist | 2–3 | 0–2 | 20 | Standard | SoldierDec | 50 |
| CQC Specialist | 4+ | 0–6 | 20 | Standard | SoldierDec | 50 |
| Soldier Jet | 4+ | 0–4 | 20 | **Air** | GENERIC | 75 |
| Soldier Shotgun | 5–6 | 0–2 | 30 | Standard | SoldierDec | 75 |
| Soldier Shotgun | 7+ | 0–6 | 30 | Standard | SoldierDec | 75 |
| Brute | 7–8 | 0–1 | 60 | Standard | BruteDec | 100 |
| Titan | 9–10 | 0–1 | 60 | Standard | TitanDec | 100 |
| Titan | 10+ | 0–2 | 60 | Standard | **BruteDec** (authored as is) | 100 |
| Brute | 11+ | 0–1 | 60 | Standard | BruteDec | 100 |
| **Soldier Tank** | **14+** | 0–1 | 60 | **Boss** | SoldierTankDec | **300** |

- **Wave-15 boss:** the Tank entry is MinWave 14 (0-based), so it appears from **displayed wave 15** (HUD = index + 1). At most
  one is alive at a time. MinCount 0, so it isn't forced: it's in the uniform random pick whenever it's eligible and none is
  alive.
- It spawns only in **Boss** zones (below).
- Wave 10 holds two Titan entries (9–10 max 1, 10+ max 2), so the effective cap there is 2 (H: each entry counts separately
  in `GenerateListOfPossibleEnemies`; both refer to the same class).

**Spawn zones** (30 TnSurvivalZoneVolumes; scores are SelfModifier by type; ±1000 effectively forbid or force a type):
- **Boss zones (6):** 1418, 4566, 498, 5278, 8330, 9622. Boss 999, Standard / ShortRange / LongRange −1000, one spawn point each.
  These are the Tank's spawn spots.
- **Air zones (3):** 15079, 3762, 9331 (Air 1000, two points each), for the Jets.
- **LongRange zones (2):** 15943, 2433 (LongRange 20, the rest −1000), for the Rocket soldiers' perches.
- **Exclusion / neighbour-only zones:** 2761 (all −1000, 0 points), 5320, 9027, 3244, 4342, 9083, 15087, 2663. Their
  `ZoneChanges` raise neighbours (typically +10 Standard / LongRange) while a player is inside.
- **Standard zones:** the rest (1036 +10 Standard, plus 14392, 2647, 8579 with 0 self).
- **Inactive at start (7):**
  - **door-unlocked:** 3931 (gen 866), 16285 (gen 7839 only, not its pair 11282: authored asymmetry), 10523 (gen 4199 only,
    not 13385);
  - **Wall_Crashers-unlocked:** 7739, 7763, 15078, 6666.

**Doors / bridges** (12 DoorGenerator events; all effects listed, from the export):

| Generator | Cost / DrainRate | Transform | Also |
|---|---|---|---|
| 13243, 12789 (pair) | 2500 / 500 | doors 14299, 2661 | TriggerVolume 15411, 7789 off; 4 NavigationCells on |
| 12564 | 1000 / 250 | doors 16265, 4577 | TV 11387 off; 4 nav cells |
| 866 | 700 / 200 | door 5499 | DynamicBlockingVolume 3124, 3763, 7511 + TV 11595 off; 2 nav cells; **zone 3931 on** |
| 8025 | 400 / 100 | door 1531 + **bridges 1476, 7490** | bridges: UnHide + ChangeCollision In; 4 nav cells |
| 2813, 8540 (pair) | 400 / 100 | doors 12155, 12616 + **bridge 5091** | bridge UnHide + collision; DBV 13810, 5793 + TV 10646, 3714 off; 6 nav cells |
| 7839, 11282 (pair) | 400 / 100 | doors 13872, 3347 | DBV 13810, 5793 + TV 11261, 915 off; 4 nav cells; **zone 16285 on (7839 only)** |
| 13385, 4199 (pair) | 300 / 75, 100 | doors 11450, 9615 | DBV 13227, 6664 + TV 15137, 15270 off; 4 nav cells; **zone 10523 on (4199 only)** |
| 14319 | 400 / 100 | door 11211 + **bridges 3522, 5711** | bridges UnHide + collision; 4 nav cells |

Totals: 13 doors and 5 bridges.
- A pair is two generators on opposite sides of the same doorway. The first purchase opens the doorway; buying the second one
  replays the same actions (H: harmless; the doors are already transformed).
- DBV 13810 / 5793 are shared by the 2813 / 8540 and 7839 / 11282 doorways. Either purchase drops them.
- Time to buy one generator ≈ Cost / DrainRate seconds while holding X (BrokenHope doors 3.5–5 s).

**Item generators (23):**

| Item | Cost (DrainRate) | Count | Pickup |
|---|---|---|---|
| Ammo | 50 (25) | ×3 | TnAmmoCratePickupFactory |
| FlakGrenade | 75 (25) | ×2 | weapon pickup |
| Kmine | 75 (25) | | weapon pickup |
| Health | 100 (50) | ×2 | TnHealthPickupFactory |
| FlashBang | 125 (50) | | weapon pickup |
| HeavyPistol | 200 (50) | | weapon pickup |
| BurstRifle | 200 (50) | | weapon pickup |
| Shotgun | 200 (CDO rate) | | weapon pickup |
| Overshield | 250 (75) | | TnOverShieldPickupFactory |
| Scatter | 300 (100) | | weapon pickup |
| Sniper | 300 (100) | | weapon pickup |
| Plasma | 300 (75) | | weapon pickup |
| RocketLauncher | 500 (125) | | weapon pickup |
| GrenadeLauncher | 500 (200) | | weapon pickup |
| HeavyMG | 600 (200) | | weapon pickup |
| HealGrenade | 800 (200) | | weapon pickup |
| IonTurret | 1200 (300) | | weapon pickup |
| RocketTurret | 1200 (300) | | weapon pickup |
| Repair | 1250 (400) | | weapon pickup |

- Each one: its TriggerVolume off, then UnHide the factory.
- **Orphan event:** `TnSeqEvent_SurvivalGenerator_3551` names "TnSurvivalGenerator_3107", which doesn't exist in the map. Its
  targets are empty object variables, so it can never fire.

**Wall_Crashers sequence:**
- **Zone gates:** `TnSeqEvent_SurvivalEvents` "Round Begin" → SeqCond_Increment (A counts rounds) → on **A == B** enables a
  disabled SeqEvent_Touch:

| Round | Trigger volume | Zone (on while touched, off on untouch) |
|---|---|---|
| 3 | TV 11856 | 7739 |
| 4 | TV 14603 | 7763 |
| 4 | TV 11067 | 15078 |
| 6 | TV 8879 | 6666 |

  - SeqEvent_Touch's default class filter is Pawn (H: any pawn, so a player or an AI standing in the volume holds the zone on).
  - A == B fires exactly once, so each gate opens once and stays open.
- **Crashable walls (always armed; first touch by a pawn):**
  - TV 9486 / 11960 / 10811 / 7836 → `SeqAct_CauseDamage` **200** (TnDamageTypeAux1) on TnStaticDestructibleActor 10489 / 6870 /
    15525 / 4868, plus an emitter on (12390 / 8889 / 6682 / 13132).
  - TV 10811 also hides an object variable and changes its collision.
- **Linked wall:** touch TV 8879 (the round-6 volume) arms TnToggleDestructible on wall 5491 and a disabled Touch (TV 13849 →
  200 damage to 5491) while touched.
- H: these are the "Wall_Crashers" set pieces, walls that break when a pawn (typically an enemy) pushes into them, opening new
  approach routes.

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

## 7. Horde AI templates, downed / revive values, loot table (C, authored.db + script; added 2026-10-18 for Gameplay)

### 7.1 AI templates (`TnAiCharacterDefinition` in TR_AiEscalation_p / TR_SpiderDec_AI_p)
All share GOAP: SearchSpace + ActionSet + GoalSet + SensorSet. The chassis (TnTransformer ROBODEF) gives health and speed. Weapons come
from the ActionOverride lists (a random pick when several are listed; H).

| AICD (Decepticon; the Autobot twin is analogous) | Chassis (health / ground speed) | Action / Goal / Sensor set | Weapons (robot; vehicle) | Pawn class | Notes |
|---|---|---|---|---|---|
| Spider_AICD | Spider_ROBODEF: **20 HP**, 2000 UU/s, SelfDamageMultiplier 0.75 | Spider_AiActionSet / Spider_AiGoalSet / Spider_AiSensorSet | (melee / explode from the action set) | default | loot class SpiderDec (absent from the loot table, so no drop) |
| Soldier_MachineGun_Decepticon | SoldierCarMGDec: **150 HP**, 1200 | Soldier_AiActionSet / TR_AiGoalSet_Agent / AggressiveSoldier | AssaultRifle / BurstRifle / HeavyPistol; AssaultRifleVehicle | default | 3 colour variations |
| Soldier_Rocket_Decepticon | SoldierCarRocketDec (150 shared SoldierCar health H) | same | RocketLauncher; RocketVehicle | default | |
| Soldier_Shotgun_Decepticon | SoldierCarShotgunDec | same | **EmpShotgun**; AssaultRifleVehicle | default | |
| CQCSpecialist_Decepticon | CQCDec: **90 HP**, 2200 | CQCSpecialist_AiActionSet / CQC_AiGoalSet / CQC_AiSensorSet | (from the set; override empty) | default | |
| Soldier_Jet_Decepticon | SoldierJetDec: **250 HP** | Soldier_Jet_AiActionSet / JetSoldier_AiGoalSet / SoldierJet_AiSensorSet | (set) | **TnAiJetSoldierPawnContent** | Air spawn type |
| Brute_Decepticon | BruteDec: **Health 100,000**, 1100, Mass 300 | Brute_AiActionSet / TR_AiGoalSet_Brute / Brute_AiSensorSet | **HalberdAxe** (melee) | default | **HeavyDamageThreshold 300** (enabled). The huge health means it's killed through its destructible weak point (backpack), not health (H; cf. the Brute backpack destruction FX). |
| Titan_Decepticon | TitanDec: **1,250 HP**, 450, Mass 300 | Titan_AiActionSet / Titan_AiGoalSet / Titan_AiSensorSet | **GatlingGun** | default | |
| **Soldier_Tank_Decepticon (boss)** | SoldierTankDec: **6,000 HP**, 720, Mass 10,000 | **Tank_AiActionSet** / TR_AiGoalSet_Agent / SoldierTank_AiSensorSet | (set) | **TransGame.TnTankAiPawn** | Boss spawn zones (§5a) |
| HoloBrute_Autobot (Remnant) | HoloBruteAut (default health) 1800 | HoloBrute_AiActionSet / TR_AiGoalSet_Agent / HoloBrute_AiSensorSet | **CeremonialStaffHolo** | default | **Opacity 0.9** (holographic) |
| Brute_Autobot | BruteAut | as Brute | **WarHammer** | | |

The Escalation buffs (§2) are layered on top at spawn: damage taken × 0.9 → 0.5, damage dealt × 1.25 → 2.25.

### 7.2 Downed / revive (`TnPawn`, `TnPlayerPawnSurvival`)
- **Going down:**
  - `TakeDamageHealthBelowZero`: when health would drop below 0 and the pawn can be downed (not KillZ / not
    TnDamageTypeEnvironmentNoDowned) → downed at Health 0.
  - `TnSeqEvent_Downed` fires; PRI.AddDowned.
  - **DownedInvulnerableLength 3 s** (TnPlayerPawn).
- **Bleeding out:** each tick while downed and draining, `TakeDamage(dt × DownedHealthDrainRate, TnDamageTypeDownedDrain)`. **Death
  when Health ≤ −DownedHealth.**
  - **Escalation (TnPlayerPawnSurvival): DownedHealth 540, drain 18 /s → 30 s** with no extra damage.
  - Other values for comparison: versus TnPlayerPawnMultiplayer 300 / 30 → 10 s; base TnPawn 400 / 40.
  - Enemy fire while down shortens it.
- **Reviving:**
  - a teammate's GiveRevive plays **Revive_Heal** (robot) / **Revive_Heal_VEH** (vehicle form). The revive takes the length of that
    animation (ClientStartReviveCountdown = the animation length);
  - while being revived the target doesn't drain;
  - success → `GetRevive`: health set to **RevivalHealth 115** (or HealthMax with a full revive);
  - **OnReviveBuffs:** TnBuffDifficultyPlayerDamageTakenRevived + TnBuffDifficultyJamAbilitiesRevived (difficulty buffs after being
    revived);
  - interrupting the animation fails the revive and draining resumes.
- **Wave end:** every downed player is revived (§1, GetRevive) and the dead respawn (TnRespawnHelperRoundEnd).
- The Revive_Heal length is in the anim sets (not in authored.db). Read it from your anim data.

### 7.3 Loot / drop table (`TR_Escalation_m.Loot_Table`, TnLootTable)
- On a kill, `SpawnPickupRewards` rolls the enemy's LootClassification, plus 'AbilityResource', plus Headshot / Melee / Explosion
  AbilityResource by kill type (§2).
- Each entry rolls ChanceToDrop, then a weighted pick.

| Classification | Drops |
|---|---|
| GENERIC (Spider, Jet, …) | nothing |
| SoldierDec / SoldierAut | 4 × AbilityResourcePickup (100 %) |
| BruteDec / Aut, TitanDec / Aut | 7 × AbilityResource |
| SoldierTankDec / Aut | 12 × AbilityResource, + **50 % one rare weapon: GatlingGun or HeavyRocketTurret** (1 : 1) |
| HoloBrute, SpiderDec | not in the table, so nothing |
| AbilityResource (every kill) | 1 × |
| HeadShotAbilityResource | 5 × |
| MeleeAbilityResource | 1 × (weighted pick of 2 identical entries) |
| ExplosionAbilityResource | 1 × |
| Health / Ammo / Overshield / ResourceCache | 100 % / 100 % / 10 % / 16 × (classifications used by other drop sources) |

- **TnAbilityResourcePickup:** AddedResource **3.0** to the player's ability resource. Player pawns only. It scores a "shard" event.
- These are not energon (power): power comes only from PowerReward (§3).

### 7.4 Weapon data in Escalation (C, TnWeapon)
- `GetWeaponDataByType(t)`: 1 → PlayerData, 2 → AiData, 3 → MultiplayerData, **4 → SurvivalData**, else none. There's no fallback
  inside it.
- **The fallback is in `TnWeapon.GivenTo`:**
  - for a player pawn: WDT = Game.DesiredWeaponDataType() (Escalation 4); **if that weapon's data for WDT is none → WDT = 1
    (PlayerData, the campaign tuning)**;
  - AI pawns always use 2 (AiData);
  - then SetWeaponDataType(WDT) → SetData.
- So in Escalation the 6 weapons with *_SV_WEPDATA use it, and **every other weapon, including the starting AssaultRifle, uses its
  PlayerData (campaign values), not MultiplayerData.**
- The same rule applies in versus (WDT 3): a weapon without MultiplayerData would fall back to PlayerData (MP_VS_SP note: MP
  weapons have MultiplayerData).
- Note: an "unset" SurvivalData on a cooked class default means it's inherited from the parent class. It's none unless a parent
  sets it.

### 7.5 BrokenHope nav-cell start state (C, level Kismet; for AssetTools' navmesh export)
- The 30 cells that the generators turn on but that are cooked unblocked (escalation_navmesh.json
  `start_state.turned_on_but_not_blocked`) are **turned off at match setup by the level script**:
  - 9 `SeqEvent_PreGameplaySetup` events fire `SeqAct_Toggle` **Turn Off** on the same Toggle ops whose **Turn On** input the door
    generators drive: Toggle 8301, 2189, 5486 (×2 events), 7257, 11121, 406, 2049, 11075;
  - **all 30 cells are covered.**
- So at runtime every door / bridge cell starts **blocked** until its generator is bought, as on Remnant. Remnant cooked them blocked
  instead.
- **Bridge cells also have remote events:** TransformBridge1 / 2 / 3 → Turn On, UntransformBridge1 / 2 / 3 → Turn Off, on cells
  13225 + 8719, 6269 + 9351, 3237 + 9411. TransformBridgeN is fired by the bridge generators (13439 / 15252, 11466, 637). The
  Untransform events have no in-match sender (the bridges never close).
- **Rule for the export:** start state = cooked bBlocked **OR turned off by a PreGameplaySetup chain**.
