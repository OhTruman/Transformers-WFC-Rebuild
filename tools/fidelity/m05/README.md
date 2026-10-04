# Milestone 05 validation: frontend + TDM + map lifecycle

**Final target rule.** The verdict is run only against the Milestone 05 integration commit pushed by the Integration
lane. Owner-branch builds are used for preparation and debugging of the tools only: no PASS/FAIL against stale
product code.

```
.\tools\fidelity\m05-final.ps1 -Ref origin/integration/milestone-05 -Build -LaneRendering <Rendering lane playtest dir>
```

This builds the exact commit in isolation and runs every suite below, then writes `M05-FINAL.md`. That document is
the hand-off: commit, exe paths, per-area results with owners, every FAIL with evidence, KNOWN, and the HUMAN-CHECK
framing.

`-Quick` runs a 10-start subset of the stress suites with 4 cycles. `-Only gate,stress,...` runs selected suites.

## Suites
| suite | script | what it proves |
|---|---|---|
| build | `m05/build-target.ps1` | isolated export of one commit: Debug + Release (all targets) + render data; `M05_TARGET.txt` records the sha |
| lifecycle gate | `m05-e2e-gate.ps1` | R1 cold boot with logos / intro → **keys only** through the shipped menus → TDM → Streets → countdown → spawn → play → pause → quit; R2 second launch; R3 (`-Full`) 10-minute match to the time limit → end screen → lobby → second match → frontend; R4 repeated frontend ↔ Streets cycles with memory / GL / audio accounting; R5 UI selects Gorge (map ownership); R6 UI selects DM (mode ownership); S every owner's self-test on this exe |
| transform stress | `transform-stress.ps1 -Scenarios all` | 84 starts × 15 scenarios: stationary / walk / drive / boost / nitro / boost + turn (both ways) / airborne / long boosts / late nitro / rapid repeated transforms. Two detectors: the press-floor crossing, and a general "fell more than 1 m below the last grounded floor while that floor is overhead". Tags: thin floor, wall, incline / decline, low ceiling |
| vehicle collision | `vehicle-collision.ps1` | 84 starts × 8 drives (drive, boost, boost ± turn, nitro, boost cycles, reverse, strafe). `collision_sweep.py` sweeps every frame step at 0.8 / 1.2 / 1.6 m against the authored wall faces and names the actor passed through |
| visual | `playtest-regressions.ps1` + `m05/visual-compare.ps1` | the human-reported smoke / fog / steam / glass / ramp viewpoints on the merged build, three ways (int-04 baseline, Rendering lane, merged): `fix_present` / `fix_lost` / `merge_diverged` |
| soak | `m05-e2e-gate.ps1 -Runs R4 -Cycles 20` | stabilisation of private memory, handles, threads, GL release counts, audio PCM / voices, UI movies and flow state; growth is attributed to an owner |

## Layers and statuses
Layers:
- DATA: authored / catalog / unit tests.
- STATE: the product trace.
- PRESENTED: product-side composed frames, `shot:`, judged in pixels.
- MATCH: Gameplay rules as executed in the frontend-launched match.
- AUDIO.
- OWNERSHIP: the UI's map and mode are what Gameplay, Rendering and Systems run.
- LIFETIME.
- SELFTEST: the owners' own tests re-run on the merged exe.

A pass in one layer never implies another. Statuses:
- PASS;
- FAIL: contradicts CONFIRMED/HIGH evidence, with a likely owner;
- KNOWN: documented gap or stale test, with owner;
- INFO;
- SKIP: hook or feature absent; the gate probes the exe for its `WFC_*` hooks first;
- HUMAN: `HUMAN-CHECK-M05.md`.

Every FAIL row names its owner, and R4 adds an attribution line for memory growth.

## Real input
- The menu path uses `WFC_FRONTEND_SCRIPT` `key:` steps: Flash key codes delivered to the shipped movies, whose own
  ActionScript makes the bridge calls.
