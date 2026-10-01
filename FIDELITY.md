# WFC Reconstruction — Fidelity Audit

**Principle:** the original Xbox 360 build is the specification. Recover what WFC actually
did; never let a provisional rebuild constant masquerade as original behaviour.

**Source-of-truth order:** (1) observed original behaviour · (2) `default.xex` code/constants ·
(3) authored cooked data (packages, `Coalesced_*` `.ini`) · (4) extracted metadata ·
(5) current rebuild · (6) guesswork.

**Evidence roots:**
- Cooked UE3 config (decompiled Coalesced): `ExtractedAssets/config/Coalesced_ini/TransGame/Config/Xenon/Cooked/*.ini`
- Per-sublevel map metadata: `ExtractedAssets/maps/*.json`
- Extracted asset metadata: `ExtractedAssets/VerticalSlice/**/{character,weapon,map}.json`
- Executable: `Game Dump/default.xex` (via Ghidra/ReVa) — for compiled class defaults.

Legend — CONFIDENCE: **CONF**(irmed from authored data/exe) · **HI** · **MED** · **LOW/GUESS**.

---

## PASS 8 — RECOIL, TURN IN PLACE, SHIPPED ANIM TREE (2026-10-01, gameplay agent)

Primary evidence: `TR_Shared_ANIMTREE_p.Robot_ANIMTREE` (cooked in `MP_IAC_Streets_BASE_m.xxx`)
plus class defaults in `TransGame.xxx` / `HM_Engine.xxx`, read with AssetTools `ue3pkg`/`props`
(read-only; dumps in `work/pass8/`).

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Upper-body blend root | `AnimNodeBlendMultiBone` start bone `C_Spine01_Lumbar01_XB` (plus a left-arm branch at `L_Arm01_Clav_XB`) | Robot_ANIMTREE | CONF | Matches the Pass 7 mask root (PROV → **CONF**). |
| Recoil bones | RightHandRecoil → `R_Arm02_Shoulder_XB`; LeftHandRecoil → `L_Arm01_Clav_XB`; SpineRecoil → `C_Spine02_Lumbar02_XB` | Robot_ANIMTREE `SkelControlLists` | CONF | **APPLIED** (spine + right hand; the Ion Blaster defines no left-hand recoil). |
| Recoil values | Spine: 0.8 s, RotAmp [500,1000,0], Freq [10,10,0], all ERS_Zero. Hand: 0.5 s, RotAmp [2000,500,−2000], Freq [15,10,10], Yaw ERS_Random, LocAmp X −8 UU @10 | `Default__TnWeaponMesh` archetype + IonBlaster_WEPMESH overrides; struct default `Default__HmSkelControlRecoil` (0.33 s, zeros) | CONF | **APPLIED** (`game/Recoil.h`), restarted per shot (`TnRecoiler`). |
| Recoil update law | sin-wave per axis × smoothstep(TimeToGo/Duration), mesh-space in the aim frame (`bBoneSpaceRecoil` false) | UE3 GameSkelCtrl_Recoil (reconstructed; native not decompiled) | MED | **APPLIED**. UE rotator → model-axis mapping (yaw/roll sign) PROV. Measured: +12° muzzle climb under sustained fire. |
| Turn-in-place values | Threshold 4096 UU (22.5°), TransitionBlendTime 0.1 s, PercentageToAllowAbort 0.5, RotTransitions ±16384/±32768; player `RRO_Discard` | `Default__TnAnimTurnInPlace`, `Default__TnAnimTurnInPlacePlayer` | CONF | **APPLIED**. |
| Turn clips | Rt_90/Rt_180 → `Nav_IdlePivot90_R`, Lt_90/Lt_180 → `Nav_IdlePivot90_L` (WS_ONE_HANDED) | TnWeaponAnimChooser in Robot_ANIMTREE | CONF | **APPLIED**. Clips author ~81° on `C_Root_Reference_XR` over 0.53 s. |
| Turn trigger / unwind | Inferred: legs planted (UnwindLowerBody), trigger when \|offset\| ≥ RotationOffset − threshold, unwind RotationOffset along the clip's root-yaw curve | Field names + values; native logic not recovered | PROV | **APPLIED**. Chosen over "trigger at 22.5° and unwind the offset" because it keeps the feet consistent with the authored step. |
| Aim offset yaw columns | Leg/aim difference feeds the aim offset horizontally (`TnAnimNodeAimOffset.TurnInPlaceOffset`) | TransGame class layout | HI | **APPLIED**: 3×3 grid, columns calibrated L −90.7° / R +81.2°. |
| Aim offset interpolation | InterpSpeed 12 | `Default__TnAnimNodeAimOffset` | CONF | **APPLIED** (FInterpTo-style). |
| Aim offset authored profile | `TnAnimNodeAimOffset` "Default" profile: baked per-bone quaternions (AimComponents), VerticalRange [−1,0.8], PawnAimOffsetRange H [−1,0.85] V [−0.7,1] | Robot_ANIMTREE | CONF (data) | NOT YET: still pose-derived deltas. Converting the baked UE-space quaternions is the next fidelity step. |
| Incoming transform clip | Matched pair `Transform_ToVehicle_ROBO` ↔ `Transform_ToVehicle_VEH` | clip durations (1.967 s both) | HI | **FIXED** (was SuperBoost_Veh, 0.8 s). |

