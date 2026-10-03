# WFC Rebuild — Playable Vertical Slice Status

_Updated as work proceeds. Build: `powershell -ExecutionPolicy Bypass -File build.ps1`
→ `build/bin/wfc_rebuild.exe`. Fidelity audit + provenance: `FIDELITY.md`._

## GAMEPLAY PASS 16 (2026-10-02) — RE d50e2a9 runtime semantics (narrow)
- Pickups:
  - touch is an overlap begin; health re-checks overlapping pawns on respawn (CheckTouching);
  - sleeping keeps collision but ignores touches;
  - the highlight beam is only for ammo crates; the ammo crate rotates while available;
  - events carry the visual state and the receiving pawn's position for the pickup sound.
- Weapon spread:
  - linear per-tick recovery (whole range in 2 s);
  - airborne ×2 ramp (0.25 s up / 0.5 s down);
  - fine aim ×0.5;
  - the same effective spread drives the hitscan cone.
- HUD: TnHUD notify calls (spread > 0.002 filter; weapon class + fine aim together) via `PlayerController::hudNotifies()`.
- Harness: `weapon.spread_after_10` / `spread_cap` now fail against their superseded expectations (see FIDELITY PASS 16); Experimental should update them.

## GAMEPLAY PASS 15 (2026-10-02) — AssetTools 7a69756 authored-data handoff
- Fine aim:
  - the Ion Blaster keeps its authored crosshair (no scope or ADS);
  - Gameplay exposes `PlayerController::hudAimState()` (weapon class, standard/fine aim type, spread, crosshair visibility, target type);
  - the native camera is unchanged.
- Pickups:
  - 24 authored Streets factories (14 ammo crate / 9 health / 1 overshield) from gameplay.json;
  - authored respawn times, touch cylinder, health +50;
  - one event per state transition via `World::pickupEvents()`;
  - objective factories not instanced (CTF/Bombing modes).
- Wall panel: authored destructible state machine (20 health → destroyed → settled after 10 s) at its authored, out-of-play location.
- Graybox near-spawn pickups removed.
- Test modes:
  - `WFC_PICKUPTEST=1` (pickup respawn and destructible validation);
  - HUD fields added to the frame log.
- Details and superseded assumptions: FIDELITY.md PASS 15.

## GAMEPLAY PASS 14 (2026-10-02) — native RE Milestone 03 vehicle reconcile
Implements only what MILESTONE03_VEHICLE_NATIVE_FIDELITY.md confirms; provenance and measurements are in FIDELITY.md
PASS 14. Changes:
- spring gravity is the rigid-body GetGravityZ (−1940.4), giving rest COM 1.287 m = native L_eq;
- TnAccelerationAnimBlend hover pose weights;
- CurveAutoClamped camera offset curve;
- HandSkelControl hand shrink (0.1, instant);
- ram victims restricted to TnPawns, with robot RammedReaction and vehicle AddVelocity ×0.5 implemented;
- exact notify times with transform Rate.
WFC_VEHTEST=1 runs the deterministic handling measurements (rest, coast-down, 0.25/0.5 m bumps, 10 m drop,
hover/boost jump, dash, drift turn).
Still PARTIAL/PROV:
- hover RB mass link (M=2500);
- boost tire coefficient;
- hull contact (min clearance, ceiling probe);
- ram victim masses (no pawn victims in the slice).

## GAMEPLAY PASS 13b (2026-10-02) — Milestone 03 handoffs (Experimental + Systems)
- Experimental's "weapon usable before a visible gun" and "vehicle jump missing" were measured on milestone-02, before
  049f614. With Pass 13 the gun is drawn at 0.28 s and first usable at 0.50 s. Firing requires the drawn gun. The
  vehicle jump exists (hover + driving).
