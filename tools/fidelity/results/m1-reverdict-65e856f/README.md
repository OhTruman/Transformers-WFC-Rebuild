# M1 re-verdict 65e856f (TIMED window 2026-10-11 19:33-20:37)

Head 65e856f = 9c3560e + Rendering (b)+(d)+(d2) + Systems HUD text-shadow scissor + shadow cache (byte-identical; off switches
WFC_NOHUDSHADOWCACHE=1, WFC_HUDSHADOWFULLCLEAR=1). WFC_BOTAI=Classic pinned. 4K = RenderSize 3840x2160 in a 1920x1080 window.
Memory start / end: commit 93/115 GB, nonpaged 24.50 -> 24.52 GB, C: 14.7 -> 14.5 GB free.

## (1) regress-8map 9c3560e -> 65e856f, -Batching: 8 PASS, no BLOCK

| map | 9c3560e p90 | 65e856f p90 | delta |
|---|---|---|---|
| 501 | 2.24 | 2.13 | -0.11 |
| 502 | 2.00 | 1.90 | -0.10 |
| 503 | 2.36 | 2.30 | -0.06 |
| 504 | 2.57 | 2.50 | -0.07 |
| 507 | 2.39 | 2.34 | -0.05 |
| 508 | 2.09 | 2.05 | -0.04 |
| 509 | 2.77 | 2.72 | -0.05 |
| 510 | 2.58 | 2.50 | -0.08 |

No row needed the batching excuse: the workload counts are in parity. 504 head r1 was CONTAMINATED at 19:41:08 by an AssetTools
python (`cmp_hdoff.py`, started from another session during the hold; `regress/map-504-head-r1-CONTAMINATED.txt`). The 504 verdict
uses the clean head r2.

## (2) Gorge real play 4K, 300 s x3: PASS 3 / 3

| rep | p90 | under 3.33 | avg fps | kills | slow-frame bins |
|---|---|---|---|---|---|
| warm-up r0 (150 s, discarded) | 2.76 | | | | |
| r1 | 2.68 MET | 99.1 % | 471 | 1001 | GPU-bound 99 % |
| r2 | 2.84 MET | 98.1 % | 441 | 922 | GPU-bound 100 % |
| r3 | 2.72 MET | 99.1 % | 463 | 767 | GPU-bound 99 % |
| pooled | 2.74 MET | 98.8 % | | p50 2.20, p99 3.39, 412323 frames | |

Valid rows: spawned 64/64, participants 64, ended by Time, no contamination.

**Not yet attributed.** Against 9c3560e's mean of 3.30 (15:35-15:59), real play improves by 0.55 ms. The claimed savings add up to
about 0.36 ms, and the lockstep regress at 510 shows only -0.08. A plausible reading: the HUD text-shadow cache hits mostly when
several rendered frames share one sim step (~7 per step at 450 fps), which lockstep, at one step per frame, cannot show. An off-switch
check (WFC_NOHUDSHADOWCACHE=1, 300 s x2, plus a cache-on control) runs after slot A of the full verdict. If the off arm does not
land near 3.2-3.3, look for an environmental cause before M1 closes.
