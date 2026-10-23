# Campaign bosses and minions: per-chapter decode (RE, 2026-10-23)

Style of MP_ESCALATION §9.
Labels: C = authored data (AI sets, ROBODEFs, Kismet) or TransGame script; H = inference.
Sources: authored.db, AssetTools `next_map/<ch>_{kismet,pawn_factories}.json`, work/script/decomp.
Generic AI behaviour (GOAP planning, threat evaluator formulas, Navigate / ShootAtTarget / Dodge…) is in MP_ESCALATION §9.1 and
CAMPAIGN_AI_LAYER_RE. Only boss-specific parts are here.

---

## A1_IAC: Starscream mini-boss (Deco End, "Starscream_Mini-Boss_Fight")

### Template (C)
- **AICD:** `StarScream_Boss_AICD`, CharacterType CType_Boss.
- **Chassis:** `StarScreamBoss_ROBODEF`:
  - **Health 4000**; ground and air speed 1200; collision 175 × 225;
  - acrobatics StarscreamBossAcrobatics; vehicle form StarScreamBossVehicleForm; no health-pickup drop.
- **Health monitor:** **Wounded at 50 %** (2000 damage taken).
- **Damage-threat bands:** damage taken in a 3 s window, as a fraction of health: Low ≥ 0.005, Medium 0.04, High 0.06,
  Extreme 0.08.
- **Weapon overrides:** robot primary = **TnWeaponStarscreamShotgun**; vehicle = TnWeaponStarscreamRocketVehicle.
- **Goal set** `TR_AiGoalSet_Boss`, in priority order:
  UseInteract (high), React, ReactToStimulus (high-priority enemies), ScriptedMoveTo, **StayInTacticalVolume**, **HealSelf**,
  DefendSelf, Heal, Kill, UseInteract (low), MoveToObjective, ReactToStimulus (low), Idle.

### Targeting (C, JetBoss_AiSensorSet)
- Threat = Attacker + Damage + Distance (stock defaults: MP_ESCALATION §9.1) + **StunnedEnemy 5.0** + **Player 0.5** + Cloaked.
- The +5 for a stunned enemy means **he switches to whoever his flash-bang just stunned.**

### Phases (C: every action carries a HealthStatus precondition)
- An action with no HealthStatus value requires **HS_Healthy**; "Wounded" requires **HS_Wounded**.
- So the fight is two exclusive move sets, switched by the 50 % health monitor.

| Phase | Form | Actions (blueprint parameters) |
|---|---|---|
| **1: Healthy (> 50 %)** | **jet** (TransformToVehicle) | ShootFromAirTactical with StarscreamRocketVehicle (InvalidateDuration 5 s); ShootAtTarget (vehicle rockets); **BarrelRoll** when damage threat ≥ Low, every **3–4 s**; **ScriptedBombingRun** (Kismet-requested, below); vehicle navigation |
| **2: Wounded (≤ 50 %)** | **robot** (TransformToRobo) | ShootFromTactical / ShootAtTarget with the **Starscream shotgun**; **ThrowGrenade FlashBangs** every **20–25 s**, only while the enemy is **not** already stunned; **Rocket Barrage "Death Blossom"** every **10–15 s**; UseInteract; robot navigation |
| both | – | Dodge, StepAway, ScanningIdle, StopAndCooldownWeapon |

**Rocket Barrage / Death Blossom** (TnAiActionRocketBarrageDeathBlossom, C):
- **Preconditions:** a target, a weapon, LOS to the enemy, **robot form**, on the ground. Also needs vertical room: a trace up by
  JumpHeight with the collision box must be clear, else it fails.
- **Start:** plays the RocketBarrage dialog; focuses and aims at the target; **Hover()**.
- **Once hovering:** plays anim `Shooting_RocketBarrage_Hover` (high priority). After **0.94 s**, fires
  **TnProjectileForwardMangaRocket** alternating sockets `MangaRocket1Socket` / `MangaRocket2Socket`, one every
  **0.03–0.06 s**, pitch offset **−70° to −20°**, until the anim ends.
- Fails if he lands mid-attack (acrobatics state 8).

**Scripted bombing run** (TnAiActionScriptedBombingRun, C):
- **Requested by Kismet** `TnSeqAct_StartBombingRun`:
  - it picks a random `TnAiScriptedBombingRunInfo`;
  - sets the AI world state BombingRun = 1 (Requested) with that info as subject, and forces a replan.
