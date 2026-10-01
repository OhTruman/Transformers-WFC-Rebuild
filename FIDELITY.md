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

## PASS 7 — WEAPON LAYERING, RECOIL, WEAPON MESH, FX, SOUNDCUES (Systems agent, 2026-10-01)
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

### PASS 8 — vehicle boost presentation (Systems agent)
Source: `TR_Optimus_VEHDEF_p.OptimusTruckForm` (TnTruckFormBlueprint) and its
`HmPlayerVehicleAudioComponent_6670`; sockets from `character.json` (VH_OptimusPrime_SKEL); FX from
`FX_Navigation_p.bumble_boost_small1_FX` (cooked in A1_IAC_Base_m); cues from `BL_VEH_OPTIMUS_PRIME`
via `SoundEvents_Vehicles_Trans.Veh_Optimus_Prime_SoundSet`.

| Item | Original (WFC) | Conf | Rebuild |
|---|---|---|---|
| Boost FX binding | `BoostFx` = BoostSocket_L / BoostSocket_R -> `bumble_boost_small1_FX` | CONF | **APPLIED** |
| Boost sockets | BoostSocket_L on L_Robo23_XT (0,-35,0) UU yaw -90 deg; BoostSocket_R on R_Robo23_XT (0,35,0) yaw +90 deg: the two exhaust stacks behind the cab | CONF | **APPLIED** (bone x socket, UE -> glTF via vs_common) |
| Ignition burst (EmitterLoops 1) | "thruster": 3 Boostermesh_02 cones, life U[0.3,0.5], scale U[(4,1,2),(5,1,1)] x size curve 0 -> 2.06 -> 1, colour (1,0.6,0.05); "Cone_thrust_Dup": bulletshape mesh, life U[0.3,0.4], scale (50,37.5,15) x 21-entry growth, colour (5,1.5,0.05) x 3; "Particle Emitter_Dup": SphereGlow sprite 30 UU x growth, colour (5,1.5,0.05) x 3, +25 UU | CONF data / MED roles | **APPLIED** |
| Looping while held | "loopcone": Boostermesh_02 at U[3,4]/s after a 0.1 s first-loop delay, life 1.0, scale U[(4,1,2),(5,1,1)] x U[1,1.1], colour (1.07,0.59,0.16); "Particle Emitter_Dup_Dup": SphereGlow at 20/s after 0.2 s, life U[0.4,0.6], 30 UU growing to x3.08, spin U[-0.75,0.75] turns/s, colour (0.8,0.8,0.3) | CONF data / MED roles | **APPLIED** |
| Alpha over life | 0 -> 1 at 20 % of life, linear to 0 (every emitter) | CONF | APPLIED |
| Local space / deactivation | every emitter bUseLocalSpace + bKillOnDeactivate | CONF | **APPLIED**: particles ride the sockets (turning, jumping); release kills them at once |
| Materials | Boostermaterial_02_MAT (additive, two-sided; LightBeam_Falloff_01 + DiffClouds + Spot), bumble_boostcone_MAT (additive, SphereGlow_01 x 1.2), Basic_Particle_Add_MAT (additive, SphereGlow_01) | CONF | APPLIED with the primary texture only (LightBeam_Falloff_01 for the cones) **PROV** |
| HDR colour | colours > 1 (x3 colour scale) | CONF | approximated by hue-preserving normalisation + GL x2/x4 overbright **PROV** |
| Boost audio | BoostSound Auto_Boost_Start -> VEH_OPTIMUS_BOOST_START (5 timed events); BoostLoops Auto_Boost_Loop -> VEH_OPTIMUS_BOOST_LOOP (5 looping layers from 0.44 s, volume/pitch curves on `Optimus_Prime_Speed` (Max 120) + time envelopes); BoostStopSound -> VEH_OPTIMUS_BOOST_END; BoostWheelsSound -> VEH_OPTIMUS_BOOST_WHEELS (50 % ChanceToPlayNone) after BoostWheelsGroundCheckDelay 0.27 s if grounded; BoostFadeOutTime 0.15 s | CONF | **APPLIED** |
| Speed parameter units | `Optimus_Prime_Speed` in mph (curves put nominal pitch at 33 = 15 m/s cruise) | MED | APPLIED |
| START cue curves | VolumeCurve/PitchCurve on an event of a cue with no root SoundParameter | MED | fed the speed parameter |
| Cue loop region | root LoopStart/LoopEnd | - | not used; looping layers loop their whole wave |
| Boost activation | presentation follows the movement code's boost condition (vehicle form + boost held, not transforming), so stationary boost shows the effect | MED | the 0.3 s DashDuration-vs-sustained question belongs to Gameplay movement |
| Material / emissive changes on boost | none authored on OptimusTruckForm (only overshield / defrag materials) | CONF (absence) | n/a |
| Camera feedback | not on the truck form or its audio component; camera behaviours belong to Gameplay | - | not applied |
| Related | HoverFX / JumpFX: PASS 9. RamFX + nitro: PASS 10. Engine / jump / land audio: PASS 11 | CONF data | done (tire squeal open) |

