# Particle module fidelity audit (Cascade modules used by MP content vs our FX runtime)

WFC RE lane, 2026-10-18. Parts: (a) inventory + (b) gap list, in this commit; (c) semantics of the missing modules in
usage-count order; (d) TypeData beam / trail / mesh + SecondsBeforeInactive / LOD.

**Ours** (read-only grep of `Rebuild/src/render`; the runtime keys modules on the exact `"PMI_*"` string from
map_fx_runtime.json):
- **C** = a code path keyed on the module exists. **C means named / implemented, not verified to match.**
- **P** = implemented but flagged PARTIAL in our runtime's own header (WfcMapFx.cpp: Gravity multiplier, ColorByParameter
  instance parameter, DynamicParameter slot order, LocationPrimitiveSphere sampling).
- **-** = no code path.

## (a) Inventory

**Scope:** 266 ParticleSystems referenced by MP content. That's every system path found in the props of:
- MP map objects;
- weapons / projectiles (WEP_* packages, TnWeapon* / TnProjectile* CDOs);
- ability, buff and class CDOs;
- death and hit-effect blueprints;
- chassis ROBODEF / VEHDEF;
- robot anim-set notifies (TR_*_ANM_p).

That is 266 of the 544 systems in authored.db. List: `work/q/ps_refs.json` (not committed); census:
`notes/data/particle_module_census.json`.

**Method:**
- WFC compiles most modules into a per-LOD native record stream (memory: only CDOs exist as UObjects for most classes).
- Every emitter LOD was decoded with AssetTools' `pstream.system()`: compiled `EParticleModuleId` per record + flagA (enabled).
  It was imported read-only, without writing bytecode. TypeData is read from the tagged LOD.
- 0 decode errors.
- Counts are LOD records; "disabled" = flagA 0 (the module is not in the executed list).
- **High Moon modules:** only `HmParticleModuleGravity` reaches the compiled stream as its own id (PMI_Gravity).
  `HmParticleModuleColorAmbient`, `HmParticleModuleLocationSkeleton` and `HmParticleModuleTypeDataCloud` exist only as CDOs.
  **No MP-referenced LOD uses them.**

## (b) Module classes, usage and our coverage

