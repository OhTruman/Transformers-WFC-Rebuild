# Campaign set pieces: Omega Supreme (D5), Trypticon (A5), soldier tanks, detpacks

WFC RE lane, 2026-10-18.

Sources:
- script: TnOmegaSupremePawn, TnSentryGroup, TnAiActionHealWithGrabbedObject, TnAiActionGrab, TnAiActionLockOnTarget,
  TnAiActionOmegaDeath, TnOmegaBatteryDestructibleActor, SeqAct_TnCorruptOmegaBattery, TnSeqAct_SetOmegaStage,
  TnTrypticonPawn, TnAiActionTrypticon*, SeqAct_TnSetTrypticonStage / TrypticonVolumes, TnTankAiPawn;
- authored data (ROBODEF extensions, CDOs).

**There is no native boss code.** All boss logic is UnrealScript on top of the generic AI, destructibles and Kismet. Everything
below is CONFIRMED script / data unless marked.

**Event payload:** every boss event is `TriggerEventClass(Event, Instigator = the boss's controller, OutputIndex)`. The output index
is the only payload.

## 1. Omega Supreme (D5_OME): `TnSeqEvent_OmegaStatus`

**Outputs:**
- 0 Healthy
- 1 Wounded
- 2 Critical
- 3 Dying
- 4 GrabbedBattery
- 5 ShootingRockets
- 6 Corrupted
- 7 SentryDestroyed
- 8 LockingGrabOnEnemy
- 9 HeartArmorDestroyed

**Setup:**
- On possession: invulnerable and pseudo-god. The plan runner is **suspended until the intro finishes** (simulated event
  Introduction: anim `Omega_Supreme_Intro`, power ramp 15 s). `TnSeqAct_SetOmegaStage` also ends it.
- **Sentries** (a TnSentryGroup on his body; **`_KillCountThreshold = 4`**) start invulnerable and dormant.
- A **heart destructible** (`DES_InternalChest`, at `HeartSocket`, `_Invulnerable`) carries piece **`HeartShell`** and an
  OmegaHeart objective marker.
- Marker text keys: `D5_OME_Mark02` (sentry), `D5_OME_Mark03` (heart).

**The loop:**

| State | Enter | Player must | Leads to |
|---|---|---|---|
| **Healthy** | `Sentries.Heal()`: sentries vulnerable, healed, kill count reset → **0 Healthy**. Omega invulnerable; sentries targetable by AI; Omega not | destroy sentries. Each kill while Healthy → **7 SentryDestroyed** | 4th kill → emote + **Wounded** |
| **Wounded** | **1 Wounded** (health status 1), simulated "wounded" (power 0.06) | — | Omega's AI locks onto a battery (**8** on lock, **4 GrabbedBattery** on grab) and runs **HealWithGrabbedObject** |
| Heal with a **clean** battery | "healthy heal" anim; at the end, sentries heal | **corrupt the battery first**: damage it with a damage type whose DestructionTrigger is `TnCorruptionDestructionTrigger`, or Kismet `SeqAct_TnCorruptOmegaBattery` | back to **Healthy**, `_NumHealsPerformed`++ |
| Heal with a **corrupted** battery | `Omega.Corrupt()`: **6 Corrupted**, Omega **vulnerable** and AI-targetable; corruption destructibles attach over time (`CorruptionDestructibles[].AttachTime`) | damage Omega | Health ≤ 1 → **Critical** (he can't die from damage: clamped by going invulnerable at 1 HP). If the heal ends while not Critical → sentries heal → Healthy |
| **Critical** | invulnerable, **2 Critical**, emote; the heal action throws the battery | **destroy the heart armor** (`HeartShell` leaves state `Healthy`) | **9 HeartArmorDestroyed** (marker removed, Reaction 4) |
| Death | AI action `OmegaDeath` (requires Critical **and** heart armor destroyed): death anim, then death idle, then Reaction 10 | — | `RobotDying` → **3 Dying** (power ramp to 0 over 10 s) |

- Rocket barrage: **5 ShootingRockets** when it fires.
- `SetStage(0)` (Kismet) = intro done + heal sentries. `SetStage(1)` = intro done + jump to Critical (the checkpoint resume).

## 2. Trypticon (A5_TRY): `TnSeqEvent_TrypticonEvents`

**Outputs:**
- 0 Left Tail Smash, 1 Right Tail Smash, 2 Left Tail Strike, 3 Center Tail Strike, 4 Right Tail Strike
- 5 Left Shoulder Destroyed, 6 Right Shoulder Destroyed
- 7 First Chest Destroyed, 8 Second Chest Destroyed
- 9 Back Exposed
- 10 First Back Destroyed, 11 Second Back Destroyed, 12 Third Back Destroyed
- 13–17 Test Attack 1–5

**Stages are driven by Kismet** `SeqAct_TnSetTrypticonStage(Stage)`. Each stage swaps in the stage's Stance, ActionSet and GoalSet
from the pawn's `Stages[]` data. `SeqAct_TnTrypticonVolumes` registers 5 tail trigger/damage volume pairs (L/R smash, L/F/R
strike).

