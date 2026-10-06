// WFC fidelity harness: native-RE Milestone 03 assertions (RE-Workspace notes/
// MILESTONE03_VEHICLE_NATIVE_FIDELITY.md, P1..P10). Independent measurements through the PRODUCTION
// PlayerController::applyToPawn path on synthetic terrain (world collision), not the product's own
// WFC_VEHTEST. Only native-CONFIRMED behaviour (or a direct consequence of confirmed algorithm +
// constants) is asserted with conf(); items still PROVISIONAL in the native report are INFO and
// explicitly labelled "not promoted".
#include "CheckUtil.h"
#include "core/Config.h"
#include "game/ChassisDef.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace fid {

using platform::Button;
using namespace core::config;

namespace {
const char* kP1 = "native M03 P1 (TnHoverCarSimulation suspension: 4 COM probes, implicit springs, no ride-height target)";
const char* kP2 = "native M03 P2 (grounded attitude = spring forces; yaw = camera each tick)";
const char* kP3 = "native M03 P3 (hover local velocity -> stick*1500, one ClampLength 3000 UU/s^2)";
const char* kP4 = "native M03 P4 (Hovering.Jump +1200 world Z additive, local ang (0,-1,0); Driving.Jump local (600,0,1400))";
const char* kP5 = "native M03 P5 (truck dash: body-local (1,0,0), mask (1,1,1), 100000 UU/s^2, exit snap fwd 1500)";
const char* kP6 = "native M03 P6 (TnScreenSpaceOffsetByPitch: orbit-space offset, X toward pawn; fine aim X 150 -> -50)";
const char* kP7 = "native M03 P7 (HandSkelControl R_Arm04_Hand_XB scale 0.1, binary, every tick)";
const char* kP8 = "native M03 P8 (RammedReaction: dir*5000 + base for 0.5 s, then (0,0,baseZ); vehicle AddVelocity x0.5)";
constexpr double kRestCom = 2.5 - 19.404 * (2500.0 / 4.0) / 10000.0;   // 1.28725 m (mass link HIGH)
template <class B> constexpr auto dashIdx(int) -> decltype(B::Dash, int()) { return (int)B::Dash; }
template <class B> constexpr int dashIdx(long) { return -1; }
constexpr int kDashBtn = dashIdx<Button>(0);
double deg(double r) { return r * 57.29578; }
core::Vec3 fwdOf(float yaw) { return core::forwardFromYawPitch(yaw, 0.0f); }
core::Vec3 rightOf(float yaw) { return core::normalize(core::cross(fwdOf(yaw), core::Vec3{0, 1, 0})); }

// Vehicle at rest on `mesh`, settled.
void vehicleOn(Rig& g, const render::MeshData& mesh, core::Vec3 at = {0, 0, 0}) {
    g.useWorldCollision(mesh);
    g.pawn().setPosition(at);
    g.pawn().groundY = at.y;
    g.pawn().setForm(game::Form::Vehicle);
    g.idle(2.5);
}
} // namespace

