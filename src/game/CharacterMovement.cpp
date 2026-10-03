#include "game/CharacterMovement.h"
#include "game/Character.h"
#include "game/Collision.h"
#include "core/Config.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace game::CharacterMovement {

static constexpr float kStepUpConf = 0.35f; // [CONF] TnRobotForm._MovementCapabilities.MaxStepHeight 35 UU
// WFC_STEPUP=m overrides it for A/B diagnostics only.
static const float kStepUp = std::getenv("WFC_STEPUP") ? (float)std::atof(std::getenv("WFC_STEPUP")) : kStepUpConf;
static constexpr float kSnapDown = 1.0f;    // follow downward slopes/stairs while grounded
// Optimus truck rigid-body hull: VH_Optimus_PHYSSYS body on C_Reference_XR, convex hull box
// x -310..338, y +-154, z -35..185 UU around the mesh root [CONF AssetTools PHYSICS_STREETS].
static constexpr float kHullFront = 3.38f, kHullBack = 3.10f, kHullHalfWidth = 1.54f;
static constexpr float kHullBottom = -0.35f, kHullTop = 1.85f;

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

// Horizontal wall block against world geometry, probed at body height (one ray along the step,
// probeR ahead). On contact the body advances up to the gap, the velocity INTO the wall (horizontal hit
// normal) is removed and the rest of the step continues along the wall plane, re-probed for a second
// or third wall (corners), like physWalking's wall slide. (Zeroing all velocity and parking one probe
// radius out made the next, slower step creep forward, flipping Idle/Moving for a frame or two when
// stopping against a wall.) [PROV collision model: no capsule sweep]
bool wallBlock(const CollisionWorld* col, const core::Vec3& oldPos, core::Vec3& p, core::Vec3& v, float probeR) {
    if (!col) return false;
    const float torso = core::config::kPawnHalfHeight;   // probe at capsule centre (~2 m)
    core::Vec3 start{oldPos.x, oldPos.y + torso, oldPos.z};
    core::Vec3 move{p.x - oldPos.x, 0.0f, p.z - oldPos.z};
    const core::Vec3 move0 = move;
    bool blocked = false;
    for (int iter = 0; iter < 3; ++iter) {
        float dist = core::length(move);
        if (dist < 1e-4f) break;
        core::Vec3 dn = move * (1.0f / dist);
        float tHit; core::Vec3 n;
        if (!col->segmentHit(start, start + dn * (dist + probeR), tHit, n)) { start = start + move; move = {0, 0, 0}; break; }
        blocked = true;
        float allowed = std::max(0.0f, std::min(dist, tHit * (dist + probeR) - probeR));
        start = start + dn * allowed;
        core::Vec3 nh{n.x, 0.0f, n.z};
        float nl = core::length(nh);
        if (nl < 0.3f) { v.x = 0; v.z = 0; move = {0, 0, 0}; break; }   // not a wall face: stop
        nh = nh * (1.0f / nl);
        if (core::dot(nh, dn) > 0.0f) nh = nh * -1.0f;                  // face against the motion
        float vn = v.x * nh.x + v.z * nh.z;
        if (vn < 0.0f) { v.x -= nh.x * vn; v.z -= nh.z * vn; }
        core::Vec3 rest = dn * (dist - allowed);
        move = rest - nh * core::dot(rest, nh);                          // remainder along the wall
        if (core::dot(move, move0) <= 0.0f) { move = {0, 0, 0}; break; } // wedged (corner): no back-slide
    }
    if (blocked) { p.x = start.x; p.z = start.z; }
    return blocked;
}

// Vehicle body axes in world space (UE local axes: x = forward, y = right, z = up), from the yaw and the
// rigid body's pitch (+ nose up) / roll (+ right side down): UE FRotationMatrix rows with Yaw = 0 in the
// yaw frame: X = (CP,0,SP), Y = (SR*SP,CR,-SR*CP), Z = (-CR*SP,SR,CR*CP) as (forward, right, up).
struct BodyAxes { core::Vec3 x, y, z; };
BodyAxes bodyAxes(float yaw, float pitch, float roll) {
    core::Vec3 F = core::forwardFromYawPitch(yaw, 0.0f);
    core::Vec3 R = core::normalize(core::cross(F, core::Vec3{0, 1, 0}));
    core::Vec3 U{0, 1, 0};
    float cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll), sr = std::sin(roll);
    auto w = [&](float a, float b, float c) { return F * a + R * b + U * c; };
    return {w(cp, 0, sp), w(sr * sp, cr, -sr * cp), w(-cr * sp, sr, cr * cp)};
}

