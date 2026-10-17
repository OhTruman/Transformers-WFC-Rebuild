# Package gate - final 9350f19 (penultimate 7cd2202, base 398b27ef + yaw + drive-probe + steer + walkable + rise-snap 63368d05)

Respin of 4078e47: the regress and the delta FAST are new on 9350f19; leak, the 1b Realistic perf rows, the eye pass and the penultimate FAST 7cd2202 are carried from 4078e47 (the respin cannot move them).

Base and final play the same match (Classic SIMHASH identical over the measured steps); the final draws more by design, so the regress reads the frame cost of the package-6 features at the same match.

| part | verdict |
|---|---|
| regress | REPORT - 8 map(s) above +0.35 - attributed: death bodies: a faithful presentation the original had at 10 players, about 0.2 ms per live body at 63 bots; GPU-skinned corpses = package 7 item 1 |
| Realistic perf | REPORT - all maps >= 60 fps (p50) |
| leak | PASS |
| eyepass | sheets |
| FAST 7cd2202 | PASS |
| FAST 9350f19 | PASS |

## 1. Regress (3-arm lockstep, RT off, Classic, best p90 of 2, ms)

| map | base | final | final lights off | lights off - base (report, <= +0.35) | lights cost (report, <= +0.35) | workload base -> final (same match) | verdict |
|---|---|---|---|---|---|---|---|
| 501 | 2.14 | 3.49 | 3.43 | +1.29 | +0.06 | draws 1460 -> 1516 (dyn 120 -> 176, fx 69 -> 122), binds 872 -> 975 | BISECT |
| 502 | 2.08 | 3.48 | 3.42 | +1.34 | +0.06 | draws 877 -> 932 (dyn 133 -> 188, fx 77 -> 126), binds 650 -> 766 | BISECT |
| 503 | 2.11 | 3.20 | 3.14 | +1.03 | +0.06 | draws 1320 -> 1370 (dyn 155 -> 205, fx 84 -> 131), binds 804 -> 907 | BISECT |
| 504 | 2.59 | 3.93 | 3.88 | +1.29 | +0.05 | draws 1441 -> 1507 (dyn 181 -> 247, fx 119 -> 182), binds 1099 -> 1233 | BISECT |
| 507 | 2.36 | 4.70 | 4.65 | +2.29 | +0.05 | draws 994 -> 1110 (dyn 144 -> 260, fx 102 -> 193), binds 733 -> 972 | BISECT |
| 508 | 2.09 | 3.33 | 3.29 | +1.20 | +0.04 | draws 1305 -> 1358 (dyn 143 -> 195, fx 91 -> 136), binds 887 -> 1003 | BISECT |
| 509 | 2.78 | 4.08 | 4.04 | +1.26 | +0.04 | draws 1936 -> 2014 (dyn 191 -> 269, fx 121 -> 189), binds 1003 -> 1155 | BISECT |
| 510 | 2.68 | 5.44 | 5.32 | +2.64 | +0.12 | draws 1198 -> 1341 (dyn 213 -> 357, fx 127 -> 245), binds 1285 -> 1592 | BISECT |

Attribution: death bodies: a faithful presentation the original had at 10 players, about 0.2 ms per live body at 63 bots; GPU-skinned corpses = package 7 item 1

## 1b. Realistic perf (package 6 defaults + HD Enhanced, 2560x1440, 31 v 32, 1 rep, report-only)

| map | p50 ms | p90 ms | note |
|---|---|---|---|
| 508 | 9.26 | 10.33 | ok |
| 510 | 9.48 | 11.63 | ok |
| 507 | 7.32 | 8.56 | ok |
| 504 | 8.18 | 10.35 | ok |
| 509 | 9.30 | 10.35 | ok |

## 2. R4 leak rows (lobby GPU dedicated MB after each map change)

2180 / 2841 / 2980 / 2974  -> PASS (plateau after change 1: spread 138 MB)

## 3. Eye pass

- 508: 26 steps x 3 modes; picks ['00900', '00780', '01320']; sim split vs off: real none, realhd none
- 510: 26 steps x 3 modes; picks ['01080', '01200', '00780', '01320']; sim split vs off: real none, realhd none
- 507: 26 steps x 3 modes; picks ['00960', '01080', '00780', '01320']; sim split vs off: real none, realhd none
- 504: 26 steps x 3 modes; picks ['00840', '00900', '00780', '01320']; sim split vs off: real none, realhd none
- 509: 26 steps x 3 modes; picks ['01140', '01800', '00780', '01320']; sim split vs off: real none, realhd none

## Difference from Off (scene pixels, HUD masked; mean over same-moment pairs)

Bigger = more visibly different from the original. Contrast 1.00 = the same contrast as Off.