- **The action then:**
  - **teleports** him to the instance's StartingPoint (here AirPathNode_16136);
  - collision mode off, Enemy cleared, physics None;
  - plays the root-motion (RMM_Translate) animation `Nav_BombingRun_01_VEH`;
  - on finish clears BombingRun and restores collision.
- **Phase 1 only.** The run itself is an authored animation path.

### Kismet flow (C, A1_IAC_DecoEnd_Design_m)

| Step | Trigger | Action |
|---|---|---|
| 1 | Togetherness volume touch (Gate, once) | intro matinee (skippable; Completed **or** Aborted continue), cinematic mode, world-prop transform, remote events: music `MUSIC_MX_STARSCREAM_FIGHT`, `PlayerEnteredCommandRoom`; looping ambient matinees (dropships, jets, explosions) |
| 2 | matinee end | **PawnFactory spawns Starscream** (factory form override TF_Robot; Phase-1 rules transform him to jet at once); AttachToEvent (his damage events); `AI_STATES_IN_COMBAT_MUTE`, `START_BOSS_FIGHT_MUTING` after 0.1 s; conversation after 1.8 s |
| 3 | **TakeDamage ≥ 400** (MaxTriggerCount 3, ReTriggerDelay 20 s) | Gate → **StartBombingRun** → random taunt (one of 2 conversations). **At most 3 runs, ≥ 20 s apart**, each needing 400 more damage, and only while Healthy |
| 4 | **TakeDamage ≥ 1250** | taunt "A lucky shot! Now…" (two lines) |
| 5 | Health monitor at 50 % (2000) | **Phase 2** (robot set) |
| 6 | **TakeDamage ≥ 3950** | **`TnSeqAct_DownRevive` on Starscream** (he goes down: not killed, H story) + remote **`Start_CINE`** |
| 7 | `Start_CINE` | cinematic mode for the trio, **SetTargetableByAi(Starscream) off**, fade-to-Bink mixer, `STNG_STARSCREAM_END` after 1.24 s, **SeqAct_TnMissionComplete**, achievement unlock, level fade-out mixer |

- **Checkpoint:** `Start_From_DecoCommand_Room_Checkpoint` replays a 0.5 s director matinee into the fight.
- Console events Jet / Stage2 / Outro / AirNode are debug hooks.
- Afterburner / boss FX emitters are destroyed on abort.
- Tactical air nodes (`TnTacticalAirPathNode` ×11+) are toggled by the AirNode debug hook. In normal play they're enabled.
- **Minions:** none in the fight. Dropships and jets are ambient matinee props.

---

## A2_KON: Soundwave and his cassettes (Soundwave_Design, Main_Sequence)

### Templates (C)

| Character | AICD / pawn | Health | Sensors | Goal set | Actions (blueprint parameters) |
|---|---|---|---|---|---|
| **Soundwave** | Soundwave_Boss_AICD (CType_Boss), SoundwaveBoss_ROBODEF | **1000** | Agent set (BiggestThreat, LOS, Range) | **Engineer_Boss**: Revive first, then Heal, UseInteract (high), React, ReactToStimulus, ScriptedMoveTo, MoveTo, DefendSelf, Kill, BuddyLead, … | **AudioBlast** (TnWeaponAudioBlast; anim `Shooting_AudioBlast`; fire from **0.45 s to 4.5 s**, aim 200 UU ahead of the muzzle; every **10–12 s**); **Revive (full revive)** of downed allies; **HealFromCurrentLocation** with TnWeaponBossRepairRay, **only while GOD_On**, needs a clear trace to the heal target, not downed targets; SpawnCharacter (socket SpawnCharacterSocket, 2 s, anim `Deploy_Cassettes`, spawned minions get Downed enabled; only via Kismet `TnSeqAct_AiSpawnCharacter`, **unused here**: the cassettes come from pawn factories); Retreated, TurnToAimAtActor, UseInteract, ScanningIdle (repair ray) |
| **Laserbeak** | Laserbeak_AICD, TnMinionAiPawn, starts and revives as **jet** | **600** | Sniper set (Damage, Distance, Player, Cloaked, TargetDowned; retreat-node picker) | Agent | ShootFromAirTactical TnWeaponLaserVehicle (valid 6 s / invalid 3 s, cost 3); **BombingRun** (TnWeaponBombingRunLaser, ShootAhead 1000, valid 2000, min range Close, every **10–12 s**); **BarrelRoll every 7–10 s**; EMP reaction; ShootAtTarget robot-form path cost 9999 (effectively never); vehicle navigation prefers enemies in Critical health |
| **Rumble** | Rumble_AICD, TnMinionAiPawn | **1275** | SoldierMinion set (Damage, Distance, Player, TurretTarget, Cloaked, Attacker) | Agent | Minion HeavyPistol; **AreaOfEffect `GroundSmash_AOE`** within **1800 UU**, every **3–8 s**; **Stampede**: charge at **×3 speed** when ≥ Medium range and reachable, lock the target position at **1500 UU**, melee at **1000 UU** (type 6 if the trace hits a pawn, else 7), every **9–12 s**; Taunt (`Nav_RunCharge_Start`); EMP / stimulus reactions |
| **Frenzy** | Frenzy_AICD, TnMinionAiPawn | **850** (speed 1000) | SoldierMinion | Agent | Minion EmpShotgun; **AbilityJammer** (triggered ability; target ≥ range band 5) every **5–7 s**; **Hover** + hover attack anim (`Shooting_RocketBarrage_Hover`) every **3–5 s**; stimulus reaction every 8–10 s |

