// Clean-room reconstruction — deterministic vehicle handling measurements (WFC_VEHTEST=1).
// Runs the real CharacterMovement vehicle step at the 60 Hz sim rate against generated collision
// geometry (flat ground, 0.25 m / 0.5 m steps, a drop) and logs the response, so handling claims are
// measured rather than judged by feel.
#include "game/VehicleTests.h"
#include "game/Character.h"
#include "game/CharacterMovement.h"
#include "game/Collision.h"
#include "core/Config.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>

namespace game {
namespace {

// Ground plane at y = 0 with an optional raised slab (top y = h) for z in [zA, zB] (forward is -Z at yaw 0).
CollisionWorld makeWorld(float h, float zA, float zB) {
    render::MeshData m;
    auto quad = [&](core::Vec3 a, core::Vec3 b, core::Vec3 c, core::Vec3 d) {
        uint32_t base = (uint32_t)(m.positions.size() / 3);
        for (const core::Vec3& p : {a, b, c, d}) { m.positions.push_back(p.x); m.positions.push_back(p.y); m.positions.push_back(p.z); }
        for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) m.indices.push_back(base + i);
    };
    const float X = 40.0f, Z0 = 60.0f, Z1 = -200.0f;
    if (h == 0.0f) { quad({-X, 0, Z0}, {X, 0, Z0}, {X, 0, Z1}, {-X, 0, Z1}); }
    else {
        quad({-X, 0, Z0}, {X, 0, Z0}, {X, 0, zA}, {-X, 0, zA});           // before
        quad({-X, 0, zA}, {X, 0, zA}, {X, h, zA}, {-X, h, zA});           // riser
        quad({-X, h, zA}, {X, h, zA}, {X, h, zB}, {-X, h, zB});           // top
        quad({-X, h, zB}, {X, h, zB}, {X, 0, zB}, {-X, 0, zB});           // drop-off
        quad({-X, 0, zB}, {X, 0, zB}, {X, 0, Z1}, {-X, 0, Z1});           // after
    }
    CollisionWorld w;
    w.build(m);
    return w;
}

struct Sim {
    Character c;
    const CollisionWorld* col = nullptr;
    float t = 0.0f;
    const float dt = 1.0f / 60.0f;
    Sim(const CollisionWorld* w, float y) : col(w) {
        c.setForm(Form::Vehicle);
        c.setYaw(0.0f);
        c.setPosition({0, y, 0});
    }
    float comY() const {
        const auto& vs = c.vehicleState();
        return c.position().y + core::config::kVehComUp * std::cos(vs.pitch) * std::cos(vs.roll);
    }
    void step(const MoveIntent& in, int n = 1) {
        for (int i = 0; i < n; ++i) { CharacterMovement::update(c, in, dt, col); t += dt; }
    }
};

float hspeed(const Character& c) { return std::sqrt(c.velocity().x * c.velocity().x + c.velocity().z * c.velocity().z); }

} // namespace

