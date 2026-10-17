# Weapon + melee batch: deliberate Classic (and Smart) re-baseline, 2026-10-17

Base int_ssb = integration 8a080210; new int_wb2 = 8a080210 + GAMEPLAY_WEAPON_MELEE.patch (eb08b8b1). NAVQ 12 seeds x 4 maps x Classic and
Smart, WFC_SIMHASH=1 (work/pass29/wb/navq3, steps_navq3.txt). The values were verified by the RE lane against authored.db before this run.

- SIMHASH: Classic identical on 2 of 48 (Streets 2), Smart on 4 of 48 (Streets 3, Debris 1); every other run diverges (per-weapon recoil
  moves the hand-socket shot origins; LightMedium melee impulse / carrier lunge). Tables: *_SIMHASH_old_vs_new.txt (first divergent step).
- Navigation health unchanged (means over 48 runs, base -> new):
  - Classic: travel 44.9 -> 45.0 %, speed 15.77 -> 15.89 m/s, slow 5.1 -> 5.0 %, reversals 3.02 -> 2.71 / travel-min, off-mesh 3.92 -> 3.94 %.
  - Smart: travel 39.1 -> 39.2 %, speed 15.01 -> 14.97 m/s, slow 3.2 -> 3.2 %, reversals 3.38 -> 3.31, off-mesh 2.43 -> 2.33 %.
- New references: *_NAVQ_REFERENCE_new.txt.
- Headless BOTTEST seed 7: 48/48.
