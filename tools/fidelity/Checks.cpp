#include "Checks.h"
#include "Rig.h"
#include "assets/Gltf.h"
#include "assets/Json.h"
#include "core/Config.h"
#include "game/TransformState.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>

namespace fid {

using platform::Button;
using namespace core::config;

namespace {
Options gOpt;

float hspeed(const Frame& f) { return std::sqrt(f.vel.x * f.vel.x + f.vel.z * f.vel.z); }

void save(const Rig& r, const std::string& name) {
    if (!gOpt.traceDir.empty()) r.writeCsv(gOpt.traceDir + "/" + name + ".csv");
}

bool loadJsonFile(const std::string& path, assets::Json& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    return assets::Json::parse(ss.str(), out);
}

// World point -> normalized device coordinates through the production camera.
core::Vec3 toNdc(const render::Camera& cam, const core::Vec3& p) {
    core::Mat4 m = cam.proj() * cam.view();
    float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
    float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
    float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    float w = m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15];
    return {x / w, y / w, z / w};
}

float angleDeg(core::Vec3 a, core::Vec3 b) {
    float d = core::clampf(core::dot(core::normalize(a), core::normalize(b)), -1.0f, 1.0f);
    return core::degrees(std::acos(d));
}

// Time (relative to t0) of the first frame satisfying `pred`; -1 if never.
template <class P> double firstAfter(const Rig& r, double t0, P pred) {
    for (const Frame& f : r.trace()) if (f.t > t0 + 1e-9 && pred(f)) return f.t - t0;
    return -1;
}

const char* kGameplay = "Gameplay";
const char* kSystems = "Systems";
} // namespace

const Options& options() { return gOpt; }
void setOptions(const Options& o) { gOpt = o; }

// ---------------------------------------------------------------------------------------------
void checkConstants(Report& r) {
    r.setGroup("constants");
    const char* ini = "Xe-TransGame.ini [Engine.WorldInfo] ";
    r.near("gravity_pawn", kGravity, 2940 / 100.0, 1e-4, "m/s2", std::string(ini) + "DefaultGravityZ=-2940");
    r.near("gravity_vehicle", kVehicleGravity, 2940 * 0.66 / 100.0, 0.01, "m/s2",
           std::string(ini) + "RBPhysicsGravityScaling=0.66");
    const char* pp = "TransGame.xxx Default__TnPlayerPawn.";
    r.near("robot_ground_speed", kRobotMoveSpeed, 550 / 100.0, 1e-4, "m/s", std::string(pp) + "GroundSpeed=550");
    r.near("robot_accel", kRobotAccel, 2048 / 100.0, 1e-4, "m/s2", "Engine.xxx Default__Pawn.AccelRate=2048");
    r.near("robot_air_control", kAirControl, 0.70, 1e-4, "", std::string(pp) + "AirControl=0.70");
    r.near("robot_air_speed", kAirSpeed, 1500 / 100.0, 1e-4, "m/s", std::string(pp) + "AirSpeed=1500");
    const char* wmc = "Default__TnPawn._WorkingMovementCapabilities.";
    r.near("robot_max_jump_height", kRobotMaxJumpH, 625 / 100.0, 1e-4, "m", std::string(wmc) + "MaxJumpHeight=625");
    r.near("robot_jump_speed_derived", kRobotJumpSpeed, std::sqrt(2.0 * kGravity * kRobotMaxJumpH), 0.01, "m/s",
           "JumpZ = sqrt(2 g MaxJumpHeight) (derived, keeps apex at MaxJumpHeight)");
    r.near("pawn_radius", kPawnRadius, 175 / 100.0, 1e-4, "m", std::string(wmc) + "CylinderRadius=175");
    r.near("pawn_half_height", kPawnHalfHeight, 200 / 100.0, 1e-4, "m", std::string(wmc) + "CylinderHeight=200");
    r.near("pawn_eye_height", kEyeHeight, (200 + 80) / 100.0, 1e-4, "m", "CylinderHeight 200 + Default__TnPawn.BaseEyeHeight 80");
    game::FormTuning rt = game::tuningFor(game::Form::Robot);
    r.truth("robot_box_matches_cylinder", rt.boxSize.x == 2 * kPawnRadius && rt.boxSize.y == 2 * kPawnHalfHeight,
            "tuningFor(Robot).boxSize == capsule diameter x full height");

    const char* hc = "TransGame.xxx Default__TnHoverCarSimulationBlueprint.";
    r.near("vehicle_max_speed", kVehicleMoveSpeed, 1500 / 100.0, 1e-4, "m/s", std::string(hc) + "MaxLinearSpeed=1500");
    r.near("vehicle_accel", kVehicleAccel, 3000 / 100.0, 1e-4, "m/s2", std::string(hc) + "MaxLinearAcceleration=3000");
    r.near("vehicle_dash_speed", kVehicleBoostSpeed, 5000 / 100.0, 1e-4, "m/s", std::string(hc) + "DashSpeed=5000");
    r.near("vehicle_dash_duration", kVehicleDashTime, 0.3, 1e-4, "s", std::string(hc) + "DashDuration=0.3");
    r.near("vehicle_hover_height", kVehicleHoverH, 200 / 100.0, 1e-4, "m", std::string(hc) + "SuspensionRadius=200");
    r.near("vehicle_jump_speed", kVehicleJumpSpeed, 1200 / 100.0, 1e-4, "m/s", std::string(hc) + "JumpLinearSpeed=1200");
    r.near("vehicle_turn_rate", kVehicleTurnRate, 3.14159, 1e-3, "rad/s", std::string(hc) + "AiMaxAngularSpeed");

    r.near("camera_fov_horizontal", kCamFovXDeg, 75.0, 1e-4, "deg", "Xe-TransCamera.ini TnFovCameraBehavior DefaultFOV=75");
    r.near("camera_fov_smooth", kCamFovSmooth, 0.4, 1e-4, "s", "Xe-TransCamera.ini TnFovCameraBehavior SmoothTime=0.4");
    r.near("fine_aim_speed_mult", kFineAimSpeedMult, 0.5, 1e-4, "", "Xe-TransGame.ini TnFineAimManager._GroundSpeedMultiplier");
    r.near("transform_blend_in", kTransformBlendIn, 0.115, 1e-4, "s", "Xe-TransGame.ini TnTransformation._BlendInTime");
    r.near("transform_blend_out", kTransformBlendOut, 0.25, 1e-4, "s", "Xe-TransGame.ini TnTransformation._BlendOutTime");
    r.known("mesh_yaw_offset", kMeshYawOffset, core::PI * 0.5, 1e-4, "rad",
            "UE meshes are +X-forward (umodel keeps UE +X as glTF +X), rebuild forward is -Z; verified by "
            "muzzle.mesh_facing_vs_yaw_* and agents/gameplay 10cba8a (Shooting_Aim_F_C barrel along +X)", kGameplay,
            "0 renders the robot/truck side-on; fixed on agents/gameplay 10cba8a, pending merge");

    // Camera projection: authored horizontal FOV must survive the vertical conversion.
    render::Camera cam;
    cam.aspect = (float)kWindowWidth / (float)kWindowHeight;
    core::Mat4 p = cam.proj();
    double hfov = 2.0 * std::atan(1.0 / p.m[0]);
    r.near("camera_projected_hfov", core::degrees((float)hfov), 75.0, 0.01, "deg",
           "render::Camera::proj() at 16:9 must keep the authored 75 deg horizontal FOV");
    r.info("sim_rate", kSimHz, "Hz", "rebuild fixed step; original 360 tick rate not yet measured (30 Hz typical)");
}

