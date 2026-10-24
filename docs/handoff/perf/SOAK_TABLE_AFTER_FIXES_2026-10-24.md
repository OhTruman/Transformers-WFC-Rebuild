# 300 fps sim soaks after the fixes (2026-10-24, headless, 5975369b + GAMEPLAY_PRELOAD_ITEM4 + GAMEPLAY_KISMET_LATENT_LIST)

ESC rows with WFC_SOAK_KILLEVERY=2 (waves advance), 600 s; chapters 120 s, D1 / D5 300 s; Streets 4v5 120 s.

| Row | Steps | Mean ms | p99 ms | Max ms | > 1.5 ms | Max horde | Max LOS / searches / thinks per step | Worst-20 top slots (mean ms) |
|---|---|---|---|---|---|---|---|---|
| CP_A1_IAC | 7201 | 0.15 | 0.26 | 5.58 | 0.0 % | 0 | 0 / 0 / 1 | match 0.64, cp.matinee 0.36, cp.vm 0.14, oppAnim(cpu) 0.05 |
| CP_A2_KON | 7201 | 0.19 | 0.26 | 4.42 | 0.0 % | 3 | 0 / 0 / 1 | match 0.32, cp.vm 0.18, cp.matinee 0.08, oppAnim(cpu) 0.04 |
| CP_A3_COR | 7201 | 0.16 | 0.24 | 1.05 | 0.0 % | 0 | 0 / 0 / 1 | match 0.13, bots 0.11, cp.vm 0.06, oppMove 0.05 |
| CP_A4_ORB | 7201 | 0.15 | 0.32 | 4.26 | 0.0 % | 0 | 0 / 0 / 1 | match 0.57, cp.vm 0.44, cp.matinee 0.08, oppAnim(cpu) 0.04 |
| CP_A5_TRY | 7201 | 0.13 | 0.16 | 4.13 | 0.0 % | 1 | 0 / 0 / 3 | oppMove 0.08, match 0.07, oppAnim(cpu) 0.05, partWeapons 0.03 |
| CP_D1_ORB | 18001 | 0.76 | 1.39 | 4.26 | 0.4 % | 5 | 14 / 0 / 4 | match 1.06, cp.sentries+mines 0.83, bots 0.25, bot.steer 0.21 |
| CP_D2_COR | 7201 | 0.38 | 0.92 | 4.61 | 0.0 % | 0 | 0 / 0 / 1 | match 0.92, cp.vm 0.70, cp.matinee 0.15, oppMove 0.03 |
| CP_D3A_IAC | 7201 | 0.34 | 0.51 | 6.15 | 0.0 % | 10 | 6 / 0 / 3 | match 0.40, partWeapons 0.32, glue.shotFx 0.29, bots 0.26 |
| CP_D3B_IAC | 4250 | 0.18 | 0.51 | 5.33 | 0.0 % | 4 | 0 / 0 / 2 | match 0.41, oppMove 0.17, cp.vm 0.13, oppAnim(cpu) 0.06 |
| CP_D5_OME | 18001 | 0.12 | 0.20 | 3.41 | 0.0 % | 2 | 5 / 0 / 3 | partWeapons 0.29, glue.shotFx 0.25, oppAnim(cpu) 0.13, oppMove 0.08 |
| ESC_BrokenHope | 35940 | 0.13 | 0.34 | 11.01 | 0.0 % | 14 | 4 / 0 / 4 | partWeapons 0.21, oppMove 0.20, glue.shotFx 0.16, bots 0.12 |
| ESC_District | 24102 | 0.08 | 0.17 | 1.94 | 0.0 % | 8 | 3 / 0 / 5 | partWeapons 0.22, glue.shotFx 0.18, oppAnim(cpu) 0.05, oppMove 0.04 |
| ESC_Pulse | 35940 | 0.15 | 0.34 | 22.34 | 0.0 % | 14 | 2 / 0 / 4 | bots 0.27, bot.steer 0.24, partWeapons 0.12, oppMove 0.12 |
| ESC_Remnant | 35940 | 0.07 | 0.21 | 2.13 | 0.0 % | 1 | 1 / 0 / 1 | bots 0.21, bot.steer 0.19, oppMove 0.07, oppAnim(cpu) 0.06 |
| ESC_Static | 9444 | 0.06 | 0.21 | 2.11 | 0.0 % | 1 | 1 / 0 / 1 | bots 0.17, oppMove 0.09, oppAnim(cpu) 0.07, match 0.01 |
| ESC_Tranquillity | 35940 | 0.10 | 0.23 | 6.10 | 0.0 % | 11 | 3 / 0 / 3 | abilities 0.30, bots 0.22, partWeapons 0.20, glue.shotFx 0.17 |
| TDM_Streets | 7741 | 0.19 | 0.31 | 76.44 | 0.2 % | 0 | 22 / 0 / 9 | projectiles 10.51, bots 3.71, bot.aim 3.63, partWeapons 1.10 |

