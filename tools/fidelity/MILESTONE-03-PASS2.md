# Fidelity validation: Milestone 03, pass 2 (measurement infrastructure)

**Measured tree:** `agents/experimental` = `origin/integration/milestone-02` (e8036f6), unless a section
says "Systems checkpoint". That one is `origin/agents/systems` d86b863, built in isolation with
`ab.ps1 -Measure` (read-only: no merge, no worktree touched).

**No product code changed.** Everything below comes from measurement builds of unmodified sources or
from the windowless harness.

## Infrastructure added
- **Deterministic real game (`wfc_rebuild_observe`).**
  - The exe's only wall clock (`core::nowSeconds`) is swapped for a lockstep clock, so every frame is
    exactly one 60 Hz step.
  - Two identical transform runs: 200/200 frame-log lines identical, audio logs identical.
  - Same build adds a front-buffer frame grabber, an exact per-frame record of the debug-overlay state,
    and the audio recorder. The recorder now models `isPlaying()`, which Systems' new cue ownership
    depends on; without it the old spy would have reported every cue as dying after one tick.
- **Exact call counter (`wfc_rebuild_count`).** `-finstrument-functions` on the counted subsystems. The
  watched functions are resolved by name, so the counters follow code moves.
  - Counting starts at the first frame; timestamps use rdtsc.
  - Counts are exact. Times are not usable: about 400 ms per instrumented idle frame.
- **Harness access** (`Access.cpp`, harness only):
  - World's collision, so probes run the PRODUCTION `PlayerController::applyToPawn` path on synthetic
    terrain.
  - The Character's drawn skinned pose, for pose-freeze and silhouette analysis.
  - The trace gained camera position/target, drawn model, pose delta, robust bounds and far vertices.
- **Suites and scripts:** `transform_analyzer`, `vehicle_profiles`, `fine_aim_probe`;
  `transform-capture.ps1`, `audio-attach.ps1` (rewritten), `vehicle-visual.ps1`, `map-audit.ps1`,
  `perf-counters.ps1`, `perf-report.ps1` (rebased), `m03-gate.ps1`, `ab.ps1 -Measure`.

## Harness counts (milestone-02, `--map`)
| | PASS | FAIL | KNOWN | INFO |
|---|---|---|---|---|
| milestone-02 + pass-2 suites | 262 | 0 | 36 | 329 |
| Systems checkpoint d86b863 (same harness) | 265 | 0 | 33 | 329 |
| Rendering checkpoint 66bea49 (same harness) | 262 | 0 | 36 | 329 |

Systems' 3 FIXED:
- `trace_cost.hitscan_trace_under_1ms`: a 300 m `segmentHit` ray goes from **31.2 ms to 0.11 ms**.
- `fine_aim_presentation.start_end_audio`
- `fine_aim_probe.start_end_audio_wired`

## 1. Transformation analyzer
Simulation side (`transform_analyzer`, production collision path, both directions, standing/moving):

| | R→V | V→R |
|---|---|---|
| authored both-visible window | 0.396–0.880 s | 0.098–0.663 s |
| incoming mesh first drawn | **1.017 s** (VEH clip normalized 0.517: pops in mid-animation) | **0.583 s** |
| outgoing mesh last drawn | 1.017 s (should hide at 0.880) | 0.583 s (should hide at 0.663) |
| steps inside the window with one mesh | all (KNOWN Gameplay, CONFIRMED window) | all |
| silhouette jump at the switch (robust bounds) | 2.05 m | 38.6 m (see far vertices) |
| pose freeze (drawn pose identical while the clip moves) | none; 9 authored-hold steps at the end of `Transform_ToVehicle_VEH` are excluded | none |
| far-flung vertices (> 10 m from the root) | 0 | **17 steps**: `Transform_ToRobot_VEH` parks vehicle parts ~39.5 m below the pawn. The original's two-mesh display may hide them; human check |
| usable weapon without a visible muzzle | 0 | 5 steps (KNOWN Gameplay, milestone-02 regression) |
| root / heading discontinuity, camera cut (> 1 m/step) | none | none |
| camera offset swing during the fold | 2.83 m (shoulder offset blends to the vehicle camera) | 2.83 m |

Real exe (`transform-capture.ps1`, lockstep grabs every 2nd frame):
- Debug geometry was never drawn (DebugFlags off in every frame). The wireframe box from the earlier
  wall-clock stills did not reproduce.
- No camera pops.
- Visual pop at the R→V switch: image change **3.4×** (side camera) / **4.8×** (chase camera) the fold
  median.
