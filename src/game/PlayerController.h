// Clean-room reconstruction — bridges neutral input to gameplay intent + camera.
// This is the ONLY place that reads platform::InputFrame; the pawn stays input-agnostic.
#pragma once
#include "platform/Input.h"
#include "render/Camera.h"
#include "game/CharacterMovement.h"
#include "core/Config.h"

#include <string>
#include <vector>

namespace game {

class CollisionWorld;

class Character;
class World;

// HM_Engine.HmC2Smoother: critically damped smoothing (omega = 2 / (SmoothTime/2)) used by the camera
// behaviours for FOV, offsets and orbit rotation [CONF bytecode].
struct C2Smoother {
    float vel = 0.0f;
    float smooth(float from, float to, float smoothTime, float dt);
    void reset() { vel = 0.0f; }
};

// HUD aim state, mirroring the TnHUD data observers [AssetTools 7a69756 fineaim_hud.json]:
//  CurrentWeaponAndAim -> NotifyCurrentWeaponChanged(class name) + NotifyFineAimChanged(EHudAimType)
//  WeaponSpreadObserver -> NotifyWeaponSpreadChanged(spread); the Ion Blaster crosshair moves its 3 prongs
//  to spread * 300 px (eased 0.2 s). The Ion Blaster keeps mc_crosshairIonBlaster in fine aim: no scope
//  (HasFineAimScope false; the movie shows scopes only for HeavyPistol/BurstRifle/SniperRifle) [CONF].
//  TargetType -> NotifyTargetTypeChanged (Friend 0 / Enemy 1 / None 2; targeting not implemented -> None).
struct HudAimState {
    const char* weaponClass = "";   // e.g. "TnWeaponIonBlaster"; "" when no robot weapon is drawn
    int aimType = 0;                // EHudAimType: 0 kHudStandardAim, 1 kHudFineAim
    float spread = 0.0f;            // effective weapon spread (PerShotSpreadModifier bloom x FineAimSpreadModifier
                                    // 0.5 in fine aim) [HIGH: the native combination is not recovered]
    int targetType = 2;             // ETargetTypeForHud TTFH_None
    bool crosshairVisible = false;  // ShowCrosshair: a weapon with ammo capacity is drawn
};

// HUD notifications produced this frame by the TnHUD observers (Movie.Invoke calls) [CONF RE d50e2a9].
struct HudNotify {
    enum class Type { WeaponSpread, CurrentWeapon, FineAim };
    Type type;
    float spread;              // _global.NotifyWeaponSpreadChanged(rawSpread)
    int aimType;               // _global.NotifyFineAimChanged(0/1)
    const char* weaponClass;   // _global.NotifyCurrentWeaponChanged(className)
};

// Fine aim state for presentation (reticle/sight UI): active flag, the TnPCS_FineAim blend implied by
// the smoothed FOV (0 = default 80, 1 = fine aim 45), and the current horizontal FOV.
struct FineAimState {
    bool wanted = false;      // TnFineAimManager bWantsToFineAim (input)
    bool active = false;      // bFineAiming (TnPCS_FineAim present)
    float blend = 0.0f;       // 0..1 FOV transition progress
    float fovXDeg = 80.0f;
};

class PlayerController {
public:
    void possess(Character* c) { pawn_ = c; }
    Character* pawn() const { return pawn_; }

    // Consume input for this frame: updates camera orientation and buffers intent.
    void handleInput(const platform::InputFrame& in, float dt);

    // Apply the buffered intent to the possessed pawn for one fixed step.
    void applyToPawn(World& world, float dt);