- Fine-aim offset semantics (raw OffsetCurvesByPCS): Y (lateral) = 300 UU in every row, so there is no lateral
  shift between default and fine aim (Experimental's ~0.05 m is expected). The 2 m difference is orbit-space X
  (+150 → −50: the camera moves 2 m back along the view; TnLocationOffset writes X = −OrbitDistance) [CONF].
- Arm: CP_OptimusArm_SKEL + OptimusArm_ROBO_ANIM loaded (text glTF + name-matched anims), attached at
  WeaponSocket_Secondary (R_Arm03_Elbow_XB, pitch 180), TnArmAttachment rules:
  - shown whenever the robot mesh is displayed with no drawn weapon (all of R→V, V→R before the restore);
  - ARM_Unequip (0.8 s) once the gun is drawn, then detached.
  HandSkelControl not applied (PROV).
- Landing: SharedAcrobatics.LandingAnims table picks Nav_Land / Nav_Land_02 / Nav_Land_03 by fall height and speed.
  None below 250 UU; a standing jump gives Nav_Land_02.
- Stop flicker (idle/walk for 1–2 frames): caused by the wall block zeroing velocity and parking one probe radius
  out, so the next, slower step crept forward. The wall block now advances to the gap, removes only the velocity
  into the wall and slides the rest of the step along it (re-probed for corners, no back-slide), with physWalking's
  displacement velocity. The harness wall-slide checks now pass (181/0/21).
- Ram: World::gameplayRamContacts → notifyRamHit during nitro (once per target per nitro), 300 damage (AI robot).
- CollisionWorld::segmentHit is Systems' validated grid walk verbatim. Gameplay only adds a normal-returning
  overload (suspension contact normals).

## GAMEPLAY PASS 13 / MILESTONE 03 (2026-10-02) — vehicle body, vehicle camera, transform handoff, firing cost
Branch agents/gameplay, fast-forwarded to integration/milestone-02 (e8036f6) first; clean build OK.
Driven by the Milestone 02 human playtest. Evidence: TransGame/HM_Engine bytecode (work/pass13/vehdis.txt,
camdis.txt, pcdis.txt via work/pass11/ue3dis.py) and authored data (VEH_SHARED_p, CAM_Driving_Strategies_p).

- **Hover = rigid body on four springs** (TnHoverCarSimulation.UpdateSuspension + TnSuspension/TnSpring):
  - mounts at SuspensionRadius 185 around the COM (Pass 7–12 wrongly used 185 as a ride height);
  - rays along body −Z, RestingLength 250;
  - implicit spring with Stiffness 10000 / Damping 4000 and per-spring mass Mass/4;
  - Truck_Physics mass 2500, COM (−47,0,5), inertia 2.27e7/4.64e7/5.89e7;
  - world gravity in the spring and RB gravity ×0.66 on the body;
  - result: the COM settles at 1.36 m.
  Pitch and roll now come from the springs and terrain, plus UpdateRoll (strafe input − yaw rate):
  - about 7° transient (2° held) when strafing;
  - about 4° banking into turns;
  - curbs and ledges tilt the body.
  Uprighting applies only with no contact. Yaw tracks the smoothed camera (PlayerInVehicleForm.PlayerMove).
- **Vehicle jump** (Hovering.UpdateJumping):
  - requires the ground (contact normal Z > 0.707), with a 0.3 s interval;
  - +1200 UU/s vertical, nose up 1 rad/s;
  - verified about 4.2 m rise, spring landing and rebound.
  Driving jump: local (600,0,1400) plus nose up 2 rad/s, air control, pitch-forward limit −25°.
- **Hover dash is forward only** (TnTruckForm.Hovering.DoDash overrides the car's dominant-axis dash):
  - refused while unstable (>30°);
  - local Z velocity is cancelled during the dash;
  - it ends with an immediate 100000 UU/s² decel to 1500.
- **Normal boost** = TnCarSimulation.UpdateBoost/Drag:
  - Lerp(MaxAccel, Drag(MaxSpeed), v/MaxSpeed) + ExtraBoost, so 30 m/s is the emergent limit;
  - the truck drops onto its wheels;
  - a frontal wall hit returns to Hovering (OnRigidBodyCollision 0.866).
  Steering = look-X input (GetNormalizedTurn), sign·s². Tire model PROV.
- **Nitro** unchanged in rules (×1.5 speed, ×0.3 steering, 3 s / 8 s). Ram collision is still not implemented.
- **Vehicle camera strategies** (HoverTruck_Optimus / Truck_Optimus; OverTheShoulder for the robot):
  - anchor, orbit, FOV and pitch-range per strategy;
  - HmC2Smoother (decoded) for FOV, offsets and orbit rotation;
  - strategy blends of 1.5 / 1.5 / 1.0 s;
  - nitro FOV 100 and orbit 650 (in 0.5 s, out 2.0 s);
  - Driving yaw locked to the truck, pitch chase at rate 3;
  - Wiggler3.
- **Robot camera offset corrected:** TnScreenSpaceOffsetByPitch Offsets is a static array of three vectors.
  - Default: (150,300,{150,−35,150}).
  - Fine aim: (−50,300,{80,−35,80}).
  Pass 11 read only the first vector.
- **Transform handoff:** the authored ToggleHidden notifies replace the 50% mesh swap.
  - Both meshes are drawn on the shared clip time: to vehicle 0.396–0.880 s, to robot 0.098–0.663 s.
  - Both meshes hang off the shared actor location: robot cylinder centre / vehicle bounds centre
    (TnVehicleForm.CalculateCylinderBounds).
  - The robot→vehicle "flattened robot freezes then the truck appears" came from the robot clip reaching its
    folded pose at 0.88 s while the truck was only shown from 1.0 s.
- **Weapon on vehicle→robot:**
  - the gun is attached to the robot mesh, drawn from 0.098 s;
  - restored at 25% of the fold (0.28 s), usable at +0.2 s (0.48 s);
  - firing also requires the gun to be drawn that step, so no gunless shot is possible.
- **Firing performance:** the gameplay traces were 23–34 ms/frame during sustained fire (AABB cell scan of two
  300 m rays per shot). The grid-walk traversal gives identical hits (0/3000 mismatches vs brute force) and
  about 0.1 ms. WFC_PERFLOG=N logs it.
- **Fine aim state** for presentation: `PlayerController::fineAimState()` (wanted, active, FOV blend, FOV).
- **Not done:**
  - CP_OptimusArm_SKEL attachment (raw umodel glTF, not loaded by the runtime);
  - the vehicle hull is approximated (min clearance, ceiling probe, wall probe);
  - ram collision;
  - wheel/tire steering.

## INTEGRATION MILESTONE 02 (2026-10-02) — branch `integration/milestone-02`
Integration and stabilisation only, no new features. Branched from milestone-01 (e62250e).
Each branch was merged with `--no-ff`, one at a time, then built and checked with the harness.

| Order | Branch | Head | Conflicts |
|---|---|---|---|
| 1 | agents/experimental | c9592f0 | none |
| 2 | agents/rendering | 13128bb | `Renderer.h` (Systems particle structs vs Rendering `CharacterColors`: kept both), `STATUS.md` |
| 3 | agents/gameplay | 21862a2 | `STATUS.md`, `FIDELITY.md` (sections kept from both sides; PROVISIONAL list combined) |
| 4 | agents/systems | b8fed05 | `Input.h`, `Win32Window.cpp`, `STATUS.md` |

**Cross-branch resolutions (ownership rules):**
- **Dash input:** Gameplay's mapping is authoritative. **Shift = Dash** (pad RB); **RMB** = Fine Aim
  in robot form and Boost in vehicle form. Systems' duplicate `Dash` enum, temporary **Q**
  binding and World-side Dash latch were dropped. That latch also latched every frame whenever
  `WFC_AUTODASH` was set, which clashed with Gameplay's frame-number hook.
- **Vehicle state:** Gameplay's `CharacterMovement` owns Driving, the hover dash, the nitro timers
  and cooldowns, and the speed/steering scales. Systems' `VehicleNitro::update` timer was replaced
  by `follow(vehicleState().nitroRemain > 0)`. RamFX, the nitro/alert cues and the ram-hit
  registry now track Gameplay's single state machine. Boost FX/audio follow Gameplay's Driving
  state instead of raw button state. Values are identical on both sides (3 s, ×1.5, ×0.3, 8 s).
- `Character::boneWorld` (Systems, vehicle FX sockets) now includes Gameplay's transform
  `meshOffset()`, matching `draw()`.
- Recoil, aim offset and upper-body layering: still exactly one implementation (Gameplay's), and
  recoil fires once per shot (`PlayerController` → `notifyFired`).
- **Ion Blaster cadence:** Systems' native-RE one-shot timer (strict `>`, overshoot discarded,
  ~900 RPM). The 923 RPM patch is not applied; Experimental withdrew it.

**Render data:** `tools\render\build_render_data.ps1` regenerated `work/render/MP_IAC_Streets`:
175/175 materials, 25 decals (`decals.glb`), `clut.png`, the post-process block, BSP, 268 lights
and 78 atlases. Output is byte-identical to agents/rendering's own output, apart from the
embedded worktree path. `slot_materials.json` is `{}` (0 default-material slots) on both.

**Validation (`.\build.ps1 -Jobs 2 -Clean`):** `wfc_rebuild.exe` and `wfc_fidelity.exe` build
with 0 compiler warnings.
- `wfc_fidelity` (latest Experimental harness): **194 pass / 0 FAIL / 19 known / 119 info / 1 skip**.
  `--map`: 196/0/19/135/0. `--no-assets`: 139/0/16/37/18.
- The same harness on milestone-01 (`ab.ps1`) gives 125/0/71/118/3. vs that baseline:
  **0 REGRESSED**, 49 FIXED, and the new checks (fine aim, form switch at t=0, map content,
  vehicle materials) pass. The Systems merge changed no check (identical to post-Gameplay).
- `runtime-probe.ps1`: 30 pass / 0 FAIL / 4 known.
- Runtime scripted runs (logs and stills in `work/fidelity/m2shots/`):
  - Robot jog 14 m/s forward/back/diagonal with directional clips; facing tracks the camera.
  - Jump about 5 m.
  - Fine aim: FOV 80→45; blocked during reload; resumes after.
  - Sustained fire: 200 shells, 3 magazine drops.
  - Reload while jogging: slot 1.0, legs keep `Nav_StrafeJog_F`.
  - Transformations both ways, stationary and moving:
    - robot→vehicle carries 14 → 15 m/s with heading continuous;
    - vehicle→robot carries 15 → 14.7 m/s until a wall;
    - the movement form switches at t=0.
  - Hover 1.85 m, camera-relative strafe 15 m/s.
  - RMB Boost: Driving on wheels, 30 m/s.
  - Shift hover dash: 30 m/s × 0.5 s.
  - Shift while boosting: nitro 3 s above 30 m/s, with RamFX and nitro cues once. Releasing Boost
    ends it immediately.
- Rendering: WFC path, CLUT grade, bloom, far DOF, robot/vehicle WFC materials, boost afterburners.

**Performance (RX 7900 XTX, WFC path):**
- Idle 6.1 ms; jog 4.7 ms; boosting 3.9 ms.
- **Sustained fire 62–68 ms** (Milestone 01: 40–46 ms).
- On the same renderer, firing costs +20.8 ms/frame on both agents/gameplay head and the merged
  build, so the increase is inherited, not a merge error. Gameplay now also traces the camera ray
  per shot. The Milestone 01 light-environment/mesh-particle interaction still applies. Not fixed
  (owners: Gameplay, Systems, Rendering).

**Known issues carried into the playtest:**
- Vehicle→robot: the weapon becomes usable and visible at the mesh handoff (~50% of the fold),
  not the confirmed 25% + 0.2 s equip. KNOWN `weapon_restore_frac_elapsed_to_robot` (Gameplay).
- Vehicle-specific camera incomplete; no vehicle shoulder offset (Gameplay, documented).
- Ram collision is not implemented in any branch; `World::notifyRamHit` exists but nothing calls it.
- Original transformation sounds are not present in any branch: the generic gears wav still
  plays (KNOWN `transform_cue_is_authored`).
- Harness KNOWNs remain:
  - jump apex 5.14 vs 5.0;
  - step-up 0.35 vs 0.37;
  - wall slide;
  - low-obstacle penetration;
  - vehicle jump;
  - dodge clips unreachable;
  - zero-step fire tap;
  - missing prefab/destructible actors;
  - level emitters.
- Two harness notes predate other branches' work. `unrendered.decals` says there's no decal pass,
  but the decals render. The probe's `boost_fx_emitted` reads weapon-FX counters only; vehicle FX
  log `VFX parts=26–28` while boosting. These are for Experimental to update.
- Robot/vehicle energon hue (AssetTools bake blue vs compiled red constant): KNOWN, Rendering.
- `LightMapTexture2D_882/_5049.png` decode warnings are pre-existing (legacy path only).

## INTEGRATION MILESTONE 01 (2026-10-01) — branch `integration/milestone-01`
Integration only: no new features. All four agent checkpoints were merged with `--no-ff`, one at a
time, building and running the fidelity harness after each merge.

| Order | Branch | Head | Conflicts |
|---|---|---|---|
| 1 | agents/experimental | 6dc6523 | none |
| 2 | agents/rendering | dd21838 | `.gitignore` (kept both rule sets) |
| 3 | agents/gameplay | 5dd1b09 | `STATUS.md`, `FIDELITY.md` (kept all sections from both sides) |
| 4 | agents/systems | ca9cd25 | `Character.cpp/.h`, `Recoil.h` (add/add), `SkinnedModel.cpp`, `Renderer.h`, `STATUS.md`, `FIDELITY.md` |

**Duplicate Gameplay/Systems work, resolved per tools/fidelity/CHECKPOINT.md:** both branches
implemented the reload upper-body slot, the TnAnimNodeAimOffset "Default" profile and
HmSkelControlRecoil, with identical recovered data. The merged build keeps **Gameplay's**
implementation, because locomotion, turn-in-place, transform and hover depend on it. Systems'
`AimOffset.h`, `updateUpperBody`/`evalLayered`, shot-serial recoil trigger, second `setAimPitch`
call and parallel `LocalPose` API were not merged. `WeaponMesh` was ported onto Gameplay's
`samplePose`/`blendPose`/`skinPose`, with the same semantics. Recoil fires once per shot
(`PlayerController` → `notifyFired`). Everything unique to Systems is kept: the animated Ion Blaster
and its sockets, AnimNotifies, original FX (muzzle flash, tracer, impact, shells, reload
flare/smoke, magazine drop), SoundCues, the audio voice API, and the glTF+.bin loader.
`Renderer.h` keeps Rendering's `loadMapRenderData`/`setVisibilityQuery` **and** Systems'
`drawParticles`. `GLRenderer::drawParticles` now restores the caller's `GL_FOG` state instead of
force-enabling it. Fixed-function fog stays off in the WFC shader path.

**Validation (clean build, `.\build.ps1 -Jobs 2 -Clean`):** `wfc_rebuild.exe` and `wfc_fidelity.exe`
build with 0 compiler warnings.
- `wfc_fidelity`: **110 pass / 0 FAIL / 6 known / 51 info / 1 skip** (exit 0). `--map`: 112/0/6/67/0.
  `--no-assets`: 79/0/6/25/10.
- vs the Experimental baseline (main code, 100/0/16): 0 REGRESSED, 10 KNOWN→PASS (Gameplay fixes).
- vs agents/gameplay head: identical. vs agents/systems head: 0 REGRESSED, 9 KNOWN→PASS.
- Runtime, with scripted `capture.ps1` runs (logs and PNGs in `work/fidelity/shots/int_*`):
  - World loads; WFC shader path active (168/169 materials, 1973 lightmapped components,
    268 lights, fog). Optimus, the vehicle and the Ion Blaster render through their WFC materials.
    The `WFC_LEGACYRENDER` fallback works.
  - Walk/strafe pick the directional clips. Facing follows the aim with the back to the chase cam
    (`face.toCam -0.91`).
  - Transformation works both ways (to vehicle → hover, to robot). The vehicle drives and boosts,
    and stops at walls.
  - Fire: SoundCue layers, impact cues, recoil, `IonBlaster_Fire` and muzzle/tracer/impact/shell
    FX all run. A full 50-round mag dump gives 50 shell notifies, then auto-reload.
  - Reload on the move: upper slot weight 1 over `Nav_StrafeWalk_F`, reload cues, flare/smoke
    @0.034, magazine drop @0.174 at MagSocket.
- Render data must be generated per worktree:
  `powershell -ExecutionPolicy Bypass -File tools\render\build_render_data.ps1` (→ `work/render`).

**Known issues carried into the playtest (none introduced as harness FAILs):**
- **Frame time while firing:** about 6 ms standing → 40–46 ms during sustained fire (WFC path).
  - About 20–28 ms of this is inherited from Systems: the agents/systems head alone shows
    +20 ms while firing on the legacy renderer.
  - About 8–12 ms is a merge interaction: each moving shell/magazine mesh particle is drawn as a
    dynamic object. It misses Rendering's 4–8 slot light-environment cache, so it re-traces
    visibility rays for every candidate light each frame. Measured with visibility traces
    disabled: 34 ms.
  - Not fixed here, because the fix changes FX lighting behaviour. Owners: Rendering and Systems.
- `collision.max_step_height` is 0.70 m effective, against 0.35 m CONF. Gameplay's constant is 0.35,
  but it is still applied twice (Experimental patch #2, not applied). Jump apex, wall slide,
  low-obstacle and fire-interval KNOWNs remain; they are Experimental patches 1–4 for the owners.
- Reload/owner-anim slot blend: Gameplay `kSlotBlend` 0.15 s [PROV] vs Systems-recovered
  0.1/0.1 s [CONF]. For Gameplay to reconcile.
- Systems' fixed-function particles composite into Rendering's linear HDR target without
  sRGB→linear conversion. They may read slightly brighter/flatter than the original. Visual check
  in playtest.
- `SHOOT_TAIL` can fire mid-burst when a frame hitch exceeds 2× FireInterval (Systems
  edge-detect on wall-clock time).
- `LightMapTexture2D_882/_5049.png` in ExtractedAssets fail to decode for the legacy lightmap path.
  This predates the merge; the WFC path uses its own regenerated atlases.

## RENDERING PASS 9 (2026-10-01) — CHARACTER CUSTOMIZATION PATH (TnCharacterApplier)
- Reconciled native RE evidence + AssetTools `materials_authored.json`: the applier pushes
  `Cust_Color_A` / `Cust_COLOR_B` / `EnergonColor` onto robot, vehicle, separate arm and weapon;
  an all-zero colour skips the override (authored value stands); faction selects the colour set; no
  separate team override in this path. Optimus character data is all-zero, so the authored MIC
  values stand — identical to the previous render (verified). The faction energon constants equal
  the master's three `EnergonColor` switch constants; the Autobot branch compiles to (1.25,0.05,0.05).
- Character/weapon materials now compile those parameters as runtime uniforms; when skipped, each
  same-named expression keeps its own authored value (UE3 duplicate-parameter semantics). World MICs
  that also declare `Cust_Color_A` stay constant (the applier only targets character meshes).
- New `IRenderer::setCharacterColors(CharacterColors)` (default no-op → authored); applied only to
  dynamic (character/weapon) draws. Gameplay ownership unchanged: nothing calls it yet.
- Raw umodel meshes (`CP_OptimusArm_SKEL`, the MP robot `RB_OptimusWeaponArm_SKEL`) resolve their
  original materials by section material name — verified rendering with the original MICs.
- Shared `world.glb` was regenerated by AssetTools with all section materials resolved; the PASS 8
  slot remap is now a no-op for current data (kept for older extractions).
- Verification hooks: `WFC_CHARCOLORS="pr,pg,pb;sr,sg,sb;er,eg,eb"`, `WFC_TESTMESH="file.glb|x,y,z|yaw"`.

## RENDERING PASS 8 (2026-10-01) — VEHICLE MATERIAL FIX + STREETS COMPOSITION AUDIT
- **Optimus vehicle regression fixed.** Root cause: the vehicle MIC enables `UseReconstructedNormal`
  (normal X in the alpha of DXT5 `VH_Optimus_NORM`, Y in green); its cooked `UnpackMin` covers R,G,B
  only, so X stayed in [0,1] and every vehicle normal was tilted (lighting, specular and the
  reflection that feeds emissive all wrong). The original compiled character PS unpacks both
  channels `(A,G)*2-1`; the translator now does the same. Robot unaffected (DXT1 RGB normal map).
  Diffuse/customization path verified identical to the AssetTools bake (`WFC_ALBEDO` A/B).
- **Authored post-process applied** (persistent level = `MP_IAC_Streets_BASE_m`, TransLevels.ini):
  Streets CLUT `ENV_MPCLUT_p.MP_Streets_CLUT` (32³ volume, decoded), Bloom_Scale 0.1, far DOF
  (max 0.6, falloff 40000 UU). PASS 7 wrongly assumed engine defaults (the BL_LVL stub misled it).
- **25 static decals recovered** from cooked receiver geometry (landmark chevrons/corners, additive
  unlit, 2105 tris) → `decals.glb`.
- **15 mesh sections** left on the default grey material by the extractor now use their original
  materials (`slot_materials.json`); 6 are null in the original data.
- Mirrored-instance winding restored (1 prop).
- Streets composition verified complete: BASE streams only ART + AUDIO (both composed); all 34
  prefab-instance actors and all 1952 props present; prop placement cross-checked against decal
  receiver geometry.
- New diagnostics: `WFC_ALBEDO`, `WFC_GLTFMATERIALS`, `WFC_SKIPMAT`, `WFC_NODOF`, `WFC_NOCLUT`,
  `WFC_NODECALS`.

## RENDERING PASS 7 (2026-10-01) — ORIGINAL WFC RENDER PATH (shaders, materials, lighting, post)
Branch `agents/rendering`. The runtime now renders Streets and Optimus through a GL 3.3 shader path
whose every stage was recovered from the original game data/binaries (details + provenance:
FIDELITY.md "PASS 7"). The legacy fixed-function path remains as an automatic fallback.
- **Materials from the original graphs:** `tools/render/matc.py` translates the cooked UE3 material
  expression graphs (master + WFC MaterialFunctions + MIC params, **static switches**, **TextureSets**,
  WFC HLSL `ShaderCode` snippets) into per-instance GLSL — 168/169 materials (world, BSP, Optimus
  robot/vehicle, Ion Blaster). Fixes the old extraction's biggest error: world materials were drawn
  with their *decal* texture (`Rust_C_CLR`) as diffuse; the real diffuse/normal/masks live in TextureSets.
- **Directional lightmaps, all 3 coefficients**, decoded from the original Xenon base-pass shader
  microcode (`tools/render/xenos_dis.py`): `L = Σ dot(N_t,B_i)² · LM_i · Scale_i`, sRGB-decoded atlases.
- **BSP lightmaps:** BSP rebuilt from the cooked `FModelVertexBuffer` (ShadowTexCoord) split per
  ModelComponent element with its own lightmap (180 lit elements, 2460 tris) → `bsp.glb`.
- **Normal maps / specular / gloss / emissive / reflections:** all as authored by the graphs; cubemaps
  and flipbooks decoded natively from Xbox-tiled data (`tools/render/xbox_texture.py`).
- **Dynamic-character lighting:** WFC UberLight decoded from microcode (ambient cube + wrapped²
  diffuse + Phong spec), light environment built from the 268 authored lights (TotalLightCount 2,
  0.3 m update threshold from TnRobotForm/TnVehicleForm), dynamic-only SkyLight, raycast visibility.
- **Fog / post:** UE3 height fog (authored component, LightBrightness 0.1 default), HDR target,
  decoded bloom gather + UberPostProcess (WorldInfo defaults), DisplayGamma 2.2.
- **Performance:** ~200 fps (RX 7900 XTX) standing and walking; VBOs, per-submesh frustum culling.
- **Render data:** generated into `work/render/<Map>` (untracked) by
  `powershell -ExecutionPolicy Bypass -File tools\render\build_render_data.ps1` — run once per worktree
  before launching. Missing data ⇒ legacy renderer.
- **New env vars:** `WFC_LEGACYRENDER`, `WFC_RENDER_DATA=dir`, `WFC_LIGHTINGONLY`, `WFC_NOBLOOM`,
  `WFC_NOFOG`, `WFC_RENDERCAM=x,y,z,yaw,pitch`, `WFC_RENDERSTATS`, `WFC_DUMPSHADER` (`WFC_NOLIGHTMAP` kept).
- **Still open:** LightsVisibilitiesVolume (precomputed light visibility, format partly decoded) not
  used; 24 vertex-lightmapped props unlit by lightmap; dynamic shadows (ShadowMask) = 1;
  DirectLightAmbientContribution = 0; 1 material with an absent master.

## FIDELITY PASS 12 (2026-10-01): RECONCILED WITH NATIVE RE; THREE VEHICLE MECHANICS; INPUT LATCHES
Inputs: confirmed native RE notes (transformation, locomotion, fine aim, input, weapon restore) and the
Systems checkpoint "VEHICLE MECHANICS" (agents/systems 3700965), plus new bytecode (TnCarForm Hovering/
Driving, TnHoverCarSimulation Update/Strafe/Turn/Dash/Drift).
- **Vehicle->robot now enters FALLING with full velocity** [RE]. Pass 11 snapped it to the ground and
  absorbed the drop with a mesh offset; that is removed. Verified: 15 m/s kept, a 1.85 m drop over
  ~0.2 s, then jogging at 14.
- **Robot->vehicle:** velocity is written to the vehicle unchanged (≤35 m/s, no reprojection); the
  vehicle starts on the robot yaw; hover steering authority fades in over 0.5 s
  (DriftScale = (1 − t/0.5)², from Hovering.BeginState → Drift). Verified 14 → 15 m/s as authority returns.
- **Hover mode corrected:** the hover truck faces the VIEW yaw and STRAFES in that frame
  (Hovering.DoUpdate passes the view yaw; UpdateTurn matches it; UpdateStrafe: 15 m/s, 30 m/s²
  clamped radially). Pass 7's "face the travel direction at π rad/s" was wrong for hovering.
- **Three vehicle mechanics, not conflated:**
  - Normal boost: hold RMB / pad LT → Driving (wheels; Truck_Physics 30 m/s, 25 m/s²); blocked during
    the drift ramp; release → Hovering + drift. Animation: Nav_HoverToBoost_VEH → Nav_Idle_Wheels_VEH,
    then Nav_BoostToHover_VEH on exit.
  - Hover dash: Dash while hovering → 30 m/s along the dominant input axis for 0.5 s (100000 UU/s²);
    cooldown 2 s.
  - Nitro: Dash while driving → speed ×1.5 (45 m/s) and steering ×0.3 for 3 s; cooldown 8 s; ends on
    leaving Driving.
- **Dash binding recovered:** Dash = VehicleSpecialMove = **Shift** (pad RightShoulder);
  PlayerInCarForm.StartVehicleSpecialMove → set_DashingInput. (Systems used a provisional Q.) Shift
  is no longer a boost alias.
- **Input latches [RE]:** the fire held flag persists; reload fires on release of a tap < 0.3 s; jump
  and dash edges stay latched until a simulation step consumes them; transform fires on press.
- **Weapon restore [RE]:** vehicle→robot restores the weapon at 25% elapsed (0.75 remaining) and it is
  usable after the 0.2 s equip. Verified usable at t = 0.48 s of the 1.13 s fold.
- **Fine aim:** transforming to the vehicle ends it (the wish is cleared); during vehicle→robot the
  control form is the robot.
- **Locomotion:** no play-rate compensation; clips stay at 1.0× (the original has the same stride
  mismatch) [RE].
- Regression: jump 5.12 m, turn in place, recoil, reload on the move, fine aim 7 m/s / FOV 45, step
  routes unchanged; clean build.
- **Known:** the weapon becomes usable at 25%+0.2 s, but the robot mesh (and so the visible gun)
  appears at the 50% mesh handoff [PROV]. Driving steering/throttle is PROV (wheel physics not
  recovered). The vehicle camera strategy (Truck_Optimus_CAMSET) is not recovered (the robot camera
  is still used). Ram collision is not implemented. Nitro state is duplicated with Systems'
  VehicleNitro: unify at integration.

## FIDELITY PASS 11 (2026-10-01): TRANSFORM MOMENTUM, ROBOT RUN SPEED, FINE AIM (player-control pass)
Driven by the integrated human playtest. Evidence: UnrealScript bytecode decoded from TransGame.xxx
(`work/pass11/ue3dis.py`), shipped input bindings, Optimus/truck/camera content objects.
- **Transform hard-stop: root cause found and fixed.** The rebuild did two non-original things:
  it zeroed velocity in `beginTransform`, and it locked all input during the fold. In WFC nothing on
  the transform path touches velocity, and no movement code checks IsTransforming. The target form
  becomes the movement form at the START of the fold (`BeginTransformation`). Robot→vehicle hands
  the velocity (≤35 m/s, `kMaxTransformSpeed`) and the rotation to the vehicle's rigid body
  (`TnVehicleForm.OnActivate`). Vehicle→robot keeps the velocity, and the robot's `CalcVelocity`
  preserves overspeed with the TruckTransformerMomentum InAir set while transforming.
  Verified on the real executable: running 14→15 m/s into the vehicle; cruising 15→14 m/s into the
  robot; boosting 30 m/s → robot 28.7 m/s after the fold, then a momentum run down to 14; strafe and
  diagonal folds continuous; standstill both ways clean.
- **"Missing fast movement" = the robot's real run speed.** `TnPawn.ApplyTransformer` applies the
  character definition (Optimus_ROBODEF): GroundSpeed 14 m/s, AccelRate 120 m/s², AirSpeed 12,
  AirControl 0.4, jump 5 m, collision r2.0 m. Passes 2/10 had used class defaults (5.5 m/s). WFC
  has no robot sprint key: full input runs at 14 m/s on the jog clips; partial pad input walks at
  ≥4.5 m/s; vehicle→robot carries vehicle/boost speed.
- **Fine aim (robot only):** RMB toggles, pad LT holds. Speed ×0.5 (7 m/s), FOV 80→45 in 0.1 s
  (exit 0.4 s), look speed ×0.5, spread ×0.5. Drops during reload and resumes. In the vehicle,
  RMB/LT = boost.
- **Camera from the robot strategy:** FOV 80, orbit 8 m, anchor 4 m, pitch ±75°, over-the-shoulder
  offset curve, basic camera collision, and traces aimed through the crosshair.
- **Truck:** boost 30 m/s / 0.5 s, hover 1.85 m.
- **Fixed pre-existing bug:** the ground/step query counted step-up and hover twice (robots stepped
  0.7 m; vehicles could snap onto decks 4.4 m above).
- Regression: jump 5.12 m, turn in place, recoil +12°, reload layer, the step A/B identical on 12
  routes; clean build.
- **Known issues:** foot slip is higher at the new speeds (≈1.1–1.8 m/s at 14 m/s, ≈1.6–2.0 m/s at
  7 m/s; no playback-rate scaling evidence); the vehicle hover/steering model is PROV; fine-aim
  target snap and fine-aim sounds are not implemented.

## FIDELITY PASS 10 (2026-10-01): LOCOMOTION BLEND FROM THE SHIPPED TREE; SPEED + STEP HEIGHT CONFIRMED
- **Moving state rebuilt to match Robot_ANIMTREE:** TnVelocityAnimBlend (450→1200 UU/s) mixes a
  walk and a jog TnStraferAnimBlend. Each strafer weights F/B/R/L by the travel direction relative
  to the facing, eased over `_BlendSpeed` 0.2. All 8 sequences are phase-locked like the "Strafers"
  AnimNodeSynch group (shared phase, highest-weight clip sets the rate). Idle↔Moving crossfade
  0.2 s (AmpCrossFadeCondition). Replaces "pick one F/B/L/R clip + 0.15 s crossfade".
- **Clip ground speeds measured:** walk ≈3.5 m/s, jog ≈12.1 m/s. The tree's 1200 UU/s MaxSpeed
  equals the jog's authored speed, so the blend is speed-matched by design.
- **Robot ground speed 5.5 m/s confirmed from script:** `TnPawn.PostBeginPlay` runs
  `_BaseGroundSpeed = GroundSpeed`, and `UpdateSpeeds` sets `GroundSpeed = _BaseGroundSpeed ×
  Π SpeedMultiplierFactors` (bytecode decoded). TnPlayerPawn GroundSpeed 550 × Ion Blaster
  GroundSpeedMultiplier 1.0. So the player's Moving state is ≈87% walk / 13% jog, as authored.
- **MaxStepHeight 35 UU (0.35 m) applied** (`TnRobotForm._MovementCapabilities`; was PROV 0.6).
  A deterministic A/B (`WFC_NOMOUSE`) on 12 routes shows no new snagging.
- Diagnostics: `WFC_NOMOUSE` (ignore live mouse in tests), `WFC_STEPUP=m` (A/B override).
- Regression: reload on the move, transforms both ways, turn in place, recoil +12°, jump 6.36 m,
  vehicle hover; clean build.

## FIDELITY PASS 9 (2026-10-01): AUTHORED AIM OFFSET PROFILE (TnAnimNodeAimOffset "Default")
- **The aim offset is now the shipped profile, not a pose-derived approximation.** The Ion Blaster
  uses the `Default` profile (selected by `WeaponTypeObserved`). It drives 11 bones (spine chain,
  head, both arms) with 9 cells each, baked from `Shooting_Aim_{L,F,R}_{D,C,U}`.
- **Bake rule cracked and verified:** for each bone/cell, rotation = Gp·(L_cell·L_centre⁻¹)·Gp⁻¹
  and position = Gp·(t_cell − t_centre), where Gp is the parent's model-space rotation in that
  cell. UE → glTF conversion: rotation `(−x,−z,−y,w)`, position `(x,z,y)·0.01`. Against the
  authored AimComponents: mean 0.01°, worst 0.07°, translations 0 mm (`work/pass8/verify_aim.js`).
  So the runtime bakes the profile from the clips (no asset data committed) and applies it the
  UE3 way: bilinear cells, each bone rotated/moved in model space, parent first.
- **Input ranges recovered:** profile H [−1,1] / V [−1,0.8]; RemapPawnAimRange from pawn aim
  (fraction of 90°) H [−1,0.85] / V [−0.7,1]. The remap is centre-preserving (PROV reading).
  Measured barrel pitch at aim −69/−34/0/+34/+69° → −42/−21/+3/+27/+50°: the gun trails the
  camera at the extremes, as the authored ranges imply.
- Turn-in-place yaw columns: the barrel stays within 5–13° of the aim while the legs lag up to 67°.
  During a pivot step the gun arm swings ≈50° and back. That swing is authored in
  `Nav_IdlePivot90_*` (root-discarded chest ±7°, forearm −50°), and the shipped tree layers the
  aim offset over it unchanged, so it is kept.
- Regression: recoil +12°, reload on the move, transforms both ways, jump 6.41 m; clean build.

## FIDELITY PASS 8 (2026-10-01): WEAPON RECOIL, TURN IN PLACE, FULL AIM GRID (shipped anim tree)
Gameplay agent. Evidence: shipped `TR_Shared_ANIMTREE_p.Robot_ANIMTREE` and class defaults, read
read-only from cooked packages (`work/pass8/dump_animtree.py` → `work/pass8/dump_*.json`).
- **Weapon recoil (was NOT YET):** a reconstruction of `HmSkelControlRecoil` (UE3
  `GameSkelCtrl_Recoil`), restarted per shot (`TnRecoiler`). Bones come from the tree's
  SkelControlLists: SpineRecoil → `C_Spine02_Lumbar02_XB`, RightHandRecoil → `R_Arm02_Shoulder_XB`.
  Ion Blaster values = `Default__TnWeaponMesh` archetype + IonBlaster_WEPMESH overrides. A/B
  (`WFC_NORECOIL`): during sustained fire the barrel climbs +12° (6.8° → 19.1°) with ±3° random yaw.
- **Turn in place (was NOT YET):** `TnAnimTurnInPlace` / `TnAnimTurnInPlaceRotator` ("UnwindLowerBody").
  Standing, the legs keep their world yaw while the torso follows the aim through the aim
  offset's L/R columns. At 22.5° short of a transition's 90°/180° (`TransitionThresholdAngle` 4096),
  `Nav_IdlePivot90_{L,R}` plays with root rotation discarded (`RRO_Discard`) and unwinds the
  offset along the clip's own root-yaw curve. Blend 0.1 s, abort after 50%. Verified: a slow pan
  holds the legs to 67°, then a 90° step returns the offset to ≈0; a fast pan (143°/s) chains pivots.
- **Aim offset is now the full 3×3 grid:** the yaw columns are calibrated from the poses
  (barrel L −90.7° / R +81.2°), and the inputs interpolate at the authored `InterpSpeed` 12.
- **Fixed (pre-existing):** robot→vehicle picked `Transform_ToVehicle_SuperBoost_Veh` (0.8 s) as
  the incoming clip. It now pairs `Transform_ToVehicle_VEH` by name (matched 1.97 s fold).
- Diagnostics: `WFC_AUTOTURN=rad/s`, `WFC_NORECOIL`, `WFC_LOGEVERY=N`; the frame log adds
  legYaw/aimYawN/turn/recoil.
- Regression: jump 6.39 m, transforms both ways, reload on the move, vehicle hover; clean build.

## FIDELITY PASS 7 (2026-10-01): ANIMATION LAYERS, MESH FACING CORRECTED, VEHICLE HOVER POSES
Gameplay agent (`agents/gameplay`). Verified by runtime screenshots + numeric logs (`work/pass7/`).
- **Mesh facing was 90° off; now fixed (+90°).** The authored straight-ahead aim pose
  `Shooting_Aim_F_C` points the Ion Blaster barrel along model **+X** (logged at load:
  `barrel dir (model space) 1.00 -0.06 -0.02`), UE's forward axis. Pass 6's `kMeshYawOffset=0`
  left the whole robot (and truck) side-on to the chase camera, with the gun 90° right of the
  reticle. Pass 6's `face·toCam` check only tested the yaw math, never the mesh. Restored
  `kMeshYawOffset=+π/2`. Now: the chase cam sees the robot's back and the truck's rear; the muzzle
  points along the aim yaw (`MUZZLE` log); `WFC_FACELOG` measures the real mesh +X.
- **Bone-space pose layering** (`assets::LocalPose`: sample / blend / mesh-space per-bone blend /
  additive / skin). Locomotion crossfades are now bone-space, not vertex lerps.
- **Upper-body aim offset (was NOT YET):** the authored `Shooting_Aim_F_{D,C,U}` poses, applied as
  a delta from F_C on the `C_Spine01_Lumbar01_XB` subtree, driven by camera pitch. The grid is
  calibrated from the poses' own barrel pitch (D −47.6°, C −3.3°, U +72.2°). Verified in profile
  at pitch −0.6/0/+0.6: the barrel measures ≈−27°/+7°/+42°.
- **Reload on the move (was PARTIAL):** the authored `Shooting_Reload_IonBlaster_ROBO` now plays as
  an upper-body slot over locomotion, blended in mesh space like UE3 `AnimNodeBlendPerBone`, so
  the torso stays forward over the strafe clips' turned hips. Full body when standing still.
- **Vehicle animation fixed:** moving used to loop `Nav_BoostToHover_VEH`, a one-shot
  transition. It now blends the authored directional poses `Nav_Hover_{Pose,F,B,L,R}_VEH` by local
  velocity, plus the additive `ADD_Nav_Hover_VEH` hover bob.
- **Vehicle turn rate applied:** the truck steers toward its travel direction at the recovered
  π rad/s (`AiMaxAngularSpeed`) instead of snapping.
- **Robot idle:** `NAV_Idle` (Optimus's own gameplay idle) replaces `Cust_Idle`, the
  customization-screen idle that was being picked as first-of-category. Take-off plays once;
  `Nav_Land` plays on touchdown after ≥0.3 s airborne.
- Regression: jump 6.36 m, transforms both directions, vehicle hover 2 m, no idle drift; clean build.
- New diagnostic: `WFC_FIXPITCH=rad` pins camera/aim pitch. The frame log adds yaw/aimW/aimN/reloadW.
- **Still open (gameplay):** ~~turn-in-place~~, ~~weapon recoil~~ (done in Pass 8), boost/dodge
  clips, camera tuning.

> **Integration note (integration/milestone-01):** Systems Passes 1–2 below describe the Systems
> branch's own upper-body slot, aim offset and recoil code. Gameplay Passes 7–9 implemented the same
> features independently, so the merged build keeps **Gameplay's** implementation
> (`Character.cpp` RobotRig, `Recoil.h`, recoil triggered once per shot from `PlayerController`).
> Systems' `AimOffset.h`, `updateUpperBody`/`evalLayered` and its second recoil trigger were not
> merged. Everything else in the Systems passes (animated weapon mesh, notifies, FX, SoundCues) is
> in the merged build. See INTEGRATION MILESTONE 01.

> **Integration note (integration/milestone-02):** the Systems sections below mention a temporary
> `Q` Dash binding and a Systems-run nitro timer. In the merged build, **Shift** is the only Dash
> binding (Gameplay, CONF). Gameplay's movement code owns the nitro/hover-dash timers, cooldowns and
> scales. `VehicleNitro` follows Gameplay's `vehicleState().nitroRemain` for RamFX, the nitro cues
> and the ram-hit registry. Boost presentation follows Gameplay's Driving state. See INTEGRATION
> MILESTONE 02.

## SYSTEMS NATIVE-RE UPDATE (2026-10-01)
- Ion Blaster cadence = original ~900 RPM: one-shot refire timer reset to 0 per shot, fires when elapsed
  > 0.065 s, overshoot discarded, max one shot per tick (standalone check: 900 shots/min). Experimental's
  923-RPM patch is NOT applied.
- Nitro: cooldown starts on activation [CONF]; ends on Boost release [CONF]; ram hits gated to one per target
  per nitro (`VehicleNitro::registerRamHit`, `World::notifyRamHit`); authored ram damage 175/300/175/300 and
  ExtraRamZVelocity 7000 UU/s exposed for Gameplay.
- Confirmed vehicle states (Boost LT/RMB, Hover Dash RB 3000 UU/s 0.5 s 2 s cooldown, Nitro RB while
  boosting) recorded in FIDELITY.md "Native RE confirmation".
- Gameplay handoff: `PlayerController` clears `wantFire_` after every fixed step but sets it once per render
  frame, so a frame that runs 2 steps drops the second step's shot (measured ~4.4 ticks/shot in-game vs 4.0).
  A held trigger should stay set for every step.

## SYSTEMS CHECKPOINT (2026-10-01) — end of round
- Branch `agents/systems`, clean build. Implemented this round: weapon layering / recoil / aim offset,
  animated Ion Blaster + notifies, weapon FX (muzzle, tracer, impact, shell, magazine, reload), weapon
  SoundCues, vehicle boost / hover / jump / ram FX, boost + nitro + engine / jump / land audio.
- Vehicle mechanics provenance (normal boost vs hover dash vs ram/nitro): FIDELITY.md "VEHICLE MECHANICS".
- Deliberately NOT done (Gameplay-owned): tire squeal (needs a lateral-slip signal), nitro camera change,
  nitro speed/steering scaling, hover dash, final Dash / RMB bindings (RMB: robot Fine Aim, vehicle Boost).

## SYSTEMS PASS 7 (2026-10-01) — VEHICLE ENGINE AUDIO
- Optimus's authored engine audio in vehicle form: off-load (idle/coasting) and on-load (throttle) drive
  loops, the jump-rev loop while airborne and the jump-start one-shot, and hover/wheels light/heavy landing
  cues by time in air; 0.2 s engine fades; all layers follow the mph speed parameter. The drive loop yields
  to the boost loop while boosting. Tire squeal not done (needs a slip signal from Gameplay).
- Read-only `PlayerController::throttleHeld()` added for the on/off-load choice.

## SYSTEMS PASS 6 (2026-10-01) — TRUCK NITRO / RAM (state, FX, audio)
- New abstract input action `Dash` (**PROV** temporary key **Q**). DASH while boosting on wheels starts
  the authored nitro: **3 s**, cooldown **8 s**; `RamFX` (rim-lit flame wedge on RamSocket) runs for its
  duration; `VEH_OPTIMUS_RAM_NITRO_START` + `VEH_TRUCK_RAM_ALERT` play at start. Releasing boost stops it.
- For Gameplay (read-only): `World::vehicleNitro()` -> `nitroActive()`, `ramActive()`, `timeRemaining()`,
  `cooldownRemaining()`, `speedScale()` (1.5 while active), `steeringScale()` (0.3 while active);
  `World::notifyRamImpact(pos)` plays the ram impact cue. Systems does **not** change speed or steering.
- Documented, not changed: Optimus's truck physics blueprints differ from the rebuild's dash values
  (HoverTruck_Physics DashSpeed 3000 / DashDuration 0.5 vs current 5000 / 0.3) — see FIDELITY PASS 10.
- Test: `WFC_STARTVEHICLE=1 WFC_AUTOBOOST=1 WFC_AUTODASH=1 WFC_BOOSTLOG=1` (NITRO lines).

## SYSTEMS PASS 5 (2026-10-01) — HOVER THRUSTERS + JUMP BOOSTERS
- Vehicle form now shows Optimus's authored hover thrusters (`CarHover_A_01_FX` on the six wheel
  HoverBooster sockets, with their socket scale): red light cones and orange rings looping, plus a
  spark / electro-ring / pulse burst whenever hover engages. Hover switches off while boosting (the truck
  drops to its wheels) and when transforming.
- Vehicle jumps fire `Jump_FX` on JumpBoostSocket_C/R/L: a 0.5 s burst of downward thruster cones,
  energon cones, glows, booster smoke, sparks and electro rings.
- `VehicleFx` replaces `VehicleBoostFx` as one data-driven system for boost / hover / jump.
- Verified: idle hover, boost (hover off, boost unchanged), jump take-off, transform out; robot weapon FX unchanged.
- PROV: light-cylinder intensity (DustPower 0.1 stands in for the volumetric shader), procedural spark
  texture, ring velocity reading. Open: drive/jump/land engine audio (RamFX: SYSTEMS PASS 6).

## SYSTEMS PASS 4 (2026-10-01) — VEHICLE BOOST PRESENTATION
- Holding boost in vehicle form now shows Optimus's authored afterburner (`bumble_boost_small1_FX` on
  BoostSocket_L/R, the two exhaust stacks): ignition burst of thruster cones + bullet cone + glow, then
  looping cones (3-4/s) and glows (20/s) attached in local space; release kills them (bKillOnDeactivate).
- Original boost audio: VEH_OPTIMUS_BOOST_START, the speed-driven VEH_OPTIMUS_BOOST_LOOP (5 looping
  layers, mph parameter), VEH_OPTIMUS_BOOST_END with the 0.15 s fade, and the 0.27 s grounded wheels peel-out.
- Verified: accelerating, stationary (against a wall), airborne after a vehicle jump, and transforming
  out while holding boost (effect + loop stop, END plays). Robot fire/reload FX unchanged.
- Diagnostics: `WFC_BOOSTLOG=1` (state, particle count, mph, socket positions). Test:
  `WFC_STARTVEHICLE=1 WFC_AUTOBOOST=1 [WFC_AUTOWALK=1]`.
- Open: ram FX, drive/jump/land engine audio (hover + jump FX: SYSTEMS PASS 5).

## SYSTEMS PASS 3 (2026-10-01) — SHELL, MAGAZINE AND RELOAD FX
- Every shot ejects the authored shell mesh (GrenadeAmmo_STAT) from ShellSocket with a vent smoke puff;
  the reload vents a blue flare + 0.75 s smoke stream at the muzzle (@0.034 s) and drops the Ion Blaster
  magazine mesh from MagSocket (@0.174 s, 3 s life) with a smoke puff. Values from the cooked
  ParticleSystems (FIDELITY PASS 7c); gravity/ground contact for the meshes is PROV (none authored).
- `WFC_ANIMLOG` now also prints live particle / mesh / impact counts.

## SYSTEMS PASS 2 (2026-10-01) — UPPER-BODY AIM OFFSET
- Robot_ANIMTREE `TnAnimNodeAimOffset` (profile Default, 11 bones x 9 authored rotations) now aims the
  spine, head and arms at the camera pitch, between locomotion and the reload slot (original tree order).
  Space/axes verified against the Shooting_Aim_* clips. Measured: aim 22.9 deg -> barrel 21.6-23.8 deg while
  jogging and firing; +0.9 / 0 / -0.9 rad screenshots show the gun raised / level / lowered.
- **Found, for Gameplay:** the robot mesh faces +X in model space but `kMeshYawOffset = 0` draws it as if it
  faced -Z, so Optimus renders 90 deg off the aim (barrel heading = aim - ~100 deg). Fix: `kMeshYawOffset = +pi/2`
  (evidence in FIDELITY PASS 7b). Not changed here (Gameplay-owned).
- Diagnostics: `WFC_AIMPITCH=<rad>` forces the aim pitch (camera untouched); `WFC_ANIMLOG` prints the aim
  values plus barrel pitch/yaw vs aim.

## SYSTEMS PASS 1 (2026-10-01) — WEAPON LAYERING, RECOIL, ANIMATED WEAPON, ORIGINAL FX + SOUNDCUES
Systems-agent branch `agents/systems`. All values recovered from cooked data (details and
confidence in FIDELITY.md PASS 7; decoders in `tools/systems/`).
- **Reload on the move**: reload plays in the original `UpperBodyCustom` slot masked from
  `C_Spine01_Lumbar01_XB` (AnimNodeBlendMultiBone), blend 0.1/0.1 s; the legs keep the strafe/jog
  clip (no more glide). Verified: base `Nav_StrafeJog_F` at 5.5 m/s + upper `Shooting_Reload_IonBlaster_ROBO`.
- **Recoil**: HmSkelControlRecoil port on SpineRecoil / RightHandRecoil with the Ion Blaster's
  authored RecoilDefs (restart per shot, decaying sinusoid in aim space).
- **Animated weapon**: the Ion Blaster is its 34-joint skeletal mesh playing its own
  Fire / Reload_AP / Idle anims with authored sockets; the muzzle is the MuzzleFlash socket.
- **Event timing**: weapon AnimNotifies drive reload/idle sounds at the authored times.
- **FX**: muzzle flash, tracer bolt + smoke trail, impact squib rebuilt from the cooked
  ParticleSystems (original textures, blend modes, bursts, lifetimes, sizes, velocities,
  colour/alpha/size curves, squib rules). Replaces the yellow line + box placeholder.
- **Audio**: original SoundCues: layered fire (near/mid/distant by distance), low-ammo, tail,
  impact, reload, idle; dB/semitone variation, timed events, concurrency, FMOD inverse rolloff.
- Diagnostics: `WFC_ANIMLOG` (base/upper/recoil/weapon clip), `WFC_NOTIFYLOG`, `WFC_CUELOG`.
- **Not done / handed off:** camera recoil + shake (camera owned by Gameplay), dry-fire trigger,
  mixer/reverb. The idle base clip
  `Cust_Idle` (showcase idle) should be `NAV_Idle`; that is Gameplay's locomotion selection.

## FIDELITY PASS 6 (2026-10-01) — INTERACTIVE PLAYER FIXES (orientation, locomotion, muzzle, reload, transform)
Runtime observation (replaying the exe) drove this pass, not headless smoke. Fixed, in the
player's priority order:
- **#1 Orientation/facing** — the robot now faces the **camera/aim (mouse) direction every
  frame** (`CharacterMovement`: robot `setYaw(faceYaw)` always; vehicle faces its travel dir).
  The old "only set yaw while moving" caused the body to **snap right** when you rotated the
  camera while standing then walked. Confirmed numerically: `face·toCam ≈ −0.9` (back to the
  chase cam). Mesh yaw offset is **0** (the earlier "90° bug" was a `WFC_FIXYAW` diagnostic
  artifact — the diagnostic set camYaw *after* faceYaw was latched; fixed its ordering too).
  WASD = move direction, mouse/camera = facing — the WFC strafe-shooter control model.
- **#2 Locomotion** — directional strafe clips: pick `Nav_Strafe{Jog,Walk}_{F/B/L/R}` by the
  travel direction **relative to facing** (dot of velocity with facing fwd/right), replacing the
  hardcoded `_F`. Verified: strafe-right → `Nav_StrafeJog_R`, forward → `_F`.
  (Upper-body aim-offset `Shooting_Aim_*` grid + start/stop/turn-in-place still PROVISIONAL.)
- **#3 Transformation** — robot and vehicle each carry a transform clip of **matching duration**
  (ToVehicle 1.97 s, ToRobot 1.13 s): one physical fold authored per mesh. Was played
  **sequentially** (robot fully → hard cut at full-fold mismatch → vehicle fully = the "crack",
  ~3.1 s). Now the outgoing mesh plays to the **midpoint**, then hands off to the partner mesh's
  clip **resumed at the same normalized time** — one continuous fold (verified frames 35→69→135:
  robot folds → matched mid-fold → clean vehicle). Weapon holstered for the whole transform.
  (Cross-mesh pop is minimised, not eliminated — true alpha cross-fade/visibility point is PROV.)
- **#4 Muzzle** — tracer + flash now originate at the **barrel tip** (weapon-local gltf
  (2.063, 0.017, 0.141), the frontmost vertex slice of `weapon.glb`), transformed by the weapon
  world matrix — **2.07 m forward** of the hand attach, so shots leave the gun, not the fist.
- **#5 Reload** — `Shooting_Reload_IonBlaster_ROBO` now plays (full-body, one-shot) while the
  weapon's reload timer runs. Verified pose + `reloading=1`. (WFC's additive `ADD_Shooting_
  Reload_*` upper-body overlay, to reload on the move, needs additive blending — PARTIAL.)
