# The 58 uncompiled weapon / prop materials: matc audit (Rendering item 6; Systems 2026-10-16)

Input: AssetTools `work/gltf_wfcmat/missing_matinst_audit.json` (58 materials that none of the 1,368 compiled keys cover, so
their meshes draw the fallback surface). Method: `tools/systems/matc58_audit.py` runs Rendering's own `tools/render/matc.py`
(`MatCompiler.build()`, imported read-only from Rebuild-Rendering) on each material. The repo is the cooked packages holding the
object (authored.db, largest copy first), the packages of every Parent link of its chain, and the shared shader packages
(TransGame, TR_ / ENV_ AllShader). No texture is exported. Full per-material data: `SYSTEMS_MATC58_AUDIT.json`.

## Result: 56 of 58 compile with matc as it is

- **56 OK.** They were missing only because no map's compile list includes them: dynamic meshes such as sentries, shields,
  pickups and vending machines are not referenced by the map worlds build_materials walks.
  - 16 of them failed on the first pass with "no master material". The MIC body in the mesh package is cooked without its
    parent; the parent lives in another package and resolves once that package is a fallback, which `parent_packages` in the
    json lists per material.
  - Textures: all resolve in ExtractedAssets except one each on OmegaLiquidSurface_MATINST and WEP_Shield_D_MATINST.
- **2 cannot be translated from the graph:** PROP_NEU_BatteryOS_p.OmegaBattery_MATINST and OmegaBatterySpikes_MATINST. Their
  graph calls the material function ENV_AllShader_p.Functions.Lighting.ENV_NormalMap_MF, which is cooked only as a 124-byte
  stub (A1_IAC_Base_m) and has no body in any package. These are the campaign Omega Supreme battery; a hand-written normal-map
  stand-in or the fallback is the only option.

## By gameplay relevance (what to compile first for the multiplayer look)

| group | materials | blend / lighting |
|---|---|---|
| MP abilities (seen every match) | WEP_SentryAbility_p.WEP_DeployedTurret_MATINST (the sentry turret: package 5 known issue); WEP_SentryBlaster01_p SentryBase / Blaster / Repair / Rocket / GlassGlow / WEP_SentryBlaster01; WEP_SentryBall_p (+ Invis); WEP_Shield_p ShieldBruteA / D (MATINST, Frame, MASK, Energon, Shield_D); WEP_KamikazeMine_p; WEP_DetPack_p (+ StandBy) | masked Phong bodies; additive / translucent unlit glow, frame and mask layers |
| MP pickups / objectives / map props | PROP_NEU_Pickups_p CodeOfPowerCORE, MPBomb (+ Explode, BombCore), RallyTotem; WEP_CodeOfPower_p; PROP_VendingMachine_p (Ammo, Glass, EnergonCube, Health, WallSwitch) | masked / opaque Phong; translucent glass |
| Campaign only | PROP_NEU_Barricade_p (barricades, force fields), PROP_NEU_BatteryOS_p / Battery_p, PROP_NEU_Interacts_p, PROP_HeartCrystal_p, PROP_dropship_dec_p, TPRO_KON_CorruptorSpike_p, TR_SoldierTank* / TR_Trypticon head, WEP_Trypticon* cannons, WEP_TurretBase_p | as listed in the json |

Caveat: some root masters read without bUsedWithSkeletalMesh in the copy the audit opened. That is not proof the original
drew the default material on those skeletal meshes (the flag may sit only in a fuller copy), so treat it as unverified.

## Further original-authored light sources (the "night and day" list)

The original's gameplay lights are only the weapon muzzle lights (shipped, 46 classes) and the death ramp lights (shipped).
TnPawn.CloakLight is never turned on (Gameplay RE). Nothing else was authored:
- **Explosions, impacts, projectiles, abilities, vehicle boost:** no light component on any projectile, ability, vehicle or
  boost class. This UE3 build has no particle light module (7,350 ParticleSystems; no ParticleModuleLight type exists), so no
  effect could emit light.
- **Pickups, energon, screens, interact objects:** emission plus particles only (SYSTEMS_VEHICLE_LIGHTS.md).
- **Vehicle headlights:** none (an invented-placement addition is in SYSTEMS_VEHICLE_BEAMS.md).

So every further light (explosion flashes, impact sparks, energon glow lights, boost glow, screen spill) would be an RT-era
addition. The obvious candidates, with emitter positions the game already has: explosion / grenade / rocket impacts
(World::radiusDamage), weapon impacts (spawnImpact), vehicle boost (the BoostFx sockets), energon / weapon pickups and
vending machines (pickup factory positions), and the barrier / shield force fields.
