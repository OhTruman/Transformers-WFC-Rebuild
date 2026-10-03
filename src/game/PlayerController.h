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
    const CollisionWorld* col_ = nullptr;        // for the third-person camera collision
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
