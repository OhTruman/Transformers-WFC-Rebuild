# DLC Escalation map mechanisms: District and Tranquillity (RE, 2026-10-23; for Gameplay's director)

**Labels:** C = DLC authored.db Kismet; H = inferred.

## 0. Export fixes (C)
- **Local remote events:**
  - `SeqAct_ActivateRemoteEvent.bLocalEvent = true` on the `*_LocalEvent` actions. Every instanced prefab copy carries its own
    action and event pairs with identical short names.
  - **Scope:** a local event only reaches SeqEvent_RemoteEvents **in the action's own sequence** (H: native; the authored flag
    plus five identical copies only make sense that way).
  - `tools/escalation_export.py` and `tools/esc_kismet_extra.py` now apply that scope.
- **Ops identified by sequence:** every downstream op and event now carries **`seq`**, its sequence path below Main_Sequence, so
  instanced copies are told apart.
- **Delay default:** a Delay with no authored Duration gets the CDO **1.0 s**.
- **Regression:** the base BrokenHope / Remnant exports change only by the added fields (one Delay default in Remnant).

## 1. District laser defense grid (5 identical sets; C)

| Set | Generator (LaserDefenseGridGenerator: 200 power, drain 75) | Button (TnInteractButtonReusable) | Damage volume | Activation TriggerVolume |
|---|---|---|---|---|
| Door_Lasers | 7381 | 11476 | DynamicPhysicsVolume_10120 | per set |
| Door_Lasers_0 | 11492 | 15142 | 2841 | |
| Door_Lasers_1 | 1022 | 944 | 3986 | 12287 |
| Door_Lasers_2 | 15098 | 9997 | 11907 | |
| Door_Lasers_3 | 7702 | 4078 | 4440 | |

- **Volumes:**
  - all five are `bPainCausing`, **500 damage/s, TnDamageTypeRust**, bPhysicsOnContact;
  - **all five are toggled off at PreGameplaySetup** (root Toggle_14580: 11907, 3986, 10120, 4440, 2841).
  - H, stock PhysicsVolume pain: it hits any pawn inside, horde and players alike; no team filter is authored.
- **Bought (generator New Power Level, `== 1` = fully powered), within its own set only:**
  1. LaserGeneratorUsed_LocalEvent → "lasers on":
     - damage volume and 9 emitters on;
     - the beam loop sound;
     - **the 45 s timer (Delay_5235)** starts;
     - the button matinee plays in reverse: after the "Enabled" event, **Delay_1730 3 s** → the button becomes usable (the real
       interact is shown, the fake switch animates).
  2. The generator's activation TriggerVolume (e.g. 12287) is turned off, so it can't be re-bought while active.
- **Ending, at 45 s or on the button's End Use (which stops the timer):**
  - LaserGeneratorTimeExpired_LocalEvent → "lasers off" + the stop sound;
  - the activation volume is back on;
  - **ModifySurvivalGeneratorPower −200** brings the generator back to 0, so **it can be re-bought**;
  - the button matinee plays forward: "Disabled" at 1.81 s → **Delay_11110 3 s** → the interact toggled off.
- **So the button is a manual early "lasers off"** (Begin Use also starts the button-down matinee). The lasers block a passage
  (Door_Lasers) for up to 45 s.
- **Level start:** GameplayStarted → **Delay_9858 1.0 s** (CDO) → the same button-down / disabled path. Lasers are off and the
  button is unusable until bought.
- Console "laser" is debug.

## 2. District sentries (C)
**Common to all:**
- Spawned at **PreGameplaySetup** by `SeqAct_TnPawnFactory` (TnActorFactorySentryAI, AlertToPlayer, not a melee target) at a
  SkeletalMeshActor spot.
- On Spawned: **SetDormant on, ToggleGodMode on, SetTargetableByAi**. **God mode is never turned off.**
- **Woken by buying its generator** (New Power Level):
  - the activation volume goes off and the emitter on;
  - **SetDormant off**;
  - a timer starts. When it ends: dormant again, the generator power is reset (re-buyable), the volume back on.

