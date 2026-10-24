# 300 fps sim soaks, worst-step table (2026-10-24, headless, 48e42af6 + GAMEPLAY_TICKLOG.patch)

## Plain soaks (ESC 600 s with no forced kills: the horde stays in wave 1; chapters 120 s, D1 / D5 300 s)
| Row | Steps | Mean ms | p99 ms | Max ms | > 1.5 ms | Max horde | Max LOS / searches / thinks per step | Worst-20 top slots (mean ms) |
|---|---|---|---|---|---|---|---|---|
| CP_A1_IAC | 7201 | 0.18 | 0.26 | 38.06 | 0.2 % | 0 | 0 / 0 / 1 | match 3.28, cp.vm 2.43, cp.matinee 0.76, oppAnim(cpu) 0.04 |
| CP_A2_KON | 7201 | 0.22 | 0.29 | 4.63 | 0.0 % | 3 | 0 / 0 / 1 | match 0.43, cp.vm 0.28, cp.matinee 0.09, oppAnim(cpu) 0.05 |
| CP_A3_COR | 7201 | 0.19 | 0.27 | 10.38 | 0.0 % | 0 | 0 / 0 / 1 | match 0.91, cp.matinee 0.54, cp.vm 0.34, bots 0.09 |
| CP_A4_ORB | 7201 | 0.16 | 0.32 | 10.65 | 0.0 % | 0 | 0 / 0 / 1 | match 0.84, cp.vm 0.61, cp.matinee 0.18, oppAnim(cpu) 0.04 |
| CP_A5_TRY | 7201 | 0.13 | 0.15 | 5.05 | 0.0 % | 1 | 0 / 0 / 3 | oppMove 0.08, match 0.08, oppAnim(cpu) 0.05, cp.vm 0.03 |
| CP_D1_ORB | 18001 | 0.74 | 1.23 | 21.11 | 0.0 % | 5 | 14 / 0 / 4 | match 1.24, cp.sentries+mines 0.82, cp.vm 0.26, cp.matinee 0.12 |
| CP_D2_COR | 7201 | 0.38 | 0.87 | 10.48 | 0.0 % | 0 | 0 / 0 / 1 | match 1.03, cp.vm 0.82, cp.matinee 0.15, oppAnim(cpu) 0.03 |
| CP_D3A_IAC | 7201 | 0.39 | 0.58 | 11.13 | 0.1 % | 10 | 6 / 0 / 3 | match 0.91, cp.vm 0.75, partWeapons 0.30, glue.shotFx 0.27 |
| CP_D3B_IAC | 4250 | 0.20 | 0.54 | 63.84 | 0.1 % | 4 | 0 / 0 / 2 | match 4.07, cp.matinee 0.50, cp.vm 0.34, oppMove 0.24 |
| CP_D5_OME | 18001 | 0.12 | 0.17 | 38.56 | 0.0 % | 2 | 5 / 0 / 3 | match 2.18, cp.vm 2.13, partWeapons 0.27, glue.shotFx 0.24 |
| ESC_BrokenHope | 35940 | 0.15 | 0.28 | 901.55 | 0.0 % | 7 | 5 / 0 / 1 | abilities 45.06, bots 7.62, oppMove 0.42, partWeapons 0.10 |
| ESC_District | 35940 | 0.14 | 1.65 | 13.80 | 1.1 % | 4 | 2 / 0 / 1 | bots 2.44, bot.steer 2.43, partWeapons 2.28, oppMove 1.85 |
| ESC_Pulse | 35940 | 0.14 | 0.22 | 9.42 | 0.0 % | 6 | 0 / 0 / 1 | oppMove 0.93, bots 0.45, bot.steer 0.44, partWeapons 0.29 |
| ESC_Remnant | 35940 | 0.11 | 0.21 | 2.48 | 0.0 % | 5 | 0 / 0 / 1 | oppMove 0.15, bots 0.15, bot.steer 0.12, oppAnim(cpu) 0.08 |
| ESC_Static | 35940 | 0.19 | 0.28 | 911.01 | 0.0 % | 5 | 2 / 0 / 1 | abilities 45.54, bots 0.23, partWeapons 0.12, oppMove 0.09 |
| ESC_Tranquillity | 35940 | 0.11 | 0.18 | 6.51 | 0.0 % | 7 | 3 / 0 / 2 | projectiles 0.33, partWeapons 0.16, glue.shotFx 0.07, bots 0.06 |
| TDM_Streets | 7741 | 0.15 | 0.31 | 3.14 | 0.1 % | 0 | 22 / 0 / 9 | partWeapons 1.07, glue.shotFx 1.03, oppAnim(cpu) 0.40, projectiles 0.19 |

