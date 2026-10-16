# SYSTEMS_FXLIGHTS_IMPACT_CAL (against integration f369baa2): impact effect lights calibrated

Rendering's calibration (Realistic lockstep Streets / Gorge / Molten, effect lights on vs off, transient gain 2):
explosions read right (fx_508_m01500.png: the blast lights the floor and the nearby robots) - unchanged; impacts threw a
green-white wash over a stretch of floor (fx_509_m01100.png) -> colour x 0.6, duration 0.25 -> 0.15 s (a flash, not a
glow). Boost and pickups unchanged (rechecked in the eye pass). Headlights are a separate table, unchanged.

How: tools/systems/gen_fxlights.py (agents/systems) gains CALIBRATION = {'impact (spawnImpact)': colour 0.6, 0.15 s} on top of
SYSTEMS_RT_LIGHT_HOOKS.json's proposals; src/game/FxLights.inc is regenerated (the unchanged generator reproduces
f369baa2's file byte for byte, so the diff is only the calibration + the stale "opt-in" header line).

| template (impact rows) | colour before | colour after | duration |
|---|---|---|---|
| Impact_AssaultRifle_FX | 5.65 / 10.7 / 3.552 | 3.39 / 6.42 / 2.131 | 0.25 -> 0.15 s |
| Impact_HeavyMG_FX | 5.874 / 10.7 / 3.745 | 3.525 / 6.42 / 2.247 | 0.25 -> 0.15 s |
| Squib_EMPShotgun_FX | 5.778 / 10.7 / 3.67 | 3.467 / 6.42 / 2.202 | 0.25 -> 0.15 s |
| Squib_HeavyPistol_FX | 11.085 / 11.085 / 11.3 | 6.651 / 6.651 / 6.78 | 0.25 -> 0.15 s |
| Impact_IonBlaster_FX | 6.3 / 6.3 / 6.3 | 3.78 / 3.78 / 3.78 | 0.25 -> 0.15 s |
| Impact_IonTurret_FX | 5.65 / 10.7 / 3.552 | 3.39 / 6.42 / 2.131 | 0.25 -> 0.15 s |
| Impact_TurretIon_OLD | 4.415 / 3.775 / 5.2 | 2.649 / 2.265 / 3.12 | 0.25 -> 0.15 s |
| Squib_RepairEnemy_FX | 1.686 / 1.827 / 3.2 | 1.012 / 1.096 / 1.92 | 0.25 -> 0.15 s |
| Squib_TurretRepairEnemy_FX | 1.686 / 1.827 / 3.2 | 1.012 / 1.096 / 1.92 | 0.25 -> 0.15 s |

Radius and falloff unchanged. Presentation only: effect lights are added only when the last drawn frame was Realistic
(fxRtMode_ == 2, SYSTEMS_FXLIGHTS_DEFAULT_ON), so the sim and Off / Faithful are untouched by construction; the table is
values only (no code), so no SIMHASH run was needed for this change.
