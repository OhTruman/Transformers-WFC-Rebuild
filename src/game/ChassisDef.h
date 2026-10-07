// Clean-room reconstruction — per-chassis character definition (TnTransformer ROBODEF + VEHDEF as applied by
// TnPawn.ApplyTransformer, plus the specialty applied by TnCharacterApplier.ApplyCharacter -> ApplySpecialty).
//
// Source: AssetTools' per-chassis export ExtractedAssets/VerticalSlice/Characters/<ChassisId>/character.json
// (vs_roster_export.py, keyed by the RE chassis UniqueId) and the authored blueprint values it references.
// Defaults below are the Optimus ("Truck") values the rebuild used before Pass 22, so a missing field keeps behaviour.
#pragma once
#include <cmath>
#include <string>
#include <vector>
#include "core/Config.h"
#include "core/Math.h"

namespace game {

// Robot form (ROBODEF scalars, acrobatics, momentum), metres / seconds.
struct RobotParams {
    float radius = core::config::kPawnRadius, halfHeight = core::config::kPawnHalfHeight;   // CollisionRadius / Height
    float eyeHeight = core::config::kEyeHeight - core::config::kPawnHalfHeight;            // BaseEyeHeight above the centre
    float groundSpeed = core::config::kRobotMoveSpeed, accel = core::config::kRobotAccel;
    float airSpeed = core::config::kAirSpeed, airControl = core::config::kAirControl;
    float terminalVel = core::config::kRobotTerminalVel;
    float jumpHeight = core::config::kRobotMaxJumpH;                                        // Acrobatics JumpHeight
    float dodgeSpeed = 30.0f, dodgeTime = 0.5f;                                             // Acrobatics DodgeSpeed / DodgeTime
    float hoverJumpHeight = 5.0f, hoverDuration = 7.0f, hoverAirSpeed = 5.0f;              // Acrobatics HoverJumpHeight / Duration / AirSpeed
    float momGroundFwd = core::config::kMomentumGroundFwd, momGroundNeutral = core::config::kMomentumGroundNeutral,
          momGroundBack = core::config::kMomentumGroundBack;
    float momAirFwd = core::config::kMomentumAirFwd, momAirNeutral = core::config::kMomentumAirNeutral,
          momAirBack = core::config::kMomentumAirBack;
    float damageMultiplier = 1.0f;                                                          // ROBODEF DamageMultiplier
    float selfDamageMultiplier = 0.45f;                                                     // ROBODEF SelfDamageMultiplier
    float jumpSpeed() const { return std::sqrt(2.0f * core::config::kGravity * jumpHeight); }   // JumpZ (ApplyTransformer)
    // TnAcrobaticsManager double jump [CONF RE addendum 10]: SharedAcrobatics DoubleJumpHeight 450 UU, DoubleJumpMinHeight 10 UU.
    float doubleJumpHeight = 4.5f, doubleJumpMinHeight = 0.10f;
    float doubleJumpSpeed() const { return std::sqrt(2.0f * core::config::kGravity * doubleJumpHeight); }
};

// Vehicle form. formType: TnCarFormBlueprint (car), TnTruckFormBlueprint (truck), TnTankFormBlueprint (tank),
// TnPlaneFormBlueprint (jet).
enum class VehicleFormType { Car, Truck, Tank, Jet };

struct WheelDef { float x, y, z, radius, maxSteerDeg, friction, driftScale; };   // UU, body local (TnWheelPhysicsBlueprint)

struct VehicleParams {
    VehicleFormType form = VehicleFormType::Truck;
    // Hover simulation (TnHoverCarSimulationBlueprint / TnHoverTankSimulationBlueprint), metres.
    float hoverSpeed = core::config::kVehicleMoveSpeed, hoverAccel = core::config::kVehicleAccel;
    float dashSpeed = core::config::kVehicleBoostSpeed, dashTime = core::config::kVehicleDashTime;
    float driftDuration = core::config::kHoverDriftDuration;
    float jumpSpeed = core::config::kVehicleJumpSpeed, jumpAngSpeed = core::config::kHoverJumpAngSpeed;
    float suspMountRadius = core::config::kSuspMountRadius;
    float suspRest = core::config::kSuspRestLength, suspStiffness = core::config::kSuspStiffness,
          suspDamping = core::config::kSuspDamping;
    float maxBoostSpeed = 0.0f;   // tank (MaxBoostSpeed)
    float recoilVelocity = 0.0f;  // tank cannon recoil
    // Car physics (TnCarPhysicsBlueprint, Driving).
    float mass = core::config::kVehMass, inertiaX = core::config::kVehInertiaX, inertiaY = core::config::kVehInertiaY,
          inertiaZ = 5890.0f;                                      // kg m^2
    float comFwd = core::config::kVehComFwd, comUp = core::config::kVehComUp;   // m (CenterOfMass - ChassisOffset)
    float driveSpeed = core::config::kTruckDriveSpeed, driveAccel = core::config::kTruckDriveAccel;
    float driveJumpFwd = core::config::kDriveJumpFwd, driveJumpUp = core::config::kDriveJumpUp;
    float driveJumpAngVel = core::config::kDriveJumpAngVel;
    float airTurnAccel = core::config::kDriveAirTurnAccel, airStrafeAccel = core::config::kDriveAirStrafeAccel;
    float angularDamping = core::config::kDriveAngularDamping;
    float rollDuration = 0.0f;                                     // car: barrel roll (RollDuration 0.7); truck 0
    std::vector<WheelDef> wheels;                                  // empty = the truck wheel set (CharacterMovement)
    float damageMultiplier = 1.0f;                                 // VEHDEF DamageMultiplier
    // VEHDEF AI boost rule (BoostStart / StopDistance UU -> m, BoostStart / StopAngle = cosines) [CONF authored].
    float aiBoostStartM = 50.0f, aiBoostStopM = 30.0f, aiBoostStartCos = 0.98f, aiBoostStopCos = 0.707f;
    float selfDamageMultiplier = 0.45f;                            // VEHDEF SelfDamageMultiplier
    // Rigid-body hull around the mesh root (m). Truck: VH_Optimus_PHYSSYS convex box [CONF AssetTools PHYSICS_STREETS];
    // every MP chassis: its VH_*_PHYSSYS convex hull (VehicleHullTable.inc) [CONF authored]; mesh bounds only as a fallback.
    float hullFront = 3.38f, hullBack = 3.10f, hullHalfWidth = 1.54f, hullBottom = -0.35f, hullTop = 1.85f;
    bool hullFromMesh = false, hullFromPhysics = false;   // physics-asset convex hull (CONF) / mesh bounds fallback (PROV)
    // Jet (TnHoverPlaneSimulation HoverPlane_Physics + TnPlaneSimulation Plane_Physics) [CONF RE TARGETED_PASS3 C3].
    float hoverRollTime = 0.6f, hoverRollSpeed = 30.0f;           // RollDuration / RollLinearSpeed
    float flySpeed = 40.0f, flyAccel = 30.0f, flyDrag = 600.0f;     // MaxSpeed / MaxAcceleration / DragCoefficient
    float pitchDuePitch = 27.0f, yawDueYaw = 16.0f, rollDueYaw = 77.0f, extraRotLerp = 0.1f;
    float maxPitchDeg = 60.0f, fullPitchDeg = 45.0f, flyRollTime = 0.8f, flyRollSpeed = 30.0f, flyRollAngSpeed = 8.0f;
    bool hasDriving() const { return form == VehicleFormType::Car || form == VehicleFormType::Truck; }
};

struct SocketDef { std::string bone; core::Mat4 local; bool valid = false; };

// Vehicle camera strategy values of the chassis' HmCameraStrategySet (CameraTable.inc) [CONF authored]: anchor Offset Z
// (m above the actor), orbit distance (m), pitch range (rad), horizontal FOV (deg). Defaults = the Optimus truck sets.
struct CamStrategy { float anchor, dist, pitchMin, pitchMax, fov; };

struct ChassisDef {
    std::string id = "Truck", iconic = "Optimus Prime", customBody;
    int faction = 0;                          // FactionRestriction 0 Autobot, 1 Decepticon
    std::string defaultSpecialty = "Leader";   // TnDataProvider_Chassis DefaultSpecialty (UI grouping only)
    std::string iconicSpecialty = "Leader";    // the iconic preset CharacterData.Specialty: applied for iconic selections
    std::string robotGlb, vehicleGlb;         // relative to the asset root's parent (ExtractedAssets)
    // The robot body without baked clips (character.json robot.source_gltf, the skeletal mesh export) and its clip list
    // (robot.animations: clip name, source AnimSet export, category, additive), so a body can be assembled from AnimSets
    // parsed once and shared by every chassis (World::chassisAssets).
    struct AnimRef { std::string name, source, category; bool additive = false; };
    std::string robotSkelGltf;
    std::vector<AnimRef> robotAnims;
    std::string armGltf, armAnimGltf;         // ArmBlueprint (umodel content paths)
    SocketDef weaponPrimary, weaponSecondary; // robot WeaponSocket_Primary / _Secondary
    SocketDef vehicleWeapon;                  // vehicle WeaponSocket_Primary
    SocketDef vehicleWeapon2;                 // vehicle WeaponSocket_Primary2 (the right gun; absent on the tanks)
    SocketDef meleeSmall, meleeLarge, positionSocket;   // robot MeleeSocket_SmallRobot / _LargeRobot / PositionSocket
    SocketDef rightHand;                                // robot MeleeSocket_RightHand (TnGrenadeThrower.TossSocket)
    RobotParams robot;
    VehicleParams vehicle;
    CamStrategy camHover{core::config::kHoverCamAnchor, core::config::kHoverCamDist, core::config::kHoverCamPitchMin,
                         core::config::kHoverCamPitchMax, core::config::kHoverCamFov};
    CamStrategy camDrive{core::config::kDriveCamAnchor, core::config::kDriveCamDist, core::config::kDriveCamPitchMin,
                         core::config::kDriveCamPitchMax, core::config::kDriveCamFov};
    CamStrategy camFly{0, 0, 0, 0, 0};   // jets: FlyingPlane strategy (zero = none)
    std::vector<std::string> iconicWeapons, iconicVehicleWeapons, allowedOnFoot, iconicAbilities;
    // Transform mesh visibility from this chassis' TnAnimNotify_ToggleHidden notifies (character.json robot / vehicle animations)
    // [CONF authored]: robot hidden at toVehRobotHide on Transform_ToVehicle_ROBO, vehicle unhidden at toVehVehicleShow on
    // Transform_ToVehicle_VEH, robot unhidden at toRobotRobotShow on Transform_ToRobot_ROBO, vehicle hidden at toRobotVehicleHide on
    // Transform_ToRobot_VEH. Defaults = the Optimus clips (the values the rebuild used for every chassis before Pass 24).
    float toVehRobotHide = core::config::kToVehRobotHide, toVehVehicleShow = core::config::kToVehVehicleShow;
    float toRobotRobotShow = core::config::kToRobotRobotShow, toRobotVehicleHide = core::config::kToRobotVehicleHide;
    std::string classDefaultSecondary;
    bool mpCharacter = true;                  // referenced by TnAssetReferencesMultiplayer
    bool lockedChassis = false, lockedCharacter = false;
    // Flyer that never transforms (Laserbeak; AssetTools flyer_state_map, PC ADAPTATION approved by Integration / the user): always in
    // its jet-hover form, robot.glb carries the hover / boost clips, transform requests are ignored.
    bool flyerNoTransform = false;
    std::string loadError;                    // non-empty: this chassis cannot be spawned
};

// TnSpecialty CDOs (TransGame.TnSpecialty<Class>): SpeedMultiplier and HealthBlueprint, applied in every TnGame with
// ApplySpecialtyBuffs (Default__TnGame true; only campaign / survival / lobby games override it to false) [CONF].
struct SpecialtyDef {
    const char* id;
    float speedMultiplier;
    std::vector<float> segments;              // TR_Health_p.Health_<Class>
    float overshield;
    const char* defaultSecondary;             // DefaultSecondaryWeapon
};
const SpecialtyDef* specialtyDef(const std::string& id);

// UE relative location (UU) + rotator (65536 units) -> glTF-space (x, z, y) local transform, metres.
core::Mat4 ueSocketToGltf(const float locUE[3], const int rotUE[3]);

// Loads Characters/<id>/character.json under `verticalSliceRoot`. Returns false (and sets def.loadError) when the
// export is missing or incomplete; nothing falls back to another chassis.
bool loadChassisDef(const std::string& verticalSliceRoot, const std::string& id, ChassisDef& def);

// TR_MPPlayerCharacterData_p.<Class>_PCD_MP WeaponTypes for a specialty ("Leader" / "Scientist" / "Scout" / "Soldier"), from
// the roster package's default_four_classes (MP presets) [CONF authored]. Empty when the package is unavailable.
std::vector<std::string> classPresetWeapons(const std::string& specialty);
// Any list of a class preset (TR_MPPlayerCharacterData_p.<Class>_PCD_MP via roster_package): "weapons", "vehicle_weapons", "melee", "abilities".
std::vector<std::string> classPresetList(const std::string& specialty, const char* field);

} // namespace game
