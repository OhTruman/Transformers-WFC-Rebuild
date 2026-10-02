#include "game/CharacterMovement.h"
#include "game/Character.h"
#include "game/Collision.h"
#include "core/Config.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace game::CharacterMovement {

static constexpr float kStepUpConf = 0.35f; // [CONF] TnRobotForm._MovementCapabilities.MaxStepHeight 35 UU
// WFC_STEPUP=m overrides it for A/B diagnostics only.
static const float kStepUp = std::getenv("WFC_STEPUP") ? (float)std::atof(std::getenv("WFC_STEPUP")) : kStepUpConf;
static constexpr float kSnapDown = 1.0f;    // follow downward slopes/stairs while grounded
static constexpr float kVehicleProbeRadius = 1.75f;   // [PROV] vehicle cylinder not recovered

namespace {

// TnPawn.CalcVelocity (script, decoded from TransGame.xxx) for the robot form:
//   DesiredNormalized = ClampLength(Acceleration (flat), 1)
//   Desired = |DesiredNormalized| == 0 ? (falling ? PreviousFlatVelocity : 0)
//                                      : Normal(DesiredNormalized) * FMax(450, |DesiredNormalized| * MaxSpeed)
//   MaxAccel = AccelRate / (1 + CalculateMomentumPreservation(...))
//   LocalAccel = ((Desired - Previous) / dt) << Rotation, each axis clamped to +-MaxAccel
//   Velocity = Previous + (LocalAccel >> Rotation) * dt            (no separate speed cap)
core::Vec3 robotCalcVelocity(const Character& c, const core::Vec3& wish, const core::Vec3& prev,
                             bool falling, float faceYaw, float dt) {
    const float mult = c.speedMultiplier();
    const float maxSpeed = (falling ? core::config::kAirSpeed : core::config::kRobotMoveSpeed) * mult;
    float wishLen = core::length(wish);
    core::Vec3 desired;
    if (wishLen < 1e-4f) desired = falling ? prev : core::Vec3{0, 0, 0};
    else desired = wish * (std::max(core::config::kRobotMinMoveSpeed, wishLen * maxSpeed) / wishLen);

    // CalculateMomentumPreservation: only when moving faster than the max speed.
    float preservation = 0.0f;
    float flatSpeed = core::length(prev);
    if (flatSpeed >= maxSpeed + 0.001f) {
        bool airSet = falling || c.isTransforming();
        float dir = 0.0f;
        if (wishLen > 1e-4f && flatSpeed > 1e-4f)
            dir = (wish.x * prev.x + wish.z * prev.z) / (wishLen * flatSpeed);
        if (wishLen > 1e-4f && dir > core::config::kMomentumFwdCos)
            preservation = airSet ? core::config::kMomentumAirFwd : core::config::kMomentumGroundFwd;
        else if (wishLen < 1e-4f)
            preservation = airSet ? core::config::kMomentumAirNeutral : core::config::kMomentumGroundNeutral;
        else
            preservation = airSet ? core::config::kMomentumAirBack : core::config::kMomentumGroundBack;
    }
    float maxAccel = core::config::kRobotAccel / (1.0f + preservation);
    // [PROV] AirControl: UE3 native falling scales acceleration; applied to the accel limit here.
    if (falling) maxAccel *= core::config::kAirControl;

    core::Vec3 fwd = core::forwardFromYawPitch(faceYaw, 0.0f);
    core::Vec3 right = core::normalize(core::cross(fwd, core::Vec3{0, 1, 0}));
    core::Vec3 a = (desired - prev) * (1.0f / dt);
    float ax = core::clampf(a.x * fwd.x + a.z * fwd.z, -maxAccel, maxAccel);
    float ay = core::clampf(a.x * right.x + a.z * right.z, -maxAccel, maxAccel);
    core::Vec3 accel = fwd * ax + right * ay;
    return prev + accel * dt;
}

// Vehicle (TnCarForm + TnHoverCarSimulation / Truck_Physics), three mechanics:
//  Hovering (default): TnCarForm.Hovering.DoUpdate passes the VIEW yaw to TnHoverCarSimulation.Update;
//    UpdateTurn matches the rigid body yaw to it each step (yaw factor 1/dt) and UpdateStrafe drives the
//    local velocity toward (ForwardBack, RightLeft) x MaxLinearSpeed with
//    ClampLength(accel, MaxLinearAcceleration x DriftScale), DriftScale = (1 - Drift/0.5)^2 [CONF bytecode].
//  Hover dash (Dash while Hovering, cooldown 2 s): local axis of the dominant input, toward DashSpeed at
//    100000 UU/s^2 for DashDuration [CONF bytecode + HoverTruck_Physics].
//  Driving (Boost held, after the drift ramp): Truck_Physics MaxSpeed 30 m/s, MaxAcceleration 25 m/s^2,
//    wheels on the ground. [PROV] car steering/throttle model (wheel physics not recovered): full throttle
//    along the heading, heading turns toward the view yaw at kVehicleTurnRate.
//  Nitro (Dash while Driving, 3 s, cooldown 8 s): speed x1.5, steering x0.3 [CONF literals].
core::Vec3 vehicleVelocity(Character& c, const MoveIntent& in, core::Vec3 hv, float dt) {
    namespace cfg = core::config;
    Character::VehicleState& vs = c.vehicleState();
    vs.driftRemain = std::max(0.0f, vs.driftRemain - dt);
    vs.dashRemain = std::max(0.0f, vs.dashRemain - dt);
    vs.dashCooldown = std::max(0.0f, vs.dashCooldown - dt);
    vs.nitroRemain = std::max(0.0f, vs.nitroRemain - dt);
    vs.nitroCooldown = std::max(0.0f, vs.nitroCooldown - dt);

    // Hovering.UpdateBoosting: Boost && !IsDrifting -> Driving; Driving.UpdateBoosting: !Boost -> Hovering
    // (Hovering.BeginState -> Drift; Driving.EndState -> StopNitro).
    if (!vs.driving && in.wantBoost && vs.driftRemain <= 0.0f) vs.driving = true;
    else if (vs.driving && !in.wantBoost) {
        vs.driving = false;
        vs.driftRemain = cfg::kHoverDriftDuration;
        vs.nitroRemain = 0.0f;
    }

    if (in.wantDash) {
        if (!vs.driving && vs.dashCooldown <= 0.0f) {
            // DoDash: MakeVector(0, Sign(RightLeft), 0) if |RightLeft| > |ForwardBack| else
            // MakeVector(Sign(ForwardBack), 0, 0); StartSpecialMoveCooldown(TimeBetweenDashes).
            auto sgn = [](float x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); };
            vs.dashDir = std::fabs(in.moveRight) > std::fabs(in.moveForward)
                             ? core::Vec3{0, 0, sgn(in.moveRight)} : core::Vec3{sgn(in.moveForward), 0, 0};
            vs.dashRemain = cfg::kVehicleDashTime;
            vs.dashCooldown = cfg::kHoverDashCooldown;
        } else if (vs.driving && vs.nitroCooldown <= 0.0f) {
            vs.nitroRemain = cfg::kNitroDuration;
            vs.nitroCooldown = cfg::kNitroCooldown;
        }
    }