| Sequence | Template | Generator (key, power) | Active |
|---|---|---|---|
| Sentry_0, 1, 2, 3, 5, 6 | WEP_SentryBlaster01_p.**LightSentry_TURRETDEF** ("ion sentry") | 4356 / 9559 / 2342 / 9740 / 7105 / 7283, IonSentryGenerator 200 | **45 s**, then −200 |
| Sentry_4, Sentry_7 | **RepairSentry_TURRETDEF** | 2249 / 13380, RepairSentryGenerator 500 | **90 s**, then −500 via a Gate with **AutoCloseCount 3** |
| Repair_Sentry (2) | RepairSentry_TURRETDEF ("healing sentry beam") | 1070 / 8605, RepairSentryGenerator 500 | 90 s, then −500 (unlimited) |

- **Sentry_4 / Sentry_7 are limited to 3 uses:**
  - after the 3rd expiry, Increment (B 3) turns the activation volume and emitter off for good;
  - plays OVERSHIELD_AI_POWERDOWN;
  - sets the generator's "door" colour red (Set_Door_Colors_1).
- The Repair_Sentry console path also drives TnTransformingWorldActor_7159 (debug only).

## 3. District Power_Outage (C)
**Enabling:** `TnSeqEvent_SurvivalEvents_9946` (Round Begin) starts **disabled**. The 4th Round Begin (Increment B 4) enables it
("ENABLE POWER OUTAGE").

**Each Round Begin from then on:**
- Gate_9942 → **Delay 5 s** → RandomSwitch_195 (4 links, looping):
  - Link 1 → **PowerOutage**;
  - Links 2 and 3 → nothing;
  - Link 4 → Gate_15340 (starts closed) → PowerOutage.
- So the chance is **25 % per wave**.
- **From the 10th Round Begin:** Gate_15340 opens and the lockout becomes **300 s**, so the chance is **50 %**.

**PowerOutage** (RemoteEvent, **MaxTriggerCount 3**, at most 3 outages per match):
- **Lockout:** Gate_9942 is closed for **180 s** (300 s after wave 10), then reopens.
- **Effect:**
  - `DisableGenerators` "TurnOff": the **63 generator activation TriggerVolumes** (every purchase point, the laser and sentry
    ones included) plus one object var are off;
  - PostProcessVolume_4388 on / 3052 off (the dark grade);
  - the power-down lights sound;
  - Interp_6320 forward (the vending / health / ammo screen and wall-switch materials go dark);
  - a random "power failed" switchboard line (5, no repeats), after 1.25 s.
- **After 60 s (Delay_7453):** "TurnOn" (the volumes back on), the PPVs swapped back, power-up sound, Interp_6320 reversed, a random
  "power restored" line (6).
- Already-active lasers and sentries keep running. Only buying is blocked.

## 4. District Vending_Machines (C)
- 30 sub-sequences, each a `SeqEvent_PickupStatusChange` hide / unhide of the machine's pickup visuals.
- **Nothing beyond what your generator flattening does.** The outage darkens them via Interp_6320 (materials only).

## 5. District Detpack_Doors (C)
- **TnInteractDetpack_12501 / 14838:** a player interact.
  - TouchDistance 500; **no cost, no generator**.
  - Plant animation: Detpack_Release on the user, then the Countdown_Detpack_* out-animation, blocking.
  - Then the Kismet **BaBoom** fires and the interact **disables itself** (single use).
  - The timer is the countdown anim's length. H: a few seconds; the clip isn't measured here.
- **12501 BaBoom:** CauseDamage 1000 (TnDamageTypeAux1) on TnStaticDestructibleActor_3947, plus NavigationCells 445 / 14240 on.
- **14838 BaBoom:** 1000 on 171, then **Delay 1.0 s**, then 1000 on 11555, plus 8 NavigationCells on (7004, 14658, 7594, 2583,
  3328, 13619, 9197, 12845).
- Those nav cells are **off at PreGameplaySetup**.

## 6. Tranquillity Flood_Gate / BaseDoors (C)
**Three FloodgateGenerators** (3589, 15956, 14278: 300 power, drain 150). Each purchase:
- **resumes the drain matinee Interp_5361** (10.03 s; Energon InterpActor_1564, DynamicPhysicsVolume_672, height fog 15690). Its
  events **Pause_1 at 1.76 s and Pause_2 at 3.49 s** feed its own Pause, so:
  - purchase 1 plays 0 → 1.76 s;
  - purchase 2 plays 1.76 → 3.49 s;
  - **purchase 3 plays 3.49 s → the end (10.03 s)**;