---

## PASS 7 — ANIMATION LAYERS & MESH FACING (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Mesh forward axis | Skeletal meshes face model **+X** (UE convention; umodel `(x,z,y)` keeps X). | `Shooting_Aim_F_C` barrel dir in model space = (1.00,−0.06,−0.02) | CONF | **FIXED** `kMeshYawOffset=+π/2` (Pass 6's 0 left robot/truck side-on, gun 90° off the reticle). Verified: chase cam sees back/rear; muzzle along aim yaw. |
| Upper-body aim offset | `Shooting_Aim_*` 9-pose grid points the gun at the aim. | `robot.glb` category `aim`; weapon.json `owner_animations.aim` | HI | **APPLIED**: F column (body faces aim yaw → yaw offset 0), delta from F_C on `C_Spine01_Lumbar01_XB` subtree, driven by controller pitch. |
| Aim pitch → grid mapping | UE3 AimOffset normalized pitch range (exact range not recovered) | Calibrated from the poses: barrel pitch D −47.6° / C −3.3° / U +72.2° | CONF-derived | **APPLIED**: piecewise-linear so barrel pitch ≈ aim pitch (measured −27/+7/+42° at −34/0/+34°). |
| Reload while moving | Owner reload clip over locomotion | weapon.json `owner_animations.reload_robot` = `Shooting_Reload_IonBlaster_ROBO` | HI (clip) / PROV (mask) | **APPLIED**: upper-body slot (mask root `C_Spine01_Lumbar01_XB` PROV), full body when standing, mesh-space per-bone blend (UE3 `AnimNodeBlendPerBone` default). |
| Slot / aim / additive ease time | — | — | GUESS | `kSlotBlend=0.15 s` **PROVISIONAL**. |
| Vehicle move animation | Directional single-frame hover poses + additive hover bob | `vehicle.glb` `Nav_Hover_{Pose,F,B,L,R}_VEH`, `ADD_Nav_Hover_VEH` (additive=true) | HI (clips) / PROV (weights) | **FIXED** (was looping the one-shot `Nav_BoostToHover_VEH` transition). Weights = local velocity / MaxLinearSpeed per axis. |
| Vehicle turn rate | **~π rad/s** | `TnHoverCarSimulationBlueprint.AiMaxAngularSpeed` | CONF | **APPLIED** (was instant snap). |
| Robot gameplay idle | Character-specific nav idle | `NAV_Idle` from `Optimus_ROBO_ANIM`; `Cust_Idle` = customization screen | MED | **FIXED** (was `Cust_Idle`, first of category). |
| Jump / land | Take-off once → descent loop → land | `Nav_TakeOff_01`, `Nav_Jump_Descent`, `Nav_Land` | MED | **APPLIED**: take-off non-looping; land after ≥0.3 s airborne (`kLandMinAirTime` PROV), skipped when moving. |
| Weapon recoil | see Pass 8 | weapon.json + archetype | CONF | **APPLIED in Pass 8**. |
| Turn in place | see Pass 8 | Robot_ANIMTREE | CONF | **APPLIED in Pass 8**. |

