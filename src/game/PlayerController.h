// Clean-room reconstruction — bridges neutral input to gameplay intent + camera.
// This is the ONLY place that reads platform::InputFrame; the pawn stays input-agnostic.
#pragma once
#include "platform/Input.h"
#include "render/Camera.h"
#include "game/CharacterMovement.h"
#include "core/Config.h"

namespace game {

class CollisionWorld;

class Character;
class World;

class PlayerController {
public:
    void possess(Character* c) { pawn_ = c; }
    Character* pawn() const { return pawn_; }

    // Consume input for this frame: updates camera orientation and buffers intent.
    void handleInput(const platform::InputFrame& in, float dt);

    // Apply the buffered intent to the possessed pawn for one fixed step.
    void applyToPawn(World& world, float dt);

    // Third-person follow camera positioned behind the pawn.
    void updateCamera(render::Camera& cam) const;
    // Camera eye position (anchor + orbit + over-the-shoulder offset), shared by the renderer
    // camera and the crosshair trace.
    core::Vec3 cameraPos() const;

    float camYaw() const { return camYaw_; }
    float camPitch() const { return camPitch_; }
    bool boostHeld() const { return intent_.wantBoost; }   // read-only, for vehicle boost presentation
    bool throttleHeld() const { return intent_.moveForward != 0.0f || intent_.moveRight != 0.0f; }   // read-only, engine audio
    float moveForwardInput() const { return intent_.moveForward; }   // read-only: vehicle EngineLoadState (audio)
    void setCameraYaw(float y) { camYaw_ = y; }
    void setCameraPitch(float p) { camPitch_ = p; }
    bool fineAiming() const { return fineAiming_; }
    float fovXDeg() const { return fovCur_; }

private:
    Character* pawn_ = nullptr;
    float camYaw_ = 0.0f;
    float camPitch_ = -0.15f;
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
    float fovSmooth_ = core::config::kCamFovSmooth;
    float shoulder_ = 0.0f;                      // smoothed lateral camera offset (m)
    const CollisionWorld* col_ = nullptr;        // for the third-person camera collision
    bool canFineAim() const;
    void tickFineAim();
};

} // namespace game
