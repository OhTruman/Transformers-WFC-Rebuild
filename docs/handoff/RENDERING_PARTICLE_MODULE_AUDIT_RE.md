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