- Diagnostics added (env-gated): `WFC_AUTOSTRAFE/AUTOBACK`, `WFC_AUTORELOAD`, `WFC_FACELOG`,
  `WFC_MUZZLELOG`; frame log now prints `reloading`.
- **Still open:** #6/#7/#8 lighting cohesion (only coeff0 of 3 directional coeffs used; character
  lighting; materials), #10 camera tuning, and a magenta-material prop near spawn (nolightmap view).

## FIDELITY PASS 5 (2026-10-01) — BAKED LIGHTMAPS RECOVERED, DECODED, AND RENDERED
**Streets is now lit by its original authored baked lightmaps.** The licensee-144 lightmap
serialization was cracked: directional lightmap (3 coeff textures), texture = BE export index
into the ART export table (seekfree forward-export → `_LM` atlas), then CoordinateScale/Bias.
- `vs_lightmap.py` decodes all **1793 lightmapped components** → **1755/1952 props** mapped
  (atlas + CoordinateScale/Bias + HDR ScaleVector), 20 atlases (PNG via umodel, 1024² DXT1).
- `vs_map.py` emits TEXCOORD_1 + lightmap node extras → `world.glb`; renderer draws lightmapped
  submeshes unlit × atlas (UV1·scale+bias via texture matrix) × HDR ScaleVector (GL_COMBINE 4×).
  **1962 submeshes lit.** A/B: `WFC_NOLIGHTMAP=1`.
