# Next integration: final gate, ready to run

Run this right after the merge is pushed (one command; isolated Debug + Release build of that exact sha; nothing
outside Experimental's `work/`):

```
.\tools\fidelity\final-gate.ps1 -Ref origin/integration/<milestone> -Build
```

It runs, in order:
1. the lifecycle gate (Release);
2. playtest acceptance;
3. motion / jitter;
4. the Debug gate;
5. transform stress;
6. vehicle collision;
7. visual three-way;
8. a 20-cycle soak.

It then writes `FINAL.md`:
- **PRODUCT FAIL** (owner lane);
- **TEST / HARNESS FAIL** (Experimental);
- **KNOWN**;
- **UNKNOWN / WAITING**;
- **HUMAN CHECK**.

`-Quick` is a 10-start, 4-cycle subset (about 1.5 h instead of about 6 h). The human list is
`tools/fidelity/HUMAN-CHECK-NEXT.md`.

## Hooks the gate consumes (Integration: please keep / carry them)
| hook / event | from | used by | missing → |
|---|---|---|---|
| `WFC_BOOT`, `WFC_FRONTEND_SCRIPT` (`wait:` / `key:` / `ui:` / `shot:` / `dump:` / `snapshot:` / `call:`), `WFC_FLOWLOG` events | Frontend | everything frontend-side | gate cannot run |
| `WFC_PLATFORM` | Frontend 76b8287 | Xbox 360 spec path pinned; R8 PC path | old key paths (the gate auto-detects) |
| `WFC_LIFECYCLE` | Integration M05 | R7 score-limit ×3, acceptance A3 | SKIP |
| `MATCH ...` lines, `match.*` / `audio.baseline` / `audio.loaded` / `audio.unloaded` flow events | Integration M05 | MATCH / AUDIO / OWNERSHIP | SKIP / weaker checks |
| `WFC_LOCKSTEP`, `WFC_PRESSTRANSFORM(_EVERY)`, `WFC_START`, `WFC_STARTVEHICLE`, `WFC_AUTO*`, `WFC_LOGEVERY` | int-04 / Systems | transform stress, vehicle collision, jitter A / E | non-deterministic or SKIP |
| `WFC_RENDERHZ`, `WFC_CAMLOG`, `WFC_SHOTEVERY` | **agents/rendering only** | jitter C (frame pacing: the M05 "interlacing" detector) and D | **WAITING**: please carry them |
| `WFC_BOOSTLOG`, `WFC_CUELOG`, `WFC_MUSICLOG`, `WFC_MIXERLOG`, `WFC_AMBLOG` | Gameplay / Systems | vehicle states, doubled sounds, music ownership | SKIP |
| `WFC_TDMTEST`, `WFC_MATCHTEST`, `WFC_XFORMTEST`, `WFC_CHAOS`, `WFC_RELOADTEST` | owners | SELFTEST layer | SKIP |
| `movie.audioStart` flow event | Frontend 8b6466e | intro audio | FAIL (no evidence) |

## What the next build is expected to change (from tonight's lane previews)
| check | M05 1e14900 | lane evidence |
|---|---|---|
| intro audio | FAIL 0 / 4 | PASS 4 / 4 on Frontend 8b6466e |
| return lands on the main menu | FAIL (Press START) | Frontend 76b8287 |
| title / lobby backgrounds | FAIL black | drawn on Frontend 0160cf1, but **lobby flat untextured grey 34%** and **black again after a return**; look is a human check |
| loading keeps presenting | FAIL (frozen 3.7–10 s) | animates on Frontend fd883dc |
| match music `DM_START` | FAIL | Systems 5f8d7c9 / 8769ecb |
| interlacing / frame pacing | not measurable (no hooks) | Gameplay 3e4650d fix; CAMSYNC 144 Hz 0.0003° vs 1.28° |
| health regen | not implemented | Gameplay 391b7f0 (CONFIRMED RE I3) |
| kill feed / HUD presentation | WAITING | Gameplay `killFeed` events; Rendering Canvas HUD layer |
| boost-state flicker, ramp contact | 16 hard-stop candidates (10 at one spot) | Gameplay 2c3854d |
| rapid-transform fall-throughs, CHAOS 10894 | 2 + 1 | still present on Gameplay 3cf84d8 |

## Pairwise conflicts (lane heads, 03:56)
- frontend + gameplay: STATUS.md, `src/game/World.cpp`, `src/game/World.h`.
- frontend + systems: FIDELITY.md, STATUS.md, `src/core/Application.cpp`, `src/game/World.cpp`.
- gameplay + rendering: `src/assets/SkinnedModel.*`, `Application.cpp`, `World.cpp`, `src/render/Renderer.h`.
- gameplay + systems: STATUS.md, `Application.cpp`, `PlayerController.h`, `World.cpp`.
- rendering + systems: FIDELITY.md, `src/game/VehicleFx.cpp`.

The full report is `LANES-0356.md`; rerun `tools/fidelity/lane-watch.ps1 -Fetch` for the current heads.
