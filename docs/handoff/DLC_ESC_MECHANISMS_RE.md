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