Cue system extension (used by the boost cues): `bLooping` wave events, VolumeCurve/PitchCurve keyed by the
root SoundParameter (SOUND_DISTANCE or speed), Envelope volume/pitch curves over playback time,
ChanceToPlayNone, live parameter/position updates and fade-out stop. The cue table is now generated
by `tools/systems/gen_cues.py` into `src/game/SoundCues.inc` (weapon cues regenerated unchanged).

### PASS 9 — hover thrusters and jump boosters (Systems agent)
Same source object as PASS 8 (`OptimusTruckForm`): `HoverFX` (6 x HoverBooster_* -> `CarHover_A_01_FX`)
and `JumpFX` (JumpBoostSocket_C/R/L -> `Jump_FX`). Implemented in the generic `VehicleFx` (which now also
carries the PASS 8 boost tables unchanged).

| Item | Original (WFC) | Conf | Rebuild |
|---|---|---|---|
| Hover sockets | HoverBooster_LFront/RFront on L/R_Wheel01_XB (scale 3), _LBack/RBack on Wheel02, _LBack2/RBack2 on Wheel03 (scale 2/2.5/2.5); +-13 UU, rotation (180, +-90, 0): emission axis down/out from the wheels | CONF | **APPLIED** incl. socket scale (positions, sizes, meshes scale with the socket) |
| Hover looping emitters | Rings_Dup (Ring_Distort_Add -> Ring_CLR, 3/s + 1, life U[0.35,0.5], 120 UU shrinking to 0.5, spin, colour (1,0.5,0.25)); lightcone_Dup (Light_Cylinder_STAT mesh, 10/s, life U[1.5,2], scale U[(0.3,0.3,0.075),(0.25,0.25,0.1)] x 1 -> 1.2 -> 1, alpha 0.5, brightness flicker 0.8-1.33, colour (1,0.1,0.1) x 2; NOT bKillOnDeactivate) | CONF data / MED roles | **APPLIED** |
| Hover one-shot on activation | Sparks_bolts (Spark_MAT, burst 10 + 100/s for 0.3 s, velocity U[(600,-50,100),(1200,50,400)] UU/s, colour (2.5,2,2) x 3); ElectroRing (lightningring_01, 20/s for 0.2 s, colour (1,0.5,0.2) x 3); Pulse (Boostermesh_03, 3-4/s for 0.5 s, scale (0.8,4,4), colour (2,0.1,0.1)) | CONF data / MED roles | **APPLIED** |
| Hover active state | no flag authored; the audio component's Hover vs Boost/Wheels land sounds and the boost wheels peel-out imply: hover thrusters whenever in vehicle form except while boosting | MED | **APPLIED**: vehicle form, not boosting, not transforming |
| Jump FX | 12 emitters, all EmitterLoops 1 / 0.5 s (0.3 / 0.2 s for sparks / electro ring), bKillOnDeactivate: glow bursts (SphereGlow 80-120 UU x5, colour (4,2,1) x 3), booster smoke (20/s, 9-10 m/s down), Boostermesh_03 thruster streaks / bases (burst 3, growth 0 -> 3 -> 1), Boost_Circuit energon cones, Spark_MAT burst 30 + tail sparks, electro ring (6,4,2) x 3, all 50 UU below the socket (socket X points straight down) | CONF data / MED roles | **APPLIED** |
| Jump trigger | not in data; the vehicle jump (JumpLinearSpeed 12 m/s) | MED | take-off edge with vy > 2 m/s; ends early only if the vehicle form ends |
| Light cylinder material | LightCylinder_Rays_MAT_INST -> LightVolume_Base_MAT: view-dependent volumetric (SideViewV, NearFade, DepthBias, dust panners); instance DustPower 0.1 | CONF params | **PROV**: drawn with LightBeam_Falloff_01 x DustPower 0.1 (shader graph not evaluated) |
| Spark_MAT | no texture; procedural streak from texture-coordinate math | CONF (absence) | **PROV**: generated soft-streak texture |
| Rings trailing (150,0,0) | role undecided (location vs velocity) | MED | used as velocity (as an offset it puts rings 4.5 m from the wheels) |
| Omitted | hover base_glow (Glow_Mod_MAT modulate), rays_Dup (Trail_Distort distortion); every material's secondary panning cloud/energon layers | - | not rendered |