## ESC with waves advancing (WFC_SOAK_KILLEVERY=2, run a / run b)
| Row | Steps | Mean ms | p99 ms | Max ms | > 1.5 ms | Max horde | Max LOS / searches / thinks per step | Worst-20 top slots (mean ms) |
|---|---|---|---|---|---|---|---|---|
| ESC_BrokenHope | 35940 | 0.13 | 0.34 | 6.38 | 0.0 % | 14 | 4 / 0 / 4 | oppMove 0.22, partWeapons 0.22, glue.shotFx 0.16, bots 0.13 |
| ESC_District | 24102 | 0.08 | 0.16 | 2.21 | 0.0 % | 8 | 3 / 0 / 5 | partWeapons 0.24, glue.shotFx 0.20, bots 0.05, oppMove 0.04 |
| ESC_Pulse | 35940 | 0.15 | 0.35 | 37.42 | 0.0 % | 14 | 2 / 0 / 4 | match 1.88, sv.director 1.87, bots 0.27, bot.steer 0.20 |
| ESC_Remnant | 35940 | 0.07 | 0.20 | 1.89 | 0.0 % | 1 | 1 / 0 / 1 | bots 0.20, bot.steer 0.17, oppMove 0.06, oppAnim(cpu) 0.05 |
| ESC_Static | 9444 | 0.07 | 0.21 | 2.21 | 0.0 % | 1 | 1 / 0 / 1 | bots 0.18, oppMove 0.09, oppAnim(cpu) 0.07, match 0.02 |
| ESC_Tranquillity | 35940 | 0.10 | 0.23 | 5.74 | 0.0 % | 11 | 3 / 0 / 3 | abilities 0.28, bots 0.22, partWeapons 0.21, glue.shotFx 0.18 |

| Row | Steps | Mean ms | p99 ms | Max ms | > 1.5 ms | Max horde | Max LOS / searches / thinks per step | Worst-20 top slots (mean ms) |
|---|---|---|---|---|---|---|---|---|
| ESC_BrokenHope | 35940 | 0.13 | 0.34 | 1.83 | 0.0 % | 14 | 4 / 0 / 4 | oppMove 0.25, partWeapons 0.20, glue.shotFx 0.17, bots 0.12 |
| ESC_District | 24102 | 0.08 | 0.17 | 2.15 | 0.0 % | 8 | 3 / 0 / 5 | partWeapons 0.23, glue.shotFx 0.19, bots 0.08, oppMove 0.05 |
| ESC_Pulse | 35940 | 0.15 | 0.37 | 1.99 | 0.0 % | 14 | 2 / 0 / 4 | bots 0.26, bot.steer 0.19, partWeapons 0.12, oppMove 0.11 |
| ESC_Remnant | 35940 | 0.07 | 0.20 | 1.87 | 0.0 % | 1 | 1 / 0 / 1 | bots 0.20, bot.steer 0.17, oppMove 0.08, oppAnim(cpu) 0.07 |
| ESC_Static | 9444 | 0.07 | 0.22 | 2.02 | 0.0 % | 1 | 1 / 0 / 1 | bots 0.19, oppMove 0.09, oppAnim(cpu) 0.06, match 0.02 |
| ESC_Tranquillity | 35940 | 0.10 | 0.24 | 6.35 | 0.0 % | 11 | 3 / 0 / 3 | abilities 0.31, bots 0.22, partWeapons 0.22, glue.shotFx 0.19 |

