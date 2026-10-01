#include "game/PlayerController.h"
#include "game/Character.h"
#include "game/World.h"
#include "core/Config.h"

namespace game {

void PlayerController::handleInput(const platform::InputFrame& in, float dt) {
    (void)dt;
    using platform::Button;

    // Mouse look (plus right-stick if a pad is present).
    camYaw_   -= in.mouseDX * core::config::kMouseSens;
    camPitch_ -= in.mouseDY * core::config::kMouseSens;
    if (in.padConnected) {
        camYaw_   -= in.padRX * 0.04f;
        camPitch_ += in.padRY * 0.03f;
    }
    camPitch_ = core::clampf(camPitch_, core::config::kPitchMin, core::config::kPitchMax);

    // Movement axes from keys or left stick.
    float fwd = 0.0f, rgt = 0.0f;
    if (in.isDown(Button::Forward)) fwd += 1.0f;
    if (in.isDown(Button::Back))    fwd -= 1.0f;
    if (in.isDown(Button::Right))   rgt += 1.0f;
    if (in.isDown(Button::Left))    rgt -= 1.0f;
    if (in.padConnected) {
        fwd += in.padLY;
        rgt += in.padLX;
    }

    // Input is locked out while a transformation is playing.
    bool locked = pawn_ && pawn_->isTransforming();

    intent_.moveForward = locked ? 0.0f : core::clampf(fwd, -1.0f, 1.0f);
    intent_.moveRight   = locked ? 0.0f : core::clampf(rgt, -1.0f, 1.0f);
    intent_.faceYaw     = camYaw_;
    intent_.wantBoost   = !locked && in.isDown(Button::Sprint);   // vehicle dash/boost
    if (!locked && in.wasPressed(Button::Jump)) wantJumpLatched_ = true;

    // Transform toggle (edge-triggered) — starts the paired transform animation.
    if (!locked && in.wasPressed(Button::Transform) && pawn_) pawn_->beginTransform();

    // Fire (auto while held) + reload, resolved against the world in applyToPawn.
    wantFire_   = !locked && in.isDown(Button::Fire);
    wantReload_ = !locked && in.wasPressed(Button::Reload);
}

void PlayerController::applyToPawn(World& world, float dt) {
    if (!pawn_) return;
    MoveIntent step = intent_;
    step.wantJump = wantJumpLatched_;
    CharacterMovement::update(*pawn_, step, dt, world.collision());
    wantJumpLatched_ = false;
    pawn_->weapon().tick(dt);
    pawn_->ability().tick(dt);

    Weapon& w = pawn_->weapon();
    if (wantReload_) w.beginReload();
    // Ion Blaster: hitscan while the trigger is held; auto-reload on an empty mag.
    if (wantFire_ && pawn_->form() == Form::Robot) {
        if (w.canFire()) {
            w.onFired();
            core::Vec3 eye = pawn_->position() + core::Vec3{0, core::config::kCamHeight, 0};
            core::Vec3 dir = core::forwardFromYawPitch(camYaw_, camPitch_);
            world.fireHitscan(eye, dir);
        } else if (w.ammo == 0 && w.canReload()) {
            w.beginReload();
        }
    }
    wantFire_ = false; wantReload_ = false;
}

void PlayerController::updateCamera(render::Camera& cam) const {
    if (!pawn_) return;
    core::Vec3 focus = pawn_->position() + core::Vec3{0, core::config::kCamHeight, 0};
    core::Vec3 dir = core::forwardFromYawPitch(camYaw_, camPitch_);
    cam.pos = focus - dir * core::config::kCamDistance;
    cam.yaw = camYaw_;
    cam.pitch = camPitch_;
    cam.fovXDeg = core::config::kCamFovXDeg;   // [CONF] WFC default 75 deg horizontal
}

} // namespace game