| Module (compiled id) | Ours | LODs (disabled) | Emitters | Systems | Used by (categories, LOD count) | Example systems |
|---|---|---|---|---|---|---|
| PMI_Size | C | 3514 (2) | 1506 | 266 | weapon 1783, anim_notify 898, death/hit 460, chassis 431, ability/buff/class 318, mp_map 272 | Impact_AssaultRifle_FX, Impact_AssaultRifle_Vehicle_FX, Impact_HeavyMG_FX |
| PMI_Lifetime | C | 3510 (9) | 1507 | 266 | weapon 1792, anim_notify 892, death/hit 447, chassis 431, ability/buff/class 318, mp_map 274 | Impact_AssaultRifle_FX, Impact_AssaultRifle_Vehicle_FX, Impact_HeavyMG_FX |
| PMI_SizeMultiplyLife | C | 2737 (36) | 1140 | 249 | weapon 1362, anim_notify 689, chassis 398, death/hit 394, mp_map 249, ability/buff/class 199 | Impact_AssaultRifle_FX, Impact_AssaultRifle_Vehicle_FX, Impact_HeavyMG_FX |
| PMI_ColorScaleOverLife | C | 2673 (49) | 1145 | 257 | weapon 1262, anim_notify 686, chassis 377, death/hit 323, ability/buff/class 265, mp_map 240 | Impact_AssaultRifle_FX, Impact_AssaultRifle_Vehicle_FX, Impact_HeavyMG_FX |
| PMI_Velocity | C | 2567 (154) | 1081 | 251 | weapon 1348, anim_notify 674, death/hit 399, chassis 246, mp_map 218, ability/buff/class 196 | Impact_AssaultRifle_FX, Impact_AssaultRifle_Vehicle_FX, Impact_HeavyMG_FX |
| PMI_Rotation | C | 2262 (65) | 950 | 242 | weapon 1231, anim_notify 545, death/hit 288, chassis 242, mp_map 187, ability/buff/class 173 | Impact_AssaultRifle_FX, Impact_AssaultRifle_Vehicle_FX, Impact_HeavyMG_FX |
| PMI_Location | C | 1956 (94) | 823 | 230 | weapon 1024, anim_notify 458, chassis 290, death/hit 187, ability/buff/class 177, mp_map 142 | Impact_AssaultRifle_FX, Impact_AssaultRifle_Vehicle_FX, Impact_HeavyMG_FX |
| PMI_ColorByParameter | P | 1697 (22) | 732 | 170 | weapon 941, anim_notify 320, chassis 257, mp_map 210, ability/buff/class 168, death/hit 84 | Impact_AssaultRifle_FX, Impact_HeavyMG_FX, MuzzleFlash_AssaultRifle_FX |
| PMI_Color | C | 1640 (71) | 707 | 184 | weapon 693, anim_notify 427, death/hit 249, chassis 246, ability/buff/class 184, mp_map 81 | Impact_AssaultRifle_FX, Impact_AssaultRifle_Vehicle_FX, Impact_HeavyMG_FX |
| PMI_LocationPrimitiveSphere | P | 1482 (26) | 593 | 154 | weapon 847, anim_notify 384, death/hit 246, ability/buff/class 121, mp_map 117, chassis 91 | Impact_AssaultRifle_FX, Impact_AssaultRifle_Vehicle_FX, Impact_HeavyMG_FX |
| PMI_Gravity | P | 1285 (79) | 511 | 161 | weapon 701, anim_notify 324, death/hit 249, mp_map 139, chassis 97, ability/buff/class 81 | Impact_AssaultRifle_FX, Impact_AssaultRifle_Vehicle_FX, Impact_HeavyMG_FX |
| PMI_VelocityOverLife | C | 1117 (96) | 448 | 129 | weapon 639, anim_notify 299, death/hit 204, mp_map 110, chassis 80, ability/buff/class 49 | Impact_AssaultRifle_FX, Impact_AssaultRifle_Vehicle_FX, Impact_HeavyMG_FX |
| PMI_DynamicParameter | P | 773 (11) | 302 | 114 | weapon 512, anim_notify 117, death/hit 99, mp_map 86, ability/buff/class 52, chassis 29 | Impact_AssaultRifle_FX, Impact_HeavyMG_FX, MuzzleFlash_AssaultRifle_FX |
| PMI_RotationRate | C | 716 (48) | 303 | 146 | weapon 292, anim_notify 206, chassis 93, ability/buff/class 81, mp_map 79, death/hit 74 | Impact_AssaultRifle_Vehicle_FX, Corruptor_Impact_Long_FX, Corruptor_Thump_FX |
| PMI_ColorOverLife | C | 613 (53) | 254 | 77 | weapon 382, anim_notify 185, death/hit 140, chassis 36, mp_map 22, ability/buff/class 17 | Impact_AssaultRifle_FX, Impact_AssaultRifle_Vehicle_FX, Impact_HeavyMG_FX |
| PMI_SubUV | C | 601 (8) | 238 | 115 | weapon 334, anim_notify 147, death/hit 104, mp_map 74, ability/buff/class 58, chassis 47 | MuzzleFlash_AssaultRifle_FX, MuzzleFlash_BurstRifle_FX, Corruptor_Chargeup_FX |
| PMI_MeshRotation | C | 235 (36) | 112 | 92 | weapon 84, anim_notify 69, chassis 51, ability/buff/class 31, death/hit 18, mp_map 6 | MuzzleFlash_AssaultRifle_FX, MuzzleFlash_HeavyMG_FX, Shell_AssaultRifle_FX |
| PMI_LocationEmitter | C | 207 (6) | 83 | 44 | weapon 124, anim_notify 42, death/hit 32, ability/buff/class 25, chassis 14, mp_map 12 | Shell_AssaultRifle_FX, Tracer_AssaultRifle_FX, Shell_BurstRifle_FX |
| PMI_LocationPrimitiveCylinder | - | 179 (4) | 80 | 43 | mp_map 65, anim_notify 59, chassis 43, ability/buff/class 16, death/hit 15, weapon 11 | Corruptor_Impact_Long_FX, Corruptor_Death_FX, Smoke_Dmg_Lrg_FX |
| PMI_VelocityInheritParent | - | 154 (10) | 66 | 43 | weapon 107, anim_notify 45, death/hit 15, chassis 11, mp_map 10, ability/buff/class 6 | MuzzleFlash_AssaultRifle_FX, Reload_AssaultRifle_FX, Shell_AssaultRifle_FX |
| PMI_SwitchableColorScaleOverLife | C | 130 (0) | 56 | 26 | weapon 102, chassis 14, ability/buff/class 14, anim_notify 10 | MuzzleFlash_AssaultRifle_FX, MuzzleFlash_HeavyMG_FX, Tracer_AssaultRifle_FX |
| PMI_Collision | C | 107 (80) | 50 | 37 | weapon 92, anim_notify 9, death/hit 8, mp_map 5 | Impact_AssaultRifle_Vehicle_FX, Corruptor_Impact_Long_FX, Magazine_EMPShotgun_FX |
| PMI_BeamSource | C (beam path) | 107 (0) | 53 | 25 | weapon 57, anim_notify 33, ability/buff/class 12, mp_map 8, death/hit 8, chassis 6 | corruptor_plasma_arcs_FX, MuzzleFlash_DeathBeam_FX, EXP_Mortar_FX |
| PMI_BeamTarget | C (beam path) | 107 (0) | 53 | 25 | weapon 57, anim_notify 33, ability/buff/class 12, mp_map 8, death/hit 8, chassis 6 | corruptor_plasma_arcs_FX, MuzzleFlash_DeathBeam_FX, EXP_Mortar_FX |
| PMI_AttractorPoint | - | 97 (7) | 45 | 33 | anim_notify 30, mp_map 22, ability/buff/class 20, weapon 19, death/hit 6, chassis 4 | Impact_AssaultRifle_Vehicle_FX, Corruptor_Impact_Long_FX, Corruptor_Thump_FX |
| PMI_SizeScale | C | 86 (0) | 39 | 20 | anim_notify 52, ability/buff/class 22, mp_map 8, weapon 4, death/hit 2 | Corruptor_Chargeup_FX, Corruptor_Thump_FX, AE_Blast_FX |
| PMI_MeshRotationRate | C | 84 (4) | 35 | 24 | weapon 44, ability/buff/class 30, anim_notify 10 | Shell_AssaultRifle_FX, Shell_BurstRifle_FX, Magazine_EMPShotgun_FX |
| PMI_BeamNoise | C (beam path) | 66 (13) | 33 | 17 | weapon 40, anim_notify 20, death/hit 8, mp_map 6 | MuzzleFlash_DeathBeam_FX, TF_Death_Buildup_FX, Muzzleflash_FusionCannon_FX |
| PMI_AccelerationOverLife | - | 33 (0) | 14 | 8 | mp_map 20, chassis 4, death/hit 4, weapon 3, anim_notify 2 | Smoke_Dmg_Lrg_FX, Squib_HomRocket_01_FX, Fire_Energon_FX |
| PMI_LocationDirect | - | 22 (0) | 10 | 7 | ability/buff/class 12, weapon 8, anim_notify 2 | Tracer_GrenadeLauncher_FX, Tracer_HomRocket_FX, Tracer_WarshipHomRocket_FX |
| PMI_AttractorParticle | - | 20 (0) | 7 | 2 | ability/buff/class 20 | Shard_FX, Shard_FadeOut_FX |
| PMI_Orbit | - | 18 (4) | 7 | 7 | mp_map 6, ability/buff/class 6, anim_notify 3, chassis 3, death/hit 3, weapon 3 | TF_Death_Energy_Med_FX, Squib_HomRocket_01_FX, Fire_Energon_FX |
| PMI_BeamSineWave | C (beam path) | 14 (0) | 7 | 3 | ability/buff/class 8, anim_notify 6, weapon 4 | DrainRay_Beam_FX, OmegaHeal_Beam_FX, Tracer_RepairBeam_FX |
| PMI_Acceleration | C | 8 (0) | 3 | 3 | mp_map 6, anim_notify 2 | Fire_Energon_FX, Fire_Energon_Lrg_FX, Shield_BlastWave_A_FX |
| PMI_LocationEmitterDirect | C | 6 (0) | 3 | 3 | weapon 6 | Shell_AssaultRifle_FX, Shell_BurstRifle_FX, Shell_HeavyPistol_FX |
| PMI_KillBox | - | 4 (0) | 2 | 2 | weapon 4 | MeleeEquip_Purple_FX, SwordEquip_Red_FX |
| PMI_SubUVDirect | C | 2 (0) | 1 | 1 | anim_notify 2 | H2H_Punch01_FX |

