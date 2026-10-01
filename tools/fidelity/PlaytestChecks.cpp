// WFC fidelity harness — checks driven by the milestone-01 human playtest:
// transformation momentum, fast robot movement, fine aim, vehicle boost (physics vs
// presentation), Optimus vehicle materials and Streets map completeness.
//
// Rule for these suites: an expected value is only enforced when it has provenance (cooked
// config, extracted metadata, original assets). Where the original behaviour is not recovered
// the check is KNOWN (behaviour the playtest flagged) or INFO (measurement), and the note says
// UNKNOWN.
#include "CheckUtil.h"
#include "assets/SkinnedModel.h"
#include "core/Config.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

namespace fid {

using platform::Button;
using namespace core::config;

namespace {

// Optional input buttons, detected at compile time so this file builds on every branch.
template <class B> constexpr auto fineAimButton(int) -> decltype(B::FineAim, int()) { return (int)B::FineAim; }
template <class B> constexpr int fineAimButton(long) { return -1; }
template <class B> constexpr auto boostButton(int) -> decltype(B::Boost, int()) { return (int)B::Boost; }
template <class B> constexpr int boostButton(long) { return -1; }

platform::InputFrame withPress(platform::InputFrame f, Button b) {
    f.pressed[(int)b] = true;
    return f;
}

std::string fmt(double v, const char* f = "%.3f") {
    char b[64];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

// Category of the clip a frame is playing (robot or vehicle model), "" if unknown.
std::string categoryOf(const Models* m, const Frame& f) {
    if (!m) return "";
    for (const assets::SkinnedModel* mdl : {&m->robot, &m->vehicle}) {
        int c = mdl->clipByName(f.anim);
        if (c >= 0) return mdl->clips[(size_t)c].category;
    }
    return "";
}

} // namespace

// ---------------------------------------------------------------------------------------------
// TRANSFORMATION MOMENTUM
// Velocity sampled before the press, on the first transform step, mid-fold, at the mesh/form
// handoff, at the end of the fold and 0.25 s later. The input that produced the pre-transform
// motion is HELD through the whole fold (what a player does).
void checkTransformMomentum(Report& r) {
    r.setGroup("transform_momentum");
    const Models* m = Models::get();
    if (!m) { r.skip("transform_momentum", "needs Optimus models (transform is instant without clips)"); return; }

    struct Case { const char* id; game::Form from; std::initializer_list<Button> hold; double spin; };
    const Case cases[] = {
        {"r2v_standstill", game::Form::Robot, {}, 0.5},
        {"r2v_forward", game::Form::Robot, {Button::Forward}, 1.5},
        {"r2v_angled", game::Form::Robot, {Button::Forward, Button::Right}, 1.5},
        {"v2r_standstill", game::Form::Vehicle, {}, 0.5},
        {"v2r_cruise", game::Form::Vehicle, {Button::Forward}, 2.0},
        {"v2r_angled", game::Form::Vehicle, {Button::Forward, Button::Right}, 2.0},
        {"v2r_boost", game::Form::Vehicle, {Button::Forward, Button::Sprint}, 1.5},   // rebuild boost key
    };
    std::string summary;
    for (const Case& c : cases) {
        Rig rig(60, true);
        rig.pawn().setForm(c.from);
        rig.idle(0.3);
        platform::InputFrame in = Rig::down(c.hold);
        rig.hold(in, c.spin);
        const Frame before = rig.last();
        double t0 = rig.time();
        rig.step(withPress(in, Button::Transform));
        rig.hold(in, 3.5);
        save(rig, std::string("tm_") + c.id);

        const auto& tr = rig.trace();
        const Frame* first = nullptr; const Frame* hand = nullptr; const Frame* end = nullptr;
        const Frame* after = nullptr; const Frame* mid = nullptr;
        double vMinDuring = 1e9, vMaxDuring = 0;
        for (const Frame& f : tr) {
            if (f.t <= t0 + 1e-9) continue;
            if (!first) first = &f;
            if (f.transforming) { vMinDuring = std::min(vMinDuring, (double)hspeed(f)); vMaxDuring = std::max(vMaxDuring, (double)hspeed(f)); }
            if (!hand && f.form != c.from) hand = &f;
            if (!end && !f.transforming) end = &f;
            if (end && !after && f.t >= end->t + 0.25 - 1e-6) after = &f;
        }
        if (hand) for (const Frame& f : tr) if (f.t > t0 && f.t >= t0 + 0.5 * (hand->t - t0) && !mid) mid = &f;
        std::string id = c.id;
        double vb = hspeed(before);
        auto sp = [](const Frame* f) { return f ? (double)hspeed(*f) : -1.0; };
        r.info(id + ".v_before", vb, "m/s", "held input: " + std::to_string(c.hold.size()) + " key(s)");
        r.info(id + ".v_first_step", sp(first), "m/s", "first simulated step of the transform");
        r.info(id + ".v_mid_outgoing", sp(mid), "m/s", "halfway to the mesh handoff");
        r.info(id + ".v_handoff", sp(hand), "m/s", "first step on the new form/mesh");
        r.info(id + ".v_end", sp(end), "m/s", "first step after the fold");
        r.info(id + ".v_end_plus_0p25", sp(after), "m/s", "input still held");
        if (end) r.info(id + ".fold_time", end->t - t0, "s", "press -> controllable");

        // Where did the speed go? Zeroed (instant), decayed (input lock + braking), or kept.
        std::string verdict;
        if (vb < 0.5) {
            r.truth(id + ".no_spurious_motion", vMaxDuring < 0.01, "standing transform must not create motion");
            verdict = "standstill";
        } else {
            double vf = sp(first);
            // < 10%: zeroed at the press (one step of re-acceleration can slip in before the lock).
            verdict = vf < 0.1 * vb ? "ZEROED at the press"
                    : vMinDuring < 0.05 * vb ? "DECAYED to 0 during the fold" : "KEPT";
            double regain = -1;
            for (const Frame& f : tr) if (end && f.t >= end->t && hspeed(f) >= 0.9 * std::min(vb, (double)tuningFor(f.form).moveSpeed)) { regain = f.t - t0; break; }
            r.info(id + ".time_to_regain_90pct", regain, "s", "press -> 90% of min(pre-transform speed, new form top speed)");
            r.knownTruth(id + ".momentum_kept", vf >= 0.5 * vb && vMinDuring >= 0.25 * vb,
                         "playtest: transforming must not dead-stop the player. Original transfer factor UNKNOWN "
                         "(FIDELITY: 'velocity transfer at transform (currently zeroed)')", kGameplay,
                         verdict + ": " + fmt(vb, "%.2f") + " m/s -> first step " + fmt(vf, "%.2f") + ", min during fold " +
                             fmt(vMinDuring, "%.2f") + ". Causes in code: Character::beginTransform sets velocity_ = 0; "
                             "PlayerController::handleInput zeroes move intent while isTransforming().");
        }
        summary += std::string(c.id) + "=" + verdict + "; ";
    }
    r.info("summary", 0, "", summary);

    // [CONF] Xe-TransGame.ini [TransGame.TnPawn] _RestoreWeaponTransformFractionRemaining=0.75
    // ("1 = at the beginning of the transform, 0 = at the end"): the weapon returns when 75% of
    // the to-robot fold remains.
    {
        Rig rig(60, true);
        rig.pawn().setForm(game::Form::Vehicle);
        rig.idle(0.3);
        double t0 = rig.time();
        rig.step(Rig::press(Button::Transform));
        rig.idle(2.0);
        double end = firstAfter(rig, t0, [](const Frame& f) { return !f.transforming; });
        double vis = firstAfter(rig, t0, [](const Frame& f) { return f.weaponVisible; });
        double fold = std::max(end, 1e-3);
        r.known("weapon_restore_frac_elapsed_to_robot", vis >= 0 ? vis / fold : -1, 0.25, 0.05, "frac",
                "Xe-TransGame.ini [TransGame.TnPawn] _RestoreWeaponTransformFractionRemaining=0.75", kGameplay,
                "rebuild holsters the weapon for the whole fold (weapon reappears at " + fmt(vis, "%.3f") + " s of " +
                    fmt(end, "%.3f") + " s); original restores it with 75% of the fold remaining");
    }
}

// ---------------------------------------------------------------------------------------------
// FAST ROBOT MOVEMENT
void checkFastMovement(Report& r) {
    r.setGroup("fast_movement");
    // Baseline jog.
    Rig base(60, Models::get() != nullptr);
    base.idle(0.2);
    base.hold(Rig::down({Button::Forward}), 2.0);
    double jog = hspeed(base.last());
    r.info("robot_jog_speed", jog, "m/s", "W only (GroundSpeed 550 UU CONF)");

    // Every rebuild input that could plausibly select a faster state, held with W.
    double fastest = jog;
    std::string fastestKey = "none", anims;
    struct K { const char* name; Button b; };
    for (K k : {K{"Sprint(Shift)", Button::Sprint}, K{"Jump", Button::Jump}, K{"Fire", Button::Fire}}) {
        Rig rig(60, Models::get() != nullptr);
        rig.idle(0.2);
        rig.hold(Rig::down({Button::Forward, k.b}), 2.0);
        double vmax = 0;
        for (const Frame& f : rig.trace()) if (f.grounded) vmax = std::max(vmax, (double)hspeed(f));
        r.info(std::string("robot_speed_W+") + k.name, vmax, "m/s", "max grounded horizontal speed; anim " + rig.last().anim);
        if (vmax > fastest + 0.05) { fastest = vmax; fastestKey = k.name; }
        if (k.b == Button::Sprint) save(rig, "fast_robot_sprint");
    }
    r.knownTruth("faster_robot_state_reachable", fastest > jog + 0.5,
                 "playtest: original robot has a faster movement state. Activation, speed and clip UNKNOWN "
                 "(authored hints only: robot.glb boost_dodge clips Nav_Boost_{F,B,L,R} 0.47 s, Nav_Slide_{F,B}, Nav_Hover; "
                 "robot BoosterSocket_L/C/R; cue BL_FS_LRG_BOT.FOLEY_JUMPJETS_BOOST; Xe-TransInput.ini has NO Sprint/Run "
                 "command: LeftShift=Ability0|VehicleSpecialMove, RMB=ToggleFineAim|Boost, LT=FineAim|Boost)",
                 kGameplay, "fastest reachable: " + fmt(fastest, "%.2f") + " m/s via " + fastestKey + " (jog " + fmt(jog, "%.2f") + ")");

    // Which authored robot clip categories are ever selected by the runtime state machine.
    const Models* m = Models::get();
    if (!m) { r.skip("clip_category_coverage", "models unavailable"); return; }
    std::set<std::string> authored, reached;
    for (const assets::AnimClip& c : m->robot.clips) authored.insert(c.category);
    auto run = [&](std::initializer_list<Button> hold, double s, bool jump, bool reload, bool fire) {
        Rig rig(60, true);
        rig.idle(0.4);
        platform::InputFrame in = Rig::down(hold);
        if (fire) in.down[(int)Button::Fire] = true;
        if (jump) in = withPress(in, Button::Jump);
        if (reload) in = withPress(in, Button::Reload);
        rig.hold(in, s);
        rig.idle(1.0);
        for (const Frame& f : rig.trace()) if (f.form == game::Form::Robot) reached.insert(categoryOf(m, f));
    };
    run({}, 1.0, false, false, false);
    for (Button b : {Button::Forward, Button::Back, Button::Left, Button::Right}) run({b}, 1.0, false, false, false);
    run({Button::Forward, Button::Sprint}, 1.5, false, false, false);
    run({}, 0.2, true, false, false);
    run({Button::Forward}, 1.0, false, true, true);
    {   // transform out and back so the transform categories count as reachable
        Rig rig(60, true);
        rig.idle(0.2);
        rig.step(Rig::press(Button::Transform));
        rig.idle(2.5);
        rig.step(Rig::press(Button::Transform));
        rig.idle(2.0);
        for (const Frame& f : rig.trace()) if (f.form == game::Form::Robot) reached.insert(categoryOf(m, f));
    }
    std::string unreached;
    for (const std::string& c : authored) if (!reached.count(c)) unreached += c + " ";
    r.info("robot_categories_unreached", (double)(authored.size() - std::min(authored.size(), reached.size())), "cats",
           "authored robot clip categories never selected as the BASE clip by these input scenarios (layered slots "
           "and synthetic blend labels are not attributed): " + unreached);
    r.knownTruth("boost_dodge_clips_reachable", reached.count("boost_dodge") > 0,
                 "robot.glb category boost_dodge (Nav_Boost_*, Nav_Slide_*, Nav_Hover) exists in the shipped set", kGameplay,
                 "no input reaches these clips; trigger UNKNOWN until RE/AssetTools recovers it");
}

// ---------------------------------------------------------------------------------------------
// FINE AIM (templated on the button enum so the measuring branch is discarded when the build
// has no FineAim input)
template <class B> static void fineAimImpl(Report& r);
void checkFineAim(Report& r) { fineAimImpl<Button>(r); }

template <class B> static void fineAimImpl(Report& r) {
    r.setGroup("fine_aim");
    // Provenance for the expected values (shown so the owner sees what to implement against).
    std::string ini;
    bool haveIni = readFileText(cookedConfigDir() + "/Xe-TransGame.ini", ini);
    bool iniHas = haveIni && ini.find("_GroundSpeedMultiplier=0.5") != std::string::npos &&
                  ini.find("CloseFov=(FOV=35") != std::string::npos && ini.find("MediumFov=(FOV=45") != std::string::npos &&
                  ini.find("FarFov=(FOV=55") != std::string::npos;
    if (haveIni)
        r.truth("authored_values_present", iniHas,
                "Xe-TransGame.ini [TnFineAimManager] _GroundSpeedMultiplier=0.5, _TargetSnapTimeout=1.0; "
                "[TnPointOfInterest] Close/Medium/Far FOV 35/45/55 (SmoothTime 1.25)");
    else
        r.skip("authored_values_present", "cooked config not found at " + cookedConfigDir());
    r.near("config_fine_aim_speed_mult", kFineAimSpeedMult, 0.5, 1e-6, "", "Xe-TransGame.ini TnFineAimManager._GroundSpeedMultiplier");

    constexpr int fa = fineAimButton<B>(0);
    if constexpr (fa < 0) {
        r.knownTruth("fine_aim_input_exists", false,
                     "Xe-TransInput.ini: RightMouseButton=ToggleFineAim|Boost, XboxTypeS_LeftTrigger=FineAim|Boost "
                     "(FineAim -> FineAim | OnRelease StopFineAim)", kGameplay,
                     "platform::Button has no FineAim; nothing to activate. When added, this suite measures FOV, speed, "
                     "sensitivity and spread against 35/45/55 deg, 0.5x speed, 0.5x spread (weapon.json FineAimSpreadModifier)");
        r.info("expected_fov_close_med_far", 35, "deg", "45 / 55 by point-of-interest distance (TnPointOfInterest) [CONF]; "
               "which band applies when is UNKNOWN");
        r.info("expected_speed_mult", 0.5, "", "TnFineAimManager._GroundSpeedMultiplier [CONF]");
        r.info("expected_spread_mult", 0.5, "", "weapon.json FineAimSpreadModifier [CONF]");
        r.info("expected_sensitivity_mult", 0, "", "UNKNOWN (not recovered)");
        r.info("expected_camera_offset", 0, "", "UNKNOWN (HM camera behaviour data not recovered)");
    } else {
        Rig base(60, Models::get() != nullptr), aim(60, Models::get() != nullptr);
        base.idle(0.3);
        aim.idle(0.3);
        platform::InputFrame f;
        f.down[fa] = true;
        aim.hold(f, 1.5);   // FOV SmoothTime 1.25 s
        render::Camera c0 = base.camera(), c1 = aim.camera();
        r.info("fov_normal", c0.fovXDeg, "deg", "");
        bool fovOk = std::fabs(c1.fovXDeg - 35) < 1 || std::fabs(c1.fovXDeg - 45) < 1 || std::fabs(c1.fovXDeg - 55) < 1;
        r.truth("fov_is_authored_band", fovOk, "TnPointOfInterest Close/Medium/Far FOV 35/45/55", "fov=" + fmt(c1.fovXDeg, "%.1f"));
        r.info("camera_offset_change", core::length(c1.pos - c0.pos), "m", "expected UNKNOWN");
        platform::InputFrame w = f;
        w.down[(int)Button::Forward] = true;
        aim.hold(w, 1.5);
        r.near("speed_mult", hspeed(aim.last()) / kRobotMoveSpeed, 0.5, 0.02, "", "TnFineAimManager._GroundSpeedMultiplier");
        platform::InputFrame look = f;
        look.mouseDX = 100;
        float y0 = aim.controller().camYaw();
        aim.step(look);
        r.info("sensitivity_mult", -(aim.controller().camYaw() - y0) / (100 * kMouseSens), "", "expected UNKNOWN");
    }
}

// ---------------------------------------------------------------------------------------------
// VEHICLE BOOST: physics vs presentation
void checkBoost(Report& r) {
    r.setGroup("boost");
    // 1) Physics: which rebuild input boosts, and does it reach DashSpeed.
    std::string boostKey = "none";
    double boostSpeed = 0;
    struct K { const char* name; Button b; };
    for (K k : {K{"Sprint(Shift)", Button::Sprint}, K{"Fire(LMB)", Button::Fire}, K{"Jump", Button::Jump}, K{"Reload", Button::Reload}}) {
        Rig rig(60, false);
        rig.pawn().setForm(game::Form::Vehicle);
        rig.idle(0.3);
        rig.hold(Rig::down({Button::Forward}), 1.5);
        rig.hold(Rig::down({Button::Forward, k.b}), 1.0);
        double v = hspeed(rig.last());
        if (v > kVehicleMoveSpeed + 0.5 && v > boostSpeed) { boostSpeed = v; boostKey = k.name; }
    }
    r.truth("physics_boost_activates", boostSpeed > kVehicleMoveSpeed + 0.5,
            "TnHoverCarSimulationBlueprint.DashSpeed 5000 UU: a boost input exists and raises speed", "via " + boostKey);
    r.near("physics_boost_speed", boostSpeed, kVehicleBoostSpeed, 0.05, "m/s", "DashSpeed 5000 UU/s [CONF]");
    constexpr int bb = boostButton<Button>(0), fa = fineAimButton<Button>(0);
    r.knownTruth("boost_on_authored_input", bb >= 0 || fa >= 0,
                 "Xe-TransInput.ini: Boost shares RMB / LeftTrigger with FineAim (RightMouseButton=ToggleFineAim|Boost); "
                 "LeftShift is Ability0|VehicleSpecialMove", kGameplay,
                 "rebuild boost is on " + boostKey + " (platform::Button::Sprint = VK_SHIFT); no Boost/FineAim button exists");

    // 2) Presentation, part the harness can see: the vehicle's authored boost transition clips.
    const Models* m = Models::get();
    if (m) {
        bool hasClips = m->vehicle.clipByName("Nav_HoverToBoost_VEH") >= 0 && m->vehicle.clipByName("Nav_BoostToHover_VEH") >= 0;
        r.truth("authored_boost_clips_exist", hasClips, "vehicle.glb Nav_HoverToBoost_VEH (0.67 s) / Nav_BoostToHover_VEH (1.0 s)");
        Rig rig(60, true);
        rig.pawn().setForm(game::Form::Vehicle);
        rig.idle(0.3);
        rig.hold(Rig::down({Button::Forward}), 1.0);
        double b0 = rig.time();
        rig.hold(Rig::down({Button::Forward, Button::Sprint}), 1.0);
        double b1 = rig.time();
        rig.hold(Rig::down({Button::Forward}), 1.5);
        save(rig, "boost_vehicle_anim");
        std::set<std::string> during, after;
        for (const Frame& f : rig.trace()) {
            if (f.t > b0 && f.t <= b1) during.insert(f.anim);
            else if (f.t > b1) after.insert(f.anim);
        }
        auto list = [](const std::set<std::string>& s) { std::string o; for (const std::string& a : s) o += a + " "; return o; };
        r.knownTruth("boost_anim_plays", during.count("Nav_HoverToBoost_VEH") > 0,
                     "vehicle.glb Nav_HoverToBoost_VEH while boosting (HI by name; exact trigger/blend UNKNOWN)", kGameplay,
                     "base clips while boost held: " + list(during) + "| after release: " + list(after));
    } else {
        r.skip("boost_anim_plays", "models unavailable");
    }

    // 3) Presentation inventory from the extracted character data (what Systems must emit).
    assets::Json cj;
    if (!loadJsonFile(Models::assetRoot() + "/Characters/Optimus/character.json", cj)) {
        r.skip("presentation_inventory", "character.json unavailable");
        return;
    }
    std::set<std::string> cues;
    std::function<void(const assets::Json&)> walk = [&](const assets::Json& j) {
        if (j.isObject()) {
            if (j["cue"].isString() && j["cue"].asString().find("BOOST") != std::string::npos) cues.insert(j["cue"].asString());
            for (const auto& kv : j.obj) walk(kv.second);
        } else if (j.isArray()) for (const auto& e : j.arr) walk(e);
    };
    walk(cj["sounds"]);
    std::string cueList;
    for (const std::string& c : cues) cueList += c + " ";
    r.info("authored_boost_cues", (double)cues.size(), "cues", cueList);
    int sockets = 0;
    std::string sockList;
    for (const assets::Json& s : cj["vehicle"]["sockets"].arr) {
        std::string n = s["socket"].asString();
        if (n.find("Boost") != std::string::npos) { ++sockets; sockList += n + "@" + s["bone"].asString() + " "; }
    }
    r.info("authored_vehicle_boost_sockets", sockets, "sockets", sockList);
    r.knownTruth("boost_presentation_emitted", false,
                 "original boost = physics + VEH_OPTIMUS_BOOST_START/LOOP/END cues + booster FX at BoostSocket_*/HoverBooster_*",
                 kSystems,
                 "integrated build has no boost state, cue or FX (boost is CharacterMovement physics only). Runtime "
                 "confirmation: runtime-probe.ps1 'vehicle_boost' scenario (no VEH_OPTIMUS_BOOST cue logged). Particle "
                 "templates for the boost were not located in the extracted data (UNKNOWN; AssetTools)");
}

// ---------------------------------------------------------------------------------------------
// INPUT EDGES ACROSS RENDER FRAMES
// The exe calls PlayerController::handleInput once per RENDER frame and applyToPawn once per
// 60 Hz step. Above 60 fps some frames run zero steps; an edge (press) seen on such a frame must
// still reach the simulation. Found by runtime-probe (R press lost at ~200 fps).
void checkInputEdges(Report& r) {
    r.setGroup("input_edges");
    auto zeroStepThenStep = [](platform::InputFrame press, Rig& rig) {
        rig.frameWithoutStep(press);           // the frame that carries the press: no sim step
        rig.step(platform::InputFrame{});      // next frame: key released, one step
    };
    {
        Rig rig(60, Models::get() != nullptr);
        rig.idle(0.2);
        rig.hold(Rig::down({Button::Fire}), 0.3);   // spend some rounds so a reload is possible
        rig.idle(0.1);
        zeroStepThenStep(Rig::press(Button::Reload), rig);
        r.knownTruth("reload_press_survives_zero_step_frame", rig.pawn().weapon().reloading(),
                     "a pressed R must start the reload regardless of render rate", kGameplay,
                     "PlayerController::handleInput assigns wantReload_ = wasPressed(Reload) every render frame "
                     "(Jump uses a latch); a press on a zero-step frame is overwritten before applyToPawn runs");
    }
    {
        Rig rig(60, false);
        rig.idle(0.2);
        zeroStepThenStep(Rig::press(Button::Jump), rig);
        r.truth("jump_press_survives_zero_step_frame", !rig.pawn().onGround(), "jump press is latched (wantJumpLatched_)");
    }
    {
        Rig rig(60, Models::get() != nullptr);
        rig.idle(0.2);
        zeroStepThenStep(Rig::press(Button::Transform), rig);
        r.truth("transform_press_survives_zero_step_frame", rig.pawn().isTransforming() || rig.pawn().form() == game::Form::Vehicle,
                "transform starts inside handleInput (beginTransform)");
    }
    {
        Rig rig(60, false);
        rig.idle(0.2);
        int before = rig.last().shots;
        zeroStepThenStep(Rig::down({Button::Fire}), rig);   // click shorter than one sim step
        r.knownTruth("fire_tap_survives_zero_step_frame", rig.last().shots > before,
                     "a mouse click shorter than one 60 Hz step must still fire", kGameplay,
                     "wantFire_ = isDown(Fire) is reassigned every render frame; a tap that begins and ends between "
                     "steps never fires (latch the press like Jump)");
    }
}

// ---------------------------------------------------------------------------------------------
// OPTIMUS VEHICLE MATERIALS
// Three layers must agree: authored (character.json, from the cooked MICs), the exported mesh
// (vehicle.glb extras.wfc_material) and the compiled render material (materials_glsl.json).
static std::string renderDataDir() {
    if (const char* e = std::getenv("WFC_RENDER_DATA")) return std::string(e) + "/MP_IAC_Streets";
    return "work/render/MP_IAC_Streets";
}

void checkVehicleMaterials(Report& r) {
    r.setGroup("vehicle_materials");
    assets::Json cj, vg, rg;
    std::string root = Models::assetRoot() + "/Characters/Optimus/";
    if (!loadJsonFile(root + "character.json", cj) || !loadGlbJson(root + "vehicle.glb", vg) || !loadGlbJson(root + "robot.glb", rg)) {
        r.skip("vehicle_materials", "character.json / vehicle.glb unavailable");
        return;
    }
    for (const char* form : {"vehicle", "robot"}) {
        const assets::Json& slots = cj[form]["materials"];
        const assets::Json& glb = std::string(form) == "vehicle" ? vg : rg;
        std::string f = form;
        r.near(f + ".slot_count_glb_vs_authored", (double)glb["materials"].size(), (double)slots.size(), 0, "slots",
               "character.json " + f + ".materials (cooked skeletal mesh material slots)");
        bool namesOk = slots.size() == glb["materials"].size();
        std::string detail;
        for (size_t i = 0; namesOk && i < slots.size(); ++i) {
            std::string a = slots[i]["material"].asString(), g = glb["materials"][i]["extras"]["wfc_material"].asString();
            namesOk = a == g;
            detail += "[" + std::to_string(i) + "] " + a + (a == g ? "" : " != glb " + g) + " ";
        }
        r.truth(f + ".slot_materials_match", namesOk, "glb extras.wfc_material == authored slot material", detail);
    }
    // Authored vehicle slot 0 must carry vehicle (VH_*) textures and parent graph.
    const assets::Json& v0 = cj["vehicle"]["materials"][0]["resolved"];
    r.truth("vehicle.authored_parent", v0["parent_chain"][0].asString() == "TR_AllShader_p.Release.CHR_Transformer_NormSpec_Cust_E_Mat",
            "character.json vehicle slot 0 parent chain", v0["parent_chain"][0].asString());
    int vhTex = 0, otherTex = 0, missing = 0;
    std::string roles;
    for (const auto& kv : v0["roles"].obj) {
        std::string tex = kv.second["texture"].asString(), file = kv.second["file"].asString();
        roles += kv.first + "=" + tex + " ";
        if (tex.find("TR_Optimus_VEH_p.") == 0) ++vhTex;
        else if (tex.find("TR_Optimus_ROBO_p.") == 0) ++otherTex;
        std::string s;
        if (!file.empty() && !readFileText(Models::assetRoot() + "/../" + file, s)) ++missing;
    }
    r.truth("vehicle.authored_textures_own_package", vhTex >= 4 && otherTex == 0,
            "vehicle slot 0 roles (normal/emissive/mask/base_color) come from TR_Optimus_VEH_p", roles);
    r.near("vehicle.authored_texture_files_missing", missing, 0, 0, "files", "role texture files exist in ExtractedAssets/content");

    // Customisation parameters differ between robot and vehicle MICs: the vehicle must not be
    // rendered with the robot's values (an easy translation slip).
    const assets::Json& r0 = cj["robot"]["materials"][0]["resolved"];
    std::string diffs;
    int nd = 0;
    for (const auto& kv : v0["scalars"].obj) {
        double a = kv.second.asDouble(), b = r0["scalars"][kv.first].asDouble(NAN);
        if (std::isnan(b) || std::fabs(a - b) > 1e-4) { ++nd; diffs += kv.first + " " + fmt(a, "%.3g") + " (robot " + fmt(b, "%.3g") + "), "; }
    }
    r.info("vehicle.authored_scalars_differing_from_robot", nd, "params", diffs);

    // Compiled render material (Rendering's materials_glsl.json), if generated for this tree.
    assets::Json M;
    std::string dir = renderDataDir();
    if (!loadJsonFile(dir + "/materials_glsl.json", M)) {
        r.skip("vehicle.compiled", "render data not found in " + dir +
               " (set WFC_RENDER_DATA or run tools/render/build_render_data.ps1)");
        return;
    }
    for (const assets::Json& s : cj["vehicle"]["materials"].arr) {
        std::string name = s["material"].asString(), shortName = name.substr(name.find('.') + 1);
        const assets::Json& e = M[name];
        r.truth("vehicle.compiled_present." + shortName, e.isObject() && e["glsl"].isString() && !e["glsl"].asString().empty(),
                "materials_glsl.json has GLSL for the authored slot (else runtime falls back to glTF)",
                e["error"].isString() ? "error=" + e["error"].asString() : "");
        if (!e.isObject()) continue;
        r.truth("vehicle.compiled_chain_is_own_mic." + shortName, e["info"]["chain"][0].asString() == name,
                "compiled parameter chain starts at the vehicle's own MaterialInstanceConstant", e["info"]["chain"][0].asString());
        int wrongPkg = 0, absent = 0;
        std::string texs;
        for (const assets::Json& t : e["info"]["textures"].arr) {
            std::string obj = t["object"].asString(), file = t["file"].asString();
            if (obj.find("TR_Optimus_ROBO_p.") == 0) ++wrongPkg;
            std::string tmp;
            if (!file.empty() && !readFileText(file, tmp)) ++absent;
            texs += obj.substr(obj.rfind('.') + 1) + "(" + t["format"].asString() + ") ";
        }
        r.near("vehicle.compiled_robot_textures." + shortName, wrongPkg, 0, 0, "tex", "no robot-package texture bound to a vehicle material", texs);
        r.near("vehicle.compiled_texture_files_missing." + shortName, absent, 0, 0, "files", "bound texture files decode-able on disk");
    }
    // Normal-map path: the vehicle graph reconstructs Z from (A,G) of a DXT5 normal map while the
    // robot uses an RGB DXT1 one. Reported for Rendering's visual A/B; not judged here.
    const assets::Json& ve = M["TR_Optimus_VEH_p.RB_OptimusPrime_Cust2_Mat_INST"];
    const assets::Json& ro = M["TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B"];
    if (ve.isObject() && ro.isObject()) {
        std::string sw;
        for (const auto& kv : ve["info"]["switches"].obj)
            if (ro["info"]["switches"][kv.first].asBool() != kv.second.asBool())
                sw += kv.first + "=" + (kv.second.asBool() ? "true" : "false") + " (robot " +
                      (ro["info"]["switches"][kv.first].asBool() ? "true" : "false") + ") ";
        r.info("vehicle.compiled_switches_differing_from_robot", sw.empty() ? 0 : 1, "", sw);
        for (const assets::Json& t : ve["info"]["textures"].arr) {
            if (t["object"].asString() != "TR_Optimus_VEH_p.VH_Optimus_NORM") continue;
            const assets::Json& um = t["unpack_min"];
            r.info("vehicle.normal_map_unpack_alpha_min", um[3].asDouble(-99), "",
                   "VH_Optimus_NORM " + t["format"].asString() + ", authored UnpackMin [" + fmt(um[0].asDouble(), "%.0f") + "," +
                       fmt(um[1].asDouble(), "%.0f") + "," + fmt(um[2].asDouble(), "%.0f") + "," + fmt(um[3].asDouble(), "%.0f") +
                       "]; graph uses X=.a, Y=.g, Z=sqrt(1-x2-y2) (UseReconstructedNormal). With alpha in [0,1] X is never "
                       "negative - Rendering to confirm 360 sampling of DXT5 normal alpha");
        }
    }
}

// ---------------------------------------------------------------------------------------------
// MAP COMPLETENESS (MP_IAC_Streets)
// Separates: (a) authored content that was never extracted, (b) extracted content the runtime
// does not render, (c) intentional non-visual actors.
void checkMapContent(Report& r) {
    r.setGroup("map_content");
    std::string mdir = Models::assetRoot() + "/Maps/MP_IAC_Streets/";
    assets::Json mj, wg, props;
    if (!loadJsonFile(mdir + "map.json", mj) || !loadGlbJson(mdir + "world.glb", wg) || !loadJsonFile(mdir + "props.json", props)) {
        r.skip("map_content", "map.json / world.glb / props.json unavailable");
        return;
    }
    r.near("sublevels_authored", (double)mj["sublevels"].size(), 3, 0, "levels", "map.json sublevels ART/AUDIO/BASE");
    int placed = mj["placed_props"].asInt();
    r.near("props_json_count", props["count"].asDouble(), placed, 0, "props", "props.json vs map.json placed_props");
    int propNodes = 0, lmNodes = 0;
    std::map<std::string, int> sub;
    for (const assets::Json& n : wg["nodes"].arr) {
        if (n["extras"]["actor"].isString()) ++propNodes;
        if (n["extras"]["lm_atlas"].isString()) ++lmNodes;
    }
    for (const assets::Json& p : props["props"].arr) sub[p["sublevel"].asString()]++;
    std::string subs;
    for (auto& kv : sub) subs += kv.first + "=" + std::to_string(kv.second) + " ";
    r.near("props_composed_in_world_glb", propNodes, placed, 0, "props", "every placed prop is a world.glb node", subs);
    r.near("props_missing_mesh", mj["props_missing_mesh"].asDouble(), 0, 0, "props", "map.json props_missing_mesh");
    r.info("props_lightmapped", lmNodes, "props", "world.glb nodes with a lightmap binding");
    r.truth("bsp_present", mj["bsp"]["MP_IAC_Streets_ART_m"]["render_triangles"].asInt() > 0,
            "map.json bsp.ART render_triangles", std::to_string(mj["bsp"]["MP_IAC_Streets_ART_m"]["render_triangles"].asInt()) + " tris");
    int wfcMats = 0;
    std::set<std::string> worldMats;
    for (const assets::Json& m : wg["materials"].arr)
        if (m["extras"]["wfc_material"].isString()) { ++wfcMats; worldMats.insert(m["extras"]["wfc_material"].asString()); }
    r.near("world_materials_named", wfcMats, mj["materials"].asDouble(), 0, "mats", "world.glb materials carrying wfc_material vs map.json materials");

    // (a) authored but never extracted / composed — original content missing from the slice.
    const assets::Json& un = mj["unhandled_actor_classes"];
    auto cnt = [&](const char* k) { return un[k].asInt(); };
    r.known("missing.prefab_instances", 0, cnt("PrefabInstance"), 0, "actors",
            "map.json unhandled PrefabInstance (modular set dressing)", kAssetTools,
            "not composed into world.glb: original content absent (extraction gap, not a rendering bug)");
    r.known("missing.static_destructibles", 0, cnt("TnStaticDestructibleActor"), 0, "actors",
            "map.json unhandled TnStaticDestructibleActor", kAssetTools, "not composed (extraction gap)");
    // (b) extracted counts the runtime has no path for.
    r.known("unrendered.decals", 0, mj["decals"].asDouble(), 0, "decals", "map.json decals (DecalActor/components)",
            kRendering, "no decal pass in the integrated renderer (grep: no decal code): extracted-but-not-rendered");
    r.known("unrendered.level_emitters", 0, mj["emitters"].asDouble(), 0, "emitters", "map.json emitters (level ParticleSystems)",
            kSystems, "WeaponFx handles weapon emitters only; level Emitter actors are not spawned");
    r.info("heightfog_actor", cnt("HeightFog"), "actors", "listed unhandled in map.json but recovered separately (FIDELITY: HeightFog CONF, applied)");
    // (c) intentional / non-visual.
    std::string nonvis;
    for (const char* k : {"BRUSH (builder brushes; geometry lives in level BSP)", "Model", "Sequence", "TnWorldInfo", "AmbientSound",
                          "HmAmbientSoundLineEmitter", "HmAmbientSoundVolumeEmitter", "BeastSettingsReferer", "CameraActor",
                          "TnAssetReferencesMultiplayer"})
        nonvis += std::string(k).substr(0, std::string(k).find(' ')) + "=" + std::to_string(cnt(k)) + " ";
    r.info("nonvisual_actor_classes", 0, "", "intentionally not geometry: " + nonvis + "; volumes=" + std::to_string(mj["volumes"].asInt()));

    // Compiled material coverage for the world (render data), if generated.
    assets::Json M;
    std::string dir = renderDataDir();
    if (!loadJsonFile(dir + "/materials_glsl.json", M)) {
        r.skip("world_materials_compiled", "render data not found in " + dir);
        return;
    }
    int compiled = 0, errored = 0, absent = 0, texMissing = 0;
    std::string bad;
    for (const std::string& name : worldMats) {
        const assets::Json& e = M[name];
        if (!e.isObject()) { ++absent; bad += "ABSENT:" + name + " "; continue; }
        if (!e["glsl"].isString() || e["glsl"].asString().empty()) { ++errored; bad += "NOGLSL:" + name + "(" + e["error"].asString() + ") "; continue; }
        ++compiled;
        for (const assets::Json& t : e["info"]["textures"].arr) {
            std::string tmp, f = t["file"].asString();
            if (!f.empty() && !readFileText(f, tmp)) ++texMissing;
        }
    }
    r.info("world_materials_compiled", compiled, "mats", "of " + std::to_string(worldMats.size()) + " world materials");
    r.knownTruth("world_materials_all_compiled", absent + errored == 0,
                 "every world material has a compiled original graph (else glTF fallback = wrong look)", kRendering, bad);
    r.near("world_material_texture_files_missing", texMissing, 0, 0, "files", "textures bound by compiled world materials exist on disk");
    assets::Json L;
    if (loadJsonFile(dir + "/lighting.json", L)) {
        r.info("render_lights", (double)L["lights"].size(), "lights", "lighting.json (map.json counts 1 light actor; components in lighting.json)");
        std::string tmp;
        r.truth("bsp_render_mesh_present", readFileText(dir + "/bsp.glb", tmp), "render data bsp.glb (else BSP stays unlit)");
    }
}

} // namespace fid
