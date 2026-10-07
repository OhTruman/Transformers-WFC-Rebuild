# 4f44465 (drawSubs fix + lightmap array + ability actors async) vs 0c8e05d baseline - same PERF window

Streets, 1080p, fixed cam, uncapped, async default-on, frontend-launched TDM, second (warm) match. Both curves in one window
(16:29-17:25, 2026-10-07); the baseline reproduces the morning's 77b6fb0 curve within +-0.16 ms, so the deltas are real.

| participants | p50 0c8e05d -> 4f44465 | p90 | 1 % low fps | <= 3.33 ms | CPU submit | GPU wait |
|---|---|---|---|---|---|---|
| 10 | 2.49 -> 1.96 (-0.53) | 3.19 -> 2.56 | 198 -> 232 | 93 -> 99 % | 1.76 -> 1.27 | 0.99 -> 1.05 |
| 20 | 2.96 -> 2.54 (-0.42) | 3.95 -> 3.48 | 154 -> 166 | 71 -> 87 % | 2.30 -> 2.04 | 0.82 -> 0.79 |
| 32 | 3.95 -> 3.58 (-0.37) | 5.33 -> 5.23 | 130 -> 139 | 26 -> 42 % | 3.00 -> 2.51 | 0.73 -> 0.82 |
| 48 | 5.21 -> 4.67 (-0.54) | 6.71 -> 6.32 | 110 -> 113 | 7 -> 13 % | 4.04 -> 4.04 | 0.85 -> 1.10 |
| 64 | 6.61 -> 6.27 (-0.34) | 8.05 -> 7.90 | 91 -> 94 | 2.5 -> 2.8 % | 5.09 -> 5.01 | 1.17 -> 1.33 |

Fit: p50 = **1.04 ms fixed** (was 1.57) + 0.080 ms per participant (unchanged): the merge removed fixed world cost; the
per-participant cost (mostly CPU submit) is the remaining 32 v 32 lever. No hitches at any size.

**300 fps target (p90 <= 3.33 ms):** 5 v 5 MET (p90 2.56); 10 v 10 0.15 ms short (p90 3.48, 87 % of frames under 3.33);
32 v 32 4.6 ms short.

**Open (unconfirmed):** the 64-participant PROFILING run (WFC_TICKPROF + RENDERSTATS glFinish) on 4f44465 shows sim windows of
2.9 / 7.7 / 3.0 / 5.3 ms/step late in the match (0c8e05d steady 1.2-1.5), with the cost hopping between unrelated buckets
(bots, ab.barrier, partWeapons, match, ab.roller) - consistent with the step thread being preempted, not one system
regressing. The TIMING run is fine (sim p99 1.04 vs 1.29 ms). The sim_step column for 4f44465 at 64 (3.13) comes from that
profiling run. Asked Gameplay; a 64-participant split rerun will check reproducibility. Bots at 64: no-path max 18 vs 5,
off-mesh samples 55 vs 18 (single run, watch).
