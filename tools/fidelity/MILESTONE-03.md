# Fidelity validation pass — Milestone 03 (2026-10-02)

Branch `agents/experimental`, fast-forwarded to `origin/integration/milestone-02` (e8036f6) before
this pass. **No product code changed.** Everything below measures the integrated milestone-02 code.

## New tooling
| Tool | What it does |
|---|---|
| `measure/Profiler.cpp` + target `wfc_rebuild_prof` | Opt-in measurement build (`-DWFC_BUILD_MEASURE=ON`). Unmodified product sources plus a 1 kHz in-process main-thread stack sampler (SEH unwind, bounds-checked, no allocation while the main thread is suspended). |
| `perf-profile.ps1` | Runs idle / walk / fine aim / burst / held-fire with the product's own WFC_* hooks. Timestamps every frame from the flushed frame log (dt, ammo, reload, fine aim, live particles and mesh particles). |
| `perf-report.ps1` | Symbolizes with `llvm-symbolizer` and attributes cost two ways: **cost** (what the CPU is doing) and **origin** (who asked). Also lists inclusive hot functions and the per-shot accumulation regression. |
| `measure/SpyAudio.cpp` + target `wfc_rebuild_audiospy` (pass 2: now part of `wfc_rebuild_observe`) | The Win32 audio backend replaced by a recorder of every `playAt` / `playVoice` / `updateVoice` / listener pose. |
| `audio-attach.ps1` | Movement scenarios (walk, transform on the move, landing, vehicle loop, boost, nitro). Flags positional sounds that stay at their trigger point while the owner moves on. |
| `stills.ps1` | Standardized screenshots: transform contact sheets (fixed side camera, labelled with the real clip/time/form per tile), and vehicle material angles WFC vs legacy. |
| harness `M03Checks.cpp` | `transform_timeline` (flags: pose freeze, visibility gap, root/heading discontinuity, usable weapon without a visible muzzle, both-mesh overlap), `vehicle_feel` (probes), `fine_aim_presentation`, `trace_cost` (`--map`). |
| Rig | Records transform progress, weapon usable/restored, mesh offset, and the vehicle state machine (driving, ride height, dash/nitro timers + cooldowns) into the trace CSV. |
| Updated checks | Decals are verified in the render data (25/25). The probe reads vehicle FX (`VFX parts`) for boost presentation. The ambient-sound bed is now counted as a gap. |

## Sustained-fire performance
Debug build (the configuration `build.ps1` produces and the playtest ran); RX 7900 XTX.

