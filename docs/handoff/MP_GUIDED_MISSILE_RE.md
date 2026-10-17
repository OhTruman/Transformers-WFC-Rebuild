# Guided missile ability in MP (RE, 2026-10-18)

Labels: CONFIRMED (C) = TransGame / HM_Engine / Engine bytecode + authored data; HIGH (H) = native physics / stock engine reading.
Classes: `TnAbilityGuidedMissile`, `TnGuidedMissile` (TnProjectile → HmProjectile → Projectile),
`TnAbilityCooldownConditionGuidedMissile`, `TnPlayerController` state `GuidingMissile`,
`CAM_Strategies_p.GuidedMissile_STRATEGY`.

## 1. Activation and launch (C)

- **Cost:** ResourceRequired 100 (a full ability meter), `Cooldown` 45 s. **The cooldown starts only when guiding ends**
  (TnAbilityCooldownConditionGuidedMissile: true once the PC has been guiding and no longer is).
- **Server trigger:**
  - after **Delay 1.0 s** (timer on the owner pawn) → `SpawnGuidedMissile`;
  - meanwhile the owner plays `Skill_GuidedMissile` (looping, blend-out 0.3 s, `OnTriggerAnimParams`), and the AI stimulus
    `TnStimulusType_GuidedMissile` is raised.
- **Spawn:**
  - rotation = the PC's control rotation, **Pitch clamped to [700, 16384]** (≈ +3.8° … +90°, so it never launches below
    near-horizontal);
  - location = socket **ReactionSocket_Chest** + **(200, 0, 0)** rotated by that rotation;
  - `Missile.Init(dir)` → **Velocity = InitialSpeed × dir**, flight effect and sound attached (not on dedicated servers);
  - then `PC.GuideMissile(missile)`.
- **Data (MP uses `MultiplayerData` when the game's DesiredWeaponDataType is MP):**

| | MP `GuidedMissile_PROJDATA` | campaign `…_SP_PROJDATA` |
|---|---|---|
| InitialSpeed / MaxSpeed | 2000 / 2000 | same |
| ControlStrength | 2500 | 2500 |
| FuseTime | **30 s** | 20 s |
| Damage / DamageRadius | **10000 / 4500** | 750 / 3500 |
| CollisionRadius | 10 | 10 |
| Health | 9999 | 9999 |
| MomentumTransfer | 1.0 | 1.0 |
| DamageType | TnDamageTypeGuidedMissile | same |

  Escalation would use SurvivalData if set; none is authored, so it falls back to the base Data.

## 2. Control model (C, H for physics)

- **Owner input:** `GuidingMissile.PlayerTick`, every frame on the owner's client:
  - pad: LeftRight = RawJoyLookRight, UpDown = −RawJoyLookUp;
  - mouse: GuidedMissileMouseSensitivity × camera yaw / pitch delta;
  - both clamped to ±1; UpDown is flipped when the invert-look setting is on.
  - Sent with **`ServerSetGuidedMissileInput(UpDown, LeftRight)`**. The missile is simulated on the server and replicated
    (`bUpdateSimulatedPosition`).
- **Steering (C):** `SetInput` → **Acceleration = (LeftRight × missile Y + UpDown × missile Z) × ControlStrength** (2500, ×
  TnBuffIncreaseGuidedMissileControl). There is no forward thrust.
- **Physics (H, PHYS_Projectile):** `bRotationFollowsVelocity`; speed is held at MaxSpeed 2000 (InitialSpeed = MaxSpeed). So
  lateral acceleration turns the velocity at about a / v = **1.25 rad/s ≈ 72°/s at full deflection**, at a constant 2000 uu/s.
  There is no throttle or boost.
- **Camera (C):** `TnCameraStrategyManager.StartGuidedMissile` → `GuidedMissile_STRATEGY`:
  - **attached to the missile** at local offset (135, 0, 125), SmoothTime 0.5;
  - **FOV 120**;
  - post-process override: Scene_Desaturation 0.75, **red CLUT** (ENV_Cluts_p.redclut), motion blur (amount 200), DOF focus
    10000, bloom 0.5;
  - transition 0.5 s.
- **The owner's pawn while guiding (C):** `OnGuideMissile` → `ClearInputs`, `StopMovement`, state `GuidingMissile`.
  - The pawn **stands in place** looping `Skill_GuidedMissile`; the stop anim is replicated on exit.
  - Fire is suppressed (`SetDeferredFire` is a no-op in the state).
  - The pawn **stays fully vulnerable**: no god mode or shield.

## 3. Duration, detonation, aborts (C)

- **Fuse:** LifeSpan = FuseTime (MP **30 s**, + TnBuffIncreaseGuidedMissileFuseTime). On expiry the actor is destroyed →
  `HmProjectile.Destroyed` → **Explode**. So the missile always explodes; it never just vanishes.
- **Impact:**
  - world hit → `HitWall` → Explode;
  - touching a pawn or other blocking actor → `Touch` / `ProcessTouch` → Explode (stock);
  - landing → Explode.
- **Manual detonate:** pressing the **ability button again** while guiding → `ServerDetonate` → Explode. Fire is disabled.
- **Shot down:** `bProjTarget`; damage reduces Health 9999, and it explodes at ≤ 0 (the destroyer gets
  `TnCompetitiveScoreEventKillProjectile`). With 9999 health it is effectively unkillable.
- **Owner killed / leaves the state:** `GuidingMissile.EndState` (server) → `StopGuidingMissile` → **Detonate** + clear. Dying
  ends the state, so the missile blows up where it is. Damage that doesn't kill does not abort.
- **Missile gone** (exploded / destroyed): the 0.1 s `UpdateGuidingMissile` timer sees it isn't controllable → PC (server and
  client) → `PlayerWalking`, camera back to normal, the cooldown starts.
- **Instances:** one per player. `TnPlayerController.GuidedMissile` holds a single reference, and the ability button is
  repurposed to detonate while guiding. There is no shared or global limit.

## 4. Explosion (C)

`Projectile.Explode` → `HurtRadius(Damage, DamageRadius, falloff −1 (stock linear), TnDamageTypeGuidedMissile, MomentumTransfer,
location, instigator controller)`:
- **MP: 10000 damage, radius 4500**, through the projectile's DamageModifier / DamageRadiusModifier buffs and the skill multiplier;
- kill-feed string `` `k [Rocket] `o `` (suicide `` `o [Suicide][Rocket] ``);
- victims get a camera shake (Intensity 2.0, roll 0.5, falloffs 0.55 / 0.5).

## 5. What other players see (C / H)

- The replicated missile actor: mesh `WEP_GuidedMissile_p.GuidedMissile_PROJMESH` with its HmProjectileMesh flight effect and
  flight sound, moving at 2000 uu/s.
- The owner standing still, looping `Skill_GuidedMissile`, and shootable.
- The explosion effect via PlayExplosion.
- No special HUD for others (H).
