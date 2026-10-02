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

## PASS 9 — CHARACTER CUSTOMIZATION (2026-10-01, branch agents/rendering)
| Item | Original (WFC) | Source | Conf | Rebuild |
|---|---|---|---|---|
| Applier parameters | Cust_Color_A, Cust_COLOR_B, EnergonColor on robot, vehicle, arm, weapon | Default__TnCharacterApplier; native RE (user-supplied) | CONF | runtime uniforms in CHR + WEP MICs |
| Zero semantics | all-zero character colour skips the override; authored value stands | native RE | CONF | **APPLIED** (RGB all zero = skip) |
| Colour set | faction selects the set; no separate team override in this path | native RE | CONF | caller passes the faction's set |
| Optimus data | PrimaryColors/SecondaryColors (0,0,0,255) per faction, EnergonColor (0,0,0,1) | OptimusPrime_PCD_SP / Leader_PCD_MP | CONF | all skipped → MIC values (Cust_A/B per MIC, EnergonColor (1.25,0.05,0.05) compiled Autobot branch) |
| Parameter reads | robot+vehicle MICs read all three; WEP_IonBlaster_MATINST reads Cust_Color_A + EnergonColor; InteriorAlt reads none | compiled MIC uniform expressions | CONF | matches |
| Arm mesh | CP_OptimusArm_SKEL used when no weapon is drawn (early vehicle→robot transform, holster, melee); slots Cust_Mat_INST_B + InteriorAlt | native RE; materials_authored.json | CONF | renders with original MICs (gameplay drives visibility) |
| MP robot mesh | RB_OptimusWeaponArm_SKEL (forearm removed; slot order differs from the slice's campaign RB_Optimus_A_SKELMESH) | materials_authored.json | CONF | renderer supports it; mesh choice is gameplay/asset ownership |

## PASS 8 — VEHICLE MATERIAL + MAP COMPOSITION (2026-10-01, branch agents/rendering)
| Item | Original (WFC) | Source | Conf | Rebuild |
|---|---|---|---|---|
| Vehicle normal map | `RB_OptimusPrime_Cust2_Mat_INST` `UseReconstructedNormal`=True: X=A, Y=G of DXT5 `VH_Optimus_NORM`, both `*2-1`, Z=sqrt(1-x²-y²) | MIC static params; BASE ShaderCache character PS (`tfetch .yw; mad r.xy, r.zy, 2.0, -1.0`) | CONF | **FIXED** (alpha was left in [0,1]) |
| Vehicle diffuse/customization | CHR master with Cust_Color_A/B, ColorBrightness 0.6, etc. | MIC params | CONF | verified = AssetTools bake |
| EnergonColor (robot+vehicle) | 3 same-named VectorParameters; compiled permutation binds (1.25,0.05,0.05) | MIC compiled uniform expressions | CONF | as compiled (team override at runtime not modelled) |
| Persistent level | `MP_IAC_Streets_Base_m` streams ART + AUDIO only | TransLevels.ini MapFilename; LevelStreamingKismet | CONF | composition complete |
| Post-process | TnWorldInfo: Bloom_Scale 0.1, bEnableDOF, DOF_MaxFarBlurAmount 0.6, FocusFarFalloff 40000, ColorCorrectionTexture MP_Streets_CLUT | BASE TnWorldInfo over Default__WorldInfo | CONF | **APPLIED** (replaces PASS 7 defaults) |
| CLUT | 32³ A8R8G8B8 Texture3D, SRGB=False (Default__Texture3D), applied after gamma: tex3D(c*(N-1)/N+0.5/N) | Xbox 3D tiling (xenia Tiled3D); uber+CLUT PS | CONF | **APPLIED**; strong authored desaturation |
| DOF | gather a' from avg depth; resolve ((1-a)scene+blur.rgb)/((1-a)+blur.a); PackedParameters (Focus, 1/NearFalloff, Exponent, 1/FarFalloff) | decoded PS | CONF shader / MED parameter packing | **APPLIED** |
| Static decals | 25 DecalComponents with cooked receiver geometry (28-byte verts, u16 indices); BSP receivers clipped, static meshes keep whole triangles; UV = 0.5 - M·(P-L) + offset, rows HitTangent·TileX/Width, HitBinormal·TileY/Height | DecalComponent native; decal VS | CONF geometry+UV / MED box clip of static-mesh receivers | **APPLIED** |
| Section materials | umodel glTF section names → original MIC paths (15); 6 null (`dummy_material_N`) | StaticMesh sections | HI | **APPLIED** |
| Prefabs | 16 PrefabInstances → 34 placed StaticMeshActors (already composed) + 1 SteamVent emitter | ArchetypeToInstanceMap | CONF | complete |
| Emitters | 8 `FX_Level_Generic_p.FX.Steam_Sm_FX` + prefab steam vent | ART actors | CONF | **NOT RENDERED** (particle FX) |
| Power-tube "flat planes" | PowerTubeCircle emissive panels (mask R, lavender ×(1+pulse)) | MIC/mask | CONF | authored; renders as designed |

## PASS 7 — ORIGINAL RENDER PATH (2026-10-01, branch agents/rendering)
Specification sources: the ORIGINAL compiled Xenon shader microcode in the cooked `ShaderCache`
exports (disassembled with `tools/render/xenos_dis.py`, bit layouts per xenia's public `ucode.h`),
the cooked material expression graphs, and cooked lighting objects. No values tuned by eye.

| Stage | Original (WFC) | Source | Conf | Rebuild |
|---|---|---|---|---|
| Base-pass lighting policy | `FDirectionalTextureLightMapPolicy` (also Vertex/Interpolated variants) | ShaderCache type names, ART pkg | CONF | **APPLIED** |
| Directional lightmap decode | `L = Σ_i dot(N_t,B_i)² · tex_i.rgb · ScaleVector_i`; B0=(0,√(2/3),1/√3) B1=(−1/√2,−1/√6,1/√3) B2=(1/√2,−1/√6,1/√3); no-normal materials use ×1/3 (=(1/√3)²) | base-pass PS microcode + literal pools | CONF | **APPLIED** (3 coefficients) |
| Lightmap colour space | `LightMapTexture2D` inherits `SRGB=True` (Default__Texture); no instance override | Engine.xxx CDOs + texture tags | CONF | **APPLIED** (GL sRGB) |
| Lightmap resolution | atlases cooked at 256² (Xenon `MaxLODSize=512`, top mips absent in dump) | `_LM` pkg sizes, Xe-TransEngine.ini | CONF | as cooked |
| Base-pass output | `Emissive + Diffuse·L·ShadowMask`, ×SceneColorBiasFactor; **no lightmap specular** | base-pass PS | CONF | **APPLIED** (ShadowMask=1) |
| What is baked | Beast bake (`FBeastAreaLightPolicy`); dominant red directional (Brightness 6) LightmapGuid present in 1932 components; no ShadowMap2D textures | component native LightGuids | CONF | data used as-is |
| BSP lightmaps | per ModelComponent element `FLightMap2D`; per-vertex ShadowTexCoord in cooked `FModelVertexBuffer` (36 B verts, indexed by `FBspNode.iVertexIndex`, 889/889 nodes verified) | Model + ModelComponent native layout (decoded) | CONF | **APPLIED** (`bsp.glb`) |
| Materials | UE3 graphs + WFC MaterialFunctions (ENV_*_MF, CHR_*), static switches & TextureSets (Color_NormX/Masks_NormY, alpha UnpackMin −1) per MIC; WFC HLSL ShaderCode | cooked Material/MIC/TextureSet exports (Streets BASE+ART) | CONF | **APPLIED** 168/169 (offline GLSL translation) |
| Old extraction error | `materials.json` mapped decal `Rust_C_CLR` as base colour; real diffuse lived in TextureSets | MIC TextureSetParameterValues | CONF | **FIXED** |
| Cubemaps / flipbooks | cooked Xbox-tiled DXT TextureCube (6 faces) / TextureFlipBook | native tails (decoded) | CONF | **APPLIED** (`xbox_texture.py`) |
| Dynamic-object lighting | WFC UberLight: `Diff·AmbientCube(N)` + per light `(Diff·sat(N·L·0.6778+0.3333)² + Spec·pow(sat(R·L),SpecPower))·Color·sat(1−(d/R)²)^Falloff·spot²·S` | uber-light PS microcode | CONF | **APPLIED** |
| Light environment | WFC `LightEnvironmentComponent`; TnRobotForm/TnVehicleForm TotalLightCount **2**, UpdateDistanceThreshold **30 UU**; dynamic-only SkyLight (Brightness 0.1, LowerBrightness 0) + dynamic fill directional | TransGame.xxx CDOs, ART lights | CONF | **APPLIED**; composition of non-direct lights into the ambient cube = MED |
| Light visibility | `LightsVisibilitiesVolume` (745 KB precomputed per-cell light visibility, leaf records `(lightIdx u16, vis u16)`) | ART native | PARTIAL | **NOT USED**; collision raycasts instead (PROV) |
| Height fog | UE3 4-layer vertex fog: `exp2(FogDistanceScale·max(d−Start,0)·layerFrac)`, cut at ExtinctionDistance; authored Height −57273.8, Density 2e-5, Start 2048, LightColor (234,91,116) × **LightBrightness 0.1** (class default) | fog VS microcode + HeightFogComponent | CONF shader / MED constants (`−Density/ln2`) | **APPLIED** (replaces softened GL_EXP pink fog) |
| Post (superseded by PASS 8: map TnWorldInfo overrides) | bloom gather (4 taps clamp [0,4], tap kept if any channel > Threshold, ×0.25·Scale) + blur; UberPostProcess tone; no PPVolumes, persistent level stub ⇒ WorldInfo defaults (Bloom 1/1, Shadows 0, HighLights 1, MidTones 1, Desat 0, identity CLUT); DisplayGamma 2.2 | DOFAndBloomGather / UberPostProcess PS microcode, Default__WorldInfo, Xe-TransEngine.ini | CONF (blur kernel PROV) | **APPLIED** |
| Character materials | `CHR_Transformer_NormSpec_Cust_E_Mat` with MIC customization, spec-power S-curve, energon pulse, metal cubemap | Streets BASE | CONF | **APPLIED** (replaces provisional GL_LIGHT0) |

**Remaining gaps:** LightsVisibilitiesVolume decode; 24 `FLightMap1D` vertex-lightmapped props;
dynamic shadow mask (characters do not cast/receive dynamic shadows); DirectLightAmbientContribution;
`ENV_ORB_D1_Debris_p SideSupport_MATINST` (master not in Streets packages); `MotionBlurEffect` in
DefaultScenePostProcess not implemented; light-env transition blending (0.5 s) not implemented.

---

## PASS 12 — RECONCILED WITH NATIVE RE (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Vehicle→robot entry | Local player keeps full velocity and enters falling | native RE | CONF | **APPLIED** (replaces Pass 11 ground snap) |
| Robot→vehicle entry | Velocity clamped 3500 UU/s written into the RB, no reprojection; starts at robot yaw | native RE + OnActivate bytecode | CONF | **APPLIED** |
| Hover steering authority | Fades in over 0.5 s: accel × (1 − Drift/0.5)², Drift started by Hovering.BeginState | native RE + CalculateDriftScale/Drift bytecode | CONF | **APPLIED** (also after leaving Driving) |
| Hover control frame | View yaw: Hovering.DoUpdate → HoverSimulation.Update(..., ViewRotation.Yaw); UpdateTurn matches the yaw; UpdateStrafe toward (fwd, right) × 15 m/s, ClampLength accel 30 m/s² × drift × stability | bytecode | CONF | **APPLIED** (supersedes Pass 7 travel-direction steering; stability scale = 1 on flat ground) |
| Normal boost | Boost held → TnCarForm.Driving (Truck_Physics MaxSpeed 3000, MaxAcceleration 2500); not while drifting; release → Hovering | Hovering/Driving.UpdateBoosting bytecode; Truck_Physics | CONF (state, values) / PROV (steering, throttle) | **APPLIED** with wheels pose (HoverToBoost → Idle_Wheels; BoostToHover on exit) |
| Hover dash | Dash while hovering: dominant input axis (local), DashSpeed 3000, 0.5 s, accel 100000; cooldown TimeBetweenDashes 2.0 | Hovering.DoDash/UpdateDashing, UpdateDash, get_TimeBetweenDashes | CONF | **APPLIED** |
| Nitro / ram | Dash while driving: 3 s, speed ×1.5, steering ×0.3, cooldown 8 s, stop on leaving Driving | Systems checkpoint (script literals) | CONF | speed/steering **APPLIED**; ram collision not implemented; state duplicated with Systems VehicleNitro (unify at integration) |
| Dash binding | VehicleSpecialMove: PC Shift ("Ability0 \| VehicleSpecialMove"), pad RightShoulder → PlayerInCarForm.StartVehicleSpecialMove → set_DashingInput | bindings + bytecode | CONF | **APPLIED** (Systems' temporary Q superseded) |
| Input latching | Fire held flag persists; reload on release of a tap < 0.3 s; jump/dash edge latched until consumed; transform immediate | native RE | CONF | **APPLIED** |
| Weapon restore | _RestoreWeaponTransformFractionRemaining 0.75 = restore at 25% elapsed (vehicle→robot); usable after EquipTime 0.2 s | native RE + ini | CONF | **APPLIED** (usable at 0.48 s of 1.13 s fold); visible gun waits for the 50% mesh handoff [PROV] |
| Fine aim on transform | Transforming to vehicle ends it | native RE | CONF | **APPLIED** (wish cleared) |
| Locomotion play rate | Walk/jog at 1.0×; the original shows the same stride mismatch | native RE | CONF | no compensation (as built) |

---

## PASS 11 — TRANSFORM MOMENTUM, ROBOT SPEED, FINE AIM (2026-10-01, gameplay agent)

Evidence: UnrealScript bytecode decoded from `TransGame.xxx` with `work/pass11/ue3dis.py` (validated on
`TnPawn.UpdateSpeeds`), shipped bindings (`Xe-TransInput.ini`), character/vehicle/camera content
objects (`Optimus_ROBODEF`, `HoverTruck_Physics`, `TruckTransformerMomentum`, `OverTheShoulder_STRATEGY`).

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Velocity at transform start | Untouched. `TnPawn.Transform` → `Transforming.BeginTransformation` → `TnTransformation.Execute` never zero velocity | bytecode | CONF | **FIXED** (rebuild zeroed velocity in beginTransform) |
| Input during a fold | Movement keeps working; only `IsAbleToFire`, `PreWeaponSwitch`, `StartTransform` check IsTransforming | bytecode (all 20 get_IsTransforming call sites) | CONF | **FIXED** (rebuild locked all input); fire/reload stay blocked, jump blocked [PROV] |
| Movement form switch | `BeginTransformation`: _CurrentForm = TargetForm + target movement capabilities at fold START | bytecode | CONF | **APPLIED** (moveForm(); mesh still hands off mid-fold) |
| Robot→vehicle handoff | `TnVehicleForm.OnActivate`: Velocity = ClampLength(pawn Velocity, 3500); RB rotation = pawn rotation | bytecode; `kMaxTransformSpeed` 3500 | CONF | **APPLIED** |
| Vehicle→robot handoff | `TnRobotForm.OnActivate` leaves velocity; robot `CalcVelocity` continues it | bytecode | CONF | **APPLIED** |
| Momentum preservation | Above max speed: MaxAccel = AccelRate/(1+P); P from TruckTransformerMomentum OnGround 7/3/1, InAir 100/100/5 (fwd/neutral/back; fwd = input within 30° of velocity); InAir set while falling OR transforming | `CalculateMomentumPreservation`, `CalculateMaxAcceleration` bytecode | CONF | **APPLIED** |
| Robot velocity model | Desired = Normal(input)·max(450, ‖input‖·MaxSpeed); no input → 0 (ground) / keep (air); local-frame per-axis accel clamp; no speed cap | `TnPawn.CalcVelocity` / `CalculateDesiredFlatVelocity` bytecode | CONF | **APPLIED** (AirControl application PROV) |
| Character movement values | BaseGroundSpeed 1400, AccelRate 12000, AirSpeed 1200, AirControl 0.4, TerminalVelocity 6000, Collision r200/h200, JumpHeight 500 (SharedAcrobatics) | `Optimus_ROBODEF` via `TnPawn.ApplyTransformer` bytecode; identical on 6 playable ROBODEFs | CONF | **APPLIED** (replaces Passes 2/10 class defaults) |
| Robot fast movement | No sprint key exists. Full input = 14 m/s jog (the "missing fast movement"); partial pad input ≥4.5 m/s walk; vehicle→robot keeps vehicle/boost speed (momentum run, ≤35 m/s handoff) | bindings + above | CONF | **APPLIED** |
| Boost input | RightMouseButton / LeftTrigger are bound to both FineAim and Boost; robot OnStartBoost empty; vehicle states boost | `Xe-TransInput.ini`, bytecode | CONF | **APPLIED** (RMB/LT boost in vehicle; Shift kept as alias) |
| Truck physics | DashSpeed 3000, DashDuration 0.5, SuspensionRadius 185 (MaxLinearSpeed default 1500) | `VEH_SHARED_p.HoverTruck_Physics` | CONF | **APPLIED** |
| Fine aim input | PC RMB = ToggleFineAim; pad LT = FineAim (hold) | `Xe-TransInput.ini` | CONF | **APPLIED** |
| Fine aim rules | Robot only (PlayerWalking); blocked while meleeing/reloading/dodging; wants persist → resumes after reload | `TnFineAimManager.Tick/Start/Stop`, `PlayerWalking.CanFineAim` bytecode | CONF | **APPLIED** (also blocked while transforming [PROV]) |
| Fine aim movement | SetSpeedMultiplier(0.5) → 7 m/s ground (air too) | `_GroundSpeedMultiplier`, UpdateSpeeds | CONF | **APPLIED** (excess speed bleeds through momentum preservation) |
| Fine aim camera | FOV 80→45 (SmoothTime 0.1), exit 0.4; look speed 25/12.5 vs 50/25; orbit distance unchanged for Ion Blaster; shoulder curve unchanged | `OverTheShoulder_STRATEGY` behaviours | CONF values / PROV smoothing curve | **APPLIED** |
| Fine aim weapon | Spread × FineAimSpreadModifier 0.5; weapon events 15/16 (sounds) | `TnWeapon.Start/StopFineAim`, WEPDATA | CONF | spread **APPLIED**; sounds not wired |
| Fine aim upper body | No fine-aim pose (WeaponPose slot plays weapon PoseName only) | `TnWeaponPoseSlotLogic` bytecode | HI | none (aim offset continues) |
| Fine aim target snap | TnOrbitRotateToTarget FineAim: snap, speed 10; _TargetSnapTimeout 1.0 | strategy + ini | CONF | NOT implemented (no target system) |
| Robot camera | Anchor actor+200 UU; orbit 800; DefaultFOV 80; pitch ±75; shoulder offset [150,300,150] by pitch, SmoothTime 0.3; third-person collision | `OverTheShoulder_STRATEGY` | CONF values / PROV offset semantics + collision rules | **APPLIED**; traces aim through the crosshair point |
| Support/step query | groundHeight(nearY = body, stepUp = 35 UU) | — | fix | **FIXED** (pre-existing double count: robots stepped 0.7 m, vehicles could snap to decks 4.4 m higher) |

---

## PASS 10 — LOCOMOTION BLEND, GROUND SPEED, STEP HEIGHT (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Moving-state structure | TnVelocityAnimBlend(Walking, Jogging) over TnStraferAnimBlend(F,B,R,L) of `Nav_Strafe{Walk,Jog}_*` (OneHanded) | Robot_ANIMTREE | CONF | **APPLIED** |
| Walk→jog speeds | MinSpeed 450 / MaxSpeed 1200 UU/s (class default 100/1000 overridden) | `TnVelocityAnimBlend_10449` | CONF | **APPLIED** (linear weight; PROV formula) |
| Clip ground speeds | walk ≈3.5 m/s, jog ≈12.1 m/s (planted-toe stance speed) | robot.glb measurement (`work/pass8/stride.js`) | HI | Explains 1200 = jog speed |
| Direction weights | from local velocity; `_BlendSpeed` 0.2 | `Default__TnStraferAnimBlend` | CONF (value) / PROV (max(0,±dot) normalized, eased) | **APPLIED** |
| Phase sync | "Strafers" AnimNodeSynch group (all 16 strafe sequences, RateScale 1) | `AnimNodeSynch_581` | CONF | **APPLIED** (shared phase, master = highest weight) |
| Idle↔Moving transition | AmpCrossFadeCondition 0.2 s | IdleToMoving / MovingToIdle | CONF | **APPLIED** (was PROV 0.15) |
| Robot ground speed | GroundSpeed = _BaseGroundSpeed (=GroundSpeed 550 at PostBeginPlay) × Π speed multipliers; Ion Blaster GroundSpeedMultiplier 1.0 | TnPawn.PostBeginPlay / UpdateSpeeds bytecode; Default__TnWeaponData | CONF | **SUPERSEDED (Pass 11):** ApplyTransformer later calls set_BaseGroundSpeed(ROBODEF.BaseGroundSpeed = 1400) → 14 m/s |
| MaxStepHeight | 35 UU = 0.35 m (WalkableFloorZ 0.7, MaxFallHeight 3400) | `Default__TnRobotForm._MovementCapabilities` | CONF | **APPLIED** step 0.35 m (was PROV 0.6); deterministic A/B shows no new snagging. WalkableFloorZ not yet enforced. |
| Idle clip names | Tree defaults `NAV_Idle_01` / `AI_Nav_Idle_Pose_05` / `ADD_NAV_Idle` resolved per character by choosers | Robot_ANIMTREE idle branch | — | Optimus set lacks them; `NAV_Idle` kept |

---

## PASS 9 — AUTHORED AIM OFFSET PROFILE (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Active profile | `Default` for the Ion Blaster (`ActiveProfileProperty`=WeaponTypeObserved; other profiles are TwoHandedMelee, TwoHandedMeleeCharge, TwoHandedGun) | Robot_ANIMTREE `TnAnimNodeAimOffset_14979` | HI | **APPLIED** |
| Profile bones | Lumbar01, Lumbar02, Neck01, Head, L Clav/Shoulder/Elbow/Hand, R Clav/Shoulder/Elbow | `AimComponents[].BoneName` | CONF | **APPLIED** (replaces the whole-upper-body local deltas of Passes 7–8) |
| Cell offsets | 9 rotations (+ translations on R_Arm01_Clav) per bone, baked from `Shooting_Aim_{L,F,R}_{D,C,U}` (`AnimName_*`) | `AimComponents` | CONF | **APPLIED**, baked at load by the verified rule: rot = Gp·(L_cell·L_C⁻¹)·Gp⁻¹, pos = Gp·(t_cell−t_C), Gp = parent model rotation in that cell. Matches the shipped quaternions to 0.01° mean / 0.07° worst and translations to 0 mm (UE→glTF: q `(−x,−z,−y,w)`, v `(x,z,y)·0.01`). |
| Application | Model-space rotation/translation about each bone's pivot, parent first; bilinear between cells | UE3 AnimNodeAimOffset (consistent with the bake: reproduces every cell pose from the centre pose) | HI | **APPLIED** (slerp per axis, weight = aimW). |
| Ranges | Profile H [−1,1], V [−1,0.8]; `RemapPawnAimRange` from PawnAimOffsetRange H [−1,0.85], V [−0.7,1]; pawn aim = angle/90° | Default profile | CONF (values) / PROV (centre-preserving remap) | **APPLIED**. Barrel pitch at aim −69/−34/0/+34/+69° = −42/−21/+3/+27/+50°. Replaces the Pass 7 barrel-pitch calibration. |
| Pivot arm swing | `Nav_IdlePivot90_*` swings the gun forearm ≈50° mid-step (root-discarded chest ±7°) | clip data; tree layers the aim offset over the turn unchanged | HI | Kept as authored. |

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
| Aim offset yaw columns | Leg/aim difference feeds the aim offset horizontally (`TnAnimNodeAimOffset.TurnInPlaceOffset`) | TransGame class layout | HI | **APPLIED** (Pass 9: through the authored profile ranges). |
| Aim offset interpolation | InterpSpeed 12 | `Default__TnAnimNodeAimOffset` | CONF | **APPLIED** (FInterpTo-style). |
| Aim offset authored profile | `TnAnimNodeAimOffset` "Default" profile | Robot_ANIMTREE | CONF | **APPLIED in Pass 9** (see below). |
| Incoming transform clip | Matched pair `Transform_ToVehicle_ROBO` ↔ `Transform_ToVehicle_VEH` | clip durations (1.967 s both) | HI | **FIXED** (was SuperBoost_Veh, 0.8 s). |

---

## PASS 7 — ANIMATION LAYERS & MESH FACING (2026-10-01, gameplay agent)

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Mesh forward axis | Skeletal meshes face model **+X** (UE convention; umodel `(x,z,y)` keeps X). | `Shooting_Aim_F_C` barrel dir in model space = (1.00,−0.06,−0.02) | CONF | **FIXED** `kMeshYawOffset=+π/2` (Pass 6's 0 left robot/truck side-on, gun 90° off the reticle). Verified: chase cam sees back/rear; muzzle along aim yaw. |
| Upper-body aim offset | `Shooting_Aim_*` 9-pose grid points the gun at the aim. | `robot.glb` category `aim`; weapon.json `owner_animations.aim` | HI | **APPLIED**: F column (body faces aim yaw → yaw offset 0), delta from F_C on `C_Spine01_Lumbar01_XB` subtree, driven by controller pitch. |
| Aim pitch → grid mapping (superseded by Pass 9 authored ranges) | UE3 AimOffset normalized pitch range | Calibrated from the poses: barrel pitch D −47.6° / C −3.3° / U +72.2° | CONF-derived | **APPLIED**: piecewise-linear so barrel pitch ≈ aim pitch (measured −27/+7/+42° at −34/0/+34°). |
| Reload while moving | Owner reload clip over locomotion | weapon.json `owner_animations.reload_robot` = `Shooting_Reload_IonBlaster_ROBO` | HI (clip) / PROV (mask) | **APPLIED**: upper-body slot (mask root `C_Spine01_Lumbar01_XB` PROV), full body when standing, mesh-space per-bone blend (UE3 `AnimNodeBlendPerBone` default). |
| Slot / aim / additive ease time | — | — | GUESS | `kSlotBlend=0.15 s` **PROVISIONAL**. |
| Vehicle move animation | Directional single-frame hover poses + additive hover bob | `vehicle.glb` `Nav_Hover_{Pose,F,B,L,R}_VEH`, `ADD_Nav_Hover_VEH` (additive=true) | HI (clips) / PROV (weights) | **FIXED** (was looping the one-shot `Nav_BoostToHover_VEH` transition). Weights = local velocity / MaxLinearSpeed per axis. |
| Vehicle turn rate | **~π rad/s** | `TnHoverCarSimulationBlueprint.AiMaxAngularSpeed` | CONF | **SUPERSEDED (Pass 11):** AiMaxAngularSpeed is AI-only (truck: 20); player steering rate PROV |
| Robot gameplay idle | Character-specific nav idle | `NAV_Idle` from `Optimus_ROBO_ANIM`; `Cust_Idle` = customization screen | MED | **FIXED** (was `Cust_Idle`, first of category). |
| Jump / land | Take-off once → descent loop → land | `Nav_TakeOff_01`, `Nav_Jump_Descent`, `Nav_Land` | MED | **APPLIED**: take-off non-looping; land after ≥0.3 s airborne (`kLandMinAirTime` PROV), skipped when moving. |
| Weapon recoil | see Pass 8 | weapon.json + archetype | CONF | **APPLIED in Pass 8**. |
| Turn in place | see Pass 8 | Robot_ANIMTREE | CONF | **APPLIED in Pass 8**. |

Additive-clip convention: `ADD_*` clips are stored as **deltas** (identity-ish quats, zero
translations at rest), so `samplePose(additive=true)` starts from identity and `addPose` composes
`base * delta` in bone-local space.

---

## PASS 7 — WEAPON LAYERING, RECOIL, WEAPON MESH, FX, SOUNDCUES (Systems agent, 2026-10-01)

> **Integration note (integration/milestone-01):** the recovered data in this pass is kept as
> provenance. In the merged build, the *Reload layering*, *Recoil controls/defs/evaluation* and
> *Upper-body aim offset* (PASS 7b) rows are implemented by **Gameplay's** code (Passes 7–9), not
> by the Systems implementation described here. Both branches recovered the same Robot_ANIMTREE /
> RecoilDef values. The Systems `AimOffset.h`, `updateUpperBody`/`evalLayered` and shot-serial
> recoil trigger were not merged, to avoid a second aim offset and double recoil per shot.
> Remaining value difference: Systems reads the owner-anim slot blend as **0.1/0.1 s [CONF]**
> (`TnWeaponOwnerAnimator` CDO), while Gameplay's reload slot uses `kSlotBlend` 0.15 s [PROV].
> This is left for the Gameplay owner to reconcile. The weapon mesh, sockets, notifies, FX, SoundCues
> and audio rows below are all live in the merged build.
Recovered from cooked packages with AssetTools `objtree`/`typed_props` (read-only); decoders in
`tools/systems/`. Owner animation, recoil, weapon-mesh, FX and cue data come from
`WEP_IonBlaster_p` / `FX_*_p` / `BL_WPN_*` exports cooked into `A1_IAC_Base_m` / `MP_IAC_Streets_BASE_m`,
plus class defaults (CDOs) in `TransGame.xxx` / `HM_Engine.xxx`.

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Reload layering | Reload plays in Robot_ANIMTREE's **UpperBodyCustom** AnimNodeSlot, fed through AnimNodeBlendMultiBone_4930 (InitTargetStartBone **C_Spine01_Lumbar01_XB**, PerBoneIncrease 1.0: spine + arms + head = 1, hips/legs = 0). Legs keep locomotion. | `TR_Shared_ANIMTREE_p.Robot_ANIMTREE` | CONF | **APPLIED** (bone-local blend, as AnimNodeBlendMultiBone). Verified: base `Nav_StrafeJog_F` @5.5 m/s while upper = `Shooting_Reload_IonBlaster_ROBO`. |
| Reload owner anim | `ReloadAnimation = Shooting_Reload_IonBlaster_ROBO`, non-additive, BlendIn/Out **0.1/0.1 s** | `IonBlaster_WEPDATA.TnWeaponOwnerAnimator_10029` + `Default__TnWeaponOwnerAnimator` | CONF | **APPLIED**. Slot releases at reload end (1.5 s) and blends out while the 1.633 s clip finishes. |
| Fire owner anim | none (`FireAmmoAnimations` empty for the Ion Blaster); firing drives skel-control recoil only | same | CONF | APPLIED |
| Equip/overheat anims | `ADD_Shooting_equip_weapon` (CAMT_UpperBody, additive, 0.2/0.3 s); overheat Start/Loop/End additive | same | CONF | not yet (no equip/overheat gameplay) |
| Recoil controls | HmSkelControlRecoil: **RightHandRecoil** on R_Arm02_Shoulder_XB, **SpineRecoil** on C_Spine02_Lumbar02_XB, LeftHandRecoil on L_Arm01_Clav_XB | Robot_ANIMTREE SkelControlLists | CONF | **APPLIED** (left hand: no Ion Blaster RecoilDef, idle) |
| Recoil defs | Spine: 0.8 s, RotAmp (500,1000,0), RotFreq (10,10,0), RotParams all Zero. RightHand: 0.5 s, RotAmp (2000,500,-2000), RotFreq (15,10,10), Y=Random, LocAmp X=-8 UU @10 | `TnWeaponMesh` CDO merged with `IonBlaster_WEPMESH` | CONF | **APPLIED** |
| Recoil evaluation | RecoilDef layout identical to UE3 GameSkelCtrl_Recoil: restart per shot, smoothstep(TimeToGo/Duration) x Amp x sin(phase + TimeToGo x Freq), aim space | struct identity with UE3 | HI | **APPLIED** (rotation about the bone origin in model aim space) |
| Camera recoil / shake | RecoilCameraParams (FOV +0.085, pitch 0.1 deg, 0.05 s, delay 0.25 s); CameraShake (0.17, roll 0.15, falloff 0.1/0.06 s) | `IonBlaster_WEPDATA` | CONF | **not applied**: camera is owned by the Gameplay agent |
| Weapon mesh | 34-joint skeletal mesh with its own AnimSet: WP_Fire -> `IonBlaster_Fire`, WP_Reload -> `Shooting_Reload_IonBlaster_AP`, idle `IonBlaster_Idle`; HmAnimatedMesh BlendOutTime 0.2 s | `IonBlaster_WEPMESH.WeaponEventAnims`, `HmAnimatedMesh_9126` | CONF | **APPLIED** (was a static mesh) |
| Weapon sockets | MuzzleFlash -> C_Robo04_XT; ShellSocket -> C_Robo15_XT (+loc/rot); MagSocket -> C_Robo01_XT (+loc/rot) | `WEP_IonBlaster_SKEL` SkeletalMeshSockets | CONF | **APPLIED**; muzzle/tracer origin = MuzzleFlash socket (replaces the geometric barrel tip) |
| Event timing (AnimNotifies) | Fire: Shell_AssaultRifle_FX @0.005 ShellSocket. Reload_AP: ANIM_RELOAD_01 @0.000, Reload_AssaultRifle_FX @0.034 MuzzleFlash, ANIM_RELOAD_02 @0.137, Magazine_IonBlaster_FX @0.174 MagSocket. Idle: IDLE_01 @0.022, IDLE_02 @2.751 | `WEP_IonBlaster_ANIM` (HmAnimNotify_Sound / HmAnimNotify_PlayEffect) | CONF | **APPLIED** for sounds and effects (shell/reload/magazine FX: PASS 7c) |
| Muzzle flash | `FX_AssaultRifle_p.FX.MuzzleFlash_AssaultRifle_FX`, local space at MuzzleFlash | WEPMESH.MuzzleFlashes | CONF | **APPLIED**: Long (MuzzleFlash_Side_02, velocity-aligned, burst 10, life 0.15-0.2, 1.9-2.1 x 3.5-5 m, +1.9 m, 9 m/s), Top (smokeball_02 star), Sparks (SparksSheet 2x2 SubUV). Omitted: Glow_Mod (modulate), distortion ring, BackSteam/BackJet (alpha <= 0.05) |
| Tracer | `Tracer_AssaultRifle_FX`: Bolt (Bolt_ADD_MAT, PSA_Velocity, burst 1, life 0.6, 2 x 5-7 m, 15000 UU/s) + Trail2 smoke ribbon (Tracer_Smoke, life 0.9) | WEPMESH.TracerTemplates | CONF | **APPLIED**; bolt removed at the impact point; smoke ribbon opacity 0.35 **PROV** |
| Impact squib | `Impact_IonBlaster_FX`: GLOW_Dup (MuzzleFlash2, burst 10), Sparks_bolts (Spark_Tail, burst 20, 1.5-10 m/s), Smoke (SmokeThin, burst 4, x5 growth); rules 60 %, max 5 live, 25 m max distance | WEPMESH.DefaultSquib / TnWeaponMesh CDO | CONF | **APPLIED**; surface normal approximated by -shot dir (collision query returns no normal) |
| Effect colour | native colour constant `ff 33 19 ff` read as ARGB = (51,25,255) blue-violet; EnergonColor param (255,255,255,A=0) = no override; SwitchableColorScaleOverLife A/B chosen by Team | LOD streams, WEPMESH params, editor thumbnails | HI | APPLIED (team A) |
| FX module roles | WFC compiles modules into a native per-LOD stream; distributions decode exactly (type/op/n/chunk + BE float table), but **which module each belongs to is inferred from order** (Lifetime, StartRotation, ..., AlphaOverLife, StartSize, SizeMultLife, Velocity, ColorOverLife, Location) | stream analysis | MED | used as above; x4 / x2 overbright assignment MED |
| Fire cue | `BL_WPN_GUN_ION_BLASTER.SHOOT`: root -9 dB, Distance 4000-35000 UU, MaxConcurrent 4; layers by SOUND_DISTANCE: HEAD (<=200 UU), HANDCANNON_LR (400-4000), NRG_DISTANT (4000-8000), shell drops (+0.474 s) | cooked SoundCue / SoundNodeRoot / SoundNodeWaveEvent | CONF | **APPLIED** (generated table) |
| Low ammo / tail / impacts | SHOOT_LOW_AMMO (ammo <= 5), SHOOT_TAIL (WP_LoopingTail, 3000-12000 UU), IMPT_WORLD (-12 dB), IMPT_DMG (-9 dB, rolloff 2), max 6 | same | CONF | **APPLIED** |
| Reload / idle cues | ANIM_RELOAD_01 (3 events), ANIM_RELOAD_02 (12 timed events to 1.33 s, Distance 1000 UU, rolloff 2), IDLE_01/02 (-21 dB) | same | CONF | **APPLIED** via AnimNotifies (replaces the single provisional clip-reload wav) |
| Attenuation model | DistanceMin/Max + RolloffFactor = FMOD Ex inverse rolloff (banks ship as .fsb) | field names + FMOD banks | HI | APPLIED |
| SOUND_DISTANCE for own weapon | `kSmartPan_PreferPlayer`: parameter measured from the owning player | inferred from the name | MED | APPLIED |
| Concurrency | MaxConcurrentPlayCount -> steal the oldest instance | - | MED | APPLIED |
| Mixer categories / reverb | SFX_WET_COMBAT_ROBOT_WPN(_SHOOT) category levels, WET reverb sends, occlusion | not extracted | - | **not applied**; master level 0.5 still PROV |
| Dry fire | `BL_WPN_FOLEY.SHOOT_DRY_FIRE_ELECTRICITY` (WP_NoAmmoFire) | cue table present | CONF | not triggered (needs a trigger-on-empty event from PlayerController, owned by Gameplay) |

Open items for the Gameplay agent: the robot's idle base clip is currently `Cust_Idle` (the
customisation-screen showcase idle, first entry of category `idle`), which turns the body and
points the gun away from the aim; `NAV_Idle` / `Nav_Idle_Pose` is the gameplay idle. Upper-body
aim offset: applied in PASS 7b. Mesh yaw offset: see PASS 7b (should be +pi/2).

### PASS 7b — upper-body aim offset (Systems agent)
- **Space and axes verified from data:** recomputing the `Shooting_Aim_F_U` vs `Shooting_Aim_F_C` mesh-space
  delta in robot.glb reproduces the authored Spine01 CU rotation exactly (0.0926), and the Spine02 delta
  (0.318) is the sum of the Spine01 + Spine02 increments. The authored rotations are therefore **mesh-space
  increments applied in hierarchy order**. The UE -> glTF quaternion mapping is (x,y,z,w) -> (-x,-z,-y,w).
- **Tree order [CONF]:** the AimOffset node sits under the `UpperBodyCustom` slot's source (via
  TnAnimTurnInPlaceRotator "UnwindLowerBody"), so it is: locomotion -> aim offset -> reload slot -> recoil.
  A playing reload replaces the aimed spine/arm rotations, as in the original.
- **Pawn aim input [MED]:** X = 0 (the body already faces the aim yaw); Y = camera pitch / 90 deg (UE3 pawn
  aim convention), remapped piecewise-linearly through PawnAimOffsetRange -> profile range. The exact
  TnAnimNodeAimOffset remap and the active-profile choice (`WeaponTypeObserved`; Default for the Ion
  Blaster) are not decompiled.
- **Mesh facing bug found (Gameplay-owned, not changed here):** the skeleton faces **+X** in model space
  (eyes are +X of the head, left clavicle at -Z, gun forearm along +X in NAV_Idle, StrafeJog_F and
  Shooting_Aim_F_C), but `core::config::kMeshYawOffset = 0` renders the mesh as if it faced -Z. The robot
  is therefore drawn rotated 90 deg from the aim; measured barrel heading = aim - 97..102 deg. With the
  renderer's rotateY convention the correct value is **kMeshYawOffset = +pi/2**. The recoil aim frame
  now uses the measured +X forward, so it is correct either way.

### PASS 7c — shell, magazine and reload FX (Systems agent)
Spawned by the weapon AnimNotifies at their authored times and sockets (see the Event timing row).

| Effect / emitter | Original data | Conf | Rebuild |
|---|---|---|---|
| Shell_AssaultRifle_FX "Shell" (ShellSocket, every shot @0.005) | mesh `FX_GrenadeLauncher_p.GrenadeAmmo_STAT` (WEP_GrenadeLauncher_MATINST -> WEP_GrenadeLauncher_CLR), burst 1, life 1.0, StartSize (0.75,0.3,0.3), spin U[(-1,-1,-1),(5,5,1)] turns/s | CONF | **APPLIED** as a mesh particle |
| shell ejection velocity | not on the Shell emitter; the paired ShellGlow emitter (same socket) carries U[(300,-300,100),(1000,-300,300)] UU/s | MED | used for the shell mesh |
| Shell "SMOKE" | SmokeCoolDepth, burst 4, life U[0.5,0.75], size U[50,100] UU growing x3, alpha 0.1 -> 0, velocity U[(100,-300,-300),(300,300,300)] UU/s | CONF (role order MED) | **APPLIED** |
| Magazine_IonBlaster_FX "Shell" (MagSocket, reload @0.174) | mesh `FX_IonBlaster_p.IonBlaster_Mag_STAT` (IonBlaster_Mag_MATINST -> WEP_IonBlaster_CLR), burst 1, life 3.0, StartSize 1, spin U[-1,1] turns/s, velocity (200,300,300) UU/s | CONF (velocity role MED) | **APPLIED** |
| Magazine "Smoke_Dup_Dup_Dup" | as Shell SMOKE, alpha 1 -> 0, colour 1 -> 0.1 | CONF | **APPLIED** |
| Reload_AssaultRifle_FX "GLOW_Dup_Dup" (MuzzleFlash, reload @0.034) | flareball01 (smokeball_01), local space, burst 10, life U[0.2,0.5], size U[20,35] UU x (1.5 -> 0.1), alpha peak 0.2, brightness curve 20 -> 1 | CONF data / MED roles (size and velocity from uniform-curve tables) | **APPLIED** |
| Reload "Smoke_Dup" | SmokeCoolDepth, SpawnRate 20/s for 0.75 s following the muzzle, life U[0.5,0.75], size U[100,200] UU x3, alpha peak 0.25, grey 0.83-0.90, -50 UU behind the muzzle | CONF | **APPLIED** (continuous emitter) |
| Gravity / ground contact for the mesh particles | no acceleration or collision module authored | - | **PROV**: world pawn gravity (-29.4 m/s^2) and rest on the collision floor |
| Omitted | ShellGlow / GLOW (Glow_Mod_MAT modulate), Shimmer (distortion), BackSteam / BackJet (alpha <= 0.05), Blaster_Trail ribbons on shell and magazine, the "Trail" emitters | - | not rendered |

Asset loading: umodel `.gltf` + `.bin` meshes are now accepted by `assets::loadGlb` (text glTF branch).

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
| Upper-body aim offset | `Shooting_Aim_{F/L/R}_{C/D/U}` 9-pose grid points the gun at the reticle. Robot_ANIMTREE `TnAnimNodeAimOffset_14979`, profile **Default**: 11 bones (spine chain, head, both arms) x 9 authored rotations (L/C/R x U/C/D) baked from `Shooting_Aim_*`; ranges H [-1,1] V [-1,0.8]; RemapPawnAimRange from pawn H [-1,0.85] V [-0.7,1] | `robot.glb` category `aim`; `TR_Shared_ANIMTREE_p.Robot_ANIMTREE` | CONF (data) / MED (remap) | ~~NOT YET~~ **APPLIED in Gameplay Pass 7/9** (the implementation in the merged build). Systems PASS 7b recovered the same profile independently (aim 22.9 deg -> barrel 21.6-23.8 deg); that implementation was not merged. |
| Transform pairing | Robot & vehicle transform clips share a duration (ToVehicle **1.97 s**, ToRobot **1.13 s**) — one fold authored per mesh, played **in sync**. | `robot.glb`/`vehicle.glb` clip durations | HI | **APPLIED** — outgoing mesh → midpoint → partner mesh resumed at same normalized time (was sequential = the "crack"). Weapon holstered through the fold. |
| Transform cross-fade point | exact visibility/alpha handoff curve | — | GUESS | `kTransformHandoffFrac=0.5` **PROVISIONAL**; cross-mesh pop minimised, not removed. |
| Muzzle origin | Ion Blaster **barrel tip** (MuzzleFlash socket). Socket transform not extracted; used geometric tip. | `weapon.glb` frontmost vertex slice | CONF-derived | **APPLIED** — weapon-local (2.063,0.017,0.141) m → +2.07 m forward of the hand; tracer+flash leave the barrel. |
| Reload anim | `Shooting_Reload_IonBlaster_ROBO` (full-body); `WeaponReloadAnimTime` 1.5 s (clip 1.633 s). | `robot.glb` category `reload`; `weapon.json` | CONF | **APPLIED** full-body one-shot while reloading. Additive `ADD_Shooting_Reload_*` (reload on the move) = PARTIAL. |

**Lighting (#6/#7/#8) — superseded by PASS 7 (original render path).** World still uses only **coeff0**
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
| Camera default FOV | **75° horizontal** (SmoothTime 0.4) | `Xe-TransCamera.ini [AnimatedFovCameraBehavior TnFovCameraBehavior] DefaultFOV` | CONF | **SUPERSEDED (Pass 11):** robot strategy instance OverTheShoulder TnFovCameraBehavior DefaultFOV 80 |
| Fine-aim (ADS) FOVs | 35 / 45 / 55 (close/med/far POI) | `Xe-TransGame.ini [TnPointOfInterest]` | CONF | **SUPERSEDED (Pass 11):** these are point-of-interest focus FOVs; fine aim uses TnPCS_FineAim FOV 45 (applied) |
| Fine-aim ground-speed mult | **0.5×** | `Xe-TransGame.ini [TnFineAimManager] _GroundSpeedMultiplier` | CONF | **SUPERSEDED (Pass 11):** APPLIED via SetSpeedMultiplier |
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
| Ion Blaster fine-aim spread | 0.5× | `weapon.json FineAimSpreadModifier` | CONF | **SUPERSEDED (Pass 11):** APPLIED |
| Ion Blaster equip/putdown | 0.2 / 0.5 s | `weapon.json EquipTime/PutDownTime` | CONF | not yet |
| Weapon socket | WeaponSocket_Primary, bone R_Arm03_Elbow_XB | `character.json sockets` | CONF | attached (rotation approx) |
| Robot ground speed | **550 UU/s = 5.5 m/s** | `TransGame.xxx Default__TnPlayerPawn.GroundSpeed` | CONF | **SUPERSEDED (Pass 11):** 14 m/s (Optimus_ROBODEF BaseGroundSpeed 1400) |
| Robot accel | **2048 UU/s² = 20.48 m/s²** | `Engine.xxx Default__Pawn.AccelRate` | CONF | **SUPERSEDED (Pass 11):** 120 m/s² (ROBODEF AccelRate 12000) |
| Robot air control | **0.70** | `Default__TnPlayerPawn.AirControl` | CONF | **SUPERSEDED (Pass 11):** 0.4 (ROBODEF) |
| Robot air speed | **1500 UU/s = 15 m/s** | `Default__TnPlayerPawn.AirSpeed` | CONF | **SUPERSEDED (Pass 11):** 12 m/s (ROBODEF AirSpeed 1200) |
| Robot max jump height | **625 UU = 6.25 m** | `Default__TnPawn._WorkingMovementCapabilities.MaxJumpHeight` | CONF | **SUPERSEDED (Pass 11):** 5.0 m (SharedAcrobatics.JumpHeight 500; JumpZ formula from ApplyTransformer; measured 5.12 m) |
| Pawn cylinder radius | **175 UU = 1.75 m** | `Default__TnPawn._WorkingMovementCapabilities.CylinderRadius` | CONF | **SUPERSEDED (Pass 11):** 2.0 m (Optimus_ROBODEF Collision) |
| Pawn cylinder half-height | **200 UU = 2.0 m** (full 4.0) | `…CylinderHeight` | CONF | **APPLIED** |
| Pawn base eye height | **80 UU = 0.8 m** (above centre → 2.8 m above feet) | `Default__TnPawn.BaseEyeHeight` | CONF | **SUPERSEDED (Pass 11):** Default__TnTransformer.BaseEyeHeight 150 → eye 3.5 m (trace origin); camera anchor 4.0 m (strategy Offset Z 200) |
| Robot walk pct | 0.5 (walk = 0.5× ground) | `Engine.xxx Default__Pawn.WalkingPct` | CONF | n/a (keyboard jogs) |
| Robot max fall speed | uncapped in movement | `…_WorkingMovementCapabilities.MaxFallSpeed=100000` | CONF | no terminal cap |
| HeightFog colour | **(234,91,116)** warm red-pink | `MP_IAC_Streets_ART_m HeightFogComponent.LightColor` | CONF | **APPLIED** (was blue-grey) |
| HeightFog density / start | 2e-5/UU (0.002/m) / 2048 UU | `…HeightFogComponent.Density/StartDistance` | CONF | APPLIED (GL_EXP, softened) |
| Vehicle max speed | **1500 UU/s = 15 m/s** | `Default__TnHoverCarSimulationBlueprint.MaxLinearSpeed` | CONF | **APPLIED** (was guessed 32) |
| Vehicle acceleration | **3000 UU/s² = 30 m/s²** | `…MaxLinearAcceleration` | CONF | **APPLIED** |
| Vehicle dash/boost speed | **5000 UU/s = 50 m/s** | `…DashSpeed` | CONF | **SUPERSEDED (Pass 11):** 30 m/s (HoverTruck_Physics DashSpeed 3000) |
| Vehicle dash duration | **0.3 s** | `…DashDuration` | CONF | **SUPERSEDED (Pass 11):** 0.5 s (HoverTruck_Physics) |
| Vehicle hover height | **200 UU = 2.0 m** | `…SuspensionRadius` | CONF | **SUPERSEDED (Pass 11):** 1.85 m (HoverTruck_Physics SuspensionRadius 185) |
| Vehicle jump speed | **1200 UU/s = 12 m/s** | `…JumpLinearSpeed` | CONF | APPLIED |
| Vehicle turn rate | **~π rad/s** (180°/s) | `…AiMaxAngularSpeed` | CONF | **SUPERSEDED (Pass 11):** AI-only field; player steering rate PROV |
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
- ~~Camera follow distance (9 m); ground-cam pitch limits~~ → 8 m / ±75° CONF (Pass 11). Mouse sensitivity still PROV.
- ~~MaxStepHeight (kStepUp 0.6 m)~~ → 0.35 m CONF (Gameplay Pass 10, TnRobotForm; Systems also found no override of the Engine.Pawn default 35 UU).
- Audio master level 0.5 and mixer-category / reverb treatment (weapon cue radii now CONF, Systems PASS 7; non-weapon transform/land cues still generic falloff).
- Locomotion crossfade time (0.15 s) for non-moving transitions (idle↔moving is CONF 0.2 s, Pass 10).

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
- ADS/fine-aim mode; camera recoil/shake; per-shot spread visualization.
- Prefab geometry; static-mesh collision; spatial/reverb audio.

---

## OPEN QUESTIONS → EVIDENCE NEEDED
- Robot GroundSpeed/JumpZ/AccelRate: read `TnPawn`/robot-form class defaults in `default.xex`
  (Ghidra/ReVa) or measure root motion from `Nav_*` clips.
- Camera follow distance/offset: HM camera behavior assets (cooked packages) or exe.
- Vehicle base speed/accel/turn: `TnCarForm`/`TnTruckForm` compiled defaults in exe.
- Lighting: lightmaps not extracted; dominant directional light direction/colour from map.