| map | mode | pairs | mean luminance change (0-255) | brighter (+) / darker (-) | contrast ratio | colourfulness change | pixels changed % | misaligned |
|---|---|---|---|---|---|---|---|---|
| Streets (508) | Realistic | 26 | 21.3 | +8.8 | 1.16 | +1.6 | 34.3 |  |
| Streets (508) | Realistic + Enhanced materials | 26 | 22.1 | +9.0 | 1.18 | +1.7 | 35.6 |  |
| Gorge (510) | Realistic | 26 | 18.5 | +12.7 | 1.34 | +4.3 | 26.2 |  |
| Gorge (510) | Realistic + Enhanced materials | 25 | 19.1 | +13.4 | 1.38 | +4.1 | 27.5 | 1 |
| Debris (507) | Realistic | 26 | 25.8 | +5.0 | 1.15 | +2.5 | 38.7 |  |
| Debris (507) | Realistic + Enhanced materials | 24 | 25.8 | +3.3 | 1.15 | +2.7 | 39.3 | 2 |
| Rust (504) | Realistic | 26 | 21.0 | +12.7 | 1.27 | +4.3 | 29.8 |  |
| Rust (504) | Realistic + Enhanced materials | 25 | 21.7 | +13.1 | 1.28 | +4.6 | 31.2 | 1 |
| Molten (509) | Realistic | 25 | 23.3 | +19.0 | 1.46 | +5.3 | 34.7 | 1 |
| Molten (509) | Realistic + Enhanced materials | 24 | 23.9 | +19.5 | 1.51 | +5.4 | 36.3 | 2 |
| **all maps** | Realistic | 129 | 22.0 | +11.6 | 1.28 | +3.6 | 32.7 | |
| **all maps** | Realistic + Enhanced materials | 124 | 22.5 | +11.7 | 1.30 | +3.7 | 34.0 | |

- contrast floor Realistic: all-map 1.28 vs floor 1.28 -> OK
- contrast floor Realistic + Enhanced materials: all-map 1.30 vs floor 1.30 -> OK
## Authored views (fixed cameras from tools/fidelity/views; difference from Off (original))

| map | view | mode | mean luminance change (0-255) | brighter (+) / darker (-) | contrast ratio | colourfulness change | pixels changed % |
|---|---|---|---|---|---|---|---|
| Streets (508) | firefight | Realistic | 13.7 | +3.9 | 1.04 | +4.9 | 30.2 |
| Streets (508) | firefight | Realistic + Enhanced materials | 17.1 | +2.0 | 1.01 | +3.9 | 35.7 |
| Streets (508) | street | Realistic | 23.6 | -18.2 | 1.04 | -1.2 | 56.8 |
| Streets (508) | street | Realistic + Enhanced materials | 24.1 | -18.1 | 0.99 | -0.8 | 55.5 |
| Streets (508) | dark | Realistic | 14.3 | +12.3 | 1.22 | +1.3 | 19.9 |
| Streets (508) | dark | Realistic + Enhanced materials | 7.2 | +2.2 | 1.16 | +3.5 | 6.8 |
| Streets (508) | reflective | Realistic | 31.4 | +30.5 | 1.38 | +6.8 | 51.8 |
| Streets (508) | reflective | Realistic + Enhanced materials | 32.4 | +31.2 | 1.42 | +8.2 | 56.8 |
| Streets (508) | lane | Realistic | 19.0 | +12.9 | 1.04 | +2.5 | 29.7 |
| Streets (508) | lane | Realistic + Enhanced materials | 20.3 | +14.3 | 1.11 | +2.8 | 31.8 |
| Gorge (510) | firefight | Realistic | 13.9 | +12.5 | 1.83 | +7.1 | 22.7 |
| Gorge (510) | firefight | Realistic + Enhanced materials | 16.2 | +15.0 | 1.95 | +7.0 | 27.5 |
| Gorge (510) | street | Realistic | 13.1 | +3.8 | 1.25 | +1.9 | 21.1 |
| Gorge (510) | street | Realistic + Enhanced materials | 14.6 | +6.3 | 1.29 | +2.4 | 24.3 |
| Gorge (510) | dark | Realistic | 5.2 | +1.0 | 1.80 | +5.1 | 7.3 |
| Gorge (510) | dark | Realistic + Enhanced materials | 5.9 | +1.9 | 2.01 | +5.4 | 9.4 |
| Gorge (510) | reflective | Realistic | 10.3 | +6.2 | 1.64 | +5.0 | 15.3 |
| Gorge (510) | reflective | Realistic + Enhanced materials | 10.7 | +6.7 | 1.66 | +4.8 | 16.1 |
| Gorge (510) | lane | Realistic | 20.0 | +13.3 | 1.24 | +6.6 | 33.7 |
| Gorge (510) | lane | Realistic + Enhanced materials | 24.8 | +17.2 | 1.26 | +6.5 | 41.9 |
| Debris (507) | firefight | Realistic | 17.8 | -7.9 | 0.96 | +1.5 | 30.0 |
| Debris (507) | firefight | Realistic + Enhanced materials | 20.7 | -3.8 | 1.04 | +1.5 | 33.0 |
| Debris (507) | street | Realistic | 39.2 | +37.1 | 1.55 | +12.8 | 64.4 |
| Debris (507) | street | Realistic + Enhanced materials | 40.2 | +37.8 | 1.65 | +9.7 | 61.2 |
| Debris (507) | dark | Realistic | 20.9 | +16.9 | 1.21 | +5.9 | 36.3 |
| Debris (507) | dark | Realistic + Enhanced materials | 18.9 | +13.4 | 1.20 | +2.9 | 28.8 |
| Debris (507) | reflective | Realistic | 41.2 | +38.5 | 1.61 | +8.9 | 66.7 |
| Debris (507) | reflective | Realistic + Enhanced materials | 35.6 | +32.6 | 1.57 | +9.4 | 62.5 |
| Debris (507) | lane | Realistic | 10.8 | +8.5 | 1.50 | +4.8 | 13.9 |
| Debris (507) | lane | Realistic + Enhanced materials | 10.7 | +7.7 | 1.51 | +5.2 | 15.1 |
| Rust (504) | firefight | Realistic | 12.2 | +7.5 | 1.36 | +11.9 | 22.5 |
| Rust (504) | firefight | Realistic + Enhanced materials | 13.1 | +8.3 | 1.39 | +13.1 | 23.3 |
| Rust (504) | street | Realistic | 17.9 | +2.4 | 1.40 | +6.4 | 30.4 |
| Rust (504) | street | Realistic + Enhanced materials | 19.5 | +3.1 | 1.44 | +5.8 | 34.1 |
| Rust (504) | dark | Realistic | 6.9 | +5.1 | 1.74 | +4.3 | 12.5 |
| Rust (504) | dark | Realistic + Enhanced materials | 7.3 | +4.8 | 1.73 | +6.0 | 13.3 |
| Rust (504) | reflective | Realistic | 13.8 | +13.7 | 2.84 | +19.2 | 27.7 |
| Rust (504) | reflective | Realistic + Enhanced materials | 13.8 | +13.7 | 2.82 | +16.3 | 27.8 |
| Rust (504) | lane | Realistic | 12.5 | +11.1 | 1.71 | +10.0 | 19.7 |
| Rust (504) | lane | Realistic + Enhanced materials | 16.3 | +13.8 | 2.00 | +12.6 | 30.1 |
| Molten (509) | firefight | Realistic | 14.1 | +2.6 | 1.15 | +5.5 | 20.7 |
| Molten (509) | firefight | Realistic + Enhanced materials | 15.5 | +3.0 | 1.15 | +5.6 | 23.4 |
| Molten (509) | street | Realistic | 21.7 | +18.5 | 1.42 | +11.6 | 52.6 |
| Molten (509) | street | Realistic + Enhanced materials | 23.4 | +19.9 | 1.46 | +9.5 | 57.2 |
| Molten (509) | dark | Realistic | 10.1 | +7.8 | 1.37 | +5.4 | 15.5 |
| Molten (509) | dark | Realistic + Enhanced materials | 10.6 | +8.5 | 1.37 | +5.2 | 16.8 |
| Molten (509) | reflective | Realistic | 38.4 | +36.0 | 2.09 | +17.0 | 45.3 |
| Molten (509) | reflective | Realistic + Enhanced materials | 37.0 | +33.9 | 1.95 | +16.0 | 48.1 |
| Molten (509) | lane | Realistic | 17.5 | +11.4 | 1.38 | +7.4 | 27.2 |
| Molten (509) | lane | Realistic + Enhanced materials | 21.5 | +13.9 | 1.45 | +7.7 | 31.8 |

