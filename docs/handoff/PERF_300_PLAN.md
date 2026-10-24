# 300 FPS minimum: campaign + Escalation (plan, 2026-10-24)

**Target (user):** every campaign chapter and every Escalation map holds 300 fps as the **worst frame**, not the average.
- **Settings:** the original look: Off, no HD, no RT, no FG.
- **Resolution:** the user's native resolution, reported both ways: 2560x1440 (as read from the desktop) and 3840x2160 (4K
  panel unconfirmed).
- **Budget:** 300 fps means 3.33 ms for the whole frame (sim + draw submission + GPU + present), every frame.
- **Ownership:** Rendering leads the render side. Gameplay owns the sim (Escalation waves, AI spikes, the campaign scripting).
  Systems owns the platform (present, threads, timers, audio, loading I/O).
- **Rules:** every fix is lossless. Identity is checked (same exe, equal SIMHASH, pixel identity or a stated bound) plus an eye
  check, with 2-rep A/Bs.

## Where we stand (2560x1440, uncapped, Off)
| row | fps | CPU p50 / p99 ms | GPU p50 / p99 ms | gap to 3.33 ms |
|---|---|---|---|---|
| D1 Crash (after D1_GPUSKIN) | 56 | 10.4 / 13.9 | 1.27 / 9.43 | CPU 4x over; GPU p99 3x over |
| Escalation Remnant | 120 | 4.2 / 8.4 | 1.30 / 4.80 | CPU 2.5x; ~8 % of intervals > 16.7 ms (sim) |
| TDM Streets (reference) | 773 | 0.90 / 1.39 | 0.46 / 0.93 | fits; 3 frames > 33 ms (hitches) |

Even the healthiest row has hitches, so **the worst-frame rule is mostly a hitch hunt.** It fails on any single spike: a first-use
shader, a texture upload, a spawn, an allocation storm, a streaming load.

## Metric and harness (phase 0)
- **Window:** InGame + 5 s to + 60 s of play, with the pawn walking a fixed route (WFC_AUTOWALK).
  - Campaign rows also run a scripted route through a combat beat and one streaming boundary.
  - Escalation rows cover waves 1-3 and one generator purchase.
- **Reported:** max frame interval (= min fps), 1 % low, p50 / p95 / p99 of CPU and GPU, and frames > 3.33 ms with their
  per-pass split.
- **Out of window, reported but not counted:** load screens and the frame of a map change. Chapter streaming inside a chapter
  IS counted, because the player plays through it.
- **Cost split per frame:** existing timers.
  - Sim: WFC_TICKPROF.
  - World::draw actors: WFC_DRAWPROF.
  - Characters / set dressing / map FX / translucency / post: the GPU pass marks and the spike logger.
  - Present: D3D12 presenter timestamps.
  - CPU hot spots: WFC_CPUPROF + llvm-symbolizer (image base 0x140000000).
- **Harness:** scratchpad perfrun.sh, extended.
  - New arms: one per chapter (A1-A5, D1-D5, by campaign map id) and one per Escalation map (Remnant, BrokenHope, Tranquillity,
    District, Pulse, Static).
  - Each arm gets the 3.33 ms frame list and a per-arm CSV for the table below.
  - Run folders copy bin/D3D12.
- **4K:** a 3840x2160 window does not fit a 1440p desktop, and non-native fullscreen is off-limits (it lost the monitor's
  144 Hz before). Two options:
  - render at 3840x2160 into the offscreen target with WFC_RENDERSCALE (if it exists; else a small harness switch), presented
    downscaled: GPU cost is identical, present differs;
  - the user's 4K panel, if confirmed.

## Rows (filled in phase 0, re-measured per lever)
| row | map | min fps 1440p | 1 % low 1440p | min fps 4K | 1 % low 4K | CPU p99 | GPU p99 | sim p99 | top pass |
|---|---|---|---|---|---|---|---|---|---|
| A1 | | | | | | | | | |
| A2 | | | | | | | | | |
| A3 | | | | | | | | | |
| A4 | | | | | | | | | |
| A5 | | | | | | | | | |
| D1 Crash | | | | | | | | | |
| D2 | | | | | | | | | |
| D3 | | | | | | | | | |
| D4 | | | | | | | | | |
| D5 | | | | | | | | | |
| ESC Remnant | | | | | | | | | |
| ESC BrokenHope | | | | | | | | | |
| ESC Tranquillity | | | | | | | | | |
| ESC District | | | | | | | | | |
| ESC Pulse | | | | | | | | | |
| ESC Static | | | | | | | | | |