- Verified across spawn 0 / region 6 / region 18: per-region baked shadows + coloured bounce
  (purple/green/warm), bright lit doorways, high contrast — authentically WFC; no artefacts.
- Regression: collision 1.85 M tris, vehicle hover, robot, emissive glow all intact; exit 0.
- **Pipeline order:** vs_lightmap → vs_map → vs_collision (vs_map resets collision.glb).

## FIDELITY PASS 4 (2026-10-01) — lightmap atlases recovered; emissive glow applied
- **Lightmap ATLASES RECOVERED:** umodel decodes the 360-tiled `MP_IAC_Streets_ART_m_LM.xxx` →
  **78 LightMapTexture2D** (1024² PF_DXT1 RGB), preserved in `…/MP_IAC_Streets/lightmaps/`.
  Confirmed real baked lighting. **Component→atlas binding still BLOCKED** (seekfree GUID
  resolution; no instance imports; mixed-endian native block). Exact layout + blocker in
  FIDELITY.md. **No guessed lightmap mapping applied.**
- **EMISSIVE GLOW APPLIED:** Optimus's authored `*_emissive.png` glow masks are loaded and
  drawn as an additive self-illumination pass — blue glowing eyes/energon/Autobot vents (iconic
  WFC look), from original data. Robot/vehicle/weapon. Verified via screenshots.