    // Ride height: hover suspension vs wheels on the ground (reached after the wheels delay).
    float targetRide = vs.driving ? 0.0f : cfg::kVehicleHoverH;
    float rideRate = cfg::kVehicleHoverH / cfg::kWheelsDropTime;
    vs.rideHeight += core::clampf(targetRide - vs.rideHeight, -rideRate * dt, rideRate * dt);

    if (!vs.driving) {
        c.setYaw(in.faceYaw);                                 // UpdateTurn: match the view yaw
        core::Vec3 fwd = core::forwardFromYawPitch(in.faceYaw, 0.0f);
        core::Vec3 right = core::normalize(core::cross(fwd, core::Vec3{0, 1, 0}));
        core::Vec3 desired;
        float maxAccel;
        if (vs.dashRemain > 0.0f) {
            desired = (fwd * vs.dashDir.x + right * vs.dashDir.z) * cfg::kVehicleBoostSpeed;
            maxAccel = cfg::kHoverDashAccel;
        } else {
            desired = (fwd * core::clampf(in.moveForward, -1.0f, 1.0f) +
                       right * core::clampf(in.moveRight, -1.0f, 1.0f)) * cfg::kVehicleMoveSpeed;
            float drift = 1.0f - vs.driftRemain / cfg::kHoverDriftDuration;
            maxAccel = cfg::kVehicleAccel * drift * drift;
        }
        core::Vec3 a = (desired - hv) * (1.0f / dt);
        float al = core::length(a);
        if (al > maxAccel && al > 1e-6f) a = a * (maxAccel / al);
        return hv + a * dt;
    }

