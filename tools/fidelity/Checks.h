// WFC fidelity harness — check suites.
#pragma once
#include <string>
#include "Report.h"

namespace fid {

struct Options {
    std::string traceDir;    // when set, every scenario writes <traceDir>/<name>.csv
    bool map = false;        // load the real Streets collision (slow; ~1.85 M tris)
};
const Options& options();
void setOptions(const Options& o);

void checkConstants(Report& r);     // Config.h vs recovered UU values
void checkWeaponData(Report& r);    // Weapon defaults vs weapon.json
void checkWeaponBehaviour(Report& r);
void checkOrientation(Report& r);   // camera / facing / input handedness
void checkMovement(Report& r);      // accel, top speed, jump, vehicle hover/cruise/boost
void checkCollision(Report& r);     // synthetic probes: wall gap, slide, step height
void checkTransform(Report& r);     // paired clip timing, lockout, weapon holster
void checkAnimation(Report& r);     // locomotion clip selection, reload clip, skeleton
void checkMuzzle(Report& r);        // weapon socket + barrel tip vs facing/aim
void checkDeterminism(Report& r);
void checkPerformance(Report& r);
void checkMap(Report& r);

// Milestone-01 playtest suites (PlaytestChecks.cpp).
void checkTransformMomentum(Report& r);
void checkFastMovement(Report& r);
void checkFineAim(Report& r);
void checkBoost(Report& r);
void checkVehicleMaterials(Report& r);
void checkMapContent(Report& r);
void checkInputEdges(Report& r);

// Compare measured metrics against values captured from the ORIGINAL game.
void compareReference(Report& r, const std::string& path);

} // namespace fid