- V→R switch: 1.2× / 0.9× (the mesh change is hidden in motion).

## 2. Vehicle profiles (why it may feel weightless)
Production controller path on synthetic terrain; every value classed.

**CONFIRMED ORIGINAL (all match):**
- hover height 1.85 m
- boost 30 m/s
- dash 30 m/s
- nitro 45 m/s
- heading follows the camera (0° error, 0° lag at 90°/s)

**Weightless indicators:**
- **Terrain snapping.** The hull climbs a 0.5 m step and 0.25 m bumps within ONE frame (`max_vertical_step` =
  feature height, vertical speed 0).
- **No suspension.** After a 1 m drop: 0 oscillation crossings, settled in 1 frame. Off a 4 m ledge:
  0.63 s airtime, lands at −12.3 m/s, back at ride height in 1 frame.
- **No body pitch/roll** (structural: the mesh rotates by yaw only).
- **Symmetric response.** HOVER accel t90 0.45 s, release-to-stop 0.47 s.
- **Instant re-aim with crabbing.** The heading snaps to the camera while velocity lags:
  - HOVER: max slip 88°; velocity swings 81° in 0.62 s.
  - DASH: velocity is re-aimed within 3 frames (a dash can be steered instantly).
  - NITRO: the velocity never swings 81° within 2 s (steer ×0.3, CONFIRMED).
- **Dead jump.** Launch 0 m/s (authored JumpLinearSpeed 1200 UU/s = HIGH CONFIDENCE).

**Defect found:** in Driving (Boost/Nitro) the truck keeps its height and drives THROUGH a 0.5 m
raised surface (150 steps below the surface; KNOWN Gameplay). Hover climbs the same step instantly.

The weight verdict remains a HUMAN CHECK.

## 3. Vehicle visual A/B
`vehicle-visual.ps1`: 20 stills, all captured in the intended state. Motion states are filmed on the
open-run spawn: hover 15, boost 30 (Driving), dash 30 (dash active), nitro 42.9 m/s (nitro active).

**Sidecar** (`materials.json`, from the compiled material records):
- Body slot `RB_OptimusPrime_Cust2_Mat_INST` → master `CHR_Transformer_NormSpec_Cust_E_Mat`, compiled.
  - Textures: normal `VH_Optimus_NORM`; diffuse `VH_Optimus_GrungeClr`; emissive/mask
    `VH_Optimus_CustAB_AuxGlow` + `VH_Optimus_SpecPwr_DifLum_EnrGlow`; cubemap `CHR_Metals_CUBEMAP3D`;
    fractal maps.
  - Switches: DiffuseReflection, EmissiveReflection, SpecPower_S_Curve, ReconstructedNormal,
    **UseAutobotEnergonColor**.
  - Runtime parameters `Cust_Color_A`, `Cust_COLOR_B`, `EnergonColor` exist, but **no game code pushes
    them** (`setCharacterColors` is never called on milestone-02), so the compiled defaults render.
- Interior slot `InteriorAlt_Energon_MAT_INST` → `InteriorAlt_Energon_MAT`, compiled.

**Variants:**
- The legacy GL1 path and the AssetTools glTF bakes (`WFC_GLTFMATERIALS`) are captured at the same
  frame and camera.
- The legacy path loads a stand-in map, so its front-¾ camera sits inside a wall: compare the vehicle
  only.

**Transform midpoint vs directly spawned vehicle (milestone-02):** the transformed vehicle is mottled
pink/grey; the spawned vehicle is clean. Rendering bb94e40 reports the fix (dynamic program cache keyed
by material address).

**Dark/bright areas:** the darkest and brightest of 8 probed FFA spawns (mean luminance 15.3 vs 37.2).

## 4. Fine Aim
**Probe results:**
- PASS: state, RMB toggle, FOV 80→45, movement ×0.5, look ×0.5, reload interrupt + resume, transform
  cancel.
- Entry t90 0.083 s (SmoothTime 0.1), exit t90 0.25 s (0.4).
- Camera distance unchanged (8.49 m).
- Spread ×0.5 is a code constant (World is not linked in the harness).

**Lateral shift:** 0.00 m in the harness. UNRESOLVED; see
[FINE-AIM-EVIDENCE-REQUEST.md](FINE-AIM-EVIDENCE-REQUEST.md). The authored X 150→−50 UU is either a 2 m
left shift or a 2 m dolly-in depending on axis semantics. Nothing is asserted.

