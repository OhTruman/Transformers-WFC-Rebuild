# Living-map validation: MP_IAC_Streets runtime presence and behaviour

This is a narrow pass that measures runtime presence and behaviour; it does not rediscover the map. No product code was modified. Authored truth comes from AssetTools a23c675, read through the ExtractedAssets slice and the manifests: `validate_m04.py` runs read-only, 15,913 checks, 0 failures.

## Product states tested

| state | what | why |
|---|---|---|
| **P1, merge candidate** | `integration/milestone-03` 356c352 + `agents/systems` 8dcb861. Composed with `git merge-tree` (no refs touched): the product code auto-merges, and only `STATUS.md` (documentation) conflicts | Integration has merged nothing since 356c352. This is the next merged state that exists without resolving product code |
| **Gameplay preview** | `agents/gameplay` 808aacd as-is (Pass 17/18: MapState movers / mode state, boost steering) | It conflicts with 356c352 in `src/game/World.cpp`. Hunk 1 is the light-visibility line test (grid `segmentHit` on the pawn world vs a segmented test on the new weapon world); hunk 2 is a hitch log vs `mapState_.tick`, both additive. It also conflicts with Systems in `Application.cpp`, `PlayerController.h` and `World.cpp`. Resolving these is integration's job. Only the **state layer** is judged here |
| baseline | 356c352 | before-images and A/B frame time |

Rendering has no new commit. Nothing in any tree draws mover poses, mode visibility, pickups, totems or the destructible yet.

## Classification

| item | class | evidence |
|---|---|---|
| 70 ambient emitter records | **PRESENT AND CORRECT** | 70/70 attributed to runtime voices over 16 runs (40 point / 13 line / 17 volume) |
| map-start ambient | **PRESENT AND CORRECT** | 50 of 70 active at all 7 starts, exactly Systems' authored per-cue instance limits (30 line/volume + point cues 13→5, 12→5, 8→5, 5→3) |
| line / volume placement | **PRESENT AND CORRECT** | 41,525 sampled voice positions (every 50th update of 2.0 M), 0 off the authored segment or box |
| 11 timed one-shot pools | **PRESENT AND CORRECT** (10/11 exercised) | 148 one-shots: all from the current zone's pool, all at the authored 20 m. `ENTER_AUTO_ROOM_01` / `PP_MEDIUM_ROOMS` was only crossed by a vehicle, so it is not exercised |
| reverb-zone transitions | **PRESENT AND CORRECT** | 9/9 zones entered, 9/9 zone presets applied (the 10th preset, TRAIN_DEPOT, has no zone). 24/24 FFA starts enter their authored zone |
| audio leak / duplicates | **PRESENT AND CORRECT** | live voices flat in every run (for example 64→65 over 40 s). 0 identical launches. Real mixer 66–96 voices (96-channel cap), 0 dropped, 42 priority steals in 60 s, 0.47 ms/block |
| pickup sound triggers | **PRESENT AND CORRECT** | two real takes on the routes: each plays `HEALTH_PU_AMMO` once (no repeat within 0.5 s). Gameplay's PICKUPTEST shows one event per take with the right sound per class |
| pickup state (available / taken / respawn) | **PRESENT AND CORRECT** (state) | PICKUPTEST: respawn after 30.02 / 60.02 / 119.99 s (authored 30/60/120). Mesh, effect and beam flags toggle as authored |
| pickup visuals | **MISSING** | no code draws the crate mesh, beam or effects. Captures at an ammo crate and a health pickup show empty floor. Correction: the M03 map audit's "pickups PRESENT + RENDERED" rested on the placement log only and was wrong |
| 8 steam emitters | **PRESENT AND CORRECT** (density UNKNOWN) | Systems LevelFx 8/8; capture motion 14.7. Live particles 23–38, steady over every 40–60 s run (no leak) |
| map particle lifetime / leak | **PRESENT AND CORRECT** | level-FX particles flat (first-third mean about 29, last-third about 30, all 16 runs) |
| rotating domes | **PRESENT BUT WRONG** | state (preview): the world delta after 883.77 s is a yaw of 293.2°, against 293.3° for the authored 2730 UU/s (14.996°/s). Drawn: frozen on P1 (footprint change 0.045, luma 0.33). The preview also rotates the domes' **collision**, so a frozen mesh would get rotating collision |
| SkyBeam animation | **PRESENT BUT WRONG** | state (preview): non-identity Matinee deltas on all 3 actors (9.0022 s loop). Drawn: frozen on P1 (deco footprint 0.000, cone 0.058) |
| domination totems, animation | **NOT TESTABLE IN CURRENT MODE** | DM: hidden (Gameplay RE 0ab03b2; idle animClock runs). In DOM they would be visible, Active, with a marker, but nothing draws a totem (skeletal prop) → **MISSING** when DOM is played |
| totem mode visibility | **PRESENT AND CORRECT** (state) | MODETEST: visible only in DOM |
| objective / base mode visibility | **PRESENT AND CORRECT** in DM; **NOT TESTABLE** in CTF/EXT | DM: hidden (P1 captures: coverage ≤0.005 vs 0.017–0.018 with `WFC_SHOWHIDDEN`). Preview MODETEST matches authored `mode_dependent_visibility` exactly (CTF + EXT unhide all 4). No renderer path reads MapState, so CTF/EXT would still draw them hidden |
| destructible wall | state **PRESENT AND CORRECT**, visual **MISSING** | placed at the authored UE (896, 89968, −352); damage 20→5 → state 0→1→2, settled 10.00 s (authored 10). The mesh is loaded for bounds only and never drawn. It is about 900 m outside the play space, so it is not player-visible |
| frame time | **no measurable impact** | same-session Release A/B, 356c352 → P1 (ms): idle 4.30→4.25, movement 1.13→1.43, firing 4.72→4.42, sustained 4.35→4.50, hover 2.44→2.36, boost 2.48→2.53, heavy FX 2.35→2.49, ambient-active 4.12→3.56. All within noise. Absolute values are above my M03 numbers on both trees alike (machine state) |
| UNKNOWN | steam density vs authored spawn rate; whether frozen domes hold their authored rest pose; mover collision vs visuals once Rendering lands | |

