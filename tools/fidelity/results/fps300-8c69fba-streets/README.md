# fps table: Streets on 09c 8c69fba (pre-MDI baseline), fixed cam, uncapped

Second-match figures; the phase split comes from the separate profiling run (glFinish-serialised; ratios, not absolute times).

| population | res | p50 ms | p90 ms | p99 ms | <=3.33 ms | submit | GPU wait | chars | FX | actors | sim step |
|---|---|---|---|---|---|---|---|---|---|---|---|
| original 5v5 | 1080p | 3.79 | 4.47 | 6.16 | 6.5 % | 2.44 | 1.54 | 0.21 | 0.77 | 0.23 | 0.35 |
| original 5v5 | 1440p | 3.58 | 3.98 | 5.17 | 19.3 % | 2.60 | 1.50 | 0.25 | 0.82 | 0.25 | 0.38 |
| rec 10v10 | 1080p | 3.85 | 5.01 | 6.28 | 7.1 % | 2.98 | 1.28 | 0.45 | 0.94 | 0.49 | 0.49 |
| rec 10v10 | 1440p | 3.87 | 4.94 | 6.19 | 7.0 % | 2.99 | 1.34 | 0.46 | 0.95 | 0.49 | 0.49 |
| 32v32 | 1080p | 7.29 | 10.39 | 12.42 | 0 % | 5.59 | 1.15 | 1.64 | 1.70 | 1.64 | 1.18 |
| 32v32 | 1440p | 7.09 | 10.12 | 12.07 | 0 % | 5.59 | 1.12 | 1.65 | 1.71 | 1.61 | 1.25 |

**Verdict: 300 fps is not met yet; the median is ~260-280 fps at 5v5 / 10v10 and ~137 fps at 32v32.** Big gains vs 28aec2e:
recommended 5.46 -> 3.85 ms, 32v32 13.2 -> 7.3 ms. The biggest remaining item everywhere is world / scene submission on the CPU.
At 5v5, GPU wait (~1.5 ms) is second. Sim top phases at 32v32: oppMove 0.31, bots 0.30, abilities 0.13 ms per step.
The HUD (ui.draw) is not instrumented in match frames (FRAMEPROF is frontend only), so it shows "-".
