// wfc_fidelity — deterministic fidelity regression + measurement harness for the WFC rebuild.
//
//   wfc_fidelity [--verbose] [--json out.json] [--trace-dir dir] [--reference file.json]
//                [--no-assets] [--map] [--only group[,group...]]
//
// Exit code: 0 = no regressions (KNOWN deviations allowed), 1 = at least one FAIL, 2 = usage.
#include "Checks.h"
#include "Rig.h"
#include "assets/SkinnedModel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    fid::Options opt;
    std::string json, reference = "tools/fidelity/reference/original_measurements.json", only;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(2); }
            return argv[++i];
        };
        if (a == "--verbose" || a == "-v") verbose = true;
        else if (a == "--json") json = next();
        else if (a == "--trace-dir") opt.traceDir = next();
        else if (a == "--reference") reference = next();
        else if (a == "--no-assets") fid::Models::disable();
        else if (a == "--map") opt.map = true;
        else if (a == "--only") only = "," + next() + ",";
        else if (a == "--dump-models") {
            const fid::Models* m = fid::Models::get();
            if (!m) { std::fprintf(stderr, "models unavailable\n"); return 1; }
            for (const assets::SkinnedModel* mdl : {&m->robot, &m->vehicle}) {
                std::printf("== %s: %zu clips, %zu nodes, %zu skin joints\n", mdl == &m->robot ? "robot" : "vehicle",
                            mdl->clips.size(), mdl->nodes.size(), mdl->skinJoints.size());
                for (const assets::AnimClip& c : mdl->clips)
                    std::printf("  clip %-48s cat=%-30s %.4f s%s\n", c.name.c_str(), c.category.c_str(), c.duration,
                                c.additive ? " additive" : "");
                for (size_t n = 0; n < mdl->nodeNames.size(); ++n)
                    std::printf("  node %3zu %-36s parent=%d\n", n, mdl->nodeNames[n].c_str(), mdl->nodes[n].parent);
            }
            return 0;
        }
        else {
            std::fprintf(stderr, "usage: wfc_fidelity [--verbose] [--json out] [--trace-dir dir] "
                                 "[--reference file] [--no-assets] [--map] [--only g1,g2]\n");
            return 2;
        }
    }
    fid::setOptions(opt);

    struct Suite { const char* name; void (*fn)(fid::Report&); };
    const Suite suites[] = {
        {"constants", fid::checkConstants},     {"weapon_data", fid::checkWeaponData},
        {"weapon", fid::checkWeaponBehaviour},  {"orientation", fid::checkOrientation},
        {"movement", fid::checkMovement},       {"collision", fid::checkCollision},
        {"transform", fid::checkTransform},     {"animation", fid::checkAnimation},
        {"muzzle", fid::checkMuzzle},           {"determinism", fid::checkDeterminism},
        {"performance", fid::checkPerformance}, {"map", fid::checkMap},
        {"transform_momentum", fid::checkTransformMomentum}, {"fast_movement", fid::checkFastMovement},
        {"fine_aim", fid::checkFineAim},        {"boost", fid::checkBoost},
        {"vehicle_materials", fid::checkVehicleMaterials}, {"map_content", fid::checkMapContent},
        {"input_edges", fid::checkInputEdges},
        {"transform_timeline", fid::checkTransformTimeline}, {"vehicle_feel", fid::checkVehicleFeel},
        {"fine_aim_presentation", fid::checkFineAimPresentation}, {"trace_cost", fid::checkTraceCost},
        {"transform_analyzer", fid::checkTransformAnalyzer}, {"vehicle_profiles", fid::checkVehicleProfiles},
        {"fine_aim_probe", fid::checkFineAimProbe}, {"native_vehicle", fid::checkNativeVehicle},
        {"native_robot", fid::checkNativeRobot},
    };
    fid::Report report;
    std::printf("wfc_fidelity: assets=%s (%s)\n", fid::Models::get() ? "loaded" : "unavailable",
                fid::Models::assetRoot().c_str());
    for (const Suite& s : suites)
        if (only.empty() || only.find("," + std::string(s.name) + ",") != std::string::npos) s.fn(report);
    if (only.empty() || only.find(",original_vs_rebuild,") != std::string::npos)
        fid::compareReference(report, reference);

    report.printText(stdout, verbose);
    if (!json.empty() && !report.writeJson(json)) std::fprintf(stderr, "cannot write %s\n", json.c_str());
    return report.count(fid::Status::Fail) > 0 ? 1 : 0;
}
