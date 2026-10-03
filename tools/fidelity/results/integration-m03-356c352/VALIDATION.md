# Milestone 03 merged-build validation: integration/milestone-03 @ 356c352

Gameplay 0762f01 + Systems d6932dc + Rendering 19cd213. This was an independent validation by Experimental.

- **Source:** the merged commit was exported with `git archive 356c352` into `work/ab/m3int`, and this harness was overlaid onto it.
- **No product code was modified.**
- **Builds:** Debug in `work/ab/m3int/build`, plus the measure targets. Release in `work/ab/m3int/build-release`.
- **Gate header:** the gate reports `agents/experimental @ 425eb1a` because the export has no `.git` and the script reads the enclosing repo's HEAD. The tree actually tested is 356c352.

## Final gate (Experimental `m03-gate.ps1 -SkipBuild` on the merged tree)

**PASS 665 / FAIL 0 / KNOWN 21 / INFO 599 / SKIP 0.** All 10 steps completed. See `gate/M03-GATE.md`.

| suite | P / F / K / I / S |
|---|---|
| harness (Debug; Release identical) | 337 / 0 / 9 / 371 / 0 |
| transform capture | 15 / 0 / 1 / 12 / 0 |
| audio attachment + voice lifetime | 192 / 0 / 0 / 168 / 0 |
| vehicle visual | 20 / 0 / 1 / 0 / 0 |
| map audit | 16 / 0 / 6 / 5 / 0 |
| runtime probe (Debug and Release) | 31 / 0 / 4 / 10 / 0 |
| render self-tests (shadow 32, DLE 20) | 52 / 0 / 0 / 0 / 0 |
| perf counters | 2 / 0 / 0 / 18 / 0 |
| perf release | INFO only |

## 1. Stale spread tests retired; recovered model validated

- `spread_after_10` is now compared against an independent per-tick re-simulation of the RE d50e2a9 model.
- `spread_cap` asserts that the 0.18 clamp is never exceeded.
- **The old 0.18-after-a-magazine expectation was wrong.** Net bloom is +0.025/s, and one 50-round magazine peaks at 0.1667. Under the native model the cap needs about 4 s of continuous fire.

Checks that now PASS in Debug and Release:
- **Bloom:** 0.08 → 0.18 at +0.005 per shot.
- **Recovery:** linear at 0.05/s. Time from the reached peak is 1.733 s; the full range takes 2 s.
- **Airborne ×2:** reaches ×2 by 0.25 s, back to ×1 after landing.
- **Fine Aim:** ×0.5, giving 0.04.
- **HUD filter:** of 13 notifies, none changed spread by ≤ 0.002. It settles at 0.0817.

**Hitscan and HUD read the same spread.** This was checked in source: `World.cpp:397` (fireHitscan) and `PlayerController.cpp:254` (HUD state) both read `Character::effectiveSpread()`. The HUD re-sends only on a change greater than 0.002 (`PlayerController.cpp:236`).

## 2. Experimental gate steps that were broken

| step | what was broken | why | fixed in Experimental | product ever wrong? |
|---|---|---|---|---|
| vehicle_visual | script aborted at `dark_area`: "Cannot convert System.Object[] to SwitchParameter" | `$script:probe` collided case-insensitively with the `[switch]$Probe` parameter | renamed to `$script:areaProbe` | no |
| perf_counters | `counts.txt` missing; count exe exit −1073741571 (stack overflow) | CallCounter's `-finstrument-functions` hook re-entered itself during lazy init | re-entrancy guard; a missing counts file is now SKIP, not FAIL | no |
| (gate) build | CMake stderr warnings became terminating errors | `$ErrorActionPreference = Stop` | steps run under Continue and judge `$LASTEXITCODE`; a missing report goes to TOOL ERRORS (exit 2), never a product FAIL | no |

## 3. Runtime-probe timing

**Cause.** The exe integrates wall-clock time. At more than 700 fps, the Release build covered the fixed 1500-frame reload scenario in about 2 s, so the magazine never emptied.

