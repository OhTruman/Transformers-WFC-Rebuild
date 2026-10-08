# 23eb534 vs 5990311 - verified overview cam, same PERF window (2026-10-07 21:47-22:57)

Streets, verified overview cam `44.3,-606.9,-475.7,-90,-31.3` (`measured_view_t64.png`, captured by the harness at match
step 600 of the measured match: lit, luma 53), 1080p, uncapped, async on, WFC_FLOWSEED pinned, frontend-launched TDM,
second match, InProgress only (MATCH start .. MATCH end), first 180 frames skipped. All rows spawned N/N, 0 hitches.
23eb534 = sprite stream batch + 9f51ce9 allocation removal + light-env LastRenderTime fix + mimalloc (on) + PLAYERBOT / SIMHASH.

| participants | p50 ms | p90 ms | 1 % low fps | 0.1 % low fps | <= 3.33 ms | CPU submit | GPU wait |
|---|---|---|---|---|---|---|---|
| 10 | 1.80 -> 1.63 | 2.41 -> 2.02 | 217 -> 278 | 112 -> 127 | 98.9 -> 99.7 % | 1.29 -> 1.02 | 1.22 -> 1.38 |
| 20 | 2.63 -> 2.11 | 3.70 -> 2.81 | 168 -> 241 | 96 -> 124 | 81.8 -> 98.3 % | 1.92 -> 1.28 | 0.89 -> 1.26 |
| 32 | 3.30 -> 2.60 | 4.84 -> 3.93 | 138 -> 167 | 91 -> 109 | 51.6 -> 78.2 % | 2.45 -> 1.96 | 0.88 -> 1.03 |
| 48 | 4.48 -> 3.32 | 6.62 -> 5.07 | 114 -> 134 | 84 -> 91 | 18.2 -> 50.6 % | 3.97 -> 2.67 | 1.07 -> 0.99 |
| 64 | 6.60 -> 4.56 | 8.14 -> 5.91 | 97 -> 128 | 70 -> 83 | 5.6 -> 15.8 % | 4.83 -> 3.89 | 1.29 -> 1.37 |

**300 fps target (p90 <= 3.33 ms): 10 v 10 MET (p90 2.81 ms, 98 % of frames). 32 v 32 not met: p90 5.91 ms (-2.6 ms to go).**

Fit p50 = 1.02 ms fixed + 0.0525 ms per participant (5990311 / 4f44465: ~0.08) - the per-participant cost fell by a third.
At 64 the frame is still ~65 % CPU submit (3.89 ms) with GPU wait 1.37 and sim 1.07.

Watch: 23eb534 t10 match 1 - 1 bot "broken" (no displacement >= 20 s; single; match 2 clean).
Real-play (PLAYERBOT) and legacy-cam rows: part B.
