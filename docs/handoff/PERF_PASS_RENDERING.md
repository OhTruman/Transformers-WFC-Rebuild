# Performance pass: Rendering rows (2026-10-24)

Head 74e41849 + the FG-output capture (work/cand_smoke). 2560x1440 window, frame limit uncapped (the cost, not the limiter),
VSync off, frontend boot -> the mode / map by script, the local pawn auto-walking, frame log; match window InGame+5 .. +50 s.
fps = real frames (FG: generated frames on top). gpu / cpu = the renderer's per-frame GPU and CPU time (ms).
Raw logs: work/perfrun/<ARM>/out.txt (Rendering worktree); shots: docs/handoff/perf_pass_shots.

| row | menus fps | match fps | GPU p50 / p95 / p99 | CPU p50 / p95 / p99 | frames > 16.7 / > 33 ms |
|---|---|---|---|---|---|
| TDM Streets Off | 60.0 | 773 | 0.46 / 0.77 / 0.93 | 0.90 / 1.29 / 1.39 | 3 / 2 |
| TDM Faithful (RT 1) | 60.0 | 292 | 3.05 / 3.43 / 4.06 | 3.03 / 3.54 / 4.06 | 3 / 2 |
| TDM Realistic (RT 2) | 60.0 | 141 | 6.71 / 7.27 / 7.71 | 6.62 / 7.15 / 7.62 | 3 / 2 |
| TDM HD Faithful | 60.0 | 770 | 0.47 / 0.75 / 0.92 | 0.91 / 1.30 / 1.40 | 2 / 1 |
| TDM HD Enhanced | 60.0 | 584 | 0.59 / 1.06 / 1.37 | 1.37 / 1.50 / 1.59 | 2 / 1 |
| TDM Upscaling FSR 1 Quality | 60.0 | 582 | 0.56 / 1.19 / 1.37 | 1.37 / 1.50 / 1.57 | 3 / 2 |
| TDM FG Off | 54.1 | 327 (+ generated) | 0.98 / 1.84 / 2.29 | 1.41 / 1.60 / 1.79 | 2 / 1 |
| TDM FG Realistic | 52.8 | 97 (+ generated) | 9.54 / 10.13 / 10.43 | 1.50 / 1.99 / 2.09 | 6 / 2 |
| Escalation Remnant Off | 60.0 | 120 | 1.30 / 3.84 / 4.80 | 4.22 / 6.89 / 8.37 | **436** / 3 |
| Escalation Remnant Realistic | 60.0 | 77 | 8.16 / 9.04 / 9.39 | 8.00 / 8.98 / 9.59 | **479** / 3 |
| **Campaign D1 Crash Off** | 60.0 | **12.7** | 3.92 / 43.17 / 47.80 | **48.1 / 51.7 / 52.0** | 572 / 572 |
| **Campaign D1 Crash Realistic** | 60.0 | **12.4** | 43.05 / 46.84 / 50.66 | **49.2 / 52.6 / 54.2** | 556 / 556 |
| DLC map | - | - | (waits for its final render data) | | |

## Findings
1. **Menus held at 60 (MENU_CAP2): confirmed** in every row (FG rows ~53-54: the real frames under FG).
2. **TDM Streets is healthy in every setting.** Off 773 fps / 0.46 ms GPU (the original look far above the 144 Hz class);
   Realistic 141 fps at 1440p (above the user's ~60 floor; budgets are reports, memory).
3. **FSR 1 Quality is slower than native here** (582 vs 773 fps): at sub-millisecond frames the upscale pass costs more than
   the smaller render saves; it pays off only where the GPU is the limit (Realistic / 4K). Not a defect; worth a note in the
   preview panel (the Frontend's text), not a code change.
4. **Campaign D1 runs at ~12.5 fps, CPU-bound (48 ms / frame) in Off and Realistic alike.** The cost sits in the
   "characters + caller" span (frame spikes: characters+caller 45-58 ms, world 0); D1 draws 469 dynamic objects (Streets 29)
   with 645 draws. The span also covers the game code calling the renderer, so the next step is a split: D1 with the
   character materials skipped (WFC_SKIPMAT=TR_) - if the 48 ms drop, it is the character path (light environments on the
   chapter's many lights / skinning), mine; if not, the game side (Gameplay's campaign AI / matinee / World draw). Needs a quiet
   CPU (after Integration's Static chain). **This is the pass's one real problem.**
5. **Escalation stutters**: ~8 % of frame intervals over 16.7 ms while the renderer's own CPU stays under 9 ms p99 and the
   GPU under 10 ms - the gaps come from outside the renderer (the simulation step: waves / AI). For Gameplay / Systems.
6. **FG**: presents normally (0 resets). The generated-frame capture (WFC_FGCAPTURE) works; the first set landed in the menu
   (FG generates there too), so a time-gated capture (n@seconds) of the walking match follows in the same slot; the eye
   check images go to docs/handoff/perf_pass_shots.

## Lossless fixes
None proposed from these rows yet: TDM is fast in every column and no visible-cost-free lever beats measurement noise there.
D1's CPU problem is the target - its fix follows the split test as its own patch.