**Fix.** Every probe scenario now runs under the product's own `WFC_LOCKSTEP=1`, which steps 1/60 s per frame. Frame counts are therefore simulated time in both Debug and Release. The game is not slowed, and no firing code is touched. The fire scenario runs at least 6 s. Reload assertions are gated on the gameplay condition: the magazine reached 0 or a reload ran. If it didn't, the result is SKIP, not FAIL.

**Result.** Both configurations give identical results: 42 reload-over-strafe frames, 3 magazine notifies, 3 reload-FX notifies.

**Shader error.** The only remaining error line is the known ParticleBase_BW_MAT translator error. It is now KNOWN (Rendering), matched by its exact signature; any other GL error still FAILs.

## 4. R→V moving camera jump — KNOWN (Gameplay, provisional behaviour). Not a merge regression.

Evidence: `tools/fidelity/camera-trace.ps1` (per-frame CSV + contact sheet) in `camera_trace/`.

**Reproduction.**
- Merged exe, spawn 0, R→V while walking: frames 107–108 (0.80–0.82 s after the press). The collided orbit distance `camD` drops from 9.29 to 1.92, and the camera sits 3.13 m directly above the pawn (frames render nearly black). It returns to 9.36 at frame 109.
- **Gameplay 0762f01 owner branch:** identical, frame for frame.

**Per-frame state during the jump.**
- Form ROBOT, move form VEHICLE, camS 1 (hover strategy), both meshes shown, arm shown.
- The strategy blend `distCur_` continues smoothly.
- View yaw is unchanged (1.01). The yaw step is 0.00–0.05°.

**Cause: the third-person camera-collision response.**
- `PlayerController::cameraPos` collides the `focus→want` segment with the world. On a hit within 0.3 m of the anchor, it places the camera on the anchor with no smoothing, and it releases on the frame the hit clears.
- The code marks this `[PROV] behaviour details not recovered`.

**It is not transform-specific.**
- Spawns 1 and 2 and press 45 also show it, lasting 2–53 frames. Spawns 3 and 5 and press 90 do not.
- **Robot walking with no transform (spawn 2) shows it.**
- **Hover-vehicle movement with no transform shows it.**

**Scripted input** applies no camera input (`WFC_NOMOUSE`). AUTOWALK only moves the pawn past the geometry.

**Authored or genuine?** Unresolved. The original's TnThirdPersoncollisionCameraBehavior / TnAvoidClippingCameraBehavior response is not recovered.

**Gate change.** `transform_capture.*.no_camera_pop` now separates collision snaps (the jump is explained by the `camD` change) into `camera_collision_snaps` (KNOWN). Any other pop remains a FAIL.

## 5. Materials / shaders — KNOWN (Rendering). Not a merge regression.

- **Byte-identical to Rendering's checkpoint.** Merged `materials_glsl.json` (211 materials) matches render data generated by Rendering 19cd213's own tools, with 0 GLSL differences. `bsp.glb`, `decals.glb`, `clut.png` and `lvv_0.bin` are also byte-identical.
- **One compile failure.** `ParticleBase_BW_MAT` is the only one: `vec4 t7 = vec4(t6, t6)`, where t6 is already a vec4. This is the known translator vector-output defect. No other material contains that pattern, and the exe falls back to glTF for it.
- **Translator fix: deferred past M03** (recorded for Rendering).

Permutations: 197 / 211 match. The 14 mismatches (`verify_permutations.py`) are classified below. **No regression; nothing was invented.**

| class | count | materials | meaning |
|---|---|---|---|
| A. no compiled reference | 7 | LightCylinder2_Rays_Red, LightRaysV2Cool, LightCylinder_Rays, MuzzleFlashWing, MuzzleFlash_Side_02, MonitorScreenActive(Purple) | compiled permutation carries no parameter table; translator binds its textures; comparison not applicable (unsupported permutation data) |
| B. unnamed compiled parameter | 6 | LightVolumePurplish, ENV_EngergonGlass, EVN_HeatedMetal_Purple, LightVolume_Base, Spark_Tail, AnimSign5_Decepticon | only difference is a compiled parameter named `None`; every named parameter matches (extraction naming, known reconstructed) |
| C. extra translated texture | 1 | MonitorScreenDead | all compiled scalars/vectors match; translator samples a `Texture` parameter the compiled permutation lacks (translator, Rendering) |