void runVehicleTests() {
    namespace cfg = core::config;
    MoveIntent idle;
    // 1) Rest height on flat ground (expected L_eq = L0 - |g| (M/4)/K = 250 - 1940.4*625/10000 = 128.7 UU).
    {
        CollisionWorld w = makeWorld(0, 0, 0);
        Sim s(&w, 2.0f);
        s.step(idle, 240);
        LOG_INFO("VEHTEST rest: COM height %.4f m (native L_eq 1.287 m) contacts=%d pitch=%.3f roll=%.3f",
                 s.comY(), s.c.vehicleState().contacts, s.c.vehicleState().pitch * 57.2958f, s.c.vehicleState().roll * 57.2958f);
    }
    // 2) Coast-down and sideways stop (expected 1500 -> 0 in 0.5 s at 3000 UU/s^2, same both axes).
    for (int axis = 0; axis < 2; ++axis) {
        CollisionWorld w = makeWorld(0, 0, 0);
        Sim s(&w, 1.3f);
        s.step(idle, 120);
        MoveIntent go; (axis ? go.moveRight : go.moveForward) = 1.0f;
        s.step(go, 60);
        float v0 = hspeed(s.c), t0 = s.t, tStop = -1.0f;
        for (int i = 0; i < 90 && tStop < 0; ++i) { s.step(idle); if (hspeed(s.c) < 0.01f) tStop = s.t - t0; }
        LOG_INFO("VEHTEST %s: cruise %.2f m/s, release -> stop in %.3f s (native 0.5 s)", axis ? "strafe" : "forward", v0, tStop);
    }
    // 3) Bumps: 0.25 m and 0.5 m slabs (10 m long) crossed at 15 m/s.
    for (float h : {0.25f, 0.5f}) {
        CollisionWorld w = makeWorld(h, -20.0f, -30.0f);
        Sim s(&w, 1.3f);
        s.step(idle, 120);
        MoveIntent go; go.moveForward = 1.0f;
        float minC = 1e9f, maxC = -1e9f, maxP = 0.0f, minP = 0.0f, maxVy = 0.0f;
        float base = s.comY();
        for (int i = 0; i < 240; ++i) {
            s.step(go);
            float z = s.c.position().z;
            float ground = (z < -20.0f && z > -30.0f) ? h : 0.0f;
            float c = s.comY() - ground;
            minC = std::min(minC, c); maxC = std::max(maxC, c);
            maxP = std::max(maxP, s.c.vehicleState().pitch); minP = std::min(minP, s.c.vehicleState().pitch);
            maxVy = std::max(maxVy, std::fabs(s.c.velocity().y));
        }
        LOG_INFO("VEHTEST bump %.2f m @15 m/s: COM-above-ground min %.3f max %.3f (rest %.3f), pitch %.1f..%.1f deg, |vy| max %.2f, speed after %.2f",
                 h, minC, maxC, base, minP * 57.2958f, maxP * 57.2958f, maxVy, hspeed(s.c));
    }
    // 4) Large drop: released 10 m above the ground.
    {
        CollisionWorld w = makeWorld(0, 0, 0);
        Sim s(&w, 10.0f);
        float minC = 1e9f, vImpact = 0.0f;
        int bounces = 0; float prevVy = 0.0f;
        for (int i = 0; i < 300; ++i) {
            s.step(idle);
            if (s.c.vehicleState().contacts > 0 && vImpact == 0.0f) vImpact = prevVy;
            minC = std::min(minC, s.comY());
            if (prevVy < 0.0f && s.c.velocity().y >= 0.0f) ++bounces;
            prevVy = s.c.velocity().y;
        }
        LOG_INFO("VEHTEST drop 10 m: first-contact vy %.2f m/s, min COM height %.3f m, settled %.3f m, reversals %d",
                 vImpact, minC, s.comY(), bounces);
    }
    // 5) Hover jump from rest and while moving (expected +12 m/s additive, pitch kick -1 rad/s, ballistic apex
    //    ~3.71 m above rest before the springs re-engage).
    for (int moving = 0; moving < 2; ++moving) {
        CollisionWorld w = makeWorld(0, 0, 0);
        Sim s(&w, 1.3f);
        MoveIntent in; in.moveForward = moving ? 1.0f : 0.0f;
        s.step(in, 150);
        float rest = s.comY(), v0 = hspeed(s.c);
        MoveIntent j = in; j.wantJump = true;
        s.step(j);
        float vy = s.c.velocity().y, apex = rest, maxPitch = 0.0f, tLand = -1.0f;
        for (int i = 0; i < 240; ++i) {
            s.step(in);
            apex = std::max(apex, s.comY());
            maxPitch = std::max(maxPitch, s.c.vehicleState().pitch);
            if (tLand < 0 && i > 10 && s.c.vehicleState().contacts > 0) tLand = s.t;
        }
        LOG_INFO("VEHTEST hover jump (%s, %.1f m/s): vy after %.2f m/s, apex +%.3f m over rest, max nose-up %.1f deg, horizontal kept %.2f m/s",
                 moving ? "moving" : "standing", v0, vy, apex - rest, maxPitch * 57.2958f, hspeed(s.c));
    }
    // 6) Hover dash: body-forward only, lateral/vertical zeroed, exit snaps to forward 15 m/s.
    {
        CollisionWorld w = makeWorld(0, 0, 0);
        Sim s(&w, 1.3f);
        MoveIntent in; in.moveRight = 1.0f;          // stick sideways: must not steer the dash
        s.step(in, 150);
        MoveIntent d = in; d.wantDash = true;
        s.step(d);
        core::Vec3 v1 = s.c.velocity();
        s.step(in);
        core::Vec3 v2 = s.c.velocity();
        s.step(in, 26);
        core::Vec3 v3 = s.c.velocity();
        s.step(in, 2);
        core::Vec3 v4 = s.c.velocity();
        // yaw 0: forward = -Z, right = +X
        LOG_INFO("VEHTEST dash (stick right): tick1 fwd %.2f lat %.2f | tick2 fwd %.2f lat %.2f | t=0.47 s fwd %.2f lat %.2f vy %.2f | exit fwd %.2f lat %.2f",
                 -v1.z, v1.x, -v2.z, v2.x, -v3.z, v3.x, v3.y, -v4.z, v4.x);
    }
    // 6b) Camera yaw change while drifting (after boost release the 0.5 s (1-t/0.5)^2 authority ramp):
    //     heading follows the camera at once, the velocity follows only as authority returns.
    {
        CollisionWorld w = makeWorld(0, 0, 0);
        Sim s(&w, 1.3f);
        MoveIntent b; b.wantBoost = true; b.moveForward = 1.0f;
        s.step(b, 150);
        MoveIntent in; in.moveForward = 1.0f; in.faceYaw = 1.5707963f;   // release boost and turn the camera 90 deg
        float v0 = hspeed(s.c);
        for (int i = 1; i <= 45; ++i) {
            s.step(in);
            if (i % 9 == 0) {
                core::Vec3 v = s.c.velocity();
                float ang = std::atan2(-v.x, -v.z) * 57.2958f;   // travel heading (yaw convention)
                LOG_INFO("VEHTEST drift turn t=%.2f s: yaw %.1f deg, travel heading %.1f deg, speed %.2f (start %.2f)",
                         i / 60.0f, s.c.yaw() * 57.2958f, ang, hspeed(s.c), v0);
            }
        }
    }
    // 7) Boost (Driving) jump: local (6, 0, 14) m/s and nose-up 2 rad/s.
    {
        CollisionWorld w = makeWorld(0, 0, 0);
        Sim s(&w, 1.3f);
        MoveIntent b; b.wantBoost = true;
        s.step(b, 120);
        float v0 = hspeed(s.c);
        MoveIntent j = b; j.wantJump = true;
        s.step(j);
        LOG_INFO("VEHTEST boost jump: speed before %.2f, after: fwd %.2f vy %.2f, pitch rate %.2f rad/s",
                 v0, -s.c.velocity().z, s.c.velocity().y, -s.c.vehicleState().angVel.y);
    }
}

} // namespace game