core::Vec3 clampLength(core::Vec3 a, float maxLen) {
    float l = core::length(a);
    return (l > maxLen && l > 1e-6f) ? a * (maxLen / l) : a;
}

// Vehicle form: TnCarForm (Hovering / Driving) driving a rigid body. One fixed step of the original
// per-tick update: inputs (Boosting, Dashing, Jumping) -> simulation -> rigid body integration.
//  Hovering = TnHoverCarSimulation.Update: UpdateSuspension (4 spring rays), UpdateTerminalVelocity,
//    UpdateDash or UpdateStrafe (body-local, ClampLength accel), UpdateRoll, UpdateTurn [CONF bytecode].
//  Driving = TnCarSimulation.Update: angular damping, air control, drag, boost [CONF bytecode]; the wheel
//    and tire model (UpdateWheelAssemblies) is not decoded: wheels rest on the support surface and
//    ground steering is a yaw rate [PROV].
//  Three mechanics stay separate: normal boost (Driving), hover dash (Dash while Hovering, forward only:
//  TnTruckForm.Hovering.DoDash), nitro/ram (Dash while Driving).
void vehicleStep(Character& c, const MoveIntent& in, float dt, const CollisionWorld* col) {
    namespace cfg = core::config;
    Character::VehicleState& vs = c.vehicleState();
    core::Vec3& v = c.velocity();
    const core::Vec3 oldPos = c.position();
    core::Vec3 p = oldPos;
    const float gRB = cfg::kVehicleGravity;
    vs.dashCooldown = std::max(0.0f, vs.dashCooldown - dt);
    vs.nitroCooldown = std::max(0.0f, vs.nitroCooldown - dt);

    // Hovering.UpdateBoosting: Boost && (!IsDrifting) -> Driving (CarSimulation.Activate: JumpTimeRemaining 0).
    // Driving.UpdateBoosting: !Boost -> Hovering (EndState StopNitro; Hovering.BeginState: hover Activate
    // clears dash/drift and resets the springs, then Drift() starts the 0.5 s authority ramp).
    if (!vs.driving && in.wantBoost && vs.driftRemain <= 0.0f) {
        vs.driving = true; vs.jumpBoost = 0.0f;
    } else if (vs.driving && !in.wantBoost) {
        vs.driving = false;
        vs.nitroRemain = 0.0f; vs.dashRemain = 0.0f;
        vs.driftRemain = cfg::kHoverDriftDuration;
        for (float& l : vs.spLen) l = -1.0f;              // TnSpring.Reset: _Length = -1
    }

    BodyAxes B = bodyAxes(c.yaw(), vs.pitch, vs.roll);
    const float stabRad = cfg::kHoverStabilityDeg * 0.0174533f;
    bool unstable = std::fabs(vs.pitch) > stabRad || std::fabs(vs.roll) > stabRad;

    // Special move (VehicleSpecialMove = Dash input): CanUseSpecialMove = no cooldown running.
    if (in.wantDash) {
        if (!vs.driving) {
            if (vs.dashCooldown <= 0.0f) {
                // TnTruckForm.Hovering.DoDash -> HoverSimulation.Dash((1,0,0)): forward only; Dash() is
                // refused while unstable; the cooldown starts either way (UpdateDashing).
                if (!unstable) { vs.dashRemain = cfg::kVehicleDashTime; vs.dashDir = core::Vec3{1, 0, 0}; }
                vs.dashCooldown = cfg::kHoverDashCooldown;
            }
        } else if (vs.nitroCooldown <= 0.0f && vs.nitroRemain <= 0.0f) {
            vs.nitroRemain = cfg::kNitroDuration;      // TnTruckForm.Driving.UpdateNitro -> StartNitro
            vs.nitroCooldown = cfg::kNitroCooldown;
        }
    }

    // Jumping (runs before the simulation update, on the previous step's ground state).
    if (!vs.driving) {
        // Hovering.UpdateJumping: the 0.3 s interval only counts down on the ground.
        if (vs.onTheGround && vs.jumpWait > 0.0f) vs.jumpWait = std::max(0.0f, vs.jumpWait - dt);
        if (in.wantJump && vs.jumpWait == 0.0f && vs.onTheGround) {
            v.y += cfg::kVehicleJumpSpeed;                 // ApplyLinearVelocity(0,0,JumpLinearSpeed 1200)
            vs.angVel.y -= cfg::kHoverJumpAngSpeed;        // ApplyLocalAngularVelocity(0,-1,0): nose up
            vs.jumpWait = cfg::kVehJumpInterval;
        }
    } else if (vs.onTheGround) {
        // Driving.UpdateJumping -> TnCarSimulation.Jump: local (600,0,1400), angular (0,-2,0).
        vs.jumpWait = std::max(0.0f, vs.jumpWait - dt);
        if (vs.jumpWait == 0.0f && in.wantJump) {
            vs.jumpBoost = cfg::kDriveJumpBoostTime;
            v = v + B.x * cfg::kDriveJumpFwd + B.z * cfg::kDriveJumpUp;
            vs.angVel.y -= cfg::kDriveJumpAngVel;
            vs.jumpWait = cfg::kVehJumpInterval;
            vs.onTheGround = false;
        }
    }
    vs.jumpBoost = std::max(0.0f, vs.jumpBoost - dt);

    core::Vec3 accel{0, -gRB, 0};       // linear acceleration this step (RB gravity included)
    float angAx = 0.0f, angAy = 0.0f;   // body-local angular acceleration (rad/s^2)
    float comAbove = 0.0f;              // COM height above the surface below it (diagnostics)

    if (!vs.driving) {
        // ---- TnHoverCarSimulation.UpdateSuspension ----
        const float ms = cfg::kVehMass * 0.25f;
        const float K = cfg::kSuspStiffness / ms, Bd = cfg::kSuspDamping / ms, rest = cfg::kSuspRestLength;
        core::Vec3 com = p + B.x * cfg::kVehComFwd + B.z * cfg::kVehComUp;
        core::Vec3 down = B.z * -1.0f;
        float gs = B.z.y * -gRB;   // (-Direction.Z) * GetGravityZ(): PHYS_RigidBody -> -2940 x 0.66 [CONF native M03 P1]
        core::Vec3 nsum{0, 0, 0};
        int contacts = 0;
        for (int i = 0; i < 4; ++i) {
            float ang = 0.7853982f + 1.5707963f * (float)i;   // Normal(1,1,0) rotated by 90 deg steps
            float rx = std::cos(ang) * cfg::kSuspMountRadius, ry = std::sin(ang) * cfg::kSuspMountRadius;
            core::Vec3 mount = com + B.x * rx + B.y * ry;
            float t = 1.0f; core::Vec3 n{0, 1, 0};
            bool hit = col ? col->segmentHit(mount, mount + down * rest, t, n) : false;
            if (!col && down.y < -1e-4f) {        // no collision world: flat plane at groundY
                float tp = (c.groundY - mount.y) / (down.y * rest);
                if (tp >= 0.0f && tp <= 1.0f) { hit = true; t = tp; n = core::Vec3{0, 1, 0}; }
            }
            float L = hit ? t * rest : rest;
            // TnSpring.Update(DeltaTime, CurrentLength, Gravity): implicit damped spring.
            float vel = vs.spLen[i] >= 0.0f ? (L - vs.spLen[i]) / dt : 0.0f;   // first update after Reset: 0
            vs.spLen[i] = L;
            float vpred = (vel + dt * (K * (rest - L) + gs)) / (1.0f + Bd * dt + K * dt * dt);
            float F = ms * ((vpred - vel) / dt - gs);
            if (!hit) continue;
            if (core::dot(n, B.z) < 0.0f) n = n * -1.0f;
            float Fa = std::max(0.0f, core::dot(B.z, n) * F);   // ApplyForce(up * FMax(0, Dot(up,N)*Force), mount)
            accel = accel + B.z * (Fa / cfg::kVehMass);
            angAx += ry * Fa / cfg::kVehInertiaX;             // body torque r x F (F along body z)
            angAy += -rx * Fa / cfg::kVehInertiaY;
            nsum = nsum + n; ++contacts;
            static const bool dbg = std::getenv("WFC_VEHDBG") != nullptr;
            if (dbg) LOG_INFO("susp %d L=%.3f vel=%.2f F=%.0f Fa=%.0f n=(%.2f,%.2f,%.2f) mount.y=%.2f",
                              i, L, vel, F, Fa, n.x, n.y, n.z, mount.y);
        }
        vs.contacts = contacts;
        vs.contactN = contacts ? nsum * (1.0f / (float)contacts) : core::Vec3{0, 0, 0};
        vs.onTheGround = vs.contactN.y > cfg::kHoverCosGroundAngle;

        // ---- UpdateTerminalVelocity ----
        if (v.y < -cfg::kHoverTerminalVel) v.y = -cfg::kHoverTerminalVel;

        // ---- UpdateDash / UpdateStrafe (body-local; ApplyLocalLinearAcceleration) ----
        core::Vec3 lv{core::dot(v, B.x), core::dot(v, B.y), core::dot(v, B.z)};
        core::Vec3 la;
        if (vs.dashRemain > 0.0f) {
            vs.dashRemain = unstable ? 0.0f : std::max(0.0f, vs.dashRemain - dt);
            float maxS = vs.dashRemain > 0.0f ? cfg::kVehicleBoostSpeed : cfg::kVehicleMoveSpeed;
            core::Vec3 des{vs.dashDir.x * maxS, vs.dashDir.z * maxS, 0.0f};
            la = clampLength((des - lv) * (1.0f / dt), cfg::kHoverDashAccel);     // AxisMask (1,1,1)
        } else {
            vs.driftRemain = std::max(0.0f, vs.driftRemain - dt);
            float stab = B.z.y * B.z.y;                                         // Square(Abs(up.Z))
            float drift = 1.0f - vs.driftRemain / cfg::kHoverDriftDuration;     // CalculateDriftScale
            float maxA = cfg::kVehicleAccel * drift * drift * stab;
            core::Vec3 des{core::clampf(in.moveForward, -1.0f, 1.0f) * cfg::kVehicleMoveSpeed,
                           core::clampf(in.moveRight, -1.0f, 1.0f) * cfg::kVehicleMoveSpeed, 0.0f};
            la = clampLength(core::Vec3{des.x - lv.x, des.y - lv.y, 0.0f} * (1.0f / dt), maxA);   // AxisMask (1,1,0)
        }
        accel = accel + B.x * la.x + B.y * la.y + B.z * la.z;

        // ---- UpdateRoll: ApplyLocalAngularAcceleration(RightLeftInput - LocalAngularVelocity.Z, 0, 0) ----
        angAx += core::clampf(in.moveRight, -1.0f, 1.0f) - vs.angVel.z;

        // ---- UpdateTurn: yaw matches the view yaw each tick; pitch/roll are corrected only when there is
        // no contact or the body is upside down (ShouldUpright), by 5% of the error per tick. ----
        float dyaw = std::remainder(in.faceYaw - c.yaw(), 6.2831853f);
        vs.angVel.z = -dyaw / dt;                      // UE yaw rate (+ = turning right)
        vs.yawRate = vs.angVel.z;
        c.setYaw(in.faceYaw);
        if (contacts == 0 || B.z.y < 0.01f) {
            float f = 1.0f - std::pow(1.0f - cfg::kHoverUprightPerTick, dt * 30.0f);
            vs.angVel.x = f * vs.roll / dt;
            vs.angVel.y = f * vs.pitch / dt;
        }
        comAbove = rest;
        float gy; core::Vec3 gn;
        if (col && col->groundHeight(com.x, com.z, com.y, 0.0f, gy, gn)) comAbove = com.y - gy;
    } else {
        // ---- Driving: TnCarSimulation.Update ----
        vs.nitroRemain = std::max(0.0f, vs.nitroRemain - dt);
        bool nitro = vs.nitroRemain > 0.0f;
        // UpdateSimulationInputs: Accelerator 1, Steering = Sign(s)*s^2 (x SteeringScale: nitro 0.3).
        float s = core::clampf(in.steer, -1.0f, 1.0f);
        vs.steer = (s < 0.0f ? -s * s : s * s) * (nitro ? cfg::kNitroSteerScale : 1.0f);
        float gy = 0.0f; core::Vec3 gn{0, 1, 0};
        bool support = col ? col->groundHeight(p.x, p.z, p.y, 0.6f, gy, gn) : (gy = c.groundY, true);
        float above = support ? p.y - gy : 1e9f;
        bool wheels = support && above <= 0.05f && v.y <= 0.5f;      // [PROV] wheel contact
        vs.onTheGround = wheels;
        // UpdateTerminalVelocity (airborne): Lerp(LandingVelocity, TerminalVelocity, dist/LandingTraceDistance).
        if (!wheels) {
            float tv = cfg::kDriveLandingVel + (cfg::kDriveTerminalVel - cfg::kDriveLandingVel) *
                       std::min(1.0f, above / cfg::kDriveLandingTrace);
            if (v.y < -tv) v.y = -tv;
        }
        // UpdateAngularDamping: AngularDamping x (1-|Steering|)^2 with a wheel down, else x 1.
        float damp = cfg::kDriveAngularDamping * (wheels ? (1.0f - std::fabs(vs.steer)) * (1.0f - std::fabs(vs.steer)) : 1.0f);
        vs.angVel = vs.angVel * (1.0f / (1.0f + damp * dt));
        core::Vec3 F = core::forwardFromYawPitch(c.yaw(), 0.0f);
        core::Vec3 R = core::normalize(core::cross(F, core::Vec3{0, 1, 0}));
        if (!wheels) {
            // UpdateAirControl: strafe + turn by steering; pitch the nose down to PitchForwardLimit.
            accel = accel + R * (vs.steer * cfg::kDriveAirStrafeAccel);
            vs.angVel.z += vs.steer * cfg::kDriveAirTurnAccel * dt;
            if (vs.pitch > cfg::kDrivePitchFwdLimit) angAy += cfg::kDrivePitchFwdAccel;
        } else {
            // UpdateDrag: -Normal(v) * v^2 |g_RB| / TerminalVelocity^2.
            float sp = core::length(v);
            if (sp > 1e-4f) accel = accel - v * (1.0f / sp) * (sp * sp * gRB / (cfg::kPawnTerminalVel * cfg::kPawnTerminalVel));
            // UpdateBoost: Lerp(MaxAcceleration, Drag(MaxSpeed), fwd/MaxSpeed) + ExtraBoost, x BoostScale x 1.
            float maxS = cfg::kTruckDriveSpeed * (nitro ? cfg::kNitroSpeedScale : 1.0f);
            float fwd = core::dot(v, B.x);
            float dragMax = maxS * maxS * gRB / (cfg::kPawnTerminalVel * cfg::kPawnTerminalVel);
            float a0 = cfg::kTruckDriveAccel * cfg::kDriveLowSpeedBoostScale, z = maxS * cfg::kDriveLowSpeedBoostThreshold;
            float cs = core::clampf(fwd, 0.0f, z);
            float extra = std::min(a0 / (z * z) * cs * cs - 2.0f * a0 / z * cs + a0, cfg::kDriveMaxExtraAccel);
            float boostScale = 1.0f - (core::clampf(B.x.y, 0.5f, 0.866f) - 0.5f) / (0.866f - 0.5f);
            float acc = cfg::kTruckDriveAccel + (dragMax - cfg::kTruckDriveAccel) * (fwd / maxS) + extra;
            accel = accel + B.x * (acc * boostScale);
            // Tires (Driving only) [native M03 P3: F = clamp(-v_lat * coeff * scale * Load, +-2 (M/4)|g|) per
            // wheel]: the cap is CONF (summed over 4 wheels: 2|g| of lateral acceleration); the coefficient
            // and load are not recovered, so the decay rate stays [PROV] under the confirmed cap.
            float vlat = core::dot(v, R);
            float alat = core::clampf(-vlat * cfg::kDriveLateralGrip, -2.0f * gRB, 2.0f * gRB);
            if (std::fabs(alat * dt) > std::fabs(vlat)) alat = -vlat / dt;
            v = v + R * (alat * dt);
            vs.angVel.z = vs.steer * cfg::kDriveTurnRate;
            vs.angVel.x = 0.0f;
            if (vs.angVel.y > 0.0f) vs.angVel.y = 0.0f;       // keep a jump's nose-up rate, else level
            float k = 1.0f - std::exp(-10.0f * dt);           // [PROV] settle on the wheels
            vs.pitch -= vs.pitch * k; vs.roll -= vs.roll * k;
        }
        c.setYaw(c.yaw() - vs.angVel.z * dt);                  // UE +z (turn right) = rebuild yaw decreasing
        vs.yawRate = vs.angVel.z;
        comAbove = above;
    }

    // Rigid-body integration (semi-implicit Euler).
    vs.angVel.x += angAx * dt;
    vs.angVel.y += angAy * dt;
    vs.roll -= vs.angVel.x * dt;                       // + angular velocity about x lifts the right side
    vs.pitch -= vs.angVel.y * dt;                      // + angular velocity about y drops the nose
    vs.roll = core::clampf(vs.roll, -1.2f, 1.2f);
    vs.pitch = core::clampf(vs.pitch, -1.2f, 1.2f);
    v = v + accel * dt;
    p = p + v * dt;

    core::Vec3 moveDir = core::normalize(core::Vec3{p.x - oldPos.x, 0.0f, p.z - oldPos.z});
    // Wall probe reach = the hull's extent along the travel direction (box support distance from the root).
    core::Vec3 hf = core::forwardFromYawPitch(c.yaw(), 0.0f);
    core::Vec3 hr = core::normalize(core::cross(hf, core::Vec3{0, 1, 0}));
    float along = core::dot(moveDir, hf);
    float hullReach = std::fabs(along) * (along >= 0.0f ? kHullFront : kHullBack) + std::fabs(core::dot(moveDir, hr)) * kHullHalfWidth;
    if (wallBlock(col, oldPos, p, v, std::max(hullReach, kHullHalfWidth)) && vs.driving) {
        // Driving.OnRigidBodyCollision: a frontal hit (contact normal . forward > CosCollisionNormalThreshold
        // 0.866) drops back to Hovering (ram consumption during nitro not implemented). [PROV contact
        // normal approximated by the blocked travel direction.]
        core::Vec3 fwdFlat = core::forwardFromYawPitch(c.yaw(), 0.0f);
        if (core::dot(moveDir, fwdFlat) > 0.866f) {
            vs.driving = false;
            vs.nitroRemain = 0.0f; vs.dashRemain = 0.0f;
            vs.driftRemain = cfg::kHoverDriftDuration;
            for (float& l : vs.spLen) l = -1.0f;
        }
    }

    // [PROV] Chassis top against overhead geometry: the PhysicalVehicleMesh hull blocks upward motion
    // through awnings/decks (otherwise a suspension ray starting just above a thin surface reports a
    // near-zero length and the spring damper term spikes).
    if (col && v.y > 0.0f) {
        float top = kHullTop;                     // hull top above the root
        core::Vec3 a{p.x, oldPos.y + cfg::kVehComUp, p.z};
        core::Vec3 b = core::Vec3{p.x, p.y + top, p.z};
        float t; core::Vec3 n;
        if (top > cfg::kVehComUp && col->segmentHit(a, b, t, n) && std::fabs(n.y) > 0.7f) {   // ceilings only
            float yHit = a.y + (b.y - a.y) * t;
            p.y = std::min(p.y, yHit - top);
            v.y = 0.0f;
        }
    }

    // Chassis / wheels against the ground.
    if (col) {
        float gy; core::Vec3 n;
        if (vs.driving) {
            if (col->groundHeight(p.x, p.z, std::max(oldPos.y, p.y), 0.6f, gy, n) && p.y <= gy) {
                p.y = gy; if (v.y < 0.0f) v.y = 0.0f;
                vs.onTheGround = true;
            }
        } else {
            // [PROV] the PhysicalVehicleMesh hull keeps the chassis off the ground when the springs bottom out.
            core::Vec3 com = p + B.x * cfg::kVehComFwd + B.z * cfg::kVehComUp;
            if (col->groundHeight(com.x, com.z, std::max(oldPos.y, p.y) + cfg::kVehComUp, 0.6f, gy, n) &&
                com.y - gy < cfg::kVehComUp - kHullBottom) {          // hull bottom 0.35 m below the root
                p.y += (cfg::kVehComUp - kHullBottom) - (com.y - gy);
                if (v.y < 0.0f) v.y = 0.0f;
            }
        }
    } else if (p.y < c.groundY) {
        p.y = c.groundY; if (v.y < 0.0f) v.y = 0.0f;
        vs.onTheGround = true;
    }
    vs.rideHeight = comAbove;
    c.setOnGround(vs.onTheGround);
    c.setPosition(p);
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

    if (vehicle) {
        // The vehicle form owns its rigid body (velocity handed over at transform start is written
        // straight into it: no reprojection). Yaw comes from the simulation (view yaw while hovering).
        vehicleStep(c, in, dt, col);
        return;
    }

    bool wasGround = c.onGround();
    core::Vec3& v = c.velocity();
    core::Vec3 hv{v.x, 0, v.z};
    if (c.rammedRemain_ > 0.0f) {
        // RammedReaction [CONF native M03 P8]: the forced velocity set on entry carries for 0.5 s (no
        // movement input); EndState restores air movement with Velocity = (0, 0, base Z).
        c.rammedRemain_ -= dt;
        if (c.rammedRemain_ <= 0.0f) { c.rammedRemain_ = 0.0f; v.x = 0.0f; v.z = 0.0f; v.y = c.rammedBaseY_; }
    } else {
        hv = robotCalcVelocity(c, wish, hv, !wasGround, in.faceYaw, dt);
        v.x = hv.x; v.z = hv.z;
    }

    // [CONF] pawn gravity -29.4 m/s^2.
    v.y -= core::config::kGravity * dt;
    v.y = std::max(v.y, -core::config::kRobotTerminalVel);

    // Jumping stays a robot-form, non-transforming action [PROV during a fold].
    if (in.wantJump && c.onGround() && !c.isTransforming() && t.jumpSpeed > 0.0f) {
        v.y = t.jumpSpeed;
        c.setOnGround(false);
    }

    core::Vec3 oldPos = c.position();
    core::Vec3 p = oldPos + v * dt;

    // physWalking: after a blocked move the velocity is the actual displacement over the step.
    if (wallBlock(col, oldPos, p, v, core::config::kPawnRadius)) {
        v.x = (p.x - oldPos.x) / dt; v.z = (p.z - oldPos.z) / dt;
    }

    // --- Ground resolution ---
    bool grounded = false;
    if (col) {
        float gy; core::Vec3 n;
        // Support search: highest surface at most MaxStepHeight above the body (groundHeight()
        // returns the highest surface <= nearY + stepUp). A vehicle->robot fold starts falling from
        // the shared actor location (TnPawn: PHYS_Falling at t=0) and lands normally.
        float base = c.onGround() ? oldPos.y : p.y;
        if (col->groundHeight(p.x, p.z, base, kStepUp, gy, n)) {
            if (p.y <= gy + 0.001f) {           // at/below the floor -> settle on it (incl. step-up)
                p.y = gy; if (v.y < 0) v.y = 0; grounded = true;
            } else if (c.onGround() && (p.y - gy) <= kSnapDown) {
                p.y = gy; if (v.y < 0) v.y = 0; grounded = true;   // follow slopes/stairs down
            }
        }
    } else {
        if (p.y <= c.groundY) { p.y = c.groundY; if (v.y < 0) v.y = 0; grounded = true; }
    }
    c.setOnGround(grounded);
    c.setPosition(p);

    // Facing: the robot faces the aim (camera) yaw every frame.
    if (c.isTransforming()) {
        // Vehicle->robot fold: turn from the vehicle heading to the aim along the fold instead of
        // snapping the still-visible vehicle mesh [PROV: turn timing not recovered].
        float p = c.transformProgress();
        float s = p * p * (3.0f - 2.0f * p);
        float d = std::remainder(in.faceYaw - c.transformStartYaw(), 6.2831853f);
        c.setYaw(c.transformStartYaw() + d * s);
    } else {
        c.setYaw(in.faceYaw);
    }
}

} // namespace game::CharacterMovement
