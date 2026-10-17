# Decoy Trap (Scout MP ability): mechanics (RE, 2026-10-18)

Labels: CONFIRMED (C) = TransGame script + CDOs (authored.db); HIGH (H) = stock UE3 / inherited-path reading.
Classes:
- TnAbilityDecoyTrap → TnAbilitySpawnInventory → TnAbility;
- TnDecoyTrap → TnBuffPickup;
- TnDroppedPickupDecoyTrap → TnDroppedPickupWeapon → TnDroppedPickupDefrag → TnDroppedPickup;
- TnBuffDecoyTrap → TnBuff;
- TnBuffDotFromDecoy → TnBuffDot.

## 1. Activation and cooldown (C)

**Trigger:**
- Cooldown **60 s** (`Cooldown [60]`, one value: both modes).
- Anim `Skill_Barrier` (blend out 0.3, root motion ignored).
- Sound **DECOY_TRAP_DEPLOY** at trigger (server PlaySound, replicated).

**Spawn** (`ServerTriggerAbility` → after **SpawnDelay 0.5 s** → `SpawnInventory`):
- spawns a `TnDecoyTrap` and calls `DropFrom(owner location, TossVelocity >> owner rotation, owner rotation)`;
- TossVelocity = (2000, 1200, 0) in the owner's frame: 2000 forward, 1200 along local Y, no up component;
- the result is a thrown dropped pickup (`TnDroppedPickupDecoyTrap`), team-stamped with the owner's team (`SetTeamNum`).

**One trap at a time:**
- `ServerCanStartCooldown` is false while the spawn timer runs **or while the dropped trap still exists**.
- So the 60 s cooldown starts only once the trap is gone (triggered, destroyed, expired or faded).
- `ForceEndAbility` fades the trap out.
- The CooldownCondition0 is a plain `TnAbilityCooldownCondition` with no authored values.

## 2. What enemies see (C)

- **It poses as a dropped weapon:**
  - `OnDropInventory` → `SetDecoyWeapon(owner's current weapon class)`, so the trap uses **that weapon's pickup mesh**;
  - the weapon class replicates (`WeaponClass` RepNotify) so every client shows the same mesh.
- **Tell 1, spin direction:** it spins at the weapon's PickupRotationRate with **Yaw negated** (the opposite way to a real drop).
- **Tell 2, glitch:**
  - after landing it defrags in over **ArmDelay 3 s** (`ClientLanded` sets DefragTime);
  - once fully faded in, every **GlitchDelay 5 s** it fades out over GlitchDuration / 2 = 1 s, and `Pickup.Tick` fades it back in;
  - so it glitches periodically.
- **Lifetime:** the inherited `TnDroppedPickupWeapon.MultiplayerLifeSpan` **30 s** (H: the weapon-drop lifespan applies; not overridden).
  It also fades out at once if its owner is dead or dying (`Pickup.Tick`, server).

## 3. Trigger rule (C, with one H)

- **Who:** `TnDecoyTrap.PickupAllowed` = the picker is **not the owner and not on the owner's team**. Teammates and the owner can't trigger it.
- **How:** it uses the dropped-weapon pickup path:
  - touch cylinder **radius 200, height 100** (inherited `TnDroppedPickupWeapon.TouchCylinder`, offset +100 Z);
  - `bPickupWhenTouched` false (TnDroppedPickup).
  - So the trap fires when an enemy **picks it up like a weapon** (H: the player-initiated `TakePickUp`; not an automatic touch).
- **On pickup:** `TnBuffPickup.GivenTo` → victim `AddBuff(TnBuffDecoyTrap, level 0, applier = the trap's DecoyCreator)`, then
  PickedUpBy and the actor is destroyed. **PickupSound = DECOY_TRAP_EXPLODE** plays at the pickup.
