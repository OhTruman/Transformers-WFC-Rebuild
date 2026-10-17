# MP ability sound / FX cues: event, actor, replication (RE, 2026-10-18)

Labels: CONFIRMED (C) = TransGame script bytecode + authored CDOs; HIGH (H) = stock UE3 engine semantics.
Abilities use their **class CDO values**: no MP archetype overrides any cue property (authored.db: 0 non-default ability or buff
objects carry a sound / FX property). Data cross-reference: `notes/data/ability_presentation_audit.json`.

## The four generic mechanisms (C)

**1. Trigger sound** (`TnAbility.OnTriggerSound`):
- `TnAbilityManager` (server) → `Abilities[i].ServerTriggerAbility()` → `TnAbility.ServerTriggerAbility` →
  `OwnerPawn.PlaySound(OnTriggerSound, false, false, false)`.
- **Server-side, replicated** to relevant clients by the engine (H: stock `Actor.PlaySound` with bNotReplicated = false), at the owner pawn.

**2. Trigger effect** (`OnTriggerEffect` at `OnTriggerEffectSocket`, default `PositionSocket`):
- `TnAbilityManager.ClientOnTriggerAbility(class)` → `HmParticleEffectManagerComponent.PlayEffect(OwnerPawn.Mesh, socket, template)`.
- It runs on the server at trigger time **and** on every client through the RepNotify `ReplicatedTriggerAbility`
  (`ReplicatedEvent` → `ClientOnTriggerAbility`), so all clients see it, on the owner's mesh.
- Killstreaks use the same path (`ServerTriggerKillstreak`).

**3. Buff cues** (`TnBuff`, applied by `TnBuffManager.DoAddBuff`):
- **Apply:**
  - `ApplySound` → `OwnerPawn.CreateAudioComponent(ApplySound, play, autodestroy)`, a held looping component, **only if `CanPlaySounds()`**.
  - `ApplyFx` on the owner's current mesh at `FxSocket` (vehicle: `VehicleFxSocket`, else FxSocket):
    - looping by default (`IsApplyFxLooping` True), started / stopped by `ClientTickBuff` as `ShouldFxBeVisible()` changes;
    - non-looping is a one-shot at Apply.
- **Unapply:** `StopEffectsAndSounds()` (stops the loop component and the looping FX), then `UnapplyFx` one-shot, then
  `PlaySound(UnapplySound, bNotReplicated = true)`, local, also gated by CanPlaySounds.
- **OwnerDied:** StopEffectsAndSounds only (no Unapply cues).
- **CanPlaySounds** = `!OnlyPlaySoundOnLocalPlayer || owner is the local player`. The TnBuff default is **True**, so buff sounds are
  **heard only by the buffed player** unless a buff sets it False (Cloak does).
- **Which machines run Apply / Unapply** (`BuffReplicationType`):
  - `kBuffReplicateToAll` → `ReplicateToAllBuffs` RepNotify → every client runs DoAddBuff / DoRemoveBuff itself;
  - `kBuffReplicateToNone` → server only (no client cues);
  - the default value (H: `kBuffReplicateToOwner`, enum 0) → owner client only.
  - The cues are therefore **local per machine**, driven by the replicated buff state, not by replicated sounds.

**4. Ability-specific actors:** barrier spawnable, decoy trap, drain buffs (below).

## Per ability (MP)