### PASS 10 — truck nitro / ram (Systems state + FX + audio; movement effect owned by Gameplay)
Source: TransGame.TnTruckForm compiled UnrealScript (TransGame.xxx) — function/state names and float
literals in the getters' bytecode — plus `OptimusTruckForm.RamFX` and the audio component / sound set.

| Item | Original (WFC) | Conf | Rebuild |
|---|---|---|---|
| Trigger | state **Driving** (on wheels = boosting): `UpdateNitro` starts the nitro on the **DASH** input (`_Dashing`) when the cooldown allows; `Driving.EndState` calls `StopNitro`; in state **Hovering** dash is a plain hover dash (`DoDash`) | CONF (script structure) | **APPLIED** in `VehicleNitro`: driving = vehicle form + boost held, not transforming |
| Nitro duration | `get_NitroDuration` = **3.0 s** x NitroDurationModifier (1.0) | CONF | **APPLIED** |
| Speed scale | `get_NitroSpeedScale` = **1.5** x modifier | CONF | **exposed** (`speedScale()`), **not applied** — Gameplay owns movement |
| Steering scale | `get_NitroSteeringScale` = **0.3** x modifier | CONF | **exposed** (`steeringScale()`), **not applied** — Gameplay owns handling |
| Cooldown | `get_TimeBetweenNitros` = **8.0 s** x modifier | CONF value / MED reference point | **APPLIED**, measured from nitro start |
| Max ram mass | `get_MaxRamMass` = 1000 | CONF | exposed constant (ram collision is Gameplay's) |
| StartNitro side effects | RamFX on RamSocket, NitroForceFeedback (3 s), nitro camera state | CONF | RamFX **APPLIED**; force feedback / camera not (no rumble path; camera = Gameplay) |
| RamFX | `Truck_ram_FX` on RamSocket (C_Body_XB (380,0,-40) UU, scale (1,1.5,1.5)): Ram_STAT wedge mesh, 20/s, life 1.0, alpha 0.35, colour (2,1.8,1.3), -250 UU; dust + rays are distortion (omitted) | CONF data / MED roles | **APPLIED** |
| Ram_model_MAT | emissive = 2 x (vertex colour x c)^2, c = saturate(pow(1-N.V, FresnelExponent 2) x FresnelScaleUp 1.5) x 2 x lerp(A x L1, L1, 0.4) over four panning Flame_Tile layers | CONF graph | fresnel rim applied per vertex (squared); panning layers = one static Flame_Tile **PROV** |
| Nitro audio | NitroSound Auto_Ram_Nitro -> `VEH_OPTIMUS_RAM_NITRO_START` (7 events); CustomLoopingSound Auto_Ram_Alert -> `BL_VEH_SOUNDWAVE.VEH_TRUCK_RAM_ALERT` | CONF | **APPLIED** at nitro start; the alert plays once **MED** (component-level looping not decoded) |
| Ram impact audio | RamSound Auto_Ram_Impact -> `BL_VEH_SOUNDWAVE.VEH_TRUCK_RAM_IMPACT` (from AttemptToRam) | CONF | hook `World::notifyRamImpact(pos)` for Gameplay's ram collision |
| BoosterSound | Auto_Ram_Boost -> `VEH_OPTIMUS_RAM_BOOST_START` | CONF mapping | in the cue table; trigger not decoded, not played |
| Dash input | abstract `platform::Button::Dash` | - | **PROV** temporary key **Q** (no PC binding recovered; right mouse reserved for Gameplay fine-aim). Final mapping = Gameplay |

**Value conflict to resolve in Gameplay (documented, not changed here):** the rebuild's vehicle boost uses
`Default__TnHoverCarSimulationBlueprint` DashSpeed 5000 UU/s / DashDuration 0.3 s (core::config
kVehicleBoostSpeed / kVehicleDashTime). Optimus's truck actually references `VEH_SHARED_p.HoverTruck_Physics`
(TnHoverCarSimulationBlueprint) with **DashSpeed 3000 UU/s, DashDuration 0.5 s**, SuspensionRadius 185 UU,
and, for the Driving (wheels) state, `VEH_SHARED_p.Truck_Physics` (TnCarPhysicsBlueprint) with MaxSpeed
3000 UU/s, MaxAcceleration 2500, JumpLinearVelocity (600,0,1400), Mass 2500. The hover DASH is a separate
mechanic from the nitro (Hovering.DoDash vs Driving nitro). Systems did not modify any vehicle movement value.

### PASS 11 — vehicle engine audio (Systems agent)
Source: `OptimusTruckForm.HmPlayerVehicleAudioComponent_6670` and `Veh_Optimus_Prime_SoundSet`; cues generated
from `BL_VEH_OPTIMUS_PRIME` (tools/systems/gen_cues.py).

| Item | Original (WFC) | Conf | Rebuild |
|---|---|---|---|
| Drive loops | DriveSounds gears (MaxSpeed 20, 110): OnLoadLoops Auto_Engine_Gear_1_OnLoad -> `VEH_OPTIMUS_DRIVE_ONLOAD` (2 looping layers), OffLoadLoops -> `VEH_OPTIMUS_DRIVE_OFFLOAD` (3 looping layers incl. idle); one-shots map to None; ReverseSound maps to the same cues | CONF | **APPLIED**; both gears share the cues, so gear selection is audible only through the speed curves |
| Speed response | every layer's volume/pitch curves on `Optimus_Prime_Speed` (mph, Max 120) | CONF curves / MED units | APPLIED |
| On-load vs off-load | native component logic not decoded | MED | on-load while throttle input is held, off-load otherwise |
| Transitions | EngineFadeOutTime 0.2 s | CONF | APPLIED (outgoing loop fades 0.2 s) |
| Airborne | JumpRevSounds UseJumpRev, Auto_Jump_Loop -> `VEH_OPTIMUS_DRIVE_JUMP_LOOP` | CONF | APPLIED while airborne |
| Jump start | AscendSound Auto_Jump_Start -> `VEH_OPTIMUS_DRIVE_JUMP_START` | CONF | APPLIED on take-off |
| Landing | HoverLandSound {0.15 s: `HOVER_LAND_LIGHT`, 2.0 s: `HOVER_LAND_HEAVY`}, BoostLandSound {0.15 s: `WHEELS_LAND_LIGHT`, 2.0 s: `WHEELS_LAND_HEAVY`} by time in air | CONF | APPLIED (wheels variant while boosting); the robot-form placeholder thump no longer plays in vehicle form |
| Boost | the boost loop cue carries its own engine layers | MED | the drive loop yields to `VEH_OPTIMUS_BOOST_LOOP` while boosting |
| Tire squeal | TireSquealSoundParameter / Auto_Tire_Squeal_Default -> `VEH_OPTIMUS_TIRE_SQUEAL`, crossfade 0.5, TireSquealSpeedMin 20 | CONF data | **not applied** (needs a lateral-slip signal from Gameplay's vehicle handling) |

### VEHICLE MECHANICS — normal boost vs hover dash vs ram/nitro (consolidated, Systems checkpoint)
Three distinct mechanics in the original; do not conflate them. Values marked CONF are authored data or
compiled-script literals; "current rebuild" is what the movement code uses today (Gameplay-owned, unchanged
by Systems).

| Mechanic | Original trigger / state | Authored values | Current rebuild | Owner |
|---|---|---|---|---|
| **Normal boost** (drive on wheels) | holding Boost switches TnCarForm from state **Hovering** (HoverBlueprint) to **Driving** (CarBlueprint, wheels on the ground); `get_IsBoosting` / `set_BoostingInput`. Original PC binding: **right mouse in vehicle form** (same button as robot Fine Aim) | `VEH_SHARED_p.Truck_Physics` (TnCarPhysicsBlueprint): MaxSpeed **3000 UU/s (30 m/s)**, MaxAcceleration **2500**, Mass 2500, JumpLinearVelocity (600,0,1400). Presentation (CONF): BoostFx bumble_boost_small1_FX on BoostSocket_L/R; BOOST_START / BOOST_LOOP / BOOST_END cues, BoostFadeOutTime 0.15 s, BoostWheelsGroundCheckDelay 0.27 s | movement: Sprint (Shift) held, top speed `kVehicleBoostSpeed` 50 m/s reached over `kVehicleDashTime` 0.3 s (taken from the hover-sim **class default** DashSpeed/DashDuration, not Truck_Physics). Presentation: Systems PASS 8 | movement + binding: Gameplay; FX/audio: Systems |
| **Hover dash** | DASH input while **Hovering**: `TnTruckForm.Hovering.DoDash` -> TnHoverCarSimulation dash (`_DashTimeRemaining`, TimeBetweenDashes) | `VEH_SHARED_p.HoverTruck_Physics` (Optimus's actual hover sim): **DashSpeed 3000 UU/s (30 m/s), DashDuration 0.5 s**, SuspensionRadius 185 UU; class defaults (`Default__TnHoverCarSimulationBlueprint`): DashSpeed 5000, DashDuration 0.3, MaxLinearSpeed 1500, accel 3000, JumpLinearSpeed 1200, SuspensionRadius 200 | **not implemented as a separate mechanic**; its class-default values currently drive the normal boost (see above). Systems reads no hover-dash state | Gameplay |
| **Ram / nitro** | DASH input while **Driving** (boosting on wheels): `TnTruckForm.Driving.UpdateNitro` -> `StartNitro`; leaving Driving (`EndState`) -> `StopNitro` | script literals: NitroDuration **3.0 s**, NitroSpeedScale **x1.5**, NitroSteeringScale **x0.3**, TimeBetweenNitros **8.0 s**, MaxRamMass **1000**; StartNitro: RamFX (Truck_ram_FX on RamSocket), NitroForceFeedback 3 s, nitro camera state; NitroSound VEH_OPTIMUS_RAM_NITRO_START, CustomLoopingSound VEH_TRUCK_RAM_ALERT, RamSound VEH_TRUCK_RAM_IMPACT | state/timer/cooldown + RamFX + audio: **Systems** (`VehicleNitro`, abstract `Dash` action, PROV key Q); speed/steering scales **exposed, not applied**; nitro camera + force feedback not applied | state/FX/audio: Systems; speed, steering, handling, ram collision, camera, final binding: Gameplay |

Input notes: `Dash` is an abstract action (no PC binding recovered; temporary **Q**, PROVISIONAL). Right mouse is
deliberately left unbound by Systems so Gameplay can map it per the original: **robot form = Fine Aim,
vehicle form = Boost**.

---

## PASS 6 — PLAYER-CONTROL, ANIMATION & WEAPON PRESENTATION (2026-10-01)
Driven by replaying the exe (runtime observation overrides headless smoke). Priority order as
the player reported it.

| Behaviour | Original (WFC) | Source | Conf | Rebuild status |
|---|---|---|---|---|
| Body facing | Robot faces the **aim/camera (mouse) yaw every frame**; WASD = move dir relative to it (strafe shooter). User confirmed: "camera facing = character facing; WASD = move direction." | Observed original + user | HI | **APPLIED** — `CharacterMovement` robot `setYaw(faceYaw)` always; removed the stand-then-walk snap. `face·toCam≈−0.9` verified. |
| Mesh yaw offset | Extracted meshes align to the rebuild's −Z-forward yaw with **no** extra rotation. | Runtime geometry check | CONF | **APPLIED** `kMeshYawOffset=0` (earlier "+90°" was a diagnostic-ordering artifact, reverted). |
| Vehicle facing | Faces its **travel direction** (steering), not the aim. | Observed | MED | **APPLIED** `yaw=atan2(-vx,-vz)` when moving. |
| Directional locomotion | `Nav_Strafe{Jog,Walk}_{F/B/L/R}` chosen by travel dir **relative to facing**. | `robot.glb` clip set (category `run`/`walk`) | HI | **APPLIED** (dot of velocity with facing fwd/right). Verified strafe-R → `Nav_StrafeJog_R`. |
| Upper-body aim offset | Robot_ANIMTREE `TnAnimNodeAimOffset_14979`, profile **Default**: 11 bones (spine chain, head, both arms) x 9 authored rotations (L/C/R x U/C/D) baked from `Shooting_Aim_*`; ranges H [-1,1] V [-1,0.8]; RemapPawnAimRange from pawn H [-1,0.85] V [-0.7,1] | `TR_Shared_ANIMTREE_p.Robot_ANIMTREE` | CONF (data) / MED (remap) | **APPLIED** (Systems PASS 7b): mesh-space increments in hierarchy order, below the reload slot. Verified: aim 22.9 deg -> barrel 21.6-23.8 deg while jogging + firing. |
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
| Vehicle turn rate | **~π rad/s** (180°/s) | `…AiMaxAngularSpeed` | CONF | PARTIAL (facing is instant) |
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
- Audio master level 0.5 and mixer-category / reverb treatment (weapon cue radii now CONF, PASS 7).
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
- ADS/fine-aim mode; camera recoil/shake; per-shot spread visualization.
- Prefab geometry; static-mesh collision; spatial/reverb audio.

---

## OPEN QUESTIONS → EVIDENCE NEEDED
- Robot GroundSpeed/JumpZ/AccelRate: read `TnPawn`/robot-form class defaults in `default.xex`
  (Ghidra/ReVa) or measure root motion from `Nav_*` clips.
- Camera follow distance/offset: HM camera behavior assets (cooked packages) or exe.
- Vehicle base speed/accel/turn: `TnCarForm`/`TnTruckForm` compiled defaults in exe.
- Lighting: lightmaps not extracted; dominant directional light direction/colour from map.