### The fight loop (C, Kismet; thresholds from the linked SeqVar_Floats)
1. **Intro:**
   - trigger volume → door world-prop transform → intro → `Soundwave_Intro_Teleport` (the trio teleports to 3 tactical nodes);
   - `Soundwave_Intro_Complete` → **PawnFactory spawns Soundwave** on his platform marker;
   - **AttachToEvent → `SetAiGodStatus GOD_On` (fully invulnerable)**.
2. **Summon rotation** (`Switch` over MinionIndex, links auto-disable, in order **Frenzy → Rumble → Laserbeak**):
   - `Start_<minion>` waits (0.5 s poll) until **`SoundwaveIsOnPlatform`**;
   - then a summon conversation + `Spawn_<minion>` → the minion's factory spawns it, and Soundwave **uses the summon interact
     emote** (TnInteractEmote_2230).
3. **Vulnerability window:**
   - a minion goes **Downed** (TnSeqEvent_Downed → `<Minion>Downed`);
   - 0.5 s later, distance Soundwave → platform marker:
     - **≥ 200 UU** (he has left the platform, H: to revive the cassette) → **`GOD_PseudoOn`**: damageable, can't die;
     - **≤ 200 UU** → stays GOD_On.
   - **`<Minion>Revived`** (Soundwave's full revive) → back to **GOD_On**.
4. **Stage ends** on his health (`SoudwaveCurrentHealth`, read on every damage event):

   | Stage | Ends at | Effect |
   |---|---|---|
   | 1 | ≤ **700** | **Frenzy recalled**: pseudo-god on Frenzy, it moves off; Soundwave GOD_On; next minion |
   | 2 | ≤ **400** | **Rumble recalled** |
   | 3 | ≤ **10** | `SoundwaveIsDefeated` → `SoundwaveDefeated` → `Start_Soundwave_Outro` |

5. **Return to platform:** `Send_Soundwave_Back_To_Platform` → `TnAiMoveToActor` to the platform node (retries on abort). If he
   isn't on it 10 s later, **teleport** there.
6. **Sentry rounds 1–3** (`Spawn_Sentries_Round_N`):
   - each corner group (BackRight / FrontRight / …) spawns one of 3 sentry factories at random;
   - sentries are deactivated **25 s** later (`Deactivate_Soundwave_Sentries`);
   - a destroyed sentry drops a random **Health / Ammo / ResourceCache** reward (10-way random switch).
7. **Checkpoints** `Soundwave2 / 3CheckpointActivated`: respawn him, pseudo-god, then **CauseDamage 300 / 600 (AOE)** to restore
   the stage health (700 / 400).
8. **Audio-blast barks:** when a player takes TnDamageTypeAudioBlast, a 2-way switch of companion lines plays.

---

## A3_COR: Protect Ratchet (Omega Gate) and the "Gigantor" tank (OmegaGate_Design)

A3 has no boss AI of its own. It has a **VIP escort / defend** and a **heavy tank**.

### Ratchet VIP (C)
- **AICD:** `Ratchet_VIP_AICD` (CType_Boss), chassis Ratchet_VIP_ROBODEF, friendly-agent sensors (BiggestThreat, LOS, Range,
  Retreated node picker).
- **Goal set:** NPC_Healer. **Actions:**
  - **Heal with the RepairRay**;
  - turret use (Enter / Leave / ShootFromTurret with MountedHeavyIonBlaster or MountedRocketTurret, TurretScanForEnemies);
  - ShootAtTarget with an AssaultRifle (robot) or RocketVehicle (car), both with the **AwayFromBuddy** cost (+10 when the target
    is away from the buddy);
  - stimulus reaction; transform both ways.

**Kismet (C):**
1. Ratchet's intro movie (`PlayRatchetIntroMovie`).
2. He's spawned (factory) and **sent to work the gate**: `TnAiUseInteract` on `TnInteractEmote_607` ("HealPosition1",
   Ratchet beam audio).
