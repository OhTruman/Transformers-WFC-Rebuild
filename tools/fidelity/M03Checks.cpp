// WFC fidelity harness — milestone-03 suites: transformation timeline flags, vehicle feel
// probes and Fine Aim presentation. Measurements are INFO unless a value is CONFIRMED by native
// RE (RE-Workspace notes); "feel"/"weight" is never asserted - it needs a human check.
#include "CheckUtil.h"
#include "assets/SkinnedModel.h"
#include "core/Config.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <map>
#include "assets/Gltf.h"

namespace fid {

using platform::Button;
using namespace core::config;

namespace {
std::string f2(double v) { char b[32]; std::snprintf(b, sizeof b, "%.3f", v); return b; }

// Fine-aim / dash buttons exist only on milestone-02+ builds.
template <class B> constexpr auto dashIdx(int) -> decltype(B::Dash, int()) { return (int)B::Dash; }
template <class B> constexpr int dashIdx(long) { return -1; }

core::Vec3 rootOf(const Frame& f) { return f.pos + f.meshOff; }
float yawDiffDeg(float a, float b) { return std::fabs(core::degrees((float)std::remainder((double)a - b, 2 * core::PI))); }
} // namespace

// ---------------------------------------------------------------------------------------------
// TRANSFORMATION TIMELINE (both directions, standing and moving): per-step trace + automatic flags.
void checkTransformTimeline(Report& r) {
    r.setGroup("transform_timeline");
    const Models* m = Models::get();
    if (!m) { r.skip("transform_timeline", "needs Optimus models"); return; }
    // On a collision floor: the game always moves with collision, and the transform height pop is
    // absorbed by Character::addTransformShift only on that path (the col==nullptr graybox fallback
    // has no absorption - not a shipping path, noted as INFO).
    BoxScene floor;
    floor.floor(0, 200);
    game::CollisionWorld col;
    col.build(floor.mesh);
    r.info("flat_fallback_has_no_shift_absorption", 0, "",
           "CharacterMovement's col==nullptr branch lifts to hover height without addTransformShift (graybox fallback only)");
    struct Case { const char* id; game::Form from; bool moving; };
    for (Case c : {Case{"r2v_stand", game::Form::Robot, false}, Case{"r2v_moving", game::Form::Robot, true},
                   Case{"v2r_stand", game::Form::Vehicle, false}, Case{"v2r_moving", game::Form::Vehicle, true}}) {
        Rig rig(60, true);
        rig.setCollision(&col);
        rig.pawn().setForm(c.from);
        rig.idle(0.3);
        platform::InputFrame in = c.moving ? Rig::down({Button::Forward}) : platform::InputFrame{};
        rig.hold(in, 1.0);
        double t0 = rig.time();
        platform::InputFrame press = in;
        press.pressed[(int)Button::Transform] = true;
        rig.step(press);
        rig.hold(in, 2.6);
        save(rig, std::string("tt_") + c.id);
        const auto& tr = rig.trace();
        std::string id = c.id;

        int freezeRun = 0, maxFreeze = 0, gapFrames = 0, usableNoMuzzle = 0, rootJumps = 0, yawJumps = 0, handoffIdx = -1;
        double maxRootErr = 0, foldLen = 0;
        for (size_t i = 1; i < tr.size(); ++i) {
            const Frame& a = tr[i - 1]; const Frame& b = tr[i];
            if (b.t <= t0) continue;
            if (b.transforming) foldLen = b.t - t0;
            if (!b.transforming) continue;
            // pose freeze: same clip, clip time not advancing
            if (b.anim == a.anim && std::fabs(b.animT - a.animT) < 1e-6f) maxFreeze = std::max(maxFreeze, ++freezeRun);
            else freezeRun = 0;
            // the rebuild draws exactly one mesh (form()); "gap" = no drawable model
            if (b.anim.empty() || b.anim == "-") ++gapFrames;
            if (b.wUsable > 0 && !b.weaponVisible) ++usableNoMuzzle;
            if (b.form != a.form && handoffIdx < 0) handoffIdx = (int)i;
            // root continuity: drawn root may move by velocity*dt plus the authored height blend
            core::Vec3 d = rootOf(b) - rootOf(a);
            double expected = core::length(core::Vec3{a.vel.x, a.vel.y, a.vel.z}) * rig.dt();
            double err = std::max(0.0, (double)core::length(d) - expected);
            maxRootErr = std::max(maxRootErr, err);
            if (err > 0.15) ++rootJumps;
            if (yawDiffDeg(b.yaw, a.yaw) > 8.0f) ++yawJumps;
        }
        r.truth(id + ".no_pose_freeze", maxFreeze < 3, "transform clips advance every step (no frozen pose)",
                "longest frozen run " + std::to_string(maxFreeze) + " steps");
        r.truth(id + ".no_visibility_gap", gapFrames == 0, "a model is drawn on every fold step", std::to_string(gapFrames) + " gap steps");
        r.knownTruth(id + ".no_usable_weapon_without_muzzle", usableNoMuzzle == 0,
                     "weapon never usable while no visible weapon/muzzle exists", kGameplay,
                     std::to_string(usableNoMuzzle) + " steps: weaponUsable() (restored at 25% + 0.2 s equip) turns true "
                     "before the robot mesh is displayed (mid-fold handoff), so shots leave an invisible gun");
        r.truth(id + ".root_continuous", rootJumps == 0, "drawn root (position + mesh offset) never jumps beyond velocity*dt + 0.15 m",
                "max excess " + f2(maxRootErr) + " m");
        r.info(id + ".max_root_excess", maxRootErr, "m", "largest per-step drawn-root move beyond velocity*dt");
        r.truth(id + ".heading_continuous", yawJumps == 0, "no per-step heading jump > 8 deg during the fold",
                std::to_string(yawJumps) + " jumps");
        r.info(id + ".fold_time", foldLen, "s", "");
        if (handoffIdx >= 0) {
            const Frame& h = tr[(size_t)handoffIdx];
            r.info(id + ".mesh_handoff_progress", h.progress, "frac", "displayed mesh switches here; rebuild shows ONE mesh at a time");
        }
        // Original: both meshes animate for the whole fold, source detached at the end (RE HANDOFF #4).
        r.confTruth(id + ".both_meshes_visible_during_fold", false,
                    "RE HANDOFF #4 (both meshes animate the whole duration; source mesh detached at t=Duration)", kGameplay,
                    "Character::draw draws only currentModel(): one mesh, switched at the mid-fold handoff (" +
                        (handoffIdx >= 0 ? f2(tr[(size_t)handoffIdx].progress) : std::string("n/a")) + ")");
    }
    r.knownTruth("separate_arm_mesh", false,
                 "RE TARGETED_PASS2 #9: the Optimus arm mesh is shown when no weapon is drawn (transform-in, melee, holster) and "
                 "hidden with the hand bone scaled 0.1 while the Ion Blaster is drawn", kGameplay,
                 "no arm mesh/visibility state exists in the rebuild (nothing to record)");
}

// ---------------------------------------------------------------------------------------------
// VEHICLE FEEL: deterministic probes. INFO unless RE-confirmed; "weight" is a human call.
template <class B> static void vehicleFeelImpl(Report& r);
void checkVehicleFeel(Report& r) { vehicleFeelImpl<Button>(r); }

template <class B> static void vehicleFeelImpl(Report& r) {
    r.setGroup("vehicle_feel");
    // In-place setup: a Rig must not be copied/moved (Player's controller points at its own pawn).
    auto veh = [](Rig& g, float settle = 0.4f) { g.pawn().setForm(game::Form::Vehicle); g.idle(settle); };
    const char* hp = "RE TARGETED_PASS2 #2 (HoverTruck_Physics; hover strafe sim, max 1500)";
    // Longitudinal
    {
        Rig g(60, false); veh(g);
        double t0 = g.time();
        g.hold(Rig::down({Button::Forward}), 2.0);
        r.info("accel_t90_forward", firstAfter(g, t0, [](const Frame& f) { return hspeed(f) >= 0.9f * 15.0f; }), "s", "0 -> 90% hover cruise");
        r.info("cruise_speed", hspeed(g.last()), "m/s", hp, 15.0);
        double t1 = g.time();
        g.idle(3.0);
        r.info("decel_t_to_1ms", firstAfter(g, t1, [](const Frame& f) { return hspeed(f) < 1.0f; }), "s",
               "release at cruise -> < 1 m/s (hover damping; capture original to compare)");
        save(g, "vf_long");
    }
    // Lateral + retention
    {
        Rig g(60, false); veh(g);
        double t0 = g.time();
        g.hold(Rig::down({Button::Right}), 2.0);
        r.info("accel_t90_lateral", firstAfter(g, t0, [](const Frame& f) { return hspeed(f) >= 0.9f * 15.0f; }), "s",
               "strafe right from rest (hover strafes in vehicle-local axes)");
        Rig h(60, false); veh(h);
        h.hold(Rig::down({Button::Forward}), 2.0);
        core::Vec3 v0 = h.last().vel;
        platform::InputFrame turn = Rig::down({Button::Forward});
        turn.mouseDX = (float)(core::PI * 0.5 / kMouseSens);   // 90 deg camera turn in one frame
        h.step(turn);
        h.hold(Rig::down({Button::Forward}), 0.5);
        core::Vec3 v1 = h.last().vel;
        double keep = core::dot(core::normalize(v0), core::Vec3{v1.x, 0, v1.z});
        r.info("lateral_retention_0p5s", keep, "m/s", "velocity still along the pre-turn direction 0.5 s after a 90 deg camera turn");
        r.info("heading_error_after_turn_deg", yawDiffDeg(h.last().yaw, h.last().camYaw), "deg",
               "hover heading tracks the camera (RE HANDOFF #7) - expect ~0");
        save(h, "vf_turn");
    }
    // Hover height + oscillation
    {
        Rig g(60, false); veh(g, 1.0f);
        r.info("ride_height", g.last().veh.valid ? g.last().veh.ride : g.last().pos.y - g.pawn().groundY, "m", "", 1.85);
        g.pawn().setPosition(g.pawn().position() + core::Vec3{0, 1.0f, 0});   // lift 1 m and release
        double t0 = g.time();
        g.idle(3.0);
        double lo = 1e9, hi = -1e9; int crossings = 0; double prev = 0; bool first = true;
        float base = g.last().pos.y;
        for (const Frame& f : g.trace()) {
            if (f.t <= t0) continue;
            double e = f.pos.y - base;
            lo = std::min(lo, e); hi = std::max(hi, e);
            if (!first && ((prev > 0.01 && e < -0.01) || (prev < -0.01 && e > 0.01))) ++crossings;
            prev = e; first = false;
        }
        r.info("hover_drop_undershoot", lo, "m", "min height error after a 1 m drop (negative = sinks below ride height)");
        r.info("hover_drop_crossings", crossings, "n", "oscillation zero-crossings after a 1 m drop (0 = critically damped / snapped)");
        r.info("hover_settle_time", firstAfter(g, t0, [&](const Frame& f) { return std::fabs(f.pos.y - base) < 0.02f; }), "s", "");
        r.info("roll_pitch_represented", 0, "", "vehicle body has yaw only (no roll/pitch state exposed)");
        save(g, "vf_hover");
    }
    // Jump
    {
        Rig g(60, false); veh(g, 1.0f);
        float base = g.last().pos.y;
        double t0 = g.time();
        g.step(Rig::press(Button::Jump));
        g.idle(2.5);
        double apex = base, vLaunch = 0, vLand = 0, tLand = -1;
        for (size_t i = 1; i < g.trace().size(); ++i) {
            const Frame& f = g.trace()[i];
            if (f.t <= t0) continue;
            apex = std::max(apex, (double)f.pos.y);
            vLaunch = std::max(vLaunch, (double)f.vel.y);
            if (tLand < 0 && f.t > t0 + 0.1 && f.pos.y <= base + 0.01) { tLand = f.t - t0; vLand = g.trace()[i - 1].vel.y; }
        }
        r.info("jump_launch_vy", vLaunch, "m/s", "HoverTruck JumpLinearSpeed 1200 UU/s expected if the jump fires", 12.0);
        r.info("jump_apex_above_hover", apex - base, "m", "");
        r.info("jump_landing_vy", vLand, "m/s", "vertical speed on touchdown");
        r.info("jump_airtime", tLand, "s", "");
        save(g, "vf_jump");
    }
    // Reversal
    {
        Rig g(60, false); veh(g);
        g.hold(Rig::down({Button::Forward}), 2.0);
        double t0 = g.time();
        g.hold(Rig::down({Button::Back}), 3.0);
        r.info("reversal_t_to_minus90", firstAfter(g, t0, [&](const Frame& f) {
                   return core::dot(core::Vec3{f.vel.x, 0, f.vel.z}, core::forwardFromYawPitch(f.camYaw, 0)) <= -0.9f * 15.0f; }),
               "s", "forward cruise -> 90% reverse speed");
    }
    // Boost (Driving), Dash, Nitro - RE-confirmed values where available.
    const char* bd = "RE TARGETED_PASS2 #2 (Boost = Driving, Truck MaxSpeed 3000; Dash 3000 x 0.5 s, cooldown 2 s, forward "
                     "only; Nitro = Dash while Driving: 3 s, speed x1.5, steer x0.3, cooldown 8 s)";
    {
        Rig g(60, false); veh(g);
        g.hold(Rig::down({Button::Forward}), 1.5);
        double t0 = g.time();
        g.hold(boostHeld({Button::Forward}), 3.0);
        r.conf("boost_top_speed", hspeed(g.last()), 30.0, 0.1, "m/s", bd, kGameplay);
        r.info("boost_t90", firstAfter(g, t0, [](const Frame& f) { return hspeed(f) >= 27.0f; }), "s", "cruise -> 90% boost speed");
        r.confTruth("boost_driving_state", g.last().veh.valid && g.last().veh.driving, bd, kGameplay, "vehicleState().driving while boost held");
        save(g, "vf_boost");
    }
    constexpr int dash = dashIdx<B>(0);
    if constexpr (dash < 0) {
        r.skip("dash", "build has no Dash input");
    } else {
        // No stick: the dash state starts but produces no motion in the rebuild ("forward only" -
        // whether the original dashes forward without input is not stated in the RE notes).
        {
            Rig s0(60, false); veh(s0);
            platform::InputFrame dp; dp.down[dash] = true; dp.pressed[dash] = true;
            s0.step(dp);
            s0.idle(0.6);
            double pk = 0;
            for (const Frame& f : s0.trace()) pk = std::max(pk, (double)hspeed(f));
            r.info("dash_no_stick_peak_speed", pk, "m/s", "Dash pressed with no stick input (expected behaviour UNKNOWN)");
        }
        Rig g(60, false); veh(g);
        g.hold(Rig::down({Button::Forward}), 1.5);           // forward cruise
        double t0 = g.time();
        platform::InputFrame dp = Rig::down({Button::Forward});
        dp.down[dash] = true; dp.pressed[dash] = true;
        g.step(dp);
        g.hold(Rig::down({Button::Forward}), 0.05);
        g.step(dp);                                          // second press inside the cooldown
        g.hold(Rig::down({Button::Forward}), 3.0);
        double peak = 0, above = 0, plateau = 0; int dashes = 0; bool was = false;
        for (const Frame& f : g.trace()) {
            if (f.t <= t0) continue;
            peak = std::max(peak, (double)hspeed(f));
            if (hspeed(f) > 15.5f) above += g.dt();
            if (hspeed(f) >= 0.9f * 30.0f) plateau += g.dt();
            bool on = f.veh.dash > 0; if (on && !was) ++dashes; was = on;
        }
        r.conf("dash_peak_speed", peak, 30.0, 0.5, "m/s", bd, kGameplay);
        r.conf("dash_plateau_time", plateau, 0.5, 0.12, "s", bd, kGameplay,
               "time at >= 90% of DashSpeed (the first decay frames can still be above 90%: tolerance 0.12 s)");
        r.info("dash_time_above_cruise", above, "s", "plateau + decay back to hover cruise (decay curve not recovered)");
        r.conf("dash_cooldown_blocks_repress", dashes, 1, 0, "dashes", bd, kGameplay, "second press 0.07 s later must be ignored (2 s cooldown)");
        save(g, "vf_dash");

        Rig n(60, false); veh(n);
        n.hold(Rig::down({Button::Forward}), 1.0);
        n.hold(boostHeld({Button::Forward}), 2.0);       // Driving at 30
        double tn = n.time();
        platform::InputFrame np = boostHeld({Button::Forward});
        np.down[dash] = true; np.pressed[dash] = true;
        n.step(np);
        n.hold(boostHeld({Button::Forward}), 4.0);
        double npeak = 0, nlen = 0;
        for (const Frame& f : n.trace()) if (f.t > tn) { npeak = std::max(npeak, (double)hspeed(f)); if (f.veh.nitro > 0) nlen += n.dt(); }
        r.conf("nitro_peak_speed", npeak, 45.0, 0.5, "m/s", bd, kGameplay, "Driving 30 x1.5");
        r.conf("nitro_duration", nlen, 3.0, 0.05, "s", bd, kGameplay);
        r.conf("nitro_cooldown", n.last().veh.valid ? n.last().veh.nitroCd + (n.time() - tn) : 0, 8.0, 0.1, "s", bd, kGameplay,
               "cooldown remaining + elapsed since nitro start");
        save(n, "vf_nitro");
    }
    r.info("weight_is_a_human_call", 0, "", "these probes explain feel differences; they do not prove 'weight' - compare with an original capture");
}

// ---------------------------------------------------------------------------------------------
// TRACE COST on the real Streets collision (opt-in with --map): the sustained-fire bottleneck.
// Each shot runs a camera-ray aim trace (PlayerController) and the 300 m hitscan (World::fireHitscan),
// both CollisionWorld::segmentHit. The profile attributes ~85% of firing frames to it.
void checkTraceCost(Report& r) {
    r.setGroup("trace_cost");
    if (!options().map) { r.skip("trace_cost", "pass --map (loads the Streets collision mesh)"); return; }
    render::MeshData mesh;
    if (!assets::loadGlb(Models::assetRoot() + "/Maps/MP_IAC_Streets/collision.glb", mesh)) { r.skip("trace_cost", "collision.glb unavailable"); return; }
    game::CollisionWorld col;
    col.build(mesh);
    const core::Vec3 eye{363.5f, -724.48f + 3.5f, -341.8f};   // FFA spawn eye
    for (float len : {2.0f, 30.0f, 300.0f}) {
        int n = len > 100 ? 6 : 40, hits = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < n; ++i) {
            float yaw = 1.01f + 0.3f * i;   // fan of directions from the spawn
            core::Vec3 b = eye + core::forwardFromYawPitch(yaw, -0.05f) * len;
            float t;
            hits += col.segmentHit(eye, b, t);
        }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
        r.info("segment_" + std::to_string((int)len) + "m_ms", ms, "ms",
               "mean CollisionWorld::segmentHit cost (" + std::to_string(hits) + "/" + std::to_string(n) + " hit); this build's config");
        if (len > 100)
            r.knownTruth("hitscan_trace_under_1ms", ms < 1.0,
                         "per-shot traces must be cheap: 2 x 300 m rays per shot at 15 shots/s (camera aim ray + hitscan)", kGameplay,
                         "segmentHit tests every grid cell in the segment's XZ bounding rectangle (O(area), 2 m cells, "
                         "1.85 M tris) instead of walking the cells the segment crosses (O(length)); " +
                             f2(ms) + " ms per 300 m ray");
    }
}