Hidden mode-specific actors are not counted as missing.

## Handoffs
- **Rendering:**
  - draw the `MapState::movers()` world deltas (domes, SkyBeam) and `modeVisibleActors()` / objective visibility;
  - draw the pickup crate mesh, beam and effects from `PickupFactory` state;
  - draw the domination totems (DOM) and the destructible mesh.
- **Integration:**
  - resolve the Gameplay↔integration `World.cpp` conflict; hunk 1 needs the light-visibility collision decision from the owners;
  - resolve the Gameplay↔Systems conflicts.
  - Note: until Rendering draws the movers, Gameplay's rotating dome collision will not match the frozen mesh.
- **Gameplay:** the test dummy is now behind `WFC_TESTDUMMY` (808aacd). It still appears in P1 until merged.
- **Systems:** none. All audio checks pass.

## Tools
- `m04-world-audio.ps1`: spy plus AMB analysis (emitters, placement, pools, zones, leaks, duplicates, pickup sounds).
- `m04-captures.ps1`: fixed-camera captures. Gives skip-mask coverage, `WFC_SHOWHIDDEN` control, luma change and footprint IoU for geometric motion.
- `m04-ambient.ps1`: zone at every FFA start, plus routes.
- `m04_assettools.py`: `validate_m04.py` run read-only.
- `perf-release.ps1`: adds the `heavy_fx` / `ambient_active` scenarios.

## Evidence (this folder)
- `audio/`
- `captures_p1/`: sheets and CSV, including `captures_movers_footprint.csv`
- `captures_baseline_356c352/`
- `gameplay_preview_808aacd/`: MODETEST, TRAVERSE
- `pickups/pickuptest_p1.txt`
- `perf/`