| TypeData | Ours | LODs | Systems | Example |
|---|---|---|---|---|
| (sprite) | C | 2899 | 253 | Impact_AssaultRifle_FX, Impact_AssaultRifle_Vehicle_FX, Impact_HeavyMG_FX |
| ParticleModuleTypeDataMesh | C ("mesh") | 325 | 106 | MuzzleFlash_AssaultRifle_FX, MuzzleFlash_HeavyMG_FX, Shell_AssaultRifle_FX |
| ParticleModuleTypeDataTrail2 | C ("trail2", FAST item) | 170 | 50 | Shell_AssaultRifle_FX, Tracer_AssaultRifle_FX, Shell_BurstRifle_FX |
| ParticleModuleTypeDataBeam2 | C ("beam2", FAST item) | 107 | 25 | corruptor_plasma_arcs_FX, MuzzleFlash_DeathBeam_FX, EXP_Mortar_FX |

**Gap list for (c), usage order** (MP LOD records; all `-` unless noted):
1. **PMI_LocationPrimitiveCylinder**: 179 LODs, 43 systems.
2. **PMI_VelocityInheritParent**: 154 / 43.
3. **PMI_AttractorPoint**: 97 / 33.
4. **PMI_AccelerationOverLife**: 33 / 8.
5. **PMI_LocationDirect**: 22 / 7.
6. **PMI_AttractorParticle**: 20 / 2.
7. **PMI_Orbit**: 18 / 7.
8. **PMI_KillBox**: 4 / 2.
9. Plus the four **P** items (Gravity 1285 LODs, ColorByParameter 1697, DynamicParameter 773, LocationPrimitiveSphere 1482).
   They carry the most weight: Gravity and LocationPrimitiveSphere are everywhere.

