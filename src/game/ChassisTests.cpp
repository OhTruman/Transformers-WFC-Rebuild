// Clean-room reconstruction — WFC_CHASSISTEST: every roster chassis definition loads from the AssetTools export, the
// "Truck" definition reproduces the hand-entered Optimus constants, and socket math matches the recovered matrix.
#include "game/ChassisDef.h"
#include "core/Config.h"
#include "core/Log.h"
#include <cmath>
#include <cstdlib>

namespace game {

int runChassisTests(const std::string& vsRoot) {
    int checks = 0, fails = 0;
    auto check = [&](bool ok, const std::string& what) { ++checks; if (!ok) ++fails; LOG_INFO("CHASSIS %s %s", ok ? "PASS" : "FAIL", what.c_str()); };
    auto near = [](float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; };

    // Socket conversion reproduces the Optimus WeaponSocket_Primary matrix used since Pass 3.
    {
        float loc[3] = {-40, 0, 0}; int rot[3] = {0, 31311, 5461};
        core::Mat4 m = ueSocketToGltf(loc, rot);
        const float ref[16] = {-0.990259f, 0.0f, 0.139234f, 0.0f, -0.069613f, 0.866041f, -0.495102f, 0.0f,
                               -0.120583f, -0.499972f, -0.857606f, 0.0f, -0.4f, 0.0f, 0.0f, 1.0f};
        bool ok = true;
        for (int i = 0; i < 16; ++i) ok &= near(m.m[i], ref[i], 2e-4f);
        check(ok, "ueSocketToGltf reproduces the recovered Optimus WeaponSocket_Primary matrix");
    }

    ChassisDef t;
    bool ok = loadChassisDef(vsRoot, "Truck", t);
    check(ok, "Truck (Optimus) loads" + (ok ? std::string() : ": " + t.loadError));
    if (ok) {
        namespace cfg = core::config;
        const RobotParams& R = t.robot; const VehicleParams& V = t.vehicle;
        check(near(R.radius, cfg::kPawnRadius) && near(R.halfHeight, cfg::kPawnHalfHeight), "Truck collision 200/200 UU");
        check(near(R.groundSpeed, cfg::kRobotMoveSpeed) && near(R.accel, cfg::kRobotAccel) && near(R.airSpeed, cfg::kAirSpeed) &&
              near(R.airControl, cfg::kAirControl) && near(R.jumpSpeed(), cfg::kRobotJumpSpeed, 0.01f), "Truck robot speeds / jump");
        check(near(R.momGroundFwd, cfg::kMomentumGroundFwd) && near(R.momAirFwd, cfg::kMomentumAirFwd), "Truck momentum");
        check(V.form == VehicleFormType::Truck && near(V.hoverSpeed, cfg::kVehicleMoveSpeed) && near(V.hoverAccel, cfg::kVehicleAccel) &&
              near(V.dashSpeed, cfg::kVehicleBoostSpeed) && near(V.dashTime, cfg::kVehicleDashTime), "Truck hover sim (HoverTruck_Physics)");
        check(near(V.suspRest, cfg::kSuspRestLength) && near(V.suspStiffness, cfg::kSuspStiffness) && near(V.suspDamping, cfg::kSuspDamping) &&
              near(V.suspMountRadius, cfg::kSuspMountRadius), "Truck suspension (HoverTruck_Suspension)");
        check(near(V.mass, cfg::kVehMass) && near(V.inertiaX, cfg::kVehInertiaX, 0.5f) && near(V.inertiaY, cfg::kVehInertiaY, 0.5f) &&
              near(V.comFwd, cfg::kVehComFwd) && near(V.comUp, cfg::kVehComUp), "Truck rigid body (Truck_Physics, ChassisOffset 15)");
        check(near(V.driveSpeed, cfg::kTruckDriveSpeed) && near(V.driveAccel, cfg::kTruckDriveAccel) && near(V.driveJumpFwd, cfg::kDriveJumpFwd) &&
              near(V.driveJumpUp, cfg::kDriveJumpUp) && V.wheels.size() == 4, "Truck driving (Truck_Physics + 4 wheels)");
        check(t.weaponPrimary.valid && t.weaponPrimary.bone == "R_Arm03_Elbow_XB", "Truck WeaponSocket_Primary on R_Arm03_Elbow_XB");
    }

    // Every roster chassis: load or report why not; MP characters must load.
    const char* ids[] = {"Car", "Car2", "Car3", "Car4", "Car5", "Car6", "Car7", "Car8", "Car9", "Car10",
                         "Jet", "Jet2", "Jet3", "Jet4", "Jet5", "Jet6", "Jet7", "Jet8",
                         "Tank", "Tank2", "Tank3", "Tank4", "Tank5",
                         "Truck", "Truck2", "Truck3", "Truck4", "Truck5", "Truck6", "Truck7", "Minion1", "Minion2", "Minion3"};
    int mpLoaded = 0, mpTotal = 0;
    const char* formN[] = {"car", "truck", "tank", "jet"};
    for (const char* id : ids) {
        ChassisDef d;
        bool l = loadChassisDef(vsRoot, id, d);
        if (d.mpCharacter) { ++mpTotal; mpLoaded += l; }
        LOG_INFO("CHASSIS %-7s %-15s %s %-9s %-5s r %.2f hh %.2f  speed %.1f  hover %.1f/%.1f drive %.1f  susp %.2f/%.0f  arm %s  %s%s",
                 id, d.iconic.c_str(), d.faction == 0 ? "AUT" : d.faction == 1 ? "DEC" : "NEU", d.defaultSpecialty.c_str(),
                 formN[(int)d.vehicle.form], d.robot.radius, d.robot.halfHeight, d.robot.groundSpeed, d.vehicle.hoverSpeed,
                 d.vehicle.hoverAccel, d.vehicle.driveSpeed, d.vehicle.suspRest, d.vehicle.suspStiffness,
                 d.armGltf.empty() ? "-" : "yes", d.mpCharacter ? "MP" : "not-MP", l ? "" : ("  LOAD FAILED: " + d.loadError).c_str());
    }
    check(mpLoaded == mpTotal && mpTotal == 27, "all 27 multiplayer chassis load (" + std::to_string(mpLoaded) + "/" + std::to_string(mpTotal) + ")");
    ChassisDef bad;
    check(!loadChassisDef(vsRoot, "NoSuchChassis", bad) && !bad.loadError.empty(), "an unknown chassis fails loudly (no fallback)");

    // Per-chassis transform visibility notifies (TnAnimNotify_ToggleHidden) [CONF authored].
    {
        ChassisDef c4, tr;
        const bool ok = loadChassisDef(vsRoot, "Car4", c4) && loadChassisDef(vsRoot, "Truck", tr);
        LOG_INFO("CHASSIS transform notifies Car4 robot hide %.3f vehicle show %.3f | robot show %.3f vehicle hide %.3f; Truck %.3f %.3f | %.3f %.3f",
                 c4.toVehRobotHide, c4.toVehVehicleShow, c4.toRobotRobotShow, c4.toRobotVehicleHide, tr.toVehRobotHide, tr.toVehVehicleShow,
                 tr.toRobotRobotShow, tr.toRobotVehicleHide);
        check(ok && near(c4.toVehVehicleShow, 0.7049f, 1e-3f) && near(c4.toVehRobotHide, 0.8487f, 1e-3f) && near(c4.toRobotRobotShow, 0.3940f, 1e-3f) &&
              near(c4.toRobotVehicleHide, 0.6663f, 1e-3f) && near(tr.toVehRobotHide, 0.8796f, 1e-3f) && near(tr.toVehVehicleShow, 0.3958f, 1e-3f),
              "transform ToggleHidden times per chassis (Barricade 0.705 vehicle show, not Optimus 0.396)");
    }
    // Specialty table (TnSpecialty CDOs + TR_Health_p.Health_<Class>).
    const SpecialtyDef* sc = specialtyDef("Scout");
    const SpecialtyDef* so = specialtyDef("Soldier");
    check(sc && so && sc->segments.size() == 4 && near(sc->speedMultiplier, 1.0f) && so->segments.size() == 6 && near(so->speedMultiplier, 0.9f),
          "specialty table: Scout 4x50 x1.0, Soldier 6x55 x0.9");
    LOG_INFO("CHASSIS SUMMARY: %d/%d checks passed", checks - fails, checks);
    return fails;
}

} // namespace game
