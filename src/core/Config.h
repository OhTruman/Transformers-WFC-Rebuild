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

// Robot movement. [CONF] recovered from Default__TnPlayerPawn / Default__TnPawn (TransGame.xxx).
constexpr float kRobotMoveSpeed   = 5.5f;    // GroundSpeed 550 UU/s  [CONF]
constexpr float kRobotAccel       = 20.48f;  // AccelRate 2048 UU/s^2 [CONF]
constexpr float kRobotMaxJumpH    = 6.25f;   // MaxJumpHeight 625 UU  [CONF]
// JumpZ is derived so jump height stays MaxJumpHeight under WFC gravity: v=sqrt(2*g*h).
constexpr float kRobotJumpSpeed   = 19.17f;  // = sqrt(2*29.4*6.25) m/s [CONF-derived]
constexpr float kAirControl       = 0.70f;   // AirControl 0.70       [CONF]
constexpr float kAirSpeed         = 15.0f;   // AirSpeed 1500 UU/s    [CONF]

// Pawn collision cylinder. [CONF] Default__TnPawn _WorkingMovementCapabilities.
constexpr float kPawnRadius       = 1.75f;   // CylinderRadius 175 UU [CONF]
constexpr float kPawnHalfHeight   = 2.0f;    // CylinderHeight 200 UU [CONF] (full height 4 m)
constexpr float kEyeHeight        = 2.8f;    // CollisionHeight 200 + BaseEyeHeight 80 UU [CONF]

// Vehicle movement. [CONF] recovered from Default__TnHoverCarSimulationBlueprint (TransGame.xxx)
// — WFC ground vehicles HOVER. (TnCarSimulationBlueprint = non-hover, MaxSpeed 500; unused here.)
constexpr float kVehicleMoveSpeed = 15.0f;   // MaxLinearSpeed 1500 UU/s        [CONF]
constexpr float kVehicleAccel     = 30.0f;   // MaxLinearAcceleration 3000 UU/s^2 [CONF]
constexpr float kVehicleBoostSpeed= 50.0f;   // DashSpeed 5000 UU/s             [CONF]
constexpr float kVehicleDashTime  = 0.3f;    // DashDuration 0.3 s              [CONF]
constexpr float kVehicleHoverH    = 2.0f;    // SuspensionRadius 200 UU (hover) [CONF]
constexpr float kVehicleJumpSpeed = 12.0f;   // JumpLinearSpeed 1200 UU/s       [CONF]
constexpr float kVehicleTurnRate  = 3.1416f; // AiMaxAngularSpeed ~pi rad/s     [CONF]

// Camera. [CONF] default FOV = 75 deg HORIZONTAL (Xe-TransCamera.ini TnFovCameraBehavior).
// Converted to vertical per aspect in render::Camera.
constexpr float kCamFovXDeg    = 75.0f;    // horizontal FOV [CONF]
constexpr float kCamFovSmooth  = 0.4f;     // DefaultFOV SmoothTime [CONF]
constexpr float kMouseSens     = 0.0022f;  // radians per pixel [PROV]
constexpr float kCamDistance   = 9.0f;     // follow distance (m) [PROV — HM camera data not recovered]
constexpr float kCamHeight     = kEyeHeight; // pivot at the pawn eye (2.8 m) [CONF-derived]
constexpr float kPitchMin      = -1.2f;    // [PROV]
constexpr float kPitchMax      =  1.2f;    // [PROV]
constexpr float kFineAimSpeedMult = 0.5f;  // [CONF] TnFineAimManager._GroundSpeedMultiplier

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
