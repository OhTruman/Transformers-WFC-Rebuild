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
| **Campaign D1 Off, GPU-skinned actors** (2 reps, head 62911b5c + D1_GPUSKIN) | 60.0 | **56.1 / 56.4** | 1.27 / 5.24 / 9.43 | **10.4 / 13.3 / 13.9** | 1346 / 14 |
| **Campaign D1 Realistic, GPU-skinned actors** | 60.0 | **50.0** | 13.95 / 19.85 / 21.83 | 11.5 / 15.2 / 15.9 | 1518 / 16 |
| TDM Faithful, denoiser on (bin/D3D12 present) | 60.0 | 279 | 3.23 / 3.59 / 4.20 | 3.20 / 3.66 / 4.15 | 3 / 2 |
| TDM Realistic, denoiser on | 60.0 | 138 | 6.84 / 7.39 / 7.84 | 6.75 / 7.30 / 7.77 | 3 / 2 |

**Correction (RT rows above):** the first Faithful / Realistic rows ran without the shadow denoiser. The run folders lacked
bin/D3D12 (the Agility runtime its SM 6.6 PSO needs; the release build ships it). Faithful also composited the never-written
denoised texture there and drew 87-98 % black. The denoiser-on rows are the two above (Faithful 279, Realistic 138).
RENDERING_RAW_SHADOW.patch makes a machine without the runtime fall back to the raw sun visibility: mean brightness 17.3, like
Off, instead of black.

## D1 resolved (RENDERING_D1_GPUSKIN.patch)
- **First split invalid.** The WFC_SKIPMAT=TR_ arm changed nothing (48.6 ms), but that switch only filters the static world draw
  list, not character draws.
- **Lights are not the cost.** WFC_OLDCHARENV (no DirectLightEnv rays) saved only 3.4 ms.
- **Profile.** WFC_CPUPROF + WFC_TICKPROF + WFC_DRAWPROF: the sim step is 2.1 ms and the World::draw actors 2.4 ms.
- **The cost:** World::cpDrawSkeletal. The chapter's 244 matinee skeletal set-dressing actors were CPU-skinned every frame
  (evaluatePose / skinPose, normalising every normal), and drawDynamic then re-checked every vertex.
- **The fix:** the same GPU skin path the characters and transforming actors use (drawSkinnedGpu, palette rebuilt only when the
  clip / time changes, CPU fallback kept).
- **Result:** CPU 48 -> 10.4 ms; 12.9 -> 56 fps (Off) and 12.4 -> 50 fps (Realistic); 0 actors fell back to the CPU.
- **Eye check:** identical actors and world (docs/handoff/perf_pass_shots/d1_cpu_vs_gpuskin_p05.png). The remaining difference is
  the Frontend's brightness-calibration panel, which the newer head draws in-game.

## FG eye check
WFC_FGCAPTURE=300@165 captured FSR 3 generated frames ~36 s into a TDM match with the pawn walking:
docs/handoff/perf_pass_shots/fg_gen_301_match.png and fg_gen_301_pawn.png (crop). One pawn: no doubling, no ghost edges.

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