3. **Defend waves 1–3:** sub-sequences of pawn factories with `TnSeqAct_AttackFromActor` targets; each wave's All Pawns Killed
   advances. Each has a checkpoint `ProtectRatchet{,2,3}CheckpointActivated`.
4. **Ratchet health monitor** (any damage ≥ 10 → read `Health`):
   - **≤ 25 → `RatchetHeavilyWounded`**, > 25 → `Ratchet_Nominal`;
   - `RatchetDowned` / `RatchetRevived` from his downed events;
   - these gate companion barks and a damage-ticker loop.
5. **Ratchet killed** (factory Single Pawn Killed) → Gate → failure conversation + hide (H: mission fail / restart from the last
   checkpoint).
6. `LetRatchetRoamFree` / `FinalRatchetConversation` / the Omega Gate cine stop the interact and free him. Navigation fall-back
   positions: `Ratchet_MoveTo_Position_2`.

### "Gigantor": Soldier_Tank_Decepticon (C)
- **Template:** `TnTankAiPawnContent`, **6000 HP**, starts as a vehicle; action set `Tank_AiActionSet`, identical to the
  Escalation boss tank in MP_ESCALATION §9.2:
  - **vehicle phase:** TankCannonHeavy, SuppressTargetFromActor, pooled AOE slam `Melee_AOE_VEH` (10 s cooldown);
  - **robot (wounded) phase:** robot cannon, **gatling ShootingSweep** (2 s, side offset 2000, every 10–15 s), TauntWithWeapon,
    **SmashAttack ≤ 1600 UU**;
  - `TransformToRoboWoundedReaction` on Wounded.
- **Kismet:**
  - `Spawn Tank` → factory → AttackFromActor, AssignTacticalVolume, AttachToEvent, **TogglePseudoGodMode(Gigantor)**: can't die
    until toggled (H: its fuel-cell / armour destructibles handle that; SETPIECES §3);
  - **`TnSeqEvent_HealthStatus` Wounded** → `MUSIC_TANK_ROBOT_FORM`, `Tank Wounded`;
  - tank health monitor: **≤ 50** → a remote event (H: the defeat sequence).
- Checkpoints: `OmegaTankCheckpointActivated` / `DefeatGigantorCheckpointActivated`.
- Another tank in Caves_Split / Routing3C uses the same template, plus move-to and untargetable-at-spawn hooks.

---

## A5_TRY: Trypticon (per-stage action sets)

The stage mechanics, weak points and TrypticonEvents outputs are in **SETPIECES §2**. This section adds the per-stage AI
parameters and the Kismet hooks (C, from TR_Trypticon_AI_p and the A5 kismet manifests).

### Template (C)
- **Pawn:** `Trypticon_AICD` → `TnTrypticonPawnContent`, chassis Trypticon_ROBODEF.
- **Default set:** ScanningIdle only. `Stages[]` replaces the action set per stage; the goal set is always Trypticon_AiGoalSet
  (React, Emote, Kill, Idle).
- **Targeting:** sensors LOS + threat, with these evaluators:
  - **RoundRobin** (each player in turn gets threat **+3 for 7 s**; AI ignored), so he spreads attacks across the trio;
  - **TargetDowned −10**: he ignores downed players;
  - Weapon-based and Cloaked evaluators (defaults).

### Stage action sets (C)

