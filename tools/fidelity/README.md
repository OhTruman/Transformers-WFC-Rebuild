# wfc_fidelity — fidelity regression & measurement harness

Windowless, deterministic driver for the production gameplay code (`PlayerController` →
`CharacterMovement` → `Weapon` → `Character::updateAnimation`, same order as `World::tick`), with
the real extracted Optimus models. Scripted `InputFrame`s at a fixed step → bit-identical traces.
Built by default with the main tree (`.\build.ps1 -Jobs 2` → `build/bin/wfc_fidelity.exe`).

```powershell
.\build\bin\wfc_fidelity.exe                       # all suites; exit 1 on any FAIL
.\build\bin\wfc_fidelity.exe -v --json work\fidelity\report.json --trace-dir work\fidelity\traces
.\build\bin\wfc_fidelity.exe --only movement,transform
.\build\bin\wfc_fidelity.exe --map                 # + real Streets collision (≈2 s load)
.\build\bin\wfc_fidelity.exe --no-assets           # pure-code checks only
.\build\bin\wfc_fidelity.exe --dump-models         # clip names/categories/durations + skeleton
.\tools\fidelity\capture.ps1 -Name strafe -Frames 120 -Env @{ WFC_AUTOSTRAFE='1' }   # PNG still
```
### Playtest suites and the runtime probe
`PlaytestChecks.cpp` adds `transform_momentum`, `fast_movement`, `fine_aim`, `boost`,
`input_edges`, `vehicle_materials` and `map_content` (see [PLAYTEST-01.md](PLAYTEST-01.md)). The
material and map suites read Rendering's generated render data when `WFC_RENDER_DATA` points at
it (`tools/render/build_render_data.ps1`).

`runtime-probe.ps1` drives the real `wfc_rebuild.exe` through scripted scenarios: startup,
sustained fire, reload while moving, manual reload press, boost, transform and fixed-camera stills.
It also runs an offline material-consistency check, and turns logs into the same report JSON:
```powershell
.\tools\fidelity\runtime-probe.ps1 -Exe work\ab\int\build\bin\wfc_rebuild.exe -RenderData work\int-src\work\render -Name int
.\tools\fidelity\runtime-probe.ps1 -Exe … -RenderData … -Name m -Sections materials   # one section
```
The exe integrates wall-clock time, so probe scenarios assert presence and counts, not timings;
exact numbers come from `wfc_fidelity`.

### Measurement builds (milestone 03)
Opt-in CMake targets compile the **unmodified** product sources plus one instrumentation file:
```powershell
.\.toolchain\cmake-4.4.3-windows-x86_64\bin\cmake.exe -S . -B build -DWFC_BUILD_MEASURE=ON
.\.toolchain\cmake-4.4.3-windows-x86_64\bin\cmake.exe --build build --target wfc_rebuild_prof wfc_rebuild_observe wfc_rebuild_count
.\tools\fidelity\perf-profile.ps1      # idle/walk/fineaim/burst/held_fire  -> work\fidelity\perf\
.\tools\fidelity\perf-report.ps1       # cost x origin attribution, hot functions, accumulation regression
.\tools\fidelity\audio-attach.ps1      # sounds left at their trigger point while the owner moves
.\tools\fidelity\stills.ps1            # transform contact sheets + vehicle material angles
.\build\bin\wfc_fidelity.exe --map --only trace_cost   # deterministic per-ray collision cost
```
`wfc_rebuild_prof` samples the main thread at ~1 kHz (SEH unwind; `WFC_PROF=<file>`).
Results and ownership: [MILESTONE-03.md](MILESTONE-03.md); preserved evidence: [results/milestone-03/](results/milestone-03/README.md). Merged-build validation: [results/integration-m03-356c352/VALIDATION.md](results/integration-m03-356c352/VALIDATION.md).

