# Final combined gate - d5bb6863 (timed) - 2026-10-23 19:20-20:22

Integration's grant after the reboot (boot 19:13; nonpaged pool 929 MB at the start, the FlightRecorder driver disabled; desktop 2560x1440 @ 144, no other game exe). PERF_HOLD set by the runner at 19:20:53, card clear at 19:21:54, **removed at 20:21:51**; no CONTAMINATED rep, the pool guard never fired.

- **Base**: work/ab/fast_af4d073_final7 = af4d0731 (package 6) + Gameplay's FINAL_BASE_ITEMS 01-12 (every sim change since, incl. Escalation + campaign; git apply on a git-initialised af4d0731 tree) - package 6's renderer, the head's simulation.
- **Head**: work/ab/fast_d5bb686 = d5bb6863 (the final code).
- **Rows**: tools/fidelity/final-gate.sh gate - 10 MP maps (505 / 506 in Escalation) x {2560x1440, 1920x1080} x {base, head}, lockstep steps 600-9600, seed 1234, PlayerBot, 2 reps, default settings (Off look, RT off), one frozen render-data snapshot for every arm (work/matb/final_d5bb686_root).

**Every head row plays the same match as the base (40 / 40 'same match True').** Workload parity is False on 9 of 10 maps (the head draws more - the expected render-side additions since package 6) and True on Molten.

### 2560x1440

| Map | base p50 / p90 / p99 ms | head p50 / p90 / p99 ms | p50 delta | p99 delta | same match | workload parity |
|---|---|---|---|---|---|---|
| 501 Seed | 2.37 / 3.15 / 4.14 | 2.29 / 3.06 / 4.02 | -0.08 (-3 %) | -0.12 | True | False |
| 502 Berth | 2.25 / 3.29 / 4.63 | 2.15 / 2.84 / 3.67 | -0.10 (-4 %) | -0.96 | True | False |
| 503 Complex | 2.71 / 3.57 / 4.72 | 2.59 / 3.46 / 4.59 | -0.11 (-4 %) | -0.12 | True | False |
| 504 Rust | 2.95 / 3.87 / 4.65 | 2.96 / 3.85 / 4.62 | +0.01 (+1 %) | -0.03 | True | False |
| 505 BrokenHope (ESC) | 0.85 / 1.10 / 3.33 | 0.89 / 1.25 / 3.31 | +0.04 (+4 %) | -0.01 | True | False |
| 506 Remnant (ESC) | 1.10 / 1.35 / 2.15 | 1.05 / 1.37 / 2.44 | -0.05 (-4 %) | +0.29 | True | False |
| 507 Debris | 3.61 / 4.59 / 5.77 | 3.51 / 4.64 / 5.94 | -0.10 (-3 %) | +0.17 | True | False |
| 508 Streets | 2.66 / 4.02 / 4.94 | 2.56 / 3.62 / 4.62 | -0.09 (-3 %) | -0.32 | True | False |
| 509 Molten | 3.00 / 3.91 / 5.31 | 2.95 / 3.85 / 5.28 | -0.05 (-2 %) | -0.04 | True | True |
| 510 Gorge | 3.76 / 4.67 / 5.82 | 3.85 / 4.59 / 5.95 | +0.08 (+2 %) | +0.12 | True | False |

### 1920x1080

| Map | base p50 / p90 / p99 ms | head p50 / p90 / p99 ms | p50 delta | p99 delta | same match | workload parity |
|---|---|---|---|---|---|---|
| 501 Seed | 2.35 / 3.12 / 4.04 | 2.31 / 3.06 / 4.01 | -0.04 (-2 %) | -0.03 | True | False |
| 502 Berth | 2.26 / 3.27 / 4.64 | 2.18 / 2.87 / 3.69 | -0.08 (-4 %) | -0.95 | True | False |
| 503 Complex | 2.70 / 3.58 / 4.72 | 2.59 / 3.47 / 4.64 | -0.11 (-4 %) | -0.08 | True | False |
| 504 Rust | 2.96 / 3.88 / 4.75 | 2.96 / 3.87 / 4.88 | +0.00 (+0 %) | +0.13 | True | False |
| 505 BrokenHope (ESC) | 0.82 / 1.08 / 2.45 | 0.85 / 1.14 / 2.38 | +0.03 (+4 %) | -0.07 | True | False |
| 506 Remnant (ESC) | 1.10 / 1.39 / 2.43 | 1.06 / 1.35 / 2.08 | -0.04 (-4 %) | -0.34 | True | False |
| 507 Debris | 3.61 / 4.64 / 5.94 | 3.52 / 4.65 / 6.06 | -0.09 (-2 %) | +0.12 | True | False |
| 508 Streets | 2.63 / 4.04 / 4.97 | 2.58 / 3.65 / 4.62 | -0.05 (-2 %) | -0.36 | True | False |
| 509 Molten | 3.09 / 4.04 / 5.54 | 3.00 / 3.92 / 5.42 | -0.09 (-3 %) | -0.12 | True | True |
| 510 Gorge | 3.79 / 4.68 / 5.79 | 3.84 / 4.61 / 5.94 | +0.06 (+2 %) | +0.15 | True | False |

2560x1440: mean p50 base 2.52 ms, head 2.48 ms (-1.8 %); worst head p50 3.85 ms = 260 fps

1920x1080: mean p50 base 2.53 ms, head 2.49 ms (-1.6 %); worst head p50 3.84 ms = 260 fps

## Read

- **No regression**: the final code is 1.6-1.8 % faster than package 6 at the median on average at both resolutions; the largest slowdowns are Gorge +0.06 / +0.08 ms (+2 %) and BrokenHope +0.03 / +0.04 ms (+4 % of 0.85 ms); p99 improves most on Berth (-0.95 ms) and Streets (-0.33 ms).
- Worst head median 3.85 ms (Gorge) = about 260 fps; the 1440p and 1080p numbers are within 0.1 ms of each other, i.e. these default-look runs are CPU-bound on this machine (7950X3D), not GPU-bound.
- The stability set and the same-match pre stage: FINAL_GATE_PRE_c3c9c157.md (sim unchanged since for versus / ESC; the campaign items 08-12 ride in both arms here).

**FINAL GATE: PASS.** Logs work/finalgate/d5bb686/ (finalgate.log, regress-<res>/map-<id>.out).