| Stage (stance) | Action | Parameters |
|---|---|---|
| **1A** (standing) | **ShoulderCannons** | AttackLength **8 s**: `Shooting_ShoulderCannons_start` → loop `Shooting_ShoulderCannons` → `_end`; blend 0.5; cost 2 |
| | **Mouth mortar** | TnProjectileTrypticonMouthMortar from WeaponSocket_MouthMortarStanding: **5 shells**, intro 1.79 s, then one per **1.802 s**; first available after **13 s**, then every **35 s**; usage interval 12 s |
| | Reactions | React_Wounded, React_Bump (blend 0.3) |
| **1B** (standing) | **MortarFrenzy** | TnProjectileTrypticonMultipleMortar: **9 shells**, intro 1.13 s, one per **1.0 s**; first after **18 s**, then every **38 s** |
| | **TailSmash** (L / R) | **175 damage** at **6.58 s** into the anim, to every non-team pawn in the paired damage volume; impulse robot 2000 / vehicle 1000; cost 1 |
| | **ExposeChest** | opens the chest plates (blend 0.3 / 0.5); while open they're damageable (SETPIECES §2) |
| | Reaction | React_Wounded = `HitReaction_ExposedRibs_R` |
| **2** (crouching) | **TailStrike** | intro `Melee_ScorpionStrike_Start`. **Left / Forward / Right** strike: anim `Melee_ScorpionStrike_L/F/R`, volume `TRYPVOLUME_*TailStrike`, Kismet event `TRYPTICON_*TailStrike`. **AttackDelay 5.2 s, DamageDelay 4.85 s, 175 damage** (TnDamageTypeTrypticonTailSmash), impulse 2000 / 1000; cost 2 |
| | **Head mortar** | TnProjectileTrypticonHeadMortar from WeaponSocket_MouthMortar: **35 shells**, intro 2.6 s, one per **0.25 s**; first after **20 s**, then every **65 s** |
| | **ScriptedAttack** | `TRYPTICON_TestAttack1`, anim `Mangaburst_Crouch`; first after **10 s**, then every **35 s** (fires TrypticonEvents 13) |
| | **RoarEmote** | `Taunt_Roar_Crouch` / `Nav_Crouch_Idle_Spiders`; TimeBeforeFinalRoar **20 s** |

### Kismet hooks (C)
1. **Stage switches** (`SeqAct_TnSetTrypticonStage`; no Stage property = 1A):
   - 1A: intro tail-spike sequence;
   - **1B:** after the stage-1 bombing-run minimatic (`STAGE1A_POSTBOMBINGRUN` / `STAGE1_BOMBINGRUN_MINIMATIC`);
   - **2:** `STAGE1_TRANSITIONTOSTAGE2_MINIMATIC`, plus an electrocution set-up path.
2. **TrypticonEvents:**
   - tail smash / strike → remote events, and Delays that drive the `AddImpulseToPawnInVolume` knock-backs (15 in A5);
   - every weak-point destroyed → **AddCompetitiveScore** + a remote event;
   - **Second Chest Destroyed** → stage-1 → 2 transition;
   - **Third Back Destroyed** → the end of the fight (3 remote events: outro).
3. **Spiders** (27 factories) and TrypticonSentry groups (shoulder / tail sentries, SETPIECES §2) are spawned / woken by the stage
   sequences.

---

## D1_ORB: no boss; heavy "mini-boss" encounters (Neutral faction)

D1 has no boss AI. Its set-piece heavies are **Neutral** (Autobot-team) variants that use the **same action, goal and sensor sets
as Escalation**, so the behaviour is MP_ESCALATION §9.2 verbatim. Only the deltas are below (C).

| Template | Health | Sets | Deltas vs Escalation |
|---|---|---|---|
| **Brute_Neutral_AICD** (BruteNeu_ROBODEF) | **100,000**: in practice only killable through the **backpack destructible** `RB_BruteNeu_Backpack_DES` (socket Destructible_C; targeting `BruteBackPack_Targeting`) + attached shield | Brute_AiActionSet / TR_AiGoalSet_Brute / Brute_AiSensorSet | melee override **WarHammer** (the set default is HalberdAxe). Ground pound 2000–5000 UU every 15–25 s, second smash ≤ 3000 every 15–20 s, BruteRocketLauncher (not at cloaked targets; enemy-on-ground cost) |
| **Titan_Neutral_AICD** (TitanNeu_ROBODEF) | **1250** | Titan sets | primary override **GatlingGun** (the set default is HeavyPistol). Threat: Uber 5 within 1000, max range 7000, attackers −0.5, AI ×0.8 |
| Soldier_Jet / Sniper / MG Neutral | per chassis | Escalation soldier sets | as §9.2 |

**Kismet hooks (C):**
- **Assembly brute** (Main_Sequence): spawn → combat-music state; all killed → `AUDIO_ASSEMBLY_BRUTE_FIGHT_OVER`, non-combat state,
  `AIObjective_Assembly2`.
- **Data Center brute** (robot form, no spawn anim): all spawned → `AUDIO_MAP_ROOM_BRUTE_APPEARS` + an intro matinee; killed →
  `AUDIO_MAP_ROOM_BRUTE_KILLED`, **`UnlockTheBruteDoor`** (progression gate), `AIObjective_VentArea`.
- **Exterior ruins brute:** move-to on spawn; deaths increment a counter.
- **After_Ramp_2 Titan:** `DX_ENEMY_SPAWNED` / `DX_ENEMY_KILLED` dialogue cues; all killed → `BrutesDead`.
- **No god-mode, HealthStatus or damage-threshold scripting** on any D1 heavy: they fight to death as in MP.

