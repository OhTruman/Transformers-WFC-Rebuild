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
        // Vehicle [CONF values, HoverTruck_Physics]: MaxLinearSpeed 15, accel 30, Dash 30 / 0.5 s.
        // [PROV] the hover-sim force model itself is not recovered; this is an accel-limited
        // approach toward the input velocity.
        bool boosting = in.wantBoost;
        float topSpeed = boosting ? core::config::kVehicleBoostSpeed : t.moveSpeed;
        core::Vec3 targetVel = wish * topSpeed;
        core::Vec3 dv = targetVel - hv;
        float accel = wasGround ? t.accel : t.accel * core::config::kAirControl;
        if (boosting) accel = core::config::kVehicleBoostSpeed / core::config::kVehicleDashTime;
        float maxDelta = accel * dt;
        float dvLen = core::length(dv);
        if (dvLen > maxDelta && dvLen > 1e-5f) dv = dv * (maxDelta / dvLen);
        hv += dv;
        // No hard speed clamp: speed handed over by a transformation (up to kMaxTransformSpeed)
        // decays through the acceleration limit instead of snapping.
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
    float hover = vehicle ? core::config::kVehicleHoverH : 0.0f;
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

    // Facing: the robot faces the aim (camera) yaw every frame; the vehicle steers toward its
    // travel direction. On robot->vehicle the vehicle starts on the pawn rotation (OnActivate
    // SetRBRotation(pawn Rotation)) because yaw is continuous here.
    if (!vehicle && c.isTransforming()) {
        // Vehicle->robot fold: turn from the vehicle heading to the aim along the fold instead of
        // snapping the still-visible vehicle mesh [PROV: turn timing not recovered].
        float p = c.transformProgress();
        float s = p * p * (3.0f - 2.0f * p);
        float d = std::remainder(in.faceYaw - c.transformStartYaw(), 6.2831853f);
        c.setYaw(c.transformStartYaw() + d * s);
    } else if (!vehicle) {
        c.setYaw(in.faceYaw);
    } else {
        core::Vec3 hv2{v.x, 0, v.z};
        if (core::length(hv2) > 1.0f) {
            float target = std::atan2(-v.x, -v.z);
            float d = std::remainder(target - c.yaw(), 6.2831853f);
            float maxStep = core::config::kVehicleTurnRate * dt;
            c.setYaw(c.yaw() + core::clampf(d, -maxStep, maxStep));
        }
    }
}

} // namespace game::CharacterMovement
