# SYSTEMS_FXLIGHTS_EXPLOSION_CAL (against integration 77a7b0f7): explosion effect lights x 0.7

Rendering's eye pass (Rust pick_504, step 840): a nearby explosion blew a large patch of Rust's bright floor out to white ->
the explosion rows' colour x 0.7; radius (12 m), falloff and duration (1.0 s) unchanged. Streets' explosion still reads
strong at 0.7. Impacts keep x 0.6 / 0.15 s (SYSTEMS_FXLIGHTS_IMPACT_CAL); muzzle flashes (a separate path), boost and
pickups unchanged. tools/systems/gen_fxlights.py CALIBRATION gains the explosion kind; FxLights.inc regenerated (the
77a7b0f7 file is exactly the generator's previous output, so the diff is these 9 rows).

| template (explosion rows) | colour before | colour after |
|---|---|---|
| Exp_2500cb_FX (mine) | 11.9 / 11.9 / 4.796 | 8.33 / 8.33 / 3.357 |
| Exp_Energy_MED_FX | 2.477 / 1.88 / 12.7 | 1.734 / 1.316 / 8.89 |
| Martyrdom_Grenade_FX | 9.9 / 9.573 / 5.108 | 6.93 / 6.701 / 3.576 |
| Exp_FusionCannon_FX | 9.4 / 9.4 / 9.4 | 6.58 / 6.58 / 6.58 |
| Explosion_GrenadeLauncher_01_FX | 13.8 / 12.517 / 2.111 | 9.66 / 8.762 / 1.478 |
| Squib_HomRocket_01_FX | 13.3 / 13.167 / 12.369 | 9.31 / 9.217 / 8.658 |
| Squib_HealGrenade_FX | 0.0 / 0.106 / 13.3 | 0.0 / 0.074 / 9.31 |
| Squib_PlasmaCannon_Lrg_FX | 5.2 / 5.002 / 4.878 | 3.64 / 3.502 / 3.414 |
| Squib_PlasmaCannon_Sm_FX | 5.617 / 6.9 / 6.521 | 3.932 / 4.83 / 4.564 |

Presentation only, values only (effect lights are added only when the last drawn frame was Realistic).