- The key paths in `lib/M05.ps1` were verified on agents/frontend 08ef880:
  1. **Start** opens the main menu (Campaign focused). **Down** + **A** selects Multiplayer.
  2. In the party lobby, **Down** + **A** selects Private Match.
  3. The mode list opens on TDM (`EditGameMode(TDM)` fires on focus).
  4. **A** opens the host options, focused on Create Game.
  5. Down×3 reaches Time Limit, Left sets 10 minutes, Down×2 returns to Create Game.
  6. **A** on Create Game opens the game lobby, focused on Start Game. Down reaches Select Map; Left/Right wraps
     over the selectable maps.
- Bridge calls are used only where the movie itself makes that call and keys add nothing: `Game.QuitToMainMenu`
  from the pause menu, and the fast R4 cycles.

## Expectations
`m05/expectations.json` holds every value with its source and confidence:
- RE `MILESTONE05_FRONTEND_MATCH_BOOTSTRAP.md` and `MILESTONE05_FRONTEND_GAMEPLAY_BLOCKERS.md` (85e340c);
- AssetTools;
- Systems M06 measurements.

Values superseded by newer evidence are listed under `retired`, with the reason:
- `PlayerRestartDelay` 6.0 is dead config (E8);
- the host's map is not preselected after a match (C7 correction);
- the lobby team is not the match team offline (E2);
- "only Streets selectable" no longer holds (Gorge has runtime data).

## Preparation findings (owner branches; not a verdict)
- **agents/frontend 08ef880:**
  - The shipped menus render, and the key path works end to end.
  - Host-option defaults match BLK B4.
  - The lobby default map and selector wrap match BLK C3/C4.
  - The intro movies decode and play to the end.
  - The in-match frame is black with a "LOADING..." spinner while the trace says InGame with the HUD on. The gate's
    `PRESENTED.in_game_world_visible` exists to catch exactly this on the merged build.
- **agents/gameplay d541c78:**
  - `WFC_TDMTEST` 30/30; `WFC_MATCHTEST` reaches the time-limit tie and DM 20.
  - `WFC_XFORMTEST` 0/76 under the map.
  - `WFC_CHAOS`: 1 run under the map, 2 stuck, 7 inside props (123 starts).
  - The independent vehicle sweep finds 0 pass-throughs on 24 runs where int-04 a03d7f7 had 9.
  - The branch lacks `WFC_LOCKSTEP` (an int-04 hook), so its lockstep runs advance by wall time. The gate
    therefore probes hooks per exe.
- **int-04 a03d7f7 (baseline):** 127/756 transform fall-throughs (all vehicle→robot at speed); the same 9/24
  vehicle wall pass-throughs.

## Merge-conflict preview (for Integration; git merge-tree, no refs touched)
Pairwise previews of the 2026-10-03 heads:
- frontend 08ef880 (8 ahead of int-04);
- gameplay d541c78 (3 ahead; base d122ef4);
- systems 3d295ec (6 ahead);
- rendering 187e6e9 (4 ahead).

| pair | conflicting paths |
|---|---|
| frontend + gameplay | STATUS.md, src/game/World.cpp, src/game/World.h |
| frontend + systems | FIDELITY.md, STATUS.md, src/core/Application.cpp, src/game/World.cpp |
| frontend + rendering | FIDELITY.md |
| gameplay + systems | STATUS.md, src/core/Application.cpp, src/game/PlayerController.h, src/game/World.cpp |
| gameplay + rendering | src/assets/SkinnedModel.cpp/.h, src/core/Application.cpp, src/game/World.cpp, src/render/Renderer.h |
| systems + rendering | FIDELITY.md, src/game/VehicleFx.cpp |

The gate is built to catch where those resolutions most likely go wrong:
- **`Application.cpp` / `World.cpp`:** match launch from the frontend, the audio level lifecycle, the
  `WFC_LOCKSTEP` / `WFC_PRESSTRANSFORM_EVERY` hooks. Covered by `OWNERSHIP.*`, `pending_countdown`, the hook probe
  and the AUDIO rows.
- **`Renderer.h`:** the map unload contract. Covered by `LIFETIME.memory_stabilizes` and `gl_release_per_return`.