## Steps over 2.5 ms
```
== CP_A1_IAC 1 steps over 2.5 ms
   step 61 total 5.6 (prefix 1.7) horde 0 | match 1.6, oppMove 0.2, oppAnim(cpu) 0.2
== CP_A2_KON 1 steps over 2.5 ms
   step 61 total 4.4 (prefix 1.3) horde 0 | match 1.2, cp.vm 0.3, oppMove 0.2
== CP_A4_ORB 1 steps over 2.5 ms
   step 61 total 4.3 (prefix 1.2) horde 0 | match 1.2, cp.vm 0.5, bots 0.1
== CP_A5_TRY 1 steps over 2.5 ms
   step 61 total 4.1 (prefix 0.6) horde 1 | oppMove 0.8, match 0.6, oppAnim(cpu) 0.2
== CP_D1_ORB 2 steps over 2.5 ms
   step 61 total 4.3 (prefix 1.1) horde 3 | match 1.0, oppAnim(cpu) 0.7, cp.vm 0.5
   step 5028 total 2.7 (prefix 0.3) horde 3 | bots 2.1, bot.steer 2.1, match 0.2
== CP_D2_COR 1 steps over 2.5 ms
   step 61 total 4.6 (prefix 1.3) horde 0 | match 1.3, oppMove 0.2, oppAnim(cpu) 0.1
== CP_D3A_IAC 3 steps over 2.5 ms
   step 61 total 6.2 (prefix 3.0) horde 1 | match 2.9, cp.vm 1.7, oppAnim(cpu) 0.2
   step 4652 total 4.5 (prefix 0.1) horde 10 | partWeapons 3.1, glue.shotFx 3.0, bots 1.2
   step 4756 total 3.0 (prefix 0.1) horde 10 | partWeapons 2.7, glue.shotFx 2.7, oppMove 0.1
== CP_D3B_IAC 2 steps over 2.5 ms
   step 61 total 5.3 (prefix 2.2) horde 0 | match 2.2, cp.vm 1.4, oppMove 0.2
   step 4310 total 4.7 (prefix 4.5) horde 4 | match 4.4, oppAnim(cpu) 0.1, oppMove 0.1
== CP_D5_OME 3 steps over 2.5 ms
   step 61 total 3.4 (prefix 0.3) horde 1 | oppAnim(cpu) 0.6, match 0.3, oppMove 0.3
   step 1863 total 2.8 (prefix 0.0) horde 2 | partWeapons 2.7, glue.shotFx 2.6, oppAnim(cpu) 0.0
   step 1909 total 2.6 (prefix 0.0) horde 2 | partWeapons 2.5, glue.shotFx 2.5, oppAnim(cpu) 0.0
== ESC_BrokenHope 1 steps over 2.5 ms
   step 32076 total 11.0 (prefix 10.7) horde 9 | oppAnim(cpu) 0.1, partWeapons 0.1, oppMove 0.1
== ESC_Pulse 1 steps over 2.5 ms
   step 31803 total 22.3 (prefix 21.8) horde 10 | bots 0.4, bot.steer 0.3, oppAnim(cpu) 0.1
== ESC_Tranquillity 1 steps over 2.5 ms
   step 16878 total 6.1 (prefix 6.0) horde 6 | abilities 5.9, oppAnim(cpu) 0.0, oppMove 0.0
== TDM_Streets 12 steps over 2.5 ms
   step 1492 total 76.4 (prefix 0.0) horde 0 | projectiles 76.3, oppAnim(cpu) 0.1, bots 0.0
   step 1813 total 72.9 (prefix 0.0) horde 0 | projectiles 72.7, oppAnim(cpu) 0.1, oppMove 0.1
   step 6638 total 72.5 (prefix 0.0) horde 0 | bots 72.3, bot.aim 72.3, oppAnim(cpu) 0.2
   step 5010 total 61.1 (prefix 0.0) horde 0 | projectiles 60.8, bots 0.2, oppAnim(cpu) 0.1
   step 1927 total 3.7 (prefix 0.0) horde 0 | partWeapons 3.5, glue.shotFx 3.5, oppAnim(cpu) 0.1
   step 1458 total 2.9 (prefix 0.0) horde 0 | partWeapons 2.8, glue.shotFx 2.7, oppAnim(cpu) 0.1
```