### Milestone 03 pass 2: deterministic exe, analyzers, merge gate
| Target / script | What |
|---|---|
| `wfc_rebuild_observe` | **Lockstep** exe: `Time.cpp` → `measure/LockstepClock.cpp` (exactly one 60 Hz step per frame, so frame N is always the same moment), frame grabber (`WFC_GRAB=60:180:2`, `WFC_GRAB_DIR`), debug-overlay record (`WFC_DEBUGSTATE`), audio recorder (`measure/SpyAudio.cpp`: simulation-time stamps, models `isPlaying()`). Product sources are unmodified. |
| `wfc_rebuild_count` | Lockstep, plus `-finstrument-functions` on collision / world / FX / skinning / WFC pipeline → `measure/CallCounter.cpp`. Gives exact per-frame call counts; absolute times are inflated, so use counts only. |
| `m03-gate.ps1` | **Milestone 03 acceptance gate** (Integration runs `.\tools\fidelity\m03-gate.ps1` after merging): build, render data, every suite below, comparison with `results/m03-baseline`, `M03-GATE.md` (PASS/FAIL/KNOWN/INFO plus PERFORMANCE / VISUAL / AUDIO / TRANSFORMATION / VEHICLE / MAP regressions and the HUMAN CHECK list), exit 1 on any FAIL. `-Quick`, `-SkipBuild`, `-ReportOnly` (re-compare an existing run), `-WriteBaseline` (Experimental only), `-CounterTimeoutSec`. |
| `transform-capture.ps1` | Real-exe transform analyzer: lockstep grabs through both folds (fixed side camera and moving chase camera), per-frame CSV, image-continuity pops, exact debug-geometry record, camera pops, labelled contact sheets. |
| harness `transform_analyzer` | Simulation side: authored overlap windows (R→V 0.396–0.880 s, V→R 0.098–0.663 s), drawn-pose freeze vs authored holds, mesh appearance time / normalized clip time, silhouette jump, far-flung vertices, camera cut / swing, usable weapon without a muzzle; per-step `transform_<case>.csv`. |
| harness `vehicle_profiles` | HOVER / BOOST / DASH / NITRO through the production `PlayerController::applyToPawn` path on synthetic terrain (flat, 0.5 m step, bumps, 4 m ledge): accel/decel, steering at speed, yaw lag, lateral, reversal, hover height / drop / oscillation, terrain snap, airtime / landing, jump. Every value tagged CONFIRMED ORIGINAL / HIGH CONFIDENCE / UNKNOWN; `vehicle_profiles.json`. |
| harness `fine_aim_probe` | State, RMB toggle, FOV entry/exit time, camera distance and lateral offset (unresolved: [FINE-AIM-EVIDENCE-REQUEST.md](FINE-AIM-EVIDENCE-REQUEST.md)), look / move / spread multipliers, reload interrupt / resume, transform cancel, start/end audio wiring, reticle. |
| `audio-attach.ps1` | Every sound instance joined with the owner pose of its frame: cue (via the tree's own `SoundCues.inc`), attachment, owner-local offset, attenuation, pan, gain, lifetime. Flags sounds that stop following the pawn; presence checks for footsteps / transform / landing / fine-aim cues; `offending_cues.txt`. |
| `vehicle-visual.ps1` | Fixed-camera vehicle stills (idle angles WFC / legacy / glTF bakes, hover FX, boost, dash, nitro on an open run, transform midpoint, darkest/brightest spawn) with a material sidecar (slots, chain, textures by role, switches, runtime params, EnergonColor, customization push). |
| `map-audit.ps1` | Streets inventory in seven classes, per object; consumes Rendering's `map_audit.json` when present; resolves PrefabInstances through member tags; covers ambient audio (runtime), movers, pickups, the destructible. |
| `perf-report.ps1` (rebased) / `perf-counters.ps1` | Costs: collision, visibility, lighting, skinning, particles, audio, render, gameplay. Origins: hitscan vs camera-aim ray, shell/magazine meshes, vehicle FX. Exact counters for visibility rays, `segmentHit`, light envs, skinning, dynamic/mesh draws. |
| `camera-trace.ps1` | Frame-by-frame camera trace through a transformation (observe exe, lockstep, no camera input): camera pose, pawn, form, camera strategy, collided orbit distance, flagged jumps, contact sheet. `-Env @{...}` for spawn/timing variants. |
| `perf-release.ps1` | Release frame cost per scenario from the renderer's 120-frame averages (`WFC_RENDERSTATS`); fixed-view and turning variants (frame cost is view-dependent). INFO vs integration baselines. |
| `checkpoint-suite.ps1` | **One command for every suite below** against a built tree (`-Root`), with `-Baseline <previous run>` for before/after; writes `SUITE.md` (PASS / FAIL / KNOWN / INFO / SKIP / HUMAN). |
| `playtest-regressions.ps1` | Human-playtest regressions as measurements: smoke/fog distance series (veil, close-range change, colour, motion) A/B against `WFC_SKIPMAT` / `WFC_NOMAPFX`; glass floors from player-like cameras (sampled surface vs where the glass contributes); ramp views (near-black). Flags are HUMAN, never FAIL. |
| `streets-sweep.ps1` | Full-footprint visual sweep from authored navigation (nodes + edge samples, 4 views each, categories interior / exterior / tunnel / ramp_stairs / glass / skyline / pickups / movers / particles + robot / vehicle chase cams); one map load per 200 shots via `WFC_SHOTLIST`; fingerprints + `-Baseline` before/after sheet; `-CompareOnly`. |
| `transform-stress.ps1` + `collision_query.py` | 84 authored starts x 9 transform scenarios (stationary / walk / airborne / drive / boost / nitro / boost+turn) on the real exe; trajectories checked against the AUTHORED collision_pawn.glb (fell-through at the crossing frame, out of bounds, wrong form, legal drops); resumable. |
| `mode-presentation.ps1` | DM/TDM/CTF/EXT/DOM/KOTH presentation: the same views per mode diffed against DM; authored expectations (Kismet unhide, RequiredGameRuleClass) PASS/FAIL, RE ones conservative; KOTH ring judged on the whole frame. |
| `match-validator.ps1` / `frontend-validator.ps1` | Validators for the MATCH / FRONTEND log protocol ([protocols/RUNTIME-EVENTS.md](protocols/RUNTIME-EVENTS.md)); authored/RE expectations only, UNKNOWN stays INFO; self-test logs in `protocols/samples/`. |
| `soak-monitor.ps1` | Release soak (real time): memory / handle slopes, frame-time, draws, map FX, cues and voices trends; duplicate-render check (draws per material vs uploaded sections); repeated-launch memory. |
| `final-gate.ps1` | **Next-milestone final gate** (one integration commit, Debug + Release of the same sha): lifecycle gate, playtest acceptance, motion jitter, Debug gate, transform stress, vehicle collision, visual three-way, soak. `FINAL.md` separates PRODUCT FAIL (owner lane) / TEST-HARNESS FAIL (Experimental) / KNOWN / UNKNOWN + WAITING / HUMAN CHECK. Human list `HUMAN-CHECK-NEXT.md`. |
| `playtest-acceptance.ps1` | The M05 human-playtest reports as checks: intro video + audio, title background, title keys (shipped buttonStart rule), back navigation, loading keeps updating (outside-the-process periodic capture), overlay gone, match state vs HUD presentation table, kill feed (TnDeathMessage), PointEvent, result state, minimap (UNKNOWN pending RE). |
| `motion-jitter.ps1` | Character / vehicle jitter split by cause: simulation / animation steps (A), composed-frame ping-pong (B), frame pacing at 60 / 144 / 240 Hz via WFC_RENDERHZ + WFC_CAMLOG (C), per-frame vehicle frames via WFC_SHOTEVERY (D), vehicle hover / boost / nitro states vs RE (E). |
| `lane-watch.ps1` | Branch-aware report of each lane head vs the integration commit: areas and shared seams, hooks added / removed (stale-test risk), new trace evidence, owner claims, merge-tree conflicts. Nothing merged. |
| `m05-final.ps1` | **Milestone 05 final validation** against one integration commit: isolated build (`m05/build-target.ps1`), lifecycle gate Release + Debug, transform stress, vehicle collision, visual three-way compare, soak; writes the hand-off `M05-FINAL.md`. Design: `m05/README.md`; human list `HUMAN-CHECK-M05.md`. |
| `m05-e2e-gate.ps1` | M05 lifecycle gate: cold boot / intro → shipped menus by key presses → TDM Streets → match end → lobby → second match → frontend; repeated cycles with memory / GL / audio attribution; map (Gorge) and mode (DM) ownership; owners' self-tests on the same exe. Layers DATA / STATE / PRESENTED / MATCH / AUDIO / OWNERSHIP / LIFETIME / SELFTEST. Helpers `lib/Flow.ps1`, `lib/M05.ps1`; expectations `m05/expectations.json` |
| `vehicle-collision.ps1` | Truck against walls / low blocks / props / gaps: 84 starts x 8 drives, every frame step swept against the authored collision (`collision_sweep.py`), the actor passed through named |
| `m04-world-audio.ps1` | Living-map audio on the observe exe (spy + `WFC_AMBLOG`): authored emitters represented, map-start limits, line/volume placement, one-shot pools per zone, reverb zones, leaks, duplicate launches, pickup sounds. Results: [results/m04-living-map](results/m04-living-map/LIVING-MAP.md). |
| `m04-captures.ps1` | Fixed-camera captures of authored targets (movers, objective bases, totem, steam, pickups, destructible) with a `WFC_SKIPMAT comp:` visibility mask, `WFC_SHOWHIDDEN` control, luma change and footprint IoU (geometric motion); spawn and vehicle-route stills. |
| `m04-ambient.ps1` / `m04_assettools.py` | Zone entered at every FFA start vs audio.json; AssetTools `validate_m04.py` run read-only. |
| `render-selftests.ps1` | Rendering's in-product `WFC_SHADOWSELFTEST` / `WFC_DLETEST` lines as results (SKIP when the tree lacks them). |
| harness `native_vehicle` / `native_robot` | Native RE M03 P1–P8 assertions (rest COM height, probes, push-only springs, RB gravity, release, steps, drop, airborne upright, camera-yaw heading, hover/boost jumps, Dash, fine-aim orbit offset, hand shrink, ram reaction); provisional items reported as INFO. Validation of gameplay 6dfaf0d: [results/xbranch-gameplay-6dfaf0d](results/xbranch-gameplay-6dfaf0d/README.md). |
| `ab.ps1 -Measure` | Also builds prof / observe / count for any ref in isolation (cross-branch validation). |
Shared helpers: `lib/Run.ps1` (process env, frame-log parser, report writer, sheets), `lib/ImageStats.cs`.
Human checklist: [HUMAN-CHECK.md](HUMAN-CHECK.md). Pass-2 findings: [MILESTONE-03-PASS2.md](MILESTONE-03-PASS2.md). Open native-RE items: [VEHICLE-RE-REQUEST.md](VEHICLE-RE-REQUEST.md), [FINE-AIM-EVIDENCE-REQUEST.md](FINE-AIM-EVIDENCE-REQUEST.md).

### Cross-branch A/B and merge gating
```powershell
.\tools\fidelity\ab.ps1 -Ref agents/gameplay                        # → work\ab\agents_gameplay\report.json
.\tools\fidelity\ab.ps1 -Ref HEAD -Patch my.patch -Name trial       # prototype a fix without committing
.\tools\fidelity\ab.ps1 -Ref agents/gameplay -Merge agents/systems  # merge preview (exit 3 + file list on conflict)
.\tools\fidelity\diff-reports.ps1 work\ab\main\report.json work\ab\agents_gameplay\report.json
```
`ab.ps1` uses `git archive` / `git merge-tree` (no worktree, index or ref is touched), overlays
*this* harness so every ref is judged by identical checks, builds into `work\ab\<name>\build` with
this worktree's toolchain. `diff-reports.ps1` tags REGRESSED / FIXED / changed / moved and exits 1
on new FAILs. Optional Character accessors (layer weights) are detected at compile time
(`fid::layer` in `Rig.h`), so API differences between branches don't break the build.

Run from the worktree root (the reference sheet path is relative). Assets are read-only, from
`WFC_ASSETS` or `Config.h kAssetRootDefault`.

## Status semantics
| Status | Meaning |
|---|---|
| PASS | matches the recovered original value / confirmed behaviour |
| FAIL | **regression** of something already recovered → non-zero exit |
| KNOWN | documented deviation with an `owner=` workstream; does not fail. Flips to PASS with `RESOLVED` once fixed — then delete the `known(...)` call or turn it into `near(...)` |
| INFO | measurement without a confirmed original value (prediction shown as `model`); `SUSPECT` prefix = likely deviation, unconfirmed |
| SKIP | prerequisite missing |

## Suites
- **constants** — every `[CONF]` value in `Config.h` re-derived from its UU source literal (÷100), derived JumpZ, projected horizontal FOV.
- **weapon_data** — `game::Weapon` defaults vs `weapon.json` (read live, so re-extraction drift is caught).
- **weapon** — fire cadence, mag dump, auto/manual reload timing + ammo accounting, spread bloom/cap/decay.
- **orientation** — screen-space handedness (D → screen right, mouse right → turn right), body follows aim, chase cam behind body, hitscan ray through crosshair, vehicle faces travel / turn rate.
- **movement** — robot accel/top speed/diagonal/stop, jump apex & airtime at 30/60/120 Hz, running jump, air control, vehicle hover/cruise/dash/jump.
- **collision** — synthetic box scenes: wall stop gap, wall slide, step-height sweep, low-obstacle penetration.
- **transform** — paired clip durations (data), clip actually played at handoff, total time, input lock, weapon holster.
- **animation** — directional strafe clip choice F/B/L/R, idle/jump/fall categories, reload clip, joint count.
- **muzzle** — skeleton-derived body facing vs gameplay yaw (independent of the yaw variable), vehicle long axis, muzzle/barrel in facing frame, tracer-vs-trace parallax, jog bob.
- **determinism**, **performance** (per-step sim+skin cost), **map** (opt-in).
- **original_vs_rebuild** — `reference/original_measurements.json`: one slot per metric, with a capture method. Fill `original` from 360/emulator captures; entries become PASS/FAIL automatically.

## Traces
`--trace-dir` writes one CSV per scenario (`step,t,x,y,z,vx,vy,vz,hspeed,yaw_deg,…,anim,anim_t,ammo,…,muzzle_xyz`) — use for plots or frame-by-frame A/B against original footage.

## Notes / limits
- Collision scenarios mirror `PlayerController::handleInput`'s intent mapping in `Rig::step` (World's collision member is private); keep in sync.
- `Models::get()` duplicates World's weapon-socket matrix; keep in sync with `World::loadVerticalSlice`.
- `WorldStub.cpp` replaces `World::fireHitscan` (records shots). World.cpp is not linked.
- The legacy `tests/GameplayTests.cpp` targets an API that no longer exists (`game/Gameplay.h`) and is not built; this harness supersedes it.
- The in-game `WFC_SMOKE_FRAMES` path integrates wall-clock time, so its numbers vary per run; prefer this harness for numbers and `capture.ps1` for pictures.

Expectations confirmed from native RE: [RE-EXPECTATIONS.md](RE-EXPECTATIONS.md) (`conf` checks: PASS when the build matches a CONFIRMED original, KNOWN with an owner otherwise).

Findings by owning workstream: [FINDINGS.md](FINDINGS.md). Integration checkpoint (four owner patches, duplicated-work merge warning): [CHECKPOINT.md](CHECKPOINT.md).
