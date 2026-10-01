#include "game/CharacterMovement.h"
#include "game/Character.h"
#include "game/Collision.h"
#include "core/Config.h"

#include <cmath>

namespace game::CharacterMovement {

static constexpr float kStepUp = 0.6f;      // [PROV] MaxStepHeight not found overridden (Engine default 35 UU)
static constexpr float kSnapDown = 1.0f;    // follow downward slopes/stairs while grounded
// Wall-block probe uses the recovered capsule radius [CONF] (CylinderRadius 175 UU = 1.75 m).
static constexpr float kProbeRadius = 1.75f;

void update(Character& c, const MoveIntent& in, float dt, const CollisionWorld* col) {
    FormTuning t = tuningFor(c.form());

    core::Vec3 fwd = core::forwardFromYawPitch(in.faceYaw, 0.0f);
    core::Vec3 right = core::normalize(core::cross(fwd, core::Vec3{0, 1, 0}));
    core::Vec3 wish = fwd * in.moveForward + right * in.moveRight;
    float wishLen = core::length(wish);
    if (wishLen > 1.0f) wish = wish * (1.0f / wishLen);

    // Robot [CONF]: GroundSpeed 5.5, AccelRate 20.48, AirControl 0.70, AirSpeed 15.
    // Vehicle [CONF, TnHoverCarSimulationBlueprint]: MaxLinearSpeed 15, accel 30, DashSpeed 50.
    bool wasGround = c.onGround();
    bool vehicle = c.form() == Form::Vehicle;
    bool boosting = vehicle && in.wantBoost;
    float topSpeed = boosting ? core::config::kVehicleBoostSpeed : t.moveSpeed;
    core::Vec3 targetVel = wish * topSpeed;

    core::Vec3& v = c.velocity();
    core::Vec3 hv{v.x, 0, v.z};
    core::Vec3 dv = targetVel - hv;
    float accel = wasGround ? t.accel : t.accel * core::config::kAirControl;
    // Vehicle dash reaches DashSpeed within DashDuration (burst), not the cruise accel.
    if (boosting) accel = core::config::kVehicleBoostSpeed / core::config::kVehicleDashTime;
    float maxDelta = accel * dt;
    float dvLen = core::length(dv);
    if (dvLen > maxDelta && dvLen > 1e-5f) dv = dv * (maxDelta / dvLen);
    hv += dv;
    float cap = wasGround ? topSpeed : (vehicle ? core::config::kVehicleBoostSpeed : core::config::kAirSpeed);
    float hs = core::length(hv);
    if (hs > cap && hs > 1e-5f) hv = hv * (cap / hs);
    v.x = hv.x; v.z = hv.z;

    // [CONF] pawn gravity -29.4 m/s^2; vehicles use RB-scaled gravity (0.66) -> -19.4 m/s^2.
    float g = (c.form() == Form::Vehicle) ? core::config::kVehicleGravity : core::config::kGravity;
    v.y -= g * dt;

    if (in.wantJump && c.onGround() && t.jumpSpeed > 0.0f) {
        v.y = t.jumpSpeed;
        c.setOnGround(false);
    }

    core::Vec3 oldPos = c.position();
    core::Vec3 p = oldPos + v * dt;

    // --- Horizontal wall block (crude): stop horizontal motion if a wall is in the way ---
    if (col) {
        float torso = core::config::kPawnHalfHeight;   // probe at capsule centre (~2 m)
        core::Vec3 a = oldPos + core::Vec3{0, torso, 0};
        core::Vec3 b = core::Vec3{p.x, oldPos.y + torso, p.z};
        core::Vec3 dir = b - a;
        float dist = core::length(dir);
        if (dist > 1e-4f) {
            core::Vec3 bEx = a + dir * ((dist + kProbeRadius) / dist); // probe slightly ahead
            float tHit;
            if (col->segmentHit(a, bEx, tHit) && tHit * (dist + kProbeRadius) < dist + kProbeRadius) {
                // Blocked: cancel horizontal advance this step.
                p.x = oldPos.x; p.z = oldPos.z;
                v.x = 0; v.z = 0;
            }
        }
    }

    // --- Ground resolution --- (vehicles hover SuspensionRadius=2 m above the surface) ---
    bool grounded = false;
    float hover = vehicle ? core::config::kVehicleHoverH : 0.0f;
    if (col) {
        float gy; core::Vec3 n;
        float searchTop = (c.onGround() ? oldPos.y : p.y) + kStepUp + hover;
        if (col->groundHeight(p.x, p.z, searchTop, kStepUp + hover, gy, n)) {
            float floorY = gy + hover;
            if (p.y <= floorY + 0.001f) {       // at/below hover floor -> settle on it (incl. step-up)
                p.y = floorY; if (v.y < 0) v.y = 0; grounded = true;
            } else if (c.onGround() && (p.y - floorY) <= (kSnapDown + hover)) {
                p.y = floorY; if (v.y < 0) v.y = 0; grounded = true;   // follow terrain at hover height
            }
        }
    } else {
        float floorY = c.groundY + hover;
        if (p.y <= floorY) { p.y = floorY; if (v.y < 0) v.y = 0; grounded = true; }
    }
    c.setOnGround(grounded);
    c.setPosition(p);

    // Facing: WFC robot is a strafe shooter — the body always faces the aim (camera) yaw, and
    // the directional locomotion clip conveys the travel direction. The vehicle instead faces
    // its travel direction (steering).
    if (c.form() == Form::Robot) {
        c.setYaw(in.faceYaw);
    } else {
        // Steer toward the travel direction at the recovered turn rate [CONF] AiMaxAngularSpeed
        // (~pi rad/s) instead of snapping; the lag shows as the L/R hover lean poses.
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