// ---------------------------------------------------------------------------------------------
void checkWeaponData(Report& r) {
    r.setGroup("weapon_data");
    game::Weapon w;
    assets::Json j;
    std::string path = Models::assetRoot() + "/Weapons/IonBlaster/weapon.json";
    if (!loadJsonFile(path, j)) { r.skip("weapon_json", "cannot read " + path); return; }
    const assets::Json& g = j["gameplay"];
    std::string src = "weapon.json gameplay.";
    auto cmp = [&](const char* id, double rebuild, const assets::Json& v, double scale, double tol,
                   const char* unit, const char* field) {
        if (!v.isNumber()) { r.skip(id, std::string("field missing: ") + field); return; }
        r.near(id, rebuild, v.asDouble() * scale, tol, unit, src + field);
    };
    cmp("fire_interval", w.fireInterval, g["FireIntervalModifier"]["IntervalRange"]["Min"], 1, 1e-6, "s", "FireIntervalModifier.IntervalRange.Min");
    r.truth("fire_interval_constant", std::fabs(g["FireIntervalModifier"]["IntervalRange"]["Min"].asDouble() -
                                               g["FireIntervalModifier"]["IntervalRange"]["Max"].asDouble()) < 1e-9 &&
                                          g["FireIntervalModifier"]["RampTime"].asDouble() == 0.0,
            src + "FireIntervalModifier (Min==Max, RampTime 0 -> no fire-rate ramp to model)");
    cmp("damage", w.damage, g["InstantHitDamage"][0], 1, 1e-6, "hp", "InstantHitDamage[0]");
    cmp("mag_size", w.magSize, g["MaxAmmoClipCount"], 1, 0, "rnd", "MaxAmmoClipCount");
    cmp("reserve_max", w.reserveMax, g["MaxAmmoCount"], 1, 0, "rnd", "MaxAmmoCount");
    cmp("reserve_initial", w.reserve, g["InitialReserveAmmoCount"], 1, 0, "rnd", "InitialReserveAmmoCount");
    cmp("reload_time", w.reloadTime, g["WeaponReloadAnimTime"], 1, 1e-6, "s", "WeaponReloadAnimTime");
    cmp("equip_time", w.equipTime, g["EquipTime"], 1, 1e-6, "s", "EquipTime");
    cmp("range", w.rangeM, g["WeaponRange"], 0.01, 1e-4, "m", "WeaponRange (UU/100)");
    const assets::Json& rdm = g["RangeDamageModifiers"];
    cmp("falloff_near", w.falloffNearM, rdm[0]["Range"], 0.01, 1e-4, "m", "RangeDamageModifiers[0].Range");
    cmp("falloff_near_mult", 1.0, rdm[0]["Modifier"], 1, 1e-6, "", "RangeDamageModifiers[0].Modifier");
    cmp("falloff_far_mult", w.falloffFarMul, rdm[1]["Modifier"], 1, 1e-6, "", "RangeDamageModifiers[1].Modifier");
    cmp("falloff_far_range", w.rangeM, rdm[1]["Range"], 0.01, 1e-4, "m", "RangeDamageModifiers[1].Range");
    const assets::Json& ps = g["PerShotSpreadModifier"];
    cmp("spread_min", w.spreadMin, ps["Modifier"]["Min"], 1, 1e-6, "", "PerShotSpreadModifier.Modifier.Min");
    cmp("spread_max", w.spreadMax, ps["Modifier"]["Max"], 1, 1e-6, "", "PerShotSpreadModifier.Modifier.Max");
    cmp("spread_per_shot", w.spreadPerShot, ps["ModifierChangePerShot"], 1, 1e-6, "", "PerShotSpreadModifier.ModifierChangePerShot");
    cmp("spread_cooldown", w.spreadCooldown, ps["Cooldown"], 1, 1e-6, "s", "PerShotSpreadModifier.Cooldown");
    r.info("put_down_time", g["PutDownTime"].asDouble(), "s", "authored; not yet modelled (no weapon switching)");
    r.info("fine_aim_spread_mult", g["FineAimSpreadModifier"].asDouble(), "", "authored; not yet modelled (no ADS)");

    r.near("damage_at_near", w.damageAt(50), 15.0, 1e-4, "hp", "RangeDamageModifiers: 1.0x at 5000 UU");
    r.near("damage_at_max_range", w.damageAt(300), 7.5, 1e-4, "hp", "RangeDamageModifiers: 0.5x at 30000 UU");
    r.info("damage_at_175m", w.damageAt(175), "hp", "linear interpolation between keys assumed (UE3 curve shape unverified)", 11.25);
}

// ---------------------------------------------------------------------------------------------
void checkWeaponBehaviour(Report& r) {
    r.setGroup("weapon");
    game::Weapon ref;
    {
        // Mag dump: hold the trigger from a full magazine until it is empty.
        Rig rig(60, false);
        rig.idle(0.1);
        size_t base = shotLog().size();
        double t0 = rig.time();
        rig.hold(Rig::down({Button::Fire}), 4.0);
        save(rig, "weapon_mag_dump");
        std::vector<ShotRecord> shots(shotLog().begin() + (long)base, shotLog().end());
        r.truth("first_shot_same_step", !shots.empty() && shots.front().t - t0 < rig.dt() * 0.5,
                "trigger pull fires on the first held step (no artificial delay)");
        // Only shots from the first magazine (auto-reload follows).
        int mag = std::min<int>((int)shots.size(), ref.magSize);
        double interval = mag > 1 ? (shots[(size_t)mag - 1].t - shots[0].t) / (mag - 1) : 0;
        r.known("fire_interval_effective", interval, ref.fireInterval, 0.0005, "s",
                "weapon.json FireIntervalModifier 0.065 s (923 RPM)", kSystems,
                "Weapon::onFired sets cooldown = interval, discarding the sub-step remainder, so at 60 Hz "
                "every shot waits a whole 4th step (66.7 ms, 900 RPM, mag empties ~0.08 s late). UE3 looping "
                "refire timers subtract Rate and keep the remainder (MED: verify in original capture). "
                "Fix: cooldown += fireInterval.");
        r.info("rounds_per_minute", interval > 0 ? 60.0 / interval : 0, "rpm", "", 60.0 / ref.fireInterval);
        r.info("mag_empty_time", mag > 1 ? shots[(size_t)mag - 1].t - shots[0].t : 0, "s",
               "first to 50th shot", (ref.magSize - 1) * ref.fireInterval);
        const Frame* reloadStart = nullptr;
        const Frame* reloadEnd = nullptr;
        for (const Frame& f : rig.trace()) {
            if (!reloadStart && f.reloading) reloadStart = &f;
            if (reloadStart && !reloadEnd && !f.reloading && f.t > reloadStart->t) reloadEnd = &f;
        }
        r.truth("auto_reload_on_empty", reloadStart != nullptr, "trigger held on an empty magazine starts a reload");
        if (reloadStart && reloadEnd) {
            r.near("auto_reload_duration", reloadEnd->t - reloadStart->t, ref.reloadTime, rig.dt() + 1e-6, "s",
                   "weapon.json WeaponReloadAnimTime 1.5");
            r.near("ammo_after_reload", reloadEnd->ammo, ref.magSize, 0, "rnd", "MaxAmmoClipCount");
            r.near("reserve_after_reload", reloadEnd->reserve, ref.reserve - ref.magSize, 0, "rnd",
                   "InitialReserveAmmoCount 150 - 50");
        }
    }
    {
        // Manual reload after 10 rounds; spread bloom and decay.
        Rig rig(60, false);
        rig.idle(0.1);
        while (rig.last().shots < 10) rig.step(Rig::down({Button::Fire}));
        r.near("spread_after_10", rig.pawn().weapon().spread, ref.spreadMin + 10 * ref.spreadPerShot, 1e-4, "",
               "PerShotSpreadModifier: 0.08 + 10 x 0.005");
        rig.step(Rig::press(Button::Reload));
        double t0 = rig.time();
        rig.idle(2.0);
        r.truth("manual_reload_starts", rig.trace()[rig.trace().size() - 120].reloading, "R starts a reload with a partial mag");
        double dur = firstAfter(rig, t0, [](const Frame& f) { return !f.reloading; });   // reload began in the press step
        r.near("manual_reload_duration", dur, ref.reloadTime, rig.dt() + 1e-6, "s", "weapon.json WeaponReloadAnimTime 1.5");
        r.near("manual_reload_reserve", rig.last().reserve, ref.reserve - 10, 0, "rnd", "only the spent 10 rounds are drawn");
        r.near("spread_after_cooldown", rig.pawn().weapon().spread, ref.spreadMin, 1e-6, "",
               "PerShotSpreadModifier.Cooldown 2.0 s", "rebuild snaps back to Min after 2 s idle; UE3 decay curve unverified");
        save(rig, "weapon_manual_reload");
        Rig cap(60, false);
        cap.idle(0.1);
        cap.hold(Rig::down({Button::Fire}), 2.5);
        r.near("spread_cap", cap.pawn().weapon().spread, ref.spreadMax, 1e-6, "", "PerShotSpreadModifier.Modifier.Max 0.18");
    }
    {
        Rig rig(60, false);
        rig.pawn().setForm(game::Form::Vehicle);
        rig.idle(0.1);
        rig.hold(Rig::down({Button::Fire}), 0.5);
        r.info("vehicle_form_shots", rig.last().shots, "shots",
               "rebuild: Ion Blaster is robot-only. WFC alt-modes carry vehicle weapons (not yet extracted for this slice)");
    }
}