## Steps over 5 ms (plain soaks)
```
== CP_A1_IAC 4 steps over 5.0 ms
   step 781 total 38.1 (prefix 37.9) horde 0 | match 37.9, cp.vm 37.8, cp.matinee 0.1
   step 61 total 22.2 (prefix 18.5) horde 0 | match 1.2, cp.vm 0.2, partWeapons 0.2
   step 62 total 8.7 (prefix 8.4) horde 0 | oppAnim(cpu) 0.1, oppMove 0.1, partWeapons 0.1
   step 2452 total 5.4 (prefix 5.3) horde 0 | match 5.3, cp.vm 5.3, partWeapons 0.0
== CP_A3_COR 3 steps over 5.0 ms
   step 1887 total 10.4 (prefix 10.3) horde 0 | match 10.2, cp.matinee 10.2, oppAnim(cpu) 0.0
   step 62 total 7.2 (prefix 6.9) horde 0 | oppAnim(cpu) 0.2, oppMove 0.2, partWeapons 0.1
   step 1844 total 5.0 (prefix 4.9) horde 0 | match 4.9, cp.vm 4.9, cp.matinee 0.0
== CP_A4_ORB 1 steps over 5.0 ms
   step 61 total 10.6 (prefix 7.5) horde 0 | match 1.4, cp.vm 0.5, oppMove 0.1
== CP_A5_TRY 1 steps over 5.0 ms
   step 61 total 5.1 (prefix 1.3) horde 1 | oppMove 0.9, match 0.6, oppAnim(cpu) 0.2
== CP_D1_ORB 2 steps over 5.0 ms
   step 61 total 21.1 (prefix 17.8) horde 3 | match 1.0, oppAnim(cpu) 0.7, cp.vm 0.4
   step 9056 total 5.7 (prefix 5.4) horde 5 | match 5.3, cp.vm 4.5, cp.sentries+mines 0.7
== CP_D2_COR 1 steps over 5.0 ms
   step 61 total 10.5 (prefix 7.2) horde 0 | match 1.1, oppMove 0.2, oppAnim(cpu) 0.2
== CP_D3A_IAC 1 steps over 5.0 ms
   step 61 total 11.1 (prefix 8.1) horde 1 | match 7.0, cp.vm 5.6, oppMove 0.2
== CP_D3B_IAC 2 steps over 5.0 ms
   step 4310 total 63.8 (prefix 63.7) horde 4 | match 63.6, oppAnim(cpu) 0.1, oppMove 0.1
   step 61 total 6.3 (prefix 3.4) horde 0 | match 2.3, cp.vm 1.6, oppMove 0.1
== CP_D5_OME 1 steps over 5.0 ms
   step 1781 total 38.6 (prefix 38.2) horde 2 | match 38.2, cp.vm 38.2, bots 0.2
== ESC_BrokenHope 2 steps over 5.0 ms
   step 3485 total 901.6 (prefix 901.3) horde 1 | abilities 901.1, oppMove 0.1, match 0.1
   step 3068 total 152.2 (prefix 0.0) horde 3 | bots 151.9, oppMove 0.1, oppAnim(cpu) 0.1
== ESC_District 55 steps over 5.0 ms
   step 24555 total 13.8 (prefix 7.4) horde 4 | oppMove 3.4, bots 1.7, bot.steer 1.7
   step 24548 total 13.3 (prefix 3.4) horde 4 | oppMove 4.3, bots 2.9, bot.steer 2.9
   step 21778 total 11.9 (prefix 3.9) horde 4 | bots 4.7, bot.steer 4.6, partWeapons 2.9
   step 24048 total 10.4 (prefix 5.4) horde 4 | bots 2.3, bot.steer 2.3, oppMove 2.3
   step 20926 total 10.2 (prefix 0.4) horde 4 | partWeapons 6.8, oppMove 1.9, bots 1.0
   step 22081 total 10.2 (prefix 0.1) horde 4 | partWeapons 7.9, bots 1.5, bot.steer 1.4
== ESC_Pulse 2 steps over 5.0 ms
   step 30890 total 9.4 (prefix 0.5) horde 6 | oppMove 6.8, partWeapons 1.6, bots 0.4
   step 30885 total 6.3 (prefix 0.5) horde 6 | oppMove 3.1, partWeapons 1.1, presentedFill 0.8
== ESC_Static 1 steps over 5.0 ms
   step 2371 total 911.0 (prefix 910.8) horde 4 | abilities 910.7, oppMove 0.1, oppAnim(cpu) 0.1
== ESC_Tranquillity 1 steps over 5.0 ms
   step 5902 total 6.5 (prefix 0.0) horde 6 | projectiles 6.4, oppAnim(cpu) 0.0, oppMove 0.0
```

## Findings (the spike sources, by evidence)

- Steady state already meets the target: every row's p99 is under 1.7 ms and most are under 0.4 ms. The 1.5 ms target fails only on isolated steps.
- Load-on-first-use is the dominant spike class, and these spikes are not reproducible across reps (cache-warm reruns shrink them):
  - BrokenHope / Static 900 ms in "abilities": the first Shockwave loads the ability models (ensureAbilityModels, chassis Truck / Truck3 / Tank3 at 80-250 ms each, gibs);
  - Pulse 37 ms in sv.director at the first wave spawn;
  - D3B 64 ms in "match" plus the "weapon model ... loaded during play" warnings (Axe, ShortSword, ViciousAxe, TortureSword, SpikedMace).
  - Fix: preload at map load. Lossless, and no sim change by construction.
- Campaign Kismet / matinee steps, reproducible:
  - A1 steps 781 / 2452 at 5 ms (cp.vm);
  - D5 step 1781 at 7-38 ms (cp.vm: teleports + factory spawn of CMP_OmegaSupreme + matinee clip load);
  - A3 step 1887 at 10 ms (cp.matinee).
  - The clip load in cpPlayPawnClip (loadAnimationsByName on first play) is the matinee part. Fix: preload every pawn group's clip_map at chapter load. The vm part goes to plan fix F.
- District (no forced kills): 55 steps at 5-14 ms across oppMove / bot.steer / partWeapons, with 4 horde. They vanish in the forced-kill reps, so they come from the stuck-horde state. Watch item.
- Tranquillity: 5.6-6.4 ms in "abilities" in both reps (step ~16878). Reproducible; to be profiled.
- Path searches per step were 0 throughout the soaks; LOS rays per step peak at 22 (Streets) and 14 (D1). Fixes A / B / D have little to buy in these rows.