- **Destroyable** (`CanBeDestroyed` true, TnDroppedPickupDefrag.TakeDamage):
  - only damage from **non-team instigators** counts (the owner can't);
  - **FlashBang or Melee damage kills it instantly**; other damage subtracts from Health (H: default Health 0, so any enemy hit destroys it);
  - destruction → FadeOut.

## 4. The stun (TnBuffDecoyTrap; C)

- **Duration:** BuffTime **2.0 s**. ReplicateToAll.
- **On Apply:**
  - **robot form only:** loops `HitReactions_React_DecoyTrap_In` (full body, Looping, high priority, blend in 0.3);
  - **input lock:** `PC.SetCinematicMode(true, bHidePlayer false, bAffectsHUD false, bAffectsMovement true, bAffectsTurning true,
    bAffectsButtons true)`, server side for player victims;
  - **Velocity and Acceleration zeroed**;
  - **ExposeSelf()** (server): removes Cloak (adds TnBuffDecloak) and Disguise (adds TnBuffDisguiseLost).
- **FX:** **DecoyTrap_FX** loops at the victim's **TargetingSocket** while stunned (all clients).
- **On Unapply:** `HitReactions_React_DecoyTrap_Out` (robot form, one-shot, blend out 0.35). Input is restored (also on owner death).
- **No direct damage and no explosion damage.** "Explode" is only the pickup sound name. Damage comes only from the skill below.

## 5. Skills (C)

- **TnSkillDecoyDot:** if the trap's creator has it, the victim also gets **TnBuffDotFromDecoy**:
  - **30 damage per second for 2.0 s** (60 total);
  - `TakeDamage(30 × dt)` every buff tick, type **TnDamageTypeDecoyDot** (no hit reaction, no HUD feedback, exempt from accuracy kills,
    kill string "`k [Decoy] `o");
  - instigated by the creator; server only (ReplicateToNone).
- **TnSkillIncreaseDecoyTrapTime:** the creator gets **TnBuffIncreaseDecoyTrapTime** (FloatModifier **1.5**, permanent, server only).
  The stun buff's `InstigatorBuffTimeModifier` lists it, so the stun lasts **2.0 × 1.5 = 3.0 s** (H: multiplicative, from the modifier type).

## 6. Marker and XP

- **Marker (C):** while the stun runs, **only the trap creator's own client** (`ApplierPawn.IsLocallyControlled()`) adds an objective marker
  on the victim:
  - text "Victim Trapped", `TnObjectiveMarkerTypeDecoyTrap`;
  - material `UI_HudMarkers_p.MarkerDeceptDestructible_MAT`, size 0.05, shown offscreen at 0.0625 with an arrow, label only when focused.
  - It is removed on unapply or death.
- **XP:** `TnXpEventTypeKillDecoyTrap` = **100 XP**, "Enjoy The Gift", "You killed an enemy after they were stunned by your trap."
  - **No script reference** awards it (no class in the decompiled TransGame / TransContent names it outside its own CDO).
  - The award point is native or data-table driven (UNKNOWN). H: the stock kill-credit check "victim had the creator's decoy buff".

## 7. Cue moments (for Systems' table)

| Cue | When |
|---|---|
| DECOY_TRAP_DEPLOY | at trigger, the server PlaySound (0.5 s before the toss) |
| DECOY_TRAP_EXPLODE | the pickup sound, when an enemy picks the trap up |
| DecoyTrap_FX | looping on the victim at TargetingSocket for the stun |
| React_DecoyTrap_In (loop) / _Out | robot-form stun start / end |

## 8. Follow-ups (2026-10-18)

**DecoyTrap_FX "Color" (ColorByParameter, default 134 / 168 / 196): nobody sets it (C).**
- TnBuffDecoyTrap (and the EMP-grenade AI buffs that reuse DecoyTrap_FX) author **no `FxParameters`**, so `TnBuff.PlayEffect` applies
  no parameter set. TnDecoyTrap / TnAbilityDecoyTrap / TnDroppedPickupDecoyTrap never touch the FX.
- The only script callers of `SetColorParameter('Color', …)` are `TnAnimNotify_PlayFactionEffect` (EnergonColor) and `TnCarForm`
  (boost colour), and neither plays DecoyTrap_FX.
- **So the effect always shows its authored default colour** (134, 168, 196), for every team and character.

**DECOY_TRAP_EXPLODE's root LoopStart 0.32 / LoopEnd 1.05: inert (C, prior audio RE).**
- WFC's SoundNodeRoot applies LoopStart / LoopEnd to its tracked playback time **only when root bLooping is set**
  (MILESTONE03_AUDIO_NATIVE_FIDELITY.md: `if root.bLooping and t >= LoopEnd: t = fmod(t − LoopStart, LoopEnd − LoopStart) + LoopStart`).
  This root does not set bLooping, and none of its wave events loops.
- **So the cue is a one-shot**, played once as the trap's `PickupSound` when an enemy picks it up. There is no armed-state loop.
  The layers run on the root's timeline:
  - TRANS_POWER_TRAP_ELEC_LP_EXPLO at 0;
  - KAMIKAZE_EXPL_01 / 02 / 03 (random, pitch variation 2) at 0;
  - **TRANS_POWER_TRAP_ELEC_LP shaped by its envelope**: pitch −36 → 0 over 0.28 s; volume up to 0.97 by 0.15 s, held to 4.10 s,
    out by 4.57 s (PlayWhenSilent);
  - DEATH_LIGHT_01 / 02 / 03 (random, −3 dB) at 3.50 s.
  - So the "loop" layer is a ~4.6 s enveloped tail covering the 2 s stun (3 s with the skill), not a loop.
- Root: −9 dB, smart pan, distance 1000–12000.