- plays its own waterfall and splash matinee (~1 s) and a material matinee;
- AUDIO_DRAIN_0n;
- turns off its TriggerVolume + DynamicPhysicsVolume pair (10055 / 14764, 6407 / 2157, 9646 / 576).

**Drain Completed (after all three):**
- TnSurvivalZoneVolume_64 and 7748 on;
- 10 + 71 navigation cells / path nodes on;
- TriggerVolumes 4313 / 6182 / 12944 on;
- 11 destructibles unhidden;
- **FogOn**;
- **BaseDoors "OpenBase"**.

**BaseDoors** (Warpath gates 1 and 2):
- At PreGameplaySetup both matinees (6.67 s) start at **1.2 s** and pause at their **"Pause" 1.25 s** event: held closed.
- OpenBase resumes them. At **"Opened" 4.35 s**: DynamicBlockingVolume_8499 / 9335 off and the pistons hidden.
- One-way: there's no close path.

All of this is at PreGameplaySetup or flows from the generators. The nav, zone and trigger initial states are the PreGameplaySetup
toggles in the export.

## 7. Spawn-point placement rules, all ESC maps (C, TnSurvivalSpawnFinder / TnSurvivalZoneVolume)
- **Spawn points are `TnSurvivalSpawnPoint` actors.**
  - A zone's `PossibleSpawnPoints` = every TnSurvivalSpawnPoint **in the same level whose Location lies inside the zone's brush**
    (`PopulateStartLocations`, run at editor save).
  - The shipped arrays are authoritative, and **many zones have none.** These are "presence" zones that only change other zones'
    scores while a player is inside (`ZoneChanges`).
- **Finding a spawn spot** (`FindSpawnLocation(type)`):
  1. **Candidate zones:** only zones that are **Active**. They're re-sorted by score for the spawn type (MSVC qsort,
     escalation_zone_order.json).
  2. **Pick a zone:** a random one among the **top NumTopZonesToConsider**, weighted by |score|.
  3. **Pick a point:** the zone's next point, round-robin (SpawnPointIndex++).
     - A zone with no points → "Null spawn point on zone", and it retries.
     - **Up to 5 tries per spawn.**
  4. **Spawn location** = **point Location + (0, 0, |pawn half-height − point cylinder half-height|)**. **There's no navmesh
     projection.**
  5. **Validation:** `IsSafeSpawnLocation(location − half-height, radius, 2·half-height)` (capsule clear).
- **Score:** each zone's score per type = its own SelfModifier + the ZoneChanges of zones that currently hold players. An occupied
  zone itself gets OccupiedZoneScore −999.
- **Nav (C for script, H for runtime):** the original never needs a spawn point to be on the navmesh. The pawn appears at the point
  and the AI re-anchors to the nearest nav from there.
  - **For the rebuild:** if a point is off our navmesh, project it to the nearest nav polygon within about the capsule radius **for
    pathing only**. Keep the authored spawn location.

## 8. Pulse (Pack 2): zones, generators and Power_Generator_Action (C)
**Zones:**
- 33 total, **27 Active at start**, but **only 14 zones have spawn points** (2 each, 28 points). 8 of those are Active at start:
  10037, 11669, 12652, 14467, 2523, 2683, 3036, 6091.
- **No zone has a non-zero SelfModifier.** All spawning is steered by **presence zones'** ZoneChanges. For example, the centre
  presence zone 5389 ("CenterAreaNothingUnlocked") gives +100 Standard / +1000 Air to 12652 and 2523, +100 LongRange to 11669 and
  3036, and +100 Boss to 6091 and 14467.
- **The initial sort's top entries are 0-point presence zones**, so at a score tie the finder retries (expected). With a player
  inside, the boosted zones lead.