## Levers, in expected order of payoff
Each lever gets a 2-rep A/B on one exe, identity (SHOTMATCH frames equal, or a pixel bound stated in advance) and an eye check.
Nothing merges that changes a pixel without a stated reason.

### Render CPU (Rendering)
1. **Campaign dynamic draws.** D1 issues ~470 dynamic draws per frame against Streets' 29. Each is its own drawDynamic: state
   changes, uniforms, its own light environment.
   - Profile D1 again after D1_GPUSKIN (where the remaining ~8 ms of non-sim CPU goes).
   - Then cache static-pose set dressing: an actor whose clip time does not change keeps its palette and bounds (partly done).
2. **Skinned batching / instancing.**
   - The 244 set-dressing actors each load their own model copy; share the model by mesh path, so one bind mesh and one GPU
     buffer serve every instance.
   - Then draw instances of one mesh in one multi-draw: the bone texture already holds every palette, the per-draw
     index is the instance.
   - Characters with the same chassis batch the same way.
3. **Light environments.** WFC_OLDCHARENV saved 3.4 ms on D1, so the per-actor DirectLightEnv update costs that much.
   - Update set dressing's environment only when it moves (static actors: once).
   - Amortize characters' N-of-M rays over frames, as UE3 does: a lossless match to the original's cadence.
4. **Map FX / particles CPU.** The throttle already exists; check the campaign's emitter counts per chapter.
5. **Shadow pass.** Off draws the original's character shadows.
   - Cache static casters' shadow depth per light.
   - Cull casters by the light's frustum and the view; skip lights with no visible receiver.

### Render GPU (Rendering)
6. **GPU spikes.** D1's GPU p99 is 9.4 ms against a 1.3 ms p50. Find them with the pass split:
   - translucency overdraw and particle bursts;
   - first-use shader programs (prewarm the chapter's set at load);
   - texture uploads during play (move to the load or to a timed upload budget).
7. **4K GPU.**
   - Post chain at full resolution.
   - Lightmap / material sampling cost.
   - The depth prepass's value per map (it is on for the world: keep where it wins).

### Hitches (Rendering + Systems)
8. **First-use costs.**
   - Shader compiles: the program cache warmed at load per chapter / map.
   - Mesh and texture first uploads.
   - Weapon / ability model loads at first use: the barrier / sentry / mine models load on the main thread at the first step.
9. **Allocation storms.** WFC_ALLOCPROF on the spike frames; reuse buffers across frames.
10. **Streaming.** Campaign unit loads happen inside chapters: keep them on the worker with a per-frame time budget for the GL
    part.

### Sim (Gameplay)
11. **Escalation wave spawns and AI decision spikes.**
    - Remnant's ~8 % of intervals over 16.7 ms come from the sim, while renderer CPU p99 is < 9.6 ms.
    - Spread spawns across steps; cap per-step AI work.
12. **Run the sim alongside the draw.** The background part of World::tick exists: overlap more of it with the render
    submission. The 3.33 ms budget cannot hold sim and draw back to back on a heavy chapter.

### Platform (Systems)
13. **Present and pacing.**
    - D3D12 presenter cost per present.
    - GL-D3D12 interop copy at 4K.
    - Timer resolution and the frame limiter off.
14. **Background threads.** Audio, loading I/O and the memory leak watch must not preempt the main thread on spike frames.

## Order of work
1. **Phase 0:** harness rows plus baseline for all 16 rows at 1440p and the 4K method. One card slot of about 16 x 6 min.
2. **Phase 1:** D1-class CPU levers 1-3 (the biggest, all render side), then re-measure the campaign rows.
3. **Phase 2:** hitch hunt 6, 8-10 on every row (the worst-frame rule).
4. **Phase 3:** Gameplay's sim items 11-12, then Systems' 13-14 in parallel; Rendering re-measures.
5. **Phase 4:** 4K GPU items 7.

## Open
- **4K measurement:** the render-scale offscreen method or the user's panel (the panel is unconfirmed; the desktop reads 2560x1440).
- **Whether 300 fps is reachable on the heaviest chapters with the sim on the main thread.** Phase 1 re-measurement answers
  it; if not, item 12 becomes the critical path (Gameplay + Systems).
