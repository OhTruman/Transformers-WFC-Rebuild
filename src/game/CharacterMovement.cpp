#include "game/CharacterMovement.h"
#include "game/Character.h"
#include "game/Collision.h"
#include "core/Config.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace game::CharacterMovement {

static constexpr float kStepUpConf = 0.35f; // [CONF] TnRobotForm._MovementCapabilities.MaxStepHeight 35 UU
// WFC_STEPUP=m overrides it for A/B diagnostics only.
static const float kStepUp = std::getenv("WFC_STEPUP") ? (float)std::atof(std::getenv("WFC_STEPUP")) : kStepUpConf;
static constexpr float kSnapDown = 1.0f;    // follow downward slopes/stairs while grounded
// Rigid-body hull extents live in VehicleParams (truck: VH_Optimus_PHYSSYS convex box, CONF).

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
    const RobotParams& RP = c.robotParams();
    const float maxSpeed = (falling ? RP.airSpeed : RP.groundSpeed) * mult;
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
            preservation = airSet ? RP.momAirFwd : RP.momGroundFwd;
        else if (wishLen < 1e-4f)
            preservation = airSet ? RP.momAirNeutral : RP.momGroundNeutral;
        else
            preservation = airSet ? RP.momAirBack : RP.momGroundBack;
    }
    float maxAccel = RP.accel / (1.0f + preservation);
    // [PROV] AirControl: UE3 native falling scales acceleration; applied to the accel limit here.
    if (falling) maxAccel *= RP.airControl;

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
bool wallBlock(const CollisionWorld* col, const core::Vec3& oldPos, core::Vec3& p, core::Vec3& v, float probeR,
               float probeY = core::config::kPawnHalfHeight, float skipNy = 2.0f, core::Vec3* firstHitN = nullptr) {
    if (!col) return false;
    // probeY: probe height above oldPos (robot: capsule centre ~2 m). skipNy: hits on faces with |n.y| > skipNy
    // (floors, ramps) are passed through - the ground / suspension code owns those. firstHitN: the first blocking
    // face normal (oriented against the motion).
    core::Vec3 start{oldPos.x, oldPos.y + probeY, oldPos.z};
    core::Vec3 move{p.x - oldPos.x, 0.0f, p.z - oldPos.z};
    const core::Vec3 move0 = move;
    bool blocked = false;
    for (int iter = 0; iter < 3; ++iter) {
        float dist = core::length(move);
        if (dist < 1e-4f) break;
        core::Vec3 dn = move * (1.0f / dist);
        float tHit; core::Vec3 n;
        bool hit = col->segmentHit(start, start + dn * (dist + probeR), tHit, n);
        for (int skip = 0; hit && std::fabs(n.y) > skipNy && skip < 4; ++skip) {
            float len = dist + probeR, from = tHit * len + 0.02f;
            float t2; core::Vec3 n2;
            if (from >= len || !col->segmentHit(start + dn * from, start + dn * len, t2, n2)) { hit = false; break; }
            tHit = (from + t2 * (len - from)) / len; n = n2;
        }
        if (!hit) { start = start + move; move = {0, 0, 0}; break; }
        if (!blocked && firstHitN) *firstHitN = core::dot(n, dn) > 0.0f ? n * -1.0f : n;
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
// Jet vehicle form: TnPlaneForm Hovering (TnHoverPlaneSimulation) / Flying (TnPlaneSimulation) [CONF RE TARGETED_PASS3 C3,
// script bytecode]. Both cancel gravity. Strafe = velocity servo a = ClampLength((dir*MaxSpeed - v)/dt, MaxAccel);
// Turn = angular servo toward the target rotation at TurnRate (roll 0.1, pitch 0.5, yaw 0.5).
void jetStep(Character& c, const MoveIntent& in, float dt, const CollisionWorld* col) {
    namespace cfg = core::config;
    Character::VehicleState& vs = c.vehicleState();
    const VehicleParams& VP = c.vehicleParams();
    core::Vec3& v = c.velocity();
    const core::Vec3 oldPos = c.position();
    core::Vec3 p = oldPos;
    vs.dashCooldown = std::max(0.0f, vs.dashCooldown - dt);
    // Hovering -> Flying: boost held and not drifting. Flying -> Hovering: boost released (Hovering.BeginState: Drift).
    if (!vs.flying && in.wantBoost && vs.driftRemain <= 0.0f) { vs.flying = true; vs.rollRemain = 0.0f; }
    else if (vs.flying && !in.wantBoost) { vs.flying = false; vs.driftRemain = VP.driftDuration; vs.rollRemain = 0.0f; }
    vs.driving = vs.flying;                       // the boost state consumers (FX / HUD) read Driving
    vs.driftRemain = std::max(0.0f, vs.driftRemain - dt);
    auto sgnsq = [](float x) { return x < 0.0f ? -x * x : x * x; };
    // View frame (controller rotation = camera).
    core::Vec3 Fv = core::forwardFromYawPitch(in.faceYaw, in.viewPitch);
    core::Vec3 Fy = core::forwardFromYawPitch(in.faceYaw, 0.0f);
    core::Vec3 Ry = core::normalize(core::cross(Fy, core::Vec3{0, 1, 0}));
    core::Vec3 Uv = core::normalize(core::cross(Ry, Fv));
    core::Vec3 accel{0, 0, 0};                    // gravity cancelled in both modes
    float tgtYaw = in.faceYaw, tgtPitch = in.viewPitch, tgtRoll = 0.0f;
    // Special move (Shift): roll. Hover: needs |stick X| >= 0.5, 0.6 s, cooldown 1.2 s; flight: 0.8 s.
    if (in.wantDash && vs.dashCooldown <= 0.0f && vs.rollRemain <= 0.0f && (vs.flying || std::fabs(in.moveRight) >= 0.5f)) {
        vs.rollDir = in.moveRight >= 0.0f ? 1.0f : -1.0f;
        vs.rollRemain = vs.flying ? VP.flyRollTime : VP.hoverRollTime;
        vs.dashCooldown = 1.2f;
    }
    vs.rollRemain = std::max(0.0f, vs.rollRemain - dt);
    const bool rolling = vs.rollRemain > 0.0f;
    if (!vs.flying) {
        if (rolling) {
            // UpdateRoll: strafe sideways at RollLinearSpeed (accel 10000), barrel roll 2pi/RollDuration about local X.
            core::Vec3 des = Ry * (vs.rollDir * VP.hoverRollSpeed);
            core::Vec3 dv = des - core::Vec3{v.x, 0.0f, v.z} * 1.0f; dv.y = -v.y;
            accel = accel + clampLength(dv * (1.0f / dt), 100.0f);
            vs.angVel.x = vs.rollDir * 6.2831853f / VP.hoverRollTime;
        } else {
            // UpdateStrafe: maxA = 2500 x max(driftScale, max(|fwd|,|right|)), mask (1,1,1) in the full VIEW frame.
            float drift = 1.0f - vs.driftRemain / VP.driftDuration;
            float fi = core::clampf(in.moveForward, -1.0f, 1.0f), ri = core::clampf(in.moveRight, -1.0f, 1.0f);
            float maxA = VP.hoverAccel * std::max(drift * drift, std::max(std::fabs(fi), std::fabs(ri)));
            core::Vec3 des = Fv * (fi * VP.hoverSpeed) + Ry * (ri * VP.hoverSpeed);
            core::Vec3 a = clampLength((des - v) * (1.0f / dt), maxA);
            // Ascend / Descend held -> Dash(+-Z) each frame: vertical servo at DashSpeed 1000, accel 10000, mask Z.
            if (in.ascend != in.descend) {
                float vz = (in.ascend ? 1.0f : -1.0f) * VP.dashSpeed;
                a.y = core::clampf((vz - v.y) / dt, -100.0f, 100.0f);
            }
            accel = accel + a;
        }
    } else {
        // TnPlaneSimulation.UpdateFly: Strafe((1,0,0), mask (1,0,0), view frame, MaxAccel, MaxSpeed) - always thrusting.
        float vf = core::dot(v, Fv);
        float af = core::clampf((VP.flySpeed - vf) / dt, -VP.flyAccel, VP.flyAccel);
        // ApplyDrag on the local (0, y, z) velocity: solve A s^2 + s - |v_perp| = 0, A = dt * DragCoefficient / Mass, and
        // set |v_perp| to s [CONF formula]. Mass: the plane rigid body's mass is not recovered -> 100 [PROV].
        core::Vec3 vperp = v - Fv * vf;
        float vp = core::length(vperp) * 100.0f;               // UU/s
        float A = dt * VP.flyDrag / 100.0f;
        float sNew = vp > 1e-3f ? (-1.0f + std::sqrt(1.0f + 4.0f * A * vp)) / (2.0f * A) : 0.0f;
        if (vp > 1e-3f) v = v - vperp * (1.0f - sNew / vp);
        if (rolling) {
            v = v + Ry * ((vs.rollDir * VP.flyRollSpeed - core::dot(v, Ry)) * std::min(1.0f, 10.0f * dt));
            vs.angVel.x = vs.rollDir * VP.flyRollAngSpeed;
        }
        accel = accel + Fv * af;
        // Target = view (+) RLerp(prev, (-pitch^2*27, yaw^2*16, yaw^2*77) deg, 0.1); the pitch term fades from 45 to 0 at 60.
        const float d2r = 0.0174533f;
        float lp = -sgnsq(in.lookUpIn) * VP.pitchDuePitch * d2r, ly = sgnsq(in.turnIn) * VP.yawDueYaw * d2r,
              lr = sgnsq(in.turnIn) * VP.rollDueYaw * d2r;
        float bodyPitchDeg = std::fabs(vs.pitch) / d2r;
        float fade = core::clampf((VP.maxPitchDeg - bodyPitchDeg) / (VP.maxPitchDeg - VP.fullPitchDeg), 0.0f, 1.0f);
        lp *= fade;
        vs.leanP += (lp - vs.leanP) * VP.extraRotLerp;
        vs.leanY += (ly - vs.leanY) * VP.extraRotLerp;
        vs.leanR += (lr - vs.leanR) * VP.extraRotLerp;
        tgtYaw -= vs.leanY; tgtPitch += vs.leanP; tgtRoll = vs.leanR;
    }
    // Turn: angular servo toward the target rotation; TurnRate (0.1, 0.5, 0.5) per tick [CONF]; AngularDamping 6 / 8 [PROV form].
    float dyaw = std::remainder(tgtYaw - c.yaw(), 6.2831853f);
    c.setYaw(c.yaw() + dyaw * (1.0f - std::pow(1.0f - 0.5f, dt * 60.0f)));
    vs.yawRate = -dyaw / std::max(dt, 1e-4f) * 0.5f;
    vs.pitch += (tgtPitch - vs.pitch) * (1.0f - std::pow(1.0f - 0.5f, dt * 60.0f));
    if (rolling) vs.roll = std::remainder(vs.roll - vs.angVel.x * dt, 6.2831853f);
    else vs.roll += (std::remainder(tgtRoll, 6.2831853f) - std::remainder(vs.roll, 6.2831853f)) * (1.0f - std::pow(1.0f - 0.1f, dt * 60.0f));
    vs.pitch = core::clampf(vs.pitch, -1.4f, 1.4f);
    // Integrate.
    v = v + accel * dt;
    p = p + v * dt;
    // Collision: hull probes against walls; floor / ceiling keep the hull out of the geometry. A flying hit faster than
    // ReturnToHoverCrashSpeed 3000 returns to hover [CONF].
    core::Vec3 moveDir = core::normalize(core::Vec3{p.x - oldPos.x, 0.0f, p.z - oldPos.z});
    core::Vec3 hf = core::forwardFromYawPitch(c.yaw(), 0.0f);
    core::Vec3 hr = core::normalize(core::cross(hf, core::Vec3{0, 1, 0}));
    float along = core::dot(moveDir, hf);
    float reach = std::fabs(along) * (along >= 0.0f ? VP.hullFront : VP.hullBack) + std::fabs(core::dot(moveDir, hr)) * VP.hullHalfWidth;
    float speedBefore = core::length(v);
    bool hit = false;
    for (float h : {VP.hullBottom + 0.15f, 0.5f * (VP.hullBottom + VP.hullTop), VP.hullTop - 0.1f})
        hit |= wallBlock(col, oldPos, p, v, std::max(reach, VP.hullHalfWidth), h, 0.5f);
    if (col) {
        float gy; core::Vec3 n;
        if (col->groundHeight(p.x, p.z, std::max(oldPos.y, p.y) - VP.hullBottom, 0.6f, gy, n) && p.y + VP.hullBottom < gy) {
            p.y = gy - VP.hullBottom; if (v.y < 0.0f) { hit |= v.y < -1.0f; v.y = 0.0f; }
        }
        float t; core::Vec3 n2;
        core::Vec3 a{p.x, oldPos.y + 0.5f * (VP.hullBottom + VP.hullTop), p.z}, b{p.x, p.y + VP.hullTop, p.z};
        if (v.y > 0.0f && col->segmentHit(a, b, t, n2) && std::fabs(n2.y) > 0.7f) { p.y = std::min(p.y, a.y + (b.y - a.y) * t - VP.hullTop); v.y = 0.0f; hit = true; }
        float gy2; core::Vec3 n3;
        vs.onTheGround = col->groundHeight(p.x, p.z, p.y, 0.0f, gy2, n3) && p.y + VP.hullBottom - gy2 < 0.3f;
    } else if (p.y + VP.hullBottom < c.groundY) { p.y = c.groundY - VP.hullBottom; v.y = std::max(v.y, 0.0f); vs.onTheGround = true; }
    static const bool jdbg = std::getenv("WFC_JETDBG") != nullptr;
    if (jdbg) { static int n = 0; if (++n % 20 == 0) LOG_INFO("JETDBG p (%.2f %.2f %.2f) v (%.2f %.2f %.2f) hit %d flying %d accel (%.1f %.1f %.1f) fwd %.2f", p.x, p.y, p.z, v.x, v.y, v.z, (int)hit, (int)vs.flying, accel.x, accel.y, accel.z, in.moveForward); }
    if (hit && vs.flying && speedBefore > 30.0f) { vs.flying = false; vs.driving = false; vs.driftRemain = VP.driftDuration; }
    vs.contacts = vs.onTheGround ? 4 : 0;
    c.setOnGround(vs.onTheGround);
    c.setPosition(p);
}

void vehicleStep(Character& c, const MoveIntent& in, float dt, const CollisionWorld* col) {
    namespace cfg = core::config;
    Character::VehicleState& vs = c.vehicleState();
    const VehicleParams& VP = c.vehicleParams();
    if (VP.form == VehicleFormType::Jet) { jetStep(c, in, dt, col); return; }
    const bool truck = VP.form == VehicleFormType::Truck, tank = VP.form == VehicleFormType::Tank;
    core::Vec3& v = c.velocity();
    const core::Vec3 oldPos = c.position();
    core::Vec3 p = oldPos;
    const float gRB = cfg::kVehicleGravity;
    vs.dashCooldown = std::max(0.0f, vs.dashCooldown - dt);
    vs.nitroCooldown = std::max(0.0f, vs.nitroCooldown - dt);

    // Hovering.UpdateBoosting: Boost && (!IsDrifting) -> Driving (CarSimulation.Activate: JumpTimeRemaining 0).
    // Driving.UpdateBoosting: !Boost -> Hovering (EndState StopNitro; Hovering.BeginState: hover Activate
    // clears dash/drift and resets the springs, then Drift() starts the 0.5 s authority ramp).
    // Tank (TnTankForm.UpdateBoosting): StartBoost while not drifting; StopBoost + Drift on release; the hover
    // simulation keeps running (TnHoverTankSimulation boost: native not recovered -> speed cap MaxBoostSpeed [PROV]).
    if (tank) {
        if (!vs.tankBoost && in.wantBoost && vs.driftRemain <= 0.0f) vs.tankBoost = true;
        else if (vs.tankBoost && !in.wantBoost) { vs.tankBoost = false; vs.driftRemain = VP.driftDuration; }
    } else if (VP.hasDriving() && !vs.driving && in.wantBoost && vs.driftRemain <= 0.0f) {
        vs.driving = true; vs.jumpBoost = 0.0f;
    } else if (vs.driving && !in.wantBoost) {
        vs.driving = false;
        vs.nitroRemain = 0.0f; vs.dashRemain = 0.0f;
        vs.driftRemain = VP.driftDuration;
        for (float& l : vs.spLen) l = -1.0f;              // TnSpring.Reset: _Length = -1
    }

    BodyAxes B = bodyAxes(c.yaw(), vs.pitch, vs.roll);
    const float stabRad = cfg::kHoverStabilityDeg * 0.0174533f;
    bool unstable = std::fabs(vs.pitch) > stabRad || std::fabs(vs.roll) > stabRad;

    // Special move (VehicleSpecialMove = Dash input): CanUseSpecialMove = no cooldown running.
    if (in.wantDash && !tank) {   // tank special move = 180 quick turn (native not recovered: PARTIAL, not implemented)
        if (!vs.driving) {
            if (vs.dashCooldown <= 0.0f) {
                // Truck: TnTruckForm.Hovering.DoDash -> Dash((1,0,0)), forward only. Car: TnCarForm.Hovering.DoDash ->
                // Dash(|StrafeRL| > |StrafeFB| ? (0, Sign(RL), 0) : (Sign(FB), 0, 0)). Dash() is refused while unstable;
                // the cooldown (TimeBetweenDashes 2.0) starts either way (UpdateDashing).
                core::Vec3 dir{1, 0, 0};
                if (!truck) {
                    float fb = in.moveForward, rl = in.moveRight;
                    auto sgn = [](float x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); };
                    dir = std::fabs(rl) > std::fabs(fb) ? core::Vec3{0, 0, sgn(rl)} : core::Vec3{sgn(fb), 0, 0};
                }
                if (!unstable && (dir.x != 0.0f || dir.z != 0.0f)) { vs.dashRemain = VP.dashTime; vs.dashDir = dir; }
                vs.dashCooldown = cfg::kHoverDashCooldown;
            }
        } else if (!truck) {
            // Car: TnCarForm.Driving.UpdateRolling -> TnCarSimulation.Roll() when CanRoll (RollDuration > 0) and the special
            // move is off cooldown; cooldown TimeBetweenRolls 2.0 [CONF RE C4]. Roll(): dir = Sign(RollControl = stick X);
            // v += yawFrame(0, dir*1200, 1000 - vz); JumpTimeRemaining = RollDuration; local angular X += -dir*100.
            if (vs.dashCooldown <= 0.0f && VP.rollDuration > 0.0f) {
                float dir = in.moveRight > 0.1f ? 1.0f : (in.moveRight < -0.1f ? -1.0f : 0.0f);
                core::Vec3 Fy = core::forwardFromYawPitch(c.yaw(), 0.0f);
                core::Vec3 Ry = core::normalize(core::cross(Fy, core::Vec3{0, 1, 0}));
                v = v + Ry * (dir * 12.0f);
                v.y += 10.0f - v.y;
                vs.rollRemain = VP.rollDuration; vs.rollDir = dir;
                // The rigid body's max angular velocity clamps the -dir*100 impulse; one turn over RollDuration is used
                // [PROV: PhysX MaxAngularVelocity of the car body not recovered].
                vs.angVel.x = dir * 6.2831853f / VP.rollDuration;
                vs.onTheGround = false;
                vs.dashCooldown = 2.0f;
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
            v.y += VP.jumpSpeed;                           // ApplyLinearVelocity(0,0,JumpLinearSpeed 1200)
            vs.angVel.y -= VP.jumpAngSpeed;                // ApplyLocalAngularVelocity(0,-1,0): nose up
            vs.jumpWait = tank ? 0.5f : cfg::kVehJumpInterval;   // TnTankForm.get_TimeBetweenJumps 0.5 / TnCarForm 0.3
        }
    } else if (vs.onTheGround) {
        // Driving.UpdateJumping -> TnCarSimulation.Jump: local (600,0,1400), angular (0,-2,0).
        vs.jumpWait = std::max(0.0f, vs.jumpWait - dt);
        if (vs.jumpWait == 0.0f && in.wantJump) {
            vs.jumpBoost = cfg::kDriveJumpBoostTime;
            v = v + B.x * VP.driveJumpFwd + B.z * VP.driveJumpUp;
            vs.angVel.y -= VP.driveJumpAngVel;
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
        const float ms = VP.mass * 0.25f;
        const float K = VP.suspStiffness / ms, Bd = VP.suspDamping / ms, rest = VP.suspRest;
        core::Vec3 com = p + B.x * VP.comFwd + B.z * VP.comUp;
        core::Vec3 down = B.z * -1.0f;
        float gs = B.z.y * -gRB;   // (-Direction.Z) * GetGravityZ(): PHYS_RigidBody -> -2940 x 0.66 [CONF native M03 P1]
        core::Vec3 nsum{0, 0, 0};
        int contacts = 0;
        for (int i = 0; i < 4; ++i) {
            float ang = 0.7853982f + 1.5707963f * (float)i;   // Normal(1,1,0) rotated by 90 deg steps
            float rx = std::cos(ang) * VP.suspMountRadius, ry = std::sin(ang) * VP.suspMountRadius;
            core::Vec3 mount = com + B.x * rx + B.y * ry;
            // [PROV] The PhysX hull keeps the body out of walls, so a mount never sits inside one. Our hull probes can let a
            // corner clip a wall while sliding along it at an angle; a mount inside the wall would cast its ray from inside the
            // solid, miss the floor, and drop that corner (the car tipped onto the wall). Start such a probe just short of the
            // face instead, on the COM side, at the same height.
            if (col) {
                float tw; core::Vec3 nw;
                if (col->segmentHit(com, mount, tw, nw) && std::fabs(nw.y) < 0.5f) mount = com + (mount - com) * std::max(0.0f, tw - 0.02f);
            }
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
            accel = accel + B.z * (Fa / VP.mass);
            angAx += ry * Fa / VP.inertiaX;                   // body torque r x F (F along body z)
            angAy += -rx * Fa / VP.inertiaY;
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
        if (tank) {
            // TnHoverTankSimulation.UpdateStrafe [CONF RE C2]: Drift -= dt; nothing while unstable (|pitch| or |roll| >= 30);
            // maxA = 2500 x (1 - Drift/DriftDuration)^2, cap Boosting ? MaxBoostSpeed : MaxLinearSpeed, input forced to
            // (1,0,0) while boosting; Strafe mask (1,1,0) in the camera-yaw frame F.
            vs.driftRemain = std::max(0.0f, vs.driftRemain - dt);
            if (!unstable) {
                core::Vec3 Fy = core::forwardFromYawPitch(in.faceYaw, 0.0f);
                core::Vec3 Ry = core::normalize(core::cross(Fy, core::Vec3{0, 1, 0}));
                float drift = 1.0f - vs.driftRemain / VP.driftDuration;
                float maxA = VP.hoverAccel * drift * drift;
                float cap = vs.tankBoost ? VP.maxBoostSpeed : VP.hoverSpeed;
                float fi = vs.tankBoost ? 1.0f : core::clampf(in.moveForward, -1.0f, 1.0f);
                float ri = vs.tankBoost ? 0.0f : core::clampf(in.moveRight, -1.0f, 1.0f);
                core::Vec3 desW = Fy * (fi * cap) + Ry * (ri * cap);
                core::Vec3 dv{desW.x - v.x, 0.0f, desW.z - v.z};
                core::Vec3 a = clampLength(dv * (1.0f / dt), maxA);
                accel = accel + a;
            }
            la = core::Vec3{0, 0, 0};
        } else if (vs.dashRemain > 0.0f) {
            vs.dashRemain = unstable ? 0.0f : std::max(0.0f, vs.dashRemain - dt);
            float maxS = vs.dashRemain > 0.0f ? VP.dashSpeed : VP.hoverSpeed;
            core::Vec3 des{vs.dashDir.x * maxS, vs.dashDir.z * maxS, 0.0f};
            la = clampLength((des - lv) * (1.0f / dt), cfg::kHoverDashAccel);     // AxisMask (1,1,1)
        } else {
            vs.driftRemain = std::max(0.0f, vs.driftRemain - dt);
            float stab = B.z.y * B.z.y;                                         // Square(Abs(up.Z))
            float drift = 1.0f - vs.driftRemain / VP.driftDuration;            // CalculateDriftScale
            float maxA = VP.hoverAccel * drift * drift * stab;
            // Tank boost: forward at MaxBoostSpeed [PROV: TnHoverTankSimulation boost native not recovered].
            const bool tb = tank && vs.tankBoost;
            const float hs = tb ? VP.maxBoostSpeed : VP.hoverSpeed;
            core::Vec3 des{core::clampf(tb ? 1.0f : in.moveForward, -1.0f, 1.0f) * hs,
                           core::clampf(in.moveRight, -1.0f, 1.0f) * hs, 0.0f};
            la = clampLength(core::Vec3{des.x - lv.x, des.y - lv.y, 0.0f} * (1.0f / dt), maxA);   // AxisMask (1,1,0)
        }
        if (!tank) accel = accel + B.x * la.x + B.y * la.y + B.z * la.z;

        // ---- UpdateRoll: ApplyLocalAngularAcceleration(RightLeftInput - LocalAngularVelocity.Z, 0, 0) ----
        angAx += core::clampf(in.moveRight, -1.0f, 1.0f) - vs.angVel.z;

        // ---- UpdateTurn: yaw matches the view yaw each tick; pitch/roll are corrected only when there is
        // no contact or the body is upside down (ShouldUpright), by 5% of the error per tick. ----
        float dyaw = std::remainder(in.faceYaw - c.yaw(), 6.2831853f);
        vs.angVel.z = -dyaw / dt;                      // UE yaw rate (+ = turning right)
        vs.yawRate = vs.angVel.z;
        c.setYaw(in.faceYaw);
        // Tank UpdateTurn [CONF RE C2]: if IsStable && OnTheGround only yaw is applied, else pitch / roll are also
        // corrected at TurnRate 0.05. Car / truck: only with no contact or upside down (ShouldUpright).
        // TnHoverCarSimulation.UpdateTurn [CONF RE pass 4 A4, corrected 6bb8855]: dw = axisAngle(current -> upright with the
        // view yaw) x (0.05, 0.05, 1) / dt - w, then x (ShouldUpright ? (1, 1, 1) : (0, 0, 1)). ShouldUpright = no suspension
        // contact or upside down (up.Z < 0.01). Grounded and upright: only yaw is replaced; pitch / roll w carries over between
        // steps (spring torques, UpdateRoll; RB angular damping 0 while hovering). Airborne / inverted: pitch / roll w is SET to
        // 0.05 x error / dt (a 5 % per tick pull). The correction lands at step start, the spring forces integrate over the step.
        // Tank (TnHoverTankSimulation [CONF RE C2]): only yaw while stable on the ground, else pitch / roll at TurnRate 0.05.
        if (tank) {
            if (!(!unstable && vs.onTheGround)) { vs.angVel.x = 0.05f * vs.roll / dt; vs.angVel.y = 0.05f * vs.pitch / dt; }
        } else {
            if (contacts == 0 || B.z.y < 0.01f) {          // ShouldUpright
                vs.angVel.x = 0.05f * vs.roll / dt;
                vs.angVel.y = 0.05f * vs.pitch / dt;
            }
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
        // RollControl = StrafeRightLeft (left-stick X / A-D): the truck cannot barrel roll (RollDuration 0); it only
        // drives UpdateLeveling: when |RollControl| > 0.1 and (sign differs from the roll or |roll| < 45 deg),
        // roll acceleration sign(Roll) * RollCorrectionAngularAcceleration 50 * 0.5 * (1 - Up.Z) [CONF].
        vs.rollControl = core::clampf(in.moveRight, -1.0f, 1.0f);
        if (std::fabs(vs.rollControl) > 0.1f && ((vs.rollControl > 0.0f) != (vs.roll > 0.0f) || std::fabs(vs.roll) < 0.7853982f))
            angAx += (vs.roll > 0.0f ? 1.0f : -1.0f) * 50.0f * 0.5f * (1.0f - B.z.y);
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
        float damp = VP.angularDamping * (wheels ? (1.0f - std::fabs(vs.steer)) * (1.0f - std::fabs(vs.steer)) : 1.0f);
        vs.angVel = vs.angVel * std::max(0.0f, 1.0f - damp * dt);   // PhysX damping form [UNKNOWN: assumed]
        // Rolling: angular accel X -dir*100 every tick, held at the body's max rate (one turn per RollDuration [PROV rate]).
        if (vs.rollRemain > 0.0f && std::fabs(vs.rollDir) == 1.0f) vs.angVel.x = vs.rollDir * 6.2831853f / VP.rollDuration;
        core::Vec3 F = core::forwardFromYawPitch(c.yaw(), 0.0f);
        core::Vec3 R = core::normalize(core::cross(F, core::Vec3{0, 1, 0}));
        vs.rollRemain = std::max(0.0f, vs.rollRemain - dt);
        if (vs.rollRemain > 0.0f && std::fabs(vs.rollDir) == 1.0f) {
            // TnCarSimulation.UpdateRolling: lateral accel yawFrame(0, dir*1000, 0); angular accel X -dir*100 (held at the
            // body's max rate) - replaces air control and leveling while JumpTimeRemaining > 0 [CONF RE C4].
            accel = accel + R * (vs.rollDir * 10.0f);
        } else if (!wheels) {
            // UpdateAirControl: strafe + turn by steering; pitch the nose down to PitchForwardLimit.
            accel = accel + R * (vs.steer * VP.airStrafeAccel);
            vs.angVel.z += vs.steer * VP.airTurnAccel * dt;
            if (vs.pitch > cfg::kDrivePitchFwdLimit) angAy += cfg::kDrivePitchFwdAccel;
        } else {
            // UpdateDrag: -Normal(v) * v^2 |g_RB| / TerminalVelocity^2.
            float sp = core::length(v);
            if (sp > 1e-4f) accel = accel - v * (1.0f / sp) * (sp * sp * gRB / (cfg::kPawnTerminalVel * cfg::kPawnTerminalVel));
            // UpdateBoost: Lerp(MaxAcceleration, Drag(MaxSpeed), fwd/MaxSpeed) + ExtraBoost, x BoostScale x 1.
            float maxS = VP.driveSpeed * (nitro ? cfg::kNitroSpeedScale : 1.0f);
            float fwd = core::dot(v, B.x);
            float dragMax = maxS * maxS * gRB / (cfg::kPawnTerminalVel * cfg::kPawnTerminalVel);
            float a0 = VP.driveAccel * cfg::kDriveLowSpeedBoostScale, z = maxS * cfg::kDriveLowSpeedBoostThreshold;
            float cs = core::clampf(fwd, 0.0f, z);
            float extra = std::min(a0 / (z * z) * cs * cs - 2.0f * a0 / z * cs + a0, cfg::kDriveMaxExtraAccel);
            float boostScale = 1.0f - (core::clampf(B.x.y, 0.5f, 0.866f) - 0.5f) / (0.866f - 0.5f);
            float acc = VP.driveAccel + (dragMax - VP.driveAccel) * (fwd / maxS) + extra;
            accel = accel + B.x * (acc * boostScale);
            // TnWheelAssembly / TnTire (RE MILESTONE03_VEHICLE_BOOST_STEERING) [CONF laws + constants]: every wheel
            // gets the same Steering; front MaxSteeringAngle 25 deg, rear 0. v_w = point velocity of the wheel in
            // the steered wheel frame; F = clamp(-v_w.lateral * TireFrictionCoefficient 0.0015 * Load,
            // +-2 (M/4)|g|), applied along BODY +Y (ApplyLocalForce((0,F,0)) in body space, reproduced literally)
            // at the contact point. Load = static (M/4)|g| [HIGH; no load transfer]. Heading comes only from the
            // resulting yaw torque (inertia 58.9e6 kg UU^2); nothing sets the yaw rate.
            // Wheels: TnWheelPhysicsBlueprint LocalPosition relative to the COM. Truck: axles +-130 UU (a = b), track
            // +-126 front / +-137 rear; car: +83 / -93, track +-75. TireFrictionCoefficient per wheel (truck 0.0015,
            // car 0.0017 front / 0.002 rear) [CONF authored].
            static const std::vector<WheelDef> kTruckWheels = {{83, -126, 21, 45, 25, 0.0015f, 0.8f}, {83, 126, 21, 45, 25, 0.0015f, 0.8f},
                                                               {-177, -137, 21, 45, 0, 0.0015f, 0.5f}, {-177, 137, 21, 45, 0, 0.0015f, 0.5f}};
            const std::vector<WheelDef>& wheelsN = VP.wheels.empty() ? kTruckWheels : VP.wheels;
            const float load = VP.mass * 0.25f * gRB;                // N (kg m/s^2), static per-wheel load
            const float fMax = 2.0f * load;
            float vx = core::dot(v, F), vy = core::dot(v, R);
            float wz = vs.angVel.z;                                   // UE yaw rate (+ = right)
            float fSum = 0.0f, tz = 0.0f;
            for (const WheelDef& w : wheelsN) {
                const float coef = w.friction * 100.0f;               // per UU/s -> per m/s (F in N)
                float rx = w.x * 0.01f - VP.comFwd, ry = w.y * 0.01f;
                float px = vx - wz * ry, py = vy + wz * rx;            // v + w x r (planar, body frame)
                float d = vs.steer * w.maxSteerDeg * 0.0174533f;      // steering angle (+ = right)
                float vlat = -std::sin(d) * px + std::cos(d) * py;     // wheel-frame lateral velocity
                float f = core::clampf(-vlat * coef * load, -fMax, fMax);
                fSum += f;
                tz += rx * f;                                          // r x (0, F, 0): yaw torque = r.x * F
            }
            const float izz = VP.inertiaZ;                            // kg m^2 (truck 58.9e6 kg UU^2)
            v = v + R * (fSum / VP.mass * dt);
            vs.angVel.z += tz / izz * dt;
            vs.tireForce = fSum;
            if (vs.rollRemain <= 0.0f) vs.angVel.x = 0.0f;
            if (vs.angVel.y > 0.0f) vs.angVel.y = 0.0f;       // keep a jump's nose-up rate, else level
            // [PROV] settle on the wheels, onto the support slope under the truck (single support normal at the root):
            // the body pitches with the ground so UpdateBoost's BoostScale (fades to 0 between forward.Z 0.5 and 0.866,
            // CONF) sees real climbs, and gravity acts along the slope (the wheel snap otherwise removes it). The per-wheel
            // TnWheelAssembly suspension (K 120000, D 8000, rest 30 + radius 45 UU) is not modelled: mount heights unknown.
            core::Vec3 gnn = core::normalize(gn); if (gnn.y < 0.0f) gnn = gnn * -1.0f;
            core::Vec3 fp = core::normalize(F - gnn * core::dot(F, gnn)), rp = core::normalize(R - gnn * core::dot(R, gnn));
            float slopePitch = std::asin(core::clampf(fp.y, -1.0f, 1.0f)), slopeRoll = std::asin(core::clampf(-rp.y, -1.0f, 1.0f));
            float k = 1.0f - std::exp(-10.0f * dt);
            vs.pitch += (slopePitch - vs.pitch) * k; vs.roll += (slopeRoll - vs.roll) * k;
            v = v + F * (-gRB * std::sin(slopePitch) * std::cos(slopePitch) * dt);   // gravity along the slope (horizontal part)
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
    if (vs.rollRemain > 0.0f && vs.driving) vs.roll = std::remainder(vs.roll, 6.2831853f);   // barrel roll: full turn
    else vs.roll = core::clampf(vs.roll, -1.2f, 1.2f);
    vs.pitch = core::clampf(vs.pitch, -1.2f, 1.2f);
    v = v + accel * dt;
    p = p + v * dt;

    core::Vec3 moveDir = core::normalize(core::Vec3{p.x - oldPos.x, 0.0f, p.z - oldPos.z});
    // Wall probe reach = the hull's extent along the travel direction (box support distance from the root).
    core::Vec3 hf = core::forwardFromYawPitch(c.yaw(), 0.0f);
    core::Vec3 hr = core::normalize(core::cross(hf, core::Vec3{0, 1, 0}));
    float along = core::dot(moveDir, hf);
    float hullReach = std::fabs(along) * (along >= 0.0f ? VP.hullFront : VP.hullBack) + std::fabs(core::dot(moveDir, hr)) * VP.hullHalfWidth;
    // Hull against walls: probes across the PhysicalVehicleMesh hull's height (root -0.35 .. +1.85 m) instead of the
    // robot torso height (2 m above the root, above the hull top: the truck drove through anything lower). The low
    // probe sits above the wheel / spring clearance (driving: wheels down, root on the floor; hovering: hull bottom
    // 0.35 m below the root). Faces with |n.y| > 0.5 (slopes under 60 deg) do not stop the hull: a rigid-body box meeting
    // a sloped face is pushed up it, which the chassis-ground / spring code reproduces; near-vertical faces block.
    // (Pass 20 skipped only n.y > 0.7, so 45-60 deg ramp faces stopped the truck dead.) [PROV: probes approximate the
    // RB box contact]
    const float probes[3] = {vs.driving ? 0.45f : VP.hullBottom + 0.15f, 0.5f * (VP.hullBottom + VP.hullTop), VP.hullTop - 0.1f};
    bool hullBlocked = false;
    core::Vec3 hitN{0, 0, 0};
    float hitH = 0.0f;
    for (float h : probes) {
        core::Vec3 n{0, 0, 0};
        if (wallBlock(col, oldPos, p, v, std::max(hullReach, VP.hullHalfWidth), h, 0.5f, &n)) { if (!hullBlocked) { hitN = n; hitH = h; } hullBlocked = true; }
    }
    if (hullBlocked && vs.driving) {
        // Driving.OnRigidBodyCollision: a frontal hit drops back to Hovering when the contact normal . forward >
        // CosCollisionNormalThreshold 0.866 [CONF] (ram consumption during nitro not implemented). The normal is the
        // blocking face's (into the obstacle): sliding along a wall no longer counts as frontal.
        core::Vec3 fwdFlat = core::forwardFromYawPitch(c.yaw(), 0.0f);
        core::Vec3 into{-hitN.x, 0.0f, -hitN.z};
        float il = core::length(into);
        if (il > 1e-4f && core::dot(into * (1.0f / il), fwdFlat) > 0.866f) {
            static const bool dropLog = std::getenv("WFC_VEHDROPLOG") != nullptr;
            if (dropLog) LOG_INFO("VEHDROP frontal hull block -> Hovering: probe %.2f m, normal (%.2f %.2f %.2f), speed %.1f, at (%.2f %.2f %.2f)", hitH, hitN.x, hitN.y, hitN.z, core::length(core::Vec3{v.x, 0, v.z}), p.x, p.y, p.z);
            vs.driving = false;
            vs.nitroRemain = 0.0f; vs.dashRemain = 0.0f; vs.rollRemain = 0.0f;
            vs.driftRemain = VP.driftDuration;
            for (float& l : vs.spLen) l = -1.0f;
        }
    }

    // [PROV] Chassis top against overhead geometry: the PhysicalVehicleMesh hull blocks upward motion
    // through awnings/decks (otherwise a suspension ray starting just above a thin surface reports a
    // near-zero length and the spring damper term spikes).
    if (col && v.y > 0.0f) {
        float top = VP.hullTop;                   // hull top above the root
        // Start at least 0.1 m above the root: in Driving the root rests ON the floor, and a probe starting at floor level
        // read the floor itself as a ceiling - it zeroed every boost jump in its first step (Pass 23 playtest: car / truck).
        core::Vec3 a{p.x, oldPos.y + std::max(VP.comUp, 0.1f), p.z};
        core::Vec3 b = core::Vec3{p.x, p.y + top, p.z};
        float t; core::Vec3 n;
        if (top > std::max(VP.comUp, 0.1f) && col->segmentHit(a, b, t, n) && std::fabs(n.y) > 0.7f) {   // ceilings only
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
            core::Vec3 com = p + B.x * VP.comFwd + B.z * VP.comUp;
            if (col->groundHeight(com.x, com.z, std::max(oldPos.y, p.y) + VP.comUp, 0.6f, gy, n) &&
                com.y - gy < VP.comUp - VP.hullBottom) {              // truck hull bottom 0.35 m below the root
                p.y += (VP.comUp - VP.hullBottom) - (com.y - gy);
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
    const RobotParams& RPd = c.robotParams();
    // TnAcrobaticsManager.DodgeTowards: refused unless CanDodge (landed since the last dodge, not rammed). SetupDodge:
    // PHYS_Flying, MaxAirSpeed = DodgeSpeed, Velocity = dir * DodgeSpeed (+ base velocity), DodgeTime [CONF script].
    if (in.dodgeDir != 0 && c.landedSinceDodge_ && c.rammedRemain_ <= 0.0f && !c.isTransforming() && c.dodgeRemain_ <= 0.0f) {
        core::Vec3 X = core::forwardFromYawPitch(c.yaw(), 0.0f);
        core::Vec3 Y = core::normalize(core::cross(X, core::Vec3{0, 1, 0}));
        core::Vec3 d = in.dodgeDir == 3 ? X : in.dodgeDir == 4 ? X * -1.0f : in.dodgeDir == 2 ? Y : Y * -1.0f;
        v = d * RPd.dodgeSpeed;
        c.dodgeRemain_ = RPd.dodgeTime;
        c.landedSinceDodge_ = false;
        c.setOnGround(false);
        hv = core::Vec3{v.x, 0, v.z};
    }
    // Hover: JumpingToHover (Falling at the HoverJumpHeight jump velocity) -> Hovering when descending or on the ground
    // (PHYS_Flying, MaxAirSpeed HoverAirSpeed, HoverDuration) -> Falling on expiry or on jump [CONF script].
    if (in.hoverRequest && c.hoverState_ == 0 && !c.isTransforming()) {
        v.y = std::sqrt(2.0f * core::config::kGravity * RPd.hoverJumpHeight);
        c.hoverState_ = 1; c.setOnGround(false);
    }
    c.hoverRequested_ = false;
    if (c.hoverState_ == 1 && (v.y <= 0.0f || c.onGround()) && !in.hoverRequest) { c.hoverState_ = 2; c.hoverRemain_ = RPd.hoverDuration; }
    if (c.hoverState_ == 2) {
        c.hoverRemain_ -= dt;
        if (c.hoverRemain_ <= 0.0f || in.wantJump) c.hoverState_ = 0;
    }
    const bool hovering = c.hoverState_ == 2;
    // Melee AttackDash: yaw-only lunge at Speed 2500 UU/s for Time 0.25 s, then Velocity x EndVelocityScaler 0.3 [CONF].
    if (c.lungeRemain_ > 0.0f) {
        c.lungeRemain_ -= dt;
        v.x = c.lungeDir_.x * 25.0f; v.z = c.lungeDir_.z * 25.0f;
        if (c.lungeRemain_ <= 0.0f) { v.x *= 0.3f; v.z *= 0.3f; }
        hv = core::Vec3{v.x, 0, v.z};
    }
    const bool lunging = c.lungeRemain_ > 0.0f;
    const bool dodging = c.dodgeRemain_ > 0.0f;
    if (hovering) {
        // PHYS_Flying: planar input toward HoverAirSpeed at AccelRate; no gravity, no vertical drift.
        core::Vec3 des = wish * RPd.hoverAirSpeed;
        core::Vec3 dv{des.x - v.x, 0.0f, des.z - v.z};
        float l = core::length(dv), mx = RPd.accel * dt;
        if (l > mx && l > 1e-5f) dv = dv * (mx / l);
        v.x += dv.x; v.z += dv.z; v.y = 0.0f;
        hv = core::Vec3{v.x, 0, v.z};
    } else if (dodging) {
        c.dodgeRemain_ -= dt;   // PHYS_Flying: no gravity, the velocity carries (Acceleration = Normal(Velocity))
    } else if (lunging) {
        // velocity set above
    } else if (c.rammedRemain_ > 0.0f) {
        // RammedReaction [CONF native M03 P8]: the forced velocity set on entry carries for 0.5 s (no
        // movement input); EndState restores air movement with Velocity = (0, 0, base Z).
        c.rammedRemain_ -= dt;
        if (c.rammedRemain_ <= 0.0f) { c.rammedRemain_ = 0.0f; v.x = 0.0f; v.z = 0.0f; v.y = c.rammedBaseY_; }
    } else {
        hv = robotCalcVelocity(c, wish, hv, !wasGround, in.faceYaw, dt);
        v.x = hv.x; v.z = hv.z;
    }

    // [CONF] pawn gravity -29.4 m/s^2 (not while dodging: PHYS_Flying).
    if (!dodging && !hovering) v.y -= core::config::kGravity * dt;
    v.y = std::max(v.y, -c.robotParams().terminalVel);

    // Jumping stays a robot-form, non-transforming action [PROV during a fold].
    if (in.wantJump && c.onGround() && !c.isTransforming() && !hovering && t.jumpSpeed > 0.0f) {
        v.y = c.robotParams().jumpSpeed();   // JumpZ = sqrt(2 g JumpHeight) of this chassis' acrobatics
        c.setOnGround(false);
    }

    core::Vec3 oldPos = c.position();
    core::Vec3 p = oldPos + v * dt;

    // physWalking: after a blocked move the velocity is the actual displacement over the step. Probes: capsule centre
    // (2 m) and head (3.6 m: the 4 m cylinder does not pass under overhangs lower than its top); steps / stairs belong to
    // the centre-point ground model (MaxStepHeight 0.35 m). [PROV collision model: no capsule sweep]
    const float pr = c.robotParams().radius, phh = c.robotParams().halfHeight;
    bool robotBlocked = wallBlock(col, oldPos, p, v, pr, phh);
    robotBlocked |= wallBlock(col, oldPos, p, v, pr, 2.0f * phh - 0.4f);
    // Knee probe (0.55 m, above MaxStepHeight 0.35; short reach 0.7 m; walkable faces skipped): low props (crates, battery, supports 1-2 m tall)
    // block the body instead of being walked through; the short reach keeps stairs to the centre-point ground model
    // (a 35 deg stair 0.7 m ahead is ~0.49 m high). [PROV: the native cylinder sweep with step-up is not reproduced]
    robotBlocked |= wallBlock(col, oldPos, p, v, 0.7f, 0.55f, 0.7f);
    if (robotBlocked) {
        v.x = (p.x - oldPos.x) / dt; v.z = (p.z - oldPos.z) / dt;
        if (dodging) c.dodgeRemain_ = 0.0f;               // Dodging.OnHitWall -> Falling
    }
    if (dodging && c.dodgeRemain_ <= 0.0f) {
        // Dodging.EndState: Velocity = ClampLength(Velocity * (1,1,0), airborne ? MaxAirSpeed : MaxGroundSpeed).
        core::Vec3 h{v.x, 0.0f, v.z};
        float cap = (c.onGround() ? RPd.groundSpeed : RPd.airSpeed) * c.speedMultiplier();
        float l = core::length(h);
        if (l > cap && l > 1e-4f) h = h * (cap / l);
        v = core::Vec3{h.x, 0.0f, h.z};
    }

    // --- Ground resolution ---
    // Collision cylinder bottom relative to the robot mesh origin (position()). The actor location (cylinder
    // centre) is shared by both forms and continuous through a transformation; the robot mesh hangs the robot
    // CollisionHeight below it. During TransformingToRobot, TnPawn.UpdateCylinderSize lerps the cylinder
    // radius / half-height from the vehicle's to the robot's by RemainingTimeAsFactor (1 -> 0) [CONF RE
    // TARGETED_PASS 1e], so the cylinder bottom starts at the vehicle's bottom (on the floor when driving: wheels
    // down, root at floor level) and grows downward; the floor then pushes the actor up as it grows. The vehicle
    // cylinder half-height = TnVehicleForm.CalculateCylinderBounds of the mesh bounds [HIGH].
    float lift = 0.0f;
    if (c.isTransforming()) {
        float remaining = 1.0f - c.transformProgress();
        float hhVeh = c.meshToActor(Form::Vehicle), hhRobot = c.robotParams().halfHeight;
        lift = hhRobot - (hhRobot * (1.0f - remaining) + hhVeh * remaining);
    }
    bool grounded = false;
    if (col) {
        float gy; core::Vec3 n;
        // Support search: highest surface at most MaxStepHeight above the cylinder bottom (groundHeight()
        // returns the highest surface <= nearY + stepUp). Falling (PHYS_Falling) sweeps the whole step, so the
        // search starts from the previous bottom: a floor crossed within one step is landed on, not skipped
        // [HIGH: UE3 physFalling MoveActor sweep]. A vehicle->robot fold starts falling (TnRobotForm.OnActivate
        // -> AcrobaticsManager.Fall) from the shared actor location and lands normally.
        float bottomOld = oldPos.y + lift, bottom = p.y + lift;
        float base = c.onGround() ? bottomOld : (v.y <= 0.0f ? std::max(bottomOld, bottom) : bottom);
        if (col->groundHeight(p.x, p.z, base, kStepUp, gy, n)) {
            if (bottom <= gy + 0.001f) {        // at/below the floor -> settle on it (incl. step-up)
                p.y = gy - lift; if (v.y < 0) v.y = 0; grounded = true;
            } else if (c.onGround() && (bottom - gy) <= kSnapDown) {
                p.y = gy - lift; if (v.y < 0) v.y = 0; grounded = true;   // follow slopes/stairs down
            }
        }
    } else {
        if (p.y <= c.groundY) { p.y = c.groundY; if (v.y < 0) v.y = 0; grounded = true; }
    }
    c.setOnGround(grounded);
    if (grounded && c.dodgeRemain_ <= 0.0f) c.landedSinceDodge_ = true;
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
