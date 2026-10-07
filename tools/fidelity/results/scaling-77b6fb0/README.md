# Scalability baseline: Streets scaling curve + 10 v 10 slow-frame breakdown, 09c 77b6fb0

Uncapped, 1080p, fixed cam (Rendering's overview), async default-on; frontend-launched private TDM with bots. Second-match
(warm) figures; no hitches (> 50 ms) at any size.

| participants | avg fps | p50 | p90 | p99 | 1% low | 0.1% low | <=3.33 ms | submit | GPU wait | sim step | chars | FX |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 10 | 384 | 2.50 | 3.20 | 4.39 | 182 | 102 | 92.8 % | 1.72 | 0.98 | 0.33 | 0.19 | 0.76 |
| 20 | 329 | 2.93 | 3.81 | 4.89 | 162 | 86 | 73.8 % | 2.22 | 0.79 | 0.48 | 0.41 | 0.90 |
| 32 | 253 | 3.89 | 5.29 | 6.50 | 133 | 85 | 31.0 % | 2.72 | 0.70 | 0.62 | 0.66 | 1.01 |
| 48 | 191 | 5.17 | 6.97 | 8.66 | 104 | 71 | 7.3 % | 3.92 | 0.84 | 0.94 | 1.02 | 1.47 |
| 64 | 157 | 6.45 | 8.06 | 9.86 | 92 | 66 | 1.8 % | 5.06 | 1.14 | 1.14 | 1.53 | 1.75 |

**Curve: LINEAR with a large fixed overhead** - p50 = 1.57 ms fixed + 0.075 ms per participant (residuals < 0.2 ms; not
superlinear on the live frame; Gameplay's headless SCALETEST shows the sim step's own jump at 64 - sentry targeting /
separation - which is a small share here).

**Targets (user): p90 <= 3.33 ms at 10 v 10 (20) and 32 v 32 (64).**
- 20 participants: p90 3.81 -> -0.5 ms needed.
- 64 participants: p90 8.06 -> -4.7 ms needed (~2.4x).
At 0.075 ms per participant, 64 adds 4.8 ms over 0 - the per-participant cost must fall ~3x, or the fixed 1.57 ms and the
per-participant cost must both fall.

**10 v 10 slow-frame breakdown** (Rendering's WFC_SLOWFRAME=3.33 on their 8229bb9 + logger build, same harness; the logger
does not change timing: p50 2.92-3.01 vs 2.93 clean):

| cause of the frame > 3.33 ms | match 1 | match 2 |
|---|---|---|
| render CPU: world | 73 % | 76 % |
| render CPU: characters | 20 % | 16 % |
| GPU-bound | 6 % | 6 % |
| translucency / outside render / upload spike / shader compile | ~1 % | ~2 % |

Average slow frame 3.88 ms = render 3.1 (world 1.1, chars 0.85, transl 0.8, FX 0.28) + outside 0.8; GPU 2.14 ms; ~2,500 draws.
=> 10 v 10's p90 is a steady-state CPU-submission problem (world + characters), not first-use / upload events.
