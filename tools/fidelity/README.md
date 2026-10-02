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
.\.toolchain\cmake-4.4.3-windows-x86_64\bin\cmake.exe --build build --target wfc_rebuild_prof wfc_rebuild_audiospy
.\tools\fidelity\perf-profile.ps1      # idle/walk/fineaim/burst/held_fire  -> work\fidelity\perf\
.\tools\fidelity\perf-report.ps1       # cost x origin attribution, hot functions, accumulation regression
.\tools\fidelity\audio-attach.ps1      # sounds left at their trigger point while the owner moves
.\tools\fidelity\stills.ps1            # transform contact sheets + vehicle material angles
.\build\bin\wfc_fidelity.exe --map --only trace_cost   # deterministic per-ray collision cost
```
`wfc_rebuild_prof` samples the main thread at ~1 kHz (SEH unwind; `WFC_PROF=<file>`).
`wfc_rebuild_audiospy` replaces only `Win32Audio.cpp` with a recorder (`WFC_AUDIOSPY=<file>`).
Results and ownership: [MILESTONE-03.md](MILESTONE-03.md); preserved evidence: [results/milestone-03/](results/milestone-03/README.md).

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
