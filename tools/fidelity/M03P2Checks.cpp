// WFC fidelity harness — milestone-03 pass 2 suites:
//   transform_analyzer  per-step transform capture (both directions, standing/moving) against the
//                       CONFIRMED authored mesh-overlap windows, with automatic flags
//   vehicle_profiles    HOVER / BOOST / DASH / NITRO handling profiles on synthetic terrain through
//                       the PRODUCTION controller path (PlayerController::applyToPawn + collision)
//   fine_aim_probe      complete fine-aim state/camera/input/interrupt probe
// Every value is tagged with its evidence class:
//   [CONFIRMED ORIGINAL]  native RE / authored data confirmed by RE -> conf()/confTruth()
//   [HIGH CONFIDENCE]     authored data value, behaviour not confirmed -> info with the value shown
//   [UNKNOWN]             no original evidence: INFO only, needs a human/original comparison
#include "CheckUtil.h"
#include "assets/SkinnedModel.h"
#include "core/Config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

namespace fid {

using platform::Button;
using namespace core::config;

namespace {
std::string f3(double v) { char b[32]; std::snprintf(b, sizeof b, "%.3f", v); return b; }
float yawDeltaDeg(float a, float b) { return std::fabs(core::degrees((float)std::remainder((double)a - b, 2 * core::PI))); }
template <class B> constexpr auto dashButton(int) -> decltype(B::Dash, int()) { return (int)B::Dash; }
template <class B> constexpr int dashButton(long) { return -1; }
constexpr int kDash = dashButton<Button>(0);
std::string outDir() { return options().traceDir.empty() ? std::string() : options().traceDir; }

// Height of the pawn above the walkable surface below it (world collision), or NAN.
float heightAboveGround(Rig& g) {
    float y; core::Vec3 n;
    core::Vec3 p = g.pawn().position();
    if (!worldCollision(g.world()).groundHeight(p.x, p.z, p.y, 0.5f, y, n)) return NAN;
    return p.y - y;
}
} // namespace

// =============================================================================================
// TRANSFORMATION ANALYZER
// Authored overlap windows [CONFIRMED, integration brief M03 pass 2]: both meshes visible
//   robot->vehicle 0.396 .. 0.880 s, vehicle->robot 0.098 .. 0.663 s (elapsed since the press).
// Outside the window only the source (before) or the target (after) mesh is visible.
void checkTransformAnalyzer(Report& r) {
    r.setGroup("transform_analyzer");
    const Models* m = Models::get();
    if (!m) { r.skip("transform_analyzer", "needs Optimus models"); return; }
    const char* win = "authored mesh-overlap windows (R->V 0.396-0.880 s, V->R 0.098-0.663 s; M03 pass-2 brief)";
    BoxScene floor;
    floor.floor(0, 400);
    struct Case { const char* id; bool r2v, moving; double w0, w1; };
    for (Case c : {Case{"r2v_stand", true, false, 0.396, 0.880}, Case{"r2v_moving", true, true, 0.396, 0.880},
                   Case{"v2r_stand", false, false, 0.098, 0.663}, Case{"v2r_moving", false, true, 0.098, 0.663}}) {
        Rig g(60, true);
        g.useWorldCollision(floor.mesh);
        g.pawn().setForm(c.r2v ? game::Form::Robot : game::Form::Vehicle);
        g.idle(0.5);
        platform::InputFrame in = c.moving ? Rig::down({Button::Forward}) : platform::InputFrame{};
        g.hold(in, 1.0);
        size_t i0 = g.trace().size();
        double t0 = g.time();
        platform::InputFrame press = in;
        press.pressed[(int)Button::Transform] = true;
        g.step(press);
        g.hold(in, 2.8);
        save(g, std::string("ta_") + c.id);
        const auto& tr = g.trace();
        const std::string src = c.r2v ? "Transform_ToVehicle_ROBO" : "Transform_ToRobot_VEH";
        const std::string dst = c.r2v ? "Transform_ToVehicle_VEH" : "Transform_ToRobot_ROBO";
        int ci = c.r2v ? m->robot.clipByName(src) : m->vehicle.clipByName(src);
        int cj = c.r2v ? m->vehicle.clipByName(dst) : m->robot.clipByName(dst);
        double durS = ci >= 0 ? (c.r2v ? m->robot.clips[(size_t)ci].duration : m->vehicle.clips[(size_t)ci].duration) : 0;
        double durD = cj >= 0 ? (c.r2v ? m->vehicle.clips[(size_t)cj].duration : m->robot.clips[(size_t)cj].duration) : 0;
        const int srcModel = c.r2v ? 0 : 1, dstModel = c.r2v ? 1 : 0;

        // Analyzer CSV: one row per step from the press to 0.3 s after the fold.
        std::FILE* f = nullptr;
        if (!outDir().empty()) f = std::fopen((outDir() + "/transform_" + c.id + ".csv").c_str(), "wb");
        if (f)
            std::fprintf(f, "step,elapsed,robot_clip_norm,vehicle_clip_norm,played_clip,played_t,robot_visible,vehicle_visible,"
                            "expect_robot_visible,expect_vehicle_visible,arm_visible,weapon_visible,weapon_usable,form,collision_form,"
                            "root_x,root_y,root_z,root_yaw_deg,vel_x,vel_y,vel_z,cam_x,cam_y,cam_z,cam_target_x,cam_target_y,"
                            "cam_target_z,pose_delta,bb_h,flags\n");
        int frozenRun = 0, maxFrozen = 0, neither = 0, overlapOutside = 0, missingOverlap = 0, usableNoMuzzle = 0;
        int rootJumps = 0, yawJumps = 0, camPops = 0, authoredHolds = 0, farSteps = 0;
        double appear = -1, hide = -1, appearNorm = -1, bbJump = 0, maxCamErr = 0, maxRootErr = 0, foldEnd = -1, farMax = 0;
        double camSwing = 0, camSwingMaxStep = 0;
        std::vector<core::Mat4> scratch;
        render::MeshData ma, mb;
        // Does the PLAYED clip itself hold still between two clip times (authored hold)?
        auto authoredHold = [&](const Frame& fa, const Frame& fb) {
            const assets::SkinnedModel& sm = fb.drawnModel == 1 ? m->vehicle : m->robot;
            int ck = sm.clipByName(fb.anim);
            if (ck < 0 || fa.anim != fb.anim) return false;
            assets::evaluatePose(sm, ck, fa.animT, scratch, ma, false);
            assets::evaluatePose(sm, ck, fb.animT, scratch, mb, false);
            float d2 = 0;
            for (size_t k = 0; k < ma.positions.size() && k < mb.positions.size(); ++k) {
                float d = ma.positions[k] - mb.positions[k];
                d2 = std::max(d2, d * d);
            }
            return std::sqrt(d2) < 1e-4f;
        };
        core::Vec3 off0{0, 0, 0}; bool off0Set = false;
        for (size_t i = i0; i < tr.size(); ++i) {
            const Frame& b = tr[i];
            const Frame& a = tr[i > 0 ? i - 1 : 0];
            double e = b.t - t0;
            if (!b.transforming && foldEnd < 0 && e > 0.05) foldEnd = e;
            if (foldEnd >= 0 && e > foldEnd + 0.3) break;
            bool robotVis = b.drawnModel == 0, vehVis = b.drawnModel == 1;
            bool inWin = e >= c.w0 && e <= c.w1;
            bool before = e < c.w0;
            bool expSrc = before || inWin, expDst = !before;
            if (foldEnd >= 0) { expSrc = false; expDst = true; }
            bool expRobot = c.r2v ? expSrc : expDst, expVeh = c.r2v ? expDst : expSrc;
            std::string flags;
            if (b.transforming) {
                if (b.drawnModel < 0) { ++neither; flags += "neither_visible;"; }
                if (b.drawnModel == a.drawnModel && b.poseDelta >= 0 && b.poseDelta < 1e-4f && authoredHold(a, b)) {
                    ++authoredHolds;   // the clip itself holds this pose: not a playback freeze
                    flags += "authored_hold;";
                    frozenRun = 0;
                } else if (b.drawnModel == a.drawnModel && b.poseDelta >= 0 && b.poseDelta < 1e-4f) {
                    maxFrozen = std::max(maxFrozen, ++frozenRun);
                    if (frozenRun >= 2) flags += "pose_frozen;";
                } else {
                    frozenRun = 0;
                }
                if (inWin && !(robotVis && vehVis)) { ++missingOverlap; flags += "missing_authored_overlap;"; }
                if (b.wUsable > 0 && !b.weaponVisible) { ++usableNoMuzzle; flags += "usable_without_muzzle;"; }
                if (b.farVerts > 0) { ++farSteps; farMax = std::max(farMax, (double)b.farMax); flags += "far_vertices;"; }
            }
            if (robotVis && vehVis && !inWin) { ++overlapOutside; flags += "overlap_outside_window;"; }
            if (appear < 0 && b.drawnModel == dstModel) {
                appear = e;
                appearNorm = durD > 0 ? e / durD : -1;
                if (a.drawnModel >= 0) {   // silhouette jump at the switch: bounds change in one step
                    core::Vec3 d0 = a.bbMax - a.bbMin, d1 = b.bbMax - b.bbMin;
                    bbJump = std::max({std::fabs(d1.x - d0.x), std::fabs(d1.y - d0.y), std::fabs(d1.z - d0.z)});
                }
                flags += "target_mesh_appears;";
            }
            if (hide < 0 && a.drawnModel == srcModel && b.drawnModel != srcModel && i > i0) hide = e;
            if (i > i0) {
                // root continuity (drawn root = position + mesh offset): beyond velocity*dt
                core::Vec3 d = (b.pos + b.meshOff) - (a.pos + a.meshOff);
                double err = std::max(0.0, (double)core::length(d) - core::length(a.vel) * g.dt());
                maxRootErr = std::max(maxRootErr, err);
                if (err > 0.15) { ++rootJumps; flags += "root_jump;"; }
                if (yawDeltaDeg(b.yaw, a.yaw) > 8.0f) { ++yawJumps; flags += "yaw_jump;"; }
                // Camera relative to its target. A POP is a cut: > 1 m in one step (60 m/s) - smoothed
                // offset changes (shoulder offset blending between strategies) are reported as a swing.
                core::Vec3 dc = (b.camPos - a.camPos) - (b.camFocus - a.camFocus);
                double ce = core::length(dc);
                maxCamErr = std::max(maxCamErr, ce);
                if (ce > 1.0) { ++camPops; flags += "camera_pop;"; }
                if (!off0Set) { off0 = a.camPos - a.camFocus; off0Set = true; }
                camSwing = std::max(camSwing, (double)core::length((b.camPos - b.camFocus) - off0));
                camSwingMaxStep = std::max(camSwingMaxStep, ce);
            }
            if (f) {
                core::Vec3 root = b.pos + b.meshOff;
                std::fprintf(f, "%d,%.4f,%.4f,%.4f,%s,%.4f,%d,%d,%d,%d,-1,%d,%d,%s,%s,%.4f,%.4f,%.4f,%.2f,%.3f,%.3f,%.3f,"
                                "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.5f,%.3f,%s\n",
                             b.step, e, c.r2v ? e / durS : e / durD, c.r2v ? e / durD : e / durS, b.anim.c_str(), b.animT,
                             (int)robotVis, (int)vehVis, (int)expRobot, (int)expVeh, (int)b.weaponVisible, (int)(b.wUsable > 0),
                             game::formName(b.form), b.moveForm == 1 ? "VEHICLE" : (b.moveForm == 0 ? "ROBOT" : "n/a"),
                             root.x, root.y, root.z, core::degrees(b.yaw), b.vel.x, b.vel.y, b.vel.z, b.camPos.x, b.camPos.y,
                             b.camPos.z, b.camFocus.x, b.camFocus.y, b.camFocus.z, b.poseDelta, b.bbMax.y - b.bbMin.y, flags.c_str());
            }
        }
        if (f) std::fclose(f);
        std::string id = c.id;
        r.truth(id + ".no_pose_freeze", maxFrozen < 2, "drawn skinned pose changes every fold step",
                "longest run of identical poses: " + std::to_string(maxFrozen) + " steps");
        r.truth(id + ".no_step_without_mesh", neither == 0, "a mesh is drawn on every fold step", std::to_string(neither) + " steps");
        r.truth(id + ".no_overlap_outside_window", overlapOutside == 0, win, std::to_string(overlapOutside) + " steps");
        r.confTruth(id + ".overlap_inside_authored_window", missingOverlap == 0, win, kGameplay,
                    std::to_string(missingOverlap) + " steps inside the window show only one mesh (rebuild draws currentModel() only)");
        r.info(id + ".target_mesh_first_visible", appear, "s",
               std::string("[CONFIRMED window start ") + f3(c.w0) + " s] incoming mesh first drawn (" + dst + " normalized " +
                   f3(appearNorm) + " at that moment: it pops in mid-animation rather than growing in)", c.w0);
        r.info(id + ".source_mesh_hidden", hide, "s", std::string("[CONFIRMED window end ") + f3(c.w1) + " s] outgoing mesh last drawn", c.w1);
        r.info(id + ".switch_bounds_jump", bbJump, "m", "largest one-step change of the drawn silhouette's bounds at the mesh switch");
        r.knownTruth(id + ".no_usable_weapon_without_muzzle", usableNoMuzzle == 0,
                     "weapon never usable while no visible weapon/muzzle exists", kGameplay,
                     std::to_string(usableNoMuzzle) + " steps usable without a drawn weapon");
        r.truth(id + ".root_continuous", rootJumps == 0, "drawn root never jumps beyond velocity*dt + 0.15 m", "max excess " + f3(maxRootErr) + " m");
        r.truth(id + ".heading_continuous", yawJumps == 0, "no per-step heading jump > 8 deg", std::to_string(yawJumps) + " jumps");
        r.truth(id + ".no_camera_pop", camPops == 0, "camera never cuts relative to its target (> 1 m in one step)",
                "max one-step camera-vs-target move " + f3(maxCamErr) + " m");
        r.info(id + ".camera_offset_swing", camSwing, "m",
               "largest change of the camera's offset from its target during the fold (form camera handoff); max " +
                   f3(camSwingMaxStep * 60) + " m/s - human check: vehicle camera");
        r.info(id + ".authored_hold_steps", authoredHolds, "steps", "identical consecutive poses that the played clip itself holds (not flagged)");
        r.info(id + ".far_vertex_steps", farSteps, "steps",
               "fold steps drawing vertices > 10 m from the root (max " + f3(farMax) +
                   " m): parts the authored clip parks far away - the original's two-mesh display may hide them; human check");
        r.info(id + ".fold_end", foldEnd, "s", "isTransforming() clears");
    }
    r.info("arm_mesh_visible", -1, "", "CP_OptimusArm_SKEL visibility is not represented in the rebuild (column arm_visible = -1)");
    r.info("debug_geometry", -1, "",
           "not observable in the windowless harness; transform-capture.ps1 checks the real exe frames (debug overlay "
           "requires DebugFlags, toggled by the Debug key or WFC_DEBUGDRAW)");
}

// =============================================================================================
// VEHICLE PROFILES: HOVER / BOOST / DASH / NITRO on synthetic terrain, production controller path.
namespace {
struct Profile { const char* name; double speed; };
const char* kConfMech = "RE TARGETED_PASS2 #2 (Boost = Driving 3000 UU/s; Dash 3000 x 0.5 s, cd 2 s; Nitro x1.5 3 s, cd 8 s)";

// Enter the profile from rest, heading -Z; returns the input to keep holding.
platform::InputFrame enterProfile(Rig& g, const std::string& p) {
    g.pawn().setForm(game::Form::Vehicle);
    g.idle(0.6);
    platform::InputFrame fwd = Rig::down({Button::Forward});
    if (p == "HOVER") { g.hold(fwd, 1.5); return fwd; }
    if (p == "BOOST") { g.hold(fwd, 0.8); g.hold(boostHeld({Button::Forward}), 1.5); platform::InputFrame b = boostHeld({Button::Forward}); b.pressed[authoredBoostIndex()] = false; return b; }
    if (p == "DASH" && kDash >= 0) {
        g.hold(fwd, 1.0);
        platform::InputFrame d = fwd; d.down[kDash] = true; d.pressed[kDash] = true;
        g.step(d);
        return fwd;
    }
    if (p == "NITRO" && kDash >= 0) {
        g.hold(fwd, 0.8);
        g.hold(boostHeld({Button::Forward}), 1.2);
        platform::InputFrame n = boostHeld({Button::Forward}); n.down[kDash] = true; n.pressed[kDash] = true;
        g.step(n);
        platform::InputFrame b = boostHeld({Button::Forward}); b.pressed[authoredBoostIndex()] = false;
        g.hold(b, 0.3);
        return b;
    }
    return fwd;
}

constexpr float kFeatureZ = -150.0f;
struct Terrain { const char* name; BoxScene scene; float startY; };
std::vector<Terrain> terrains() {
    std::vector<Terrain> t(4);
    t[0].name = "flat";  t[0].scene.floor(0, 800); t[0].startY = 0;
    // Features start at z = kFeatureZ (the vehicle drives toward -Z from z = 0).
    t[1].name = "step";  t[1].scene.floor(0, 800); t[1].scene.box({-30, 0, -600}, {30, 0.5f, kFeatureZ}); t[1].startY = 0;
    t[2].name = "bumps"; t[2].scene.floor(0, 800);
    for (int k = 0; k < 60; ++k) t[2].scene.box({-30, 0, kFeatureZ - 6.0f * k - 1.0f}, {30, 0.25f, kFeatureZ - 6.0f * k}); t[2].startY = 0;
    t[3].name = "ledge"; t[3].scene.floor(0, 800); t[3].scene.box({-30, 0, kFeatureZ}, {30, 4.0f, 60}); t[3].startY = 4.0f;
    return t;
}
} // namespace

void checkVehicleProfiles(Report& r) {
    r.setGroup("vehicle_profiles");
    std::FILE* js = nullptr;
    if (!outDir().empty()) js = std::fopen((outDir() + "/vehicle_profiles.json").c_str(), "wb");
    if (js) std::fprintf(js, "{\n  \"note\": \"probes explain feel; they do not prove weight. classes: CONFIRMED ORIGINAL / HIGH CONFIDENCE / UNKNOWN\",\n  \"metrics\": [\n");
    bool firstJ = true;
    auto put = [&](const std::string& id, double v, const std::string& unit, const std::string& cls, const std::string& note,
                   double expected = NAN) {
        std::string tag = "[" + cls + "] " + note;
        r.info(id, v, unit, tag, expected);
        if (js) {
            std::fprintf(js, "%s    {\"id\": \"%s\", \"value\": %.5g, \"unit\": \"%s\", \"class\": \"%s\"", firstJ ? "" : ",\n", id.c_str(),
                         std::isfinite(v) ? v : -1.0, unit.c_str(), cls.c_str());
            if (std::isfinite(expected)) std::fprintf(js, ", \"original\": %.5g", expected);
            std::fprintf(js, "}");
            firstJ = false;
        }
    };
    const std::vector<std::string> profiles = kDash >= 0 ? std::vector<std::string>{"HOVER", "BOOST", "DASH", "NITRO"}
                                                         : std::vector<std::string>{"HOVER", "BOOST"};
    const double profSpeed[] = {15, 30, 30, 45};

    // --- Longitudinal curves per profile (flat) -------------------------------------------------
    for (size_t pi = 0; pi < profiles.size(); ++pi) {
        const std::string& P = profiles[pi];
        BoxScene flat; flat.floor(0, 2000);
        Rig g(60, false);
        g.useWorldCollision(flat.mesh);
        double t0 = g.time() + 0.6;   // enterProfile idles 0.6 s first
        platform::InputFrame hold = enterProfile(g, P);
        double tEnter = g.time();
        g.hold(hold, 1.0);
        double peak = 0;
        for (const Frame& f : g.trace()) if (f.t > t0) peak = std::max(peak, (double)hspeed(f));
        double tRel = g.time();
        g.idle(3.0);
        save(g, "vp_long_" + P);
        std::string pre = "long." + P;
        put(pre + ".peak_speed", peak, "m/s", P == "HOVER" ? "UNKNOWN" : "CONFIRMED ORIGINAL",
            P == "HOVER" ? "hover cruise (rebuild 15 m/s; original value not stated in RE notes)" : kConfMech,
            P == "HOVER" ? NAN : profSpeed[pi]);
        put(pre + ".t_to_90pct", firstAfter(g, t0, [&](const Frame& f) { return hspeed(f) >= 0.9 * peak; }), "s", "UNKNOWN",
            "from rest (HOVER) / from profile entry chain; accel curve in vp_long_" + P + ".csv");
        put(pre + ".release_to_1ms", firstAfter(g, tRel, [](const Frame& f) { return hspeed(f) < 1.0f; }), "s", "UNKNOWN",
            "release all input at profile speed -> < 1 m/s (deceleration curve)");
        put(pre + ".release_half_speed", firstAfter(g, tRel, [&](const Frame& f) { return hspeed(f) < 0.5 * peak; }), "s",
            "UNKNOWN", "release -> 50% of peak");
        (void)tEnter;
    }

    // --- Steering response at different speeds --------------------------------------------------
    for (size_t pi = 0; pi < profiles.size(); ++pi) {
        const std::string& P = profiles[pi];
        BoxScene flat; flat.floor(0, 2000);
        Rig g(60, false);
        g.useWorldCollision(flat.mesh);
        platform::InputFrame hold = enterProfile(g, P);
        core::Vec3 v0 = g.last().vel;
        double sp0 = hspeed(g.last());
        platform::InputFrame turn = hold;
        turn.mouseDX = (float)(core::PI * 0.5 / kMouseSens);   // 90 deg camera step
        double t0 = g.time();
        g.step(turn);
        g.hold(hold, 2.0);
        save(g, "vp_steer_" + P);
        core::Vec3 n0 = core::normalize(core::Vec3{v0.x, 0, v0.z});
        double maxSlip = 0, keep05 = 0, t90 = -1;
        for (const Frame& f : g.trace()) {
            if (f.t <= t0) continue;
            core::Vec3 hv{f.vel.x, 0, f.vel.z};
            double sp = core::length(hv);
            if (sp < 0.5) continue;
            core::Vec3 head = core::forwardFromYawPitch(f.yaw, 0);
            double slip = angleDeg(hv, head);
            maxSlip = std::max(maxSlip, slip);
            double turned = angleDeg(hv, n0);
            if (t90 < 0 && turned >= 81.0) t90 = f.t - t0;
            if (std::fabs(f.t - t0 - 0.5) < 0.5 * g.dt()) keep05 = core::dot(hv, n0);
        }
        std::string pre = "steer." + P;
        put(pre + ".speed_at_turn", sp0, "m/s", "UNKNOWN", "speed when the 90 deg camera step is applied");
        put(pre + ".heading_error_after_step", yawDeltaDeg(g.last().yaw, g.last().camYaw), "deg", "CONFIRMED ORIGINAL",
            "hover heading tracks the camera (RE HANDOFF #7)", 0.0);
        put(pre + ".velocity_t_to_81deg", t90, "s", "UNKNOWN", "time for the velocity direction to swing 90% of the turn");
        put(pre + ".velocity_kept_0p5s", keep05, "m/s", "UNKNOWN", "velocity along the old direction 0.5 s after the step (drift / mass feel)");
        put(pre + ".max_slip_deg", maxSlip, "deg", "UNKNOWN", "max angle between body heading and velocity (sideways skid)");
    }

    // --- Continuous yaw response (mouse turning at 90 deg/s while moving) ------------------------
    {
        BoxScene flat; flat.floor(0, 2000);
        Rig g(60, false);
        g.useWorldCollision(flat.mesh);
        platform::InputFrame hold = enterProfile(g, "HOVER");
        double lagMax = 0;
        for (int i = 0; i < 120; ++i) {
            platform::InputFrame t = hold;
            t.mouseDX = (float)(core::radians(90.0f) * g.dt() / kMouseSens);
            g.step(t);
            lagMax = std::max(lagMax, (double)yawDeltaDeg(g.last().yaw, g.last().camYaw));
        }
        save(g, "vp_yawrate");
        put("yaw.hover_heading_lag_at_90dps", lagMax, "deg", "CONFIRMED ORIGINAL", "heading follows the camera; max lag while turning 90 deg/s", 0.0);
    }

    // --- Lateral / reversal (HOVER) ---------------------------------------------------------------
    {
        BoxScene flat; flat.floor(0, 2000);
        Rig g(60, false);
        g.useWorldCollision(flat.mesh);
        g.pawn().setForm(game::Form::Vehicle);
        g.idle(0.6);
        double t0 = g.time();
        g.hold(Rig::down({Button::Right}), 2.0);
        put("lateral.hover_strafe_t90", firstAfter(g, t0, [](const Frame& f) { return hspeed(f) >= 13.5f; }), "s", "UNKNOWN",
            "strafe from rest to 90% of 15 m/s");
        g.hold(Rig::down({Button::Forward}), 2.0);
        double t1 = g.time();
        g.hold(Rig::down({Button::Back}), 3.0);
        put("lateral.hover_reversal_t_to_minus90", firstAfter(g, t1, [&](const Frame& f) {
                return core::dot(core::Vec3{f.vel.x, 0, f.vel.z}, core::forwardFromYawPitch(f.camYaw, 0)) <= -13.5f; }),
            "s", "UNKNOWN", "forward cruise -> 90% reverse");
        save(g, "vp_lateral");
    }

    // --- Vertical: hover height, drop response, terrain, airborne, landing, jump -----------------
    {
        BoxScene flat; flat.floor(0, 2000);
        Rig g(60, false);
        g.useWorldCollision(flat.mesh);
        g.pawn().setForm(game::Form::Vehicle);
        g.idle(1.0);
        double ride = heightAboveGround(g);
        put("vertical.hover_height", ride, "m", "CONFIRMED ORIGINAL", "TnCarForm hover height 185 UU", 1.85);
        g.pawn().setPosition(g.pawn().position() + core::Vec3{0, 1.0f, 0});
        double t0 = g.time();
        g.idle(3.0);
        double lo = 1e9; int cross = 0; double prev = 0; double tFirstMin = -1, tFirstMax = -1; bool first = true;
        for (const Frame& f : g.trace()) {
            if (f.t <= t0) continue;
            double e = f.pos.y - ride;
            if (e < lo) lo = e;
            if (!first && ((prev > 0.01 && e < -0.01) || (prev < -0.01 && e > 0.01))) {
                ++cross;
                if (cross == 1) tFirstMin = f.t - t0; else if (cross == 2) tFirstMax = f.t - t0;
            }
            prev = e; first = false;
        }
        put("vertical.drop1m_undershoot", lo, "m", "UNKNOWN", "lowest height error after release from +1 m (negative = sinks below ride height)");
        put("vertical.drop1m_crossings", cross, "n", "UNKNOWN", "zero crossings of the height error (0 = no spring: snapped or overdamped)");
        put("vertical.drop1m_oscillation_period", (tFirstMin > 0 && tFirstMax > 0) ? 2 * (tFirstMax - tFirstMin) : -1, "s", "UNKNOWN",
            "-1 = no oscillation to measure");
        put("vertical.drop1m_settle", firstAfter(g, t0, [&](const Frame& f) { return std::fabs(f.pos.y - ride) < 0.02; }), "s", "UNKNOWN",
            "time to |error| < 2 cm (damping)");
        put("vertical.body_pitch_roll", 0, "deg", "UNKNOWN",
            "structural: Character::draw rotates the mesh by yaw only - no pitch/roll exists to respond to slopes, bumps or accel");
        save(g, "vp_drop");
    }
    for (Terrain& T : terrains()) {
        if (std::string(T.name) == "flat") continue;
        for (size_t pi = 0; pi < profiles.size(); ++pi) {
            const std::string& P = profiles[pi];
            Rig g(60, false);
            g.useWorldCollision(T.scene.mesh);
            g.pawn().setPosition({0, T.startY, 0});
            g.pawn().groundY = T.startY;
            // Approach in the base mode (HOVER, or BOOST for BOOST/NITRO) until a fixed lead distance
            // before the feature, then trigger DASH/NITRO so the feature is crossed IN the profile.
            g.pawn().setForm(game::Form::Vehicle);
            g.idle(0.6);
            bool boosted = P == "BOOST" || P == "NITRO";
            platform::InputFrame base = boosted ? boostHeld({Button::Forward}) : Rig::down({Button::Forward});
            g.step(base);
            base.pressed[authoredBoostIndex()] = false;
            const float lead = P == "HOVER" ? 8.0f : P == "BOOST" ? 15.0f : P == "DASH" ? 6.0f : 20.0f;
            for (int k = 0; k < 60 * 20 && g.pawn().position().z > kFeatureZ + lead; ++k) g.step(base);
            if ((P == "DASH" || P == "NITRO") && kDash >= 0) {
                platform::InputFrame d = base; d.down[kDash] = true; d.pressed[kDash] = true;
                g.step(d);
            }
            platform::InputFrame hold = base;
            double t0 = g.time();
            double sp0 = hspeed(g.last());
            g.hold(hold, 3.0);
            save(g, "vp_" + std::string(T.name) + "_" + P);
            double maxErr = 0, maxVy = 0, minVy = 0, air = 0, landVy = 0, tLand = -1, tRecover = -1, spMin = 1e9, maxDy = 0;
            bool wasAir = false; double prevY = NAN;
            int below = 0;
            for (const Frame& f : g.trace()) {
                if (f.t <= t0) continue;
                float y; core::Vec3 n;
                double hag = worldCollision(g.world()).groundHeight(f.pos.x, f.pos.z, f.pos.y, 0.5f, y, n) ? f.pos.y - y : NAN;
                if (std::isfinite(hag)) {
                    double e = hag - (f.veh.valid ? f.veh.ride : 1.85);   // vs the state's own ride height
                    if (hag < -0.05) ++below;   // hull origin below the walkable surface under it
                    maxErr = std::max(maxErr, std::fabs(e));
                    if (wasAir && tLand >= 0 && tRecover < 0 && std::fabs(e) < 0.05) tRecover = f.t - t0 - tLand;
                }
                if (std::isfinite(prevY)) maxDy = std::max(maxDy, std::fabs((double)f.pos.y - prevY));
                prevY = f.pos.y;
                maxVy = std::max(maxVy, (double)f.vel.y);
                minVy = std::min(minVy, (double)f.vel.y);
                if (!f.grounded) { air += g.dt(); wasAir = true; landVy = f.vel.y; }
                else if (wasAir && tLand < 0) tLand = f.t - t0;
                spMin = std::min(spMin, (double)hspeed(f));
            }
            std::string pre = std::string(T.name) + "." + P;
            put(pre + ".max_ride_error", maxErr, "m", "UNKNOWN", "max |height above ground - vehicleState().rideHeight| crossing the feature");
            put(pre + ".max_vertical_step", maxDy, "m", "UNKNOWN",
                "largest one-step (1/60 s) change of hull height: ~feature height = the hull snaps to the terrain instantly");
            put(pre + ".vy_range", maxVy - minVy, "m/s", "UNKNOWN", "vertical velocity excursion (0 = the hull follows terrain instantly)");
            put(pre + ".speed_loss", sp0 - spMin, "m/s", "UNKNOWN", "horizontal speed lost crossing the feature");
            if (below == 0)
                r.truth(pre + ".never_below_surface", true, "vehicle origin never sinks below the walkable surface under it");
            else
                r.knownTruth(pre + ".never_below_surface", false, "vehicle origin never sinks below the walkable surface under it", kGameplay,
                             std::to_string(below) + " steps below the surface: Driving keeps its height and passes THROUGH a raised "
                             "surface (0.5 m step) instead of climbing or being blocked");
            if (std::string(T.name) == "ledge") {
                put(pre + ".airtime", air, "s", "UNKNOWN", "time off the ground after driving off a 4 m ledge");
                put(pre + ".landing_vy", landVy, "m/s", "UNKNOWN", "vertical speed on touchdown");
                put(pre + ".landing_recovery", tRecover, "s", "UNKNOWN", "touchdown -> ride height within 5 cm (-1 = never airborne / no recovery)");
            }
        }
    }
    {
        BoxScene flat; flat.floor(0, 2000);
        Rig g(60, false);
        g.useWorldCollision(flat.mesh);
        g.pawn().setForm(game::Form::Vehicle);
        g.idle(1.0);
        double base = g.last().pos.y, t0 = g.time();
        g.step(Rig::press(Button::Jump));
        g.idle(2.5);
        double apex = base, vUp = 0;
        for (const Frame& f : g.trace()) if (f.t > t0) { apex = std::max(apex, (double)f.pos.y); vUp = std::max(vUp, (double)f.vel.y); }
        save(g, "vp_jump");
        put("vertical.jump_launch_vy", vUp, "m/s", "HIGH CONFIDENCE", "HoverTruck JumpLinearSpeed 1200 UU/s (authored; jump behaviour not RE-confirmed)", 12.0);
        put("vertical.jump_apex", apex - base, "m", "UNKNOWN", "apex above hover height");
    }
    if (js) { std::fprintf(js, "\n  ]\n}\n"); std::fclose(js); }
    r.info("weight_is_a_human_call", 0, "",
           "profiles explain WHY the vehicle may feel weightless (instant terrain following, no pitch/roll, symmetric accel/decel, "
           "no hover spring, dead jump); they do not prove weight - compare with an original capture");
}

// =============================================================================================
// FINE AIM PROBE
void checkFineAimProbe(Report& r) {
    r.setGroup("fine_aim_probe");
    const int rmb = authoredBoostIndex();
    auto lateral = [](const Frame& f) {
        // Camera offset from the un-offset orbit position, along the camera's right vector.
        core::Vec3 dir = core::forwardFromYawPitch(f.camYaw, f.camPitch);
        core::Vec3 right = core::normalize(core::cross(core::forwardFromYawPitch(f.camYaw, 0.0f), core::Vec3{0, 1, 0}));
        core::Vec3 base = f.camFocus - dir * core::config::kCamDistance;
        return (double)core::dot(f.camPos - base, right);
    };
    auto dist = [](const Frame& f) { return (double)core::length(f.camPos - f.camFocus); };
    Rig g(60, Models::get() != nullptr);
    g.idle(1.0);
    const Frame before = g.last();
    platform::InputFrame on; on.down[rmb] = true; on.pressed[rmb] = true;
    double tOn = g.time();
    g.step(on);
    g.idle(1.5);
    const Frame aimed = g.last();
    double tOff = g.time();
    g.step(on);   // toggle off
    g.idle(1.5);
    save(g, "fap_toggle");
    r.info("rmb_toggles_state", aimed.fineAim, "", "[CONFIRMED ORIGINAL] RMB press toggles TnFineAimManager (1 = active)", 1.0);
    r.info("fov_normal", before.fov, "deg", "[CONFIRMED ORIGINAL] DefaultFOV 80", 80.0);
    r.info("fov_fine_aim", aimed.fov, "deg", "[CONFIRMED ORIGINAL] TnPCS_FineAim 45", 45.0);
    auto fovT = [&](double t0, double from, double to) {
        return firstAfter(g, t0, [&](const Frame& f) { return std::fabs(f.fov - to) <= 0.1 * std::fabs(to - from); });
    };
    r.info("entry_t90", fovT(tOn, before.fov, aimed.fov), "s",
           "[HIGH CONFIDENCE] FOV 80->45 to 90%: SmoothTime 0.1 (authored); smoothing law not RE-confirmed");
    r.info("exit_t90", fovT(tOff, aimed.fov, before.fov), "s", "[HIGH CONFIDENCE] FOV 45->80 to 90%: DefaultFOV SmoothTime 0.4 (authored)");
    r.info("camera_distance_normal", dist(before), "m", "[CONFIRMED ORIGINAL] orbit 800 UU (+ shoulder offset)");
    r.info("camera_distance_fine_aim", dist(aimed), "m", "[HIGH CONFIDENCE] no generic FineAim orbit override for the Ion Blaster (authored)");
    r.info("camera_lateral_normal", lateral(before), "m", "[UNKNOWN semantics] TnScreenSpaceOffsetByPitch DefaultOffsetCurve (rebuild: rightward offset)");
    r.info("camera_lateral_fine_aim", lateral(aimed), "m", "[UNKNOWN] see FINE-AIM-EVIDENCE-REQUEST.md - neither 0.05 m nor ~2 m is asserted");
    r.info("camera_lateral_shift", lateral(aimed) - lateral(before), "m",
           "[UNKNOWN] shift on entering fine aim; UNRESOLVED - evidence request filed (do not treat as a defect yet)");

    // Movement / look multipliers.
    {
        Rig a(60, false); a.idle(0.3);
        a.hold(Rig::down({Button::Forward}), 1.0);
        double vN = hspeed(a.last());
        Rig b(60, false); b.idle(0.3);
        b.step(on); b.idle(0.3);
        b.hold(Rig::down({Button::Forward}), 1.0);
        r.info("movement_multiplier", vN > 0 ? hspeed(b.last()) / vN : -1, "", "[CONFIRMED ORIGINAL] SetSpeedMultiplier(0.5)", 0.5);
        Rig c(60, false); c.idle(0.3);
        platform::InputFrame look; look.mouseDX = 100;
        float y0 = c.controller().camYaw(); c.step(look); float dN = c.controller().camYaw() - y0;
        Rig d(60, false); d.idle(0.3); d.step(on); d.idle(0.3);
        float y1 = d.controller().camYaw(); d.step(look); float dF = d.controller().camYaw() - y1;
        r.info("look_multiplier", std::fabs(dN) > 1e-6f ? dF / dN : -1, "", "[CONFIRMED ORIGINAL] look speed 25/12.5 vs 50/25", 0.5);
    }
    r.info("spread_multiplier", kFineAimSpreadMult, "",
           "[CONFIRMED ORIGINAL] FineAimSpreadModifier 0.5 - applied in World::fireHitscan (not linked here: code constant shown)", 0.5);
    // Reload interruption and resume.
    {
        Rig g2(60, Models::get() != nullptr);
        g2.idle(0.3);
        g2.step(on); g2.idle(0.5);
        g2.hold(Rig::down({Button::Fire}), 0.5);
        g2.tap(Button::Reload);
        bool offDuring = false, back = false;
        double tR = g2.time();
        g2.idle(4.0);
        for (const Frame& f : g2.trace()) {
            if (f.t <= tR) continue;
            if (f.reloading && f.fineAim == 0) offDuring = true;
            if (offDuring && !f.reloading && f.fineAim == 1) back = true;
        }
        save(g2, "fap_reload");
        r.confTruth("reload_interrupts", offDuring, "CanFineAim false while reloading [CONF bytecode]", kGameplay);
        r.confTruth("reload_resumes", back, "fine aim resumes while still wanted after the reload [CONF bytecode]", kGameplay);
    }
    // Transform cancels (vehicle form cannot fine aim).
    {
        Rig g3(60, Models::get() != nullptr);
        g3.idle(0.3);
        g3.step(on); g3.idle(0.5);
        g3.step(Rig::press(Button::Transform));
        g3.idle(0.3);
        bool cancelled = g3.last().fineAim == 0;
        g3.idle(3.0);
        save(g3, "fap_transform");
        r.confTruth("transform_cancels", cancelled, "CanFineAim false in vehicle states [CONF bytecode]", kGameplay);
        r.info("fov_after_transform", g3.last().fov, "deg", "FOV back to the default after the fold", 80.0);
    }
    // Start/end audio: code-level presence of the authored cues in the tree under test (runtime
    // playback is checked by audio-attach.ps1, scenario fine_aim).
    {
        std::string inc;
        bool has = readFileText("src/game/SoundCues.inc", inc) && inc.find("FINE_AIM_START") != std::string::npos &&
                   inc.find("FINE_AIM_END") != std::string::npos;
        std::string world;
        bool wired = readFileText("src/game/World.cpp", world) && world.find("FINE_AIM_START") != std::string::npos;
        r.confTruth("start_end_audio_wired", has && wired,
                    "RE TARGETED_PASS #3: StartFineAim/StopFineAim -> WP_StartFineAim (BL_WPN_GUN_PULSE_RIFLE.FINE_AIM_START/END)", kSystems,
                    std::string("cue table ") + (has ? "has" : "lacks") + " FINE_AIM_START/END; World.cpp " + (wired ? "plays" : "does not play") +
                        " them (source scan; run from the worktree root)");
    }
    r.info("reticle_hud", -1, "", "[UNKNOWN] no HUD exists; the reticle asset relationship is not recovered - nothing asserted");
}

} // namespace fid