    // Third-person follow camera (active camera strategy of the possessed form).
    void updateCamera(render::Camera& cam) const;
    // Camera eye position (anchor + orbit + screen-space offset), shared by the renderer camera and
    // the crosshair trace.
    core::Vec3 cameraPos() const;
    // Camera obstruction (RE MILESTONE04_CAMERA_COLLISION, 990f3e7): the last strategy behaviour, run once per
    // fixed step after the pawn moved. Robot TnThirdPersoncollisionCameraBehavior / vehicle
    // TnAvoidClippingCameraBehavior.
    void tickCameraCollision(float dt);
    // Transform clearance [CONF RE OVERNIGHT 2026-10-04 B3]: TnPawn.Transform -> MoveToSafeTransformationLocation ->
    // FindSpotAwayFromPawns(target extent). No spot: HUD NotifyCantTransform (mc_cantTransform) + TransformFailedSound
    // (TnPlayerController default BL_TRANS_POWER.TRANSFORM_DISABLED) and no transform. A displaced spot moves the
    // collision at once and slides the robot meshes back over 0.5 s. After a vehicle->robot transform,
    // InRobotForm.BeginState MoveToSafeLocation; failing that, ForceIntoForm(vehicle).
    int cantTransformCount() const { return cantTransformCount_; }      // pulses for the HUD / Systems
    // [Systems M08i] audio pulses: successful ability triggers (+ the id), presses refused because abilities are jammed,
    // and refused transforms (Disruptor buff / already transforming / no room: TransformFailedSound).
    int abilityTriggerCount() const { return abilityTriggers_; }
    const std::string& lastTriggeredAbility() const { return lastTriggeredAbility_; }
    int abilitiesJammedCount() const { return abilitiesJammedCount_; }
    int transformFailedCount() const { return transformFailedCount_; }
    int forcedVehicleCount() const { return forcedVehicleCount_; }
    bool tryBeginTransform();
    // LocalProfile look settings (Frontend owns the values) [CONF RE TARGETED_PASS3 G2]: CameraSensitivity 0-100 (default
    // 30) scales the orbit speed by Lerp(0.03, 0.20, s/100) - applied relative to the default 30 (the absolute mouse rate
    // stays PROV); invert Y per form: InvertY_Robot, InvertY_Car (car AND truck), InvertY_Plane, InvertY_Tank.
    void setLookSettings(int cameraSensitivity, bool invertRobot, bool invertCar, bool invertPlane, bool invertTank) {
        auto curve = [](float s) { return 0.03f + (0.20f - 0.03f) * s; };
        lookScale_ = curve(std::max(0, std::min(100, cameraSensitivity)) / 100.0f) / curve(0.30f);
        invertY_[0] = invertRobot; invertCar_ = invertCar; invertPlane_ = invertPlane; invertTank_ = invertTank;
    }    static bool robotFitsAt(const CollisionWorld* col, const core::Vec3& feet, const Character* pawn = nullptr);
    static bool findRobotSpot(const CollisionWorld* col, const core::Vec3& feet, core::Vec3& out, const Character* pawn = nullptr);
    // No pawn (PendingMatch: ShouldSpectateOnLogin): the controller views from its own location / rotation, which
    // GameInfo.Login took from FindPlayerStart [HIGH: stock UE3 Login + PlayerWaitingSpectating].
    void setSpectatorView(const core::Vec3& pos, float yaw) { spectating_ = true; specPos_ = pos; specYaw_ = yaw; specPitch_ = 0.0f; specFov_ = 0.0f; }
    // Camera strategy override with pitch / FOV (TnGuidedMissileCameraStrategyType).
    void setSpectatorView(const core::Vec3& pos, float yaw, float pitch, float fov) { spectating_ = true; specPos_ = pos; specYaw_ = yaw; specPitch_ = pitch; specFov_ = fov; }
    // PlayerController state GuidingMissile: inputs cleared (the pawn stops), camera deltas steer, an ability press detonates.
    void setGuiding(bool g) { if (g && !guiding_) { guideYaw0_ = camYaw_; guidePitch0_ = camPitch_; } guiding_ = g; }
    bool guiding() const { return guiding_; }
    float guideLR() const { return guideLR_; }
    float guideUD() const { return guideUD_; }
    bool consumeDetonateRequest() { bool b = detonate_; detonate_ = false; return b; }
    void clearSpectatorView() { spectating_ = false; }
    bool spectating() const { return spectating_; }
    void setViewAspect(float a) { aspect_ = a > 0.0f ? a : aspect_; }
    // Diagnostics: last step's obstruction state.
    bool cameraObstructed() const { return camObstructed_; }
    core::Vec3 desiredCameraPos() const;      // orbit + offset result before the obstruction behaviour

