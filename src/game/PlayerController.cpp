#include "game/PlayerController.h"
#include "game/Character.h"
#include "game/World.h"
#include "game/Collision.h"

#include <algorithm>
#include "core/Config.h"

#include <cmath>
#include <cstdlib>

namespace game {

void PlayerController::handleInput(const platform::InputFrame& in, float dt) {
    using platform::Button;

    // Mouse look (plus right-stick if a pad is present). Fine aim halves the look speed
    // (OverTheShoulder TnOrbitRotationCameraBehavior: FineAim 25/12.5 vs default 50/25) [CONF ratio].
    float look = fineAiming_ ? core::config::kFineAimLookScale : 1.0f;
    camYaw_   -= in.mouseDX * core::config::kMouseSens * look;
    camPitch_ -= in.mouseDY * core::config::kMouseSens * look;
    if (in.padConnected) {
        camYaw_   -= in.padRX * 0.04f * look;
        camPitch_ += in.padRY * 0.03f * look;
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

    // WFC keeps processing movement input during a transformation: no movement path in
    // TnPlayerController checks IsTransforming (only IsAbleToFire / PreWeaponSwitch /
    // StartTransform do) [CONF bytecode]. Firing, reloading and jumping stay blocked mid-fold.
    bool transforming = pawn_ && pawn_->isTransforming();
    bool vehicleForm = pawn_ && pawn_->moveForm() == Form::Vehicle;

    intent_.moveForward = core::clampf(fwd, -1.0f, 1.0f);
    intent_.moveRight   = core::clampf(rgt, -1.0f, 1.0f);
    intent_.faceYaw     = camYaw_;
    // Boost (vehicle only): RightMouseButton / LeftTrigger = "FineAim | Boost" [CONF bindings];
    // the robot's OnStartBoost is empty. Shift kept as a legacy alias.
    bool boostKey = in.isDown(Button::FineAim) || in.padLT > 0.5f || in.isDown(Button::Sprint);
    intent_.wantBoost   = vehicleForm && boostKey;
    if (!transforming && in.wasPressed(Button::Jump)) wantJumpLatched_ = true;

    // Fine aim wants (robot): PC RightMouseButton = ToggleFineAim; pad LeftTrigger = FineAim |
    // OnRelease StopFineAim (hold) [CONF Xe-TransInput.ini]. Vehicle states ignore FineAim.
    if (!vehicleForm) {
        if (in.wasPressed(Button::FineAim)) fineAimWanted_ = !fineAimWanted_;
        static bool padHeld = false;
        bool padNow = in.padLT > 0.5f;
        if (padNow != padHeld) fineAimWanted_ = padNow;
        padHeld = padNow;
    }

    // Transform (edge-triggered); StartTransform refuses while already transforming.
    if (!transforming && in.wasPressed(Button::Transform) && pawn_) pawn_->beginTransform();

    // Fire (auto while held) + reload, resolved against the world in applyToPawn.
    wantFire_   = !transforming && in.isDown(Button::Fire);
    wantReload_ = !transforming && in.wasPressed(Button::Reload);

    // FOV (TnFovCameraBehavior): smooth toward the active state's FOV with that state's SmoothTime
    // (FineAim 45/0.1, default 80/0.4) [CONF values; smoothing curve PROV: ~98% at SmoothTime].
    float targetFov = fineAiming_ ? core::config::kFineAimFovXDeg : core::config::kCamFovXDeg;
    fovSmooth_ = fineAiming_ ? core::config::kFineAimFovSmooth : core::config::kCamFovSmooth;
    float k = 1.0f - std::exp(-4.0f * dt / fovSmooth_);
    fovCur_ += (targetFov - fovCur_) * k;

    // Over-the-shoulder offset: OverTheShoulder TnScreenSpaceOffsetByPitchCameraBehavior
    // DefaultOffsetCurve Offsets [150,300,150] UU across PitchRange -75..75, SmoothTime 0.3; the
    // Ion Blaster's fine aim has no override (only Sniper/BurstRifle/HeavyPistol+FineAim) [CONF
    // values]. [PROV] applied as a rightward camera offset in metres; vehicle strategies not
    // recovered -> no offset in vehicle form.
    float target = 0.0f;
    if (!vehicleForm) {
        float t = core::clampf(camPitch_ / core::config::kPitchMax, -1.0f, 1.0f);
        target = core::config::kShoulderOffsetMid + (core::config::kShoulderOffsetEnd - core::config::kShoulderOffsetMid) * std::fabs(t);
    }
    float ks = 1.0f - std::exp(-4.0f * dt / core::config::kShoulderOffsetSmooth);
    shoulder_ += (target - shoulder_) * ks;
}

core::Vec3 PlayerController::cameraPos() const {
    // Anchor: actor (cylinder centre) + Offset Z 200 UU, following the pawn's visual mesh offset
    // (so the camera does not jump with the transform position shift); orbit 800 UU.
    core::Vec3 focus = pawn_->position() + pawn_->meshOffset() + core::Vec3{0, core::config::kCamHeight, 0};
    core::Vec3 dir = core::forwardFromYawPitch(camYaw_, camPitch_);
    core::Vec3 right = core::normalize(core::cross(core::forwardFromYawPitch(camYaw_, 0.0f), core::Vec3{0, 1, 0}));
    core::Vec3 want = focus - dir * core::config::kCamDistance + right * shoulder_;
    // Third-person camera collision (OverTheShoulder TnThirdPersoncollisionCameraBehavior): keep
    // the camera on the anchor's side of world geometry. [PROV] behaviour details not recovered;
    // pulled 0.3 m in front of the hit (TnCamera PawnCylinderRadiusPadding 30 UU).
    if (col_) {
        float t;
        core::Vec3 d = want - focus;
        float len = core::length(d);
        if (len > 1e-3f && col_->segmentHit(focus, want, t)) {
            float keep = std::max(0.0f, t * len - 0.3f);
            want = focus + d * (keep / len);
        }
    }
    return want;
}

// TnPlayerController.PlayerWalking.CanFineAim [CONF bytecode]: not while meleeing, reloading or
// dodging; vehicle states return false. Weapon holstered during a fold -> not while transforming.
bool PlayerController::canFineAim() const {
    if (!pawn_) return false;
    if (pawn_->moveForm() != Form::Robot || pawn_->isTransforming()) return false;
    if (pawn_->weapon().reloading()) return false;
    return true;
}

// TnFineAimManager.Tick / StartFineAim / StopFineAim [CONF bytecode].
void PlayerController::tickFineAim() {
    bool can = canFineAim();
    if (fineAimWanted_ && !fineAiming_ && can) {
        fineAiming_ = true;
        pawn_->setSpeedMultiplier(core::config::kFineAimSpeedMult);   // SetSpeedMultiplier(0.5)
    }
    if ((!fineAimWanted_ || !can) && fineAiming_) {
        fineAiming_ = false;
        pawn_->setSpeedMultiplier(1.0f);                              // RemoveSpeedMultiplier
    }
    pawn_->setFineAiming(fineAiming_);
}

void PlayerController::applyToPawn(World& world, float dt) {
    if (!pawn_) return;
    col_ = world.collision();
    tickFineAim();
    MoveIntent step = intent_;
    step.wantJump = wantJumpLatched_;
    CharacterMovement::update(*pawn_, step, dt, world.collision());
    pawn_->setAimPitch(camPitch_);   // drives the upper-body aim offset
    wantJumpLatched_ = false;
    pawn_->weapon().tick(dt);
    pawn_->ability().tick(dt);

    Weapon& w = pawn_->weapon();
    if (wantReload_) w.beginReload();
    // Ion Blaster: hitscan while the trigger is held; auto-reload on an empty mag.
    if (wantFire_ && pawn_->form() == Form::Robot) {
        if (w.canFire()) {
            w.onFired();
            static const bool noRecoil = std::getenv("WFC_NORECOIL") != nullptr;   // A/B diagnostic
            if (!noRecoil) pawn_->notifyFired();   // per-shot skeletal recoil (TnRecoiler)
            // Aim through the crosshair: trace the camera ray to find the aimed point, then fire
            // from the pawn eye toward it (the camera is offset over the shoulder).
            core::Vec3 eye = pawn_->position() + core::Vec3{0, core::config::kEyeHeight, 0};
            core::Vec3 camDir = core::forwardFromYawPitch(camYaw_, camPitch_);
            core::Vec3 camPos = cameraPos();
            float range = pawn_->weapon().rangeM;
            core::Vec3 aimPoint = camPos + camDir * range;
            float th;
            if (world.collision() && world.collision()->segmentHit(camPos, aimPoint, th))
                aimPoint = camPos + camDir * (range * th);
            core::Vec3 dir = core::normalize(aimPoint - eye);
            world.fireHitscan(eye, dir);
        } else if (w.ammo == 0 && w.canReload()) {
            w.beginReload();
        }
    }
    wantFire_ = false; wantReload_ = false;
}

void PlayerController::updateCamera(render::Camera& cam) const {
    if (!pawn_) return;
    // Anchor: actor (cylinder centre) + Offset Z 200 UU; orbit 800 UU (FineAim keeps the orbit
    // distance for the Ion Blaster: only HeavyPistol/BurstRifle/SniperRifle+FineAim change it).
    cam.pos = cameraPos();
    cam.yaw = camYaw_;
    cam.pitch = camPitch_;
    cam.fovXDeg = fovCur_;
}

} // namespace game