---

## D2_COR: no boss; Security Drones (+ Autobot jets)

D2 is a flight chapter (Starscream / Thundercracker / Skywarp). Its only new enemy type is the **Security Drone**. The jets,
snipers and the single Brute use the Escalation sets (MP_ESCALATION §9.2).

### SecurityDrone_AICD (C)
- **Template:** SecurityDrone_ROBODEF, **250 HP**, **starts and revives as a vehicle** (always flying), Autobot team.
- **Action set:** ScanningIdle, plus the **RepairRay** as its weapon. Vehicle form only:
  - **ShootAtTarget** with the precondition "target in the nearest range band (Close)";
  - **ShootFromTactical** (move to a tactical node, then beam).
  - The ray hits enemies with the enemy repair squib (`Squib_RepairEnemy_FX`, WEAPON_PRESENTATION_AUDIT), so it **damages players**.
    H: it would also heal Autobot allies, but no heal action is authored.
- **Targeting** (threat evaluators): damage-based; distance (min 1500, max 10000); AI ×0.75; **view angle 60°**, so it prefers
  targets in front of it. Goal set: the generic Agent set.

### Kismet hooks (C)
- **Cave:** drones spawn in chains (each spawn triggers the next factory), go to a **TnInteractAirEmote** hover point
  (`emote 1`), then engage. Kills increment a shared counter. One drone is **attached to an actor** and made untargetable at spawn
  (SetTargetableByAi), then released by an event.
- **IntroGorge:** two drones take **CauseDamage 240** on spawn, which leaves them at 10 HP: scripted near-dead drones for the
  opening beat.
- **Chamber `InitializeRockets`:** 4 drones; each spawn toggles an emitter after a 0.3 s delay (the rocket-prep FX).
- **TNLMR / Cave:** move-to on spawn; combat-music events.
- No god-mode or health scripting.

---

## D3A_IAC: Brawl VIP (tank buddy), city tank, AA-gun defence

There's no boss AI. The novel templates are the **Brawl buddy** and the **Defender jets**. HoloBrute, Titan, Brute and Sniper are
as in MP_ESCALATION §9.2.

### Brawl_VIP_AICD (C)
- **Template:** CType_Boss, chassis Brawl_ROBODEF.
- **Action set:** Soldier_Tank_Buddy:
  - robot: AssaultRifle ShootFromTactical / ShootAtTarget (AwayFromBuddy cost);
  - **tank: TankCannon ShootFromTactical every 15–25 s**, RocketVehicle ShootAtTarget;
  - turrets (HeavyIon, Rocket);
  - ScanningIdle only in the matching form.
- **Goals and sensors:** Buddy_AiGoalSet (follows / leads: TOGETHERNESS_BUDDY); friendly-agent sensors (BiggestThreat,
  Retreated node picker).

**Kismet (C):**
- `SpawnBrawl` → factory (two instances, BrawlAI1 / BrawlAI2):
  - **BrawlAI1: `SeqAct_ToggleGodMode`, fully invulnerable**;
  - **BrawlAI2: `TogglePseudoGodMode`, damageable but can't die**.
  - The buddy can never be lost, so there's no VIP-fail path (unlike A3 Ratchet).
- **Movement:**
  - `MarkBrawl` / `UnMarkBrawl` objective markers;
  - **Buddy_Waypoints:** `TnAiSetMoveToObjective` per stage, `MoveBrawlThroughCity1Door`, `TeleportBrawl1`;
  - crowd sound beds `Start/StopBrawlCrowd`.
- **`Critical_AI_Interact` fail-safe** (a reusable sub-sequence):
  1. the AI is told to use a critical interact;
  2. after a Delay, if the interact isn't yet in use → **`TnSeqAct_DownRevive`** (revive him if he's downed) → **teleport** to
     the interact's point → use the interact.
  - This guarantees that progression-critical buddy interacts happen.

### City tank: Soldier_Tank_Autobot (C)
- **Sets:** Tank_AiActionSet (Escalation boss tank).
- **On spawn:** ShootAtActor at a scripted note target (an opening barrage), plus a switch / gate set-up.
- **`TnSeqEvent_TankStatus`:**
  - **FuelCellDamaged** → AttackFromActor (repositions it);
  - **FuelCellDestroyed** → new tactical volume, `MX_TANK_TRANSFORMS` (it transforms to robot), an int flag.
- All killed → `TankKilled`.

