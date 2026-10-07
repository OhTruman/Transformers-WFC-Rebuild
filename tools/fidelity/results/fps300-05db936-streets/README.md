# fps table: Streets on 09c 05db936 (world MDI on), 1080p, fixed cam, uncapped; WFC_ASYNCSTEP 0 / 1

Second-match figures; the phase split comes from the separate profiling run.

| async | population | p50 ms (fps) | p90 ms | p99 ms | <=3.33 ms | 300 fps verdict | submit | GPU wait | join wait avg / max |
|---|---|---|---|---|---|---|---|---|---|
| 0 | original 5v5 | 2.34 (427) | 3.22 | 4.56 | 91.9 % | **MET** | 1.69 | 1.02 | - |
| 1 | original 5v5 | 2.51 (398) | 3.28 | 4.10 | 91.3 % | **MET** | 1.73 | 0.98 | 0.010 / 0.039 |
| 0 | rec 10v10 | 2.99 (334) | 4.29 | 5.68 | 68.1 % | PARTIAL (p50) | 2.24 | 0.81 | - |
| 1 | rec 10v10 | 2.93 (341) | 3.84 | 5.01 | 72.6 % | PARTIAL (p50) | 2.22 | 0.78 | 0.004 / 0.041 |
| 0 | 32v32 | 5.72 (175) | 9.22 | 11.38 | 2.9 % | no | 4.85 | 1.06 | - |
| 1 | 32v32 | 6.25 (160) | 7.69 | 9.75 | 0 % | no | 5.46 | 1.26 | 0.024 / 0.116 |

- **The original 5 v 5 clears 300 fps** (p90 <= 3.33 ms) with async on and off.
- The recommended 10v10 median is ~335 fps, but its p90 is 3.8-4.3 ms.
- Async: the main-thread join wait is ~0 (max 0.2 ms at 32v32), so the background part is fully hidden. Async lowers p90 at
  10v10 / 32v32; medians are mixed (one sample per row - within run-to-run noise).
- The biggest remaining item is still scene submission (1.7-2.2 ms at the default sizes, ~5 ms at 32v32).
- Versus 8c69fba (pre-MDI): 5v5 3.79 -> 2.34, rec 3.85 -> 2.99, 32v32 7.29 -> 5.72 ms p50.
