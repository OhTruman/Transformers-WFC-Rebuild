# RT-era light hooks: proposals (Realistic only, off by default; NOT original lights)

The original authors no lights for these effects (SYSTEMS_MATC58_AUDIT.md: no particle light module in this UE3 build). Each row is an effect the game already spawns, with its authored colour / size / time read from Rendering's decoded templates (map_fx_runtime.json) by tools/systems/rt_light_hooks.py, and a proposed light for Rendering to pick and calibrate. Hook = where World spawns the effect (the light would spawn there, same position). No code until Rendering picks rows; the patch would gate on Realistic + a WFC_ key (default off until adopted).

Method: bright emitters = not smoke / cloud / debris / dust / dark; colour = StartColor x colour-over-life at its peak, magnitude-weighted, normalised; size = StartSize x size-over-life peak; time = emitter duration + lifetime. Proposal: radius = clamp(2.5 x size) per hook (explosion 3-12 m, impact 1-4 m, boost 1-6 m, pickup 1-4 m), brightness = clamp(2 log2(1 + peak), 1, 16), falloff 2, one-shots fade over their time (explosion max 1 s, impact flash max 0.25 s), loops continuous.

| hook | template | users | colour (norm.) | peak | size m | time s | radius m | brightness | duration |
|---|---|---|---|---|---|---|---|---|---|
| explosion (radiusDamage / projectile impact) | Squib_HomRocket_01_FX | 5 | [1.0, 0.99, 0.93] | 100.0 | 45.00 | 1.35 | 12.0 | 13.3 | 1.0 |
| explosion (radiusDamage / projectile impact) | Squib_PlasmaCannon_Lrg_FX | 3 | [1.0, 0.962, 0.938] | 5.0 | 40.50 | 2.1 | 12.0 | 5.2 | 1.0 |
| explosion (radiusDamage / projectile impact) | Explosion_GrenadeLauncher_01_FX | 2 | [1.0, 0.907, 0.153] | 120.0 | 60.00 | 1.0 | 12.0 | 13.8 | 1.0 |
| explosion (radiusDamage / projectile impact) | Exp_FusionCannon_FX | 1 | [1.0, 1.0, 1.0] | 25.0 | 45.00 | 1.5 | 12.0 | 9.4 | 1.0 |
| explosion (radiusDamage / projectile impact) | Exp_Energy_MED_FX | 1 | [0.195, 0.148, 1.0] | 80.0 | 60.00 | 2.5 | 12.0 | 12.7 | 1.0 |
| explosion (radiusDamage / projectile impact) | Squib_HealGrenade_FX | 1 | [0.0, 0.008, 1.0] | 100.0 | 18.00 | 2.2 | 12.0 | 13.3 | 1.0 |
| explosion (radiusDamage / projectile impact) | Exp_2500cb_FX | 1 | [1.0, 1.0, 0.403] | 60.0 | 90.00 | 1.1 | 12.0 | 11.9 | 1.0 |
| explosion (radiusDamage / projectile impact) | Squib_PlasmaCannon_Sm_FX | 1 | [0.814, 1.0, 0.945] | 10.0 | 18.00 | 1.6 | 12.0 | 6.9 | 1.0 |
| explosion (radiusDamage / projectile impact) | Martyrdom_Grenade_FX | 1 | [1.0, 0.967, 0.516] | 30.0 | 60.00 | 1.6 | 12.0 | 9.9 | 1.0 |
| impact (spawnImpact) | Impact_AssaultRifle_FX | 6 | [0.528, 1.0, 0.332] | 40.0 | 1.00 | 4.1 | 2.5 | 10.7 | 0.25 |
| impact (spawnImpact) | Impact_TurretIon_OLD | 3 | [0.849, 0.726, 1.0] | 5.0 | 15.00 | 4.1 | 4.0 | 5.2 | 0.25 |
| impact (spawnImpact) | Squib_EMPShotgun_FX | 2 | [0.54, 1.0, 0.343] | 40.0 | 6.75 | 1.25 | 4.0 | 10.7 | 0.25 |
| impact (spawnImpact) | Impact_IonTurret_FX | 2 | [0.528, 1.0, 0.332] | 40.0 | 1.12 | 4.1 | 2.8 | 10.7 | 0.25 |
| impact (spawnImpact) | Squib_TurretRepairEnemy_FX | 2 | [0.527, 0.571, 1.0] | 2.0 | 12.00 | 1.6 | 4.0 | 3.2 | 0.25 |
| impact (spawnImpact) | Impact_HeavyMG_FX | 1 | [0.549, 1.0, 0.35] | 40.0 | 6.75 | 4.8 | 4.0 | 10.7 | 0.25 |
| impact (spawnImpact) | Squib_HeavyPistol_FX | 1 | [0.981, 0.981, 1.0] | 50.0 | 5.00 | 5.0 | 4.0 | 11.3 | 0.25 |
| impact (spawnImpact) | Impact_IonBlaster_FX | 1 | [1.0, 1.0, 1.0] | 8.0 | 7.50 | 0.8 | 4.0 | 6.3 | 0.25 |
| impact (spawnImpact) | Squib_RepairEnemy_FX | 1 | [0.527, 0.571, 1.0] | 2.0 | 12.00 | 1.6 | 4.0 | 3.2 | 0.25 |
| vehicle boost (BoostFx socket) | Afterburner_D_FX | 25 | [1.0, 0.722, 0.611] | 15.0 | 6.00 | loop | 6.0 | 8.0 | continuous |
| vehicle boost (BoostFx socket) | Trails_Jet_D_FX | 20 | [1.0, 1.0, 1.0] | 2.0 | 0.14 | loop | 1.0 | 3.2 | continuous |
| vehicle boost (BoostFx socket) | Afterburner_A_FX | 19 | [1.0, 0.722, 0.611] | 15.0 | 6.00 | loop | 6.0 | 8.0 | continuous |
| vehicle boost (BoostFx socket) | Booster_1_FX | 13 | [0.55, 0.65, 1.0] | 1.0 | 1.00 | loop | 2.5 | 2.0 | continuous |
| vehicle boost (BoostFx socket) | Trails_Jet_A_FX | 12 | [1.0, 1.0, 1.0] | 2.0 | 0.14 | loop | 1.0 | 3.2 | continuous |
| vehicle boost (BoostFx socket) | bumble_boost_small1_FX | 10 | [1.0, 0.332, 0.024] | 15.1 | 2.92 | loop | 6.0 | 8.0 | continuous |
| pickup factory | Pickup_FX | ammo / weapon / objective factories | [1.0, 0.8, 0.3] | 0.5 | 0.02 | loop | 1.0 | 1.2 | continuous |
| pickup factory | HealthPickup_FX | health factories | [0.013, 0.375, 1.0] | 50.0 | 6.07 | loop | 4.0 | 11.3 | continuous |
| pickup factory | OvershieldPickup_FX | overshield factories | [0.02, 0.314, 1.0] | 25.0 | 10.11 | loop | 4.0 | 9.4 | continuous |

Users: weapon classes using the template (explosions / impacts), vehicle forms (boost), factory kinds (pickups). Per-emitter detail (material, colour, size, time): SYSTEMS_RT_LIGHT_HOOKS.json.

Caveats: "size m" is the largest bright sprite including its growth over life (explosion fireballs grow to 18-90 m sprites), so the radius caps decide; the colour is the particle tint and ignores the sprite texture (white rows are texture-coloured: sample the texture if it matters); peak = the overbright factor of the brightest emitter, a relative scale, not a calibrated intensity. Impact rows share templates across many weapons (Impact_AssaultRifle_FX: 6 classes). Rendering calibrates by eye; then the code patch gates on Realistic + a WFC_ key, default off until adopted.