    // Driving (normal boost) [PROV handling].
    float steer = cfg::kVehicleTurnRate * (vs.nitroRemain > 0.0f ? cfg::kNitroSteerScale : 1.0f);
    float d = std::remainder(in.faceYaw - c.yaw(), 6.2831853f);
    c.setYaw(c.yaw() + core::clampf(d, -steer * dt, steer * dt));
    core::Vec3 heading = core::forwardFromYawPitch(c.yaw(), 0.0f);
    float top = cfg::kTruckDriveSpeed * (vs.nitroRemain > 0.0f ? cfg::kNitroSpeedScale : 1.0f);
    core::Vec3 a = (heading * top - hv) * (1.0f / dt);
    float al = core::length(a);
    if (al > cfg::kTruckDriveAccel && al > 1e-6f) a = a * (cfg::kTruckDriveAccel / al);
    return hv + a * dt;
}

} // namespace

void update(Character& c, const MoveIntent& in, float dt, const CollisionWorld* col) {
    const Form mf = c.moveForm();
    const bool vehicle = mf == Form::Vehicle;
    FormTuning t = tuningFor(mf);

    core::Vec3 fwd = core::forwardFromYawPitch(in.faceYaw, 0.0f);
    core::Vec3 right = core::normalize(core::cross(fwd, core::Vec3{0, 1, 0}));
    core::Vec3 wish = fwd * in.moveForward + right * in.moveRight;
    float wishLen = core::length(wish);
    if (wishLen > 1.0f) wish = wish * (1.0f / wishLen);   // ClampLength(Acceleration, 1)

    bool wasGround = c.onGround();
    core::Vec3& v = c.velocity();
    core::Vec3 hv{v.x, 0, v.z};

    if (!vehicle) {
        hv = robotCalcVelocity(c, wish, hv, !wasGround, in.faceYaw, dt);
    } else {
        // Velocity handed over at transform start (<= kMaxTransformSpeed) is written straight into
        // the vehicle body; no reprojection, it decays through the acceleration limit.
        hv = vehicleVelocity(c, in, hv, dt);
    }
    v.x = hv.x; v.z = hv.z;

    // [CONF] pawn gravity -29.4 m/s^2; vehicles use RB-scaled gravity (0.66) -> -19.4 m/s^2.
    float g = vehicle ? core::config::kVehicleGravity : core::config::kGravity;
    v.y -= g * dt;
    if (!vehicle) v.y = std::max(v.y, -core::config::kRobotTerminalVel);

    // Jumping stays a robot-form, non-transforming action [PROV during a fold].
    if (in.wantJump && c.onGround() && !vehicle && !c.isTransforming() && t.jumpSpeed > 0.0f) {
        v.y = t.jumpSpeed;
        c.setOnGround(false);
    }

    core::Vec3 oldPos = c.position();
    core::Vec3 p = oldPos + v * dt;

    // --- Horizontal wall block (crude): stop horizontal motion if a wall is in the way ---
    if (col) {
        float probeR = vehicle ? kVehicleProbeRadius : core::config::kPawnRadius;
        float torso = core::config::kPawnHalfHeight;   // probe at capsule centre (~2 m)
        core::Vec3 a = oldPos + core::Vec3{0, torso, 0};
        core::Vec3 b = core::Vec3{p.x, oldPos.y + torso, p.z};
        core::Vec3 dir = b - a;
        float dist = core::length(dir);
        if (dist > 1e-4f) {
            core::Vec3 bEx = a + dir * ((dist + probeR) / dist); // probe slightly ahead
            float tHit;
            if (col->segmentHit(a, bEx, tHit) && tHit * (dist + probeR) < dist + probeR) {
                p.x = oldPos.x; p.z = oldPos.z;
                v.x = 0; v.z = 0;
            }
        }
    }

    // --- Ground resolution --- (vehicles hover SuspensionRadius above the surface) ---
    bool grounded = false;
    float hover = vehicle ? c.vehicleState().rideHeight : 0.0f;
    if (col) {
        float gy; core::Vec3 n;
        // Support search: highest surface at most MaxStepHeight above the body (groundHeight()
        // returns the highest surface <= nearY + stepUp). The hover then holds a vehicle at
        // surface + SuspensionRadius, pushing it up if it sits lower. (Before Pass 11 step-up and
        // hover were added twice, so robots stepped 0.7 m and vehicles could snap onto decks 4.4 m
        // above their body.) [PROV] a hovering vehicle can therefore mount ledges up to
        // hover + step (2.2 m); the original suspension-ray model is not recovered.
        float base = c.onGround() ? oldPos.y : p.y;
        if (col->groundHeight(p.x, p.z, base, kStepUp, gy, n)) {
            float floorY = gy + hover;
            if (p.y <= floorY + 0.001f) {       // at/below hover floor -> settle on it (incl. step-up)
                // A transformation into vehicle form lifts the pawn to hover height in one step;
                // absorb that pop visually (mesh offset decays over 0.5 s) [kTransformShiftBlend].
                if (c.isTransforming() && floorY - p.y > 0.05f)
                    c.addTransformShift(core::Vec3{0, p.y - floorY, 0});
                p.y = floorY; if (v.y < 0) v.y = 0; grounded = true;
            } else if (c.onGround() && (p.y - floorY) <= (kSnapDown + std::max(hover, c.hoverApplied()))) {
                // Follow terrain at hover height. A vehicle->robot fold drops the support height by
                // the hover offset in one step; the mesh offset absorbs it like the original's
                // MoveToSafeTransformationLocation + OffsetMeshes blend, so the robot stays grounded.
                if (c.isTransforming() && p.y - floorY > 0.05f)
                    c.addTransformShift(core::Vec3{0, p.y - floorY, 0});
                p.y = floorY; if (v.y < 0) v.y = 0; grounded = true;
            }
        }
    } else {
        float floorY = c.groundY + hover;
        if (p.y <= floorY) { p.y = floorY; if (v.y < 0) v.y = 0; grounded = true; }
    }
    c.setOnGround(grounded);
    if (grounded) c.setHoverApplied(hover);
    c.setPosition(p);

    // Facing: the robot faces the aim (camera) yaw every frame. The vehicle's yaw is set in
    // vehicleVelocity(); on robot->vehicle it starts on the pawn rotation (OnActivate
    // SetRBRotation(pawn Rotation)), which is the robot's aim yaw.
    if (!vehicle && c.isTransforming()) {
        // Vehicle->robot fold: turn from the vehicle heading to the aim along the fold instead of
        // snapping the still-visible vehicle mesh [PROV: turn timing not recovered].
        float p = c.transformProgress();
        float s = p * p * (3.0f - 2.0f * p);
        float d = std::remainder(in.faceYaw - c.transformStartYaw(), 6.2831853f);
        c.setYaw(c.transformStartYaw() + d * s);
    } else if (!vehicle) {
        c.setYaw(in.faceYaw);
    }   // vehicle yaw is set by vehicleVelocity() (view yaw while hovering; steering while driving)
}

} // namespace game::CharacterMovement