// ---------------------------------------------------------------------------------------------
// FINE AIM PRESENTATION (beyond fine_aim.*): what exists, what is wired, what is unknown.
void checkFineAimPresentation(Report& r) {
    r.setGroup("fine_aim_presentation");
    Rig g(60, Models::get() != nullptr);
    g.idle(0.3);
    platform::InputFrame on;
    on.down[authoredBoostIndex()] = true;   // FineAim button (RMB/LT) on builds that have it
    on.pressed[authoredBoostIndex()] = true;
    g.step(on);
    g.hold(platform::InputFrame{}, 0.6);
    const Frame& f = g.last();
    r.info("state_fine_aiming", f.fineAim, "", "controller fineAiming() after an RMB toggle (-1 = not exposed)");
    r.info("fov", f.fov, "deg", "CONFIRMED 45 (RE TARGETED_PASS #3)", 45.0);
    r.info("spread_multiplier_applied_in", 0, "",
           "World::fireHitscan scales spread by kFineAimSpreadMult (0.5, CONFIRMED) while pawn.fineAiming(); not observable "
           "in the windowless harness (World is stubbed) - code-level check only");
    {   // source scan of the tree under test (run from the worktree root); runtime: audio-attach.ps1
        std::string inc, world;
        bool wired = readFileText("src/game/SoundCues.inc", inc) && inc.find("FINE_AIM_START") != std::string::npos &&
                     readFileText("src/game/World.cpp", world) && world.find("FINE_AIM_START") != std::string::npos;
        r.knownTruth("start_end_audio", wired,
                     "RE TARGETED_PASS #3: StartFineAim/StopFineAim fire weapon events 15/16 (WP_StartFineAim sound)", kSystems,
                     wired ? "FINE_AIM_START/END cues present and played by World.cpp"
                           : "no fine-aim start/stop cue is wired in the rebuild (no matching SoundCues entry / call)");
    }
    r.info("reticle_hud", 0, "", "no HUD/reticle exists (window-title HUD only); the reticle asset relationship is not recovered - "
           "not asserted");
    save(g, "fine_aim_presentation");
}

} // namespace fid