**For (d):**
- TypeDataTrail2: 170 LODs / 50 systems, every tracer and shell trail;
- TypeDataBeam2: 107 / 25;
- TypeDataMesh: 325 / 106.

## (c) Semantics of missing / partial modules

**Source:** headless Ghidra on the compiled module runners (`work/q/pr*.txt`):
- spawn runner `0x8303E288`, update runner `0x83039A18`;
- both switch on the record's ModuleId (record +0x0A), payload at LOD-blob +0x5C + record payload offset.

**Particle layout (this build):**
- OldLocation +0x00, **RelativeTime +0x0C**, Location +0x10, **OneOverMaxLifetime +0x1C**, BaseVelocity +0x20, Rotation +0x2C,
  Velocity +0x30, …, **Color +0x60**, **BaseColor +0x70**, Flags +0x5C;
- update cases skip particles with flag bit 0.

**Common rules:**
- **Spawn-module distributions are evaluated at EmitterTime** (instance +0xD4). Over-life modules use the particle's RelativeTime.
- **Modules run in the LOD's compiled record order** (authored module order).
- The **shared RNG** is the engine LCG: `x = x·0x0BB38435 + 0x3619636B`, mantissa-filled float in [0, 1).

### Batch 1: the four modules our runtime flags PARTIAL (highest usage)

**PMI_Gravity** (HmParticleModuleGravity; update case 0x1C; 1285 LODs). **CONFIRMED.** Per update, per live particle:

```
g      = ComponentScale(component).Z * inst[+0x94]          // 0x82DC2BF0
dv     = GravityMultiplier(payload float; CDO 1.0) * g * DeltaTime
Velocity.Z     += dv
BaseVelocity.Z += dv                                         // both, like stock Acceleration
```

- `ComponentScale` = `Component.Scale × Component.Scale3D × (Owner ? Owner.DrawScale × Owner.DrawScale3D : 1)`, per axis. **Gravity
  scales with the emitter's actor and component scale.**
