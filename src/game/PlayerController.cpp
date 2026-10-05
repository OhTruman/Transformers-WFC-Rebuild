#include "game/PlayerController.h"
#include "core/Log.h"
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
    const VehicleFormType vform = pawn_ ? pawn_->vehicleParams().form : VehicleFormType::Truck;
    const bool jet = vehicleForm && vform == VehicleFormType::Jet;
    // Jets keep the camera mouse-driven in both modes (the plane turns toward the view rotation); "driving" here means
    // the wheeled boost camera of cars / trucks.
    bool driving = vehicleForm && !jet && pawn_->vehicleState().driving;

    // Look input. Fine aim halves the look speed (OverTheShoulder TnOrbitRotationCameraBehavior:
    // FineAim 25/12.5 vs default 50/25) [CONF ratio]. While Driving the look X axis is the steering
    // input (PlayerInCarForm.SetLocalInputs: SteeringInput = TnPlayerInput.GetNormalizedTurn()) and
    // the orbit yaw follows the truck (TnDrivingOrbitRotationCameraBehavior) [CONF bytecode].
    float look = (fineAiming_ ? cfg::kFineAimLookScale : 1.0f) * lookScale_;
    // UpdateInvertMouseByRobotForm / ...ByVehicleForm: one bInvertMouse flag from the profile per form [CONF].
    bool inv = invertY_[0];
    if (vehicleForm) inv = vform == VehicleFormType::Jet ? invertPlane_ : vform == VehicleFormType::Tank ? invertTank_ : invertCar_;
    const float invY = inv ? -1.0f : 1.0f;
    if (!driving) {
        camYaw_ -= in.mouseDX * cfg::kMouseSens * look;
        if (in.padConnected) camYaw_ -= in.padRX * 0.04f * look;
    }
    camPitch_ -= in.mouseDY * cfg::kMouseSens * look * invY;
    if (in.padConnected) camPitch_ += in.padRY * 0.03f * look * invY;
    // PitchRange of the active strategy: OverTheShoulder -75..75, HoverTruck -20..30, Truck -25..25.
    float pMin = cfg::kPitchMin, pMax = cfg::kPitchMax;
    if (vehicleForm) {
        // The chassis' camera set: hover / driving / flying strategy PitchRange [CONF authored per chassis].
        const ChassisDef& cd = pawn_->chassis();
        const CamStrategy& cs = (jet && pawn_->vehicleState().flying && cd.camFly.dist > 0.0f) ? cd.camFly : (driving ? cd.camDrive : cd.camHover);
        pMin = cs.pitchMin; pMax = cs.pitchMax;
    }
    camPitch_ = core::clampf(camPitch_, pMin, pMax);
    // Boost steering input = TnPlayerInput.GetNormalizedTurn() = aTurn (XboxTypeS_RightX) after HmPlayerInput's
    // RADIAL 0.25 deadzone over (aTurn, aLookUp): v' = n * (min(1,|v|) - 0.25) / 0.75; no temporal filter
    // [CONF RE MILESTONE03_VEHICLE_BOOST_STEERING]. The left stick X is RollControl, not steering.
    float steerIn = 0.0f;
    if (in.padConnected) {
        float rx = in.padRX, ry = in.padRY, mag = std::sqrt(rx * rx + ry * ry);
        if (mag > 0.25f) steerIn = core::clampf(rx * ((std::min(1.0f, mag) - 0.25f) / 0.75f) / mag, -1.0f, 1.0f);
    }
    // PC translation [PROV]: the mouse X axis is the camera-yaw axis, i.e. the right stick X, so it supplies
    // aTurn: mouse rate / kDriveMouseFullRate = stick deflection (a short 0.05 s average turns per-frame
    // mouse deltas into a rate). A/D stay left-stick X (strafe in hover, RollControl in boost).
    float mouseRate = dt > 0.0f ? in.mouseDX / dt : 0.0f;
    float mouseStick = core::clampf(mouseRate / cfg::kDriveMouseFullRate, -1.0f, 1.0f);
    steerSmoothed_ += (mouseStick - steerSmoothed_) * (1.0f - std::exp(-dt / 0.05f));
    if (std::fabs(steerIn) < 1e-4f) steerIn = steerSmoothed_;
    if (const char* s = std::getenv("WFC_STEERSTICK")) steerIn = (float)std::atof(s);   // test: right-stick X after deadzone
    intent_.steer = driving ? steerIn : 0.0f;
    // Jet flight lean inputs: GetNormalizedTurn / GetNormalizedLookUp (PlayerInPlaneForm.SetLocalInputs) [CONF]; the PC
    // mouse supplies them through the same rate translation as boost steering [PROV].
    {
        float mouseRateY = dt > 0.0f ? -in.mouseDY / dt : 0.0f;
        float stickY = core::clampf(mouseRateY / cfg::kDriveMouseFullRate, -1.0f, 1.0f);
        lookUpSmoothed_ += (stickY - lookUpSmoothed_) * (1.0f - std::exp(-dt / 0.05f));
        float lookIn = lookUpSmoothed_;
        if (in.padConnected && std::fabs(in.padRY) > 0.25f) lookIn = core::clampf(in.padRY, -1.0f, 1.0f);
        intent_.turnIn = jet ? steerIn : 0.0f;
        intent_.lookUpIn = jet ? lookIn : 0.0f;
    }
    intent_.ascend = jet && in.isDown(Button::Ascend);
    intent_.descend = jet && in.isDown(Button::Descend);

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
    if (in.wasPressed(Button::Dash)) {
        wantDashLatched_ = true;
        // Tank special move "180": TnCamera.RecenterCamera, cooldown TimeBetween180s 1.2 s [CONF RE C2]; the hull follows
        // the camera yaw. The recentre is applied as a half turn of the view [PROV: TnTurnAroundCameraBehavior timing].
        if (vehicleForm && vform == VehicleFormType::Tank && tank180Cooldown_ <= 0.0f) { camYaw_ += 3.1415927f; tank180Cooldown_ = 1.2f; }
    }
    tank180Cooldown_ = std::max(0.0f, tank180Cooldown_ - dt);

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
        tryBeginTransform();
    }

    // Fire: held flag persists across frames (consumed per simulation step while held) [CONF RE].
    wantFire_ = in.isDown(Button::Fire);
    // Reload: activates on RELEASE of a tap shorter than 0.3 s [CONF RE]; latched until consumed.
    bool reloadDown = in.isDown(Button::Reload);
    if (reloadDown) reloadHeld_ += dt;
    if (!reloadDown && prevReloadDown_ && reloadHeld_ < cfg::kReloadTapTime) wantReload_ = true;
    if (!vehicleForm && in.wasPressed(Button::Dash)) wantAbility_ = 0;      // Ability0
    if (!vehicleForm && in.wasPressed(Button::Ability1)) wantAbility_ = 1;  // Ability1
    abilityStickFwd_ = intent_.moveForward; abilityStickRight_ = intent_.moveRight;
    if (in.wasPressed(Button::Killstreak)) wantKillstreak_ = true;
    if (in.wasPressed(Button::NextWeapon)) wantSwitch_ = 1;
    if (in.wasPressed(Button::PrevWeapon)) wantSwitch_ = -1;
    if (!reloadDown) reloadHeld_ = 0.0f;
    prevReloadDown_ = reloadDown;

    updateCameraStrategy(in, dt);
    tickHud();
    // Hovering faces the controller rotation, which PlayerInVehicleForm.PlayerMove sets to the CAMERA
    // rotation (after the strategy's orbit smoother) [CONF bytecode]; the robot faces the aim.
    intent_.faceYaw = vehicleForm ? viewYaw_ : camYaw_;
    intent_.viewPitch = viewPitch_;
}