## Rerun (same SIMHASH stream): Streets, BrokenHope, Pulse
```
== TDM_Streets 8 steps over 2.5 ms
   step 1458 total 3.0 (prefix 0.0) horde 0 | partWeapons 2.8, glue.shotFx 2.8, oppAnim(cpu) 0.1
   step 1541 total 3.0 (prefix 0.0) horde 0 | partWeapons 2.8, glue.shotFx 2.8, oppAnim(cpu) 0.1
   step 1927 total 2.9 (prefix 0.0) horde 0 | partWeapons 2.7, glue.shotFx 2.7, oppAnim(cpu) 0.1
   step 1665 total 2.6 (prefix 0.0) horde 0 | partWeapons 2.5, glue.shotFx 2.5, oppAnim(cpu) 0.1
   step 1467 total 2.6 (prefix 0.0) horde 0 | partWeapons 2.4, glue.shotFx 2.4, oppAnim(cpu) 0.1
   step 1781 total 2.6 (prefix 0.0) horde 0 | partWeapons 2.4, glue.shotFx 2.4, oppAnim(cpu) 0.1
```

## Reading

- Gone versus the first table: the chapter first-use loads, the Kismet Character-target roster parse and the 2-5.5 ms matinee-end nav syncs. A1's worst step went from 38 ms to 1.0 ms, A3's from 10 to 1.05, D5's from 38 to 3.4 and D3B's from 64 to 5.3 (except step 61).
- Not reproducible, so machine contention (other lanes ran in parallel): the 60-76 ms Streets steps, BrokenHope 32076 (11 ms) and Pulse 31803 (22 ms). The rerun has the same SIMHASH stream and no such steps.
- Reproducible and owned by Systems (sent, with the exact envs):
  - glue.shotFx at 2.4-3.1 ms on many shots: D5 EMPShotgun 1863 / 1909, D3A 4652 / 4756, Streets 1458-1927;
  - Tranquillity 16878, 6 ms: the first melee death per horde chassis loads its CharacterAudio cue set in the step.
- Left in Gameplay's lane:
  - step 61 (the first measured step, 3.4-6.2 ms on every chapter: "match" + remainder);
  - D3B 4310 (4.7 ms, "match");
  - D1 5028 (2.1 ms bot.steer);
  - D1 mean 0.76 ms (cp.sentries+mines 0.83 in its worst steps).