- No spawn case, no X/Y term.
- **UNKNOWN:** the source of `inst[+0x94]`. It is not `GetGravityZ` (no particle-code caller); the instance is written by a
  per-tick block copy near `0x83038B3C`. Until it is traced, use the world gravity Z ([H], WFC −520 × the WorldInfo factor) and
  keep the scale factor above.

**PMI_LocationPrimitiveSphere** (spawn case 0x16; 1482 LODs). **CONFIRMED** (stock UE3 structure). Payload flags `[0..5]` =
Positive_X / Y / Z, Negative_X / Y / Z, `[6]` SurfaceOnly, `[7]` Velocity.

1. `u` = DetermineUnitDirection (`0x8300E2C0`), per axis from 3 RNG draws:
   - both signs → 2r − 1;
   - positive only → r;
   - negative only → −r;
   - neither → 0.
   - **This is a cube sample, not a sphere sample.**
2. `n = u / |u|` (if |u| > ε). If SurfaceOnly, `u = n`.
3. `off = u × StartRadius(EmitterTime)`; then, for each axis enabled in either sign, `off[i] = clamp(off[i], −|n[i]|·R, +|n[i]|·R)`.
   That keeps the point inside the sphere along that direction; the distribution is non-uniform, denser along the axes.
4. `p = StartLocation(EmitterTime) + off`. Transformed by the component LocalToWorld unless the emitter is local-space (instance
   flag +0xA8 bit 0x40000000). Then `Location += p`.
5. If Velocity: `v = (p_world − StartLocation) × VelocityScale(EmitterTime)`; `Velocity += v`, `BaseVelocity += v`.
- **Compiled distribution order (C, 2026-10-17):** float k=0 **VelocityScale**, k=1 **StartRadius**; vector k=0 StartLocation
  (base-class fields first; a StartRadius-first decode swaps them).

**PMI_ColorByParameter** (spawn case 3; 1697 LODs). **CONFIRMED.**

1. Search the component's InstanceParameters (component +0x1F0, count +0x1F4, 36-byte entries) for an entry whose **name equals the
   module's ColorParam**.
2. **Found:** particle Color = the parameter's FColor bytes **× 1/255, clamped to [0, 1], linear: no gamma**.
3. **Not found:** `DefaultColor` (CDO white) converted through the engine's **FColor → FLinearColor table: (c/255)^2.2** for RGB,
   alpha /255.
4. Then **BaseColor = Color**. It **overwrites** both; it does not multiply. A later ColorScaleOverLife scales BaseColor.

So an unset parameter gives gamma-decoded DefaultColor, while a set one is linear /255. **Ours lacks the instance-parameter lookup.**
H: which gameplay code sets the parameters (weapon team colour, character colours) is the next step if needed.

**PMI_DynamicParameter** (spawn and update case 0x1D; 773 LODs). **CONFIRMED.**
- Four slots in order 0..3. Distributions are stored consecutively in the LOD's float-distribution table (0x14 stride), so **slot
  order = natural order. Our "natural order used" is right.**
- Per slot, the time = `bUseEmitterTime ? EmitterTime : particle RelativeTime` (payload ints `[0..3]`).
- Value = ParamValue(t), written to the particle's dynamic-parameter payload (offset from the LOD's per-module payload table).
- **The same evaluation runs at spawn and on every update** (no "spawn only" variant). The dynamic parameter feeds the vertex
  stream (DynParam +0x54) to the material.

**Bonus, PMI_Lifetime** (spawn case 1), the stacking rule:
- First Lifetime: `OneOverMaxLifetime = 1 / L`.
- A **second Lifetime module adds**: `1 / (1/old + L)`.
- L ≤ 0 → OneOverMaxLifetime = 0 (immortal).
- `RelativeTime = OneOverMaxLifetime × SpawnTime`.

### Where the per-instance module values live (answer to Integration / AssetTools)

The cook strips the module objects, but **every per-instance value the runners read is in the compiled LOD stream's parameter
blob**:
- In pstream's raw "between lists and groups" hex, after the module-order lists: `int 0, int SIZE`, then SIZE bytes, each module's
  block **concatenated in record order**.
- Verified byte-exact on Tracer_AssaultRifle_FX (SIZE 100): SizeMultiplyLife 3 ints; ColorByParameter name index + DefaultColor
  A,R,G,B; ColorScaleOverLife bEmitterTime; Gravity float; LocationEmitter 24; DynamicParameter bUseEmitterTime ×4; Sphere 8 ints.
