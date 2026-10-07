# PERFORMANCE LOG (Experimental; user scalability brief, Integration 2026-10-07)

Uncapped, fixed cam (WFC_FIXEDCAM per map), frontend-launched private TDM with bots, second-match (warm) figures. Steady stats exclude hitch events (> 50 ms), which are counted separately; the first 180 in-play frames are warm-up. 1 % / 0.1 % low = fps of the slowest 1 % / 0.1 % of steady frames. Splits come from a separate profiling run (glFinish-serialised: ratios, not absolute).

## 2026-10-07 13:49 - 77b6fb0 - Streets scaling curve (async default-on)

| map | res | async | participants | avg fps | p50 | p90 | p95 | p99 | worst steady | 1% low | 0.1% low | hitches | <=3.33 ms | submit | GPU wait | sim step | chars | FX | MB at load |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 508 | 1920x1080 | - | 10 | 384.3 | 2.5 | 3.2 | 3.49 | 4.39 | 26.59 | 182.4 | 102.3 | 0 | 92.8 % | 1.72 | 0.98 | 0.33 | 0.19 | 0.76 | 5106 |
| 508 | 1920x1080 | - | 20 | 328.8 | 2.93 | 3.81 | 4.11 | 4.89 | 37.86 | 161.9 | 86.4 | 0 | 73.8 % | 2.22 | 0.79 | 0.48 | 0.41 | 0.9 | 4874 |
| 508 | 1920x1080 | - | 32 | 252.9 | 3.89 | 5.29 | 5.65 | 6.5 | 27.37 | 133.4 | 85.4 | 0 | 31 % | 2.72 | 0.7 | 0.62 | 0.66 | 1.01 | 4814 |
| 508 | 1920x1080 | - | 48 | 190.8 | 5.17 | 6.97 | 7.46 | 8.66 | 26.72 | 103.5 | 71.1 | 0 | 7.3 % | 3.92 | 0.84 | 0.94 | 1.02 | 1.47 | 4799 |
| 508 | 1920x1080 | - | 64 | 157.2 | 6.45 | 8.06 | 8.61 | 9.86 | 26.82 | 92.2 | 65.9 | 0 | 1.8 % | 5.06 | 1.14 | 1.14 | 1.53 | 1.75 | 4804 |
