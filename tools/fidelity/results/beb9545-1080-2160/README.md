# 09c beb9545 (world depth prepass, WFC_RENDERSIZE) vs playtest 798bb26 - 1080p and 2160p (2026-10-08 16:33-17:35)

Streets, verified overview cam, uncapped, async on, PGO, frontend-launched TDM, second match, InProgress only, no captures in
timed rows. Every row's rendered size verified from the log ("warm-up draw ... at WxH"); 2160p = WFC_RENDERSIZE=3840x2160
(all 3D passes at 3840x2160, scaled into a 1920x1080 window).

## Overview cam (p90 target <= 3.33 ms)
| participants | 798bb26 1080p | beb9545 1080p | beb9545 2160p |
|---|---|---|---|
| 20 (10 v 10) | p50 1.88 / p90 2.19 (99.7 %) | 1.89 / **2.13** (99.9 %), stalls >16.7 ms: 0 | 2.16 / **2.43** (99.9 %), 0 |
| 64 (32 v 32) | p50 2.77 / p90 3.37 (88.7 %) | 2.43 / **3.12** (95.3 %), 0 | 2.97 / 3.53 (79.5 %), 0 |

## Real play (PLAYERBOT, follow cam), beb9545
| participants | 1080p p50 / p90 | 2160p p50 / p90 |
|---|---|---|
| 20 | 1.31 / **1.82** (99.9 %) | 1.90 / **2.22** (99.8 %) |
| 64 | 1.68 / **2.66** (98.2 %) | 2.16 / **2.84** (98.5 %) |
No crashes. The pilot never acquires a target at 64 (0 shots) - Gameplay; 1 kill at 20.

**Verdict:** 1080p - 10 v 10 and 32 v 32 MET on the overview (2.13 / 3.12 ms) and in real play. 2160p - 10 v 10 MET (2.43);
32 v 32 MET in real play (2.84), 0.2 ms over on the worst-case overview (3.53 ms, 79.5 % of frames). No frames > 33 ms anywhere.

## Depth prepass visual guard + positive control
- Same build, prepass on vs WFC_NOZPREPASS=1, native size, overview cam, match-step-aligned frames 300-900 (21): **bit-identical**
  (mean abs diff 0 on every frame).
- Positive control (the switch really toggles): GPU world time at 3840x2160 (WFC_SLOWFRAME=0.001, 4,000 frames each):
  **1.10 ms prepass on vs 2.50 ms off**. So the prepass saves 1.4 ms of GPU world time at 4K with no image change.