- The per-module layouts are in the AssetTools message, mirrored below where relevant.
- H: the Gravity value seen (−0.003) is small, so the unknown instance scalar `inst[+0x94]` is probably large (world gravity order).
  Verify once AssetTools' decode gives the full census.

### Batch 2: the eight modules with no runtime path (usage order)

**Compiled modules (CONFIRMED from the runners):**

**PMI_LocationPrimitiveCylinder** (spawn 0x15; 179 LODs):
- Block (40 bytes): ±X / Y / Z flags ×6, SurfaceOnly, Velocity, RadialVelocity, HeightAxis (0 = X, 1 = Y, else Z).
- Distributions (EmitterTime), **compiled order (C)**: float k=0 VelocityScale, k=1 StartRadius, k=2 StartHeight; vector k=0
  StartLocation (the LocationPrimitiveBase fields come first).
- Steps:
  1. `u` = DetermineUnitDirection (as the sphere).
  2. The two radial axes form `r2 = (u_a, u_b)`, normalised in 2D. SurfaceOnly puts the point on the radius.
  3. Radial offset = `r2 × Radius`, clamped per axis to `±|n_i|·Radius`; height offset = `u_h × StartHeight`, clamped to
     ±StartHeight.
  4. `p = offset + StartLocation`; to world unless local-space; `Location += p`.
  5. If Velocity: `v = (p_world − StartLocation)` with the height component zeroed when **RadialVelocity** is set, × VelocityScale;
     added to Velocity and BaseVelocity.

**PMI_VelocityInheritParent** (spawn 0x18; 154 LODs):
- `Velocity += Scale(EmitterTime) ⊙ EmitterVelocity` and `BaseVelocity +=` the same.
- EmitterVelocity is the emitter instance velocity (+0x88, `(Location − OldLocation)/dt` of the component), or **0 if the emitter
  is local-space**. Component-wise product. No update case.

**PMI_AccelerationOverLife** (update 0x1B; 33 LODs):
- Per update: `a = AccelOverLife(RelativeTime)`; `Velocity += a·dt`; `BaseVelocity += a·dt`. No spawn case.

**PMI_LocationDirect** (spawn and update 0x12; 22 LODs). Distributions: Location, LocationOffset, ScaleFactor, (Direction: read by
neither runner).
- **Spawn:** `Location = Location(RelativeTime)` → world unless local-space (full LocalToWorld incl. translation); `off =
  LocationOffset(EmitterTime)` is stored in the particle payload; `Location += off`.
- **Update:**
  1. `target = Location(RelativeTime)` → world, `+ stored off`;
  2. `diff = target − Location`;
  3. `Velocity = diff × ScaleFactor(RelativeTime).X / dt` (if |dt| > ε, else ×1);
  4. `Location += diff × (1 − ScaleFactor.X)`.
  - ScaleFactor.X = 1 means fully velocity-driven; 0 means a snap.

**PMI_Orbit** (spawn and update 0x1E; 18 LODs). Block (40): +0 (H ChainMode, unread), then {bProcessDuringSpawn,
bProcessDuringUpdate, bUseEmitterTime} for OffsetAmount @+4, RotationAmount @+0x10, RotationRateAmount @+0x1C. Per-particle
payload 12 floats: Offset[0..2], Rotation[3..5], RotationRate[6..8], RotationRateBase[9..11].
- **Spawn:** if the flag is set, Offset += OffsetAmount(t), Rotation += RotationAmount(t), RotationRate[6..8] and [9..11] +=
  RotationRateAmount(t); t = EmitterTime or RelativeTime per bUseEmitterTime.
- **Update:** the same additions per frame for the components whose bProcessDuringUpdate is set (RotationRate into [9..11]).
- **They accumulate every frame**: the stock "add" chaining.
- The orbit offset is applied when the vertex is filled (sprite fill `0x8305F7D8`): H, stock UE3, offset rotated by Rotation +
  RotationRate-integrated angle around the particle. **Fill-side math not traced.**

**Object-run modules** (compiled id PMI_Unknown; executed through the module object's virtual Spawn / Update in record order; their
tagged objects survive the cook):