**Start/end audio:** KNOWN on milestone-02; wired on the Systems checkpoint (auto PASS).

**Reticle:** no HUD on milestone-02. Rendering M03 adds the Ion Blaster crosshair.

## 5. Audio attachment (milestone-02)
11 scenarios. The offending list is `results/m03-baseline/offending_cues.txt`: 19 wave/cue entries.

**Fixed-position:**
- `EVENT_IACON_BRIDGE_TRANSFORM_GEARS` (generic `playSfx` transform placeholder): pawn 29 m away.

**Never-updated voices:**
- `TRANS_ION_UNEQUIP_SERVOS` (IDLE/RELOAD cues)
- `GUN_RIFLE_BOLT_ACTION`
- boost/nitro start layers: `WHSH_BOOST_FLARE_LR_03`, `SYNTH_ENGINE_ACCEL`, `SYNTH_AIR_RELEASE_04/08`,
  `MECH_TRANS_RAM_ACTIVATE`
- `AUTO_BOOST_PEELOUT_TRUCK`

**Updated, but not following the pawn:**
- `AUTO_OPTIMUS_BOOST_START_LR`: updates stop mid-wave, 54 m.
- `VEH_TRUCK_RAM_ALERT` (`MECH_ORB_ARM_04`): 120 updates to a non-following position, 16 m.

**Presence (KNOWN Systems on milestone-02):** robot footsteps, `BL_TRANSFORM.OPTIMUS_BOT2VEH`, robot
landing, fine-aim start/end.

**Systems checkpoint d86b863 (isolated build, same harness):**
- **0 pawn-owned sounds stop following** in all 11 scenarios (milestone-02: 19 wave/cue entries).
- All presence checks pass: footsteps, transform cue, landing, fine-aim start/end.
- 100 PASS / 0 FAIL / 0 KNOWN.
- The Streets ambient bed plays (neon buzz, furnace, Kaon smelt, TV, steam loops), so the map audit's
  `ambient_audio` category flips after that merge.