Sheets: work/eyepass/9350f19/sheet_<map>.png, pick_<map>.png

## 4. FAST 7cd2202 (full)

PASS 59 / KNOWN 1 / INFO 9 / UNKNOWN 1 / PARTIAL 1 / SKIP 1

## 4. FAST 9350f19 (delta)

PASS 59 / KNOWN 1 / INFO 9 / UNKNOWN 1 / PARTIAL 1 / SKIP 1


## 5. Per-body cost and 5v5 (Berth, RT off, 3840x2160, same match, 2 reps, p90 ms)

| arm | 63 bots r1 / r2 | 5v5 r1 / r2 |
|---|---|---|
| base (398b27ef + sim fixes) | 2.19 / 2.16 | 1.57 / 1.57 |
| 9350f19b | 3.47 / 3.47 | 1.63 / 1.63 |
| 9350f19b + WFC_NOCORPSE=1 | 2.17 / 2.18 | - |

- At 63 bots the death bodies are the whole RT-off delta: with them off the head equals base (2.17 vs 2.16). With the static-pose
  fix the bodies cost about +1.3 ms p90 on Berth (4078e47b: +1.9).
- At the original lobby default (5v5) the head costs +0.06 ms p90 over base: the bodies' cost scales with the bot count.

## Notes

- Regress maps 502 and 503 were rerun inside the same hold: Rendering's X3 compile ran 18:33-18:39 during their first runs
  (kept as map-502_contaminated / map-503_contaminated). Map 510's base-r2 overlapped a few seconds of Systems' verify_slim
  static checks at about 19:10; its reps agree (2.68 / 2.69), no rerun.
- The mapswitch rows of both FASTs (gate block) were rerun after a reference-exe defect of the harness (3616470c); PASS 6/6.
