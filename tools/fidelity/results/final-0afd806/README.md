# FINAL pre-playtest verdict - 09c 0afd806 vs 5990311 (2026-10-08 10:31-11:18, one Experimental window)

Streets, verified overview cam, 1080p, uncapped, async on, PGO (`WFC_PGO` = tools/pgo/wfc.profdata), FLOWSEED pinned,
frontend-launched TDM, second match, InProgress only. Runs strictly sequential (intervals checked: 0 overlaps). Timed rows
carry NO screenshots / VISUALCHECK / profiling env (the view check uses the untimed split run: lit, luma 53).

| participants | p50 ms | p90 ms | p99 ms | 1 % / 0.1 % low fps | <= 3.33 ms | >16.7 / >33 / >50 ms | submit / GPU wait / sim |
|---|---|---|---|---|---|---|---|
| 10 | 1.68 | 1.82 | 2.05 | 366 / 168 | 99.9 % | 3 / 0 / 0 | 0.84 / 1.44 / 0.20 |
| **20 (10 v 10)** | 1.88 | 3.36 -> **2.25** | 2.50 | 293 / 117 | 89.4 -> **99.8 %** | 4 / 1 / 1 | 1.09 / 1.26 / 0.28 |
| 32 | 2.30 | 2.89 | 3.54 | 236 / 132 | 97.8 % | 2 / 0 / 0 | 1.38 / 1.11 / 0.34 |
| 48 | 2.36 | 2.92 | 3.62 | 240 / 140 | 97.2 % | 2 / 0 / 0 | 1.67 / 0.96 / 0.51 |
| **64 (32 v 32)** | 6.34 -> **2.67** | 7.82 -> **3.34** | 4.31 | 100 -> 183 / 85 | 5.8 -> **89.7 %** | 3 / 0 / 0 | 1.96 / 0.82 / 0.60 |

Fit: p50 = 1.56 ms fixed + 0.018 ms per participant (2026-10-07 morning: 1.57 + 0.075).

Real play (PLAYERBOT, follow cam): 20 -> p90 1.95 ms (99.8 %), 64 -> p90 2.93 ms (97.3 %, match 1 only - see crash).
The pilot still barely fights (2 fire samples, 0 kills at 20; no target at 64) - Gameplay.

**Verdict (p90 <= 3.33 ms): 10 v 10 MET (2.25 ms). 32 v 32 at the line: 3.34 ms on the overview (0.01 over; 89.7 % of frames),
2.93 ms in walking real play.** No stalls > 33 ms at 32 v 32.

Visual guard (WFC_SHOTMATCH match-step aligned, steps 300-900, overview cam, 8 v 8): 5990311 vs 0afd806 0 flagged frames
(max 0 % of pixels > 24 levels); 0afd806 A/A 0 %. The moving-cam set is VOID (scripted walk faces dark walls: view check).

**PLAYTEST BLOCKER found:** `crash_64p_realplay_end_of_match.txt` - 0xc0000005 in gfx::avm1::ScopeVars::set <-
nativeUpdateInterpObjects on the 64-player results screen (EndGameStats + PlayerList.swf define the tween library twice;
Frontend found and fixed the root cause, pending push). Intermittent (the overview 64p run completed two matches).
