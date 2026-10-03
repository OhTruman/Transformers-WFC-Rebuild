#include "game/PlayerController.h"
#include "game/Character.h"
#include "game/World.h"
#include "game/Collision.h"

#include <algorithm>
#include "core/Config.h"

#include <cmath>
#include <cstdlib>

namespace game {

namespace {
float smoothstep01(float x) { x = core::clampf(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }
// HmCurveVector.Build through the three TnCameraOffsetCurve points at InVal 0 / 0.5 / 1 [CONF native M03 P6]:
// cubic (InterpMode 3), end tangents 0, inner tangent (next - prev)/2, AutoClamped (a local extremum gets a
// flat tangent [HIGH: UE3 CurveAutoClamped]); evaluated as UE3 CubicInterp(P0, T0*Diff, P1, T1*Diff, a).
float curve3(float a, float b, float c, float f) {
    f = core::clampf(f, 0.0f, 1.0f);
    float tm = ((b < a && b < c) || (b > a && b > c)) ? 0.0f : (c - a) * 0.5f;
    auto hermite = [](float p0, float t0, float p1, float t1, float u) {
        float u2 = u * u, u3 = u2 * u;
        return (2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * t0 + (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * t1;
    };
    const float diff = 0.5f;
    return f < 0.5f ? hermite(a, 0.0f, b, tm * diff, f / 0.5f) : hermite(b, tm * diff, c, 0.0f, (f - 0.5f) / 0.5f);
}
// TnScreenSpaceOffsetByPitchCameraBehavior.GetPitchFractionOfLimits.
float pitchFraction(float pitch, float lo, float hi) { return core::clampf((pitch - lo) / (hi - lo), 0.0f, 1.0f); }
} // namespace

float C2Smoother::smooth(float from, float to, float smoothTime, float dt) {
    float st = smoothTime * 0.5f;
    if (st < 0.001f) { vel = 0.0f; return to; }
    if (dt <= 0.0f) { vel = 0.0f; return from; }
    float omega = 2.0f / st, x = omega * dt;
    float e = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = from - to;
    float temp = (vel + omega * change) * dt;
    vel = (vel - omega * temp) * e;
    return to + (change + temp) * e;
}

void PlayerController::handleInput(const platform::InputFrame& in, float dt) {
    using platform::Button;
    namespace cfg = core::config;

    // Movement input is live from frame 0 of a transformation in both directions [CONF RE]; the
    // control form switches at transform start (see Character::moveForm).
    bool transforming = pawn_ && pawn_->isTransforming();
    bool vehicleForm = pawn_ && pawn_->moveForm() == Form::Vehicle;
    bool driving = vehicleForm && pawn_->vehicleState().driving;

    // Look input. Fine aim halves the look speed (OverTheShoulder TnOrbitRotationCameraBehavior:
    // FineAim 25/12.5 vs default 50/25) [CONF ratio]. While Driving the look X axis is the steering
    // input (PlayerInCarForm.SetLocalInputs: SteeringInput = TnPlayerInput.GetNormalizedTurn()) and
    // the orbit yaw follows the truck (TnDrivingOrbitRotationCameraBehavior) [CONF bytecode].
    float look = fineAiming_ ? cfg::kFineAimLookScale : 1.0f;
    if (!driving) {
        camYaw_ -= in.mouseDX * cfg::kMouseSens * look;
        if (in.padConnected) camYaw_ -= in.padRX * 0.04f * look;
    }
    camPitch_ -= in.mouseDY * cfg::kMouseSens * look;
    if (in.padConnected) camPitch_ += in.padRY * 0.03f * look;
    // PitchRange of the active strategy: OverTheShoulder -75..75, HoverTruck -20..30, Truck -25..25.
    float pMin = cfg::kPitchMin, pMax = cfg::kPitchMax;
    if (vehicleForm) {
        pMin = driving ? cfg::kDriveCamPitchMin : cfg::kHoverCamPitchMin;
        pMax = driving ? cfg::kDriveCamPitchMax : cfg::kHoverCamPitchMax;
    }
    camPitch_ = core::clampf(camPitch_, pMin, pMax);
    // [PROV] PC mouse -> GetNormalizedTurn scale; lightly filtered so per-frame mouse deltas steer smoothly.
    float steerIn = core::clampf(in.mouseDX * cfg::kDriveMouseSteer + (in.padConnected ? in.padRX : 0.0f), -1.0f, 1.0f);
    steerSmoothed_ += (steerIn - steerSmoothed_) * (1.0f - std::exp(-dt / 0.08f));
    intent_.steer = driving ? steerSmoothed_ : 0.0f;

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
    intent_.moveForward = core::clampf(fwd, -1.0f, 1.0f);
    intent_.moveRight   = core::clampf(rgt, -1.0f, 1.0f);

    // Boost (vehicle only, held): RightMouseButton / LeftTrigger = "FineAim | Boost" [CONF bindings];
    // the robot's OnStartBoost is empty.
    bool boostKey = in.isDown(Button::FineAim) || in.padLT > 0.5f;
    intent_.wantBoost   = vehicleForm && boostKey;
    // Edge latches persist until a simulation step consumes them [CONF RE input semantics], so a
    // press on a render frame that runs zero 60 Hz steps is not lost.
    if (in.wasPressed(Button::Jump)) wantJumpLatched_ = true;
    if (in.wasPressed(Button::Dash)) wantDashLatched_ = true;

    // Fine aim wants (robot): PC RightMouseButton = ToggleFineAim; pad LeftTrigger = FineAim |
    // OnRelease StopFineAim (hold) [CONF Xe-TransInput.ini]. Vehicle states ignore FineAim.
    if (!vehicleForm) {
        if (in.wasPressed(Button::FineAim)) fineAimWanted_ = !fineAimWanted_;
        static bool padHeld = false;
        bool padNow = in.padLT > 0.5f;
        if (padNow != padHeld) fineAimWanted_ = padNow;
        padHeld = padNow;
    }

    // Transform activates immediately on press; StartTransform refuses while already transforming.
    // Transforming to the vehicle ends fine aim [CONF RE] (the wish is dropped, not just paused).
    if (!transforming && in.wasPressed(Button::Transform) && pawn_) {
        if (pawn_->moveForm() == Form::Robot) fineAimWanted_ = false;
        pawn_->beginTransform();
    }

    // Fire: held flag persists across frames (consumed per simulation step while held) [CONF RE].
    wantFire_ = in.isDown(Button::Fire);
    // Reload: activates on RELEASE of a tap shorter than 0.3 s [CONF RE]; latched until consumed.
    bool reloadDown = in.isDown(Button::Reload);
    if (reloadDown) reloadHeld_ += dt;
    if (!reloadDown && prevReloadDown_ && reloadHeld_ < cfg::kReloadTapTime) wantReload_ = true;
    if (!reloadDown) reloadHeld_ = 0.0f;
    prevReloadDown_ = reloadDown;

    updateCameraStrategy(in, dt);
    tickHud();
    // Hovering faces the controller rotation, which PlayerInVehicleForm.PlayerMove sets to the CAMERA
    // rotation (after the strategy's orbit smoother) [CONF bytecode]; the robot faces the aim.
    intent_.faceYaw = vehicleForm ? viewYaw_ : camYaw_;
}

// Camera strategies (HmCameraStrategySet Truck_Optimus_CAMSET): OverTheShoulder for the robot (and
// TransformToRobo), HoverTruck_Optimus for Hovering (and TransformToVehicle), Truck_Optimus for Driving.
void PlayerController::updateCameraStrategy(const platform::InputFrame& in, float dt) {
    namespace cfg = core::config;
    if (!pawn_) return;
    bool vehicleForm = pawn_->moveForm() == Form::Vehicle;
    const Character::VehicleState& vs = pawn_->vehicleState();
    bool driving = vehicleForm && vs.driving;
    bool nitro = driving && vs.nitroRemain > 0.0f;
    int want = !vehicleForm ? 0 : (driving ? 2 : 1);

    // Strategy targets [CONF strategy data; see Config.h].
    float anchor = cfg::kCamHeight - cfg::kPawnHalfHeight, dist = cfg::kCamDistance;
    if (want == 1) { anchor = cfg::kHoverCamAnchor; dist = cfg::kHoverCamDist; }
    if (want == 2) {
        // TnLocationOffsetCameraBehavior: TnPCS_Boosting (nitro) orbit 650, in 0.5 s / out 2.0 s.
        float rate = nitro ? 1.0f / cfg::kNitroCamDistIn : -1.0f / cfg::kNitroCamDistOut;
        nitroDist_ = core::clampf(nitroDist_ + rate * dt, 0.0f, 1.0f);
        anchor = cfg::kDriveCamAnchor;
        dist = cfg::kDriveCamDist + (cfg::kNitroCamDist - cfg::kDriveCamDist) * smoothstep01(nitroDist_);
    } else {
        nitroDist_ = 0.0f;
    }
    if (want != strategy_) {
        stratDur_ = want == 0 ? cfg::kCamTransRobot : (want == 1 ? cfg::kCamTransHover : cfg::kCamTransDrive);
        stratT_ = 0.0f;
        anchorFrom_ = anchorCur_; distFrom_ = distCur_;
        strategy_ = want;
    }
    stratT_ += dt;
    float w = stratDur_ > 0.0f ? smoothstep01(stratT_ / stratDur_) : 1.0f;   // [PROV] blend curve
    anchorCur_ = anchorFrom_ + (anchor - anchorFrom_) * w;
    distCur_ = distFrom_ + (dist - distFrom_) * w;

    // Orbit rotation. Robot: no orbit smoothing (OverTheShoulder HmOrbitSmoother SmoothTime unset).
    // Hover: HmOrbitSmoother 0.1 s. Driving: yaw = pawn yaw, pitch chases the body/velocity pitch at
    // MatchRotationSpeed 3 (+ the look input as an offset [PROV for mouse]), smoother 0.25 s.
    if (want == 0) {
        viewYaw_ = camYaw_; viewPitch_ = camPitch_;
        yawS_.reset(); pitchS_.reset();
    } else if (want == 1) {
        float ty = viewYaw_ + std::remainder(camYaw_ - viewYaw_, 6.2831853f);
        viewYaw_ = yawS_.smooth(viewYaw_, ty, cfg::kHoverCamRotSmooth, dt);
        viewPitch_ = pitchS_.smooth(viewPitch_, camPitch_, cfg::kHoverCamRotSmooth, dt);
    } else {
        camYaw_ = pawn_->yaw();
        float ty = viewYaw_ + std::remainder(camYaw_ - viewYaw_, 6.2831853f);
        viewYaw_ = yawS_.smooth(viewYaw_, ty, cfg::kDriveCamRotSmooth, dt);
        const core::Vec3& v = pawn_->velocity();
        float hs = std::sqrt(v.x * v.x + v.z * v.z);
        float velPitch = std::atan2(v.y, std::max(hs, 1e-3f));
        float blend = std::min(1.0f, std::sqrt(hs * hs + v.y * v.y) / cfg::kTruckDriveSpeed);   // PitchVelocityBlend 1
        float target = vs.pitch + (velPitch - vs.pitch) * blend + camPitch_;
        float p = viewPitch_ + (target - viewPitch_) * std::min(1.0f, cfg::kDriveCamMatchRate * dt);
        viewPitch_ = core::clampf(p, vs.pitch + cfg::kDriveCamPitchMin, vs.pitch + cfg::kDriveCamPitchMax);
    }

    // FOV (TnFovCameraBehavior): first matching PCS row, HmC2Smoother with that row's SmoothTime.
    float fovT = cfg::kCamFovXDeg, fovSm = cfg::kCamFovSmooth;
    if (want == 0 && fineAiming_) { fovT = cfg::kFineAimFovXDeg; fovSm = cfg::kFineAimFovSmooth; }
    if (want == 1) fovT = cfg::kHoverCamFov;
    if (want == 2) { fovT = nitro ? cfg::kNitroCamFov : cfg::kDriveCamFov; fovSm = nitro ? cfg::kNitroCamFovSmooth : cfg::kCamFovSmooth; }
    fovCur_ = fovS_.smooth(fovCur_, fovT, fovSm, dt);

    // Screen-space offset by pitch (orbit space: x toward the anchor, y right, z up).
    float deg = viewPitch_ * 57.29578f;
    core::Vec3 off;
    float offSm;
    if (want == 0) {
        float f = pitchFraction(deg, -75.0f, 75.0f);
        float zEnd = fineAiming_ ? cfg::kFineAimShoulderZEnd : cfg::kShoulderZEnd;
        off = core::Vec3{fineAiming_ ? cfg::kFineAimShoulderX : cfg::kShoulderX, cfg::kShoulderY,
                         curve3(zEnd, cfg::kShoulderZMid, zEnd, f)};
        offSm = fineAiming_ ? cfg::kFineAimOffsetSmooth : cfg::kShoulderOffsetSmooth;
    } else {
        float range = cfg::kVehCamOffsetPitch * 57.29578f;
        float f = pitchFraction(deg - (want == 2 ? vs.pitch * 57.29578f : 0.0f), -range, range);
        off = core::Vec3{0.0f, 0.0f, curve3(cfg::kVehCamOffsetZLow, 0.0f, cfg::kVehCamOffsetZHigh, f)};
        offSm = want == 1 ? cfg::kHoverCamOffsetSmooth : cfg::kDriveCamOffsetSmooth;
    }
    offset_.x = offXS_.smooth(offset_.x, off.x, offSm, dt);
    offset_.y = offYS_.smooth(offset_.y, off.y, offSm, dt);
    offset_.z = offZS_.smooth(offset_.z, off.z, offSm, dt);
    wiggleT_ += dt;
    (void)in;
}

FineAimState PlayerController::fineAimState() const {
    namespace cfg = core::config;
    FineAimState s;
    s.wanted = fineAimWanted_;
    s.active = fineAiming_;
    s.fovXDeg = fovCur_;
    s.blend = strategy_ == 0 ? core::clampf((cfg::kCamFovXDeg - fovCur_) / (cfg::kCamFovXDeg - cfg::kFineAimFovXDeg), 0.0f, 1.0f) : 0.0f;
    return s;
}

// TnHUD tick [CONF RE d50e2a9]: TnHudDataObserverWeaponSpread sends the raw spread (ConversionFactor 1)
// whenever it moves by more than 0.002; TnHudDataObserverCurrentWeaponAndAim sends
// NotifyCurrentWeaponChanged(class) and NotifyFineAimChanged(0/1) together whenever either changes.
void PlayerController::tickHud() {
    hudNotifies_.clear();
    HudAimState s = hudAimState();
    if (!hudInit_ || std::fabs(s.spread - hudSpreadSent_) > 0.002f) {
        hudSpreadSent_ = s.spread;
        hudNotifies_.push_back({HudNotify::Type::WeaponSpread, s.spread, 0, ""});
    }
    if (!hudInit_ || std::string(s.weaponClass) != hudWeaponSent_ || s.aimType != hudAimSent_) {
        hudWeaponSent_ = s.weaponClass; hudAimSent_ = s.aimType;
        hudNotifies_.push_back({HudNotify::Type::CurrentWeapon, 0.0f, 0, s.weaponClass});
        hudNotifies_.push_back({HudNotify::Type::FineAim, 0.0f, s.aimType, ""});
    }
    hudInit_ = true;
}

HudAimState PlayerController::hudAimState() const {
    HudAimState s;
    if (!pawn_) return s;
    bool drawn = pawn_->hasWeapon();                 // gun attached on a displayed robot mesh
    s.weaponClass = drawn ? "TnWeaponIonBlaster" : "";
    s.aimType = fineAiming_ ? 1 : 0;
    s.spread = pawn_->effectiveSpread();             // raw spread: bloom x airborne x fine aim [CONF RE d50e2a9]
    s.crosshairVisible = drawn;
    return s;
}

core::Vec3 PlayerController::cameraPos() const {
    // HmOrbitUpdateLocationRotation: Location = anchor + (OrbitLocationOffset rotated by the orbit
    // rotation). Anchor = actor location (shared by both forms during a transformation) + the
    // strategy's HmOffsetAnchorPointRelativeToActor Z offset.
    core::Vec3 focus = pawn_->actorLocation() + core::Vec3{0, anchorCur_, 0};
    core::Vec3 dir = core::forwardFromYawPitch(viewYaw_, viewPitch_);
    core::Vec3 right = core::normalize(core::cross(core::forwardFromYawPitch(viewYaw_, 0.0f), core::Vec3{0, 1, 0}));
    core::Vec3 up = core::cross(right, dir);
    core::Vec3 want = focus + dir * (offset_.x - distCur_) + right * offset_.y + up * offset_.z;
    // Third-person camera collision (OverTheShoulder TnThirdPersoncollisionCameraBehavior; vehicle
    // strategies TnAvoidClippingCameraBehavior): keep the camera on the anchor's side of world
    // geometry. [PROV] behaviour details not recovered; pulled 0.3 m in front of the hit.
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
// dodging (no melee/dodge in the rebuild yet); vehicle states return false. The control form is the
// robot from the start of a vehicle->robot fold [CONF RE].
bool PlayerController::canFineAim() const {
    if (!pawn_) return false;
    if (pawn_->moveForm() != Form::Robot) return false;
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
    step.wantDash = wantDashLatched_;
    wantDashLatched_ = false;   // consumed by this step
    CharacterMovement::update(*pawn_, step, dt, world.collision());
    pawn_->setAimPitch(camPitch_);   // drives the upper-body aim offset
    pawn_->tickSpreadModifier(dt);   // TnWeaponSpreadModifier airborne ramp
    wantJumpLatched_ = false;
    pawn_->weapon().tick(dt);
    pawn_->ability().tick(dt);

    Weapon& w = pawn_->weapon();
    // Weapon gate: robot control form, and during vehicle->robot only once restored (25% of the
    // fold) + EquipTime 0.2 s [CONF]. Robot->vehicle stores the weapon at fold start.
    bool usable = pawn_->weaponUsable();
    if (wantReload_) {                       // latched tap; consumed by this step
        if (usable) w.beginReload();
        wantReload_ = false;
    }
    // Ion Blaster: hitscan while the trigger is held (held flag persists across render frames).
    if (wantFire_ && usable) {
        if (w.canFire()) {
            w.onFired();
            static const bool noRecoil = std::getenv("WFC_NORECOIL") != nullptr;   // A/B diagnostic
            if (!noRecoil) pawn_->notifyFired();   // per-shot skeletal recoil (TnRecoiler)
            // Aim through the crosshair: trace the camera ray to find the aimed point, then fire
            // from the pawn eye toward it (the camera is offset over the shoulder).
            core::Vec3 eye = pawn_->actorLocation() + core::Vec3{0, core::config::kEyeHeight - core::config::kPawnHalfHeight, 0};
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
}

void PlayerController::updateCamera(render::Camera& cam) const {
    if (!pawn_) return;
    namespace cfg = core::config;
    cam.pos = cameraPos();
    cam.yaw = viewYaw_;
    cam.pitch = viewPitch_;
    // TnWiggler3CameraBehavior (vehicle strategies): small rotation wiggle at 12 Hz, full above
    // FullWiggleSpeedThreshold 100 UU/s [CONF amplitude/frequency; waveform PROV].
    if (strategy_ != 0) {
        const core::Vec3& v = pawn_->velocity();
        float k = std::min(1.0f, std::sqrt(v.x * v.x + v.z * v.z) / 1.0f);
        float amp = (strategy_ == 1 ? cfg::kHoverCamWiggleDeg : cfg::kDriveCamWiggleDeg) * 0.0174533f * k;
        cam.yaw += amp * std::sin(6.2831853f * 12.0f * wiggleT_);
        cam.pitch += amp * std::sin(6.2831853f * 12.0f * 1.37f * wiggleT_ + 1.0f);
    }
    cam.fovXDeg = fovCur_;
}

} // namespace game