**PMI_AttractorPoint** (97 LODs), **PMI_AttractorParticle** (20), **PMI_KillBox** (4):
- The native vtables were **not located**. The class registrations are found (`0x8304D708` / `0x8304D658` / `0x83047DA0`), but
  not the constructor / vtable. **These semantics are stock UE3, HIGH-unverified.**
- **AttractorPoint** (props: Position (vector, EmitterTime), Range, Strength, StrengthByDistance (CDO true), bAffectBaseVelocity,
  bOverrideVelocity, bEnabled):
  1. Position → world unless local-space.
  2. Per particle: `d = P − Location`, `dist = |d|`; if `dist ≤ Range`: `s = Strength × (StrengthByDistance ? (Range − dist) /
     Range : 1)`; `a = normalize(d) × s × dt`.
  3. `bOverrideVelocity` → velocity set rather than added (H); `bAffectBaseVelocity` → also BaseVelocity.
- **AttractorParticle** (EmitterName, SelectionMethod, bRenewSource, Range, Strength, bStrengthByDistance, bAffectBaseVelocity):
  - spawn picks a source particle index from the named emitter (Random / Sequential, as LocationEmitter);
  - update attracts toward that particle's location with the same falloff;
  - a dead source: re-pick if bRenewSource, else no force.
- **KillBox** (LowerLeftCorner / UpperRightCorner vectors at EmitterTime, bKillInside; H bAbsolute): per update, kill particles
  inside the box (bKillInside) or outside it (default). Corners are in emitter-local space unless absolute.
- **Note:** the attractors appear in 33 + 2 systems, KillBox in 2. A binary check of these three is a follow-up if Rendering
  wants CONFIRMED.

**Batch 2 follow-up (AssetTools census + naming):**
- AssetTools' census over all 2,736 MP LODs: SIZE = Σ blocks with 0 mismatches. The leading int is the count of a fourth
  module-order list (non-empty only in the Orbit LODs).
- Named from the runners:
  - MeshRotation: int **bInheritParent** (≠ 0 adds the component rotation at spawn);
  - VelocityOverLife: int **Absolute** (spawn ≠ 0 sets Velocity = BaseVelocity = VelOverLife(t); update 0 multiplies, ≠ 0 sets);
  - SizeMultiplyVelocity: int MultiplyX / Y / Z;
  - SizeScale: int EnableX / Y / Z.
- **Gravity scalar `inst[+0x94]`: setter not found.** Ruled out: GetGravityZ, the instance constructor `0x830398E0`, the instance
  motion tick `0x8300BF18`, the PSC tick. The MP maps author no gravity, so `DefaultGravityZ = −2940` UU/s² (Xe-TransGame.ini;
  RBPhysicsGravityScaling 0.66). **Use −2940 (H).**

## (d) TypeData and the PSC tick: LOD selection, SecondsBeforeInactive, detail mode

### TypeData (already decoded in pass 5; status vs ours)

| TypeData | LODs / systems | Native | RE source | Ours |
|---|---|---|---|---|
| Beam2 | 107 / 25 | fill `0x830298E8` (replay +0xD8…+0x13C) | `notes/TARGETED_PASS5_2026-10-05.md` §9, §9h–§9j: base path = CubicInterp source → target; **BeamSineWave formula decoded** (Σ Direction·Amp·sin(2π((Speed·t + d)/Period + Phase)), +Z along the beam, endpoint fade min(6r, 1)·min(6(1 − r), 1)); noise smoothing / lock radius / NoiseRangeScale in component space; Hermite sub-steps; **NoiseTension irrelevant, NoiseTangentStrength unused**; taper × size | C. **Our header still says "render formula UNKNOWN": stale, implementable from §9i / §9j** |
| Trail2 | 170 / 50 | spawn `0x83048E40`, tick `0x83048218`, fill `0x8301A700` | §13 / §14 (+ addenda): one chain per emitter (no per-source filter, zig-zags between sources); head placed by LocationEmitter at spawn only; tessellation and U from the head by cumulative distance; spawn clamp 1 per tick; cap MaxTrailCount × MaxParticleInTrailCount | C, marked PARTIAL ("follows the source's recent path"). The §14 rules make it exact |
| Mesh | 325 / 106 | sprite / mesh fill | mesh rotation modules (bInheritParent above) | C |
| TextureTile / TextureTileDistance | beams | **not applied in the fill (C)**, MP default 1 | §9 | no action needed |