- **Regression:** vehicle hover (Y −722.5 = ground+2 m) + cruise ~15 m/s + collision (1.85 M
  tris) + robot all intact after the renderer change; clean build, exit 0.

## FIDELITY PASS 3 (2026-10-01) — vehicle physics recovered; lightmaps investigated
- **Optimus VEHICLE PHYSICS RECOVERED** from `Default__TnHoverCarSimulationBlueprint`
  (TransGame.xxx) — WFC vehicles **hover**. Applied: max speed **15 m/s** (MaxLinearSpeed 1500),
  accel **30 m/s²** (3000), dash/boost **50 m/s** (DashSpeed 5000, 0.3 s burst), hover height
  **2 m** (SuspensionRadius 200), jump **12 m/s** (1200), turn ~π rad/s, terminal 80 m/s.
  Verified: vehicle floats 2 m above the street, cruises ~13.6→15 m/s. Boost on Sprint.
  (The Form CDOs only held `=1.0` modifiers; base values live on the *Simulation* blueprints.)
- **Baked lightmaps — investigated deeply, BLOCKED.** 78 LightMapTexture2D atlases exist;
  umodel can decode the 360 textures; meshes already carry TEXCOORD_1. Blocker: the per-
  component lightmap binding is native-serialized (licensee 144) with **GUID-based** texture
  refs (0 imports) and unresolved scale/bias float offsets. Precise findings in FIDELITY.md.