### DefendAAguns: Soldier_Jet_Autobot_Defender (C)
- **Defender set**, a jet that **hovers to fight**:
  - jet PlaneMachineGun ShootFromAirTactical / ShootAtTarget (JetSoldier cost modulators);
  - robot **HoverFromTactical** with HeavyMG-AIHover (min interval 4 s);
  - **ShootingStrafeFromHover**: a **4 s** strafe every **2–3 s**.
  - No strafing / bombing runs, unlike the standard jet.
- **Waves** (JetEnemies_3 / _4): each spawn is assigned a tactical volume around the AA guns and fires attack / dialogue events;
  each kill fires a delayed follow-up event.
- A `TogglePseudoGodMode` sits in the DefendAAguns sequence, but its Target variable is unbound in the data (H: no effect, or bound at runtime).

---

## D3B_IAC: the Omega Supreme ship balcony fight (Balcony_DSN, Omega_Ship_Balcony_Fight)

A **scripted boss with no boss AI**:
- the ship is an `ActorFactoryAI` pawn (`OSshipPawn`) moved entirely by **Matinee**;
- its weapons are **sentries attached to it**;
- the fight is driven by Kismet damage accumulators.

C throughout (Kismet + linked SeqVar values).

### Health and stages
- **On spawn:**
  - `TogglePseudoGodMode(OSshipPawn)`: it can't die from health; the fight ends by script.
  - `OSshipMaxHealth` = **25000**.
  - Hit threshold = Max / 20 = **1250**.
  - Stage thresholds: **8000** (stage 2), **16000** (stage 3), **24000** (death).
- **Only heavy weapons count.** Every damage event filters on HeavyIonBlaster, HeavyTankShell, MountedIonBlaster and
  MountedRocketTurret, so the players must use the balcony **turrets / tank**. Small-arms damage is ignored, except for barks
  (below).
- **Accumulators** (stock SeqEvent_TakeDamage: damage accumulates until the threshold, then it fires):

  | Event | Threshold | Effect |
  |---|---|---|
  | each **1250** (repeatable) | 1250 | a 20-way Switch toggles successive damage FX emitters + an impact sound |
  | **8000** | 8000 | `OSshipStage = 2`, a destructible hit (Aux1 100), stage-2 remote event, competitive score |
  | **16000** | 16000 | stage-3 event (`OSshipStage = 3`, **OSshipAIspawnNum = 5**), score |
  | **24000** | 24000 | **death**: toggle, competitive score; then `EndOSshipFight` |

- **Per-stage pacing:** move-rate / beam-rate pairs `OSshipMoveRate` / `OSshipBeamRate` = **0.5 / 0.4 → 0.6 / 0.5 → 0.7 / 0.6**.
  These are the Matinee play rates, so it moves and beams faster each stage.

### Attack loop
- **Gate → CompareInt(OSshipStage vs 2)** chooses a **looping Switch** of matinees:

  | Stage | Loop |
  |---|---|
  | 1 | Death Beam 1 → Rockets 1 → General Move |
  | 2 | Spawn Fliers → General Move → Death Beam 1 → Rockets 1 |
  | 3 | Spawn Fliers → General Move → Rockets 2 → Death Beam 1 |

  Each entry is a RandomSwitch over the A / B variants of that matinee. Its `Completed` → `OSshipBalconyMove` → the next loop step.
- **Matinee event keys drive the weapons:**
  - `BeamOn` / `BeamOff` → `OSshipBossBeamOn/Off` (the death beam);
  - `TurretsOn` / `TurretsOff` → **`TnAiSentrySetDormant`** on the ship's attached MG sentries and its two **big rocket turrets**
    (`OSbossBigTurret1/2`), with random sentry targets re-picked on wake;
  - `GodModeON` / `GodModeOFF` → **`ToggleGodMode` + `ToggleHealthDisplay` + ToggleTargetableComponent** on the ship: it's
    invulnerable and its health bar is hidden while it flies off / spawns fliers, and vulnerable and shown in attack positions;
  - `Spawn` → flier spawn emitters (`SpawnBalconyShipAI`).
- **Sentries:** 6 `TnActorFactorySentryAI` sentries spawned on ship skeletal-mesh spawn points (no collision check) and
  **AttachToActor**'d to the ship.

### Hooks
- **Small-arms bark:** any other damage ≥ 1 (**ReTrigger 20 s**) → compare the instigator with Soundwave / Megatron / Breakdown →
  a random "...<name>!" line telling them to use the turrets.