Additive-clip convention: `ADD_*` clips are stored as **deltas** (identity-ish quats, zero
translations at rest), so `samplePose(additive=true)` starts from identity and `addPose` composes
`base * delta` in bone-local space.

---

## PASS 6 — PLAYER-CONTROL, ANIMATION & WEAPON PRESENTATION (2026-10-01)
Driven by replaying the exe (runtime observation overrides headless smoke). Priority order as
the player reported it.

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Body facing | Robot faces the **aim/camera (mouse) yaw every frame**; WASD = move dir relative to it (strafe shooter). User confirmed: "camera facing = character facing; WASD = move direction." | Observed original + user | HI | **APPLIED** — `CharacterMovement` robot `setYaw(faceYaw)` always; removed the stand-then-walk snap. `face·toCam≈−0.9` verified. |
| Mesh yaw offset | ~~Extracted meshes align to the rebuild's −Z-forward yaw with no extra rotation.~~ **Superseded by Pass 7:** meshes face model +X; offset is +90°. | ~~Runtime geometry check~~ (only tested yaw math) | — | ~~`kMeshYawOffset=0`~~ → **+π/2** (Pass 7). |
| Vehicle facing | Faces its **travel direction** (steering), not the aim. | Observed | MED | **APPLIED** `yaw=atan2(-vx,-vz)` when moving. |
| Directional locomotion | `Nav_Strafe{Jog,Walk}_{F/B/L/R}` chosen by travel dir **relative to facing**. | `robot.glb` clip set (category `run`/`walk`) | HI | **APPLIED** (dot of velocity with facing fwd/right). Verified strafe-R → `Nav_StrafeJog_R`. |
| Upper-body aim offset | `Shooting_Aim_{F/L/R}_{C/D/U}` 9-pose grid points the gun at the reticle. | `robot.glb` category `aim` | — | ~~NOT YET~~ **APPLIED in Pass 7**. |
| Transform pairing | Robot & vehicle transform clips share a duration (ToVehicle **1.97 s**, ToRobot **1.13 s**) — one fold authored per mesh, played **in sync**. | `robot.glb`/`vehicle.glb` clip durations | HI | **APPLIED** — outgoing mesh → midpoint → partner mesh resumed at same normalized time (was sequential = the "crack"). Weapon holstered through the fold. |
| Transform cross-fade point | exact visibility/alpha handoff curve | — | GUESS | `kTransformHandoffFrac=0.5` **PROVISIONAL**; cross-mesh pop minimised, not removed. |
| Muzzle origin | Ion Blaster **barrel tip** (MuzzleFlash socket). Socket transform not extracted; used geometric tip. | `weapon.glb` frontmost vertex slice | CONF-derived | **APPLIED** — weapon-local (2.063,0.017,0.141) m → +2.07 m forward of the hand; tracer+flash leave the barrel. |
| Reload anim | `Shooting_Reload_IonBlaster_ROBO` (full-body); `WeaponReloadAnimTime` 1.5 s (clip 1.633 s). | `robot.glb` category `reload`; `weapon.json` | CONF | **APPLIED** full-body one-shot while reloading. Additive `ADD_Shooting_Reload_*` (reload on the move) = PARTIAL. |

