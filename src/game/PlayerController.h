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
    int forcedVehicleCount() const { return forcedVehicleCount_; }
    bool tryBeginTransform();
    // LocalProfile look settings (Frontend owns the values): camera sensitivity (profile default 30 = scale 1.0 [PROV
    // linear mapping until the profile -> look-rate scale is recovered]) and invert Y per form (0 robot, 1 vehicle).
    void setLookSettings(float sensitivity, bool invertRobot, bool invertVehicle) {
        lookScale_ = sensitivity > 0.0f ? sensitivity / 30.0f : 1.0f; invertY_[0] = invertRobot; invertY_[1] = invertVehicle;
    }
    static bool robotFitsAt(const CollisionWorld* col, const core::Vec3& feet);
    static bool findRobotSpot(const CollisionWorld* col, const core::Vec3& feet, core::Vec3& out);
    // No pawn (PendingMatch: ShouldSpectateOnLogin): the controller views from its own location / rotation, which
    // GameInfo.Login took from FindPlayerStart [HIGH: stock UE3 Login + PlayerWaitingSpectating].
    void setSpectatorView(const core::Vec3& pos, float yaw) { spectating_ = true; specPos_ = pos; specYaw_ = yaw; }
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
    bool wantJumpLatched_ = false;
    bool wantFire_ = false;
    bool wantReload_ = false;     // latched on release of a tap < kReloadTapTime
    bool wantDashLatched_ = false;
    bool prevReloadDown_ = false;
    float reloadHeld_ = 0.0f;
    // TnFineAimManager: bWantsToFineAim (input) vs bFineAiming (active when CanFineAim allows).
    bool fineAimWanted_ = false;
    bool fineAiming_ = false;
    float fovCur_ = core::config::kCamFovXDeg;   // smoothed horizontal FOV (TnFovCameraBehavior)
    C2Smoother fovS_, offXS_, offYS_, offZS_, yawS_, pitchS_, distS_;
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
    bool invertY_[2] = {false, false};
    bool wasTransforming_ = false;

    core::Vec3 specPos_{0, 0, 0};
    float specYaw_ = 0.0f;
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
