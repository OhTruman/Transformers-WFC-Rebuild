# Milestone 03 checkpoint: preserved evidence

Measured tree: `origin/integration/milestone-02` (e8036f6). Tooling commit: c44c418. Analysis and
owner handoffs: [../../MILESTONE-03.md](../../MILESTONE-03.md). No product code was changed.

## Counts
| Run | PASS | FAIL | KNOWN | INFO | SKIP |
|---|---|---|---|---|---|
| `wfc_fidelity --map` on milestone-02 (`harness/report_milestone-02.json`) | 223 | 0 | 27 | 176 | 0 |
| Same harness on milestone-01 (`harness/report_milestone-01.json`) | 157 | 2 | 79 | 177 | 1 |
| `audio-attach.ps1` (`audio/report.json`) | 24 | 0 | 20 | 1 | 0 |

`harness/diff_m01_vs_m02.txt` is the full `diff-reports.ps1` output.

## New regression (milestone-01 → milestone-02)
`transform_timeline.v2r_{stand,moving}.no_usable_weapon_without_muzzle`: PASS → **KNOWN (Gameplay)**.
- In vehicle→robot, `weaponUsable()` turns true at fold progress ≈0.43 (restore at 25% + 0.2 s equip).
- The robot mesh and gun are drawn from progress 0.50.
- For 5 steps (83 ms) a shot can leave a gun that isn't visible.

## Contents
| Path | What |
|---|---|
| `perf/report.txt`, `perf/attribution.csv` | cost × origin per phase (idle, walk, fine aim, burst firing / 0–1 s / 1–3 s / settled after, held fire) |
| `perf/hot_functions.txt` | inclusive functions (`segmentHit` 85%) and `segmentHit` callers |
| `perf/<scenario>.frames.csv` | per-frame dt, ammo, reload, fine aim, speed, particles, mesh particles, FOV |
| `audio/<scenario>.audiospy.txt` | full audio event log (`L` load, `A` playAt, `V` voice, `U` update, `S` stop, `H` listener) |
| `audio/findings.txt` | the 20 sounds left at their trigger point |
| `stills/transform_r2v.png`, `transform_v2r.png` | transform contact sheets, fixed side camera, labelled form/clip/time |
| `stills/vehicle_materials.png` | vehicle front/side/rear/¾, WFC vs legacy, plus the robot |
| `harness/run_milestone-02.txt` | console output of the harness run |

Raw 1 kHz stack samples aren't kept (≈10 MB, regenerate with `perf-profile.ps1`).

## Key numbers
- **Sustained fire:** ≈299 ms per firing frame (gameplay traces 268 ms), constant and not
  accumulating (−0.65 ms/shot, r −0.10).
  - Idle 6.9 / walk 5.9 / fine aim 5.8 ms.
  - 0–1 s after the last shot 12.0 ms, after 1–3 s 7.6 ms.
  - Owner: **Gameplay** (`CollisionWorld::segmentHit`, 32.3 ms per 300 m ray, two rays per shot).
- **Vehicle:**

  | Probe | Value |
  |---|---|
  | accel t90 | 0.45 s |
  | decel to < 1 m/s | 0.47 s |
  | strafe t90 | 0.45 s |
  | lateral retention after 0.5 s | 4.0 m/s |
  | heading error | 0° |
  | ride height | 1.85 m |
  | hover after a 1 m drop | snaps, 0 crossings, 0.32 s |
  | roll/pitch | none |
  | jump | 0 m/s, KNOWN |
  | reverse | 0.95 s |
  | boost | 30 m/s |
  | dash | 30 m/s, 0.6 s, cooldown OK |
  | nitro | 45 m/s, 3.0 s, cooldown 8.0 s |

- **Map:**
  - Missing: 16 PrefabInstances and 1 static destructible (AssetTools/RE), 8 level emitters and 70
    ambient-sound actors (Systems).
  - Complete: decals 25/25, props 1952, lights 268, 3 sublevels, BSP, fog.

## Human-validation checklist
- [ ] Vehicle "weight": the hover snaps (no spring), there is no roll/pitch, accel and decel are both
      0.45 s, and jump does nothing. Do these explain the playtest complaint?
- [ ] Transform handoff pop on the contact sheets: a single-mesh switch at progress 0.50 (the
      original animates both meshes).
- [ ] A shot during vehicle→robot progress 0.43–0.50: is a gunless shot noticeable?
- [ ] The vehicle reached by transforming looks mottled compared with a directly spawned vehicle.
- [ ] The white wireframe box drawn around the pawn mid-fold (r2v p045).
- [ ] Energon hue: WFC red (compiled default, `setCharacterColors` never called) vs legacy and bakes
      blue. The correct Optimus value is UNKNOWN.
- [ ] Hover/boost FX vs the original: WFC draws pale rings only, legacy adds orange rings and a
      thruster plume.
- [ ] Sounds left behind (transform gears 25.8 m, boost peel-out 35.4 m, engine accel 27.7 m): are
      they audible in play?
- [ ] Fine aim camera: is the original's sideways shift large (authored row ≈2 m vs measured 0.05 m)?
- [ ] Reticle/HUD for fine aim: the original asset relationship is not recovered, so nothing is asserted.