- **Robot movement sanity:** jump re-measured 6.41 m (analytically 6.25; no regression);
  GroundSpeed/accel/air/jump/capsule all intact.

## FIDELITY PASS 2 (2026-10-01) — recover from the executable + extend extraction
Ghidra/ReVa is live with `default.xex`; pawn/vehicle CDOs read from cooked packages
(`Game Dump/TransGame/CookedXenon/{TransGame,Engine}.xxx`) via `ue3pkg`+`props`.
- **Street collision RECOVERED.** props.json already carries authored block flags: **1693 of
  1952 props block**, 259 decorative don't. New `AssetTools/scripts/wfc/vs_collision.py`
  regenerates `collision.glb` = BSP + blocking volumes + **1693 blocking props** (instanced,
  5.46 MB → 1.85 M world tris). Player now stands on the street floor and is blocked by real
  building walls (previously walked through). Verified across 4 spawns (~180×164 m).
- **Robot movement RECOVERED** from `Default__TnPlayerPawn`/`Default__TnPawn`:
  GroundSpeed 5.5 m/s, AccelRate 20.48 m/s², AirControl 0.70, AirSpeed 15 m/s — all applied.
- **Jump/collision RECOVERED:** MaxJumpHeight 6.25 m (JumpZ derived 19.17 m/s; **measured
  6.39 m**), capsule radius 1.75 m / half-height 2.0 m (4 m tall), eye height 2.8 m.