// ---------------------------------------------------------------------------------------------
void checkOrientation(Report& r) {
    r.setGroup("orientation");
    core::Vec3 f0 = core::forwardFromYawPitch(0, 0);
    r.truth("yaw0_forward_is_minus_z", std::fabs(f0.z + 1) < 1e-6 && std::fabs(f0.x) < 1e-6,
            "rebuild convention: -Z forward at yaw 0 (Math.h)");

    {
        Rig rig(60, false);
        rig.idle(0.2);
        render::Camera cam0 = rig.camera();
        core::Vec3 p0 = rig.pawn().position();
        rig.hold(Rig::down({Button::Right}), 1.0);
        core::Vec3 p1 = rig.pawn().position();
        float n0 = toNdc(cam0, p0 + core::Vec3{0, kEyeHeight, 0}).x;
        float n1 = toNdc(cam0, p1 + core::Vec3{0, kEyeHeight, 0}).x;
        r.truth("strafe_right_moves_screen_right", n1 > n0 + 0.05f, "D key must move the pawn toward screen-right");
        r.truth("strafe_keeps_facing", std::fabs(rig.pawn().yaw() - rig.controller().camYaw()) < 1e-6,
                "strafe shooter: body keeps facing the aim while strafing (FIDELITY pass 6, HI)");
        save(rig, "orient_strafe_right");
    }
    {
        Rig rig(60, false);
        rig.idle(0.2);
        render::Camera cam0 = rig.camera();
        platform::InputFrame look;
        look.mouseDX = 100;
        rig.step(look);
        core::Vec3 fwd = core::forwardFromYawPitch(rig.pawn().yaw(), 0);
        core::Vec3 ahead = rig.pawn().position() + core::Vec3{0, kEyeHeight, 0} + fwd * 20.0f;
        r.truth("mouse_right_turns_right", toNdc(cam0, ahead).x > 0.0f,
                "mouse moved right must rotate the view/body clockwise (target appears right of the old centre)");
        r.near("standing_body_follows_aim", rig.pawn().yaw() - rig.controller().camYaw(), 0.0, 1e-6, "rad",
               "WFC robot faces the camera/aim yaw every frame, even standing (user-confirmed, pass 6)");
        r.near("mouse_yaw_per_pixel", -(rig.controller().camYaw() - rig.trace()[rig.trace().size() - 2].camYaw) / 100.0,
               kMouseSens, 1e-7, "rad/px", "Config kMouseSens [PROV]");
        platform::InputFrame up;
        up.mouseDY = -50;   // Win32: moving the mouse up gives negative dy
        float before = rig.controller().camPitch();
        rig.step(up);
        r.info("mouse_up_pitch_delta", rig.controller().camPitch() - before, "rad",
               "positive = mouse-up looks up (non-inverted); WFC default inversion setting not yet confirmed");
        platform::InputFrame big;
        big.mouseDY = -100000;
        rig.step(big);
        r.info("pitch_max", rig.controller().camPitch(), "rad", "kPitchMax [PROV]");
        big.mouseDY = 100000;
        rig.step(big);
        r.info("pitch_min", rig.controller().camPitch(), "rad", "kPitchMin [PROV]");
    }
    {
        // Chase camera placement relative to the body.
        Rig rig(60, false);
        rig.idle(0.2);
        render::Camera cam = rig.camera();
        core::Vec3 eye = rig.pawn().position() + core::Vec3{0, kEyeHeight, 0};
        core::Vec3 toCam = cam.pos - eye;
        core::Vec3 toCamH = core::normalize(core::Vec3{toCam.x, 0, toCam.z});
        core::Vec3 face = core::forwardFromYawPitch(rig.pawn().yaw(), 0);
        r.near("camera_behind_body", core::dot(face, toCamH), -1.0, 1e-3, "dot",
               "third-person chase cam sits directly behind the facing direction");
        r.info("camera_distance", core::length(toCam), "m", "kCamDistance [PROV] - TnCamera orbit data not recovered");
        r.info("pawn_screen_x", toNdc(cam, eye).x, "ndc", "0 = centred; WFC shoulder offset (if any) not recovered");
        r.info("pawn_feet_screen_y", toNdc(cam, rig.pawn().position()).y, "ndc", "framing of the feet");
    }
    {
        // Aim: the hitscan ray must pass through the screen centre (crosshair).
        Rig rig(60, false);
        rig.idle(0.2);
        platform::InputFrame look;
        look.mouseDX = 37; look.mouseDY = -60;
        rig.step(look);
        size_t base = shotLog().size();
        rig.step(Rig::down({Button::Fire}));
        if (shotLog().size() > base) {
            const ShotRecord& s = shotLog()[base];
            render::Camera cam = rig.camera();
            core::Vec3 n = toNdc(cam, s.origin + core::normalize(s.dir) * 50.0f);
            r.near("aim_ray_through_crosshair", std::sqrt(n.x * n.x + n.y * n.y), 0.0, 1e-3, "ndc",
                   "hitscan dir = camera forward from the eye; impact must land under the crosshair");
            r.near("aim_origin_eye_height", s.origin.y - rig.pawn().position().y, kEyeHeight, 1e-4, "m",
                   "PlayerController: trace starts at pawn eye (CylinderHeight+BaseEyeHeight)");
        } else {
            r.truth("aim_shot_fired", false, "a held trigger must fire");
        }
    }
    {
        // Vehicle steers toward its travel direction.
        Rig rig(60, false);
        rig.pawn().setForm(game::Form::Vehicle);
        rig.idle(0.2);
        rig.hold(Rig::down({Button::Forward}), 1.5);
        rig.hold(Rig::down({Button::Right}), 2.0);
        const Frame& f = rig.last();
        r.near("vehicle_faces_travel", angleDeg(core::forwardFromYawPitch(f.yaw, 0), {f.vel.x, 0, f.vel.z}), 0.0, 0.5,
               "deg", "vehicle faces its velocity (steering), not the aim (FIDELITY pass 6, MED)");
        save(rig, "orient_vehicle_turn");
        // Saturating manoeuvre: full reversal at cruise demands a 180 deg heading change at once.
        Rig rev(60, false);
        rev.pawn().setForm(game::Form::Vehicle);
        rev.idle(0.2);
        rev.hold(Rig::down({Button::Forward}), 1.5);
        size_t from = rev.trace().size();
        rev.hold(Rig::down({Button::Back}), 2.5);
        double maxRate = 0;
        const auto& tr = rev.trace();
        for (size_t i = from; i < tr.size(); ++i) {
            double d = std::remainder((double)tr[i].yaw - tr[i - 1].yaw, 2 * core::PI);
            maxRate = std::max(maxRate, std::fabs(d) / rev.dt());
        }
        r.known("vehicle_max_yaw_rate", core::degrees((float)maxRate), 180.0, 5.0, "deg/s",
                "Default__TnHoverCarSimulationBlueprint.AiMaxAngularSpeed ~pi rad/s", kGameplay,
                "measured on a full reversal at cruise. Unlimited = facing snaps to velocity. Confirm in an "
                "original capture whether the 'Ai' rate also limits player steering.");
        save(rev, "orient_vehicle_reverse");
    }
}