## 6. Behaviour run-through (merged exe)

- **Robot and weapon:** harness, probe, transform capture and audio, all PASS. Reload presentation PASS. Fine Aim is 2 m back with no lateral shift at FOV 45 (stale −2 m lateral prediction retired). Muzzle, tracer and crosshair PASS (probe).
- **Transform:** both directions, stationary and moving.
  - Native notify windows hold exactly: harness `transform_analyzer` and `transform_timeline`.
  - Capture `mesh_switch_time` now uses the exact native times with the grab-stride tolerance.
  - No firing before the weapon is usable.
  - Arm, hand, root and shadow continuity hold.
  - Audio follows the player.
- **Vehicle:** native_vehicle suites PASS — rest COM 1.287 m, steps, drop, jumps, Dash, boost; nitro is INFO (provisional). Vehicle visual 20/0/1; materials compiled.
- **World:** the map audit now uses runtime evidence. Present:
  - level FX: 8/8 emitters (Systems LevelFx);
  - pickups: authored factories, ammo 14, health 9, overshield 1;
  - destructible WallPanelSign placed;
  - decals: 25/25;
  - ambient: 40 of 70 heard from spawn in 240 frames.

  Remaining KNOWN:
  - 4 InterpActors and LightsVisibilitiesVolume: Rendering's own audit, identical in integration's run;
  - pickup particle FX: deliberately not drawn, PROVISIONAL;
  - 30 ambient emitters not reached from spawn.
- **Non-original content in normal play:** Gameplay's weapon-test `DamageTarget` dummy is an untextured box 10 m in front of the spawn. It is KNOWN; Gameplay should gate it before a wider playtest.
- **Shadows:** `WFC_SHADOWSELFTEST` 32/32 and `WFC_DLETEST` 20/20. Normal play was judged by stills: vehicle visual in bright and dark areas, transform side captures, contact sheets. These are human check items.
- **Audio:** no accumulation in any of 13 scenarios. 30 s of sustained fire starts at a mean of 53.9 live voices in the first third and ends at 34.8 in the last. Vehicle loops (engine, boost, tire, turbo) all end after V→R, with no player-owned loop left. 120+ attachment checks PASS.

## 7. Release performance (`perf_release/`, renderer 120-frame averages)

| scenario | median ms | integration baseline |
|---|---|---|
| idle | 3.91 | ~4 |
| movement | 1.02 | 1.0–1.5 |
| firing | 4.13 | 4.0–4.1 |
| sustained (6000 frames) | 4.06 | 3.75–4.6 |
| fine aim | 3.11 | 3.1–4.2 |
| R→V (turning) | 1.18 | 0.9–1.7 |
| vehicle idle | 4.26 | 4.3–4.5 |
| hover move | 2.41 | 1.2–1.8 |
| boost (turning) | 1.57 | 1.2–1.7 |
| nitro (turning) | 1.36 | 1.2–1.8 |
| V→R (turning) | 1.13 | 1.1–2.1 |

**Frame cost depends on the view.** Facing the spawn view costs about 4 ms for any behaviour; R→V, boost, nitro and V→R with a fixed camera all measure 3.7–4.2 ms. All results are within or near baseline. There were no post-startup spikes except one in nitro.

The Debug sampled-profile `idle.steady` rise (6.06 → 10.41 ms vs milestone-02) is Debug instrumentation of a view that now draws more content. The Release idle of 3.91 ms is within baseline.

## 8. Superseded assumptions retired / rewritten

- 1.85 m hover height → rest COM 1.287 m (native mass link); 185 is the probe radius.
- Flat 30 m/s boost → INFO (provisional tire model).
- "Missing" static destructible, level emitters, ambient actors and prefabs → measured placement / runtime evidence.
- Decals → asserted against render data.
- Graybox pickups → authored factories from the runtime log.
- Fine Aim lateral −2 m → 2 m back, no lateral shift.
- Spread cap within one magazine → native model (above).
- `fine_aim_spread_mult` "not modelled" → applied.
- Transform "never shows both meshes" → native overlap windows.

The footstep check is unchanged: the merged RobotFoley plays `BL_FS_LRG_BOT.FS_*`, which matches the existing check.
