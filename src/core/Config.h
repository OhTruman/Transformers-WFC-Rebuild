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
// Optimus truck overrides [CONF] VEH_SHARED_p.HoverTruck_Physics (OptimusTruckForm.HoverBlueprint):
constexpr float kVehicleBoostSpeed= 30.0f;   // DashSpeed 3000 UU/s (class default 5000) [CONF]
constexpr float kVehicleDashTime  = 0.5f;    // DashDuration 0.5 s (class default 0.3)   [CONF]
constexpr float kVehicleHoverH    = 1.85f;   // SuspensionRadius 185 UU (class default 200) [CONF]
// TnVehicleForm.OnActivate: Velocity = ClampLength(pawn Velocity, 3500) is handed to the rigid body
// with the pawn rotation when the vehicle form activates (start of robot->vehicle) [CONF].
constexpr float kMaxTransformSpeed = 35.0f;  // TnVehicleForm.kMaxTransformSpeed 3500 UU/s
constexpr float kVehicleJumpSpeed = 12.0f;   // JumpLinearSpeed 1200 UU/s       [CONF]
// [PROV] player steering rate not recovered. (Pass 7's source, AiMaxAngularSpeed, is an AI-only
// field and is 20 rad/s for the Optimus truck.)
constexpr float kVehicleTurnRate  = 3.1416f;

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
// TnScreenSpaceOffsetByPitchCameraBehavior DefaultOffsetCurve [150,300,150] UU over pitch -75/0/75,
// SmoothTime 0.3 [CONF values]; interpreted as a rightward camera offset [PROV semantics].
constexpr float kShoulderOffsetMid    = 3.0f;
constexpr float kShoulderOffsetEnd    = 1.5f;
constexpr float kShoulderOffsetSmooth = 0.3f;

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
constexpr float kTransformHandoffFrac = 0.5f; // [PROV]
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
constexpr float kLandMinAirTime    = 0.3f;   // s [PROV]
// Turn in place. [CONF] TransGame.Default__TnAnimTurnInPlace: TransitionThresholdAngle 4096 UU,
// TransitionBlendTime 0.1, PercentageToAllowAbort 0.5. Aim offset InterpSpeed [CONF]
// Default__TnAnimNodeAimOffset 12.
constexpr float kTurnThreshold       = 0.3926991f; // 4096 UU = 22.5 deg [CONF]
constexpr float kTurnTransitionBlend = 0.1f;       // s [CONF]
constexpr float kTurnAbortPct        = 0.5f;       // [CONF]
// Transformation mesh offset: TnPawn.Transforming.OnUpdate OffsetMeshes(Remaining/0.5 * Shift)
// [CONF shape]; used here to absorb the height change when the movement form switches at fold start.
constexpr float kTransformShiftBlend = 0.5f;
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