- **Weapon socket rotation RECOVERED** (rotator yaw 172°/roll 30° → gltf matrix); applied.
- **Vehicle base physics**: still in a data asset (not parsed) — provisional.
- **Camera distance**: orbit-list driven (data asset not extracted) — provisional (9 m).
- **HeightFog RECOVERED**: LightColor (234,91,116) warm red-pink, Density 2e-5/UU, StartDistance
  2048 UU (from `MP_IAC_Streets_ART_m HeightFogComponent`) — applied (was guessed blue-grey).
- **Lightmaps**: 78 LightMapTexture2D in `MP_IAC_Streets_ART_m_LM.xxx`; per-component binding is
  native-serialized + 360-tiled textures — path documented in FIDELITY.md, #1 remaining visual gap.
- Fidelity table + provenance: `FIDELITY.md`.

## FIDELITY PASS 1 (2026-10-01) — recover original WFC behaviour from authored data
Evidence root: cooked UE3 config `ExtractedAssets/config/Coalesced_ini/.../Cooked/*.ini`,
map metadata `ExtractedAssets/maps/*.json`, asset metadata `VerticalSlice/**/*.json`.
- **Gravity [CONF]** −29.4 m/s² (WorldInfo.DefaultGravityZ −2940); vehicles −19.4 (RB ×0.66).
- **Transformation [CONF+fix]** now **crossfades** (blend-in 0.115 s / blend-out 0.25 s from
  TnTransformation); was a hard cut at every clip boundary → the jaggedness. Cross-skeleton
  mesh handoff is still an inherent cut.