## 6. Map audit (milestone-02 render data; Rendering's map_audit.json is consumed when present)
| category | result |
|---|---|
| BSP, decals 25, lights, lightmaps, fog, post-process | PRESENT + RENDERED |
| props | 1782 PRESENT + RENDERED; **124 PRESENT + WRONG MATERIAL** (section materials missing from milestone-02's compiled set: e.g. `stains_01_CLR_MatINST_BAT` ×30, `TrainTrack_MATINST` ×20, `ENV_ForceFieldNoDestortionLessGlow` ×18; Rendering 27a677b recovers section materials) |
| **PrefabInstance 16** | **resolved**: 15 are containers whose 34 member meshes are all placed and rendered (StackedCratesSmall/_02, StackedCrates3HighSmall, centerPlanterSmall, GroundVentWithBSP); the 16th (`PrefabInstance_4340`, SteamVentFX) only holds a level emitter. "16 missing prefabs" is retired |
| movers 12 | PRESENT + WRONG EFFECT: drawn static. 4× PHYS_Rotating DecoSphere/DomeDeco at 15°/s, 8 Matinee/Kismet InterpActors (light-beam cones, repair nodes, objective bases) |
| level emitters 8 | NOT INSTANTIATED (Steam_Sm_FX incl. the prefab's) |
| pickup FX 37, pickup factories (14 ammo, 9 health, 1 overshield, 1 bomb + 2 flag objectives) | NOT INSTANTIATED: graybox placeholder pickups near the spawn instead |
| destructible 1 | NOT INSTANTIATED: `TnStaticDestructibleActor_14465`, blueprint `DES_IAC_WallPanelSignDSYS_p.WallPanelSign`; meshes exist (`content/DES_IAC_WallPanelSign_p`), but the DSYS blueprint is not exported (AssetTools request) |
| ambient audio 70 | NOT INSTANTIATED (40 point, 13 line, 17 volume; no ambient wave played at runtime) |
| reflections | UNKNOWN (Rendering's permutation check covers it per material) |

## 7. Performance rebase
**Categories:**
- Costs: collision, visibility, lighting, skinning, particles, audio, render, gameplay.
- Origins: hitscan vs camera-aim ray (now separate), shell/magazine meshes (matched by function, not
  line numbers), vehicle FX, movement.

**Milestone-02 firing frame, re-attributed:**
- collision 268 ms = hitscan 132 + camera-aim 135
- skinning 12.4 ms
- visibility 12.0 ms
- render 3 ms
- audio 2.3 ms

**Steady state** (idle 6.9 ms): CPU skinning is the largest cost (4.5–4.8 ms), owned by Rendering +
Gameplay.

**Exact counters, idle, per frame:** 2 `skinPose`, 2 dynamic draws, 2 vertex builds, 1 mesh draw,
2 `segmentHit`, about 0 light-env/visibility rays.

### Re-measured on the Systems checkpoint d86b863 (profile + exact counters, isolated build)
| phase | milestone-02 | Systems d86b863 |
|---|---|---|
| idle | 6.92 ms | 6.33 ms |
| held fire, firing frames (mag 1, 2–3 s) | 298.7 ms | **11.2 ms** (p95 13.5) |
| held fire, whole run | 61.7 ms | 11.7 ms |
| per-shot accumulation | −0.65 ms/shot (r −0.10) | −0.19 ms/shot (r −0.09): none |

This independently confirms Systems' 10–11 ms claim.

**Remaining firing-frame cost (11.2 ms):**
| cost | ms | note |
|---|---|---|
| CPU skinning | 4.56 | the same 4.3 ms is present at idle |
| visibility rays | 3.92 | origin shell/magazine meshes 3.82 |
| render | 1.78 | |
| audio | 0.62 | |
| collision | 0.14 | |

**Exact per-frame counters while firing (lockstep):**
- 57.2 light-visibility rays (= 229 per shot at 0.25 shots/frame)
- 59.9 `segmentHit`
- 6.65 light-environment computations
- 14.0 mesh draws (live shells/magazines)
- 2 CPU skins
- Idle: 0 visibility rays. Walking: 3.3.

**First-use hitches:**
- First 5 firing frames after load: 190 ms (156 ms first-use texture decode + program builds:
  `Pipeline::programFor` → `texture` → `decodeImage`) + 17 ms audio.
- First second of held fire: 24.7 ms (12 ms first-use, 5 ms audio).

**Owners:**
- Rendering: per-shell light environments + visibility rays; prewarm programs/textures at load; CPU
  skinning (with Gameplay).
- Systems: first-play audio cost.

## Merge gate
`tools/fidelity/m03-gate.ps1` (see README):
- Builds, then runs every suite: harness, transform-capture, audio-attach, vehicle-visual, map-audit,
  perf profile + report, counters.
- Compares against `results/m03-baseline` (milestone-02 measurements, written by the same gate with
  `-WriteBaseline`).
- Writes `M03-GATE.md` with PASS/FAIL/KNOWN/INFO and PERFORMANCE / VISUAL / AUDIO ATTACHMENT /
  TRANSFORMATION / VEHICLE / MAP COMPLETENESS / FINE AIM regressions, improvements, perf phases, FAILs
  and the HUMAN CHECK list.
- Exits 1 on any FAIL. No manual edits are needed between merges: cue names come from the merged
  tree's `SoundCues.inc`, watched functions are resolved by name, and Rendering's `map_audit.json` is
  consumed when present.

## AssetTools requests
1. **`DES_IAC_WallPanelSignDSYS_p`:** the WallPanelSign destructible blueprint (HmDestructible piece
   `HmDestructiblePiece_11226`): piece → mesh (Base / Base_Aqua / Base_Blue), piece transform, state
   materials (`WallPanelSign_MATINST` vs `_EMISSOFF_MATINST`), damage states. Do NOT re-extract the map.
2. **None for prefabs:** members are already exported with prefab tags. `map.json`'s
   `unhandled_actor_classes.PrefabInstance` can be documented as "containers; members placed".

## ReVa requests
1. `TnScreenSpaceOffsetByPitchCameraBehavior` offset application (FINE-AIM-EVIDENCE-REQUEST.md).
2. Hover-truck suspension/terrain following: the `HoverTruck_Physics` hover spring (stiffness, damping,
   ride-height correction rate). Also whether the original body pitches/rolls on slopes, bumps and
   acceleration. That is the main open weight question.
3. Vehicle jump: does `JumpLinearSpeed` 1200 apply in Hovering and in Driving, and what is the airborne
   gravity scale (RBPhysicsGravityScaling 0.66 is in the constants)?
4. Dash steering: is the dash vector locked at activation (`forward only`) or re-aimed with the
   camera?

## Handoffs (evidence only; Experimental implements none of these)
**Gameplay**
- Draw both meshes through the authored overlap windows (R→V 0.396–0.880 s, V→R 0.098–0.663 s). The
  rebuild switches at 1.017 s / 0.583 s, and the switch is a visible pop (3.4–4.8× the fold's median
  frame change).
- Keep the weapon unusable until the robot mesh and gun are drawn (5 steps of V→R, milestone-02
  regression).
- Driving (Boost/Nitro) passes THROUGH a 0.5 m raised surface at constant height
  (`vehicle_profiles.step.{BOOST,NITRO}.never_below_surface`).
- Vehicle jump launches at 0 m/s.
- Hover snaps to terrain within one frame and has no spring and no pitch/roll. Pending ReVa
  suspension data and the human weight check.
- Movers (12) are drawn static: 4 PHYS_Rotating at 15°/s plus 8 Matinee-driven.
- Authored pickup factories (14 ammo, 9 health, 1 overshield, 3 objectives) are replaced by graybox
  placeholders.
- `setCharacterColors` (TnCharacterApplier) is never called, so EnergonColor and Cust A/B are
  compiled defaults (shared with Rendering).
- Fine-aim lateral offset semantics: pending the evidence request.

**Rendering**
- Shell/magazine mesh particles: about 229 light-visibility rays per shot and 6.65 light envs per frame
  while firing (3.9 ms of the 11.2 ms post-fix frame).
- Prewarm programs/textures at load: the first firing frames hitch 190 ms (156 ms `programFor` →
  `texture` → `decodeImage`).
- CPU skinning 4.3–4.6 ms is the largest steady cost (with Gameplay).
- 124 props on non-compiled section materials in milestone-02's render data (expected to flip with
  27a677b).
- Hover FX look; the far-flung vehicle vertices (39.5 m below the pawn) during `Transform_ToRobot_VEH`
  when shown as one mesh.

**Systems**
- Level emitters (8 incl. the SteamVentFX prefab member) and pickup FX (37) are not spawned.
- First-play audio cost: 5–17 ms in the first firing frames.
- Milestone-02's 19 offending cues are all resolved on d86b863 (verified); keep them so with the gate.

## Known limitations
- **Lockstep exe timing.** Simulation, frames, audio and grabs are deterministic, but the renderer's
  shader clock (`uTime`: animated signs, scrolling textures) still runs on wall time.
- **Grabber.** It reads the front buffer: keep no other window over the game window during captures
  (the gate runs suites sequentially). The final frame of a run is captured via `WFC_SHOT`.
- **Call counter.** Only counts are valid (about 400 ms per instrumented frame). Pre-fix collision
  makes firing frames impractical to count, so the gate caps the run (`-CounterTimeoutSec`, INFO).
- **Audio harness.** It sees waves, not cue instances: a wave shared by several cues is reported with
  every candidate cue. The owner is the pawn (start within 6 m); IMPT_* impacts are world-anchored by
  design.
- **Harness world collision.** Only the windowless harness runs on synthetic terrain; the real-map
  feel needs the human check.
- **Visual comparison.** Images are compared with the baseline thumbnails by mean luminance; a change
  means "review", not "wrong". The legacy-path idle stills are not comparable (stand-in map).
- **Map audit.** Without Rendering's `map_audit.json`, material correctness comes from compiled-set
  membership only. "Animated signs" (panner/time materials) are not separately classified.
- **Fine-aim reticle.** Not asserted (asset relationship unrecovered); Rendering's crosshair is a human
  check.

## Merge gate validated on three trees
| tree | gate result | highlights |
|---|---|---|
| milestone-02 (this branch) → **baseline** | 345 PASS / 0 FAIL / 71 KNOWN / 388 INFO | self-comparison: 0 regressions (`results/m03-baseline/GATE-RUN.md`) |
| agents/systems d86b863 (suites run individually) | harness 265/0/33; audio 100/0/0 | sustained fire 11.2 ms; 0 cues stop following; all presence checks PASS; ambient bed plays |
| agents/rendering 66bea49 (full gate in its export) | 344 PASS / 0 FAIL / 71 KNOWN | WRONG MATERIAL props 124 → 0, movers 12 → 3, held fire 51.5 → 38.1 ms, 25 visual changes for review (`results/xbranch-rendering-66bea49`) |

Gate defects found and fixed while validating:
- `$R`/`$r` case-insensitive collision
- `.Add("..." -f …)` argument splitting
- PowerShell 5.1 `ExitCode` loss
- truncated counter lines after a timeout kill
- `verify_permutations.py` argument order
- Python stderr treated as fatal
- toolchain lookup in exports
- pinned scene spawns, so lighting changes cannot change which place is filmed