**Power_Generator_Action** (one prefab `Zone_Manager` reused 10×; each instance is bound to its own zone, nav and volumes):
- **Inputs:** `Generator Used`.
- **Zone_Manager (template):**
  - TransformWorldProp ("Open Doors"; its target is unbound in the template);
  - **"Disable Generator Volumes" off**;
  - **"Enable Survival Zones" on**;
  - **NavMeshCells on** (they're **off at PreGameplaySetup**);
  - then FinishSequence → output **"Open Matinee Door"**.

| Instance | Fired by | Enables zone | Volumes off | Matinee / extras |
|---|---|---|---|---|
| Zone_Manager | **LowerRampGenerator** 2403 (1500, drain 500) | – | TV 16245 | Lower Windows (Interp_4544) + **Lower Ramp** (Interp_54) + **FinalRoomUnlocked** |
| _1 | Door 15987 (500) | 14658 | TV 12943 | Interp_5622: spikes; at "BlowUpGenerator" destroys generator 15987 + emitter; at "OpenSideDoors" removes the force field; after 0.3 s destroys the spikes |
| _2 | Door 7312 (500); also remote **OpenInitial3LeftSideDoors** | 13164 | TV 1506 | door Interp_7549 |
| _3 | remote **LeftSideBackwardProgression** | 2452 | – | door Interp_12776 |
| _4 | **Gate_8203** (first of Door 7876 (750) / Door 2441 (300); the gate closes itself) | 1577 | TV 3883, 12021 | door Interp_11165 |
| _5 | **Gate_13329** (first of Door 7554 (750) / Door 11205 (300)) | 1577 | TV 8362, 1001 | door Interp_11287 |
| _6 | remote **RightSideBackwardProgression** | 5643 | – | door Interp_14950 |
| _8 | Door 1963 (500); remote OpenInitial3LeftSideDoors | 2452 | TV 7603, 6698 | doors Interp_12613 + TransformWorldProp 10778 (then destroyed) |
| _9 | Door 15272 (500); remote OpenInitial3RightSideDoors | 5643 | TV 6212, 14773 | doors Interp_7793 + TransformWorldProp 7288 (then destroyed) |
| _10 | Door 7636 (500); remote OpenInitial3RightSideDoors | 7345 | TV 2438 | door Interp_542 |

**Senders:**
- Door **1460 (150)** → **OpenInitial3LeftSideDoors** (_2 + _8) + TV 6698 off.
- Door **7911 (150)** → **OpenInitial3RightSideDoors** (_9 + _10) + TV 14773 off.
- Door 2441 → LeftSideBackwardProgression (_3).
- Door 11205 → RightSideBackwardProgression (_6).
- Console "opendoors" opens all of them (debug).

**Other Pulse Kismet:**
- Healing_Platform, Nexus_Attack, repair / rocket / eye sentries, Tank_AI_Helper_Logic, SpaceVignettes, Trypticon_Dialog. See
  `MP_ESC_Pulse_kismet_extra.json`.

**Waves:** 30 rows (MP_ESC_Pulse.json). Soldier_Tank EST_Boss in waves 0–1, 13–19 and 20+ (×2). The buffs reach
DecreaseDamageTaken 11 / IncreaseDamage 11.

## 9. Static (Pack 2): zones and generators (C)
**Zones:**
- 34 total, **9 Active at start**: 11652 (no points), 12636 (6), 14072 (1), 2233 (1), 3053 (2), 4423 (2), 6232 (4), 948 (3),
  9809 (2).
- 26 zones have points (80 points in all); 8 presence zones have none.

**Generators:**
- **Every zone change is a DoorGenerator chain** (root level, ~40 generators at 225 / 300 / 550 power). Each one turns **On** a set
  of zones (Toggle "Turn On"), and many share zones.
- **Turn On is idempotent:** a zone stays on once any generator has opened it. There's no Turn Off.
- The **starting cluster** (11652, 12636, 14072, 2233, 3053, 4423, 6232, 948, 9809) is re-enabled by generators 8564, 6094 and 453.
  Those chains are redundant at start.
- The full per-generator zone lists are in MP_ESC_Static.json `events[].downstream` (Toggle ops with their zone vars).

**Other Static Kismet:**
- FIREJETS01–16 / FIREEXP01–10 hazards (Interp + remote events), FIREJET_DMG (damage toggle), TOGGLE_FIREJETS_OFF, Cameras. See
  `MP_ESC_Static_kismet_extra.json`.

**Waves:** 21 rows; Autobot horde.