void checkNativeVehicle(Report& r) {
    r.setGroup("native_vehicle");
    bool attValid = false;

    // ---- REST: separate quantities (never one "hover height") -------------------------------------
    {
        BoxScene s; s.floor(0, 400);
        Rig g(60, Models::get() != nullptr);
        vehicleOn(g, s.mesh);
        const Frame f = g.last();
        r.conf("rest.com_height", f.comH, kRestCom, 0.02, "m", std::string(kP1) + "; L_eq = 250 - 1940.4*(M/4)/10000 UU, mass link HIGH",
               kGameplay, "centre-of-mass height above level ground at rest");
        if (f.att.valid) {
            r.conf("rest.probe_contacts", f.att.contacts, 4, 0, "probes", std::string(kP1) + ": 4 body-down probes", kGameplay);
            r.conf("rest.spring_length", f.att.springMean, kRestCom, 0.02, "m", std::string(kP1) +
                   ": probes sit on the COM plane, so the equilibrium spring length is L_eq", kGameplay, "mean TnSpring length (rebuild state)");
        }
        r.info("rest.spring_rest_length_native", 2.5, "m", "HoverTruck_Suspension RestingLength 250 UU = probe ray length (CONFIRMED data, not a height)");
        r.info("rest.probe_radius_native", 1.85, "m", "SuspensionRadius 185 UU: horizontal probe radius around the COM (+-130.8 UU square)");
        r.info("rest.pawn_origin_height", f.pos.y, "m", "rebuild body-origin height (implementation quantity, no native counterpart)");
        if (f.drawnModel >= 0)
            r.info("rest.visual_mesh_clearance", f.bbMin.y, "m", "lowest drawn vehicle mesh point (1st percentile) above the floor - visual only, no native assertion");
        r.info("rest.hull_clearance", -1, "", "PROVISIONAL - not promoted: hull/chassis contact clearance (PhysicalVehicleMesh hull not recovered)");
    }
    // ---- Push-only springs + rigid-body gravity -----------------------------------------------------
    for (float lift : {1.6f, 0.8f}) {
        BoxScene s; s.floor(0, 400);
        Rig g(60, false);
        vehicleOn(g, s.mesh);
        g.pawn().setPosition(g.pawn().position() + core::Vec3{0, lift, 0});
        g.pawn().velocity() = core::Vec3{0, 0, 0};
        g.step(platform::InputFrame{});
        const Frame a = g.last();
        g.step(platform::InputFrame{});
        const Frame b = g.last();
        double acc = (b.vel.y - a.vel.y) / g.dt();
        if (lift > 1.0f) {   // COM 2.89 m: beyond the 2.5 m probe rays -> no contact, pure RB gravity
            r.conf("free_fall.contacts", b.att.valid ? b.att.contacts : -1, 0, 0, "probes", std::string(kP1) + ": rays of RestingLength 250 UU", kGameplay,
                   "probes beyond reach after a 1.6 m lift");
            r.conf("free_fall.rb_gravity", acc, -19.404, 0.15, "m/s2", "native M03 shared constants: RB gravity -2940 x 0.66 = -1940.4 UU/s^2; "
                   "zero RB linear damping in Hovering (P1)", kGameplay, "vertical acceleration with no probe contact");
        } else {             // COM 2.09 m: springs extended past rest, inside reach -> push less than weight, never pull
            r.confTruth("push_only.no_pull", acc >= -19.404 - 0.15, std::string(kP1) + ": force max(0, ...) along body-down", kGameplay,
                        "vertical acceleration " + std::to_string(acc) + " m/s^2 with the springs extended (pull would exceed gravity)");
            r.info("push_only.accel_extended", acc, "m/s2", "between -19.404 (no push) and 0 (full weight) while extended");
        }
    }
    // ---- Stationary hover jump + airborne-only upright correction ---------------------------------------
    {
        BoxScene s; s.floor(0, 400);
        Rig g(60, false);
        vehicleOn(g, s.mesh);
        Frame before = g.last();
        g.step(Rig::press(Button::Jump));
        Frame after = g.last();
        g.idle(3.0);
        save(g, "nv_hover_jump_stationary");
        r.conf("hover_jump_stationary.dvz", after.vel.y - before.vel.y, 12.0, 0.4, "m/s", kP4, kGameplay, "vertical velocity change from rest");
        r.conf("hover_jump_stationary.horizontal_kept", hspeed(after), 0.0, 0.05, "m/s", kP4, kGameplay, "no horizontal velocity from a stationary hop");
        // Airborne / inverted (ShouldUpright): UpdateTurn's pitch / roll pull is 0.05 x error / dt - 5% of the error per tick, not a
        // one-step snap [RE TARGETED_PASS4 §A4 as corrected by RE 5448755 / 6bb8855; Gameplay Pass 24m].
        std::vector<double> airPitch;
        for (const Frame& f : g.trace()) if (f.t > after.t && f.att.valid && f.att.contacts == 0) airPitch.push_back(f.att.pitch);
        double ratio = -1; int nr = 0; double acc = 0;
        for (size_t k = 4; k + 2 < airPitch.size(); k += 2)
            if (std::fabs(airPitch[k]) > 0.004) { acc += airPitch[k + 2] / airPitch[k]; ++nr; }
        if (nr) ratio = acc / nr;
        if (nr)
            r.conf("upright.airborne_ratio_per_tick", ratio, 0.95, 0.03, "", "RE TARGETED_PASS4 §A4 (corrected): airborne / inverted only, 5% of the "
                   "pitch / roll error per tick", kGameplay, "pitch ratio per 1/30 s while no probe touches");
        else r.confTruth("upright.airborne_ratio_per_tick", false, kP4, kGameplay, "no airborne pitch error left to measure a ratio: a one-step snap (pre-Pass-24m reading of §A4) reads like this");
        r.info("upright.grounded", 0, "", "not a measurement: grounded and upright, UpdateTurn replaces only yaw (mask 0,0,1); pitch / roll come from the springs (§A4 corrected)");
        r.info("hover_lean.anim_blend", -1, "",
               "TnAccelerationAnimBlend velocity-driven lean (ClampLength(v,2000)/2000 x max(0,up.Z)): animation weights are not exposed to "
               "the harness - code-level only (Gameplay Pass 14 provenance)");
    }

    // ---- Rest + steps (0.25 m / 0.5 m) at hover cruise -----------------------------------------
    for (float h : {0.25f, 0.5f}) {
        std::string id = h < 0.3f ? "step_025" : "step_050";
        BoxScene s; s.floor(0, 800); s.box({-40, 0, -600}, {40, h, -60});   // raised slab from z=-60 forward
        Rig g(60, false);
        vehicleOn(g, s.mesh);
        attValid = g.last().att.valid;
        g.hold(Rig::down({Button::Forward}), 6.0);   // reaches 15 m/s, crosses the riser at z=-60 after ~4.5 s
        save(g, "nv_" + id);
        double maxDy = 0, comMin = 1e9, comMax = -1e9, pitchMax = 0, rollMax = 0;
        const Frame* prev = nullptr;
        double tCross = -1;
        for (const Frame& f : g.trace()) {
            if (f.t < 2.6) { prev = &f; continue; }
            if (tCross < 0 && f.pos.z < -60.0f) tCross = f.t;
            if (prev && prev->comH >= 0 && f.comH >= 0)
                maxDy = std::max(maxDy, (double)std::fabs(f.comY - prev->comY));   // world COM height change per step
            if (tCross > 0 && f.t < tCross + 1.5) {
                double yw = f.comY;
                comMin = std::min(comMin, yw); comMax = std::max(comMax, yw);
                if (f.att.valid) { pitchMax = std::max(pitchMax, std::fabs(deg(f.att.pitch))); rollMax = std::max(rollMax, std::fabs(deg(f.att.roll))); }
            }
            prev = &f;
        }
        const Frame& last = g.last();
        r.confTruth(id + ".no_one_frame_snap", tCross > 0 && maxDy < 0.5 * h, kP1, kGameplay,
                    "largest one-step (1/60 s) COM height change " + std::to_string(maxDy) + " m crossing a " + std::to_string(h) +
                        " m riser (a snap moves the full riser height in one step)");
        // §A4 corrected (RE 5448755 / 6bb8855): grounded pitch / roll angular velocity carries over (only yaw is replaced), so a riser
        // pitches the hull - RE pitch-plane estimate: car 2.6-2.8 deg nose-up at 1500 UU/s on 12.5 UU, ~5.5 deg on 25 UU; truck 2.0 / 4.0.
        r.confTruth(id + ".attitude_follows_terrain", attValid && pitchMax > 0.5, kP2, kGameplay,
                    attValid ? "max |pitch| " + std::to_string(pitchMax) + " deg, |roll| " + std::to_string(rollMax) + " deg crossing the riser"
                             : std::string("no rigid-body attitude in this build"));
        r.conf(id + ".rest_com_on_top", last.comH, kRestCom, 0.03, "m", std::string(kP1) + "; L_eq 128.7 UU (mass link HIGH)", kGameplay,
               "COM height above the slab after crossing");
        r.info(id + ".com_range_world", comMax - comMin, "m", "COM world-height excursion in the 1.5 s after the riser (spring response)");
        r.info(id + ".pitch_max_deg", pitchMax, "deg", "peak |pitch| over the riser (emerges from the springs; magnitude is a human check)");
    }

    // ---- Large drop (10 m ledge) -----------------------------------------------------------------
    {
        BoxScene s; s.floor(0, 800); s.box({-40, 0, -60}, {40, 10.0f, 60});   // 10 m high platform, edge at z=-60
        Rig g(60, false);
        vehicleOn(g, s.mesh, {0, 10.0f, 0});
        g.hold(Rig::down({Button::Forward}), 9.0);
        save(g, "nv_drop10");
        double vImpact = 0, comMin = 1e9, tLand = -1, tSettle = -1, tMin = -1;
        bool air = false;
        std::vector<const Frame*> landed;
        for (const Frame& f : g.trace()) {
            if (f.pos.z > -61.0f) continue;
            if (f.att.valid && f.att.contacts == 0) air = true;
            if (air && f.vel.y < vImpact) vImpact = f.vel.y;
            if (air && f.att.valid && f.att.contacts > 0 && tLand < 0) tLand = f.t;
            if (tLand > 0 && f.comH >= 0) {
                landed.push_back(&f);
                if (f.comH < comMin) { comMin = f.comH; tMin = f.t; }
            }
        }
        // Settle = after the compression minimum (first contact is ~a spring length above the slab and the COM falls THROUGH the
        // rest height on the way down - Gameplay WFC_DROPTEST 2026-10-06), and |COM - rest| < 3 cm held for 0.25 s.
        for (size_t i = 0; i < landed.size() && tSettle < 0; ++i) {
            if (landed[i]->t < tMin) continue;
            bool held = true; size_t j = i;
            for (; j < landed.size() && landed[j]->t <= landed[i]->t + 0.25; ++j) if (std::fabs(landed[j]->comH - kRestCom) >= 0.03) { held = false; break; }
            if (held && j < landed.size()) tSettle = landed[i]->t - tLand;
        }
        r.info("drop10.impact_vy", vImpact, "m/s", "vertical speed at touchdown (RB gravity 19.404 m/s^2, terminal 35 m/s)");
        r.info("drop10.min_com_height", comMin, "m",
               "PROVISIONAL - not promoted: hull/chassis contact (min clearance, ceiling probe) is not recovered natively");
        r.confTruth("drop10.recovery_not_instant", tSettle < 0 || tSettle > 0.2, kP1, kGameplay,
                    "touchdown -> COM settled within 3 cm (after the compression minimum, held 0.25 s) took " + std::to_string(tSettle) + " s (springs; 1 step = a snap; Gameplay WFC_DROPTEST ~1.3-1.6 s)");
        r.conf("drop10.settles_to_rest", g.last().comH, kRestCom, 0.03, "m", kP1, kGameplay, "COM height long after landing");
        r.info("drop10.recovery_time", tSettle, "s", "touchdown -> COM within 3 cm of rest (human check: landing weight)");
    }

    // ---- Forward / lateral release from 15 m/s (P3: 3000 UU/s^2 -> 0.5 s) ------------------------
    for (int lat = 0; lat < 2; ++lat) {
        BoxScene s; s.floor(0, 2000);
        Rig g(60, false);
        vehicleOn(g, s.mesh);
        g.hold(Rig::down({lat ? Button::Right : Button::Forward}), 2.0);
        double v0 = hspeed(g.last()), t0 = g.time();
        g.idle(1.5);
        save(g, lat ? "nv_release_lat" : "nv_release_fwd");
        double tStop = firstAfter(g, t0, [](const Frame& f) { return hspeed(f) < 0.05f; });
        std::string id = lat ? "release_lateral" : "release_forward";
        r.info(id + ".start_speed", v0, "m/s", "", 15.0);
        r.conf(id + ".time_to_stop", tStop, 0.5, 1.5 / 60.0, "s", std::string(kP3) + ": 15 m/s / 30 m/s^2", kGameplay,
               lat ? "sideways stop uses the same single clamp as forward" : "coast-down");
    }

    // ---- Camera yaw change while moving (heading = camera each tick; velocity re-aims at 3000) ---
    {
        BoxScene s; s.floor(0, 2000);
        Rig g(60, false);
        vehicleOn(g, s.mesh);
        g.hold(Rig::down({Button::Forward}), 2.0);
        platform::InputFrame turn = Rig::down({Button::Forward});
        turn.mouseDX = (float)(core::PI * 0.5 / kMouseSens);
        double t0 = g.time();
        g.step(turn);
        g.hold(Rig::down({Button::Forward}), 1.5);
        save(g, "nv_camera_turn");
        const auto& tr = g.trace();
        size_t i0 = 0; while (i0 < tr.size() && tr[i0].t <= t0) ++i0;
        // Native P2: UpdateTurn's frame of reference is the CAMERA rotation yaw (after the camera behaviours),
        // so compare the body heading with the rendered view yaw, not the raw input yaw.
        double errMax = 0;
        for (size_t k = i0; k < tr.size(); ++k)
            errMax = std::max(errMax, std::fabs(deg(std::remainder((double)tr[k].yaw - tr[k].viewYaw, 2 * core::PI))));
        double err2 = errMax;
        double viewT90 = firstAfter(g, t0, [](const Frame& f) {
            return std::fabs(deg(std::remainder((double)f.viewYaw - f.camYaw, 2 * core::PI))) < 9.0; });
        double tAlign = firstAfter(g, t0, [](const Frame& f) {
            core::Vec3 want = fwdOf(f.viewYaw) * 15.0f;
            core::Vec3 v{f.vel.x, 0, f.vel.z};
            return core::length(v - want) < 0.5f; });
        r.conf("camera_turn.heading_vs_view_yaw", err2, 0.0, 0.5, "deg", kP2, kGameplay,
               "max |body heading - rendered camera yaw| after a 90 deg camera input step");
        r.info("camera_turn.view_yaw_t90", viewT90, "s",
               "rendered camera yaw reaches 90% of the input step (vehicle camera rotation smoothing; human check: vehicle camera)");
        r.info("camera_turn.velocity_realign_time", tAlign, "s",
               std::string(kP3) + ": with an instant camera |dv| = 21.2 m/s at 30 m/s^2 = 0.707 s; the vehicle camera rotation "
               "smoothing adds to it (time until velocity is within 0.5 m/s of view-forward x 15; human check: sideways inertia)",
               std::sqrt(2.0) * 15.0 / 30.0);
    }

    // ---- Hover jump (P4) ---------------------------------------------------------------------------
    {
        BoxScene s; s.floor(0, 2000);
        Rig g(60, false);
        vehicleOn(g, s.mesh);
        g.hold(Rig::down({Button::Forward}), 2.0);
        Frame before = g.last();
        platform::InputFrame j = Rig::down({Button::Forward});
        j.pressed[(int)Button::Jump] = true;
        g.step(j);
        Frame after = g.last();
        double rest = before.comH;
        g.hold(Rig::down({Button::Forward}), 3.0);
        save(g, "nv_hover_jump");
        double apex = -1e9;
        for (const Frame& f : g.trace()) if (f.t > before.t) apex = std::max(apex, (double)f.comH);
        r.conf("hover_jump.dvz", after.vel.y - before.vel.y, 12.0, 0.4, "m/s", kP4, kGameplay,
               "vertical velocity change on the jump step (additive +1200 UU/s; one step of gravity/springs inside tolerance)");
        r.conf("hover_jump.horizontal_kept", hspeed(after) - hspeed(before), 0.0, 0.3, "m/s", kP4, kGameplay,
               "horizontal speed change on the jump step (world-Z impulse only)");
        if (after.att.valid)
        if (after.att.valid)   // UpdateJumping's angular kick is not masked on the ground: about -1 rad/s right after the jump [§A4 corrected]
            r.conf("hover_jump.pitch_kick", after.att.angVel.y - before.att.angVel.y, -1.0, 0.25, "rad/s", kP4, kGameplay,
                   "local pitch-axis angular velocity change (JumpAngularSpeed 1.0, local -Y = nose up)");
        r.info("hover_jump.apex_above_rest", apex - rest, "m", "ballistic 1200^2/(2*1940.4) = 3.71 m plus the spring push (native note)", 3.71);
    }

    // ---- JET TURN SERVO: TnHoverPlaneSimulation / TnPlaneSimulation turn toward the target rotation at TurnRate (roll 0.1,
    // pitch 0.5, yaw 0.5) per call; the sim runs once per 30 Hz game tick [CONF RE pass 4 A4 addendum 59eac82]. So the remaining
    // error after each 1/30 s is x0.5 (yaw, pitch) and x0.9 (roll), independent of the rebuild's 60 Hz step. Real Jet4 chassis data.
    {
        static game::ChassisDef jet;
        static const bool jetOk = game::loadChassisDef(Models::assetRoot(), "Jet4", jet);
        if (!jetOk || jet.vehicle.form != game::VehicleFormType::Jet) {
            r.skip("jet_servo", jetOk ? "Jet4 chassis is not a Jet vehicle form in this export" : "Jet4 chassis definition unavailable (assets)");
        } else {
            BoxScene s; s.floor(0, 400);
            Rig g(60, false);
            g.pawn().setChassis(&jet);
            vehicleOn(g, s.mesh, {0, 3.0f, 0});
            g.idle(1.0);
            auto& vs = g.pawn().vehicleState();
            // ratio of the remaining error after each 1/30 s (two 60 Hz steps), averaged while the error is measurable
            auto ratio = [&](auto err, auto kick) {
                kick();
                std::vector<double> e;
                for (int k = 0; k < 16; ++k) { e.push_back(err()); g.step(platform::InputFrame{}); }
                double acc = 0; int n = 0;
                for (size_t k = 0; k + 2 < e.size(); k += 2) if (std::fabs(e[k]) > 1e-3) { acc += e[k + 2] / e[k]; ++n; }
                return n ? acc / n : -1.0;
            };
            const float yaw0 = g.pawn().yaw();
            const double ry = ratio([&] { return (double)std::remainder(yaw0 + 0.5f - g.pawn().yaw(), 6.2831853f); },
                                    [&] { g.controller().setCameraYaw(yaw0 + 0.5f); });
            r.conf("jet_servo.yaw_ratio_per_30hz", ry, 0.5, 0.03, "", "RE pass 4 A4 addendum 59eac82: TurnRate yaw 0.5 per 30 Hz script tick",
                   kGameplay, "remaining yaw error after each 1/30 s for a 0.5 rad view step (pre-Pass-24n 60 Hz per-step application reads 0.25)");
            g.idle(1.0);
            const double rp = ratio([&] { return (double)(0.3f - vs.pitch); }, [&] { g.controller().setCameraPitch(0.3f); });
            r.conf("jet_servo.pitch_ratio_per_30hz", rp, 0.5, 0.03, "", "RE pass 4 A4 addendum 59eac82: TurnRate pitch 0.5 per 30 Hz script tick",
                   kGameplay, "remaining pitch error after each 1/30 s for a 0.3 rad view pitch step");
            g.controller().setCameraPitch(0.0f); g.idle(1.0);
            const double rr = ratio([&] { return (double)std::remainder(vs.roll, 6.2831853f); }, [&] { vs.roll = 0.3f; });
            r.conf("jet_servo.roll_ratio_per_30hz", rr, 0.9, 0.03, "", "RE pass 4 A4 addendum 59eac82: TurnRate roll 0.1 per 30 Hz script tick",
                   kGameplay, "remaining roll error after each 1/30 s (hover target roll 0) from a 0.3 rad roll");
        }
    }

    // ---- Boost (Driving) jump (P4) -------------------------------------------------------------------
    {
        BoxScene s; s.floor(0, 2000);
        Rig g(60, false);
        vehicleOn(g, s.mesh);
        g.hold(Rig::down({Button::Forward}), 1.0);
        g.hold(boostHeld({Button::Forward}), 0.5);
        Frame before = g.last();
        platform::InputFrame j = boostHeld({Button::Forward});
        j.pressed[authoredBoostIndex()] = false;
        j.pressed[(int)Button::Jump] = true;
        g.step(j);
        Frame after = g.last();
        g.hold(boostHeld({Button::Forward}), 2.0);
        save(g, "nv_boost_jump");
        core::Vec3 dv = after.vel - before.vel;
        r.info("boost_jump.driving_before", before.veh.valid && before.veh.driving ? 1 : 0, "", "vehicle in Driving when the jump is pressed", 1);
        r.conf("boost_jump.dv_up", dv.y, 14.0, 0.6, "m/s", kP4, kGameplay, "local (600,0,1400): +14 up (level body; one step of gravity in tolerance)");
        r.conf("boost_jump.dv_forward", core::dot(dv, fwdOf(before.yaw)), 6.0, 0.6, "m/s", kP4, kGameplay, "local (600,0,1400): +6 forward");
        if (after.att.valid)
            r.info("boost_jump.pitch_rate", after.att.angVel.y, "rad/s", "native local ang (0,-2,0) before air damping (Gameplay measures ~1.8)", -2.0);
    }

    // ---- Hover Dash with contradictory stick input (P5) ---------------------------------------------
    if (kDashBtn >= 0) {
        BoxScene s; s.floor(0, 2000);
        Rig g(60, false);
        vehicleOn(g, s.mesh);
        g.hold(Rig::down({Button::Forward}), 1.5);
        platform::InputFrame d = Rig::down({Button::Right});   // stick says RIGHT
        d.down[kDashBtn] = true; d.pressed[kDashBtn] = true;
        double t0 = g.time();
        g.step(d);
        g.hold(Rig::down({Button::Right}), 1.0);
        save(g, "nv_dash_contradictory");
        double fwdDuring = 0, latDuring = 0, vertDuring = 0; int n = 0;
        double fwdExit = -1, latExit = -1; bool wasDash = false;
        for (const Frame& f : g.trace()) {
            if (f.t <= t0) continue;
            core::Vec3 fw = fwdOf(f.yaw), rt = rightOf(f.yaw);
            bool on = f.veh.valid && f.veh.dash > 0;
            if (on && f.t > t0 + 2.5 / 60.0) {
                fwdDuring += core::dot(f.vel, fw); latDuring = std::max(latDuring, (double)std::fabs(core::dot(f.vel, rt)));
                vertDuring = std::max(vertDuring, (double)std::fabs(f.vel.y)); ++n;
            }
            if (wasDash && !on && fwdExit < 0) { fwdExit = core::dot(f.vel, fw); latExit = std::fabs(core::dot(f.vel, rt)); }
            wasDash = on;
        }
        r.conf("dash.forward_speed", n ? fwdDuring / n : 0, 30.0, 0.6, "m/s", kP5, kGameplay, "mean body-forward speed during the dash with RIGHT held");
        r.conf("dash.lateral_zeroed", latDuring, 0.0, 0.5, "m/s", kP5, kGameplay, "max sideways speed during the dash (stick ignored, mask (1,1,1))");
        r.conf("dash.vertical_zeroed", vertDuring, 0.0, 0.5, "m/s", kP5, kGameplay, "max vertical speed during the dash on flat ground");
        r.conf("dash.exit_forward", fwdExit, 15.0, 0.6, "m/s", kP5, kGameplay, "body-forward speed on the step the dash ends (snap to 1500)");
        r.conf("dash.exit_lateral", latExit, 0.0, 0.5, "m/s", kP5, kGameplay, "sideways speed on the exit step");
    } else {
        r.skip("dash", "build has no Dash input");
    }

    // ---- Boost top speed on collision (CONFIRMED MaxSpeed 3000; tire model coefficient PROV) ------------
    {
        BoxScene s; s.floor(0, 4000);
        Rig g(60, false);
        vehicleOn(g, s.mesh);
        g.hold(Rig::down({Button::Forward}), 1.0);
        double tb = g.time();
        g.hold(boostHeld({Button::Forward}), 6.0);
        save(g, "nv_boost_top");
        double peak = 0, at1 = -1;
        for (const Frame& f : g.trace()) {
            peak = std::max(peak, (double)hspeed(f));
            if (at1 < 0 && f.t >= tb + 1.0) at1 = hspeed(f);
        }
        r.conf("boost.top_speed_6s", peak, 30.0, 0.5, "m/s", "RE TARGETED_PASS2 #2 (Driving, Truck MaxSpeed 3000) [CONFIRMED]", kGameplay,
               "peak speed over 6 s of boost on flat collision ground");
        r.info("boost.speed_at_1s", at1, "m/s", "speed 1 s into boost (acceleration curve; tire coefficient PROVISIONAL)");
        if (kDashBtn >= 0) {   // Nitro = Dash while Driving at the cap: x1.5 -> 45 m/s (RE TARGETED_PASS2 #2)
            platform::InputFrame n = boostHeld({Button::Forward});
            n.pressed[authoredBoostIndex()] = false;
            n.down[kDashBtn] = true; n.pressed[kDashBtn] = true;
            double tn = g.time();
            g.step(n);
            platform::InputFrame b = boostHeld({Button::Forward}); b.pressed[authoredBoostIndex()] = false;
            g.hold(b, 3.5);
            double np = 0; for (const Frame& f : g.trace()) if (f.t > tn) np = std::max(np, (double)hspeed(f));
            r.info("nitro.peak_after_full_boost", np, "m/s",
                   "RE TARGETED_PASS2 #2: nitro speed cap x1.5 = 45 m/s [CONFIRMED]; whether the 3 s nitro reaches the cap depends on the "
                   "Driving acceleration near the top (PROVISIONAL tire model) - not asserted", 45.0);
        }
    }

    // ---- Provisional (NOT promoted) --------------------------------------------------------------------
    r.info("provisional.hull_contact", -1, "", "PROVISIONAL - not promoted: vehicle hull contact / ceiling probe (PhysicalVehicleMesh hull not recovered)");
    r.info("provisional.boost_tire_coefficient", -1, "", "PROVISIONAL - not promoted: TnTire lateral coefficient (cap 2(M/4)|g| is native)");
    r.info("provisional.wheel_steering", -1, "", "PROVISIONAL - not promoted: Driving wheel steering behaviour");
    r.info("provisional.camera_curve_tangents", -1, "",
           "PROVISIONAL - not promoted: CurveAutoClamped tangent rule between keys; fine-aim checks use level pitch (a key) only");
}