// ---------------------------------------------------------------------------------------------
static void jumpMetrics(double hz, double& apex, double& air) {
    Rig rig(hz, false);
    rig.idle(0.3);
    double t0 = rig.time();
    rig.step(Rig::press(Button::Jump));
    rig.idle(2.5);
    apex = 0; air = -1;
    double tOff = -1;
    for (const Frame& f : rig.trace()) {
        if (f.t < t0) continue;
        apex = std::max(apex, (double)f.pos.y);
        if (tOff < 0 && !f.grounded) tOff = f.t - rig.dt();   // left the ground during this step
        else if (tOff >= 0 && air < 0 && f.grounded) air = f.t - tOff;
    }
    if (hz == 60.0) save(rig, "move_jump_standing");
}

void checkMovement(Report& r) {
    r.setGroup("movement");
    const double dt60 = 1.0 / 60.0;
    {
        Rig rig(60, false);
        rig.idle(0.3);
        double t0 = rig.time();
        rig.hold(Rig::down({Button::Forward}), 2.0);
        double tTop = firstAfter(rig, t0, [](const Frame& f) { return hspeed(f) >= kRobotMoveSpeed - 1e-3f; });
        r.near("robot_time_to_top_speed", tTop, kRobotMoveSpeed / kRobotAccel, dt60 + 1e-6, "s",
               "GroundSpeed 550 / AccelRate 2048 (linear accel)");
        r.near("robot_top_speed", hspeed(rig.last()), kRobotMoveSpeed, 1e-3, "m/s", "Default__TnPlayerPawn.GroundSpeed 550");
        double t1 = rig.time();
        rig.idle(1.0);
        double tStop = firstAfter(rig, t1, [](const Frame& f) { return hspeed(f) < 1e-3f; });
        core::Vec3 pStop = rig.last().pos;
        const Frame* at = nullptr;
        for (const Frame& f : rig.trace()) if (std::fabs(f.t - t1) < 1e-6) at = &f;
        r.info("robot_stop_time", tStop, "s",
               "rebuild brakes at AccelRate; original UE3 walking braking (friction) not yet captured",
               kRobotMoveSpeed / kRobotAccel);
        if (at) r.info("robot_stop_distance", core::length(pStop - at->pos), "m", "",
                       kRobotMoveSpeed * kRobotMoveSpeed / (2 * kRobotAccel));
        save(rig, "move_robot_run_stop");
    }
    {
        Rig rig(60, false);
        rig.idle(0.3);
        rig.hold(Rig::down({Button::Forward, Button::Right}), 1.5);
        r.near("robot_diagonal_speed", hspeed(rig.last()), kRobotMoveSpeed, 1e-3, "m/s", "input normalised: no diagonal speed boost");
        Rig back(60, false);
        back.idle(0.3);
        back.hold(Rig::down({Button::Back}), 1.5);
        r.info("robot_backpedal_speed", hspeed(back.last()), "m/s", "rebuild = GroundSpeed; any WFC backpedal scale not recovered");
    }
    {
        double apex, air;
        jumpMetrics(60, apex, air);
        r.known("robot_jump_apex", apex, kRobotMaxJumpH, 0.05, "m",
                "Default__TnPawn MaxJumpHeight 625 UU", kGameplay,
                "explicit Euler (velocity updated before position) overshoots by ~JumpZ*dt/2 and varies with "
                "tick rate; UE3 physFalling integrates with the average of old/new velocity, which lands on "
                "MaxJumpHeight. Fix: p += (vOld + vNew) * 0.5 * dt for the vertical axis.");
        r.near("robot_jump_airtime", air, 2 * kRobotJumpSpeed / kGravity, 2 * dt60, "s",
               "2 JumpZ / g (derived from MaxJumpHeight + DefaultGravityZ)");
        double a30, x30, a120, x120;
        jumpMetrics(30, a30, x30);
        jumpMetrics(120, a120, x120);
        r.info("robot_jump_apex_30hz", a30, "m", "frame-rate dependence (original apex is rate-independent)", kRobotMaxJumpH);
        r.info("robot_jump_apex_120hz", a120, "m", "", kRobotMaxJumpH);
    }
    {
        Rig rig(60, false);
        rig.idle(0.3);
        rig.hold(Rig::down({Button::Forward}), 1.0);
        double t0 = rig.time();
        core::Vec3 p0 = rig.pawn().position();
        platform::InputFrame jf = Rig::down({Button::Forward});
        jf.pressed[(int)Button::Jump] = true;
        rig.step(jf);
        rig.hold(Rig::down({Button::Forward}), 2.0);
        double land = firstAfter(rig, t0 + 0.1, [](const Frame& f) { return f.grounded; });
        core::Vec3 pl = p0;
        for (const Frame& f : rig.trace()) if (f.t > t0 + 0.1 && f.grounded) { pl = f.pos; break; }
        r.info("robot_running_jump_distance", core::length(core::Vec3{pl.x - p0.x, 0, pl.z - p0.z}), "m",
               "jog jump", kRobotMoveSpeed * 2 * kRobotJumpSpeed / kGravity);
        r.info("robot_running_jump_land_time", land, "s", "");
        save(rig, "move_jump_running");

        Rig ac(60, false);
        ac.idle(0.3);
        ac.step(Rig::press(Button::Jump));
        ac.hold(Rig::down({Button::Right}), 0.6);
        r.info("robot_air_control_lateral_speed_0p6s", hspeed(ac.last()), "m/s",
               "standing jump + strafe: AirControl 0.70 x AccelRate, capped at GroundSpeed",
               std::min(0.6 * kRobotAccel * kAirControl, (double)kRobotMoveSpeed));
    }
    {
        Rig rig(60, false);
        rig.pawn().setForm(game::Form::Vehicle);
        rig.idle(0.5);
        r.near("vehicle_hover_height", rig.pawn().position().y - rig.pawn().groundY, kVehicleHoverH, 1e-3, "m",
               "TnHoverCarSimulationBlueprint.SuspensionRadius 200");
        double t0 = rig.time();
        rig.hold(Rig::down({Button::Forward}), 2.0);
        r.near("vehicle_cruise_speed", hspeed(rig.last()), kVehicleMoveSpeed, 1e-3, "m/s", "MaxLinearSpeed 1500");
        r.near("vehicle_time_to_cruise",
               firstAfter(rig, t0, [](const Frame& f) { return hspeed(f) >= kVehicleMoveSpeed - 1e-3f; }),
               kVehicleMoveSpeed / kVehicleAccel, dt60 + 1e-6, "s", "MaxLinearAcceleration 3000");
        double t1 = rig.time();
        rig.hold(Rig::down({Button::Forward, Button::Sprint}), 2.0);
        r.near("vehicle_dash_speed", hspeed(rig.last()), kVehicleBoostSpeed, 1e-3, "m/s", "DashSpeed 5000");
        r.info("vehicle_dash_rise_time",
               firstAfter(rig, t1, [](const Frame& f) { return hspeed(f) >= kVehicleBoostSpeed - 1e-3f; }), "s",
               "cruise -> DashSpeed", (kVehicleBoostSpeed - kVehicleMoveSpeed) / (kVehicleBoostSpeed / kVehicleDashTime));
        double above = 0;
        for (const Frame& f : rig.trace()) if (f.t > t1 && hspeed(f) > kVehicleMoveSpeed + 0.5f) above += rig.dt();
        r.info("vehicle_dash_sustained", above, "s",
               "SUSPECT (MED): held Sprint sustains DashSpeed indefinitely; DashDuration 0.3 suggests a timed burst. "
               "Owner Gameplay - capture original boost speed curve", kVehicleDashTime);
        double t2 = rig.time();
        rig.hold(Rig::down({Button::Forward}), 2.0);
        r.info("vehicle_dash_decay_time",
               firstAfter(rig, t2, [](const Frame& f) { return hspeed(f) <= kVehicleMoveSpeed + 1e-3f; }), "s",
               "SUSPECT (MED): releasing Sprint snaps 50 -> 15 m/s in one step (grounded speed cap clamps instantly). "
               "Model value = decel at MaxLinearAcceleration", (kVehicleBoostSpeed - kVehicleMoveSpeed) / kVehicleAccel);
        save(rig, "move_vehicle_cruise_dash");

        Rig vj(60, false);
        vj.pawn().setForm(game::Form::Vehicle);
        vj.idle(0.5);
        double base = vj.pawn().position().y;
        vj.step(Rig::press(Button::Jump));
        vj.idle(2.0);
        double apex = base;
        for (const Frame& f : vj.trace()) apex = std::max(apex, (double)f.pos.y);
        r.known("vehicle_jump_apex", apex - base, kVehicleJumpSpeed * kVehicleJumpSpeed / (2 * kVehicleGravity), 0.05, "m",
                "JumpLinearSpeed 1200 under RB gravity -19.4 (derived)", kGameplay,
                "same explicit-Euler overshoot as robot_jump_apex");
        save(vj, "move_vehicle_jump");
    }
}

