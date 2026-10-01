# WFC Rebuild — Playable Vertical Slice Status

_Updated as work proceeds. Build: `powershell -ExecutionPolicy Bypass -File build.ps1`
→ `build/bin/wfc_rebuild.exe`. Fidelity audit + provenance: `FIDELITY.md`._

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
- WASD move, mouse look, Space jump, LMB fire (hold = auto), RMB fine aim (robot, toggle) / boost (vehicle, hold), R reload, F transform,
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