    float camYaw() const { return camYaw_; }
    float camPitch() const { return camPitch_; }
    bool boostHeld() const { return intent_.wantBoost; }   // read-only, for vehicle boost presentation
    bool throttleHeld() const { return intent_.moveForward != 0.0f || intent_.moveRight != 0.0f; }   // read-only, engine audio
    float moveForwardInput() const { return intent_.moveForward; }   // read-only: vehicle EngineLoadState (audio)
    void setCameraYaw(float y) { camYaw_ = y; viewYaw_ = y; }
    void setCameraPitch(float p) { camPitch_ = p; viewPitch_ = p; }
    bool fineAiming() const { return fineAiming_; }
    float fovXDeg() const { return fovCur_; }
    FineAimState fineAimState() const;
    HudAimState hudAimState() const;
    const std::vector<HudNotify>& hudNotifies() const { return hudNotifies_; }
    // Last value sent through NotifyWeaponSpreadChanged (moves only by > 0.002), i.e. what the HUD
    // movie currently holds. Read-only, for the renderer's crosshair.
    float hudSpread() const { return hudSpreadSent_; }
    // Active camera strategy (diagnostics): 0 = OverTheShoulder, 1 = HoverTruck, 2 = Truck (Driving).
    int cameraStrategy() const { return strategy_; }
    float viewYaw() const { return viewYaw_; }

private:
    Character* pawn_ = nullptr;
    float camYaw_ = 0.0f;         // orbit input rotation
    float camPitch_ = -0.15f;
    float viewYaw_ = 0.0f;        // camera rotation after the strategy's HmOrbitSmoother
    float viewPitch_ = -0.15f;
    MoveIntent intent_;
public:
    // [Systems M08d] read-only: the jet Hover Up / Down held inputs drive TnPlaneForm.Hovering.UpdateDashing sounds.
    const MoveIntent& moveIntent() const { return intent_; }
private:
    bool wantJumpLatched_ = false;
    bool wantFire_ = false;
    bool fireLatch_ = false;
public:
    void setQaNoclip(bool on) { qaNoclip_ = on; }   // DEV / QA TOOLING (World::qaSetNoclip)
private:
    bool qaNoclip_ = false, wantJumpHeld_ = false;
    struct FineAimProfile { float fov, distM, offX, offZ, look; };
    FineAimProfile fineAimProfile() const;
    float lookBlend_ = 1.0f, robotDist_ = 8.0f;   // fine-aim look speed blend; robot orbit distance
    float quickTurnRemain_ = 0.0f, quickTurnFrom_ = 0.0f, quickTurnTo_ = 0.0f;   // tank 180 quick turn (TnQuickTurnCameraBehavior)
    float sinceStep_ = 0.0f, stepFaceYaw_ = 0.0f, stepBodyYaw_ = 0.0f, stepYawRate_ = 0.0f;   // presentation yaw (per render frame)   // Fire pressed since the last simulation step
    bool wantReload_ = false;     // latched on release of a tap < kReloadTapTime
    bool wantDashLatched_ = false;
    bool prevReloadDown_ = false;
    float reloadHeld_ = 0.0f;
    // TnFineAimManager: bWantsToFineAim (input) vs bFineAiming (active when CanFineAim allows).
    bool fineAimWanted_ = false;
    bool fineAiming_ = false;
    float fovCur_ = core::config::kCamFovXDeg;   // smoothed horizontal FOV (TnFovCameraBehavior)
    C2Smoother fovS_, offXS_, offYS_, offZS_, yawS_, pitchS_, distS_;
    // Simulation copy of the orbit smoothing, advanced once per fixed step (the camera's runs per render frame for presentation):
    // the vehicle faces / aims by it, so the same inputs give the same motion at any frame rate.
    C2Smoother simYawS_, simPitchS_;
    float simYaw_ = 0.0f, simPitch_ = 0.0f;
    int simMode_ = -1;
    // Mouse deltas accumulated over the render frames of a step; the steering / look-up rate smoothing runs once per step on them.
    float accMouseDX_ = 0.0f, accMouseDY_ = 0.0f;
    // Look accumulators in double: per-frame mouse deltas summed in float round differently by frame partition, which vehicle
    // contact physics amplifies; resynced whenever camYaw_ / camPitch_ are set elsewhere.
    double camYawD_ = 0.0, camPitchD_ = 0.0;
    bool padSteer_ = false, padLook_ = false; float padSteerIn_ = 0.0f, padLookIn_ = 0.0f;
    core::Vec3 offset_{core::config::kShoulderX, core::config::kShoulderY, core::config::kShoulderZMid};
    // Strategy blend (HmCameraStrategy.TransitionTime of the new strategy).
    int strategy_ = 0;
    float stratT_ = 1.0f, stratDur_ = 0.0f;
    float anchorFrom_ = 0.0f, distFrom_ = 0.0f;
    float anchorCur_ = core::config::kCamHeight - core::config::kPawnHalfHeight, distCur_ = core::config::kCamDistance;
    float nitroDist_ = 0.0f;      // TnLocationOffsetCameraBehavior TnPCS_Boosting transition 0..1
    float wiggleT_ = 0.0f;
    float steerSmoothed_ = 0.0f;
    const CollisionWorld* col_ = nullptr;        // non-zero-extent world (camera box sweeps / overlap)
    const CollisionWorld* colRay_ = nullptr;     // zero-extent world (camera ray)
    float aspect_ = 16.0f / 9.0f;
    core::Vec3 camLoc_{0, 0, 0};
    bool camLocValid_ = false, camObstructed_ = false;
    core::Vec3 camOld_{0, 0, 0};                 // smoothed camera offset in target space (UE X fwd, Y right, Z up)
    bool camOldValid_ = false;
    bool spectating_ = false;
    int cantTransformCount_ = 0, forcedVehicleCount_ = 0;
    float lookScale_ = 1.0f;
    float lookUpSmoothed_ = 0.0f, tank180Cooldown_ = 0.0f;
    int wantSwitch_ = 0;
    bool wantKillstreak_ = false, wantMelee_ = false, wantGrenade_ = false, wantPickup_ = false;
public:
    bool consumeKillstreakRequest() { bool b = wantKillstreak_; wantKillstreak_ = false; return b; }
    bool consumeMeleeRequest() { bool b = wantMelee_; wantMelee_ = false; return b; }
    bool fireHeld() const { return wantFire_ || fireLatch_; }
    bool consumeGrenadeRequest() { bool b = wantGrenade_; wantGrenade_ = false; return b; }
    bool consumePickupRequest() { bool b = wantPickup_; wantPickup_ = false; return b; }
private:
    int wantAbility_ = -1, abilityTriggers_ = 0;
    float abilityStickFwd_ = 0.0f, abilityStickRight_ = 0.0f;
    std::string lastRefusedAbility_;
    std::string lastTriggeredAbility_;                 // [Systems M08i]
    int abilitiesJammedCount_ = 0, transformFailedCount_ = 0;
    bool invertY_[2] = {false, false};
    bool invertCar_ = false, invertPlane_ = false, invertTank_ = false;
    bool wasTransforming_ = false;

    core::Vec3 specPos_{0, 0, 0};
    float specYaw_ = 0.0f, specPitch_ = 0.0f, specFov_ = 0.0f;
    bool guiding_ = false, detonate_ = false;
    float guideYaw0_ = 0.0f, guidePitch0_ = 0.0f, guideLR_ = 0.0f, guideUD_ = 0.0f;
    float camSmoothRemain_ = 0.0f;
    int camCollStrategy_ = -1;
    std::vector<HudNotify> hudNotifies_;
    float hudSpreadSent_ = 0.0f;
    std::string hudWeaponSent_;
    int hudAimSent_ = -1;
    bool hudInit_ = false;
    void tickHud();
    bool canFineAim() const;
    void tickFineAim();
    void updateCameraStrategy(const platform::InputFrame& in, float dt);
};

} // namespace game