- **Camera FOV [CONF]** 75° horizontal (Xe-TransCamera TnFovCameraBehavior), converted to
  vertical by aspect; was a guessed 70° vertical.
- **Ion Blaster [CONF]** damage 15, interval 0.065, mag 50, reserve max 250 confirmed;
  **fixed** initial reserve 150, reload 1.5 s, range 300 m + 1.0→0.5× falloff, per-shot spread.
- **Lighting/materials [fix, PROV]** washed-out cause = ambient floored brightness at ~0.70;
  lowered ambient + warm key + specular + exp2 fog. (Baked lightmaps not extracted.)
- **Audio [fix, PROV]** master 0.5 + 3D attenuation/pan (`playAt`/`setListener`); was full-
  volume mono.
- **Map [investigated]** Streets composed from all 3 sublevels (BSP + 1952 props). Gaps:
  static-mesh collision not extracted (confines walkable area), 16 PrefabInstances + HeightFog
  not composed, lightmaps not extracted. See FIDELITY.md.

## DONE (verified via screenshots / logs)
- **P1 Scale / placement / camera.** World is authored in **metres** (no global scale
  applied). Optimus ≈ 4.4 m. Huge ±4800 bounds were the **skydome**, not a unit mismatch.
  Player spawns at the authored FFA start (363.5, −723.6, −341.8), ground-snapped onto the
  collision surface, facing the play-area centroid. Third-person follow camera
  (dist 9 m, height 3.2 m, mouse look, near 0.1 / far 20000). **Framing bug fixed:** a
  leftover `cam.y < 0.5 → 0.5` clamp was launching the camera ~720 m into the sky at the
  map's negative Y; removed. Optimus is now clearly framed on the Streets.
- **P2 Collision.** Flat-ground hack removed. Loads `collision.glb` (3,495 tris) into a
  uniform XZ grid (2 m cells). Downward ground raycast + grounding + gravity; horizontal
  wall blocking via segment tests; respawn when falling below kill-Z. Auto-walk test: player
  walks across real geometry staying grounded at Y≈−724.5.
- **P3 Skeletal animation.** Full glTF skin path: 106-joint skeleton, inverse-bind
  matrices, JOINTS_0/WEIGHTS_0, animation samplers/channels (T/R/S), LINEAR/STEP/CUBICSPLINE
  interpolation, quaternion slerp, hierarchical node transforms, **CPU skinning** each step
  into a dynamic mesh. State machine (ROBOT: idle/walk/run/jump/fall; VEHICLE:
  idle/move) keyed off the **authoritative `extras.category`** strings in the GLBs.
  Additive (`ADD_`) clips are skipped for base poses. Debug log: form / anim name / anim time.
- **P4 Transformation.** ROBOT↔VEHICLE via the paired transform clips
  (`transform_to_vehicle` → handoff → `vehicle_transform_to_vehicle`, and the reverse).
  **Separate skeletons, no interpolation** between them; skeleton/mesh handoff at the end of
  the outgoing clip; input locked for the whole transition; settles into the new form's idle.
  Verified: ROBOT → Transform_ToVehicle_ROBO → VEHICLE → …_Veh → Nav_Hover_Pose_VEH.

- **P5 Materials / textures.** glTF materials parsed (baseColorTexture + baseColorFactor);
  meshes split into per-material submeshes with UVs on both the static baker (map) and the
  skinned loader (characters). PNGs decoded via a platform adapter (GDI+) behind
  `platform::decodeImage` → `render::ImageData` → `IRenderer::uploadTexture` (GL texture),
  cached by URI. Fixed-function GL modulates the texture by the directional+ambient light.
  Verified: Optimus red/blue paint + back wheel; Streets wear their rust/metal textures.
  24 textures loaded, 0 failed. (Normal/emissive/specular deferred — not straightforward in
  GL 1.1 fixed-function.)

- **P6 Ion Blaster.** `weapon.glb` loaded + textured, held at `WeaponSocket_Primary`
  (bone `R_Arm03_Elbow_XB`, node 42) — the weapon follows the bone's world transform each
  frame (robot form only). Hitscan while trigger held: 15 dmg, 0.065 s interval, 50-round
  mag, 250 reserve, auto-reload on empty + manual reload (R), ~1.8 s. Ray vs collision mesh +
  vs damageable targets (AABB). Yellow tracer (muzzle→impact) + muzzle-flash box. A practice
  **DamageTarget** dummy spawns ahead of the player. Verified: weapon in hands, tracer fires,
  ammo 50→25 under auto-fire.
- **P7 Audio.** `audio::IAudio` abstraction + Win32 `waveOut` polling mixer (decodes PCM WAV,
  nearest-resamples to 48 kHz stereo, mixes overlapping one-shot voices; silent no-op if no
  device). Cues loaded & wired by gameplay event (edge-detected in `World::tick`): fire
  (Ion Blaster), reload (gun foley), transform (gears), land (thump). Gameplay never touches
  Windows audio directly. Footsteps deferred (no clear footstep asset located).
- **P8 Dev tools.** Screenshot capture (kept). Runtime debug overlay toggle (B / env
  `WFC_DEBUGDRAW`): world/collision bounds, player capsule, aim ray, weapon-socket marker.
  Window-title HUD now shows FPS, position, speed, form, grounded/airborne, current anim +
  time, HP, ammo/reserve, reload + DBG state. STATUS.md maintained; headless verification
  knobs documented below.

## CURRENT
- P1–P8 complete and screenshot-verified. Polish candidates below.

## NEXT (polish, optional)
- Weapon socket rotation (currently translation-only offset; orientation is approximate).
- Walk clip picks a strafe-jog; prefer a forward locomotion clip by name.
- Normal/emissive/specular maps (needs a programmable path; GL 1.1 fixed-function can't).
- Footstep audio; smoother transform-handoff frame (config-driven fraction < 1.0).

## BLOCKERS
- None. (Headless smoke runs ~real-time, so long clips need proportionally more
  `WFC_SMOKE_FRAMES`; not a product blocker.)

## CONTROLS
- WASD move, mouse look, Space jump, LMB fire (hold = auto), RMB fine aim (robot, toggle) / boost (vehicle, hold),
  Shift dash (vehicle: hover dash; nitro/ram while boosting) [CONF], R reload (tap), F transform,
  C free/capture cursor, B debug overlay, Esc quit.

## ASSET PATHS (root = `F:/Transformers Rebuild/ExtractedAssets/VerticalSlice`, override `WFC_ASSETS`)
- Map:        `Maps/MP_IAC_Streets/world.glb`, `collision.glb`, `spawnpoints.json`
- Optimus:    `Characters/Optimus/robot.glb` (skin+75 clips), `vehicle.glb` (skin+13 clips),
              `character.json`
- (P6) Weapon: `Weapons/IonBlaster/…`

## DEV / TEST ENV VARS
- `WFC_SMOKE_FRAMES=N`  headless; run N frames then exit.
- `WFC_SHOT=path.bmp`   capture a screenshot on the final smoke frame.
- `WFC_DEBUGCAM=front|top`  diagnostic camera + a bright beacon on the player.
- `WFC_AUTOWALK=1`      hold Forward (scripted locomotion test).
- `WFC_AUTOFIRE=1`      hold the trigger (scripted weapon test).
- `WFC_AUTOTRANSFORM=F` trigger a transform at frame F.
- `WFC_DEBUGDRAW=1`     enable the debug overlay from start (same as toggling B).
- `WFC_FIXYAW=rad` / `WFC_FIXPITCH=rad`  pin the camera (= aim) yaw / pitch.
- `WFC_STARTVEHICLE=1`  start in vehicle form. `WFC_AUTOSTRAFE/AUTOBACK/AUTORELOAD/AUTOJUMP=1`
  scripted inputs; `WFC_FACELOG`/`WFC_MUZZLELOG` facing/muzzle diagnostics.
- `WFC_ASSETS=dir`      override the asset root.
- `WFC_ANIMLOG=1` / `WFC_NOTIFYLOG=1` / `WFC_CUELOG=1` / `WFC_BOOSTLOG=1`  weapon layering / AnimNotify / SoundCue / vehicle boost logs.
