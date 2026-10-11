# M1 re-verdict 9c3560e (TIMED window 2026-10-11 15:02-16:28)

Head 9c3560e = base 9be437d + Rendering d8c6720 (skin palette batching + near-face occlusion boxes). Both Gameplay sim changes
(b749a83 blast LOS, 5eeb3f7 pawnRayHit) are in base and head. Harness runs pin WFC_BOTAI=Classic (0f35196 / edd73e4).
4K = RenderSize 3840x2160 in a 1920x1080 window, as in the previous verdicts. Script: `reverdict_9c3560e.sh`, log: `reverdict_9c3560e.log`.
No CONTAMINATED rows; no leftover Experimental exes. Memory start / end: commit 92/114 GB, nonpaged 24.12 -> 24.14 GB, C: 17.1 -> 16.7 GB free.

## (1) regress-8map 9be437d -> 9c3560e (lockstep, BLOCK > +0.15): no BLOCK

| map | base p90 (r1 / r2) | head p90 (r1 / r2) | delta (best of 2) | verdict |
|---|---|---|---|---|
| 501 | 2.31 / 2.31 | 2.24 / 2.27 | -0.07 | UNKNOWN (parity) |
| 502 | 2.01 / 2.01 | 2.01 / 2.01 | 0.00 | UNKNOWN (parity) |
| 503 | 2.49 / 2.48 | 2.37 / 2.38 | -0.11 | UNKNOWN (parity) |
| 504 | 2.67 | 2.58 | -0.09 | PASS |
| 507 | 2.29 | 2.38 | +0.09 | PASS |
| 508 | 2.20 / 2.21 | 2.12 / 2.09 | -0.11 | PASS (r1 parity UNKNOWN) |
| 509 | 2.88 | 2.77 | -0.11 | PASS |
| 510 | 2.72 | 2.59 | -0.13 | PASS |

Every row is the same match (SIMHASH identical over steps 600-9600). The UNKNOWNs are the workload-parity gate (counts within 3 %):
d8c6720 lowers the counts by design, so a renderer change that batches cannot pass a count gate. Program binds -2.6..-4.8 %
(501 873 -> 831, 502 624 -> 600, 503 868 -> 841, 508 872 -> 843), dyn draws -7..-10 %, total draws -0.8..-1.3 %, fx draws equal.
The pawn-occlusion excuse covers dyn only, so it does not apply when binds move too. Raw p90 is equal or lower on all three maps. The gate is
left as it is: whether a batching change may relax the binds check is for Integration to decide.

## (2) Gorge real play 4K, 300 s x3, each <= 3.33: MISS (1 of 3)

| rep | p90 | under 3.33 | avg fps | slow-frame bins (match 2) | time under deck / ext. SE | enemies in range |
|---|---|---|---|---|---|---|
| r1 | 3.14 MET | 94.8 % | 392 | GPU-bound 99 % | 24 % / 64 % | 3.2 |
| r2 | 3.41 MISS | 86.9 % | 353 | GPU-bound 100 % | 47 % / 42 % | 5.1 |
| r3 | 3.36 MISS | 89.1 % | 353 | GPU-bound 99 % | 45 % / 43 % | 3.9 |
| pooled | 3.32 MET | 90.4 % | | p50 2.75, p99 3.88, 329133 frames | | |

The misses are entirely GPU-bound (render: chars <= 1 %, outside render ~0 %). The spread follows the player bot's route: r2 and r3
spent about twice as long in A_UNDER_DECK (pooled slow rate 13 % vs 9 % in EXTERIOR_MIDDLE_SOUTHEAST) and saw more enemies. Zone mix alone
predicts 9.3 / 10.2 / 9.9 % slow against the actual 5.2 / 13.1 / 10.9 %, so in-zone load (fights, enemies in view) adds to it (`rows/zonemix-510-rp.txt`).
Rep mean 3.30 ms. With sigma ~0.12 ms per rep, P(3 of 3 <= 3.33) ~ 22 %; about 90 % needs a mean of ~3.12, i.e. about 0.2 ms of GPU off Gorge 4K real play.

## (3) Checks, 150 s x2

| row | p90 | verdict |
|---|---|---|
| Gorge overview 4K | 3.08 / 3.09 | MET (no autojump since cf6d68b: not comparable with earlier overview rows) |
| Rust real play 4K | 2.96 / 3.15 | MET |
