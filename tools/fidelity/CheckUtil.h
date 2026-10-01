// WFC fidelity harness — helpers shared by the check suites.
#pragma once
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include "Checks.h"
#include "Rig.h"
#include "assets/Json.h"

namespace fid {

inline const char* kGameplay = "Gameplay";
inline const char* kSystems = "Systems";
inline const char* kRendering = "Rendering";
inline const char* kAssetTools = "AssetTools/RE";

inline float hspeed(const Frame& f) { return std::sqrt(f.vel.x * f.vel.x + f.vel.z * f.vel.z); }

inline void save(const Rig& r, const std::string& name) {
    if (!options().traceDir.empty()) r.writeCsv(options().traceDir + "/" + name + ".csv");
}

inline bool readFileText(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

inline bool loadJsonFile(const std::string& path, assets::Json& out) {
    std::string s;
    return readFileText(path, s) && assets::Json::parse(s, out);
}

// JSON chunk of a .glb (independent of the engine's loaders, so it reads what the file declares).
inline bool loadGlbJson(const std::string& path, assets::Json& out) {
    std::string s;
    if (!readFileText(path, s) || s.size() < 20) return false;
    auto u32 = [&](size_t o) {
        return (unsigned)(unsigned char)s[o] | (unsigned)(unsigned char)s[o + 1] << 8 |
               (unsigned)(unsigned char)s[o + 2] << 16 | (unsigned)(unsigned char)s[o + 3] << 24;
    };
    size_t len = u32(12);
    if (20 + len > s.size()) return false;
    return assets::Json::parse(s.data() + 20, len, out);
}

// World point -> normalized device coordinates through the production camera.
inline core::Vec3 toNdc(const render::Camera& cam, const core::Vec3& p) {
    core::Mat4 m = cam.proj() * cam.view();
    float x = m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12];
    float y = m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13];
    float z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    float w = m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15];
    return {x / w, y / w, z / w};
}

inline float angleDeg(core::Vec3 a, core::Vec3 b) {
    float d = core::clampf(core::dot(core::normalize(a), core::normalize(b)), -1.0f, 1.0f);
    return core::degrees(std::acos(d));
}

// Time (relative to t0) of the first frame satisfying `pred`; -1 if never.
template <class P> double firstAfter(const Rig& r, double t0, P pred) {
    for (const Frame& f : r.trace()) if (f.t > t0 + 1e-9 && pred(f)) return f.t - t0;
    return -1;
}

// Cooked UE3 config root (read-only shared original data).
inline std::string cookedConfigDir() {
    return Models::assetRoot() + "/../config/Coalesced_ini/TransGame/Config/Xenon/Cooked";
}

} // namespace fid