- **`EndOSshipFight`:**
  - stops every matinee, `DestroyOSbossSentries`, health display off;
  - the "Run Away" matinee (rate 0.3), a destructible hit + force feedback;
  - `KILL PLAYER ON OMEGA` (InstantKill for anyone standing on the ship).

---

## D5_OME: Omega Supreme (per-action decode)

The state machine (sentries → Wounded → battery heal → corrupted → Critical → heart → death) and the OmegaStatus outputs are in
**SETPIECES §1**. This section adds the AI action parameters and the Kismet battery loop (C).

### Template (C)
- **Pawn:** `OmegaSupreme_AICD` → TnOmegaSupremePawn, chassis OmegaSupreme_ROBODEF, **5000 HP**: never killed by health
  (SETPIECES §1 clamps him at 1 HP).
- **Goal set:** TR_AiGoalSet_OmegaSupreme.
- **Threat evaluators:**
  - **RoundRobin +3 for 7 s** (like Trypticon);
  - TargetDowned −10;
  - **SentryGroupDamage**: players who damage his sentries draw his attention;
  - a PawnClass evaluator for TnSentryPawn: his own sentries aren't targets;
  - weapon-based.

### Actions (C)

| Action | Weapon arm | Parameters |
|---|---|---|
| **LockOnTarget → ShootLockedOnWeapon** (missiles) | OmegaRocketArm (left-arm tracking) | lock **2 s** (high-threat warning 1 s), then `Missile_Shoot` every **1–5 s** |
| **LockOnTarget → LockedOnShootingFrenzy** | OmegaShootingFrenzyArm | tell `ShootingFrenzy_Tell`; lock 2 s, warning 1 s; `Melee_ShootingFrenzy` every **25 s** |
| **RocketBarrageOmega** | – | `Melee_RocketBarrage` (blend 1.5) every **15 s**; fires OmegaStatus 5 |
| **AreaOfEffectAttack** | – | `Melee_AOE` when the target is within **3200 UU** |
| **LockOnTarget (grab) → Grab → ThrowEnemy** | OmegaTractorBeamArm | **4 s** warning (lock-on threat shown), **needs LOS** (second LOS socket WeaponSocket_Primary), every **20 s**. Tractor: speed 3000 / accel 5000, lift 1 s at 1000 / accel 2000, rotation 180°/s, attached at WeaponSocket_R. `Melee_ClawCrush_Omega`, release at **5.3 s**; detach **1000 damage** (TnDamageTypeOmegaTractorBeamGrab), impulse 1e6. Fires OmegaStatus 8 |
| **LockOnBattery → HealWithGrabbedObject** | tractor beam | healthy heal `HitReactions_React_HealingBattery_F`; corrupted heal `…CorruptBattery_F`, with corruption spikes attaching at **0 s (hand), 1.5 s (arm), 3.0 s (shoulder)** |
| **ThrowDestructible** | – | `Melee_BatteryThrow`, release at 1.0 s (throws the battery away when Critical) |
| **ExposeHeart** | – | `HitReactions_React_FallToKnee_F`; **ExposeHeartDelay 23 s** |
| EquipWeapon ×3, Emote (`HitReactions_React_Stand_F`), OmegaDeath, ScanningIdle | | |

### Kismet battery loop (C, D5_OME_DSN Main_Sequence)
- **OmegaStatus Healthy** → every `Battery_0N` sub-sequence's "Healthy Destroy Battery" input:
  - the battery destructible is (re)spawned by its `TnActorFactoryOmegaBattery…` factory;
  - `ToggleBatteryToUncorrupted` resets its material.
- **OmegaStatus Wounded** → **`Battery_Randomness`**: one of Battery_01 / 02 and one of 03 / 04 are picked at random. Each picked
  battery:
  - plays its spawn matinee (2.53 s);
  - opens the gates of its **6 pawn factories** (`MaxNumSpawnedInWorld` 15: the Autobot jet waves);
  - competitive score.
- **Corrupting a battery:** a player uses `TnInteractCorruptOmegaBattery` (Begin Use) → `SeqAct_TnCorruptOmegaBattery` →
  `CorruptBatteryMaterial`; VO lines for the 1st–4th corruption.
- **GrabbedBattery** → `OMEGA_GrabbedBattery` + each battery's "Grabbed Battery" input (hide it, toggle FX).
- **Corrupted / Critical / SentryDestroyed / LockingGrabOnPlayer** → dialogue barks.
- **HeartArmorDestroyed** → a remote event, competitive score, dialogue.
- `SetOmegaStage ×3` for the stage / checkpoint resumes (SETPIECES §1).
