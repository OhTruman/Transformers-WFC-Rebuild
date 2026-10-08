# 300 fps verdict - 09c f7bae0d vs 5990311 (CLEAN re-run, 2026-10-08 00:02-01:08)

Replaces the retracted 23eb534 comparison. One Experimental PERF window, every run strictly sequential (run intervals checked:
no overlaps), no other lane running. Streets, 1080p, uncapped, async on, WFC_FLOWSEED pinned, frontend-launched TDM, second
match, InProgress only (MATCH start .. MATCH end), first 180 frames skipped, all rows spawned N/N.
f7bae0d = 23eb534 (sprite stream, 9f51ce9, light-env fix, mimalloc) + held-weapon light env + pawn occlusion / prep skip + AVM1.

## Verified overview cam (44.3,-606.9,-475.7,-90,-31.3; `view_overview_t64.png`, view check lit, luma 53)

| participants | p50 ms | p90 ms | p99 ms | 1 % low fps | 0.1 % low fps | <= 3.33 ms | CPU submit | GPU wait | sim |
|---|---|---|---|---|---|---|---|---|---|
| 10 | 1.64 | 1.97 | 2.49 | 297 | 137 | 99.7 % | 0.96 | 1.34 | 0.33 |
| **20 (10 v 10)** | 2.49 -> **2.03** | 3.31 -> **2.63** | 3.20 | 191 -> 250 | 113 | 90.6 -> **99.4 %** | 1.91 -> 1.15 | 0.96 -> 1.23 | 0.46 |
| 32 | 2.33 | 3.51 | 4.44 | 192 | 106 | 86.9 % | 1.66 | 0.98 | 0.66 |
| 48 | 2.79 | 4.39 | 5.70 | 147 | 80 | 69.7 % | 2.11 | 0.87 | 0.89 |
| **64 (32 v 32)** | 6.29 -> **3.96** | 8.32 -> **5.18** | 6.14 | 91 -> 145 | 96 | 5.7 -> 27.8 % | 4.06 -> 2.64 | 1.12 -> 0.89 | 1.09 |

(arrows: 5990311 -> f7bae0d, same window). Fit: p50 = 1.15 ms fixed + 0.0403 ms per participant (this morning: 0.075).

## Real play (WFC_PLAYERBOT=1, follow cam) - "walking real play"
| participants | p50 | p90 | p99 | 1 % low | <= 3.33 ms |
|---|---|---|---|---|---|
| 20 | 1.79 | **2.47** | 3.20 | 242 | 99.3 % |
| 64 | 3.30 | **4.89** | 6.22 | 142 | 51.3 % |
The pilot moved / pathed / switched goals but fired once (20) and never acquired a target (64) - Gameplay fixed the fire
flicker after this window; combat load of the player's own weapon is under-represented here.

## Legacy cam (continuity only): `view_legacy_t64.png` - the view check FAILS (near-black wall), as expected
p50 / p90 at 10..64: 1.80/2.18, 2.11/2.56, 2.26/2.90, 2.66/3.72, 3.34/4.45 - lighter than the overview (64: p90 4.45 vs 5.18).

## Verdict (user target: p90 <= 3.33 ms = 300 fps)
- **10 v 10: MET** - overview p90 2.63 ms (99.4 % of frames), real play p90 2.47 ms.
- **32 v 32: NOT MET** - overview p90 5.18 ms (1.85 ms to go), real play p90 4.89 ms (1.56 ms to go). At 64 the frame is
  CPU submit 2.64 + GPU wait 0.89 + sim 1.09 (split run).

Watch: f7bae0d bots - t10 match 1: 1 "broken" (also on 23eb534); t64 match 2: Lifter p61 / Nacelle p63 pinned ~25 s in vehicle
form ~16 UU below ground at x 280-295, z -340..-354 (Gameplay). One hitch at t32 (single).