**Lighting (#6/#7/#8) — deferred to a dedicated shader pass.** World still uses only **coeff0**
of the 3-coefficient directional baked lightmap (fixed-function can't apply the directional basis
per-pixel); character uses a provisional `GL_LIGHT0` not sampled from the world. True fidelity
(3 coeffs · normal, gamma/sRGB, env-probe character lighting) needs a GL2+ shader path — a large,
isolated effort, not cut into this pass to avoid leaving the build broken.

---

## PARAMETER TABLE (recovered so far)

| Property | Original value | Source | Conf | Rebuild status |
|---|---|---|---|---|
| World gravity (pawn) | DefaultGravityZ **−2940 UU/s²** = −29.4 m/s² | `Xe-TransGame.ini [Engine.WorldInfo]` | CONF | **APPLIED** (was 22) |
| RB physics gravity scale (vehicles) | **0.66** → −19.4 m/s² | `Xe-TransGame.ini [Engine.WorldInfo] RBPhysicsGravityScaling` | CONF | applied to vehicle form |
| Transformation blend-in | **0.115 s** | `Xe-TransGame.ini [TransGame.TnTransformation] _BlendInTime` | CONF | **APPLIED** (crossfade) |
| Transformation blend-out | **0.25 s** | `Xe-TransGame.ini [TransGame.TnTransformation] _BlendOutTime` | CONF | **APPLIED** (crossfade) |
| Camera default FOV | **75° horizontal** (SmoothTime 0.4) | `Xe-TransCamera.ini [AnimatedFovCameraBehavior TnFovCameraBehavior] DefaultFOV` | CONF | **APPLIED** (h-FOV→v-FOV by aspect) |
| Fine-aim (ADS) FOVs | 35 / 45 / 55 (close/med/far POI) | `Xe-TransGame.ini [TnPointOfInterest]` | CONF | not yet (no ADS mode) |
| Fine-aim ground-speed mult | **0.5×** | `Xe-TransGame.ini [TnFineAimManager] _GroundSpeedMultiplier` | CONF | not yet |
| Camera pawn-cylinder padding | R=30, H=30 UU | `Xe-TransCamera.ini [TnCamera]` | CONF | n/a (no camera collision yet) |
| Camera pawn fade start | 200 UU | `Xe-TransCamera.ini [TnCamera] PawnFadeStartDistance` | CONF | not yet |
| Ion Blaster fire interval | **0.065 s** | `weapon.json gameplay.FireIntervalModifier` | CONF | correct ✓ |
| Ion Blaster damage | **15** (InstantHit) | `weapon.json gameplay.InstantHitDamage` | CONF | correct ✓ |
| Ion Blaster magazine | **50** | `weapon.json MaxAmmoClipCount` | CONF | correct ✓ |
| Ion Blaster max reserve | **250** | `weapon.json MaxAmmoCount` | CONF | correct ✓ |
| Ion Blaster initial reserve | **150** | `weapon.json InitialReserveAmmoCount` | CONF | **FIXED** (was 250) |
| Ion Blaster reload time | **1.5 s** | `weapon.json WeaponReloadAnimTime` | CONF | **FIXED** (was 1.8) |
| Ion Blaster range | **30000 UU = 300 m** | `weapon.json WeaponRange` | CONF | **FIXED** (was 400) |
| Ion Blaster damage falloff | 1.0× ≤5000 UU → 0.5× @30000 UU | `weapon.json RangeDamageModifiers` | CONF | **APPLIED** |
| Ion Blaster per-shot spread | 0.08→0.18, +0.005/shot, 2.0 s cooldown | `weapon.json PerShotSpreadModifier` | CONF | **APPLIED** |
| Ion Blaster fine-aim spread | 0.5× | `weapon.json FineAimSpreadModifier` | CONF | not yet (no ADS) |
| Ion Blaster equip/putdown | 0.2 / 0.5 s | `weapon.json EquipTime/PutDownTime` | CONF | not yet |
| Weapon socket | WeaponSocket_Primary, bone R_Arm03_Elbow_XB | `character.json sockets` | CONF | attached (rotation approx) |
| Robot ground speed | **550 UU/s = 5.5 m/s** | `TransGame.xxx Default__TnPlayerPawn.GroundSpeed` | CONF | **APPLIED** (was 8) |
| Robot accel | **2048 UU/s² = 20.48 m/s²** | `Engine.xxx Default__Pawn.AccelRate` | CONF | **APPLIED** (was 60) |
| Robot air control | **0.70** | `Default__TnPlayerPawn.AirControl` | CONF | **APPLIED** (was none) |
| Robot air speed | **1500 UU/s = 15 m/s** | `Default__TnPlayerPawn.AirSpeed` | CONF | **APPLIED** |
| Robot max jump height | **625 UU = 6.25 m** | `Default__TnPawn._WorkingMovementCapabilities.MaxJumpHeight` | CONF | **APPLIED** (JumpZ derived = √(2·g·h) = 19.17 m/s; measured 6.39 m) |
| Pawn cylinder radius | **175 UU = 1.75 m** | `Default__TnPawn._WorkingMovementCapabilities.CylinderRadius` | CONF | **APPLIED** (capsule + wall probe) |
| Pawn cylinder half-height | **200 UU = 2.0 m** (full 4.0) | `…CylinderHeight` | CONF | **APPLIED** |
| Pawn base eye height | **80 UU = 0.8 m** (above centre → 2.8 m above feet) | `Default__TnPawn.BaseEyeHeight` | CONF | **APPLIED** (camera pivot) |
| Robot walk pct | 0.5 (walk = 0.5× ground) | `Engine.xxx Default__Pawn.WalkingPct` | CONF | n/a (keyboard jogs) |
| Robot max fall speed | uncapped in movement | `…_WorkingMovementCapabilities.MaxFallSpeed=100000` | CONF | no terminal cap |
| HeightFog colour | **(234,91,116)** warm red-pink | `MP_IAC_Streets_ART_m HeightFogComponent.LightColor` | CONF | **APPLIED** (was blue-grey) |
| HeightFog density / start | 2e-5/UU (0.002/m) / 2048 UU | `…HeightFogComponent.Density/StartDistance` | CONF | APPLIED (GL_EXP, softened) |
| Vehicle max speed | **1500 UU/s = 15 m/s** | `Default__TnHoverCarSimulationBlueprint.MaxLinearSpeed` | CONF | **APPLIED** (was guessed 32) |
| Vehicle acceleration | **3000 UU/s² = 30 m/s²** | `…MaxLinearAcceleration` | CONF | **APPLIED** |
| Vehicle dash/boost speed | **5000 UU/s = 50 m/s** | `…DashSpeed` | CONF | **APPLIED** (Sprint; burst over DashDuration) |
| Vehicle dash duration | **0.3 s** | `…DashDuration` | CONF | APPLIED (dash accel = 50/0.3) |
| Vehicle hover height | **200 UU = 2.0 m** | `…SuspensionRadius` | CONF | **APPLIED** (vehicle floats above terrain) |
| Vehicle jump speed | **1200 UU/s = 12 m/s** | `…JumpLinearSpeed` | CONF | APPLIED |
| Vehicle turn rate | **~π rad/s** (180°/s) | `…AiMaxAngularSpeed` | CONF | **APPLIED** (Pass 7: yaw steers toward travel dir at π rad/s) |
| Vehicle terminal velocity | 8000 UU/s = 80 m/s | `Default__TnTransformer.TerminalVelocity` | CONF | n/a (fall uncapped) |

---

## STREET COLLISION (RECOVERED this pass)
- **Authored collision participation recovered from props.json:** of 1952 placed static
  meshes, **1693 BLOCK** (CollideActors+BlockActors true) and **259 do not** (decorative:
  deco spheres, etc.). These flags come from the original ART/BASE actor/component properties.
- **UE3 collision model:** the player cylinder (non-zero extent) collides against each static
  mesh's collision. Default `UseSimpleBoxCollision=true` routes the player to the mesh's
  simplified BodySetup collision; architectural meshes commonly set per-poly. We reproduce the
  **per-poly path** (render geometry of blocking props) — faithful for the structural
  floors/walls/ramps that use it, an over-approximation for small simple-collision props.
- **Extraction extended:** new `AssetTools/scripts/wfc/vs_collision.py` regenerates
  `collision.glb` = pristine base (BSP-solid + blocking-volume hulls, kept as
  `collision_base.glb`) **+ 1693 blocking props** (instanced; file 5.46 MB → ~1.85 M world
  collision tris baked at load). Non-colliders excluded; oversize guard (>2000 m) excludes
  skydome/background shells (0 hit).
- **Verified:** player stands on the street floor at the authored FFA spawn (Y −724.5) and at
  4 spawns spanning ~180×164 m; auto-walk is now correctly **blocked by building walls** it
  previously passed through. Collision bounds Y [−800..−329].
- **Remaining:** simple-collision (BodySetup convex/box) extraction for props that use it;
  vehicle RB collision channel; moving-platform (InterpActor) dynamic collision (baked at rest
  position); exact MaxStepHeight/slope (pawn exe defaults).

## MAP / WORLD (investigated)
- `world.glb` is composed from **all three sublevels** (ART/AUDIO/BASE): BSP 2460 tris
  (ART only) + **1952 placed static meshes** + spawns/objectives. It is NOT a thin subset.
- Collision extent spans ~740 m (X) × 451 m (Z) in gltf metres (BSP-solid + blocking-volume
  convex hulls). Auto-walk forward from the FFA spawn is blocked by a wall after ~23 m — a
  building/volume ahead, not the map edge.
- **Known gaps (why it can look smaller/simpler than the original):**
  1. **Static-mesh collision not extracted** (`collision.json` note) — the 1952 prop meshes
     have no collision, so floors/ramps built from static meshes are not walkable and some
     structures are pass-through. Highest-value map task: build collision from prop render
     meshes where `collide/block=true` (data is in `props.json`).
  2. **PrefabInstance (16)** and **HeightFog** actors are not composed into `world.glb`
     (unhandled in `vs_map.py`). Prefabs hold modular set-dressing; missing them thins detail.
  3. Lighting is **baked into lightmaps** (StaticLightCollectionActor) which were **not
     extracted** — the single biggest reason the scene reads flatter than the original.

## LIGHTING / MATERIALS (investigated + partially fixed)
- Washed-out cause identified: the fixed-function renderer used global-ambient 0.35 + light-
  ambient 0.35, flooring every surface at ~0.70 brightness (no contrast). **Fixed [PROV]:**
  ambient lowered (global 0.14 / light 0.10), warm directional key, separate **specular**
  term (metallic highlights).
- **HeightFog RECOVERED [CONF]** from `MP_IAC_Streets_ART_m HeightFogComponent_11010`:
  LightColor **(234,91,116)** warm red-pink, Density **2e-5/UU = 0.002/m**, StartDistance
  2048 UU. Applied (GL_EXP, density softened to 0.0014/m for the play-area scale; clear colour
  warmed to match). This replaced the earlier **guessed blue-grey** fog.
- **BAKED LIGHTMAPS — FULLY RECOVERED, DECODED, AND RENDERED [CONF].** Streets is now lit by
  its original authored baked lightmaps instead of the stand-in directional/ambient.
  - **Serialization CRACKED (file_version 511, licensee 144).** After a component's tagged
    props the native `FStaticMeshComponentLODInfo` block is **big-endian**:
    `… [LightMapType=2] [LightGuids.Num: byte] [LightGuids: Num × FGuid(16)]`
    `[lead int32=0] 3 × ([tex: BE int32 export index][ScaleVector: 3 BE floats][1.0])`
    `[CoordinateScale: 2 BE floats] [CoordinateBias: 2 BE floats]`.
    It is a **directional lightmap** (3 coefficient textures); LightGuids[0] is a shared
    dominant-light GUID. The **texture is a positive export index into the ART package's own
    export table** (a seekfree forward-export whose name matches the `_LM` atlas) — this solved
    the "no imports / GUID" blocker.
  - **Values recovered:** per prop instance — atlas (coeff-0 `LightMapTexture2D`),
    CoordinateScale (e.g. 0.0625 = 1/16), CoordinateBias, and the coeff-0 ScaleVector (HDR).
    `AssetTools/scripts/wfc/vs_lightmap.py` parses all **1793 lightmapped components** and joins
    them to props (**1755/1952 props → lightmap**, 20 atlases used). Atlases decoded to PNG via
    umodel (1024² DXT1) in `…/MP_IAC_Streets/lightmaps/`.
  - **Rendering:** `vs_map.py` now emits TEXCOORD_1 + per-instance lightmap node extras;
    `world.glb` carries them; the loader attaches them to submeshes; the renderer draws
    lightmapped submeshes **unlit** then **multiplies by the atlas** (blend DST_COLOR·ZERO,
    UV1 via a texture matrix = uv1·CoordinateScale + CoordinateBias), with the **HDR ScaleVector
    applied via GL_COMBINE RGB_SCALE 4×**. 1962 submeshes lit. A/B toggle: `WFC_NOLIGHTMAP=1`.
  - **Verified** across spawn 0 / region 6 / region 18: correct per-region baked shadows and
    coloured bounce (purple, green/teal, warm), high contrast, bright lit doorways; no wrong-
    atlas / UV-flip / seam artefacts. Colour space: textures sampled as-is (sRGB-ish), modulate
    then HDR-scale — matches WFC's moody baked look.
  - **Remaining [PROV]:** only coeff-0 of the 3 directional coefficients is used (no per-pixel
    normal reconstruction — a shader refinement); ScaleVector >4 clamps (rare); BSP surfaces
    have no static-mesh lightmaps (BSP lightmaps are a separate path, not yet done).
  - **Pipeline ordering:** run `vs_lightmap.py` → `vs_map.py` (writes world.glb + base
    collision.glb) → `vs_collision.py` (restores full prop collision). vs_map resets
    collision.glb, so vs_collision must run last.
- **Emissive RECOVERED + APPLIED [CONF]:** Optimus's authored emissive textures
  (`textures/*_emissive.png`, the glow mask — blue optics/energon/Autobot vents on black) are
  loaded (derived as the `_basecolor`→`_emissive` sibling) and drawn as an **additive
  self-illumination pass** (GL_ONE/GL_ONE, unlit, depth-write off). Result: Optimus's eyes and
  energon details glow blue (iconic WFC look), from original data, not invented. Applies to
  robot/vehicle/weapon. [PROV] emissive *intensity* scale not recovered (drawn at 1×).
- Still missing: **normal/specular maps** (roles extracted; need a programmable GL path),
  map/environment emissive (different naming), tone-mapping / bloom / DOF.

## CONFIRMED ORIGINAL (authored data)
- Streets map = three sublevels **BASE** (gameplay: 24 FFA + 60 team starts, 58 blocking
  volumes, pickups, objectives) + **ART** (visual: BSP 2460 tris, 34 StaticMeshActors,
  16 PrefabInstances, 25 decals, HeightFog) + **AUDIO** (40 AmbientSound + 30 Hm spatial
  emitters). Source: `maps/MP_IAC_Streets_*_m.json`.
- Gravity, transform blend times, camera FOV, Ion Blaster stats — see table.

## HIGH-CONFIDENCE RECONSTRUCTION
- GLB skinning / animation sampling; material base-colour; collision ground query.

## PARTIALLY CONFIRMED
- Vehicle movement: forms (TnTruckForm/TnCarForm) are **modifier** layers (=1.0) over
  compiled base values; WFC vehicles use RB thruster/suspension/hover physics. Base
  constants live in `default.xex` — NOT yet recovered.
- Map completeness: 1952 props + BSP composed, but **PrefabInstance (16)** and **HeightFog**
  not yet composed; **static-mesh collision not extracted** (only BSP-solid + blocking hulls).

## WEAPON SOCKET (RECOVERED this pass)
- `WeaponSocket_Primary` relative transform (character.json): loc_ue [-40,0,0],
  rot_ue [pitch 0, yaw 31311 = 172°, roll 5461 = 30°]. Converted to a gltf-space socket
  matrix (via `vs_common.ue_rot`/`ue_to_gltf_matrix`) and applied — the Ion Blaster now holds
  with the correct orientation (barrel forward along the arm), not translation-only.

## CAMERA (partial)
- Recovered: pivot/eye height 2.8 m [CONF] (CollisionHeight 200 + BaseEyeHeight 80 UU).
  Flying-cam pitch range ±60°, first-person rotation ±45°, flying AnchorOffset [0,0,220 UU].
- STILL [PROV]: ground third-person **follow distance** — WFC selects it from an orbit-distance
  list (`TnLocationOffsetCameraBehavior.CurrentOrbitDistanceIndex`); the distances live in a
  camera data asset / `DefaultCamera.ini` that was not extracted. Keeping 9 m. Mouse
  sensitivity, ground-cam pitch limits still provisional.

## VEHICLE PHYSICS (RECOVERED this pass)
- WFC ground vehicles **hover**; Optimus's truck uses **`Default__TnHoverCarSimulationBlueprint`**
  (TransGame.xxx). The `TnCarForm`/`TnTruckForm` CDOs only carry `=1.0` modifiers; the base
  values live on the *Simulation* blueprint classes (which I'd missed earlier). Recovered +
  applied: max speed 15 m/s, accel 30 m/s², dash/boost 50 m/s (0.3 s burst), hover height 2 m,
  jump 12 m/s, turn ~π rad/s, terminal 80 m/s. Verified: vehicle hovers 2 m above the street
  and cruises ~13.6→15 m/s. (Non-hover `TnCarSimulationBlueprint` = MaxSpeed 500 UU; unused.)
- Remaining: reverse speed + braking/damping constants (via LinearDamping, not a single value);
  per-axis turn rate; exact dash-as-impulse vs the accel-ramp approximation; velocity transfer
  at transform (currently zeroed).

## PROVISIONAL / APPROXIMATE (rebuild guesses — NOT evidence)
- Camera follow distance (9 m); mouse sensitivity; ground-cam pitch limits.
- MaxStepHeight (kStepUp 0.6 m) — not found overridden; Engine.Pawn default 35 UU=0.35 m.
- Audio attenuation radii / reverb (master 0.5 + generic falloff).
- Locomotion crossfade time (0.15 s).

## AUDIO (investigated + fixed)
- Original Streets audio = 40 `AmbientSound` + 17 `HmAmbientSoundVolumeEmitter` +
  13 `HmAmbientSoundLineEmitter` (spatial bed) with SoundCues; weapon/transform cues are
  FMOD SoundCues. Exact per-cue attenuation radii/reverb not yet extracted.
- "Too loud / not spatial" cause: the mixer played every cue at full volume, mono, no
  attenuation. **Fixed:** master level 0.5 [PROV], and a 3D path (distance attenuation +
  equal-power stereo pan vs the camera listener) via `IAudio::playAt`/`setListener`. Fire is
  positioned at the muzzle; land/transform/reload at the pawn.
- Remaining: real SoundCue min/max radii + falloff curves, the ambient emitter bed, reverb,
  interior/exterior treatment, concurrency/voice limits, pitch randomization.

## NOT YET IMPLEMENTED
- Normal/specular/emissive materials; lightmaps/baked lighting; HeightFog; post FX (bloom/DOF).
- ADS/fine-aim mode; weapon recoil; per-shot spread visualization.
- Prefab geometry; static-mesh collision; spatial/reverb audio.

---

## OPEN QUESTIONS → EVIDENCE NEEDED
- Robot GroundSpeed/JumpZ/AccelRate: read `TnPawn`/robot-form class defaults in `default.xex`
  (Ghidra/ReVa) or measure root motion from `Nav_*` clips.
- Camera follow distance/offset: HM camera behavior assets (cooked packages) or exe.
- Vehicle base speed/accel/turn: `TnCarForm`/`TnTruckForm` compiled defaults in exe.
- Lighting: lightmaps not extracted; dominant directional light direction/colour from map.