| Phase | Frame ms | p95 | Dominant cost |
|---|---|---|---|
| idle | 6.9 | 8.1 | render 6.3 |
| walk | 5.9 | 7.5 | render 5.2 |
| fine aim | 5.8 | 7.7 | render 5.2 |
| burst: firing frames | 367 | 851 | **trace 282**, render 63 |
| burst: 0–1 s after the last shot | 12.0 | 17.3 | render 7.0, **visibility 3.2** (shell/magazine lighting) |
| burst: 1–3 s after | 7.6 | 9.5 | render 6.8 |
| held fire, firing frames (mag 1, 2–3 s) | **299** | 309 | **trace 268**, visibility 12.0, render 7.1, anim 7.1 |
| held fire, whole run (incl. reloads) | 61.7 | 296 | trace 47.3 (matches the integration's 62–68 ms) |

- **Not accumulating.** Frame time vs cumulative shots has slope −0.65 ms/shot, r = −0.10.
- **Constant while the trigger is held.** Reload frames, with 14 shell/magazine meshes and about 150
  particles still alive, run at about 20 ms. Frames fall to 7–9 ms as the FX expire.
- **Inclusive (held-fire firing window):** `CollisionWorld::segmentHit` 85%. Under
  `PlayerController::applyToPawn` (79%) it splits into:
  - `World::fireHitscan` → 300 m hitscan trace, 39%
  - the controller's camera-ray "aim through the crosshair" trace, about 40%
- **Deterministic confirmation** (`trace_cost`, real Streets collision): `segmentHit` costs 0.022 ms
  at 2 m, 0.70 ms at 30 m and **32.3 ms at 300 m**. It tests every 2 m grid cell in the segment's
  XZ bounding rectangle, so cost grows with the area rather than the length. Two 300 m rays per shot
  is about 65 ms per shot; at 2 shots per frame that's the ~270 ms.
- **Secondary costs:**
  - Shell/magazine mesh particles go through `drawMesh` → `computeEnv` → visibility rays: ≈12 ms
    per firing frame (origin `shell_mag`).
  - Character skinning and upload: ≈7 ms.
  - Render submission: ≈7 ms.
  - Audio: < 2.5 ms.
- **First shot:** the first firing frame of a burst is 396–834 ms. That includes first-use texture
  and program builds (`Pipeline::programFor`/`texture` appear in the burst profile).

**Primary owner: Gameplay** (`Collision.cpp` `segmentHit`, called twice per shot).
- **Fix direction:** walk only the cells the segment crosses (2D DDA, Amanatides–Woo), test each
  triangle once (mailbox), and stop at the first hit beyond the current best `t`.
- The camera aim trace and the hitscan could share one query.

**Secondary owners:**
- **Rendering:** per-dynamic-object light-environment visibility rays. Cache them per object, or skip
  them for short-lived mesh particles.
- **Systems:** mesh-particle count and lifetime.

## Transformation timeline (`transform_timeline`, both directions, standing and moving, on a collision floor)
- **PASS:**
  - no pose freeze
  - no visibility gap
  - drawn root continuous: max excess ≤ 0.06 m
  - heading continuous
  - fold 1.983 s (robot→vehicle) / 1.150 s (vehicle→robot)
- **KNOWN `both_meshes_visible_during_fold` (Gameplay, CONFIRMED RE HANDOFF #4):**
  - The original animates **both** meshes for the whole fold and detaches the source at the end.
  - The rebuild draws one mesh and switches at progress 0.508 (robot→vehicle) / 0.500
    (vehicle→robot). That's the visible handoff pop.
- **KNOWN `no_usable_weapon_without_muzzle` (Gameplay, new in milestone-02):**
  - In vehicle→robot, `weaponUsable()` (restore at 25% + 0.2 s equip) turns true at progress ≈0.43.
  - The robot mesh (and gun) only appear at 0.50, so for 5 steps (83 ms) shots leave an invisible gun.
- **KNOWN `separate_arm_mesh`:** the arm mesh shown when no weapon is drawn (RE PASS2 #9) has no
  representation.
- **Robot→vehicle height:** the pawn lifts to hover height at t=0. That pop is absorbed by
  `addTransformShift` only on the collision path; the graybox no-collision path pops 1.85 m (INFO,
  not shipping).
- **Contact sheets:** `work/fidelity/stills/transform_r2v.png`, `transform_v2r.png`.

## Vehicle feel (`vehicle_feel`; probes explain feel, they do not prove "weight")
| Probe | Value | Note |
|---|---|---|
| 0 → 90% hover cruise (15 m/s) | 0.45 s | |
| release → < 1 m/s | 0.47 s | symmetric with accel |
| strafe 0 → 90% | 0.45 s | camera-relative strafe |
| velocity kept along the old direction 0.5 s after a 90° camera turn | 4.0 of 15 m/s | grippy, little drift |
| heading error after a camera turn | 0° | hover heading = camera (CONFIRMED) |
| ride height | 1.85 m | CONFIRMED |
| 1 m drop → oscillation crossings / undershoot / settle | 0 / 0 m / 0.32 s | **snaps; no spring/damper behaviour** |
| roll / pitch | not represented | no body lean |
| jump | **0 m/s launch, 0 m apex** | vehicle jump does nothing (KNOWN) |
| forward → 90% reverse | 0.95 s | |
| Boost (RMB, Driving) | 30 m/s (PASS), t90 0.48 s | |
| Dash (Shift) | 30 m/s peak (PASS), plateau 0.6 s, cooldown blocks a re-press (PASS) | no stick: 0 m/s (original UNKNOWN) |
| Nitro (Shift while boosting) | 45 m/s, 3.0 s, cooldown 8.0 s | all PASS |

**Likely contributors to "no weight"** (human check needed):
- instant hover snap with no suspension response
- no roll/pitch
- symmetric 0.45 s accel/decel
- non-functional jump

## Audio attachment (`audio-attach.ps1`, spy backend; owner proxy = listener)
- **Follow their owner (PASS):** all loops via `updateVoice`, 32–223 updates each:
  - engine idle/mid/boost
  - tire noise, turbo whine, energy hum
  - the 3.4 s `AUTO_OPTIMUS_BOOST_START` voice
- **Stay at their trigger point while the owner moves on (KNOWN, Systems):**

  | Sound | Kind | Owner distance during playback |
  |---|---|---|
  | `EVENT_IACON_BRIDGE_TRANSFORM_GEARS` | 3.49 s `playAt` (the generic transform sound) | up to **25.8 m** |
  | `RELOAD_AIR_RELEASE_THUMP` (landing) | `playAt` | 12.4 m |
  | `TRANS_ION_EQUIP_SERVOS` / `TRANS_ION_UNEQUIP_SERVOS` (weapon store/restore) | voices | 9–14 m |
  | `AUTO_BOOST_PEELOUT_TRUCK` | voice | **35.4 m** |
  | `SYNTH_ENGINE_ACCEL` | voice | 27.7 m |
  | `WHSH_BOOST_FLARE_LR_03` | voice | 20 m |
  | `SYNTH_AIR_RELEASE_07/08` | voices | 6–14 m |
  | `MECH_TRANS_RAM_ACTIVATE` | voice | 7.6 m |
  | `MECH_TANK_LAND_LIGHT_01` | voice | 4 m |
  | `GUN_RIFLE_BOLT_ACTION` | voice | 6.1 m |

  - Cause: non-looping SoundCue voices are positioned once at launch and never updated.
    `World::playSfx` uses fixed `playAt`.
- **Robot footsteps:** none are triggered while walking (no footstep cue exists to test).

## Vehicle materials / FX
- **Authored → GLB → compiled bindings:** all PASS.
  - 2 slots, names match.
  - Own `VH_*` textures, own MIC parameter values baked into the GLSL.
  - No robot textures bound; texture files present.
- **Runtime colours:**
  - The renderer has `setCharacterColors` (Cust_Color_A/B, `EnergonColor` uniforms), but **no game
    code calls it**, so the TnCharacterApplier colours are never applied.
  - The energon glow keeps the compiled default (red `vec4(1.25,0.05,0.05)`) while the AssetTools
    bakes are blue (`material_consistency.*`).
  - The correct Optimus `EnergonColor` (character data) is UNKNOWN. Owners: Gameplay/World (call
    site) + Rendering.
- **Vehicle FX sockets:** 11 authored (`BoostSocket_L/R`, `HoverBooster_*`, `JumpBoostSocket_*`);
  the probe reads `VFX parts` while Driving.
- **Stills:** `work/fidelity/stills/vehicle_materials.png` (front/side/rear/¾, WFC vs legacy, plus the
  robot).

## Map completeness (concrete inventory)
| Item | Authored | Runtime | Owner |
|---|---|---|---|
| sublevels | 3 | 3 | — |
| placed props | 1952 | 1952 in `world.glb`, 0 missing meshes | — |
| BSP | 2460 tris | `bsp.glb` | — |
| world materials | 164 | all named; compiled render data covers all world materials | — |
| static decals | 25 | **25** in `decals.glb` (drawn by the WFC decal pass) | — (stale "unrendered" note fixed) |
| lights | 1 actor / 268 components | 268 | — |
| **PrefabInstance** | 16 | 0 (never extracted) | AssetTools/RE |
| **TnStaticDestructibleActor** | 1 | 0 | AssetTools/RE |
| **level Emitters** | 8 | 0 spawned | Systems |
| **ambient sound actors** | 70 (40 AmbientSound + 13 line + 17 volume emitters) | 0 | Systems |
| HeightFog | 1 | applied | — |

## Regression check: the same harness on milestone-01 vs milestone-02
`ab.ps1 -Ref integration/milestone-01 --map` vs this tree:
- **One product regression:** `transform_timeline.v2r_{stand,moving}.no_usable_weapon_without_muzzle`
  went PASS → KNOWN.
  - Milestone-01 never made the weapon usable mid-fold.
  - Milestone-02's 25% restore + 0.2 s equip makes it usable before the robot mesh exists.
- **Improved:** `r2v_*.root_continuous` FAIL → PASS. Milestone-01 popped the drawn root at the fold
  start; milestone-02's mesh shift absorbs it.
- **50+ checks FIXED** (Gameplay Pass 11/12):
  - movement constants
  - transform momentum and live input
  - boost/dash/nitro
  - vehicle heading
  - reload-on-release
  - fine aim
- Milestone-01 itself scores 157/2/79; the 2 FAILs are the root pops above.

## Fine Aim presentation
- **PASS:** state (RMB toggle), FOV 45 (smoothing 0.1/0.4), speed ×0.5, look ×0.5, blocked while
  reloading, resumes after the reload.
- **Spread:** ×0.5 is applied in `World::fireHitscan` (code check; World is not linked in the harness).
- **Camera:** entering fine aim moves the camera laterally by only 0.05 m. The authored generic row
  moves the shoulder offset X from 150 to −50 UU (≈2 m). That suggests the fine-aim offset row isn't
  applied, but the offset's screen-space semantics are PROV, so this is INFO for Gameplay.
- **KNOWN `start_end_audio` (Systems):** no WP_StartFineAim/StopFineAim cue is wired.
- **Reticle / HUD:** there is no HUD (window title only). The reticle asset relationship is not
  recovered, so nothing is asserted.

## Stills review (`work/fidelity/stills/*.png`, human confirmation needed)
- **robot→vehicle sheet:**
  - The robot mesh plays `Transform_ToVehicle_ROBO` to t=0.77, then the vehicle mesh appears whole
    at `_VEH` t=1.00. There is no frame with both meshes, which is the visible handoff pop.
  - At p045 a white **wireframe box** is drawn around the pawn: a debug/bounds overlay leaking into
    the WFC render path (Rendering).
  - The vehicle reached by transforming looks mottled pink/grey. The vehicle spawned directly
    (`WFC_STARTVEHICLE`) shows clean red/blue panels. Possibly a pose/lighting difference, possibly a
    material/section mismatch after the fold. Rendering should confirm with a matched camera.
- **vehicle→robot sheet:** the vehicle folds to `_VEH` t=0.53, the robot appears at `_ROBO` t=0.87,
  and the weapon is visible from the first robot tile.
- **vehicle_materials, WFC vs legacy:**
  - The WFC hover FX draws only pale rings. Legacy draws orange rings plus a red/white downward
    thruster plume.
  - Rear energon/light strips: red in WFC, blue in legacy. This matches the unapplied `EnergonColor`
    (compiled red default).
  - The legacy path renders a different stand-in map at that spawn, so legacy backgrounds and the
    legacy ¾ tile (camera inside a wall) aren't comparable. Compare the vehicle only.