// ---------------------------------------------------------------------------------------------
void checkCollision(Report& r) {
    r.setGroup("collision");
    const float wallZ = -10.0f;
    {
        BoxScene s;
        s.floor(0, 100);
        s.box({-30, 0, -14}, {30, 10, wallZ});
        game::CollisionWorld col;
        col.build(s.mesh);
        Rig rig(60, false);
        rig.setCollision(&col);
        rig.idle(0.2);
        r.near("flat_floor_rest_height", rig.pawn().position().y, 0.0, 1e-4, "m", "grounding on a flat floor");
        rig.hold(Rig::down({Button::Forward}), 4.0);
        float gap = rig.pawn().position().z - wallZ;
        r.truth("wall_blocks", gap > 0, "a 10 m wall must stop the pawn");
        r.near("wall_stop_gap", gap, kPawnRadius, 0.15, "m", "pawn cylinder radius 175 UU: body stops one radius from the wall");
        save(rig, "col_wall_head_on");

        Rig sl(60, false);
        sl.setCollision(&col);
        sl.idle(0.2);
        sl.hold(Rig::down({Button::Forward}), 3.0);    // reach the wall
        double x0 = sl.pawn().position().x, t0 = sl.time();
        sl.hold(Rig::down({Button::Forward, Button::Right}), 2.0);
        double along = (sl.pawn().position().x - x0) / (sl.time() - t0);
        r.known("wall_slide_speed", along, kRobotMoveSpeed * std::sqrt(0.5), 0.3, "m/s",
                "UE3 SlideAlongSurface keeps the tangential velocity on contact (engine behaviour)", kGameplay,
                "CharacterMovement zeroes ALL horizontal velocity when the probe hits, so diagonal input against a "
                "wall sticks instead of sliding (WFC street corners/doorways feel sticky)");
        save(sl, "col_wall_slide");
    }
    {
        // Ramps: UE3 WalkableFloorZ default 0.7 -> surfaces up to ~45.6 deg are walkable.
        auto climbs = [&](float deg) {
            BoxScene s;
            s.floor(0, 100);
            s.ramp(-30, 30, -4, 10, deg);
            game::CollisionWorld col;
            col.build(s.mesh);
            Rig rig(60, false);
            rig.setCollision(&col);
            rig.idle(0.2);
            rig.hold(Rig::down({Button::Forward}), 5.0);
            if (!gOpt.traceDir.empty()) save(rig, "col_ramp_" + std::to_string((int)deg));
            return rig.pawn().position().z < -14.5f;   // reached the top platform
        };
        r.truth("ramp_20deg_climbable", climbs(20), "UE3 WalkableFloorZ 0.7: gentle ramps are walkable");
        r.truth("ramp_35deg_climbable", climbs(35), "UE3 WalkableFloorZ 0.7 (~45.6 deg)");
        r.truth("ramp_60deg_blocks", !climbs(60), "UE3 WalkableFloorZ 0.7: a 60 deg slope is a wall");
    }
    {
        // Step / ledge probe: raised platform of height h ahead of the pawn.
        const float hs[] = {0.2f, 0.3f, 0.35f, 0.4f, 0.5f, 0.6f, 0.7f, 0.9f, 1.1f, 1.3f, 1.6f, 1.9f};
        float maxClimb = 0;
        float firstPenetrated = -1;
        std::string detail;
        for (float h : hs) {
            BoxScene s;
            s.floor(0, 100);
            s.box({-30, 0, -40}, {30, h, -6});
            game::CollisionWorld col;
            col.build(s.mesh);
            Rig rig(60, false);
            rig.setCollision(&col);
            rig.idle(0.2);
            rig.hold(Rig::down({Button::Forward}), 3.0);
            core::Vec3 p = rig.pawn().position();
            bool climbed = p.z < -6.5f && std::fabs(p.y - h) < 0.01f;
            bool inside = p.z < -6.0f && p.y < h - 0.01f;
            if (climbed) maxClimb = std::max(maxClimb, h);
            if (inside && firstPenetrated < 0) firstPenetrated = h;
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.2f:%s ", h, climbed ? "climb" : inside ? "INSIDE" : "blocked");
            detail += buf;
        }
        r.known("max_step_height", maxClimb, 0.35, 0.05, "m",
                "UE3 Pawn.MaxStepHeight default 35 UU (no TnPawn override found - CharacterMovement.cpp note)", kGameplay,
                "kStepUp 0.6 is applied twice (search top + groundHeight window) so ledges up to ~1.2 m are "
                "stepped onto. Per-height: " + detail);
        r.knownTruth("no_low_obstacle_penetration", firstPenetrated < 0,
                     "collision invariant: the pawn cylinder never ends inside solid geometry", kGameplay,
                     "wall probe is one ray at capsule centre (2 m): ledges above the step window but below 2 m "
                     "are entered. First penetrated height: " + std::to_string(firstPenetrated));
    }
}