### PSC tick (`UParticleSystemComponent::Tick` = `0x83055528`; activation `0x83053088`). CONFIRMED.

**Per tick, in order:**
1. **Skip** unless active (flag bit 0x2000 or 0x80000000) with a template and emitter instances; also skip when the world query
   `0x82D3CEE0` returns 1 (H: dedicated server).
2. **Unrendered throttle** (skipped entirely if PSC flag 0x400000 is set). This is **not a full stop**:
   - `SBI = max(PSC.SecondsBeforeInactive (+0x21C), Template.SecondsBeforeInactive (+0x78))`;
   - `interval = 1.0 s` if `SBI > 0` **and** `PSC.SecondsBeforeInactive < AccumTickTime (+0x220)` **and** `LastRenderTime (+0x18C)
     + SBI < WorldTime`; **else `interval = 0.1 s`**;
   - if `LastRenderTime + 0.3 < WorldTime` (unrendered for more than 0.3 s): `skipped (+0x224) += dt`; **return** while
     `skipped < interval`.
   - So an unrendered component ticks at **10 Hz** after 0.3 s off-screen, and at **1 Hz** once it has been unrendered longer than
     SecondsBeforeInactive (and alive longer than its own SBI). **When it does tick, the accumulated time is used as one large
     step.**
3. `dt_eff = (dt + skipped) × Owner.CustomTimeDilation` (Owner +0x60); `skipped = 0`; `AccumTickTime += dt_eff`.
4. Parameter / owner bookkeeping (component attach checks).
5. **LOD distance** `0x83028C28`: `d = |Bounds.Origin (+0x80) − view location| × the view's LOD factor` (first local player view,
   view +0x2C → +0x320). It is **recomputed every executed tick; there is no LODDistanceCheckTime interval.**
6. **LOD choice** `0x83051B58`:
   - If PSC flag 0x4000000 is set, use the template's fixed LOD (+0x54).
   - Else LODMethod = `bOverrideLODMethod (flag 0x80000) ? PSC.LODMethod (+0x228) : Template.LODMethod (+0x29)`.
   - **Automatic (0):** level = the largest i ≥ 1 with `d ≥ LODDistances[i]`, else 0.
   - Any other method leaves the computed level at 0 here (H: DirectSet levels come only from `SetLODLevelIndex`).
7. **SetLODLevel** `0x83051A90`:
   - `level = clamp(level + GParticleLODBias, 0, LODCount − 1)`, where GParticleLODBias = SystemSettings ParticleLODBias = **0**
     (Xe-TransEngine.ini);
   - on change, every emitter instance gets `SetCurrentLODIndex` (vtable +0x10).
8. If `Template.SystemUpdateMode == 1` (FixedTime), `dt_eff = Template.UpdateTime_Delta` (+0x2C).
9. Emitter ticks, bounds, and the deactivate / complete checks.

**Activation detail mode** (`0x83053088`): emitter instances are created only if **PSC.DetailMode (+0xE6) ≤ the system DetailMode
(SystemSettings DetailMode = 2**, i.e. all). The initial LOD is chosen at activation by the same rule.

**Findings vs ours:**
1. **SecondsBeforeInactive** (Systems' FxInactive, default on) **stops** ticking after SBI unrendered. **The original throttles** to
   10 Hz after 0.3 s unrendered and 1 Hz after SBI, and **catches up the skipped time in one step**. So effects resume mid-life,
   not frozen. A loop that is off-screen keeps evolving. **Recommend changing FxInactive to the throttle rule above.** It is
   cheaper than ticking and closer than freezing.
2. **LOD selection** matches ours (`dist ≥ lodDistances[i]` → i). The original also scales the distance by the view LOD factor
   and adds the global bias (0). LOD is evaluated only on executed ticks, so throttled components switch LOD at 10 Hz / 1 Hz.
3. **DetailMode gate:** with the shipped DetailMode 2, every component spawns. No action.

**(d) complete. The particle module audit is complete.**
