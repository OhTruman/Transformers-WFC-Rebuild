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