// Camera strategies (HmCameraStrategySet Truck_Optimus_CAMSET): OverTheShoulder for the robot (and
// TransformToRobo), HoverTruck_Optimus for Hovering (and TransformToVehicle), Truck_Optimus for Driving.
void PlayerController::updateCameraStrategy(const platform::InputFrame& in, float dt) {
    namespace cfg = core::config;
    if (!pawn_) return;
    bool vehicleForm = pawn_->moveForm() == Form::Vehicle;
    const Character::VehicleState& vs = pawn_->vehicleState();
    const bool jet = vehicleForm && pawn_->vehicleParams().form == VehicleFormType::Jet;
    bool driving = vehicleForm && !jet && vs.driving;
    bool nitro = driving && vs.nitroRemain > 0.0f;
    int want = !vehicleForm ? 0 : (driving ? 2 : 1);

    // Strategy targets [CONF strategy data; see Config.h]. The robot anchor sits Offset Z 200 UU above the actor
    // (cylinder centre) of this chassis.
    float anchor = cfg::kCamHeight - cfg::kPawnHalfHeight, dist = cfg::kCamDistance;
    const ChassisDef& cdef = pawn_->chassis();
    const CamStrategy& camH = (jet && vs.flying && cdef.camFly.dist > 0.0f) ? cdef.camFly : cdef.camHover;
    if (want == 1) { anchor = camH.anchor; dist = camH.dist; }
    if (want == 2) {
        // TnLocationOffsetCameraBehavior: TnPCS_Boosting (nitro) orbit 650, in 0.5 s / out 2.0 s.
        float rate = nitro ? 1.0f / cfg::kNitroCamDistIn : -1.0f / cfg::kNitroCamDistOut;
        nitroDist_ = core::clampf(nitroDist_ + rate * dt, 0.0f, 1.0f);
        anchor = cdef.camDrive.anchor;
        dist = cdef.camDrive.dist + (cfg::kNitroCamDist - cfg::kDriveCamDist) * smoothstep01(nitroDist_);
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
        viewPitch_ = core::clampf(p, vs.pitch + pawn_->chassis().camDrive.pitchMin, vs.pitch + pawn_->chassis().camDrive.pitchMax);
    }

    // FOV (TnFovCameraBehavior): first matching PCS row, HmC2Smoother with that row's SmoothTime.
    float fovT = cfg::kCamFovXDeg, fovSm = cfg::kCamFovSmooth;
    if (want == 0 && fineAiming_) { fovT = cfg::kFineAimFovXDeg; fovSm = cfg::kFineAimFovSmooth; }
    if (want == 1) fovT = camH.fov;
    if (want == 2) { fovT = nitro ? cfg::kNitroCamFov : cdef.camDrive.fov; fovSm = nitro ? cfg::kNitroCamFovSmooth : cfg::kCamFovSmooth; }
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

core::Vec3 PlayerController::desiredCameraPos() const {
    // HmOrbitUpdateLocationRotation: Location = anchor + (OrbitLocationOffset rotated by the orbit
    // rotation). Anchor = actor location (shared by both forms during a transformation) + the
    // strategy's HmOffsetAnchorPointRelativeToActor Z offset.
    core::Vec3 focus = pawn_->actorLocation() + core::Vec3{0, anchorCur_, 0};
    core::Vec3 dir = core::forwardFromYawPitch(viewYaw_, viewPitch_);
    core::Vec3 right = core::normalize(core::cross(core::forwardFromYawPitch(viewYaw_, 0.0f), core::Vec3{0, 1, 0}));
    core::Vec3 up = core::cross(right, dir);
    return focus + dir * (offset_.x - distCur_) + right * offset_.y + up * offset_.z;
}

// Evaluated per RENDER frame from the current orbit rotation and pawn location, like the strategy's other behaviours
// (camera rotation is updated per frame in handleInput). Caching the position per fixed step (Pass 20) paired a stale
// position with a fresh rotation on every frame that ran 0 or 2 simulation steps: the view swung around the pawn each
// frame (the M05 "interlaced" character / vehicle).
core::Vec3 PlayerController::cameraPos() const {
    if (spectating_) return specPos_;
    const core::Vec3 desiredCam = desiredCameraPos();
    if (!pawn_ || !col_) return desiredCam;
    static const bool reModel = std::getenv("WFC_CAMRE") != nullptr;
    if (!reModel) {
        // Default: provisional pull-in toward the anchor in front of the first hit.
        core::Vec3 focus = pawn_->actorLocation() + core::Vec3{0, anchorCur_, 0};
        core::Vec3 d = desiredCam - focus;
        float len = core::length(d), t;
        if (len > 1e-3f && col_->segmentHit(focus, desiredCam, t)) return focus + d * (std::max(0.0f, t * len - 0.3f) / len);
        return desiredCam;
    }
    if (!camOldValid_) return desiredCam;
    // RE model: the smoothed offset lives in target space (camera rotation, target location) - apply it with the
    // current frame's rotation and location.
    const core::Vec3 F = core::forwardFromYawPitch(viewYaw_, viewPitch_);
    const core::Vec3 R = core::normalize(core::cross(core::forwardFromYawPitch(viewYaw_, 0.0f), core::Vec3{0, 1, 0}));
    const core::Vec3 U = core::cross(R, F);
    return pawn_->actorLocation() + F * camOld_.x + R * camOld_.y + U * camOld_.z;
}

namespace {
// Camera traces: Actor.TraceCamera with bTraceActors false = world geometry only (BSP, static meshes,
// BlockingVolumes), BlockCameras-gated; the 252 + 8 Streets BlockCameras=False props are also non-colliding and
// absent from the collision worlds. Movers are actors: the static (mover-free) segmentHit is used.
// Box sweeps / overlaps (non-zero extent) are approximated by rays from the box's 15 sample points (corners, face
// centres, centre) [PROV: no exact AABB sweep in the collision code].
struct CamTrace {
    const CollisionWorld* box = nullptr;   // non-zero-extent world
    const CollisionWorld* ray = nullptr;   // zero-extent world
    core::Vec3 ext{0, 0, 0};
    bool rayHit(const core::Vec3& a, const core::Vec3& b) const {
        float t; return ray && ray->segmentHit(a, b, t);
    }
    // Distance travelled along a -> b before the box hits (full length if clear).
    float sweep(const core::Vec3& a, const core::Vec3& b) const {
        core::Vec3 d = b - a;
        float len = core::length(d);
        if (!box || len < 1e-4f) return len;
        float best = 1.0f;
        for (int i = 0; i < 15; ++i) {
            core::Vec3 o{0, 0, 0};
            if (i < 8) o = {(i & 1) ? ext.x : -ext.x, (i & 2) ? ext.y : -ext.y, (i & 4) ? ext.z : -ext.z};
            else if (i < 14) { int ax = (i - 8) / 2; float s = ((i - 8) & 1) ? 1.0f : -1.0f;
                o = {ax == 0 ? s * ext.x : 0.0f, ax == 1 ? s * ext.y : 0.0f, ax == 2 ? s * ext.z : 0.0f}; }
            float t;
            if (box->segmentHit(a + o, b + o, t)) best = std::min(best, t);
        }
        return best * len;
    }
    // Box overlap at c: any box edge / diagonal crosses geometry.
    bool overlap(const core::Vec3& c) const {
        if (!box) return false;
        auto corner = [&](int i) { return c + core::Vec3{(i & 1) ? ext.x : -ext.x, (i & 2) ? ext.y : -ext.y, (i & 4) ? ext.z : -ext.z}; };
        static const int e[16][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7},{0,7},{1,6},{2,5},{3,4}};
        float t;
        for (const auto& ed : e) if (box->segmentHit(corner(ed[0]), corner(ed[1]), t)) return true;
        return false;
    }
};
float stepToward(float cur, float d, float rate, float dt) {   // Old += sign(D) * min(|D|, rate*dt)
    float m = std::min(std::fabs(d), rate * dt);
    return cur + (d < 0.0f ? -m : m);
}
} // namespace