// ---------------------------------------------------------------------------------------------
void checkTransform(Report& r) {
    r.setGroup("transform");
    const Models* m = Models::get();
    if (!m) { r.skip("transform", "Optimus robot.glb/vehicle.glb unavailable"); return; }
    // Authored data: the plain (non-SuperBoost) fold is one clip per mesh with identical length.
    auto dur = [](const assets::SkinnedModel& mdl, const char* name) {
        int c = mdl.clipByName(name);
        return c >= 0 ? (double)mdl.clips[(size_t)c].duration : -1.0;
    };
    double rv = dur(m->robot, "Transform_ToVehicle_ROBO"), vv = dur(m->vehicle, "Transform_ToVehicle_VEH");
    double vr = dur(m->vehicle, "Transform_ToRobot_VEH"), rr = dur(m->robot, "Transform_ToRobot_ROBO");
    r.near("clip_robot_to_vehicle", rv, 1.9667, 0.01, "s", "robot.glb Transform_ToVehicle_ROBO");
    r.near("clip_vehicle_to_vehicle", vv, rv, 1e-3, "s", "vehicle.glb Transform_ToVehicle_VEH pairs with the robot clip");
    r.near("clip_vehicle_to_robot", vr, 1.1333, 0.01, "s", "vehicle.glb Transform_ToRobot_VEH");
    r.near("clip_robot_to_robot", rr, vr, 1e-3, "s", "robot.glb Transform_ToRobot_ROBO pairs with the vehicle clip");
    r.info("superboost_variants", dur(m->robot, "Transform_ToVehicle_SuperBoost_Robo"), "s",
           "Transform_ToVehicle_SuperBoost_Robo (Veh: " + std::to_string(dur(m->vehicle, "Transform_ToVehicle_SuperBoost_Veh")) +
           " s) share the transform categories - a separate dash-transform, not the normal fold");

    struct Case { const char* name; game::Form from; double clip; const char* outClip; const char* inClip; };
    for (Case c : {Case{"to_vehicle", game::Form::Robot, rv, "Transform_ToVehicle_ROBO", "Transform_ToVehicle_VEH"},
                   Case{"to_robot", game::Form::Vehicle, vr, "Transform_ToRobot_VEH", "Transform_ToRobot_ROBO"}}) {
        Rig rig(60, true);
        rig.pawn().setForm(c.from);
        rig.idle(0.5);
        double t0 = rig.time();
        rig.step(Rig::press(Button::Transform));
        rig.hold(Rig::down({Button::Forward, Button::Fire}), 1.0);    // must be ignored while folding
        rig.idle(2.0);
        std::string id = c.name;
        // Times are measured from the start of the step in which the button was pressed.
        double handoff = firstAfter(rig, t0, [&](const Frame& f) { return f.form != c.from; });
        double end = firstAfter(rig, t0, [&](const Frame& f) { return !f.transforming; });
        r.info(id + "_handoff_time", handoff, "s", "kTransformHandoffFrac [PROV] x clip", c.clip * kTransformHandoffFrac);
        bool outOk = false, inOk = false;
        std::string inPlayed;
        for (const Frame& f : rig.trace()) {
            if (f.t <= t0 || !f.transforming) continue;
            if (f.anim == c.outClip) outOk = true;
            if (f.form != c.from && inPlayed.empty()) inPlayed = f.anim;
        }
        inOk = inPlayed == c.inClip;
        r.truth(id + "_outgoing_clip", outOk, std::string("plays ") + c.outClip);
        r.knownTruth(id + "_incoming_clip_paired", inOk, std::string("handoff must resume the paired ") + c.inClip,
                     kGameplay, "HIGH: Character::updateAnimation uses firstClipOfCategory(); vehicle.glb lists "
                     "Transform_ToVehicle_SuperBoost_Veh (0.80 s) first in vehicle_transform_to_vehicle, so the 1.97 s "
                     "robot fold hands off into the wrong (SuperBoost) clip at 50% -> visible pop + transform ends "
                     "~0.5 s early. Played: " + inPlayed + ". Fix: select by name / exclude SuperBoost / match duration.");
        r.known(id + "_total_time", end, c.clip, 2 * rig.dt() + 1e-6, "s", "transform lasts one paired clip", kGameplay,
                "includes 1 swallowed step (playClip resets animTime on the first transform step) and the clip "
                "pairing bug above where present");
        bool moved = false, fired = false, gun = false;
        std::string seq, lastAnim;
        for (const Frame& f : rig.trace()) {
            if (f.t <= t0 || !f.transforming) continue;
            moved |= hspeed(f) > 1e-4f;
            gun |= f.weaponVisible;
            fired |= f.shots > 0;
            if (f.anim != lastAnim) { seq += (seq.empty() ? "" : " > ") + f.anim; lastAnim = f.anim; }
        }
        r.truth(id + "_input_locked", !moved && !fired, "TnTransformation: movement/fire input ignored during the fold");
        r.truth(id + "_weapon_holstered", !gun, "Ion Blaster hidden for the whole transform (pass 6)");
        r.info(id + "_post_anim_settle", end, "s", "clips: " + seq + " -> " + rig.last().anim);
        save(rig, std::string("transform_") + c.name);
    }
    {
        Rig rig(60, true);
        rig.idle(0.2);
        rig.hold(Rig::down({Button::Forward}), 1.0);
        platform::InputFrame f = Rig::down({Button::Forward});
        f.pressed[(int)Button::Transform] = true;
        rig.step(f);
        r.info("transform_while_running_speed", hspeed(rig.last()), "m/s",
               "SUSPECT (LOW): rebuild zeroes velocity at transform start; WFC transforming on the move appears "
               "to keep momentum - capture original");
    }
}

// ---------------------------------------------------------------------------------------------
void checkAnimation(Report& r) {
    r.setGroup("animation");
    const Models* m = Models::get();
    if (!m) { r.skip("animation", "Optimus models unavailable"); return; }
    r.info("robot_clip_count", (double)m->robot.clips.size(), "clips", "STATUS: 75");
    r.info("vehicle_clip_count", (double)m->vehicle.clips.size(), "clips", "STATUS: 13");
    assets::Json cj;
    if (loadJsonFile(Models::assetRoot() + "/Characters/Optimus/character.json", cj) && cj["robot"]["joints"].isNumber())
        r.near("robot_skin_joints", (double)m->robot.skinJoints.size(), cj["robot"]["joints"].asDouble(), 0, "joints",
               "character.json robot.joints");

    struct Dir { const char* name; Button b; const char* suffix; };
    for (Dir d : {Dir{"forward", Button::Forward, "_F"}, Dir{"back", Button::Back, "_B"},
                  Dir{"right", Button::Right, "_R"}, Dir{"left", Button::Left, "_L"}}) {
        Rig rig(60, true);
        rig.idle(0.3);
        rig.hold(Rig::down({d.b}), 1.0);
        const std::string& a = rig.last().anim;
        bool ok = a.find("Strafe") != std::string::npos && a.size() >= 2 && a.compare(a.size() - 2, 2, d.suffix) == 0;
        r.truth(std::string("locomotion_") + d.name, ok,
                "robot.glb Nav_Strafe*_{F,B,L,R} chosen by travel relative to facing", "anim=" + a);
    }
    {
        Rig rig(60, true);
        rig.idle(1.0);
        int c = m->robot.clipByName(rig.last().anim);
        r.truth("idle_category", c >= 0 && m->robot.clips[(size_t)c].category == "idle", "idle state plays an idle-category clip",
                "anim=" + rig.last().anim);
        rig.step(Rig::press(Button::Jump));
        rig.idle(0.2);
        int jc = m->robot.clipByName(rig.last().anim);
        r.truth("jump_category", jc >= 0 && m->robot.clips[(size_t)jc].category == "jump", "ascending pawn plays a jump clip",
                "anim=" + rig.last().anim);
        rig.idle(0.6);
        int fc = m->robot.clipByName(rig.last().anim);
        r.truth("fall_category", fc >= 0 && m->robot.clips[(size_t)fc].category == "fall", "descending pawn plays a fall clip",
                "anim=" + rig.last().anim);
        save(rig, "anim_idle_jump");
    }
    {
        Rig rig(60, true);
        rig.idle(0.3);
        rig.hold(Rig::down({Button::Fire}), 0.3);
        rig.step(Rig::press(Button::Reload));
        rig.idle(0.5);
        // Either the full-body base clip (pre-layer builds) or the upper-body reload slot (layered).
        const Frame& lf = rig.last();
        r.truth("reload_clip", lf.anim == "Shooting_Reload_IonBlaster_ROBO" || lf.reloadW > 0.5f,
                "robot.glb reload category (base clip or reload slot weight > 0.5)",
                "anim=" + lf.anim + " reloadW=" + std::to_string(lf.reloadW));
        save(rig, "anim_reload");

        Rig mv(60, true);
        mv.idle(0.3);
        mv.hold(Rig::down({Button::Fire}), 0.3);
        platform::InputFrame f = Rig::down({Button::Forward});
        f.pressed[(int)Button::Reload] = true;
        mv.step(f);
        mv.hold(Rig::down({Button::Forward}), 0.5);
        const Frame& mf = mv.last();
        bool legsRun = mf.anim.find("Strafe") != std::string::npos;
        r.knownTruth("reload_on_the_move", legsRun && mf.reloadW > 0.5f && mf.reloading,
                     "WFC layers the reload over locomotion (robot.glb ADD_Shooting_Reload_* / upper-body slot): "
                     "legs keep jogging while the arms reload", kGameplay,
                     "anim=" + mf.anim + " reloadW=" + std::to_string(mf.reloadW));
        save(mv, "anim_reload_moving");
    }
    {
        // Upper-body aim offset: the barrel should follow the camera pitch (reticle).
        auto barrelPitchAt = [](float pitch, float& outAimN) {
            Rig rig(60, true);
            rig.idle(0.2);
            platform::InputFrame look;
            look.mouseDY = -(pitch - rig.controller().camPitch()) / kMouseSens;
            rig.step(look);
            rig.idle(0.6);   // let any aim smoothing settle
            outAimN = rig.last().aimPitchN;
            core::Vec3 b = core::normalize(core::transformDir(rig.pawn().weaponWorld(), {1, 0, 0}));
            return core::degrees(std::asin(core::clampf(b.y, -1, 1)));
        };
        float nUp, nDn, nMid;
        float up = barrelPitchAt(0.4f, nUp), dn = barrelPitchAt(-0.4f, nDn), mid = barrelPitchAt(0.0f, nMid);
        double ratio = (up - dn) / core::degrees(0.8f);
        r.info("barrel_pitch_cam_up_0p4", up, "deg", "camera pitch +22.9 deg");
        r.info("barrel_pitch_cam_level", mid, "deg", "camera pitch 0");
        r.info("barrel_pitch_cam_down_0p4", dn, "deg", "camera pitch -22.9 deg");
        // Only the direction is enforced: the exact gain comes from the authored
        // TnAnimNodeAimOffset profile (Pass 9 measures ~0.7), and the hitscan uses the camera ray.
        r.knownTruth("aim_pitch_follows_camera", up > mid + 2.0f && mid > dn + 2.0f,
                     "Robot_ANIMTREE TnAnimNodeAimOffset: the gun pitches with the aim", kGameplay,
                     "barrel pitch must rise monotonically with camera pitch (no upper-body aim offset yet)");
        r.info("aim_pitch_tracking", ratio, "ratio",
               "d(barrel pitch)/d(camera pitch) over +-0.4 rad; gain set by the authored aim-offset profile, "
               "not enforced (capture original to compare)");
        int rc = m->robot.clipByName("Shooting_Reload_IonBlaster_ROBO");
        if (rc >= 0)
            r.info("reload_clip_vs_gameplay", m->robot.clips[(size_t)rc].duration, "s",
                   "clip length vs WeaponReloadAnimTime 1.5: clip is cut when the timer ends", 1.5);
    }
}

