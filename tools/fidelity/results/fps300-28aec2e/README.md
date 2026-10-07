# 300 fps table: Streets, FIXED camera (worst-case overview), 09c 28aec2e

The camera is Rendering's WFC_FIXEDCAM "100,-700,-680,-141.6,-12" (above team 0's spawn, looking into team 1's). Uncapped.
Frontend-launched private matches, TimeLimit 60 s, two matches per population; clean Experimental PERF window.

| population | res | p50 ms | p90 ms | p99 ms | under 3.33 ms | CPU submit / GPU wait (RENDERSTATS run) |
|---|---|---|---|---|---|---|
| recommended 10 v 10 | 1080p | 5.46 | 7.45 | 9.30 | 0 % | 4.62 / 0.86 |
| recommended 10 v 10 | 1440p | 5.45 | 7.38 | 8.94 | 0 % | 4.58 / 0.87 |
| FFA recommended 15 | 1080p | 5.43 | 6.89 | 8.56 | 0 % | 4.36 / 0.87 |
| FFA recommended 15 | 1440p | 5.57 | 7.12 | 8.94 | 0 % | 4.45 / 0.86 |
| 32 v 32 | 1080p | 13.22 | 15.55 | 17.78 | 0 % | 9.22 / 1.28 |
| 32 v 32 | 1440p | 12.86 | 15.29 | 17.53 | 0 % | 8.93 / 1.32 |

(Second match each. The first matches are within +-0.3 ms.)

**Verdict: 300 fps is NOT met at any size; the median is ~180 fps at the recommended sizes and ~77 fps at 32 v 32.** The
game is CPU-bound on render submission at every size: submit alone is ~4.5 ms at 20 participants, already above the
3.33 ms budget. Resolution has no effect. Versus 8a380ea (scripted camera, ~21 ms at 32 v 32), 32 v 32 improved to ~13 ms
p50; the camera differs, so the ratio is indicative only.

Bots at 32 v 32: 1-2 broken per match (no displacement >= 20 s with a moving goal and no target). Two were off the nav mesh
(cell -1), one with stuck 0 throughout; one was in vehicle form on the mesh with an Attack goal. Sent to Gameplay.