void PlayerController::tickCameraCollision(float dt) {
    namespace cfg = core::config;
    if (!pawn_) return;
    const core::Vec3 desiredCam = desiredCameraPos();
    // Strategy change (transformation): each behaviour's Activate clears the smoothing state.
    if (strategy_ != camCollStrategy_) { camCollStrategy_ = strategy_; camOldValid_ = false; camSmoothRemain_ = 0.0f; }
    if (!col_) { camLoc_ = desiredCam; camLocValid_ = true; camObstructed_ = false; return; }
    // Default: the provisional pull-in (validated feel). WFC_CAMRE=1: the RE obstruction behaviours below, whose
    // native box sweeps are approximated by rays (PARTIAL; WFC_CAMTEST shows more visible clipping than the default).
    static const bool oldModel = std::getenv("WFC_CAMRE") == nullptr;
    if (oldModel) {                          // stateless: cameraPos() evaluates it per render frame
        core::Vec3 focus = pawn_->actorLocation() + core::Vec3{0, anchorCur_, 0};
        core::Vec3 d = desiredCam - focus;
        float len = core::length(d), t;
        camObstructed_ = len > 1e-3f && col_->segmentHit(focus, desiredCam, t);
        return;
    }
    // Camera frame (UE X forward, Y right, Z up).
    const core::Vec3 F = core::forwardFromYawPitch(viewYaw_, viewPitch_);
    const core::Vec3 R = core::normalize(core::cross(core::forwardFromYawPitch(viewYaw_, 0.0f), core::Vec3{0, 1, 0}));
    const core::Vec3 U = core::cross(R, F);
    const float nearD = 1.0f;                                  // near-clip-plane centre 100 UU ahead
    // Near-plane box: local half extent (0.01, tan(FOV/2)*100, tan(FOV/2)*100 / max(1, aspect)) UU, world AABB of
    // the rotated corners.
    float th = std::tan(fovCur_ * 0.5f * 0.0174533f);
    core::Vec3 le{0.0001f, th * nearD, th * nearD / std::max(1.0f, aspect_)};
    CamTrace tr;
    tr.box = col_; tr.ray = colRay_ ? colRay_ : col_;
    tr.ext = {std::fabs(F.x) * le.x + std::fabs(R.x) * le.y + std::fabs(U.x) * le.z,
              std::fabs(F.y) * le.x + std::fabs(R.y) * le.y + std::fabs(U.y) * le.z,
              std::fabs(F.z) * le.x + std::fabs(R.z) * le.y + std::fabs(U.z) * le.z};
    const core::Vec3 desired = desiredCam + F * nearD;
    const bool vehicle = strategy_ != 0;
    // Origin: robot = pawn location (cylinder centre); vehicles = location + _OriginOffset (-200, 0, 75) rotated by
    // the vehicle rotation (yaw + body pitch; roll not applied [PROV]).
    core::Vec3 origin = pawn_->actorLocation();
    if (vehicle) {
        const auto& vs = pawn_->vehicleState();
        core::Vec3 vf = core::forwardFromYawPitch(pawn_->yaw(), vs.pitch);
        core::Vec3 vr = core::normalize(core::cross(core::forwardFromYawPitch(pawn_->yaw(), 0.0f), core::Vec3{0, 1, 0}));
        core::Vec3 vu = core::cross(vr, vf);
        origin = origin + vf * cfg::kCamVehOriginFwd + vu * cfg::kCamVehOriginUp;
    }
    core::Vec3 clip = desired;
    camObstructed_ = tr.rayHit(origin, desired) || tr.overlap(desired);
    if (camObstructed_) {
        camSmoothRemain_ = 1.0f;
        core::Vec3 off = desired - origin;
        float ox = core::dot(off, F), oy = core::dot(off, R), oz = core::dot(off, U);
        auto upThenHoriz = [&](core::Vec3 horiz) {                 // rise along camera up, then sweep horiz
            core::Vec3 upMove = U * oz;
            float lu = core::length(upMove);
            float dUp = tr.sweep(origin, origin + upMove);
            core::Vec3 P = lu > 1e-4f ? origin + upMove * (dUp / lu) : origin;
            float lh = core::length(horiz);
            float dH = tr.sweep(P, P + horiz);
            return lh > 1e-4f ? P + horiz * (dH / lh) : P;
        };
        if (!vehicle) {
            clip = upThenHoriz(F * ox + R * oy);
        } else {
            // Candidate 0: diagonal (X, Y, 0.5 Z); candidate 1: up, then straight back by -OffCS.X.
            core::Vec3 dd = F * ox + R * oy + U * (0.5f * oz);
            float ld = core::length(dd);
            core::Vec3 c0 = ld > 1e-4f ? origin + dd * (tr.sweep(origin, origin + dd) / ld) : origin;
            core::Vec3 c1 = upThenHoriz(F * (-1.0f) * (-ox));
            auto weight = [&](const core::Vec3& c) {
                return (1.0f - std::min(1.0f, core::length(c - desired) * 0.2f)) + std::min(1.0f, core::length(c - origin) * 0.2f);
            };
            clip = weight(c1) > weight(c0) ? c1 : c0;              // tie keeps index 0
        }
    }
    core::Vec3 cam = clip - F * nearD;
    // Smoothing in target space T = (camera rotation, target location).
    const core::Vec3 L = pawn_->actorLocation();
    core::Vec3 O{core::dot(cam - L, F), core::dot(cam - L, R), core::dot(cam - L, U)};
    const float quick = cfg::kCamCollQuickSpeed, slow = cfg::kCamCollSlowSpeed;
    if (!vehicle) {
        camSmoothRemain_ = std::max(0.0f, camSmoothRemain_ - dt);
        if (!camOldValid_ || camSmoothRemain_ == 0.0f) { camOld_ = O; camOldValid_ = true; }
        core::Vec3 D = O - camOld_;
        camOld_.x = stepToward(camOld_.x, D.x, D.x < 0.0f ? slow : quick, dt);
        camOld_.y = stepToward(camOld_.y, D.y, D.y > 0.0f ? slow : quick, dt);
        camOld_.z = stepToward(camOld_.z, D.z, D.z > 0.0f ? slow : quick, dt);
    } else {
        if (!camOldValid_) { camOld_ = O; camOldValid_ = true; }      // always smoothed
        core::Vec3 D = O - camOld_;
        camOld_.x = stepToward(camOld_.x, D.x, D.x < 0.0f ? slow : quick, dt);
        camOld_.y = stepToward(camOld_.y, D.y, slow, dt);
        camOld_.z = stepToward(camOld_.z, D.z, D.z > 0.0f ? slow : quick, dt);
    }
    camLoc_ = L + F * camOld_.x + R * camOld_.y + U * camOld_.z;
    camLocValid_ = true;
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

// Robot cylinder (radius 200, height 400 UU) standing on the floor at feet: five vertical columns (centre + 4 at 0.7 r)
// from above MaxStepHeight to the cylinder top must be clear of the movement collision. [PROV: whether the native
// FindSpotAwayFromPawns tests world geometry as well as pawns is PARTIAL in RE; the refusal itself is CONF]
bool PlayerController::robotFitsAt(const CollisionWorld* col_, const core::Vec3& feet, const Character* pawn) {
    if (!col_) return true;
    // Robot cylinder of the pawn's chassis (ROBODEF CollisionRadius / Height).
    const float r = (pawn ? pawn->robotParams().radius : core::config::kPawnRadius) * 0.7f,
                top = 2.0f * (pawn ? pawn->robotParams().halfHeight : core::config::kPawnHalfHeight);
    const core::Vec3 off[5] = {{0, 0, 0}, {r, 0, 0}, {-r, 0, 0}, {0, 0, r}, {0, 0, -r}};
    float t;
    // Centre column from above MaxStepHeight; offset columns from 1.8 m (0.4 + r x tan 45 deg): walkable slopes and stairs
    // under the cylinder's edge are not obstructions (the floor supports the cylinder at its centre).
    for (int i = 0; i < 5; ++i) {
        const core::Vec3& o = off[i];
        float from = i == 0 ? 0.4f : 0.4f + r;
        if (col_->segmentHit(feet + o + core::Vec3{0, from, 0}, feet + o + core::Vec3{0, top, 0}, t)) return false;
    }
    return true;
}

bool PlayerController::findRobotSpot(const CollisionWorld* col_, const core::Vec3& feet, core::Vec3& out, const Character* pawn) {
    if (robotFitsAt(col_, feet, pawn)) { out = feet; return true; }
    if (!col_) return false;
    // UWorld::FindSpot order [CONF RE TARGETED_PASS3 C6]: two passes at probe scale 1.0 then 0.5, pushing out along Z, then
    // X, then Y (both directions), then the diagonals +-X+-Y+-Z at 0.5; the first clear spot wins. Extent = the target
    // (robot) cylinder. A spot needs floor under it and no wall between it and the start.
    const float r = pawn ? pawn->robotParams().radius : core::config::kPawnRadius;
    const float hh = pawn ? pawn->robotParams().halfHeight : core::config::kPawnHalfHeight;
    std::vector<core::Vec3> cand;
    for (float sc : {1.0f, 0.5f}) {
        cand.push_back({0, hh * sc, 0});
        cand.push_back({r * sc, 0, 0}); cand.push_back({-r * sc, 0, 0});
        cand.push_back({0, 0, r * sc}); cand.push_back({0, 0, -r * sc});
    }
    for (int dx : {-1, 1}) for (int dz : {-1, 1}) for (int dy : {1, -1}) cand.push_back({dx * r * 0.5f, dy * hh * 0.5f, dz * r * 0.5f});
    float t;
    for (const core::Vec3& o : cand) {
        core::Vec3 c = feet + core::Vec3{o.x, 0.0f, o.z};
        float gy; core::Vec3 gn;
        // Floor under the spot: a lateral push may step up to 1.5 m; a vertical push rises at most its own extent (never
        // onto a surface above the start, e.g. the top of a ceiling slab).
        bool vert = o.x == 0.0f && o.z == 0.0f;
        if (!col_->groundHeight(c.x, c.z, feet.y + (vert ? o.y : 0.5f), vert ? 0.0f : 1.0f, gy, gn)) continue;
        c.y = gy;
        if ((o.x != 0.0f || o.z != 0.0f) && col_->segmentHit(feet + core::Vec3{0, 1.0f, 0}, c + core::Vec3{0, 1.0f, 0}, t)) continue;
        if (robotFitsAt(col_, c, pawn)) { out = c; return true; }
    }
    return false;
}

bool PlayerController::tryBeginTransform() {
    if (!pawn_) return false;
    if (pawn_->moveForm() == Form::Vehicle && col_) {
        // Target = robot: its 4 m cylinder must fit (vehicle actor -> floor below).
        core::Vec3 a = pawn_->actorLocation();
        float gy; core::Vec3 gn;
        core::Vec3 feet = a;
        if (col_->groundHeight(a.x, a.z, a.y, 4.0f, gy, gn)) feet.y = gy; else feet.y = a.y - pawn_->meshToActor(Form::Vehicle);
        core::Vec3 spot;
        if (!findRobotSpot(col_, feet, spot, pawn_)) {
            ++cantTransformCount_;                       // NotifyCantTransform + TransformFailedSound; no transform
            return false;
        }
        core::Vec3 shift{spot.x - feet.x, 0.0f, spot.z - feet.z};
        if (core::length(shift) > 1e-3f) {
            // SetLocation(Safe): the collision jumps; OffsetMeshes(Old - Safe) decays over 0.5 s (robot target).
            pawn_->setPosition(pawn_->position() + shift);
            pawn_->addTransformShift(shift * -1.0f);
        }
    }
    pawn_->beginTransform();
    return true;
}

void PlayerController::applyToPawn(World& world, float dt) {
    if (!pawn_) return;
    col_ = world.collision();
    colRay_ = world.weaponCollision();
    tickFineAim();
    MoveIntent step = intent_;
    step.wantJump = wantJumpLatched_;
    step.wantDash = wantDashLatched_;
    wantDashLatched_ = false;   // consumed by this step
    // TnAbilityManager.TriggerAbility: spam prevention, availability (cooldown), LocalTriggerAbility, then the cooldown
    // waits for CanStartCooldown. PlayerWalking.CanUseAbilities refuses while reloading or dodging [CONF].
    step.dodgeDir = 0;
    if (wantAbility_ >= 0) {
        Character::AbilitySlot& a = pawn_->abilities_[wantAbility_];
        bool can = pawn_->moveForm() == Form::Robot && !pawn_->isTransforming() && !pawn_->weapon().reloading() && !pawn_->isDodging();
        if (can && !a.id.empty() && a.spam <= 0.0f && a.cooldown <= 0.0f && !a.pendingCooldown) {
            if (a.implemented) {
                if (a.id == "Dodge") {
                    // TnPlayerInput.Dodge: |JoyUp| >= |JoyRight| ? (Up < 0 ? back : forward) : (Right < 0 ? left : right).
                    float up = abilityStickFwd_, rt = abilityStickRight_;
                    step.dodgeDir = std::fabs(up) >= std::fabs(rt) ? (up < 0.0f ? 4 : 3) : (rt < 0.0f ? 1 : 2);
                }
                if (a.id == "Warcry" || a.id == "Shockwave") pawn_->pendingAbilityEffect_ = a.id;   // ServerTriggerAbility (World)
                if (a.id == "Cloaking") pawn_->cloakRemain_ = 20.0f;                               // AddBuff(TnBuffCloak)
                if (a.id == "Hover") { step.hoverRequest = true; pawn_->hoverRequested_ = true; }   // PlayerController.Hover
                a.spam = 1.0f; a.pendingCooldown = true; ++abilityTriggers_;
            } else if (a.id != lastRefusedAbility_) {
                LOG_WARN("ability %s (slot %d) is not implemented in the rebuild [PARTIAL]", a.id.c_str(), wantAbility_);
                lastRefusedAbility_ = a.id;
            }
        }
        wantAbility_ = -1;
    }
    pawn_->tickAbilities(dt);
    CharacterMovement::update(*pawn_, step, dt, world.collision());
    // InRobotForm.BeginState (authority): MoveToSafeLocation; still stuck -> ForceIntoForm(vehicle) [CONF B3].
    if (wasTransforming_ && !pawn_->isTransforming() && pawn_->form() == Form::Robot && col_) {
        core::Vec3 feet = pawn_->position(), spot;
        if (!findRobotSpot(col_, feet, spot, pawn_)) {
            float above = pawn_->meshToActor(Form::Robot) - pawn_->meshToActor(Form::Vehicle);
            static const bool xlog = std::getenv("WFC_XFORMLOG") != nullptr;
            if (xlog) LOG_INFO("XFORMLOG forced back to vehicle at (%.2f %.2f %.2f)", feet.x, feet.y, feet.z);
            pawn_->setForm(Form::Vehicle);
            pawn_->setPosition(pawn_->position() + core::Vec3{0, above, 0});
            ++forcedVehicleCount_;
        } else if (core::length(spot - feet) > 1e-3f) {
            pawn_->setPosition(spot);
        }
    }
    wasTransforming_ = pawn_->isTransforming();
    pawn_->setAimPitch(camPitch_);   // drives the upper-body aim offset
    pawn_->tickSpreadModifier(dt);   // TnWeaponSpreadModifier airborne ramp
    wantJumpLatched_ = false;
    // Swap Weapons (latched until a step consumes it), robot form only.
    if (wantSwitch_ != 0) { if (pawn_->moveForm() == Form::Robot && !pawn_->isTransforming()) pawn_->requestWeaponSwitch(wantSwitch_); wantSwitch_ = 0; }
    pawn_->tickWeaponSwitch(dt);
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
    // Vehicle form: the vehicle weapon (CharacterData.VehicleWeapons, first active) fires from the vehicle's
    // WeaponSocket_Primary toward the camera aim point [CONF loadout + socket].
    if (pawn_->moveForm() == Form::Vehicle && !pawn_->isTransforming()) {
        if (Weapon* vw = pawn_->vehicleWeapon()) {
            vw->tick(dt);
            if (wantFire_ && vw->canFire()) {
                pawn_->exposeSelf();
                vw->onFired();
                // Origin: the chassis' vehicle WeaponSocket_Primary (bone x socket) when the vehicle mesh is displayed [CONF
                // socket data]; else the actor + 1 m.
                core::Vec3 origin = pawn_->actorLocation() + core::Vec3{0, 1.0f, 0};
                const SocketDef& vs = pawn_->chassis().vehicleWeapon;
                core::Mat4 bm;
                if (vs.valid && pawn_->form() == Form::Vehicle && pawn_->boneWorld(vs.bone, bm)) {
                    core::Mat4 w = bm * vs.local;
                    origin = core::Vec3{w.m[12], w.m[13], w.m[14]};
                }
                core::Vec3 camDir = core::forwardFromYawPitch(viewYaw_, viewPitch_);
                core::Vec3 camPos = cameraPos();
                float range = vw->rangeM > 0.0f ? vw->rangeM : 300.0f;
                core::Vec3 aimPoint = camPos + camDir * range;
                float th;
                if (world.collision() && world.collision()->segmentHit(camPos, aimPoint, th)) aimPoint = camPos + camDir * (range * th);
                core::Vec3 dir = core::normalize(aimPoint - origin);
                if (vw->projectile()) world.fireWeapon(*vw, origin + dir * 1.5f, dir);
                else if (vw->simulated()) for (int k = 0; k < std::max(1, vw->shots); ++k) world.fireWeapon(*vw, origin, dir);
            } else if (vw->ammo == 0 && vw->canReload()) vw->beginReload();
        }
    }
    // Robot weapon: fires while the trigger is held (held flag persists across render frames).
    if (wantFire_ && usable) {
        if (w.canFire()) {
            pawn_->exposeSelf();          // TnWeapon.OnPreServerFire -> ExposeSelf (decloak)
            w.onFired();
            static const bool noRecoil = std::getenv("WFC_NORECOIL") != nullptr;   // A/B diagnostic
            if (!noRecoil) pawn_->notifyFired();   // per-shot skeletal recoil (TnRecoiler)
            // Aim through the crosshair: trace the camera ray to find the aimed point, then fire
            // from the pawn eye toward it (the camera is offset over the shoulder).
            core::Vec3 eye = pawn_->actorLocation() + core::Vec3{0, pawn_->robotParams().eyeHeight, 0};   // BaseEyeHeight above the actor
            core::Vec3 camDir = core::forwardFromYawPitch(camYaw_, camPitch_);
            core::Vec3 camPos = cameraPos();
            float range = pawn_->weapon().rangeM;
            core::Vec3 aimPoint = camPos + camDir * range;
            float th;
            if (world.collision() && world.collision()->segmentHit(camPos, aimPoint, th))
                aimPoint = camPos + camDir * (range * th);
            core::Vec3 dir = core::normalize(aimPoint - eye);
            if (w.projectile()) world.fireWeapon(w, eye, dir);
            // NumShotsToFire traces per shot (shotgun pellets), each with its own spread sample [CONF data; HIGH: one ammo per shot].
            else for (int k = 0; k < std::max(1, pawn_->weapon().shots); ++k) world.fireHitscan(eye, dir);
        } else if (w.ammo == 0 && w.canReload()) {
            w.beginReload();
        }
    }
}

void PlayerController::updateCamera(render::Camera& cam) const {
    if (!pawn_) return;
    namespace cfg = core::config;
    if (spectating_) { cam.pos = specPos_; cam.yaw = specYaw_; cam.pitch = 0.0f; cam.fovXDeg = fovCur_; return; }
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