// ---------------------------------------------------------------------------------------------
void checkMuzzle(Report& r) {
    r.setGroup("muzzle");
    const Models* m = Models::get();
    if (!m) { r.skip("muzzle", "Optimus models unavailable"); return; }
    const float* s = m->socket.m;
    core::Vec3 c0{s[0], s[1], s[2]}, c1{s[4], s[5], s[6]}, c2{s[8], s[9], s[10]};
    float det = core::dot(c0, core::cross(c1, c2));
    float ortho = std::fabs(core::dot(c0, c1)) + std::fabs(core::dot(c1, c2)) + std::fabs(core::dot(c0, c2));
    r.near("socket_rotation_det", det, 1.0, 1e-3, "", "socket rotator -> matrix must be a proper rotation");
    r.near("socket_rotation_orthogonality", ortho, 0.0, 1e-3, "", "");

    // Skeleton-derived body facing: forward = up x (R_shoulder - L_shoulder), using the same model
    // matrix Character::draw uses. Independent of the yaw variable, so it catches a wrong
    // kMeshYawOffset that every yaw-only metric (face.toCam etc.) is blind to.
    {
        const assets::SkinnedModel& mdl = m->robot;
        int idle = mdl.firstClipOfCategory("idle");
        std::vector<core::Mat4> g;
        render::MeshData tmp;
        assets::evaluatePose(mdl, idle, 0.0f, g, tmp, true);
        const float yaw = 0.7f;   // arbitrary non-zero test yaw
        core::Mat4 model = core::Mat4::rotateY(yaw + kMeshYawOffset);
        auto bone = [&](const char* n) {
            int i = mdl.nodeByName(n);
            const core::Mat4& b = g[(size_t)i];
            return core::transformPoint(model, {b.m[12], b.m[13], b.m[14]});
        };
        struct Pair { const char* id; const char* l; const char* rr; };
        for (Pair p : {Pair{"shoulders", "L_Arm02_Shoulder_XB", "R_Arm02_Shoulder_XB"},
                       Pair{"thighs", "L_Leg01_Thigh_XB", "R_Leg01_Thigh_XB"}}) {
            if (mdl.nodeByName(p.l) < 0 || mdl.nodeByName(p.rr) < 0) { r.skip(std::string("mesh_facing_") + p.id, "bones missing"); continue; }
            core::Vec3 lr = bone(p.rr) - bone(p.l);
            core::Vec3 meshFwd = core::normalize(core::cross({0, 1, 0}, core::Vec3{lr.x, 0, lr.z}));
            core::Vec3 face = core::forwardFromYawPitch(yaw, 0);
            float signedDeg = core::degrees(std::atan2(core::dot(core::cross(face, meshFwd), {0, 1, 0}), core::dot(face, meshFwd)));
            r.known(std::string("mesh_facing_vs_yaw_") + p.id, signedDeg, 0.0, 25.0, "deg",
                    "skeleton R-L axis: rendered body must face the gameplay yaw (UE meshes are +X-forward; "
                    "umodel maps UE +X -> glTF +X, rebuild forward is -Z)", kGameplay,
                    "HIGH: kMeshYawOffset=0 renders Optimus side-on to the chase cam (screenshot-confirmed). "
                    "Experiment with +pi/2 gives shoulders/hips within ~18 deg (idle stance twist) and a back view. "
                    "Tolerance 25 deg covers stance twist; ~+-90 = offset wrong, ~180 = facing the camera");
        }
        // Vehicle: the truck's long bind-pose axis must line up with its facing.
        const assets::SkinnedModel& veh = m->vehicle;
        core::Vec3 ext = veh.boundsMax - veh.boundsMin;
        core::Vec3 longLocal = ext.x >= ext.z ? core::Vec3{1, 0, 0} : core::Vec3{0, 0, 1};
        core::Vec3 longWorld = core::transformDir(model, longLocal);
        float off = angleDeg(longWorld, core::forwardFromYawPitch(yaw, 0));
        if (off > 90) off = 180 - off;
        r.known("vehicle_long_axis_vs_yaw", off, 0.0, 10.0, "deg",
                "vehicle bind-pose long axis must run along its facing/travel", kGameplay,
                "same kMeshYawOffset cause: the truck drives sideways when ~90");
        r.info("vehicle_bind_extent_x", ext.x, "m", "");
        r.info("vehicle_bind_extent_z", ext.z, "m", "");
    }

    Rig rig(60, true);
    rig.idle(0.5);
    const Frame& f = rig.last();
    if (!f.weaponVisible) { r.truth("weapon_visible_idle", false, "weapon attached in robot idle"); return; }
    {
        const core::Mat4& wm = rig.pawn().weaponWorld();
        core::Vec3 hand = core::Vec3{wm.m[12], wm.m[13], wm.m[14]} - f.pos;
        core::Vec3 fw = core::forwardFromYawPitch(f.yaw, 0);
        r.info("weapon_origin_forward", core::dot(hand, fw), "m", "weapon attach (socket) in facing frame");
        r.info("weapon_origin_right", core::dot(hand, core::normalize(core::cross(fw, {0, 1, 0}))), "m", "");
    }
    core::Vec3 fwd = core::forwardFromYawPitch(f.yaw, 0);
    core::Vec3 right = core::normalize(core::cross(fwd, {0, 1, 0}));
    core::Vec3 d = f.muzzle - f.pos;
    r.knownTruth("muzzle_in_front_of_body", core::dot(d, fwd) > 0.0f, "barrel tip must be ahead of the pawn origin",
                 kGameplay, "follows from mesh_facing_vs_yaw (muzzle is behind the body while kMeshYawOffset=0)");
    r.info("muzzle_forward", core::dot(d, fwd), "m", "idle pose, facing frame");
    r.info("muzzle_right", core::dot(d, right), "m", "+ = right of the body");
    r.info("muzzle_height", d.y, "m", "above feet");
    core::Vec3 barrel = core::transformDir(rig.pawn().weaponWorld(), {1, 0, 0});
    r.info("barrel_vs_facing_deg", angleDeg({barrel.x, 0, barrel.z}, fwd), "deg",
           "horizontal angle between barrel axis (+X weapon-local) and facing; 0 = gun points where the body faces");
    r.info("barrel_pitch_deg", core::degrees(std::asin(core::clampf(core::normalize(barrel).y, -1, 1))), "deg", "");
    // Parallax: perpendicular distance from the muzzle to the hitscan ray (tracer vs actual trace).
    core::Vec3 eye = f.pos + core::Vec3{0, kEyeHeight, 0};
    core::Vec3 aim = core::forwardFromYawPitch(f.camYaw, f.camPitch);
    core::Vec3 rel = f.muzzle - eye;
    r.info("muzzle_to_aim_ray_dist", core::length(rel - aim * core::dot(rel, aim)), "m",
           "tracer starts here but damage traces from the eye; large values = visible tracer/impact mismatch");
    Rig run(60, true);
    run.idle(0.3);
    run.hold(Rig::down({Button::Forward}), 1.0);
    double mn = 1e9, mx = -1e9;
    for (const Frame& g : run.trace()) {
        if (g.t < 0.8 || !g.weaponVisible) continue;
        double h = g.muzzle.y - g.pos.y;
        mn = std::min(mn, h); mx = std::max(mx, h);
    }
    r.info("muzzle_bob_running", mx - mn, "m", "vertical muzzle travel during the jog cycle");
    save(run, "muzzle_running");
}