| Ability (class) | Cue | Event | Actor / attach | Who hears / sees |
|---|---|---|---|---|
| **Barrier** (TnAbilityBarrier) | BARRIER_DEPLOY | trigger (ServerTriggerAbility) | owner pawn | all (server PlaySound) |
| | anim `Barrier_Equip` + **BARRIER_LP** (PlaySound, bStopWhenOwnerDestroyed) | `TnBarrierSpawnable.Initialize`: on the server (SetBlueprint) **and** on each client (RepNotify `BarrierBP`) | the barrier actor (spawned at owner + BarrierOffset) | all. H: the server's replicated play plus the client's own Initialize may double on clients |
| | **BARRIER_RETRACT** | `TakeDamage` when Health ≤ 0 or a FlashBang hit (melee damage is ignored) | barrier | all (server) |
| **Decoy** (TnAbilityDecoyTrap) | DECOY_TRAP_DEPLOY | trigger | owner | all |
| | **DECOY_TRAP_EXPLODE** = `TnDecoyTrap.PickupSound` | when an enemy picks the trap up (pickup path, H: stock pickup sound) | the trap pickup | all (H) |
| | **DecoyTrap_FX** (looping) | `TnBuffDecoyTrap` Apply on the victim (TrapClass's BuffClass) | victim at `TargetingSocket` | ReplicateToAll: every client |
| **Disguise** (TnAbilityDisguise) | DISGUISE_INITIATE | trigger | owner | all |
| | colour override + stolen name (no sound / FX) | `TnBuffDisguise.Apply` / `Unapply` (ReplicateToAll) | owner. Shown **only to the owner and to enemies** (ShouldShowDisguise); enemies see the disguise faction's colours | per client |
| **Whirlwind** (TnAbilityWhirlwind) | none in script: `LocalTriggerAbility` → `TnMeleeService.StartMeleeAttack(3 = whirlwind)` | the melee attack | owner | the windup / spin cues are the **whirlwind melee attack's AnimNotifies** (melee set; `notes/data/melee_presentation_audit.json`) |
| **Drain** (TnAbilityDrain) | DRAIN_INITIATE + **Drain_Blast_FX** (OnTriggerEffect @ PositionSocket) | trigger | owner | all |
| | **DRAIN_HEAL** (PlaySound, bNotReplicated) + `PlayHit(TnHealTypeDrain)` | `TnBuffDrainSource.ClientTickBuff` → UpdateEffects, **every buff tick** while it has targets, **only on the locally controlled owner** | owner (heal hit from `MeleeSocket_LeftHand`) | the drainer only. Note: called per tick, so the cue's concurrency limits throttle it |
| | anim `Skill_Drain` (left arm, looping) | DrainSource Apply / Unapply | owner | (anim) |
| | **DrainRay_Beam_FX** (looping, tracer end point = the drainer's `MeleeSocket_LeftHand`) + **DRAIN_DAMAGE** (PlaySound, bNotReplicated) + `PlayHit(TnDamageTypeDrain)` | `TnBuffDrainTarget` Apply (FX) / ClientTickBuff (sound + hit, per tick) | the victim at `PositionSocket` | FX: all clients (ReplicateToAll). Sound: each client locally, gated by nothing beyond the per-tick call (H: heard by all) |
| **Energon Sling** (TnAbilityAbilityJammer) | ENERGON_SLING_FIRE | trigger | owner | all |
| | (projectile `TnProjectileAbilityJammer`; victim buff `TnBuffAbilityJammed`, ReplicateToAll, no cues of its own) | hit | victim | — |
| **Cloak** (TnAbilityCloaking → TnBuffCloak) | **CLOAK_AUTOBOT_START_LP / CLOAK_DECEPTICON_START_LP** (held loop) | Cloak Apply. Faction by team (Autobots / Decepticons team info, else the character faction) | owner | **every client**: ReplicateToAll **and `OnlyPlaySoundOnLocalPlayer = False`** |
| | **CLOAK_*_OFF** | Unapply | owner | every client |
| | **Cloaking_FX** (one-shot, not looping) | Apply **and** Unapply (ApplyFx = UnapplyFx) | owner at `PositionSocket` | every client. **Suppressed** when the owner has `TnSkillDisableCloakEffects` (CanPlayEffects) |
| | cloak material effect | `TnPawn.StartCloaking` / `StopCloaking` → `_CharacterMaterials.Start(0)` / `Stop(0)` | owner materials | every client |
| **Dash / Dodge** (TnAbilityDodge) | none in script: `LocalTriggerAbility` → `TnPlayerController.Dodge()` | acrobatics | owner | cues come from the chassis' acrobatics / SoundEventSet dodge events (transform / acrobatics data; presentation audit 2) |
| **Hover** (TnAbilityHover) | none in script: `LocalTriggerAbility` → `TnPlayerController.Hover()` | acrobatics (vehicle form) | owner | cues come from the vehicle blueprint's HoverFX and the hover sound events (audit 2 transform table) |
| HardLock | HARD_LOCK_INITIATED; victim `TnBuffHardLocked`: **HARD_LOCK_WARNING_LP** (loop) + **Hardlock_FX** (loop @ PositionSocket) | trigger / buff | owner / victim | trigger all; buff FX all clients (ReplicateToAll); loop sound **victim only** (OnlyPlaySoundOnLocalPlayer default) |
| Warcry | **WarCry_AOE_FX** @ `Chest_FX` (OnTriggerEffect); buffs `TnBuffWarcry0/1/2`: **WAR_CRY_STATE_START** loop / **_STOP**, FX WarCry_Buff_01/02/03_FX @ Chest_FX (vehicle ReactionSocket_Chest) | trigger / buff | owner + allies | FX all; loop sound buffed player only |
| Spawn Ammo Crate | AMMO_DEPLOY | trigger | owner | all |
| Transform Disruptor | TRANSFORM_DISRUPTOR; victim `TnBuffTransformDisruptor`: TRANSFORM_DISRUPTOR_HIT (ApplySound) | trigger / buff | owner / victim | all / victim only |
| Aoe Heal (where used) | AURA_HEAL_INITIATE; `TnBuffAoeHeal` AURA_HEAL_PATIENT_LP / _STOP | trigger / buff | owner / patient | all / patient only (buff replication default, H) |
| Shockwave | SHIELD_PUSH + the Skill_Shockwave AnimNotifies (DashPulse_1_FX, Shield_BlastWave_A_FX, Trails…) | anim notifies | owner sockets | anim-driven on every client (H) |

**Other buff cues seen in MP** (same rules):
- **Lock-on threat** (`TnBuffLockOnThreatLow / Medium / High`, ReplicateToAll): ATTACK_BEAM_TARGET_LP_01 / 02 / 03 loops
  (target only) + Tractor_CharacterLvl2 / 3_FX @ TargetingSocket (all).
- **Flash-bang** (`TnBuffFlashBangRobot`): EMP_STATIC_DISCHARGE_VICTIM_LP / _STOP.
- **Scramble HUD:** STATIC_DISCHARGE_VICTIM_LP / _STOP (victim only).
- **Kill-streak regen:** KillStreak_HealthRegen_FX @ Chest_FX / ReactionSocket_Chest (all).
- **Berserker:** Prime_Target_FX (all).

## For Systems: the no-hit list

1. **Barrier / Decoy / Disguise / Energon Sling / HardLock / Ammo:** the trigger sound is a plain server PlaySound at the owner.
   Wire it at trigger.
2. **Cloak:** the faction loop + OFF are heard by **everyone**. The one-shot Cloaking_FX plays at both start and end, plus the material fade.
3. **Drain:** DRAIN_HEAL is local to the drainer, DRAIN_DAMAGE local per client, both re-triggered per tick. DrainRay_Beam_FX
   runs from the victim to the drainer's left hand.
4. **Whirlwind windup, Dash, Hover:** **not ability cues**. They are the melee whirlwind attack's notifies, the acrobatics dodge
   events, and the vehicle hover FX / sound events, already in the audit-2 tables.
5. **Most buff loop sounds are owner-only** (`OnlyPlaySoundOnLocalPlayer` default True). The exception is Cloak.
