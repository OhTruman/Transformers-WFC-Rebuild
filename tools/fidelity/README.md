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

Findings from the first run, by owning workstream: [FINDINGS.md](FINDINGS.md).