// ---------------------------------------------------------------------------------------------------
// Fine Aim camera (P6), hand shrink (P7), ram reactions (P8)
void checkNativeRobot(Report& r) {
    r.setGroup("native_robot");
    const int rmb = authoredBoostIndex();
    {   // Fine aim at level pitch: X key 150 -> -50 (toward the pawn along the view axis) = 2.0 m farther back.
        Rig g(60, Models::get() != nullptr);
        BoxScene fl; fl.floor(0, 200);
        g.useWorldCollision(fl.mesh);
        g.controller().setCameraPitch(0.0f);
        g.idle(1.5);
        Frame a = g.last();
        platform::InputFrame on; on.down[rmb] = true; on.pressed[rmb] = true;
        g.step(on);
        g.idle(1.5);
        Frame b = g.last();
        save(g, "nr_fine_aim");
        core::Vec3 fwd = core::forwardFromYawPitch(a.camYaw, a.camPitch), rt = rightOf(a.camYaw);
        core::Vec3 d = b.camPos - a.camPos;   // the pawn does not move: camera displacement = offset change
        r.conf("fine_aim.camera_back_shift", -core::dot(d, fwd), 2.0, 0.1, "m", kP6, kGameplay,
               "camera moves back along the view axis at level pitch (X 150 toward pawn -> -50 = 200 UU farther)");
        r.conf("fine_aim.camera_lateral_shift", core::dot(d, rt), 0.0, 0.05, "m", kP6, kGameplay, "Y stays 300 in both rows: no lateral shift");
        r.info("fine_aim.camera_vertical_shift", d.y, "m", "Z key -35 in both rows at level pitch (expect ~0)", 0.0);
        r.conf("fine_aim.fov", b.fov, 45.0, 0.2, "deg", "TnPCS_FineAim FOV 45 [CONFIRMED]", kGameplay);
        r.info("fine_aim.non_level_pitch", -1, "",
               "PROVISIONAL - not promoted: offsets between curve keys depend on the CurveAutoClamped tangent rule");
    }
    {   // Hand shrink: binary, = weapon attached and drawn (Ion Blaster IsHandRequired false).
        Rig g(60, Models::get() != nullptr);
        BoxScene fl; fl.floor(0, 200);
        g.useWorldCollision(fl.mesh);
        g.idle(0.5);
        g.step(Rig::press(Button::Transform));
        g.idle(3.0);
        g.step(Rig::press(Button::Transform));
        g.idle(2.5);
        save(g, "nr_hand");
        int known = 0, mismatch = 0, shrunkIdle = 0, robotSteps = 0;
        for (const Frame& f : g.trace()) {
            if (f.hand < 0) continue;
            ++known;
            if (f.form == game::Form::Robot) {
                ++robotSteps;
                if ((f.hand > 0) != f.weaponVisible) ++mismatch;
                if (!f.transforming && f.hand > 0) ++shrunkIdle;
            }
        }
        r.confTruth("hand_shrink.matches_drawn_weapon", known > 0 && mismatch == 0, kP7, kGameplay,
                    known ? std::to_string(mismatch) + " of " + std::to_string(robotSteps) + " robot steps where hand shrink != weapon attached+drawn"
                          : std::string("no hand-shrink state in this build"));
        r.confTruth("hand_shrink.on_with_gun", shrunkIdle > 0, kP7, kGameplay, "hand scaled 0.1 while the gun is drawn in robot form");
    }
    {   // Robot RammedReaction: dir*5000 + base for 0.5 s, then horizontal cancelled.
        Rig g(60, Models::get() != nullptr);
        BoxScene fl; fl.floor(0, 400);
        g.useWorldCollision(fl.mesh);
        g.idle(0.5);
        core::Vec3 dir{1, 0, 0};
        bool has = layer::ramRobot(g.pawn(), dir, 0);
        if (!has) {
            r.skip("ram", "build has no robot RammedReaction");
        } else {
            double t0 = g.time();
            g.idle(1.2);
            save(g, "nr_ram");
            double hMid = -1, hAfter = -1;
            for (const Frame& f : g.trace()) {
                if (hMid < 0 && f.t >= t0 + 0.25) hMid = hspeed(f);
                if (hAfter < 0 && f.t >= t0 + 0.55) hAfter = hspeed(f);
            }
            r.conf("ram.robot_speed_during", hMid, 50.0, 1.0, "m/s", kP8, kGameplay, "horizontal speed 0.25 s into the reaction (RammedSpeed 5000, base 0)");
            r.conf("ram.robot_horizontal_after", hAfter, 0.0, 0.5, "m/s", kP8, kGameplay, "horizontal speed after the 0.5 s reaction (cancelled)");
        }
        Rig v(60, false);
        BoxScene fv; fv.floor(0, 400);
        vehicleOn(v, fv.mesh);
        core::Vec3 v0 = v.pawn().velocity();
        if (layer::addVehVelocity(v.pawn(), core::Vec3{10, 0, 0}, 0))
            r.conf("ram.vehicle_add_velocity_half", v.pawn().velocity().x - v0.x, 5.0, 1e-3, "m/s", kP8, kGameplay, "InVehicleForm.AddVelocity(v) adds v*0.5");
        r.info("ram.victim_rule", -1, "", "TnPawn-only victims (mass <= 1000, other team): World code, not linked in the harness - code-level only");
    }
}

} // namespace fid