| Stage | Weak points | Player must | Events |
|---|---|---|---|
| **1A** | Shoulder cannons = a sentry group (dormant; overheat FX `Shoulder_Overheat_FX` during cooldown) | destroy each shoulder cannon | stun reaction `HitReaction_Stun_L/R`; **after the reaction ends** (Reaction 9) → **5 / 6** |
| **1B** | Left and right chest destructibles (`CP_TrypticonL/RChest_DSYS`, piece `DestructiblePlate`). The AI action ExposeChest opens them (CustomTriggerA, vulnerable); HideChest closes them (TriggerB, invulnerable) | destroy both while exposed | first → **7**; both → Wounded goal set + **8** (after the reaction) |
| **2** | Head mortar triggered; tail sentries (dormant); 3 back generators (`BackGenerator_DSYS` at Center / Right / Left back sockets) | wait **ExposeBackDelay = 45 s** → **9 Back Exposed**; then destroy the exposed generator, which gets a Destructible marker; the next one is exposed after each | **10 → 11 → 12**. After the third, the plan runner is suspended (Kismet finishes the fight) |

**Tail attacks:**
- The first attack whose trigger volume contains the target plays its anim and fires **0–4**.
- After `DamageDelay`, every non-team pawn inside the paired damage volume takes the blueprint Damage.
- **Scripted attacks** fire **13–17**.
- A reaction event is queued until the reaction anim finishes, then the idle action set comes back and the event fires.

## 3. Soldier tanks (TnTankAiPawn; `TnSeqEvent_TankStatus` + HealthStatus)

**Setup:**
- Vehicle-form giant tank: cylinder **radius 1210, height 850**; robot movement disabled while in tank form.
- Spawned on its body:
  - a **faction fuel cell** destructible at `FuelCell_Socket` (piece `FuelCell`, highlighted, FuelCell marker);
  - armor destructibles `…Front_Stage01 / 02` and `…Back_Stage01` at `DSYS_*` sockets.

**Fuel cell:**
- Piece state `HeavilyDamaged` → **0 FuelCellDamaged**.
- Final state → **1 FuelCellDestroyed**, plus:
  - **vulnerable** (H: invulnerable until now);
  - Reaction 3, health status Wounded, `TnSeqEvent_HealthStatus` Wounded;
  - **vehicle movement disabled and robot movement enabled** (it fights on as a robot);
  - the fuel cell is removed.
- **Player:** shoot the fuel cell (the targetable location points at it while it is intact), then kill the robot.

## 4. Detpacks, flight and rail sections

- **Detpacks:** `TnInteractDetpack` (TransContent interact). Its interact sequence's `TnInteractActionFireDetpackEvent` fires
  **TnSeqEvent_DetPack "BaBoom"** on the interact actor. Everything else (the explosion, destruction) is Kismet and data.
- **Flight / rail sections:** **no dedicated code found** in script (absence of evidence). Flight sections use normal plane form;
  sequencing is Kismet (streaming, matinee, factories).

## 5. Data vs code

- **Code** (script, above): the state machines, thresholds hard-coded in script (Omega: Critical at Health ≤ 1; the order of
  Trypticon's back events), and the event indices.
- **Data:**
  - kill threshold 4, ExposeBackDelay 45;
  - destructible blueprints and piece names, sockets;
  - stage Stance / ActionSet / GoalSet arrays, attack lists (trigger volume + anim + Kismet event), tail Damage / DamageDelay;
  - the heal blueprint's CorruptionDestructibles;
  - power and anim names in Omega's SimulatedEventsBlueprint.
- **Kismet:** stage changes (SetOmegaStage / SetTrypticonStage), battery corruption (scripted), volume registration, and the
  fight's start and end.
