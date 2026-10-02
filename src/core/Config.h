// Clean-room reconstruction — tunable constants.
// Provenance tags: [CONF]=recovered from authored WFC data (see FIDELITY.md),
// [PROV]=provisional placeholder not yet confirmed against the original.
#pragma once

namespace core::config {

// Window
constexpr int   kWindowWidth  = 1280;
constexpr int   kWindowHeight = 720;
constexpr const char* kWindowTitle = "WFC Rebuild";

// Simulation
constexpr double kSimHz = 60.0;

// World gravity. [CONF] WorldInfo.DefaultGravityZ = -2940 UU/s^2 -> -29.4 m/s^2
// (Xe-TransGame.ini [Engine.WorldInfo]). Vehicles additionally scale RB gravity by 0.66.
constexpr float kGravity          = 29.4f;   // m/s^2 (pawn)
constexpr float kVehicleGravity   = 19.4f;   // m/s^2 (-2940 * 0.66 / 100) [CONF]
constexpr float kGroundY = 0.0f;

// Robot movement. [CONF] TnPawn.ApplyTransformer copies the character definition
// (TR_Optimus_ROBODEF_p.Optimus_ROBODEF, a TnTransformer) onto the pawn at spawn: AccelRate,
// set_BaseGroundSpeed(BaseGroundSpeed), set_BaseAirSpeed(AirSpeed), AirControl, collision size,
// MaxJumpHeight = AcrobaticsManagerBlueprint.JumpHeight, JumpZ = sqrt(-2*GravityZ*JumpHeight).
// All six playable ROBODEFs checked share 1400/12000/1200/0.4/SharedAcrobatics. (Passes 2/10 used
// the TnPlayerPawn/Engine.Pawn class defaults 550/2048/1500/0.7, which ApplyTransformer replaces.)
constexpr float kRobotMoveSpeed   = 14.0f;   // BaseGroundSpeed 1400 UU/s [CONF]
constexpr float kRobotAccel       = 120.0f;  // AccelRate 12000 UU/s^2   [CONF]
constexpr float kRobotMaxJumpH    = 5.0f;    // SharedAcrobatics.JumpHeight 500 UU [CONF]
constexpr float kRobotJumpSpeed   = 17.146f; // JumpZ = sqrt(2*29.4*5.0) m/s [CONF formula, ApplyTransformer]
constexpr float kAirControl       = 0.40f;   // AirControl 0.4          [CONF]
constexpr float kAirSpeed         = 12.0f;   // AirSpeed 1200 UU/s      [CONF]
constexpr float kRobotTerminalVel = 60.0f;   // TerminalVelocity 6000 UU/s [CONF]
// TnPawn.CalculateDesiredFlatVelocity: any movement input targets at least 450 UU/s [CONF].
constexpr float kRobotMinMoveSpeed = 4.5f;
// TnTransformerMomentumBlueprint TR_Acrobatics_p.TruckTransformerMomentum [CONF]. Used by
// TnPawn.CalculateMomentumPreservation only while flat speed exceeds the max speed:
// MaxAcceleration = AccelRate / (1 + Preservation); the InAir set applies while falling OR
// transforming (TnPawn.CalculateMaxAcceleration / CalculateMomentumPreservation bytecode).
constexpr float kMomentumGroundFwd = 7.0f, kMomentumGroundNeutral = 3.0f, kMomentumGroundBack = 1.0f;
constexpr float kMomentumAirFwd = 100.0f, kMomentumAirNeutral = 100.0f, kMomentumAirBack = 5.0f;
constexpr float kMomentumFwdCos = 0.866f;    // input within 30 deg of the velocity = "forward" [CONF]

// Pawn collision cylinder. [CONF] Optimus_ROBODEF.Collision (ApplyTransformer SetCollisionSize).
constexpr float kPawnRadius       = 2.0f;    // CollisionRadius 200 UU [CONF] (Default__TnPawn: 175)
constexpr float kPawnHalfHeight   = 2.0f;    // CollisionHeight 200 UU [CONF] (full height 4 m)
// BaseEyeHeight: Optimus_ROBODEF keeps Default__TnTransformer.BaseEyeHeight 150 UU (applied by
// ApplyTransformer) -> eye 2.0 + 1.5 m above the feet; used as the weapon trace origin [CONF].
constexpr float kEyeHeight        = 3.5f;

// Vehicle movement. [CONF] recovered from Default__TnHoverCarSimulationBlueprint (TransGame.xxx)
// — WFC ground vehicles HOVER. (TnCarSimulationBlueprint = non-hover, MaxSpeed 500; unused here.)
constexpr float kVehicleMoveSpeed = 15.0f;   // MaxLinearSpeed 1500 UU/s        [CONF]
constexpr float kVehicleAccel     = 30.0f;   // MaxLinearAcceleration 3000 UU/s^2 [CONF]
// Three distinct vehicle mechanics (RE notes, Systems checkpoint; TnCarForm/TnTruckForm/TnHoverCarSimulation
// bytecode). Do not conflate them:
//  1) Normal boost: Boost held -> TnCarForm state Driving (wheels, VEH_SHARED_p.Truck_Physics).
//  2) Hover dash: Dash input while Hovering -> TnHoverCarSimulation.Dash (HoverTruck_Physics).
//  3) Ram/nitro: Dash input while Driving -> TnTruckForm.Driving nitro (script literals).
// (1) Truck_Physics (TnCarPhysicsBlueprint) [CONF]:
constexpr float kTruckDriveSpeed  = 30.0f;   // MaxSpeed 3000 UU/s
constexpr float kTruckDriveAccel  = 25.0f;   // MaxAcceleration 2500 UU/s^2
constexpr float kWheelsDropTime   = 0.27f;   // BoostWheelsGroundCheckDelay: wheels reach the ground [CONF value, use PROV]
// (2) Hover dash [CONF]: HoverTruck_Physics overrides + TnHoverCarSimulation.UpdateDash literals.
constexpr float kVehicleBoostSpeed= 30.0f;   // hover DashSpeed 3000 UU/s (class default 5000)
constexpr float kVehicleDashTime  = 0.5f;    // hover DashDuration 0.5 s (class default 0.3)
constexpr float kHoverDashAccel   = 1000.0f; // get_DashAcceleration / get_DashDeceleration 100000 UU/s^2
constexpr float kHoverDashCooldown = 2.0f;   // TnCarForm.get_TimeBetweenDashes = 2.0 x modifier
// (3) Nitro [CONF script literals, TnTruckForm getters]:
constexpr float kNitroDuration    = 3.0f;
constexpr float kNitroSpeedScale  = 1.5f;
constexpr float kNitroSteerScale  = 0.3f;
constexpr float kNitroCooldown    = 8.0f;    // TimeBetweenNitros (measured from nitro start)
// Hover steering authority after entering Hovering (TnCarForm.Hovering.BeginState -> Drift):
// accel scale = (1 - DriftTimeRemaining/DriftDuration)^2 [CONF bytecode, DriftDuration class default 0.5].
constexpr float kHoverDriftDuration = 0.5f;
// SUPERSEDED (Pass 13): SuspensionRadius is the horizontal radius of the four hover suspension
// mounts (TnHoverCarSimulation.CalculateSuspensionLocation), not a ride height; the ride height comes
// from the spring model below. Kept for the fidelity harness, unused by movement.
constexpr float kVehicleHoverH    = 1.85f;
// TnVehicleForm.OnActivate: Velocity = ClampLength(pawn Velocity, 3500) is handed to the rigid body
// with the pawn rotation when the vehicle form activates (start of robot->vehicle) [CONF].
constexpr float kMaxTransformSpeed = 35.0f;  // TnVehicleForm.kMaxTransformSpeed 3500 UU/s
constexpr float kVehicleJumpSpeed = 12.0f;   // JumpLinearSpeed 1200 UU/s       [CONF]
// [PROV] player steering rate not recovered. (Pass 7's source, AiMaxAngularSpeed, is an AI-only
// field and is 20 rad/s for the Optimus truck.)
constexpr float kVehicleTurnRate  = 3.1416f;

// Hover rigid body (Pass 13) [CONF bytecode + authored data unless marked]:
//  TnCarSimulation.InitializeFromBlueprint: RB Mass, LocalCenterOfMass = CenterOfMass - (0,0,ChassisOffset),
//  LocalInertiaTensor from VEH_SHARED_p.Truck_Physics (2500; (-47,0,20); 2.27e7/4.64e7/5.89e7 kg UU^2) and
//  TR_Optimus_VEHDEF_p.OptimusTruckForm ChassisOffset 15.
//  TnHoverCarSimulation.UpdateSuspension: 4 TnSuspension rays from COM + Normal(1,1,0)*SuspensionRadius
//  rotated by 90 deg steps, along body -Z, length RestingLength (WheelRadius 0); TnSpring implicit update
//  (K = Stiffness/m, B = Damping/m, m = Mass/4) with the world gravity; force applied along body up at the
//  mount, scaled by Dot(up, contact normal). HoverTruck_Suspension RestingLength 250, Damping 4000;
//  Stiffness = class default 10000. Activate(): linear and angular damping 0.
constexpr float kVehMass         = 2500.0f;     // kg
constexpr float kVehInertiaX     = 2270.0f;     // kg m^2 (roll axis)
constexpr float kVehInertiaY     = 4640.0f;     // kg m^2 (pitch axis)
constexpr float kVehComFwd       = -0.47f;      // m, body-local COM forward of the mesh root
constexpr float kVehComUp        = 0.05f;       // m, body-local COM above the mesh root
constexpr float kSuspMountRadius = 1.85f;       // m
constexpr float kSuspRestLength  = 2.5f;        // m
constexpr float kSuspStiffness   = 10000.0f;    // TnSpring units: K = Stiffness / (Mass/4)
constexpr float kSuspDamping     = 4000.0f;
constexpr float kHoverCosGroundAngle = 0.707f;  // get_CosGroundAngle: IsOnTheGround = ContactNormal.Z > 0.707
constexpr float kHoverStabilityDeg = 30.0f;     // get_StabilityThreshold: |pitch| or |roll| > 30 = unstable
constexpr float kHoverTerminalVel  = 35.0f;     // get_TerminalVelocity 3500
constexpr float kHoverUprightPerTick = 0.05f;   // UpdateTurn TurnRate (0.05,0.05,1) per tick, only when no
                                                // contact or upside down [per-tick factor; 30 Hz tick PROV]
constexpr float kHoverJumpAngSpeed = 1.0f;      // JumpAngularSpeed (class default 1): local -Y = nose up
constexpr float kVehJumpInterval   = 0.3f;      // TnCarForm.get_TimeBetweenJumps
constexpr float kHoverMinClearance = 0.6f;      // [PROV] chassis-vs-ground contact (PhysicalVehicleMesh hull not recovered)
// Driving (TnCarSimulation + Truck_Physics / class defaults) [CONF bytecode unless marked]:
constexpr float kDriveLowSpeedBoostScale = 8.0f;     // ExtraBoost = MaxAccel*8 at 0 speed ...
constexpr float kDriveLowSpeedBoostThreshold = 0.5f; // ... falling quadratically to 0 at MaxSpeed*0.5
constexpr float kDriveMaxExtraAccel = 50.0f;         // MaxExtraAcceleration 5000
constexpr float kPawnTerminalVel    = 60.0f;         // drag reference: Owner.GetTerminalVelocity() 6000
constexpr float kDriveJumpFwd = 6.0f, kDriveJumpUp = 14.0f;   // Truck_Physics JumpLinearVelocity (600,0,1400), local
constexpr float kDriveJumpAngVel   = 2.0f;           // JumpAngularVelocity class default 2 (nose up)
constexpr float kDriveJumpBoostTime = 0.7f;          // get_JumpBoostDuration
constexpr float kDriveAirTurnAccel = 12.0f;          // AirControlTurnAcceleration (rad/s^2 x steering)
constexpr float kDriveAirStrafeAccel = 26.0f;        // AirControlStrafeAcceleration 2600
constexpr float kDrivePitchFwdLimit = -0.43633f;     // -25 deg
constexpr float kDrivePitchFwdAccel = 3.0f;          // rad/s^2 nose-down while airborne above the limit
constexpr float kDriveAngularDamping = 5.0f;         // AngularDamping class default; x (1-|steer|)^2 on wheels
constexpr float kDriveTerminalVel = 35.0f, kDriveLandingVel = 20.0f, kDriveLandingTrace = 10.0f;
constexpr float kDriveTurnRate = 3.1416f;            // [PROV] wheel/tire steering not recovered: yaw rate at full steer
constexpr float kDriveLateralGrip = 8.0f;            // [PROV] lateral velocity decay on wheels (1/s)
constexpr float kDriveMouseSteer = 0.012f;           // [PROV] PC mouse delta -> GetNormalizedTurn

// Camera. Robot strategy [CONF] CAM_Strategies_p.OverTheShoulder_STRATEGY (FOV is HORIZONTAL,
// UE3 convention; converted to vertical per aspect in render::Camera):
//   TnFovCameraBehavior DefaultFOV 80 / SmoothTime 0.4; FOVsByPCS TnPCS_FineAim 45 / 0.1
//   TnLocationOffsetCameraBehavior DefaultOrbitDistance 800 (no generic FineAim override)
//   HmOffsetAnchorPointRelativeToActorCameraBehavior Offset Z 200 (above the actor = cylinder centre)
//   TnOrbitRotationCameraBehavior look Yaw/Pitch 50/25, FineAim 25/12.5, PitchRange -75..75
constexpr float kCamFovXDeg    = 80.0f;    // horizontal FOV [CONF strategy instance; ini class default 75]
constexpr float kCamFovSmooth  = 0.4f;     // DefaultFOV SmoothTime [CONF]
constexpr float kMouseSens     = 0.0022f;  // radians per pixel [PROV]
constexpr float kCamDistance   = 8.0f;     // DefaultOrbitDistance 800 UU [CONF]
constexpr float kCamHeight     = kPawnHalfHeight + 2.0f; // actor centre + anchor Offset Z 200 UU [CONF]
constexpr float kPitchMin      = -1.309f;  // PitchRange -75 deg [CONF]
constexpr float kPitchMax      =  1.309f;  // PitchRange +75 deg [CONF]
// Fine aim (robot only; TnPlayerController.PlayerWalking.FineAim -> TnFineAimManager).
constexpr float kFineAimSpeedMult  = 0.5f;  // _GroundSpeedMultiplier -> SetSpeedMultiplier [CONF]
constexpr float kFineAimFovXDeg    = 45.0f; // TnPCS_FineAim FOV [CONF]
constexpr float kFineAimFovSmooth  = 0.1f;  // TnPCS_FineAim SmoothTime [CONF]
constexpr float kFineAimLookScale  = 0.5f;  // FineAim look speed 25/12.5 vs default 50/25 [CONF ratio]
constexpr float kFineAimSpreadMult = 0.5f;  // IonBlaster WEPDATA FineAimSpreadModifier [CONF]
// TnScreenSpaceOffsetByPitchCameraBehavior: Offsets is a static array of THREE vectors (orbit space:
// X toward the anchor, Y right, Z up) evaluated at pitch fraction 0 / 0.5 / 1 of PitchRange and added to
// the orbit offset (-OrbitDistance, 0, 0) before the orbit rotation [CONF bytecode + raw property data].
// Pass 11 read the first vector only. OverTheShoulder: (150,300,150) (150,300,-35) (150,300,150), 0.3 s;
// [TnPCS_FineAim]: (-50,300,80) (-50,300,-35) (-50,300,80), 0.1 s.
constexpr float kShoulderX = 1.5f, kShoulderY = 3.0f, kShoulderZEnd = 1.5f, kShoulderZMid = -0.35f;
constexpr float kFineAimShoulderX = -0.5f, kFineAimShoulderZEnd = 0.8f;
constexpr float kShoulderOffsetSmooth = 0.3f;
constexpr float kFineAimOffsetSmooth  = 0.1f;
// Strategy blend when the active camera strategy changes (HmCameraStrategy.TransitionTime of the NEW
// strategy): OverTheShoulder 1.5, HoverTruck_Optimus 1.5, Truck_Optimus 1.0 s [CONF]. Remap curve PROV.
constexpr float kCamTransRobot = 1.5f, kCamTransHover = 1.5f, kCamTransDrive = 1.0f;
// Vehicle camera strategies [CONF CAM_Driving_Strategies_p]:
//  HoverTruck_Optimus_STRATEGY: anchor Offset Z 185 over the actor; FOV 80 / 0.4; orbit 950; look 50/25,
//   PitchRange -20..30; HmOrbitSmoother SmoothTime 0.1 (rotation); screen offset (0,0,45) (0,0,0)
//   (0,0,120) over pitch -25..25, 0.5 s; Wiggler3 rotation 0.06 deg @ 12 Hz (full at 100 UU/s).
//  Truck_Optimus_STRATEGY (Driving): anchor 215; FOV 85 / 0.4, TnPCS_Boosting (nitro) 100 / 0.5; orbit
//   1050, Boosting 650 (in 0.5 s, out 2.0 s); TnDrivingOrbitRotation: yaw = pawn yaw, pitch chases
//   Lerp(pawn pitch, velocity pitch, |v|/3000) at MatchRotationSpeed 3, PitchRange -25..25; HmOrbitSmoother
//   0.25; screen offset (0,0,45) (0,0,0) (0,0,120), 1.0 s; Wiggler3 0.15 deg.
constexpr float kHoverCamAnchor = 1.85f, kHoverCamDist = 9.5f, kHoverCamFov = 80.0f;
constexpr float kHoverCamPitchMin = -0.34907f, kHoverCamPitchMax = 0.52360f;   // -20 / +30 deg
constexpr float kHoverCamRotSmooth = 0.1f, kHoverCamOffsetSmooth = 0.5f, kHoverCamWiggleDeg = 0.06f;
constexpr float kDriveCamAnchor = 2.15f, kDriveCamDist = 10.5f, kDriveCamFov = 85.0f;
constexpr float kNitroCamDist = 6.5f, kNitroCamFov = 100.0f, kNitroCamFovSmooth = 0.5f;
constexpr float kNitroCamDistIn = 0.5f, kNitroCamDistOut = 2.0f;
constexpr float kDriveCamPitchMin = -0.43633f, kDriveCamPitchMax = 0.43633f;    // -25 / +25 deg
constexpr float kDriveCamRotSmooth = 0.25f, kDriveCamOffsetSmooth = 1.0f, kDriveCamWiggleDeg = 0.15f;
constexpr float kDriveCamMatchRate = 3.0f;
constexpr float kVehCamOffsetZLow = 0.45f, kVehCamOffsetZHigh = 1.2f;
constexpr float kVehCamOffsetPitch = 0.43633f;   // offset curve PitchRange +-25 deg

// Mesh facing offset. [CONF] extracted meshes keep UE's +X-forward convention (v_gltf maps
// UE +X -> gltf +X), while our yaw/camera use -Z-forward; a +90 deg model rotation aligns them.
// Evidence: the authored straight-ahead aim pose Shooting_Aim_F_C points the Ion Blaster barrel
// along model (1.00,-0.06,-0.02), i.e. +X (logged at load as "aim rig: ... barrel dir").
constexpr float kMeshYawOffset = 1.5707963f;   // rotateY(+90deg): mesh +X -> world forward (-Z)

// Weapon muzzle. [CONF-derived] Ion Blaster barrel-tip centroid in weapon-local gltf metres
// (frontmost vertex slice of weapon.glb). The MuzzleFlash socket transform was not extracted;
// this is the geometric barrel tip, where the tracer/flash should originate.
constexpr float kMuzzleLocalX = 2.063f;
constexpr float kMuzzleLocalY = 0.017f;
constexpr float kMuzzleLocalZ = 0.141f;

// Transformation. [CONF] TnTransformation._BlendInTime / _BlendOutTime (Xe-TransGame.ini).
constexpr float kTransformBlendIn  = 0.115f; // s [CONF]
constexpr float kTransformBlendOut = 0.25f;  // s [CONF]
// Robot and vehicle each carry a transform clip of matching duration (ToVehicle 1.97 s,
// ToRobot 1.13 s) authored in lockstep: one continuous fold seen from each mesh. We play the
// outgoing mesh's clip up to this fraction, then hand off to the incoming mesh's clip resumed at
// the SAME normalized time, so the fold is continuous instead of two sequential animations.
// [PROV] exact cross-fade/visibility handoff point not yet recovered; midpoint minimises the
// unavoidable cross-mesh pop (different vertex counts can't be vertex-blended).
constexpr float kTransformHandoffFrac = 0.5f; // SUPERSEDED (Pass 13) by the authored visibility notifies
// Authored ToggleHidden notifies [CONF AssetTools EVIDENCE_TARGETED_PASS 5a-5d], clip time in seconds:
//   Transform_ToVehicle_ROBO Hide @0.880; Transform_ToVehicle_VEH Unhide @0.396;
//   Transform_ToRobot_ROBO Unhide @0.098; Transform_ToRobot_VEH Hide @0.663.
// Both clips of a pair start together and have identical lengths, so both meshes are drawn in the
// overlap (0.396-0.880 s to vehicle, 0.098-0.663 s to robot) on the shared clip time.
constexpr float kToVehRobotHide = 0.880f, kToVehVehicleShow = 0.396f;
constexpr float kToRobotRobotShow = 0.098f, kToRobotVehicleHide = 0.663f;
constexpr float kLocomotionBlend   = 0.15f;  // s [PROV] crossfade between locomotion clips
// Robot Moving state [CONF Robot_ANIMTREE]: TnVelocityAnimBlend MinSpeed 450 / MaxSpeed 1200 UU/s
// (walk -> jog; the clips' authored ground speeds are ~3.5 / ~12.1 m/s), TnStraferAnimBlend
// _BlendSpeed 0.2, Idle<->Moving AmpCrossFadeCondition TransitionTime 0.2 s.
constexpr float kVelBlendMinSpeed  = 4.5f;   // m/s [CONF]
constexpr float kVelBlendMaxSpeed  = 12.0f;  // m/s [CONF]
constexpr float kStraferBlendTime  = 0.2f;   // s [CONF value; used as an easing time PROV]
constexpr float kIdleMoveBlend     = 0.2f;   // s [CONF]
// Animation layers. Upper-body slot (reload) / aim-offset / hover-additive weight ease time, and
// the minimum airborne time before Nav_Land plays on touchdown (filters curb step-offs).
constexpr float kSlotBlend         = 0.15f;  // s [PROV]
constexpr float kLandMinAirTime    = 0.3f;   // s SUPERSEDED (Pass 13): SharedAcrobatics.LandingAnims table in Character
// Turn in place. [CONF] TransGame.Default__TnAnimTurnInPlace: TransitionThresholdAngle 4096 UU,
// TransitionBlendTime 0.1, PercentageToAllowAbort 0.5. Aim offset InterpSpeed [CONF]
// Default__TnAnimNodeAimOffset 12.
constexpr float kTurnThreshold       = 0.3926991f; // 4096 UU = 22.5 deg [CONF]
constexpr float kTurnTransitionBlend = 0.1f;       // s [CONF]
constexpr float kTurnAbortPct        = 0.5f;       // [CONF]
// Transformation mesh offset: TnPawn.Transforming.OnUpdate OffsetMeshes(Remaining/0.5 * Shift)
// [CONF shape]; used here to absorb the height change when the movement form switches at fold start.
constexpr float kTransformShiftBlend = 0.5f;
// Weapon restore on vehicle->robot [CONF, Xe-TransGame.ini TnPawn._RestoreWeaponTransformFractionRemaining
// = 0.75 => restored when 25% of the fold has elapsed], usable after the weapon's EquipTime 0.2 s [CONF].
constexpr float kRestoreWeaponElapsed = 0.25f;
constexpr float kWeaponEquipTime      = 0.2f;
// Input latching [CONF RE]: reload fires on release of a tap shorter than this.
constexpr float kReloadTapTime        = 0.3f;
constexpr float kAimInterpSpeed      = 12.0f;      // [CONF]
// Aim offset "Default" profile ranges [CONF Robot_ANIMTREE TnAnimNodeAimOffset]: profile
// Horizontal [-1,1] / Vertical [-1,0.8]; RemapPawnAimRange with PawnAimOffsetRange
// Horizontal [-1,0.85] / Vertical [-0.7,1] (pawn aim as a fraction of 90 deg).
constexpr float kAimProfHMin = -1.0f,  kAimProfHMax = 1.0f;
constexpr float kAimProfVMin = -1.0f,  kAimProfVMax = 0.8f;
constexpr float kAimPawnHMin = -1.0f,  kAimPawnHMax = 0.85f;
constexpr float kAimPawnVMin = -0.7f,  kAimPawnVMax = 1.0f;

// Asset source: local, legally-owned extracted vertical slice (never committed).
// Override at runtime with the WFC_ASSETS environment variable.
constexpr const char* kAssetRootDefault = "F:/Transformers Rebuild/ExtractedAssets/VerticalSlice";

} // namespace core::config