// ---------------------------------------------------------------------------------------------
void checkDeterminism(Report& r) {
    r.setGroup("determinism");
    auto run = [] {
        Rig rig(60, Models::get() != nullptr);
        rig.idle(0.2);
        rig.hold(Rig::down({Button::Forward, Button::Right}), 0.7);
        rig.step(Rig::press(Button::Jump));
        rig.hold(Rig::down({Button::Fire, Button::Left}), 1.3);
        rig.step(Rig::press(Button::Transform));
        rig.idle(2.5);
        return rig.trace();
    };
    std::vector<Frame> a = run(), b = run();
    bool same = a.size() == b.size();
    for (size_t i = 0; same && i < a.size(); ++i)
        same = a[i].pos.x == b[i].pos.x && a[i].pos.y == b[i].pos.y && a[i].pos.z == b[i].pos.z &&
               a[i].yaw == b[i].yaw && a[i].anim == b[i].anim && a[i].animT == b[i].animT && a[i].ammo == b[i].ammo;
    r.truth("replay_identical", same, "same scripted input -> bit-identical trace (A/B tooling prerequisite)");
}

void checkPerformance(Report& r) {
    r.setGroup("performance");
    const Models* m = Models::get();
    if (!m) { r.skip("skinning", "models unavailable"); return; }
    r.info("robot_vertices", (double)m->robot.vertexCount(), "verts", "CPU-skinned every sim step");
    Rig rig(60, true);
    rig.idle(0.2);
    auto t0 = std::chrono::steady_clock::now();
    rig.hold(Rig::down({Button::Forward}), 10.0);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 600.0;
    r.info("sim_step_cost_running", ms, "ms", "controller + movement + anim eval + CPU skin + crossfade (Debug build "
           "unless configured Release); budget at 60 Hz is 16.7 ms total");
}

void checkMap(Report& r) {
    r.setGroup("map");
    if (!gOpt.map) { r.skip("streets_collision", "pass --map to load the Streets collision mesh"); return; }
    render::MeshData mesh;
    auto t0 = std::chrono::steady_clock::now();
    if (!assets::loadGlb(Models::assetRoot() + "/Maps/MP_IAC_Streets/collision.glb", mesh)) {
        r.skip("streets_collision", "collision.glb unavailable");
        return;
    }
    game::CollisionWorld col;
    col.build(mesh);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    r.info("collision_triangles", (double)col.triangleCount(), "tris", "STATUS: ~1.85 M");
    r.info("collision_load_build", ms, "ms", "glb load + grid build");
    // Documented FFA spawn (STATUS P1).
    core::Vec3 spawn{363.5f, -723.6f, -341.8f};
    float gy; core::Vec3 n;
    bool hit = col.groundHeight(spawn.x, spawn.z, spawn.y + 0.5f, 1.0f, gy, n);
    r.truth("spawn_has_floor", hit, "authored FFA spawn sits on collision");
    if (hit) r.info("spawn_floor_y", gy, "m", "STATUS: -724.5");
    // Traversal sweep: from the spawn, walk 4 s in 8 compass directions (camera yaw rotated) and
    // record how far the pawn gets, how long it is stuck against input and how often it leaves
    // the ground. Compare runs with diff-reports.ps1 to see how a collision change plays on the
    // real street layout (synthetic boxes cannot show this).
    double totalDist = 0, stuck = 0, steps = 0, air = 0, stepMs = 0, minY = 1e9, maxY = -1e9;
    for (int k = 0; k < 8; ++k) {
        Rig rig(60, false);
        rig.setCollision(&col);
        rig.pawn().setPosition({spawn.x, hit ? gy : spawn.y, spawn.z});
        rig.controller().setCameraYaw(core::PI * 0.25f * k);
        rig.idle(0.3);
        core::Vec3 p0 = rig.pawn().position();
        size_t first = rig.trace().size();
        auto t1 = std::chrono::steady_clock::now();
        rig.hold(Rig::down({Button::Forward}), 4.0);
        stepMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        core::Vec3 p1 = rig.pawn().position();
        double d = core::length(core::Vec3{p1.x - p0.x, 0, p1.z - p0.z});
        totalDist += d;
        for (size_t i = first; i < rig.trace().size(); ++i) {
            const Frame& f = rig.trace()[i];
            steps += 1;
            if (f.t - rig.trace()[first].t > 0.4 && hspeed(f) < 0.5f) stuck += rig.dt();
            if (!f.grounded) air += 1;
            minY = std::min(minY, (double)f.pos.y);
            maxY = std::max(maxY, (double)f.pos.y);
        }
        r.info("sweep_dist_dir" + std::to_string(k * 45), d, "m", "", kRobotMoveSpeed * 4.0);
        save(rig, "map_sweep_" + std::to_string(k * 45));
    }
    r.info("sweep_total_distance", totalDist, "m", "8 directions x 4 s", 8 * kRobotMoveSpeed * 4.0);
    r.info("sweep_stuck_time", stuck, "s", "input held but speed < 0.5 m/s (after 0.4 s spin-up)");
    r.info("sweep_airborne_frac", air / steps, "", "fraction of steps not grounded");
    r.info("sweep_height_range", maxY - minY, "m", "");
    r.truth("sweep_no_fall_through", minY > (hit ? gy : spawn.y) - 30.0, "pawn never drops through the street floor");
    r.info("collision_step_cost", stepMs / (8 * 240), "ms", "movement step against the 1.85 M-tri grid");
}

// ---------------------------------------------------------------------------------------------
void compareReference(Report& r, const std::string& path) {
    r.setGroup("original_vs_rebuild");
    assets::Json j;
    if (!loadJsonFile(path, j)) { r.skip("reference", "cannot read " + path); return; }
    const assets::Json& ms = j["measurements"];
    int pending = 0;
    for (const auto& kv : ms.obj) {
        const assets::Json& e = kv.second;
        double rebuild;
        if (!r.metric(kv.first, rebuild)) { r.skip(kv.first, "no rebuild metric with this id"); continue; }
        if (!e["original"].isNumber()) { ++pending; continue; }
        r.near(kv.first, rebuild, e["original"].asDouble(), e["tol"].asDouble(0.0), e["unit"].asString(),
               "original capture: " + e["method"].asString());
    }
    r.info("pending_original_captures", pending, "", "reference entries still awaiting an original-game measurement");
}

} // namespace fid
