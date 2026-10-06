// Authored map presentation that is not static world geometry (AssetTools a23c675):
//
//  * Particle components (tools/render/build_map_fx.py -> map_fx_runtime.json): 8 Steam_Sm_FX level emitters
//    and the 37 pickup-factory effects, simulated from their decoded compiled module streams (pstream, every
//    LOD assignment-complete). Components start in their authored bAutoActivate state; Gameplay toggles
//    factory effects through setMapEffectActive(). Module evaluation follows UE3 ParticleModule semantics
//    [HIGH: engine behaviour, not re-derived from the WFC binary]; per-emitter MaxPeakCount (WFC field,
//    CDO 1) caps the active particles [HIGH: every LOD's PeakActiveParticles <= MaxPeakCount].
//    PARTIAL / not reproduced: HmParticleModuleGravity (WFC module, GravityMultiplier native), ColorByParameter
//    (no instance parameter), DynamicParameter slot order (values decoded, order unproven: natural order used),
//    SERM_Octagon / BestFit sprite cut-out polygons (drawn as full quads), LocationPrimitiveSphere sampling.
//  * TnDominationPoint energon totems (render_index.json): NEU_EnergonTotem_SKEL, DeactivatedLoopAnim
//    EnergonTotem_StandBy looping.
//  * TnStaticDestructibleActor_14465 (WallPanelSign): state 0 Base mesh (texture lightmap of its authored
//    StaticMeshComponent), states 1/2 the Chunk02 stump; Gameplay drives the state (setDestructibleState).
//    Physical chunk, debris and destruction particle systems are simulation-driven: not presented here.
#include "render/gl/WfcPipeline.h"
#include "assets/Gltf.h"
#include "assets/Json.h"
#include "assets/SkinnedModel.h"
#include "core/Log.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace render {
namespace wfc {


namespace {
std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::string();
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

// A module distribution decoded from the compiled stream; a module whose stream did not carry it evaluates to 0
// (logged once per module / property) instead of aborting.
void evalDist(const Pipeline::FxModule& m, const char* name, float t, uint32_t& rng, float* out) {
    auto it = m.dists.find(name);
    if (it != m.dists.end()) { it->second.eval(t, rng, out); return; }
    // pstream labels LocationPrimitiveSphere's two float distributions DynamicParams[0] / [1] (as SubUVDirect's):
    // serialization order, base class first - [0] = VelocityScale (ParticleModuleLocationPrimitiveBase), [1] =
    // StartRadius (the sphere's own) [HIGH: matches every MP value pair, e.g. AR sparks 30 / U(2,5), smoke 0.5 / 5]
    if (m.name == "PMI_LocationPrimitiveSphere") {
        const char* alias = std::strcmp(name, "VelocityScale") == 0 ? "DynamicParams[0].ParamValue"
                          : std::strcmp(name, "StartRadius") == 0 ? "DynamicParams[1].ParamValue" : nullptr;
        if (alias) {
            auto al = m.dists.find(alias);
            if (al != m.dists.end()) { al->second.eval(t, rng, out); return; }
        }
    }
    out[0] = out[1] = out[2] = 0.0f;
    static std::set<std::string> logged;
    std::string key = m.name + "." + name;
    if (logged.insert(key).second) LOG_WARN("map fx: %s has no decoded %s (0 used)", m.name.c_str(), name);
}

int kindOf(const std::string& k) {
    if (k.find("uniform curve") != std::string::npos) return 3;
    if (k.find("constant curve") != std::string::npos) return 2;
    if (k.find("uniform") != std::string::npos) return 1;
    return 0;
}

// UE units (x, y, z) -> glTF metres (x, z, y) / 100
core::Vec3 ueToGltf(const float p[3]) { return {p[0] * 0.01f, p[2] * 0.01f, p[1] * 0.01f}; }

// UE row-vector transform (rows = local axes, row 3 = translation, UE units) -> glTF column matrix for a
// content-glTF mesh. Content glTFs are metres in the same Y-up local frame as world.glb's meshes (identical vertex
// data; local y = UE local Z, local z = UE local Y): M = P * A_ue * P with P the y<->z swap, so a proper UE rotation
// stays a proper rotation (no winding change).
} // namespace
core::Mat4 ueRowsToGltf(const float R[3][3], const float T[3]) {
    core::Mat4 M = core::Mat4::identity();
    auto sw = [](int i) { return i == 0 ? 0 : (i == 1 ? 2 : 1); };
    for (int c = 0; c < 3; ++c)                  // column c = image of local glTF axis c = swapped UE local axis sw(c)
        for (int r = 0; r < 3; ++r) M.m[c * 4 + r] = R[sw(c)][sw(r)];
    M.m[12] = T[0] * 0.01f; M.m[13] = T[2] * 0.01f; M.m[14] = T[1] * 0.01f;
    return M;
}

void rotRows(float pitch, float yaw, float roll, float out[3][3]) {   // UE3 FRotationMatrix, UE units
    const float k = 3.14159265358979f / 32768.0f;
    float p = pitch * k, y = yaw * k, r = roll * k;
    float SP = std::sin(p), CP = std::cos(p), SY = std::sin(y), CY = std::cos(y), SR = std::sin(r), CR = std::cos(r);
    const float rows[3][3] = {{CP * CY, CP * SY, SP},
                              {SR * SP * CY - CR * SY, SR * SP * SY + CR * CY, -SR * CP},
                              {-(CR * SP * CY + SR * SY), CY * SR - CR * SP * SY, CR * CP}};
    std::memcpy(out, rows, sizeof(rows));
}
namespace {
} // namespace

// ---- distributions (FRawDistribution layouts, AssetTools pstream DIST_LAYOUT) ----
void Pipeline::FxDist::eval(float t, uint32_t& rng, float out[3]) const {
    auto rnd = [&]() { rng = rng * 1664525u + 1013904223u; return (float)(rng >> 8) / 16777216.0f; };
    const int c = comps;
    if (v.empty()) { for (int i = 0; i < c; ++i) out[i] = 0; return; }
    if (kind == 0) { for (int i = 0; i < c; ++i) out[i] = v[(size_t)std::min<int>(i, (int)v.size() - 1)]; return; }
    if (kind == 1) {
        for (int i = 0; i < c; ++i) {
            float a = v[(size_t)i], b = (size_t)(c + i) < v.size() ? v[(size_t)(c + i)] : a;
            out[i] = a + (b - a) * rnd();
        }
        return;
    }
    // curves: [min, max, start time, time scale] then samples (constant: c floats, uniform: 2c floats)
    int stride = kind == 2 ? c : 2 * c;
    int n = ((int)v.size() - 4) / stride;
    if (n <= 0) { for (int i = 0; i < c; ++i) out[i] = 0; return; }
    float f = (t - v[2]) * v[3];
    f = std::min(std::max(f, 0.0f), (float)(n - 1));
    int i0 = (int)std::floor(f), i1 = std::min(i0 + 1, n - 1);
    float a = f - (float)i0;
    float r = kind == 3 ? rnd() : 0.0f;
    for (int k = 0; k < c; ++k) {
        float s0 = v[(size_t)(4 + i0 * stride + k)], s1 = v[(size_t)(4 + i1 * stride + k)];
        float val = s0 + (s1 - s0) * a;
        if (kind == 3) {
            float e0 = v[(size_t)(4 + i0 * stride + c + k)], e1 = v[(size_t)(4 + i1 * stride + c + k)];
            float val2 = e0 + (e1 - e0) * a;
            val = val + (val2 - val) * r;
        }
        out[k] = val;
    }
}

namespace {
Pipeline::FxDist parseDist(const assets::Json& d, int comps) {
    Pipeline::FxDist r;
    r.kind = kindOf(d["kind"].asString());
    r.comps = comps;
    const assets::Json& v = d["values"];
    for (size_t i = 0; i < v.size(); ++i) r.v.push_back(v[i].asFloat());
    return r;
}
} // namespace

bool Pipeline::loadMapFx(const std::string& path) {
    std::string txt = readFile(path);
    assets::Json J;
    if (txt.empty() || !assets::Json::parse(txt, J)) return false;
    auto lower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; };
    const assets::Json& S = J["systems"];
    for (const auto& kv : S.obj) {
        FxSystem sys;
        sys.name = kv.first;
        const assets::Json& ld = kv.second["lod_distances"];
        for (size_t i = 0; i < ld.size(); ++i) sys.lodDistances.push_back(ld[i].asFloat());
        sys.directSet = kv.second["lod_method"].asString() == "PARTICLESYSTEMLODMETHOD_DirectSet";
        const assets::Json& es = kv.second["emitters"];
        for (size_t e = 0; e < es.size(); ++e) {
            FxEmitter em;
            em.name = es[e]["name"].asString();
            {
                const std::string la = es[e]["lock_axis"].asString();
                const char* modes[] = {"EPAL_NONE", "EPAL_X", "EPAL_Y", "EPAL_Z", "EPAL_NEGATIVE_X", "EPAL_NEGATIVE_Y",
                                       "EPAL_NEGATIVE_Z", "EPAL_ROTATE_X", "EPAL_ROTATE_Y", "EPAL_ROTATE_Z", "EPAL_ROTATE_X_U",
                                       "EPAL_ROTATE_Y_U", "EPAL_ROTATE_Z_U"};
                for (int k = 0; k < 13; ++k) if (la == modes[k]) em.lockAxis = k;
            }
            em.maxPeak = std::max(1, es[e]["max_peak_count"].asInt(1));
            {
                const std::string rm = es[e]["render_mode"].asString();
                em.renderMode = rm == "SERM_Octagon" ? 1 : rm == "SERM_BestFit" ? 2 : 0;
                const assets::Json& pl = es[e]["best_fit_polygons"];
                for (size_t k = 0; k < pl.size(); ++k) {
                    FxEmitter::Polygon pg;
                    pg.time = pl[k]["time"].asFloat(0.0f);
                    pg.count = pl[k]["count"].asInt(0);
                    for (size_t j = 0; j < pl[k]["vertices"].size(); ++j)
                        pg.v.push_back({pl[k]["vertices"][j][(size_t)0].asFloat(0.0f), pl[k]["vertices"][j][(size_t)1].asFloat(0.0f)});
                    em.polygons.push_back(std::move(pg));
                }
            }
            // flag-invariant look only (build_map_fx.py flag_analysis); WFC_FX_FLAGREADING=A|B draws the rest
            // under one of the two unproven readings (experimental)
            em.renderable = es[e]["renderable"].asBool(true) || std::getenv("WFC_FX_FLAGREADING") != nullptr;
            const assets::Json& lods = es[e]["lods"];
            for (size_t l = 0; l < lods.size(); ++l) {
                const assets::Json& L = lods[l];
                FxLod lod;
                lod.material = L["material"].asString();
                const assets::Json& rq = L["required"];
                lod.duration = std::max(rq["emitter_duration"].asFloat(1.0f), 1e-3f);
                lod.loops = rq["emitter_loops"].asInt(0);
                lod.spawnRate = parseDist(rq["spawn_rate"], 1);
                const assets::Json& bl = rq["burst_list"];
                for (size_t b = 0; b < bl.size(); ++b)
                    lod.bursts.push_back({bl[b]["Count"].asInt(0), bl[b]["CountLow"].asInt(0), bl[b]["Time"].asFloat(0)});
                lod.localSpace = rq["use_local_space"].asBool(false);
                lod.rectangle = rq["screen_alignment"].asString() == "PSA_Rectangle";
                lod.velocityAligned = rq["screen_alignment"].asString() == "PSA_Velocity";
                {
                    const std::string td = L["type_data"].asString();
                    lod.typeData = td == "mesh" ? 1 : td == "trail2" ? 2 : td == "beam2" ? 3 : 0;
                    const assets::Json& bt = L["beam_trail"];
                    lod.textureTile = std::max(1.0f, bt["TextureTile"].asFloat(1.0f));
                    {
                        const std::string dir = bt["billboard"]["direction"].asString(), al = bt["billboard"]["alignment"].asString();
                        const char* axes[6] = {"LocalX", "LocalY", "LocalZ", "WorldX", "WorldY", "WorldZ"};
                        for (int k = 0; k < 6; ++k) if (dir.find(axes[k]) != std::string::npos) lod.billboardAxis = k;
                        if (al.find("Positive") != std::string::npos) { lod.sideScale[0] = 0.0f; lod.sideScale[1] = 2.0f; }
                        else if (al.find("Negative") != std::string::npos) { lod.sideScale[0] = 2.0f; lod.sideScale[1] = 0.0f; }
                    }
                    lod.textureTileDistance = bt["TextureTileDistance"].asFloat(0.0f);
                    lod.tilePerParticle = bt["bTilePerParticle"].asBool(false);
                    if (lod.typeData == 3) {
                        lod.maxBeams = std::max(0, bt["MaxBeamCount"].asInt(1));
                        const std::string tm = bt["TaperMethod"].asString();
                        lod.taperMethod = tm == "PEBTM_Full" ? 1 : tm == "PEBTM_Partial" ? 2 : 0;
                        lod.interpPoints = std::max(0, bt["InterpolationPoints"].asInt(0));
                        if (bt["taper_factor"].isObject()) lod.taperFactor = parseDist(bt["taper_factor"], 1);
                        if (bt["taper_scale"].isObject()) lod.taperScale = parseDist(bt["taper_scale"], 1);
                        if (lod.taperFactor.v.empty()) { lod.taperFactor.kind = 0; lod.taperFactor.v = {1.0f}; }
                        if (lod.taperScale.v.empty()) { lod.taperScale.kind = 0; lod.taperScale.v = {1.0f}; }
                        const assets::Json& bm = bt["modules"];
                        if (bm.isObject() && bm["noise"].isObject()) {
                            const assets::Json& nz = bm["noise"];
                            auto& N = lod.noise;
                            N.freq = nz["frequency"].asInt(0);
                            N.freqLow = nz["frequency_low"].asInt(0);
                            // native: Spawn draws points when Frequency != 0; the render fill applies them only with
                            // bLowFreq_Enabled (stock UE3's noise switch) (RE 9h / 9i, CONFIRMED)
                            N.on = N.freq != 0 && nz["low_freq"].asBool(false);
                            N.smooth = nz["smooth"].asBool(false);
                            N.lockTime = nz["lock_time"].asFloat(0.0f);
                            N.applyScale = nz["apply_scale"].asBool(false);
                            N.oscillate = nz["oscillate"].asBool(false);
                            N.targetNoise = nz["target_noise"].asBool(false);
                            N.nrEmitterTime = nz["nr_scale_emitter_time"].asBool(false);
                            N.tessellation = std::max(1, nz["tessellation"].asInt(1));
                            N.lockRadius = nz["lock_radius"].asFloat(1.0f);
                            N.frequencyDistance = nz["frequency_distance"].asFloat(0.0f);
                            N.range = parseDist(nz["range"], 3);
                            N.rangeScale = parseDist(nz["range_scale"], 1);
                            N.speed = parseDist(nz["speed"], 3);
                            N.tangent = parseDist(nz["tangent_strength"], 1);
                            N.scale = parseDist(nz["scale"], 1);
                        }
                        auto strength = [&](const char* k, Pipeline::FxDist& d) {
                            if (bm.isObject() && bm[k].isObject() && bm[k]["strength"].isObject()) d = parseDist(bm[k]["strength"], 1);
                            if (d.v.empty()) { d.kind = 0; d.v = {25.0f}; }
                        };
                        strength("source", lod.sourceStrength);
                        strength("target", lod.targetStrength);
                        auto end = [&](const char* k, FxLod::BeamEnd& E) {
                            E.position.kind = 0; E.position.comps = 3; E.position.v = {0.0f, 0.0f, 0.0f};
                            E.tangent.kind = 0; E.tangent.comps = 3; E.tangent.v = {1.0f, 0.0f, 0.0f};
                            if (!bm.isObject() || !bm[k].isObject()) return;     // no module: the component (Default (0,0,0))
                            const assets::Json& j = bm[k];
                            const std::string m = j["method"].asString(), tm = j["tangent_method"].asString();
                            E.method = m == "PEB2STM_UserSet" ? 1 : m == "PEB2STM_Emitter" ? 2 : m == "PEB2STM_Particle" ? 3 :
                                       m == "PEB2STM_Actor" ? 4 : 0;
                            E.tangentMethod = tm == "PEB2STTM_UserSet" ? 1 : tm == "PEB2STTM_Distribution" ? 2 :
                                              tm == "PEB2STTM_Emitter" ? 3 : 0;
                            E.named = j["name"].isString() && !j["name"].asString().empty() && j["name"].asString() != "None";
                            E.absolute = j["absolute"].asBool(false);
                            E.lock = j["lock"].asBool(false);
                            E.lockTangent = j["lock_tangent"].asBool(false);
                            if (j["position"].isObject()) E.position = parseDist(j["position"], 3);
                            if (j["tangent"].isObject()) E.tangent = parseDist(j["tangent"], 3);
                        };
                        end("source", lod.beamSrc);
                        end("target", lod.beamTgt);
                        lod.beamDistance = bt["BeamMethod"].asString() == "PEB2M_Distance";
                        if (bt["distance"].isObject()) lod.distance = parseDist(bt["distance"], 1);
                        if (bm.isObject() && bm["sine_waves"].isArray())
                            for (size_t w = 0; w < bm["sine_waves"].size(); ++w) {
                                const assets::Json& sw = bm["sine_waves"][w];
                                FxLod::BeamSine s;
                                s.amp = sw["amplitude"].asFloat(0.0f); s.period = sw["period"].asFloat(1.0f);
                                s.speed = sw["speed"].asFloat(0.0f); s.phase = sw["phase"].asFloat(0.0f);
                                for (int c = 0; c < 3; ++c) s.dir[c] = sw["direction"][(size_t)c].asFloat(0.0f);
                                lod.sines.push_back(s);
                            }
                    }
                    if (lod.typeData == 2) {
                        // spawn cap = MaxTrailCount (CDO 1) x MaxParticleInTrailCount (Trail2 Spawn 0x83048E40, RE s14),
                        // not the emitter's MaxPeakCount
                        lod.trailCap = std::max(1, bt["MaxTrailCount"].asInt(1)) * std::max(1, bt["MaxParticleInTrailCount"].asInt(100));
                        lod.tessFactor = std::max(1, bt["TessellationFactor"].asInt(1));   // 0 -> 1 (native)
                        lod.tessStrength = bt["TessellationStrength"].asFloat(1.0f);
                    }
                    if (L["size_param"].isObject()) {
                        lod.sizeParam = L["size_param"]["name"].asString();
                        for (int c = 0; c < 3; ++c) lod.sizeParamConst[c] = L["size_param"]["constant"][(size_t)c].asFloat(1.0f);
                    }
                    lod.subH = std::max(1, rq["subimages"][(size_t)0].asInt(1));
                    lod.subV = std::max(1, rq["subimages"][(size_t)1].asInt(1));
                    const assets::Json& dc = L["default_color"];         // FColor (R, G, B, A) -> FLinearColor
                    if (dc.isArray() && dc.size() == 4) {
                        lod.hasDefaultColor = true;
                        for (int c = 0; c < 4; ++c) {
                            float v = dc[(size_t)c].asFloat(255) / 255.0f;
                            lod.defaultColor[c] = c < 3 ? std::pow(v, 2.2f) : v;
                        }
                    }
                    const std::string sm = L["subuv_method"].asString();   // EParticleSubUVInterpMethod
                    lod.subMethod = sm == "PSUVIM_Linear" ? 1 : sm == "PSUVIM_Linear_Blend" ? 2 : sm == "PSUVIM_Random" ? 3
                                  : sm == "PSUVIM_Random_Blend" ? 4 : 0;
                    lod.randomImageTime = L["random_image_time"].asFloat(0.0f);
                }
                if (L.has("mesh") && L["mesh"].isObject()) {
                    lod.meshGltf = L["mesh"]["gltf"].asString();
                    lod.overrideMaterial = L["mesh"]["override_material"].asBool(false);
                }
                const assets::Json& ms = L["modules"];
                for (size_t m = 0; m < ms.size(); ++m) {
                    FxModule mod;
                    mod.name = ms[m]["module"].asString();
                    mod.flagA = ms[m]["raw_flags"][0].asInt(1);
                    mod.flagB = ms[m]["raw_flags"][1].asInt(1);
                    const assets::Json& le = ms[m]["location_emitter"];
                    if (le.isObject()) {             // M63 (decoded from the LOD stream by build_map_fx)
                        mod.sourceEmitter = le["emitter"].asString();
                        const std::string sel = le["selection"].asString();
                        mod.selection = sel == "ELESM_Sequential" ? 1 : sel == "ELESM_Particle0" ? 2 : 0;
                        mod.inheritVelocity = le["inherit_velocity"].asBool(false);
                        mod.inheritVelocityScale = le["inherit_velocity_scale"].asFloat(1.0f);
                        mod.inheritRotation = le["inherit_rotation"].asBool(false);
                        mod.inheritRotationScale = le["inherit_rotation_scale"].asFloat(1.0f);
                    }
                    const assets::Json& d = ms[m]["dists"];
                    for (const auto& dv : d.obj) {
                        bool vec = dv.second["kind"].asString().rfind("vector", 0) == 0;
                        mod.dists[dv.first] = parseDist(dv.second, vec ? 3 : 1);
                    }
                    if (lod.typeData == 2 && mod.name == "PMI_LocationEmitter" && !mod.sourceEmitter.empty())
                        lod.particleTrail = true;
                    lod.modules.push_back(std::move(mod));
                }
                em.lods.push_back(std::move(lod));
            }
            sys.emitters.push_back(std::move(em));
        }
        fxSystems_[sys.name] = std::move(sys);
    }
    const assets::Json& I = J["instances"];
    static const bool allActive = std::getenv("WFC_MAPFX_ALLACTIVE") != nullptr;
    for (size_t i = 0; i < I.size(); ++i) {
        FxInstance in;
        in.component = lower(I[i]["component"].asString());
        in.owner = lower(I[i]["owner"].asString());
        in.system = I[i]["template"].asString();
        in.role = I[i]["role"].asString();
        {   // UE3 FLinearColor(const FColor&): RGB through the 1/255 ^ 2.2 table, alpha linear
            const assets::Json& cp = I[i]["color_params"];
            for (const auto& kv : cp.obj) {
                std::array<float, 4> c{};
                for (int ch = 0; ch < 4; ++ch) {
                    float v = kv.second[(size_t)ch].asFloat() / 255.0f;
                    c[(size_t)ch] = ch < 3 ? std::pow(v, 2.2f) : v;
                }
                in.colorParams[kv.first] = c;
            }
        }
        in.ownerClass = I[i]["owner_class"].asString();
        in.attached = I[i]["attached"].asBool(true);
        in.requiredRule = I[i]["required_game_rule"].asString();
        in.active = allActive || I[i]["auto_activate"].asBool(true);
        const assets::Json& M = I[i]["ue_matrix"];
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) in.R[r][c] = M[(size_t)r][(size_t)c].asFloat();
        for (int c = 0; c < 3; ++c) in.T[c] = M[3][(size_t)c].asFloat();
        std::copy(&in.R[0][0], &in.R[0][0] + 9, &in.R0[0][0]);
        std::copy(in.T, in.T + 3, in.T0);
        in.ownerShort = in.owner.substr(in.owner.rfind('.') == std::string::npos ? 0 : in.owner.rfind('.') + 1);
        in.rng = 0x9E3779B9u * (uint32_t)(i + 1);
        auto it = fxSystems_.find(in.system);
        if (it == fxSystems_.end()) continue;
        in.emitters.resize(it->second.emitters.size());
        fxInstances_.push_back(std::move(in));
    }
    size_t act = std::count_if(fxInstances_.begin(), fxInstances_.end(), [](const FxInstance& x) { return x.active; });
    LOG_INFO("wfc: map FX: %zu components (%zu active), %zu systems", fxInstances_.size(), act, fxSystems_.size());
    if (const char* dump = std::getenv("WFC_AUDIT_DUMP")) {     // tools/render/audit_map.py: FX presentation state
        if (FILE* f = std::fopen((std::string(dump) + ".fx.jsonl").c_str(), "w")) {
            for (const FxInstance& in : fxInstances_) {
                const FxSystem& sys = fxSystems_[in.system];
                int drawable = 0, total = (int)sys.emitters.size();
                for (const FxEmitter& em : sys.emitters) drawable += em.renderable ? 1 : 0;
                std::fprintf(f, "{\"component\":\"%s\",\"role\":\"%s\",\"attached\":%d,\"active\":%d,"
                                "\"rule\":\"%s\",\"rule_active\":%d,\"emitters\":%d,\"drawable_emitters\":%d}\n",
                             in.component.c_str(), in.role.c_str(), in.attached ? 1 : 0, in.active ? 1 : 0,
                             in.requiredRule.c_str(), in.requiredRule.empty() || ruleActive(in.requiredRule) ? 1 : 0,
                             total, drawable);
            }
            std::fclose(f);
        }
    }
    return true;
}

void Pipeline::setMapEffectActive(const std::string& what, bool active) { setMapEffectState(what, active, false); }

// TnPickupFactory.SetPickupHidden: CustomPickupEffect.SetHidden(true) + DeactivateSystem(); highlight
// DeactivateSystem() (its live particles finish). SetPickupVisible: the reverse (decompiled script, Systems 8dcb861).
void Pipeline::setMapEffectState(const std::string& key, bool active, bool hidden) {
    std::string w = key, role;
    std::transform(w.begin(), w.end(), w.begin(), ::tolower);
    size_t bar = w.find('|');
    if (bar != std::string::npos) { role = w.substr(bar + 1); w = w.substr(0, bar); }
    std::string shortName = w.substr(w.rfind('.') == std::string::npos ? 0 : w.rfind('.') + 1);   // full path or name
    if (role.empty() || role == "custom") {          // SetPickupHidden / SetPickupVisible also toggle the pickup mesh
        if (hidden) pickupMeshHidden_.insert(shortName); else pickupMeshHidden_.erase(shortName);
    }
    for (FxInstance& in : fxInstances_) {
        if (in.owner != w && in.owner != shortName && in.component != w) continue;
        if (!role.empty() && in.role != role) continue;
        if (active && !in.active)
            for (FxEmitterRT& e : in.emitters) { e.time = 0; e.spawnFrac = 0; e.loop = 0; e.burstFired.clear(); e.done = false; }
        in.active = active;
        in.hidden = hidden;
        if (hidden) for (FxEmitterRT& e : in.emitters) e.parts.clear();   // SetHidden: nothing drawn
    }
}

void Pipeline::setActiveGameRules(const std::vector<std::string>& rules) {
    activeRules_.clear();
    for (std::string r : rules) {
        if (r.find('.') == std::string::npos) r = "TransGame." + r;
        activeRules_.insert(r);
    }
}

bool Pipeline::ruleActive(const std::string& rule) const { return activeRules_.count(rule) > 0; }

// Mesh emitter geometry (content glTF, metres in UE axes; winding restored for the det -1 axis swap).
int Pipeline::fxMeshFor(const FxLod& L) {
    auto mit = fxMeshes_.find(L.meshGltf);
    if (mit != fxMeshes_.end()) return mit->second;
    MeshData md;
    int id = -1;
    if (assets::loadGlb(L.meshGltf, md)) {
        // TypeDataMesh: the mesh's own section materials unless bOverrideMaterial with a RequiredModule material
        for (render::Material& m : md.mats)
            m.wfcName = (L.overrideMaterial && !L.material.empty()) ? resolveName(L.material) : resolveName(m.sourceName);
        id = upload(md); yieldLoad();
    }
    fxMeshes_[L.meshGltf] = id;
    return id;
}

// ---- simulation (UE3 FParticleEmitterInstance order: kill/update, then spawn) ----
void Pipeline::tickMapFx(float dt) {
    if (dt <= 0.0f || std::getenv("WFC_NOMAPFX")) return;
    // M67 SubUV (RE pass 5 s16, update runner case 0x1F, CONFIRMED): every tick for every live particle (and at spawn as
    // the first update). Cells row-major; the second cell is the next one, wrapping. Linear / Linear_Blend: f =
    // SubImageIndex(RelativeTime), cell floor(f) clamped, interp frac(f) (Linear: 0). Random / Random_Blend: a new pick
    // on the first update, RandomImageTime == 0, or RelativeTime - lastChange > RandomImageTime (lifetime fraction);
    // cell floor(total x r), interp r (Random: 0); otherwise held (interp 0, second cell = the cell).
    auto subuvStep = [&](const FxLod& L, const FxModule& m, FxParticle& q, uint32_t& rng) {
        const int total = L.subH * L.subV;
        if (total <= 1 || L.subMethod == 0) return;
        if (L.subMethod <= 2) {
            float v[3]; evalDist(m, "SubImageIndex", q.relTime, rng, v);
            float f = std::floor(v[0]);
            q.subImage = std::max(0, std::min((int)f, total - 1));
            q.subInterp = L.subMethod == 2 ? v[0] - f : 0.0f;
            q.subImage2 = (q.subImage + 1) % total;
        } else if (!q.subInit || L.randomImageTime == 0.0f || q.relTime - q.subLastChange > L.randomImageTime) {
            rng = rng * 1664525u + 1013904223u;
            float r = (float)(rng >> 8) / 16777216.0f;
            q.subImage = std::min((int)((float)total * r), total - 1);
            q.subInterp = L.subMethod == 4 ? r : 0.0f;
            q.subImage2 = (q.subImage + 1) % total;
            q.subLastChange = q.relTime;
        } else {
            q.subInterp = 0.0f;
            q.subImage2 = q.subImage;
        }
        q.subInit = true;
    };
    // M71 SubUVDirect (update runner case 0x21; RE s16 + addendum 1, CONFIRMED): (H, V) = SubUVPosition(RelativeTime),
    // (H2, V2) = SubUVSize(RelativeTime), every tick, interp 0; the fill writes U = (Pos.x + Size.x c) / SubImages_H,
    // V likewise (cell units, the same scale as the normal modes). pstream labels the two distributions
    // DynamicParams[0] (SubUVPosition) and [1] (SubUVSize).
    auto subuvDirect = [&](const FxModule& m, FxParticle& q, uint32_t& rng) {
        float p[3], s[3];
        auto ip = m.dists.find("DynamicParams[0].ParamValue"), is = m.dists.find("DynamicParams[1].ParamValue");
        if (ip == m.dists.end() || is == m.dists.end()) return;
        ip->second.eval(q.relTime, rng, p);
        is->second.eval(q.relTime, rng, s);
        q.subDirect = true;
        q.subPos[0] = p[0]; q.subPos[1] = p[1];
        q.subSize[0] = s[0]; q.subSize[1] = s[1];
        q.subInterp = 0.0f;
    };
    auto t0 = std::chrono::steady_clock::now();
    float camUE[3] = {camPos_.x * 100.0f, camPos_.z * 100.0f, camPos_.y * 100.0f};
    for (const PickupMeshRT& pm : pickupMeshes_)          // PHYS_Rotating only while available (Pickup state)
        if (pm.yawRate != 0.0f && !pickupMeshHidden_.count(pm.owner))
            pickupSpin_[pm.owner] = std::fmod(pickupSpin_[pm.owner] + pm.yawRate * dt, 65536.0f);
    for (FxInstance& in : fxInstances_) {
        const FxSystem& sys = fxSystems_[in.system];
        if (!in.requiredRule.empty() && !ruleActive(in.requiredRule)) continue;   // factory not in this mode
        if (!actorPoses_.empty()) {                     // frontend matinee: the Emitter actor's pose / DrawScale
            // world = M (x - L0) + L1; rows of R are the component's axes in world space (TransformNormal), so
            // each row is rotated and scaled
            float Mx[9], L0[3], L1[3];
            if (frontendPoseUE(in.ownerShort, Mx, L0, L1)) {
                auto apply = [&](const float v[3], float o[3]) {
                    for (int r = 0; r < 3; ++r) o[r] = Mx[0 * 3 + r] * v[0] + Mx[1 * 3 + r] * v[1] + Mx[2 * 3 + r] * v[2];
                };
                for (int r = 0; r < 3; ++r) apply(in.R0[r], in.R[r]);
                float d[3] = {in.T0[0] - L0[0], in.T0[1] - L0[1], in.T0[2] - L0[2]}, o[3];
                apply(d, o);
                for (int c = 0; c < 3; ++c) in.T[c] = L1[c] + o[c];
            }
        }
        // LOD: Automatic = by distance to the camera; DirectSet = level 0 (no code sets it)
        float dx = in.T[0] - camUE[0], dy = in.T[1] - camUE[1], dz = in.T[2] - camUE[2];
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        int lodIdx = 0;
        if (!sys.directSet)
            for (size_t i = 0; i < sys.lodDistances.size(); ++i) if (dist >= sys.lodDistances[i]) lodIdx = (int)i;
        for (size_t e = 0; e < sys.emitters.size(); ++e) {
            const FxEmitter& em = sys.emitters[e];
            FxEmitterRT& rt = in.emitters[e];
            if (!em.renderable || !in.attached) continue;
            const FxLod& L = em.lods[(size_t)std::min<int>(lodIdx, (int)em.lods.size() - 1)];
            rt.lod = std::min<int>(lodIdx, (int)em.lods.size() - 1);
            if (L.typeData == 2) {
                // M44 Trail2 [PARTIAL]: the ribbon follows the source's recent path (UE3 links the trail particles
                // spawned at the moving source); points live for the longest particle lifetime (min 0.25 s)
                float life = 0.25f;
                for (const FxParticle& q : rt.parts) if (q.oneOverLife > 0.0f) life = std::max(life, 1.0f / q.oneOverLife);
                for (auto& t : rt.trail) t[3] += dt;
                while (!rt.trail.empty() && rt.trail.front()[3] > life) rt.trail.erase(rt.trail.begin());
                if (in.active && in.hasTarget && rt.parts.empty() && rt.loop == 0 && rt.time == 0.0f)
                    rt.forceSpawn = 1;                   // a trail spawned along a segment (tracer): one ribbon
                if (in.active && !in.hasTarget) {
                    const auto& last = rt.trail.empty() ? std::array<float, 4>{1e30f, 0, 0, 0} : rt.trail.back();
                    float dx = in.T[0] - last[0], dy = in.T[1] - last[1], dz = in.T[2] - last[2];
                    if (dx * dx + dy * dy + dz * dz > 25.0f || rt.trail.empty()) {
                        rt.trail.push_back({in.T[0], in.T[1], in.T[2], 0.0f});
                        ++rt.forceSpawn;                 // UE3 trails spawn per distance travelled (not by rate)
                    }
                    if (rt.trail.size() > 96) rt.trail.erase(rt.trail.begin());
                }
            }
            // update / kill
            for (size_t p = 0; p < rt.parts.size();) {
                FxParticle& q = rt.parts[p];
                q.relTime += dt * q.oneOverLife;
                if (q.oneOverLife > 0.0f && q.relTime >= 1.0f) { rt.parts[p] = rt.parts.back(); rt.parts.pop_back(); continue; }
                // ParticleModuleAcceleration: Velocity and BaseVelocity += UsedAcceleration * dt
                for (int c = 0; c < 3; ++c) q.baseVel[c] += q.accel[c] * dt;
                for (int c = 0; c < 3; ++c) q.vel[c] = q.baseVel[c], q.size[c] = q.baseSize[c];
                for (int c = 0; c < 4; ++c) q.color[c] = q.baseColor[c];
                for (const FxModule& m : L.modules) {
                    if (m.flagA == 0) continue;               // disabled module (RE: flagA = bEnabled, HIGH)
                    if (m.name == "PMI_VelocityOverLife") {   // bAbsolute = false (multiplier) [PARTIAL: flag]
                        float s[3]; evalDist(m, "VelOverLife", q.relTime, in.rng, s);
                        for (int c = 0; c < 3; ++c) q.vel[c] *= s[c];
                    } else if (m.name == "PMI_SizeScale") {   // Size = BaseSize * SizeScale(RelativeTime)
                        float s[3]; evalDist(m, "SizeScale", q.relTime, in.rng, s);
                        for (int c = 0; c < 3; ++c) q.size[c] = q.baseSize[c] * s[c];
                    } else if (m.name == "PMI_ColorOverLife") {   // sets Color (not BaseColor) from RelativeTime
                        float cv[3], av[1];
                        auto ic = m.dists.find("ColorOverLife"), ia = m.dists.find("AlphaOverLife");
                        if (ic != m.dists.end()) { ic->second.eval(q.relTime, in.rng, cv); for (int c = 0; c < 3; ++c) q.color[c] = cv[c]; }
                        if (ia != m.dists.end()) { ia->second.eval(q.relTime, in.rng, av); q.color[3] = av[0]; }
                    } else if (m.name == "PMI_SubUV" && L.subMethod != 0) {   // M67: every tick (RE s16)
                        subuvStep(L, m, q, in.rng);
                    } else if (m.name == "PMI_SubUVDirect" && L.subMethod != 0) {   // M71: every tick
                        subuvDirect(m, q, in.rng);
                    } else if (m.name == "PMI_SizeMultiplyLife") {
                        float s[3]; evalDist(m, "LifeMultiplier", q.relTime, in.rng, s);
                        for (int c = 0; c < 3; ++c) q.size[c] *= s[c];
                    } else if (m.name == "PMI_ColorScaleOverLife") {
                        float cs[3], as[1];
                        auto ic = m.dists.find("ColorScaleOverLife"), ia = m.dists.find("AlphaScaleOverLife");
                        if (ic != m.dists.end()) { ic->second.eval(q.relTime, in.rng, cs); for (int c = 0; c < 3; ++c) q.color[c] *= cs[c]; }
                        if (ia != m.dists.end()) { ia->second.eval(q.relTime, in.rng, as); q.color[3] *= as[0]; }
                    }
                }
                for (const FxModule& m : L.modules) {
                    if (m.name != "PMI_LocationEmitterDirect" || m.sourceEmitter.empty() || m.flagA == 0) continue;
                    // M63 Direct update (native update runner 0x83039A18 case 0x14): re-snap particle i to source active
                    // particle i every tick; i >= the source count is left untouched
                    for (size_t e2 = 0; e2 < sys.emitters.size(); ++e2) {
                        if (sys.emitters[e2].name != m.sourceEmitter) continue;
                        const auto& src = in.emitters[e2].parts;
                        if (p < src.size()) { std::copy(src[p].pos, src[p].pos + 3, q.pos); std::copy(src[p].vel, src[p].vel + 3, q.vel); }
                        break;
                    }
                }
                if (L.typeData == 3) {
                    // M60 beam ends (RE pass 5 s11: ResolveSourceData 0x8302F320 / ResolveTargetData 0x8302F738, CONFIRMED),
                    // at spawn and every tick unless locked. Particle methods never read a particle (BeamMethod Target):
                    // they take the Default path. Emitter source = the component origin; Emitter target needs a name,
                    // else the distribution. Default: the distribution at EmitterTime through the component's
                    // LocalToWorld (raw world if bAbsolute); a named Default target first reads the instance parameter
                    // (our segment end point). UserSet: the SetBeam*Point array (our segment end point = target[0]),
                    // empty -> the distribution. Tangents: Direct / Emitter = component X axis; Distribution = raw;
                    // UserSet empty = distribution rotated (unless bAbsolute); times strength.
                    auto xform = [&](const float* v, bool point, float* o) {
                        for (int c = 0; c < 3; ++c) o[c] = v[0] * in.R[0][c] + v[1] * in.R[1][c] + v[2] * in.R[2][c] + (point ? in.T[c] : 0.0f);
                    };
                    auto distPath = [&](const FxLod::BeamEnd& E, float* o) {
                        float v[3]; E.position.eval(rt.time, in.rng, v);
                        if (E.absolute) std::copy(v, v + 3, o); else xform(v, true, o);
                    };
                    auto resolvePos = [&](const FxLod::BeamEnd& E, bool target, float* o) {
                        if (!target && E.method == 2) { std::copy(in.T, in.T + 3, o); return; }
                        if (target && E.method == 2 && E.named) { std::copy(in.T, in.T + 3, o); return; }   // emitter Location (H)
                        if (target && in.hasTarget && (E.method == 1 || (E.method == 0 && E.named))) { std::copy(in.target, in.target + 3, o); return; }
                        distPath(E, o);
                    };
                    auto resolveTan = [&](const FxLod::BeamEnd& E, const FxDist& strength, float* o) {
                        float t[3], s[1];
                        if (E.tangentMethod == 0 || E.tangentMethod == 3) { for (int c = 0; c < 3; ++c) t[c] = in.R[0][c]; }
                        else {
                            float v[3]; E.tangent.eval(q.relTime, in.rng, v);
                            if (E.tangentMethod == 2 || E.absolute) std::copy(v, v + 3, t); else xform(v, false, t);
                        }
                        strength.eval(q.relTime, in.rng, s);
                        for (int c = 0; c < 3; ++c) o[c] = t[c] * s[0];
                    };
                    if (!q.beamInit || !L.beamSrc.lock) resolvePos(L.beamSrc, false, q.beamSrc);
                    if (!q.beamInit || !L.beamSrc.lockTangent) resolveTan(L.beamSrc, L.sourceStrength, q.beamSrcT);
                    if (L.beamDistance) {
                        float d[1]; L.distance.eval(q.relTime, in.rng, d);
                        float xl = std::sqrt(in.R[0][0] * in.R[0][0] + in.R[0][1] * in.R[0][1] + in.R[0][2] * in.R[0][2]);
                        for (int c = 0; c < 3; ++c) q.beamTgt[c] = q.beamSrc[c] + (xl > 0 ? in.R[0][c] / xl : 0.0f) * d[0];
                    } else if (!q.beamInit || !L.beamTgt.lock) resolvePos(L.beamTgt, true, q.beamTgt);
                    if (!q.beamInit || !L.beamTgt.lockTangent) resolveTan(L.beamTgt, L.targetStrength, q.beamTgtT);
                    q.beamInit = true;
                }
                if (L.typeData == 3 && L.noise.on) {
                    // M56 Beam2 noise points (RE pass 5 s9h, native UParticleModuleBeamNoise Spawn 0x8301F6F8 / Update
                    // 0x8301F928, CONFIRMED): N = Frequency, or int(appSRand * (Frequency - LowRange) + LowRange) when
                    // Frequency_LowRange > 0; N + 1 points, Point[i] = NoiseRange(i / (N + 1)) (a random vector in the
                    // range at that ratio). NoiseLockTime < 0: never refreshed; <= 1e-4: re-drawn every tick; > 0: on a
                    // timer. bSmooth re-draws into a target array; the render fill moves each point toward it by
                    // normalize(target - current) * NoiseSpeed * (seconds since the re-draw), snapping within
                    // NoiseLockRadius (RE 9i, CONFIRMED). NoiseRangeScale is applied by the render fill.
                    const auto& N = L.noise;
                    auto rnd = [&]() { in.rng = in.rng * 1664525u + 1013904223u; return (float)(in.rng >> 8) / 16777216.0f; };
                    auto draw = [&](std::vector<float>& dst) {
                        for (int i = 0; i <= q.noiseCount; ++i)
                            N.range.eval((float)i / (float)(q.noiseCount + 1), in.rng, &dst[(size_t)i * 3]);
                    };
                    if (q.noiseCount == 0) {
                        int n = std::abs(N.freq);
                        if (N.freqLow > 0) n = (int)(rnd() * (float)(n - N.freqLow) + (float)N.freqLow);
                        q.noiseCount = std::max(1, n);
                        q.noiseCur.assign((size_t)(q.noiseCount + 1) * 3, 0.0f);
                        q.noiseNext = q.noiseCur;
                        draw(q.noiseCur);
                        if (N.smooth) q.noiseNext = q.noiseCur;
                    } else {
                        q.noiseTimer += dt;
                        bool redraw = false;
                        if (N.lockTime >= 0.0f && (N.lockTime <= 1e-4f || q.noiseTimer > N.lockTime)) redraw = true;
                        if (redraw) { draw(N.smooth ? q.noiseNext : q.noiseCur); q.noiseTimer = 0.0f; }
                        if (N.smooth && N.lockTime >= 0.0f) {
                            float sp[3];
                            N.speed.eval(q.relTime, in.rng, sp);
                            for (int i = 0; i <= q.noiseCount; ++i) {
                                float* cur = &q.noiseCur[(size_t)i * 3];
                                const float* tgt = &q.noiseNext[(size_t)i * 3];
                                float d[3] = {tgt[0] - cur[0], tgt[1] - cur[1], tgt[2] - cur[2]};
                                float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                                float st[3] = {0, 0, 0};
                                if (dl > 1e-6f) for (int c = 0; c < 3; ++c) st[c] = d[c] / dl * sp[c] * q.noiseTimer;
                                float sl = std::sqrt(st[0] * st[0] + st[1] * st[1] + st[2] * st[2]);
                                if (sl >= N.lockRadius) for (int c = 0; c < 3; ++c) cur[c] += st[c];
                                else for (int c = 0; c < 3; ++c) cur[c] = tgt[c];
                            }
                        }
                    }
                }
                for (int c = 0; c < 3; ++c) q.pos[c] += q.vel[c] * dt;
                q.rot += q.rotRate * dt;
                for (int c = 0; c < 3; ++c) q.meshRot[c] += q.meshRotRate[c] * dt;
                ++p;
            }
            if (!in.active || rt.done) continue;
            // emitter time / loops
            rt.time += dt;
            if (rt.time >= L.duration) {
                if (L.loops == 0 || rt.loop + 1 < L.loops) {
                    rt.time = std::fmod(rt.time, L.duration);
                    ++rt.loop;
                    rt.burstFired.clear();
                } else { rt.done = true; continue; }
            }
            float efrac = rt.time / L.duration;
            int spawn = 0;
            float rate[1]; L.spawnRate.eval(efrac, in.rng, rate);
            rt.spawnFrac += std::max(rate[0], 0.0f) * dt;
            spawn += (int)std::floor(rt.spawnFrac);
            rt.spawnFrac -= std::floor(rt.spawnFrac);
            spawn += rt.forceSpawn;
            rt.forceSpawn = 0;
            rt.burstFired.resize(L.bursts.size(), false);
            for (size_t b = 0; b < L.bursts.size(); ++b) {
                if (rt.burstFired[b] || efrac < L.bursts[b].time) continue;
                rt.burstFired[b] = true;
                int lo = L.bursts[b].countLow, hi = L.bursts[b].count;
                if (lo > 0 && hi > lo) { in.rng = in.rng * 1664525u + 1013904223u; spawn += lo + (int)((in.rng >> 8) % (uint32_t)(hi - lo + 1)); }
                else spawn += hi;
            }
            const int peak = L.maxBeams > 0 ? std::min(em.maxPeak, L.maxBeams)              // Beam2: MaxBeamCount
                             : L.trailCap > 0 ? L.trailCap : em.maxPeak;                    // Trail2: trail capacity
            if (L.particleTrail) spawn = std::min(spawn, 1);   // Trail2 Spawn clamps to 1 per tick (RE s14)
            for (int s = 0; s < spawn && (int)rt.parts.size() < peak; ++s) {
                FxParticle q;
                q.seq = rt.spawnSeq++;
                float origin[3] = {0, 0, 0};
                if (!L.localSpace) std::copy(in.T, in.T + 3, origin);
                std::copy(origin, origin + 3, q.pos);
                auto toWorldDir = [&](const float v[3], float o[3]) {   // TransformNormal by the component
                    if (L.localSpace) { std::copy(v, v + 3, o); return; }
                    for (int c = 0; c < 3; ++c) o[c] = v[0] * in.R[0][c] + v[1] * in.R[1][c] + v[2] * in.R[2][c];
                };
                float radial = 0.0f;
                static const char* reading = std::getenv("WFC_FX_FLAGREADING");
                for (const FxModule& m : L.modules) {
                    float v3[3], v1[1], w3[3];
                    // flagA = 0: a spawn-class module contributes nothing under either flag reading
                    if (m.flagA == 0) continue;
                    if (m.name == "PMI_Lifetime") {
                        evalDist(m, "Lifetime", efrac, in.rng, v1);
                        q.oneOverLife = v1[0] > 0.0f ? 1.0f / v1[0] : 0.0f;   // 0 = lives until killed
                    } else if (m.name == "PMI_Size") {
                        evalDist(m, "StartSize", efrac, in.rng, v3);
                        for (int c = 0; c < 3; ++c) q.baseSize[c] += v3[c];
                    } else if (m.name == "PMI_Color") {
                        evalDist(m, "StartColor", efrac, in.rng, v3);
                        evalDist(m, "StartAlpha", efrac, in.rng, v1);
                        for (int c = 0; c < 3; ++c) q.baseColor[c] = v3[c];
                        q.baseColor[3] = v1[0];
                    } else if (m.name == "PMI_Location") {
                        evalDist(m, "StartLocation", efrac, in.rng, v3);
                        toWorldDir(v3, w3);
                        for (int c = 0; c < 3; ++c) q.pos[c] += w3[c];
                    } else if ((m.name == "PMI_LocationEmitter" || m.name == "PMI_LocationEmitterDirect") &&
                               !m.sourceEmitter.empty()) {
                        // M63 (RE pass 5 s14, native spawn runner 0x8303E288 cases 0x13 / 0x14, CONFIRMED): the source
                        // emitter instance of this system by EmitterName. No source / no live particle: nothing (the
                        // particle keeps its normal spawn position). Random = floor(frand x count); Sequential = a
                        // pre-incremented counter wrapping to 0 (1, 2, .., n-1, 0); else particle 0. Direct: the same
                        // active index (re-snapped every tick below). Position = the source particle's location through
                        // source space -> this space; a source born this frame uses the source emitter's location.
                        // Inherit velocity (BaseVelocity and Velocity) / rotation x scale. [PARTIAL: the sub-frame
                        // terms - src.Velocity x S + Velocity x S are not modelled; this runtime spawns at frame time]
                        for (size_t e2 = 0; e2 < sys.emitters.size(); ++e2) {
                            if (sys.emitters[e2].name != m.sourceEmitter) continue;
                            const auto& srcRt = in.emitters[e2];
                            const auto& src = srcRt.parts;
                            if (src.empty()) break;
                            size_t idx = 0;
                            if (m.name == "PMI_LocationEmitterDirect") {
                                idx = rt.parts.size();
                                if (idx >= src.size()) break;
                            } else if (m.selection == 0) {
                                in.rng = in.rng * 1664525u + 1013904223u;
                                idx = std::min((size_t)((float)(in.rng >> 8) / 16777216.0f * (float)src.size()), src.size() - 1);
                            } else if (m.selection == 1) {
                                if (++rt.locSequence >= (int)src.size()) rt.locSequence = 0;
                                idx = (size_t)rt.locSequence;
                            }
                            const FxParticle& sp = src[idx];
                            const FxLod& SL = sys.emitters[e2].lods[(size_t)std::min<int>(srcRt.lod, (int)sys.emitters[e2].lods.size() - 1)];
                            float wpos[3], wvel[3];
                            if (sp.relTime == 0.0f) std::copy(in.T, in.T + 3, wpos);     // born this frame: emitter location
                            else if (SL.localSpace) {
                                for (int c = 0; c < 3; ++c) wpos[c] = sp.pos[0] * in.R[0][c] + sp.pos[1] * in.R[1][c] + sp.pos[2] * in.R[2][c] + in.T[c];
                            } else std::copy(sp.pos, sp.pos + 3, wpos);
                            if (SL.localSpace) for (int c = 0; c < 3; ++c) wvel[c] = sp.vel[0] * in.R[0][c] + sp.vel[1] * in.R[1][c] + sp.vel[2] * in.R[2][c];
                            else std::copy(sp.vel, sp.vel + 3, wvel);
                            if (L.localSpace) {                          // world -> this emitter's local space
                                float d[3] = {wpos[0] - in.T[0], wpos[1] - in.T[1], wpos[2] - in.T[2]};
                                for (int r = 0; r < 3; ++r) {
                                    float l2 = in.R[r][0] * in.R[r][0] + in.R[r][1] * in.R[r][1] + in.R[r][2] * in.R[r][2];
                                    q.pos[r] = l2 > 0 ? (d[0] * in.R[r][0] + d[1] * in.R[r][1] + d[2] * in.R[r][2]) / l2 : 0.0f;
                                    float v = l2 > 0 ? (wvel[0] * in.R[r][0] + wvel[1] * in.R[r][1] + wvel[2] * in.R[r][2]) / l2 : 0.0f;
                                    if (m.inheritVelocity) { q.baseVel[r] += v * m.inheritVelocityScale; }
                                }
                            } else {
                                std::copy(wpos, wpos + 3, q.pos);
                                if (m.inheritVelocity) for (int c = 0; c < 3; ++c) q.baseVel[c] += wvel[c] * m.inheritVelocityScale;
                            }
                            if (m.inheritRotation) q.rot += sp.rot * m.inheritRotationScale;
                            if (m.name == "PMI_LocationEmitterDirect")
                                for (int c = 0; c < 3; ++c) q.baseVel[c] = L.localSpace ? q.baseVel[c] : wvel[c];
                            break;
                        }
                    } else if (m.name == "PMI_LocationPrimitiveSphere") {   // [PARTIAL] sampling rule
                        float rad[1], vs[1], off[3], d[3];
                        evalDist(m, "StartRadius", efrac, in.rng, rad);
                        evalDist(m, "VelocityScale", efrac, in.rng, vs);
                        evalDist(m, "StartLocation", efrac, in.rng, off);
                        float n2 = 0;
                        do {
                            for (int c = 0; c < 3; ++c) { in.rng = in.rng * 1664525u + 1013904223u; d[c] = (float)(in.rng >> 8) / 8388608.0f - 1.0f; }
                            n2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
                        } while (n2 < 1e-6f || n2 > 1.0f);
                        float inv = 1.0f / std::sqrt(n2);
                        for (int c = 0; c < 3; ++c) { d[c] *= inv; v3[c] = off[c] + d[c] * rad[0]; }
                        toWorldDir(v3, w3);
                        for (int c = 0; c < 3; ++c) q.pos[c] += w3[c];
                        toWorldDir(d, w3);
                        for (int c = 0; c < 3; ++c) q.baseVel[c] += w3[c] * vs[0];
                    } else if (m.name == "PMI_Velocity") {
                        evalDist(m, "StartVelocity", efrac, in.rng, v3);
                        toWorldDir(v3, w3);
                        for (int c = 0; c < 3; ++c) q.baseVel[c] += w3[c];
                        evalDist(m, "StartVelocityRadial", efrac, in.rng, v1);
                        radial += v1[0];
                    } else if (m.name == "PMI_Rotation") {
                        evalDist(m, "StartRotation", efrac, in.rng, v1);
                        q.rot += v1[0] * 6.2831853f;                     // turns -> radians
                    } else if (m.name == "PMI_MeshRotation") {
                        evalDist(m, "StartRotation", efrac, in.rng, v3);
                        for (int c = 0; c < 3; ++c) q.meshRot[c] += v3[c] * 360.0f;   // turns -> degrees
                    } else if (m.name == "PMI_RotationRate") {   // turns / s -> radians / s
                        evalDist(m, "StartRotationRate", efrac, in.rng, v1);
                        q.rotRate += v1[0] * 6.2831853f;
                    } else if (m.name == "PMI_ColorOverLife") {  // spawn: Color = BaseColor = value at RelativeTime 0
                        auto ic = m.dists.find("ColorOverLife"), ia = m.dists.find("AlphaOverLife");
                        if (ic != m.dists.end()) { ic->second.eval(0.0f, in.rng, v3); for (int c = 0; c < 3; ++c) q.baseColor[c] = v3[c]; }
                        if (ia != m.dists.end()) { ia->second.eval(0.0f, in.rng, v1); q.baseColor[3] = v1[0]; }
                    } else if (m.name == "PMI_Acceleration") {
                        evalDist(m, "Acceleration", efrac, in.rng, v3);
                        toWorldDir(v3, w3);
                        for (int c = 0; c < 3; ++c) q.accel[c] += w3[c];
                    } else if (m.name == "PMI_SubUV" && L.subMethod != 0) {   // spawn = the first update (M67)
                        subuvStep(L, m, q, in.rng);
                    } else if (m.name == "PMI_SubUVDirect" && L.subMethod != 0) {   // spawn = the first update (M71)
                        subuvDirect(m, q, in.rng);
                    } else if (m.name == "PMI_MeshRotationRate") {
                        evalDist(m, "StartRotationRate", efrac, in.rng, v3);
                        for (int c = 0; c < 3; ++c) q.meshRotRate[c] += v3[c] * 360.0f;
                    } else if (m.name == "PMI_ColorByParameter") {
                        // Color = BaseColor = the component's colour InstanceParameter (Steam_Sm_FX: 'SteamColor',
                        // the FName in the compiled LOD stream), else DefaultColor (CDO white; the stream carries
                        // FFFFFFFF). Single colour parameter per Streets instance.
                        float c4[4] = {1, 1, 1, 1};
                        // order: the component's InstanceParameter, the spawnFx caller's colour, the module's decoded
                        // DefaultColor (M34), white
                        auto it = in.colorParams.find("SteamColor");
                        if (it == in.colorParams.end()) it = in.colorParams.find("Color");   // PSC 'Color' parameter
                        if (it == in.colorParams.end()) it = in.colorParams.find("*");   // spawnFx colour
                        if (it != in.colorParams.end()) std::copy(it->second.begin(), it->second.end(), c4);
                        else if (L.hasDefaultColor) std::copy(L.defaultColor, L.defaultColor + 4, c4);
                        std::copy(c4, c4 + 4, q.baseColor);
                    } else if (m.name == "PMI_DynamicParameter") {          // [PARTIAL] slot order
                        for (int k = 0; k < 4; ++k) {
                            auto it = m.dists.find("DynamicParams[" + std::to_string(k) + "].ParamValue");
                            if (it != m.dists.end()) { it->second.eval(efrac, in.rng, v1); rt.dynParam[k] = v1[0]; }
                        }
                        rt.hasDyn = true;
                    }
                }
                if (radial != 0.0f) {   // StartVelocityRadial: along (particle - owner) location
                    float d[3];
                    for (int c = 0; c < 3; ++c) d[c] = q.pos[c] - origin[c];
                    float n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                    if (n > 1e-4f) for (int c = 0; c < 3; ++c) q.baseVel[c] += d[c] / n * radial;
                }
                for (int c = 0; c < 3; ++c) { q.vel[c] = q.baseVel[c]; q.size[c] = q.baseSize[c]; }
                for (int c = 0; c < 4; ++c) q.color[c] = q.baseColor[c];
                // spawn-time over-life values at RelativeTime 0
                for (const FxModule& m : L.modules) {
                    if (m.flagA == 0) continue;
                    if (m.name == "PMI_SizeMultiplyLife") {
                        float s3[3]; evalDist(m, "LifeMultiplier", 0.0f, in.rng, s3);
                        for (int c = 0; c < 3; ++c) q.size[c] *= s3[c];
                    } else if (m.name == "PMI_ColorScaleOverLife") {
                        float cs[3], as[1];
                        auto ic = m.dists.find("ColorScaleOverLife"), ia = m.dists.find("AlphaScaleOverLife");
                        if (ic != m.dists.end()) { ic->second.eval(0.0f, in.rng, cs); for (int c = 0; c < 3; ++c) q.color[c] *= cs[c]; }
                        if (ia != m.dists.end()) { ia->second.eval(0.0f, in.rng, as); q.color[3] *= as[0]; }
                    }
                }
                rt.parts.push_back(q);
            }
        }
    }
    // runtime effects: released once nothing will spawn again and every particle has died
    fxInstances_.erase(std::remove_if(fxInstances_.begin(), fxInstances_.end(), [](const FxInstance& in) {
        if (!in.transient) return false;
        for (const FxEmitterRT& rt : in.emitters)
            if (!rt.parts.empty() || (in.active && !rt.done)) return false;
        return true;
    }), fxInstances_.end());
    statFxMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// ---- runtime particle effects (template library) ----
int Pipeline::spawnFx(const std::string& tpl, const float R[3][3], const float T[3], const float* color, const float* target) {
    auto it = fxSystems_.find(tpl);
    if (it == fxSystems_.end()) {
        static std::set<std::string> logged;
        if (logged.insert(dataDir_ + "|" + tpl).second)
            LOG_WARN("fx: template %s is not in this map's render data (map_fx_runtime.json)", tpl.c_str());
        return -1;
    }
    FxInstance in;
    in.system = tpl;
    in.role = "runtime";
    in.transient = true;
    in.id = nextFxId_++;
    std::copy(&R[0][0], &R[0][0] + 9, &in.R[0][0]);
    std::copy(T, T + 3, in.T);
    std::copy(&R[0][0], &R[0][0] + 9, &in.R0[0][0]);
    std::copy(T, T + 3, in.T0);
    if (color) in.colorParams["*"] = {color[0], color[1], color[2], color[3]};
    if (target) { in.hasTarget = true; std::copy(target, target + 3, in.target); }
    in.rng = 0x9E3779B9u * (uint32_t)in.id;
    in.emitters.resize(it->second.emitters.size());
    fxInstances_.push_back(std::move(in));
    return fxInstances_.back().id;
}

bool Pipeline::setFxTransform(int id, const float R[3][3], const float T[3]) {
    for (FxInstance& in : fxInstances_)
        if (in.transient && in.id == id) {
            std::copy(&R[0][0], &R[0][0] + 9, &in.R[0][0]);
            std::copy(T, T + 3, in.T);
            return true;
        }
    return false;
}

bool Pipeline::setFxTarget(int id, const float target[3]) {
    for (FxInstance& in : fxInstances_)
        if (in.transient && in.id == id) { in.hasTarget = true; std::copy(target, target + 3, in.target); return true; }
    return false;
}

bool Pipeline::setFxParam(int id, const std::string& name, const float v[4]) {
    for (FxInstance& in : fxInstances_)
        if (in.transient && in.id == id) { in.colorParams[name] = {v[0], v[1], v[2], v[3]}; return true; }
    return false;
}

void Pipeline::stopFx(int id) {
    for (FxInstance& in : fxInstances_)
        if (in.transient && in.id == id) in.active = false;
}

bool Pipeline::setMaterialParam(const std::string& actor, const std::string& param, const float v[4]) {
    std::string a = actor;
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    if (a.rfind('.') != std::string::npos) a = a.substr(a.rfind('.') + 1);
    auto it = miaMaterial_.find(a);
    if (it == miaMaterial_.end()) {
        static std::set<std::string> logged;
        if (logged.insert(dataDir_ + "|" + a).second)
            LOG_WARN("material param: %s is not a MaterialInstanceActor of this scene", actor.c_str());
        return false;
    }
    std::string id;                                     // matc rt_ident: the uniform suffix
    for (char ch : param) id += (std::isalnum((unsigned char)ch) || ch == '_') ? ch : '_';
    auto& ps = matParams_[it->second];
    for (auto& pv : ps)
        if (pv.first == id) { std::copy(v, v + 4, pv.second.begin()); return true; }
    ps.push_back({id, {v[0], v[1], v[2], v[3]}});
    return true;
}

int Pipeline::liveFx() const {
    return (int)std::count_if(fxInstances_.begin(), fxInstances_.end(), [](const FxInstance& in) { return in.transient; });
}

// ---- map props: totems + destructible ----
void Pipeline::loadMapProps(const std::string& indexPath) {
    std::string txt = readFile(indexPath);
    assets::Json J;
    if (txt.empty() || !assets::Json::parse(txt, J)) return;
    const assets::Json& PV = J["pickup_factory_visuals"];
    for (size_t i = 0; i < PV.size(); ++i) {
        const assets::Json& e = PV[i];
        std::string gl = e["gltf"].asString();
        if (gl.rfind("content/", 0) != 0 || e["ue_matrix"].size() < 4) continue;
        if (e["kind"].asString().find("mesh particle") != std::string::npos) continue;   // drawn by its FX emitter
        PickupMeshRT pm;
        std::string a = e["actor"].asString();
        pm.owner = a.substr(a.rfind('.') + 1);
        std::transform(pm.owner.begin(), pm.owner.end(), pm.owner.begin(), ::tolower);
        pm.gltf = gl.substr(8);
        pm.mesh = e["mesh"].asString();
        const assets::Json& U = e["ue_matrix"];
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) pm.R[r][c] = U[(size_t)r][(size_t)c].asFloat();
        for (int c = 0; c < 3; ++c) pm.T[c] = U[3][(size_t)c].asFloat();
        const assets::Json& ct = e["component_transform"]["Translation"];       // pickup mesh offset (actor space)
        if (ct.isObject()) {
            float o[3] = {ct["X"].asFloat(), ct["Y"].asFloat(), ct["Z"].asFloat()};
            for (int c = 0; c < 3; ++c) pm.off[c] = o[c];
        }
        pm.yawRate = e["rotation_rate"]["Yaw"].asFloat(0.0f);
        pm.cullDistance = e["CullDistance"].asFloat(0.0f);
        pickupMeshes_.push_back(pm);
    }
    // destructible state mesh -> authored StaticMeshComponent (cooked HmStaticMeshDestructionEffect.MeshComponents,
    // written by build_map_fx.py): the lightmap join key of each state mesh
    std::map<std::string, std::vector<std::string>> destructComps;
    {
        assets::Json D;
        std::string t = readFile(dataDir_ + "/map_fx_runtime.json");
        if (!t.empty() && assets::Json::parse(t, D))
            for (const auto& kv : D["destructible_mesh_components"].obj)
                for (const auto& mv : kv.second.obj)
                    for (size_t k = 0; k < mv.second.size(); ++k)
                        destructComps[kv.first + "|" + mv.first].push_back(mv.second[k].asString());
    }
    {   // UI families: actor table (poses, bHidden, PHYS_Rotating) + skeletal actors not in world.glb
        const assets::Json& AL = J["actors_by_level (names = Matinee / Kismet targets; world_glb_node = node name in world.glb)"];
        if (AL.isObject()) {
            loadSceneActors(AL);
            loadSceneNonDrawnActors(AL, J["cameras"]);
            std::map<std::string, int> meshByGltf;
            for (const auto& lv : AL.obj)
                for (size_t i = 0; i < lv.second.size(); ++i) {
                    const assets::Json& e = lv.second[i];
                    if (e["class"].asString() != "HmSkeletalMeshActor") continue;
                    std::string gl = e["gltf"].asString();
                    if (gl.rfind("content/", 0) != 0 || e["gltf_matrix"].size() < 16) continue;
                    int id;
                    auto mi = meshByGltf.find(gl);
                    if (mi != meshByGltf.end()) id = mi->second;
                    else {
                        MeshData md;
                        id = -1;
                        if (assets::loadGlb(contentRoot() + gl.substr(8), md)) {   // bind pose
                            const std::string mesh = e["mesh"].asString(), pkg = mesh.substr(0, mesh.rfind('.'));
                            for (render::Material& m : md.mats) {
                                std::string full = pkg + "." + m.sourceName;
                                m.wfcName = resolveName(mats_.count(full) ? full : m.sourceName);
                            }
                            id = upload(md); yieldLoad();
                        }
                        meshByGltf[gl] = id;
                    }
                    if (id < 0) continue;
                    MapProp p;
                    p.actor = e["actor"].asString(); p.kind = 3;
                    p.actorLower = p.actor; std::transform(p.actorLower.begin(), p.actorLower.end(), p.actorLower.begin(), ::tolower);
                    for (int k = 0; k < 16; ++k) p.model.m[k] = e["gltf_matrix"][(size_t)k].asFloat();
                    p.stateMesh[0] = id;
                    mapProps_.push_back(p);
                }
        }
    }
    const assets::Json& R = J["not_in_world_glb (authored renderables)"];
    for (size_t i = 0; i < R.size(); ++i) {
        const assets::Json& e = R[i];
        std::string kind = e["kind"].asString();
        const assets::Json& T = e["transform"];
        float loc[3] = {T["Location"]["X"].asFloat(), T["Location"]["Y"].asFloat(), T["Location"]["Z"].asFloat()};
        float rows[3][3];
        rotRows(T["Rotation"]["Pitch"].asFloat(), T["Rotation"]["Yaw"].asFloat(), T["Rotation"]["Roll"].asFloat(), rows);
        float ds = T["DrawScale"].asFloat(1.0f);
        float s3[3] = {T["DrawScale3D"]["X"].asFloat(1.0f) * ds, T["DrawScale3D"]["Y"].asFloat(1.0f) * ds, T["DrawScale3D"]["Z"].asFloat(1.0f) * ds};
        float R3[3][3];
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) R3[r][c] = rows[r][c] * s3[r];
        std::string actor = e["actor"].asString();
        actor = actor.substr(actor.rfind('.') + 1);
        if (kind.find("totem") != std::string::npos) {
            if (!totemModel_) {
                totemModel_ = std::make_unique<assets::SkinnedModel>();
                std::string gl = contentRoot() + e["gltf"].asString().substr(8);   // strip "content/"
                std::string an = contentRoot() + e["anim"].asString().substr(8);
                if (!assets::loadSkinnedGlb(gl, *totemModel_)) { totemModel_.reset(); continue; }
                assets::loadAnimationsByName(an, *totemModel_);
                for (render::Material& m : totemModel_->mats)
                    m.wfcName = resolveName("PROP_NEU_Pickups_p.EnergonTotem." + m.sourceName);
                totemClip_ = totemModel_->clipByName(e["idle_anim (DeactivatedLoopAnim)"].asString());
            }
            MapProp p;
            p.actor = actor; p.kind = 0;
            p.actorLower = actor; std::transform(p.actorLower.begin(), p.actorLower.end(), p.actorLower.begin(), ::tolower);
            p.model = ueRowsToGltf(R3, loc);
            mapProps_.push_back(p);
        } else if (kind.find("KOTH") != std::string::npos) {
            // TnKingOfTheHillZone ActiveMeshComponent0 (class template, pTorus1_STAT, CaptureZone_Reverse_MAT_INST):
            // shown only for the zone Gameplay activates (default bHidden; KOTH Inactive zones hidden)
            const assets::Json& U = e["ue_matrix"];
            float R4[3][3], T4[3];
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) R4[r][c] = U[(size_t)r][(size_t)c].asFloat();
            for (int c = 0; c < 3; ++c) T4[c] = U[3][(size_t)c].asFloat();
            MapProp p;
            p.actor = actor; p.kind = 2;
            p.actorLower = actor; std::transform(p.actorLower.begin(), p.actorLower.end(), p.actorLower.begin(), ::tolower);
            authoredHiddenActors_.insert(p.actorLower);
            p.model = ueRowsToGltf(R4, T4);
            if (kothMesh_ < 0) {
                MeshData md;
                std::string gl = contentRoot() + e["gltf"].asString().substr(8);
                if (assets::loadGlb(gl, md)) {
                    std::string mat = e["section_materials"][0].asString();
                    for (render::Material& m : md.mats) m.wfcName = resolveName(mat.empty() ? m.sourceName : mat);
                    kothMesh_ = upload(md); yieldLoad();
                }
            }
            p.stateMesh[0] = kothMesh_;
            mapProps_.push_back(p);
        } else if (kind.find("destructible") != std::string::npos) {
            const assets::Json& ct = e["component_translation_ue"];
            float tl[3] = {ct[0].asFloat(), ct[1].asFloat(), ct[2].asFloat()};
            float wl[3];
            for (int c = 0; c < 3; ++c) wl[c] = loc[c] + tl[0] * R3[0][c] + tl[1] * R3[1][c] + tl[2] * R3[2][c];
            MapProp p;
            p.actor = actor; p.kind = 1;
            p.actorLower = actor; std::transform(p.actorLower.begin(), p.actorLower.end(), p.actorLower.begin(), ::tolower);
            p.model = ueRowsToGltf(R3, wl);
            // state meshes (render_index states[]: handle 0 intact, the next state's static mesh = destroyed) and
            // their authored StaticMeshComponents (lightmap join keys; the candidate that carries a lightmap)
            const std::string actorFull = e["actor"].asString();
            const assets::Json& ST = e["states"];
            for (int k = 0; k < 2 && (size_t)k < ST.size(); ++k) {
                const assets::Json& sm0 = ST[(size_t)k]["static_meshes"][0];
                std::string gl = sm0["gltf"].asString(), meshObj = sm0["mesh"].asString();
                MeshData md;
                if (gl.rfind("content/", 0) != 0 || !assets::loadGlb(contentRoot() + gl.substr(8), md)) {
                    p.stateMesh[k] = -1; continue;
                }
                std::string comp;
                for (const std::string& c : destructComps[actorFull + "|" + meshObj]) {
                    std::string lc = c; std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
                    if (comp.empty() || lightmaps_.count(c) || lightmaps_.count(lc)) comp = c;
                    if (lightmaps_.count(c) || lightmaps_.count(lc)) break;
                }
                for (SubMesh& sm : md.subs) sm.component = comp;
                const std::string pkg = meshObj.substr(0, meshObj.find('.'));
                for (render::Material& m : md.mats) {
                    std::string full = pkg + ".Materials." + m.sourceName;
                    m.wfcName = resolveName(mats_.count(full) ? full : m.sourceName);
                }
                p.stateMesh[k] = upload(md); yieldLoad();
                if (std::getenv("WFC_PROPLOG")) {         // diagnostics: world-space bounds of each state mesh
                    core::Vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
                    for (size_t v = 0; v + 2 < md.positions.size(); v += 3) {
                        core::Vec3 w = core::transformPoint(p.model, core::Vec3{md.positions[v], md.positions[v + 1], md.positions[v + 2]});
                        mn = {std::min(mn.x, w.x), std::min(mn.y, w.y), std::min(mn.z, w.z)};
                        mx = {std::max(mx.x, w.x), std::max(mx.y, w.y), std::max(mx.z, w.z)};
                    }
                    LOG_INFO("map prop %s state mesh %d: id %d, world bounds (%.2f %.2f %.2f)-(%.2f %.2f %.2f)", actor.c_str(), k,
                             p.stateMesh[k], mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);
                }
            }
            mapProps_.push_back(p);
        }
    }
    LOG_INFO("wfc: map props: %zu (energon totems + destructible), totem clip %d", mapProps_.size(), totemClip_);
    int fxMeshes = 0;
    for (auto& kv : fxSystems_)                     // mesh-emitter geometry loaded up front (no draw-time loads)
        for (const FxEmitter& em : kv.second.emitters)
            if (em.renderable)
                for (const FxLod& L : em.lods) if (!L.meshGltf.empty() && fxMeshFor(L) >= 0) ++fxMeshes;
    LOG_INFO("wfc: map FX mesh emitters loaded: %d", fxMeshes);
}

void Pipeline::setDestructibleState(const std::string& actor, int state) {
    std::string a = actor;
    for (MapProp& p : mapProps_)
        if (p.kind == 1 && (p.actor == a || a.substr(a.rfind('.') == std::string::npos ? 0 : a.rfind('.') + 1) == p.actor))
            p.state = state;
}

// Ammo-crate factory yaw while available: PickupRotationRate Yaw 10000 UU/s (TnAmmoCratePickup, authored) from the
// map clock; the factory's local-space effects and its mesh follow it (RE MILESTONE04 pickup §2, HIGH). The phase of
// the spin and its reset on respawn are native [PROVISIONAL: continuous from map start].
bool Pipeline::pickupRuleBlocked(const std::string& ownerLower) const {
    for (const FxInstance& in : fxInstances_)
        if (in.owner == ownerLower && !in.requiredRule.empty() && !ruleActive(in.requiredRule)) return true;
    return false;
}

float Pipeline::pickupYaw(const std::string& ownerLower) const {
    auto it = pickupSpin_.find(ownerLower);
    return it == pickupSpin_.end() ? 0.0f : it->second;
}

// ---- drawing (after the frame's opaque + character draws, before post) ----
void Pipeline::drawMapPresentation() {
    if (std::getenv("WFC_NOMAPFX")) { flushTranslucency(); return; }
    auto t0 = std::chrono::steady_clock::now();
    // props
    for (MapProp& p : mapProps_) {
        if (actorHidden(p.actorLower)) continue;
        if (p.kind == 0 && totemModel_) {
            assets::evaluatePose(*totemModel_, totemClip_, mapTime(), totemScratch_, totemPose_, true);
            drawDynamic(totemPose_, p.model);
        } else if (p.kind == 1) {
            int k = p.state == 0 ? 0 : 1;
            if (p.stateMesh[k] >= 0) draw(p.stateMesh[k], p.model);
        } else if (p.kind == 2 && p.stateMesh[0] >= 0) {   // active KOTH zone ring
            draw(p.stateMesh[0], p.model);
        } else if (p.kind == 3 && p.stateMesh[0] >= 0) {   // scene skeletal actor (bind pose) + its matinee / mover delta
            auto mv = moverDelta_.find(p.actorLower);
            draw(p.stateMesh[0], mv != moverDelta_.end() ? mv->second * p.model : p.model);
        }
    }
    // pickup factory meshes (render_index pickup_factory_visuals)
    {
        float camUE[3] = {camPos_.x * 100.0f, camPos_.z * 100.0f, camPos_.y * 100.0f};
        for (PickupMeshRT& pm : pickupMeshes_) {
            if (pickupMeshHidden_.count(pm.owner)) continue;
            if (pm.meshId == -1) {                        // lazy, shared per glTF
                pm.meshId = -2;
                for (const PickupMeshRT& o : pickupMeshes_) if (o.gltf == pm.gltf && o.meshId >= 0) pm.meshId = o.meshId;
                MeshData md;
                if (pm.meshId < 0 && assets::loadGlb(contentRoot() + pm.gltf, md)) {
                    const std::string pkg = pm.mesh.substr(0, pm.mesh.rfind('.'));
                    for (render::Material& m : md.mats) {
                        std::string full = pkg + "." + m.sourceName;
                        m.wfcName = resolveName(mats_.count(full) ? full : m.sourceName);
                    }
                    pm.meshId = upload(md); yieldLoad();
                }
            }
            if (pm.meshId < 0) continue;
            if (pickupRuleBlocked(pm.owner)) continue;    // factory Disabled outside its game rule (flag / bomb)
            float dx = pm.T[0] - camUE[0], dy = pm.T[1] - camUE[1], dz = pm.T[2] - camUE[2];
            if (pm.cullDistance > 0.0f && dx * dx + dy * dy + dz * dz > pm.cullDistance * pm.cullDistance) continue;
            float spin[3][3], rows[3][3];
            rotRows(0.0f, pickupYaw(pm.owner), 0.0f, spin);
            for (int r = 0; r < 3; ++r)                   // FRotationMatrix(P, Y + d, R) = base rows x Yaw(d)
                for (int c = 0; c < 3; ++c) rows[r][c] = pm.R[r][0] * spin[0][c] + pm.R[r][1] * spin[1][c] + pm.R[r][2] * spin[2][c];
            float wt[3];                                  // component offset rotates with the actor
            for (int c = 0; c < 3; ++c) wt[c] = pm.T[c] + pm.off[0] * rows[0][c] + pm.off[1] * rows[1][c] + pm.off[2] * rows[2][c];
            draw(pm.meshId, ueRowsToGltf(rows, wt));
        }
    }
    // particles
    core::Vec3 camR{camView_.m[0], camView_.m[4], camView_.m[8]};
    core::Vec3 camU{camView_.m[1], camView_.m[5], camView_.m[9]};
    core::Vec3 camF{-camView_.m[2], -camView_.m[6], -camView_.m[10]};
    int sprites = 0, meshes = 0;
    for (FxInstance& in : fxInstances_) {
        const FxSystem& sys = fxSystems_[in.system];
        if (in.hidden) continue;
        for (size_t e = 0; e < sys.emitters.size(); ++e) {
            FxEmitterRT& rt = in.emitters[e];
            if (rt.parts.empty()) continue;
            const FxLod& L = sys.emitters[e].lods[(size_t)rt.lod];
            float IR[3][3];                                    // instance rows (ammo factory: spinning yaw)
            std::memcpy(IR, in.R, sizeof(IR));
            // Particle size scale, CONFIRMED in the WFC xex (RE 2026-10-05): FillReplayData sprite 0x8300C678 / mesh
            // 0x83052BD8: Scale = Component Scale(+0x178) * Scale3D(+0x17C) * owner DrawScale(+0x140) * DrawScale3D(+0x144)
            // unless AbsoluteScale (+0xF4 & 0x100000). Mesh gates: local-space emitters get none (LocalToWorld carries
            // it: IR below) and a per-instance ignore-component-scale flag drops the component factor - no TypeDataMesh
            // in Streets / Berth / UI_FrontEnd sets it. = the per-axis length of the instance rows.
            static const bool noSizeScale = std::getenv("WFC_FX_NOSIZESCALE") != nullptr;   // A/B diagnostics
            float sizeScale[3] = {1, 1, 1};
            if (!noSizeScale)
                for (int r = 0; r < 3; ++r)
                    sizeScale[r] = std::sqrt(in.R[r][0] * in.R[r][0] + in.R[r][1] * in.R[r][1] + in.R[r][2] * in.R[r][2]);
            if (pickupSpin_.count(in.owner)) {               // the factory actor's spin carries its components
                float spin[3][3];
                rotRows(0.0f, pickupYaw(in.owner), 0.0f, spin);
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        IR[r][c] = in.R[r][0] * spin[0][c] + in.R[r][1] * spin[1][c] + in.R[r][2] * spin[2][c];
            }
            // ParticleModuleSizeMultiplyLife by instance parameter (RE pass 4, CONFIRMED data): Size *= the PSC's vector
            // parameter (identity mapping), else the distribution's Constant; only emitters that carry the module
            float sizeParam[3] = {1, 1, 1};
            if (!L.sizeParam.empty()) {
                auto sz = in.colorParams.find(L.sizeParam);
                for (int r = 0; r < 3; ++r) {
                    sizeParam[r] = sz != in.colorParams.end() ? std::max(sz->second[(size_t)r], 0.0f) : L.sizeParamConst[r];
                    sizeScale[r] *= sizeParam[r];
                }
            }
            auto worldPos = [&](const float p[3], float o[3]) {
                if (!L.localSpace) { std::copy(p, p + 3, o); return; }
                for (int c = 0; c < 3; ++c) o[c] = in.T[c] + p[0] * IR[0][c] + p[1] * IR[1][c] + p[2] * IR[2][c];
            };
            if (L.typeData >= 2) {
                // M44 Trail2 / Beam2. M55: Beam2 taper and Trail2 tessellation (RE pass 5 s9, native) [HIGH]; noise /
                // BeamSineWave (render formula UNKNOWN) and tiling distance not applied [PARTIAL]. A beam, or a trail spawned
                // along a segment (tracer), draws each live particle as a camera-facing ribbon from the source to the
                // target with that particle's width / colour; a trail on a moving source draws one ribbon through its
                // recent path, fading with age, at the newest particle's width / colour.
                std::vector<Sprite> sp;
                // M65 BillboardSettings: a fixed side axis (component LocalToWorld row, normalized, or a world axis)
                // instead of cross(view, dir); offset = (2V - 1) x Size x sideScale x axis (RE s13 addendum 3, HIGH).
                // fixedSide = the V0 direction (our corners put V1 at p - side); scales applied per side
                core::Vec3 fixedSide{0, 0, 0};
                if (L.billboardAxis >= 0) {
                    float a[3] = {0, 0, 0};
                    if (L.billboardAxis < 3) { for (int c = 0; c < 3; ++c) a[c] = in.R[L.billboardAxis][c]; }
                    else a[L.billboardAxis - 3] = 1.0f;
                    core::Vec3 g = ueToGltf(a);
                    float gl = core::length(g);
                    if (gl > 1e-6f) fixedSide = g * (-1.0f / gl);
                }
                const bool fixedAxis = core::length(fixedSide) > 0.0f;
                if (fixedAxis && std::getenv("WFC_FXTEST")) {
                    static std::set<std::string> logged;
                    if (logged.insert(in.system + "/" + sys.emitters[e].name).second)
                        LOG_INFO("FXTEST billboard %s/%s axis %d side (%.3f %.3f %.3f) component R%d (%.3f %.3f %.3f) scales %.1f/%.1f",
                                 in.system.c_str(), sys.emitters[e].name.c_str(), L.billboardAxis, fixedSide.x, fixedSide.y,
                                 fixedSide.z, L.billboardAxis % 3, in.R[L.billboardAxis % 3][0], in.R[L.billboardAxis % 3][1],
                                 in.R[L.billboardAxis % 3][2], L.sideScale[0], L.sideScale[1]);
                }
                auto quad = [&](const core::Vec3& a, const core::Vec3& b, float w0, float w1, const float* col0,
                                const float* col1, float u0, float u1) {
                    core::Vec3 d = b - a;
                    float l = core::length(d);
                    if (l < 1e-4f) return;
                    core::Vec3 mid = (a + b) * 0.5f;
                    // sprite corner order: x (u) along the ribbon, y (v) across it, facing the camera
                    core::Vec3 side = fixedAxis ? fixedSide : core::cross(core::normalize(camPos_ - mid), d * (1.0f / l));
                    float sl = core::length(side);
                    if (sl < 1e-4f) return;
                    side = side * (1.0f / sl);
                    Sprite s;
                    // half-width = Size (RE s13 addendum 2: Xenos beam / trail VS, offset (2V - 1) Size cross(view, dir))
                    s.c[0] = a - side * w0; s.c[1] = b - side * w1;
                    s.c[2] = b + side * w1; s.c[3] = a + side * w0;
                    const float uv[4][2] = {{u0, 1}, {u1, 1}, {u1, 0}, {u0, 0}};
                    std::memcpy(s.uv, uv, sizeof(uv));
                    for (int k = 0; k < 4; ++k) s.color[k] = 0.5f * (col0[k] + col1[k]);
                    sp.push_back(s);
                };
                // M56: a connected camera-facing strip (UE3 beam / trail vertex pairs are shared between segments):
                // each point's side uses the averaged direction of its neighbouring segments, so kinks stay joined
                struct SP { core::Vec3 p; float w; float col[4]; float u; };
                // M61 texture layout (RE pass 5 s13, CONFIRMED CPU writes): U along the ribbon, V across (0 / 1 per vertex
                // pair; which world side is V 0 is in the Xenon vertex shader: UNKNOWN, convention kept). Beams: U = r
                // (TextureTile not applied). Trails: U by cumulative distance from the head, x TextureTile, clamped
                static constexpr bool kAlongV = false;
                float tile = 1.0f;
                auto strip = [&](const std::vector<SP>& v) {
                    const size_t n = v.size();
                    if (n < 2) return;
                    std::vector<core::Vec3> side(n);
                    for (size_t i = 0; i < n; ++i) {
                        if (fixedAxis) { side[i] = fixedSide; continue; }
                        core::Vec3 d = v[std::min(i + 1, n - 1)].p - v[i == 0 ? 0 : i - 1].p;
                        core::Vec3 sd = core::cross(core::normalize(camPos_ - v[i].p), d);
                        float sl = core::length(sd);
                        side[i] = sl > 1e-6f ? sd * (1.0f / sl) : (i > 0 ? side[i - 1] : core::Vec3{0, 0, 0});
                    }
                    for (size_t i = 0; i + 1 < n; ++i) {
                        if (core::length(side[i]) < 1e-6f || core::length(side[i + 1]) < 1e-6f) continue;
                        Sprite s;
                        // half-width = Size (the ribbon VS offsets each vertex pair by (2V - 1) x Size; RE s13 add. 2, HIGH)
                        const float s1 = L.sideScale[1], s0 = L.sideScale[0];   // V1 corners (c0, c1), V0 corners (c2, c3)
                        s.c[0] = v[i].p - side[i] * (v[i].w * s1); s.c[1] = v[i + 1].p - side[i + 1] * (v[i + 1].w * s1);
                        s.c[2] = v[i + 1].p + side[i + 1] * (v[i + 1].w * s0); s.c[3] = v[i].p + side[i] * (v[i].w * s0);
                        // along-ribbon coordinate (tiled) and across (0 / 1); kAlongV swaps them (M61)
                        float ua = v[i].u * tile, ub = v[i + 1].u * tile;
                        float uv[4][2] = {{ua, 1}, {ub, 1}, {ub, 0}, {ua, 0}};
                        if (kAlongV) for (auto& c : uv) std::swap(c[0], c[1]);
                        std::memcpy(s.uv, uv, sizeof(uv));
                        for (int k = 0; k < 4; ++k) s.color[k] = 0.5f * (v[i].col[k] + v[i + 1].col[k]);
                        sp.push_back(s);
                    }
                };
                if (in.hasTarget || L.typeData == 3) {
                  for (const FxParticle& qq : rt.parts) {
                    // beam: this particle's resolved ends (M60); a tracer trail spawned along a segment: the segment
                    const bool beamEnds = L.typeData == 3 && qq.beamInit;
                    const float src[3] = {in.T[0], in.T[1], in.T[2]};
                    core::Vec3 a = beamEnds ? ueToGltf(qq.beamSrc) : ueToGltf(src);
                    core::Vec3 b = beamEnds ? ueToGltf(qq.beamTgt) : ueToGltf(in.target);
                    // Beam fill (M55 / M56; RE pass 5 s9i, native 0x830298E8). Points along source -> target: noise points
                    // (N steps, NoiseTessellation cubic sub-steps; tangents HIGH), else InterpolationPoints steps.
                    // Width = size * TaperFactor(r) * TaperScale(r) (CONFIRMED). Noise offsets are rotated by the
                    // component's LocalToWorld (HIGH, call chain); sine waves by the beam frame (CONFIRMED).
                    const float lenM = core::length(b - a);
                    // component space -> glTF metres (in.R rows = the component's axes in UE world space)
                    auto compVec = [&](const float* o) {
                        float w3[3];
                        for (int c = 0; c < 3; ++c) w3[c] = o[0] * in.R[0][c] + o[1] * in.R[1][c] + o[2] * in.R[2][c];
                        return core::Vec3{w3[0] * 0.01f, w3[2] * 0.01f, w3[1] * 0.01f};
                    };
                    // sine frame (CONFIRMED): the quaternion rotating UE +Z onto dir = normalize(lerp(srcTangent x strength,
                    // tgtTangent x strength, r)); local X / Y across the beam, Z along it (rows = images of the axes)
                    auto frameAt = [&](float r, float M3[3][3]) {
                        float du[3];
                        for (int c = 0; c < 3; ++c)
                            du[c] = beamEnds ? qq.beamSrcT[c] + (qq.beamTgtT[c] - qq.beamSrcT[c]) * r
                                             : (in.target[c] - in.T[c]);
                        float dl = std::sqrt(du[0] * du[0] + du[1] * du[1] + du[2] * du[2]);
                        for (int i = 0; i < 3; ++i) for (int c = 0; c < 3; ++c) M3[i][c] = i == c ? 1.0f : 0.0f;
                        if (dl <= 1e-6f) return;
                        for (float& c : du) c /= dl;
                        float ax[3] = {-du[1], du[0], 0.0f};            // cross((0,0,1), dir)
                        float al = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1]);
                        float ang = std::acos(std::max(-1.0f, std::min(1.0f, du[2])));
                        if (al > 1e-6f) {
                            ax[0] /= al; ax[1] /= al;
                            float cs = std::cos(ang), sn = std::sin(ang), t1 = 1.0f - cs;
                            float R[3][3] = {{cs + ax[0] * ax[0] * t1, ax[0] * ax[1] * t1 - ax[2] * sn, ax[0] * ax[2] * t1 + ax[1] * sn},
                                             {ax[1] * ax[0] * t1 + ax[2] * sn, cs + ax[1] * ax[1] * t1, ax[1] * ax[2] * t1 - ax[0] * sn},
                                             {ax[2] * ax[0] * t1 - ax[1] * sn, ax[2] * ax[1] * t1 + ax[0] * sn, cs + ax[2] * ax[2] * t1}};
                            for (int i = 0; i < 3; ++i) for (int c = 0; c < 3; ++c) M3[i][c] = R[c][i];
                        } else if (du[2] < 0.0f) {
                            M3[1][1] = -1.0f; M3[2][2] = -1.0f;
                        }
                    };
                    // base path (RE 9i, HIGH stock CubicInterp): source -> target with the payload tangents x strengths
                    auto basePath = [&](float r) {
                        if (!beamEnds) return a + (b - a) * r;
                        core::Vec3 m0 = ueToGltf(qq.beamSrcT), m1 = ueToGltf(qq.beamTgtT);
                        float r2 = r * r, r3 = r2 * r;
                        return a * (2 * r3 - 3 * r2 + 1) + m0 * (r3 - 2 * r2 + r) + b * (-2 * r3 + 3 * r2) + m1 * (r3 - r2);
                    };
                    const bool shaped = L.typeData == 3 || L.taperMethod != 0 || L.noise.on || !L.sines.empty();
                    {
                        const FxParticle& q = qq;
                        float w = q.size[0] * sizeScale[0] * 0.01f;
                        if (shaped) {
                            struct BP { core::Vec3 p; float r; };
                            std::vector<BP> pts;
                            const bool noisy = L.noise.on && q.noiseCount > 0;
                            if (noisy) {
                                const int n = q.noiseCount, T = L.noise.tessellation;
                                // noise scale, once per beam (native, RE 9h): 1 unless bApplyNoiseScale with
                                // FrequencyDistance > 0, then NoiseScale(min(N, int(len / FD)) / N)
                                float noiseScale = 1.0f;
                                if (L.noise.applyScale && L.noise.frequencyDistance > 0.0f) {
                                    int st = std::min(n, (int)(lenM * 100.0f / L.noise.frequencyDistance));
                                    uint32_t g = 1; float s1[3]; L.noise.scale.eval((float)st / (float)n, g, s1); noiseScale = s1[0];
                                }
                                std::vector<core::Vec3> K((size_t)n + 1);
                                for (int i = 0; i <= n; ++i) {
                                    float r = (float)i / (float)n;
                                    core::Vec3 off{0, 0, 0};
                                    if (i > 0 && (i < n || L.noise.targetNoise)) {
                                        const float* o = &q.noiseCur[(size_t)i * 3];
                                        uint32_t g2 = 1; float rs[3];
                                        L.noise.rangeScale.eval(L.noise.nrEmitterTime ? rt.time : q.relTime, g2, rs);
                                        float ov[3] = {o[0] * noiseScale * rs[0], o[1] * noiseScale * rs[0], o[2] * noiseScale * rs[0]};
                                        off = compVec(ov);
                                    }
                                    K[(size_t)i] = basePath(r) + off;
                                }
                                // tangents (native fill, RE 9j CONFIRMED): T_i = normalize(N_{i+1} - N_{i-1}) *
                                // lerp(SourceStrength, TargetStrength, r_i) (UU); NoiseTension / NoiseTangentStrength
                                // have no effect on the curve
                                uint32_t g = 1; float ss[3], tsg[3];
                                L.sourceStrength.eval(q.relTime, g, ss);
                                L.targetStrength.eval(q.relTime, g, tsg);
                                auto tan = [&](int i) {
                                    core::Vec3 v = K[(size_t)std::min(i + 1, n)] - K[(size_t)std::max(i - 1, 0)];
                                    float vl = core::length(v);
                                    float r = (float)i / (float)n, st = (ss[0] + (tsg[0] - ss[0]) * r) * 0.01f;   // UU -> m
                                    return vl > 1e-6f ? v * (st / vl) : core::Vec3{0, 0, 0};
                                };
                                for (int i = 0; i < n; ++i) {
                                    core::Vec3 m0 = tan(i), m1 = tan(i + 1);
                                    for (int s = 0; s < T; ++s) {
                                        float t = (float)s / (float)T, t2 = t * t, t3 = t2 * t;
                                        core::Vec3 p = K[(size_t)i] * (2 * t3 - 3 * t2 + 1) + m0 * (t3 - 2 * t2 + t) +
                                                       K[(size_t)i + 1] * (-2 * t3 + 3 * t2) + m1 * (t3 - t2);
                                        pts.push_back({p, ((float)i + t) / (float)n});
                                    }
                                }
                                pts.push_back({K[(size_t)n], 1.0f});
                            } else {
                                const int n = std::max(std::max(L.interpPoints, 1), L.sines.empty() ? 1 : 32);
                                for (int i = 0; i <= n; ++i) { float r = (float)i / (float)n; pts.push_back({basePath(r), r}); }
                            }
                            if (!L.sines.empty()) {
                                // BeamSineWave (WFC addition; native 0x83029618, RE 9i CONFIRMED) at every point:
                                // offset = sum Direction * Amplitude * sin(2 pi ((Speed * t + d) / Period + PhaseOffset)),
                                // d = r * length (UU), t = particle age (s); rotated into the beam frame; faded by
                                // min(6r, 1) * min(6(1 - r), 1)
                                const float age = q.oneOverLife > 0.0f ? q.relTime / q.oneOverLife : rt.time;
                                for (BP& bp : pts) {
                                    float d = bp.r * lenM * 100.0f, o[3] = {0, 0, 0};
                                    for (const auto& s : L.sines) {
                                        float v = s.amp * std::sin(6.2831853f * ((s.speed * age + d) / std::max(s.period, 1e-3f) + s.phase));
                                        for (int c = 0; c < 3; ++c) o[c] += s.dir[c] * v;
                                    }
                                    float fade = std::min(6.0f * bp.r, 1.0f) * std::min(6.0f * (1.0f - bp.r), 1.0f);
                                    float sineM[3][3], w3[3];
                                    frameAt(bp.r, sineM);
                                    for (int c = 0; c < 3; ++c) w3[c] = (o[0] * sineM[0][c] + o[1] * sineM[1][c] + o[2] * sineM[2][c]) * fade;
                                    bp.p = bp.p + core::Vec3{w3[0] * 0.01f, w3[2] * 0.01f, w3[1] * 0.01f};
                                }
                            }
                            uint32_t trng = 0x9e3779b9u;
                            auto taperAt = [&](float r) {
                                if (L.taperMethod == 0) return 1.0f;
                                float f[3], s[3];
                                L.taperFactor.eval(r, trng, f);
                                L.taperScale.eval(r, trng, s);
                                return f[0] * s[0];
                            };
                            std::vector<SP> sv;
                            for (const BP& bp : pts)
                                sv.push_back({bp.p, w * taperAt(bp.r), {q.color[0], q.color[1], q.color[2], q.color[3]}, bp.r});
                            tile = 1.0f;     // the beam fill never applies TextureTile: U = r, 0 .. 1 (RE s13, CONFIRMED)
                            strip(sv);
                            continue;
                        }
                        static const bool lg = std::getenv("WFC_FXTEST") != nullptr;
                        static int n = 0;
                        if (lg && n++ % 60 == 0)
                            LOG_INFO("FXTEST beam %s w %.3f m size (%.1f %.1f %.1f) col (%.2f %.2f %.2f %.2f) len %.2f m", in.system.c_str(), w,
                                     q.size[0], q.size[1], q.size[2], q.color[0], q.color[1], q.color[2], q.color[3], core::length(b - a));
                        quad(a, b, w, w, q.color, q.color, 0.0f, 1.0f);
                    }
                  }
                } else if (L.typeData == 2 && L.particleTrail) {
                    // M63 Trail2 placed by LocationEmitter (RE pass 5 s14, CONFIRMED): one chain through this emitter's
                    // own particles in spawn order (each new particle becomes the head; no per-source trails; the head
                    // does not follow its source). Per-particle size / colour; U from the head by distance (s13).
                    std::vector<const FxParticle*> chain;
                    for (const FxParticle& q : rt.parts) chain.push_back(&q);
                    std::sort(chain.begin(), chain.end(), [](const FxParticle* x, const FxParticle* y) { return x->seq < y->seq; });
                    if (chain.size() >= 2) {
                        auto wpos = [&](const FxParticle& q) {
                            if (!L.localSpace) return ueToGltf(q.pos);
                            float w3[3];
                            for (int c = 0; c < 3; ++c) w3[c] = q.pos[0] * in.R[0][c] + q.pos[1] * in.R[1][c] + q.pos[2] * in.R[2][c] + in.T[c];
                            return ueToGltf(w3);
                        };
                        const size_t n = chain.size();
                        std::vector<core::Vec3> P(n);
                        for (size_t k = 0; k < n; ++k) P[k] = wpos(*chain[k]);
                        const int T = std::max(1, L.tessFactor);
                        auto tangent = [&](size_t k) {
                            core::Vec3 d = k == 0 ? P[1] - P[0] : k + 1 == n ? P[n - 1] - P[n - 2] : (P[k + 1] - P[k - 1]) * 0.5f;
                            return d * L.tessStrength;
                        };
                        std::vector<SP> sv;
                        for (size_t k = 0; k + 1 < n; ++k) {
                            const FxParticle& q0 = *chain[k];
                            const FxParticle& q1 = *chain[k + 1];
                            core::Vec3 m0 = tangent(k), m1 = tangent(k + 1);
                            for (int st = 0; st < T; ++st) {
                                float ta = (float)st / (float)T, t2 = ta * ta, t3 = t2 * ta;
                                core::Vec3 pt = st == 0 ? P[k] : P[k] * (2 * t3 - 3 * t2 + 1) + m0 * (t3 - 2 * t2 + ta) +
                                                                 P[k + 1] * (-2 * t3 + 3 * t2) + m1 * (t3 - t2);
                                float w = (q0.size[0] + (q1.size[0] - q0.size[0]) * ta) * sizeScale[0] * 0.01f;
                                SP v{pt, w, {0, 0, 0, 0}, L.tilePerParticle ? (float)(n - 1) - ((float)k + ta) : 0.0f};
                                for (int c = 0; c < 4; ++c) v.col[c] = q0.color[c] + (q1.color[c] - q0.color[c]) * ta;
                                sv.push_back(v);
                            }
                        }
                        const FxParticle& ql = *chain[n - 1];
                        sv.push_back({P[n - 1], ql.size[0] * sizeScale[0] * 0.01f, {ql.color[0], ql.color[1], ql.color[2], ql.color[3]}, 0.0f});
                        if (!L.tilePerParticle) {
                            float total = 0.0f;
                            for (size_t i = 0; i + 1 < sv.size(); ++i) total += core::length(sv[i + 1].p - sv[i].p);
                            float cum = 0.0f;
                            sv.back().u = 0.0f;
                            for (size_t i = sv.size() - 1; i-- > 0;) {
                                cum += core::length(sv[i + 1].p - sv[i].p);
                                sv[i].u = total > 1e-6f ? std::min(cum / total, 1.0f) : 0.0f;
                            }
                        }
                        tile = L.textureTile;
                        strip(sv);
                    }
                } else if (L.typeData == 2 && rt.trail.size() >= 2 && !rt.parts.empty()) {
                    const FxParticle& q = rt.parts.back();
                    float w = q.size[0] * sizeScale[0] * 0.01f;
                    float life = 0.25f;
                    for (const FxParticle& p : rt.parts) if (p.oneOverLife > 0.0f) life = std::max(life, 1.0f / p.oneOverLife);
                    const size_t n = rt.trail.size();
                    std::vector<core::Vec3> P(n);
                    for (size_t k = 0; k < n; ++k) { float p[3] = {rt.trail[k][0], rt.trail[k][1], rt.trail[k][2]}; P[k] = ueToGltf(p); }
                    // tessellation (native Trail2 vertex count: TessellationFactor steps per segment); stock UE3 Hermite
                    // between trail points, tangents scaled by TessellationStrength, size / colour lerped per step
                    const int T = std::max(1, L.tessFactor);
                    auto tangent = [&](size_t k) {
                        core::Vec3 d = k == 0 ? P[1] - P[0] : k + 1 == n ? P[n - 1] - P[n - 2] : (P[k + 1] - P[k - 1]) * 0.5f;
                        return d * L.tessStrength;
                    };
                    std::vector<SP> sv;
                    for (size_t k = 0; k + 1 < n; ++k) {
                        const auto& t0 = rt.trail[k];
                        const auto& t1 = rt.trail[k + 1];
                        float f0 = 1.0f - std::min(t0[3] / life, 1.0f), f1 = 1.0f - std::min(t1[3] / life, 1.0f);
                        core::Vec3 m0 = tangent(k), m1 = tangent(k + 1);
                        auto at = [&](float t) {
                            float t2 = t * t, t3 = t2 * t;
                            return P[k] * (2 * t3 - 3 * t2 + 1) + m0 * (t3 - 2 * t2 + t) + P[k + 1] * (-2 * t3 + 3 * t2) + m1 * (t3 - t2);
                        };
                        for (int s = 0; s < T; ++s) {
                            float ta = (float)s / (float)T, fa = f0 + (f1 - f0) * ta;
                            // the along coordinate starts (0) at the trail head, the newest point at the source: the
                            // authored trail textures fade from U 0 to U 1 (iontrail_01 149 -> 7, RingsTrail 72 -> 8 by
                            // quarter) [HIGH, data]. bTilePerParticle: the texture spans each segment; else the whole trail
                            const float fromHead = (float)(n - 1) - ((float)k + ta);
                            sv.push_back({s == 0 ? P[k] : at(ta), w * fa, {q.color[0], q.color[1], q.color[2], q.color[3] * fa},
                                          L.tilePerParticle ? fromHead : fromHead / (float)(n - 1)});
                        }
                    }
                    {
                        float fl = 1.0f - std::min(rt.trail[n - 1][3] / life, 1.0f);
                        sv.push_back({P[n - 1], w * fl, {q.color[0], q.color[1], q.color[2], q.color[3] * fl}, 0.0f});
                    }
                    // Trail2 fill (0x8301A700): U from 0 at the head (newest, HIGH), proportional to the cumulative distance:
                    // U += SegmentLength x TextureTile / TotalLength, clamped to [0, TextureTile]; bTilePerParticle keeps
                    // 0 .. Tile per segment (set above)
                    if (!L.tilePerParticle && sv.size() >= 2) {
                        float total = 0.0f;
                        for (size_t i = 0; i + 1 < sv.size(); ++i) total += core::length(sv[i + 1].p - sv[i].p);
                        float cum = 0.0f;
                        sv.back().u = 0.0f;
                        for (size_t i = sv.size() - 1; i-- > 0;) {
                            cum += core::length(sv[i + 1].p - sv[i].p);
                            sv[i].u = total > 1e-6f ? std::min(cum / total, 1.0f) : 0.0f;
                        }
                    }
                    tile = L.textureTile;
                    strip(sv);
                }
                static const bool ribbonLog = std::getenv("WFC_FXTEST") != nullptr;
                if (ribbonLog) {
                    static std::map<std::string, int> seen;
                    int& c = seen[in.system + "/" + sys.emitters[e].name];
                    if (c++ % 30 == 0)
                        LOG_INFO("FXTEST ribbon %s/%s type %d parts %zu trail %zu target %d quads %zu mat %s", in.system.c_str(),
                                 sys.emitters[e].name.c_str(), L.typeData, rt.parts.size(), rt.trail.size(), in.hasTarget ? 1 : 0, sp.size(),
                                 L.material.c_str());
                }
                if (!sp.empty()) {
                    if (rt.hasDyn) { std::copy(rt.dynParam, rt.dynParam + 4, dynParam_); }
                    if (!drawSprites(L.material.c_str(), sp.data(), sp.size(), camF * -1.0f) && ribbonLog)
                        LOG_INFO("FXTEST ribbon %s: drawSprites refused material %s", in.system.c_str(), L.material.c_str());
                    std::fill(dynParam_, dynParam_ + 4, 1.0f);
                    sprites += (int)sp.size();
                }
                continue;
            }
            if (!L.meshGltf.empty()) {                       // mesh emitter (TypeDataMesh)
                int meshId = fxMeshFor(L);
                if (meshId < 0) continue;
                for (const FxParticle& q : rt.parts) {
                    float rows[3][3];   // payload rotation (degrees): Roll = X, Pitch = Y, Yaw = Z
                    const float d2u = 65536.0f / 360.0f;
                    rotRows(q.meshRot[1] * d2u, q.meshRot[2] * d2u, q.meshRot[0] * d2u, rows);
                    float Rm[3][3];
                    for (int r = 0; r < 3; ++r)
                        for (int c = 0; c < 3; ++c) {
                            float s = q.size[r] * (L.localSpace ? sizeParam[r] : sizeScale[r]);   // local space: IR carries it
                            Rm[r][c] = L.localSpace
                                ? s * (rows[r][0] * IR[0][c] + rows[r][1] * IR[1][c] + rows[r][2] * IR[2][c])
                                : s * rows[r][c];
                        }
                    float wp[3]; worldPos(q.pos, wp);
                    core::Mat4 M = ueRowsToGltf(Rm, wp);
                    float col[4] = {q.color[0], q.color[1], q.color[2], q.color[3]};
                    drawFx(meshId, M, col);
                    ++meshes;
                }
            } else {                                          // sprites
                std::vector<Sprite> sp;
                sp.reserve(rt.parts.size());
                for (const FxParticle& q : rt.parts) {
                    float wp[3]; worldPos(q.pos, wp);
                    core::Vec3 c = ueToGltf(wp);
                    float w = q.size[0] * sizeScale[0] * 0.01f,
                          h = (L.rectangle || L.velocityAligned ? q.size[1] : q.size[0]) * sizeScale[1] * 0.01f;
                    core::Vec3 ax, ay;
                    bool aligned = false;
                    // Our quad: U grows along ax, V grows along -ay (c0 = c - hx - hy has UV (0, 1)).
                    const core::Vec3 toCam = core::normalize(camPos_ - c);
                    if (L.velocityAligned) {
                        // PSA_Velocity (RE pass 5 s15; CPU CONFIRMED, shader HIGH): D = normalize(Pos - OldPos), world;
                        // length Size.y along D with V = 0 the LEADING edge, width Size.x along cross(camera - particle, D);
                        // rotation ignored; no speed stretch. A stationary particle collapses (invisible), as the original.
                        float v[3] = {q.vel[0], q.vel[1], q.vel[2]}, vw[3];
                        if (L.localSpace) { for (int k = 0; k < 3; ++k) vw[k] = v[0] * IR[0][k] + v[1] * IR[1][k] + v[2] * IR[2][k]; }
                        else std::copy(v, v + 3, vw);
                        core::Vec3 dir{vw[0], vw[2], vw[1]};           // UE -> glTF axes
                        float dl = core::length(dir);
                        if (dl <= 1e-4f) continue;
                        ay = dir * (1.0f / dl);
                        core::Vec3 rx = core::cross(toCam, ay);
                        float rl = core::length(rx);
                        if (rl <= 1e-4f) continue;                      // moving straight along the view: zero width
                        ax = rx * (1.0f / rl);
                        aligned = true;
                    } else if (sys.emitters[e].lockAxis != 0) {
                        // M66 LockAxisFlags (RE s15 + addenda; CPU CONFIRMED, world formulas HIGH). Axes: the component
                        // rows when the LOD is local-space, else world X / Y / Z (CONFIRMED).
                        auto axis = [&](int k) {                        // UE axis k (0 X, 1 Y, 2 Z) -> glTF unit vector
                            float a[3] = {0, 0, 0};
                            if (L.localSpace) for (int c2 = 0; c2 < 3; ++c2) a[c2] = in.R[k][c2];
                            else a[k] = 1.0f;
                            core::Vec3 g = ueToGltf(a);
                            float gl = core::length(g);
                            return gl > 1e-6f ? g * (1.0f / gl) : core::Vec3{0, 0, 0};
                        };
                        const int la = sys.emitters[e].lockAxis;
                        if (la <= 6) {
                            // lock 1-6: Pos + (U - 1/2) Size.x (-A) + (V - 1/2) Size.y B; the quad fixed in the A-B plane,
                            // particle Rotation spins it in-plane. (A, B): X (Z, Y), Y (Z, -X), Z (X, -Y), -X (Z, -Y),
                            // -Y (Z, X), -Z (X, Y)
                            static const int aAx[7] = {0, 2, 2, 0, 2, 2, 0}, bAx[7] = {0, 1, 0, 1, 1, 0, 1};
                            static const float bSg[7] = {0, 1, -1, -1, -1, 1, 1};
                            core::Vec3 A = axis(aAx[la]), Bv = axis(bAx[la]) * bSg[la];
                            core::Vec3 R0 = A * -1.0f, U0 = Bv * -1.0f;   // U along -A; V along +B (ay = -B)
                            float cr = std::cos(q.rot), sr = std::sin(q.rot);
                            ax = R0 * cr + U0 * sr; ay = U0 * cr - R0 * sr;
                        } else {
                            // ROTATE_X/Y/Z: Pos + (U - 1/2) Size.x A + (V - 1/2) Size.y P, P = normalize(cross(C, A));
                            // ROTATE_*_U: U and V swapped with A negated. Rotation ignored.
                            const bool swapUV = la >= 10;
                            core::Vec3 A = axis((la - 7) % 3) * (swapUV ? -1.0f : 1.0f);
                            core::Vec3 Pv = core::cross(toCam, A);
                            float pl = core::length(Pv);
                            if (pl <= 1e-6f) continue;                  // viewed straight down the axis: zero width
                            Pv = Pv * (1.0f / pl);
                            if (!swapUV) { ax = A; ay = Pv * -1.0f; } else { ax = Pv; ay = A * -1.0f; }
                        }
                        aligned = true;
                    }
                    if (!aligned) {
                        float cr = std::cos(q.rot), sr = std::sin(q.rot);
                        ax = camR * cr + camU * sr; ay = camU * cr - camR * sr;
                    }
                    core::Vec3 hx = ax * (w * 0.5f), hy = ay * (h * 0.5f);
                    Sprite s;
                    s.c[0] = c - hx - hy; s.c[1] = c + hx - hy; s.c[2] = c + hx + hy; s.c[3] = c - hx + hy;
                    float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
                    if (L.subMethod != 0 && L.subH * L.subV > 1) {     // SubUV: the cell (and the blend cell) of the sheet
                        float du = 1.0f / (float)L.subH, dv = 1.0f / (float)L.subV;
                        float u0 = (float)(q.subImage % L.subH) * du, v0 = (float)(q.subImage / L.subH) * dv;
                        float u1 = (float)(q.subImage2 % L.subH) * du, v1 = (float)(q.subImage2 / L.subH) * dv;
                        float eu = du, ev = dv, eu2 = du, ev2 = dv;
                        if (q.subDirect) {               // M71: origin Pos / SubImages, extent Size / SubImages; UV2 = UV
                            u0 = u1 = q.subPos[0] * du; v0 = v1 = q.subPos[1] * dv;
                            eu = eu2 = q.subSize[0] * du; ev = ev2 = q.subSize[1] * dv;
                        }
                        for (int k = 0; k < 4; ++k) { s.uv2[k][0] = u1 + uv[k][0] * eu2; s.uv2[k][1] = v1 + uv[k][1] * ev2; }
                        for (auto& t : uv) { t[0] = u0 + t[0] * eu; t[1] = v0 + t[1] * ev; }
                        s.blend = q.subInterp;
                        if (std::getenv("WFC_FXTEST") && L.subMethod == 2) {
                            static int nb = 0;
                            if (nb++ < 3) LOG_INFO("FXTEST subuv %s/%s cell %d -> %d blend %.3f (%dx%d)", in.system.c_str(),
                                                   sys.emitters[e].name.c_str(), q.subImage, q.subImage2, q.subInterp, L.subH, L.subV);
                        }
                    }
                    std::memcpy(s.uv, uv, sizeof(uv));
                    std::copy(q.color, q.color + 4, s.color);
                    const FxEmitter& EM = sys.emitters[e];
                    if (EM.renderMode == 0) { sp.push_back(s); continue; }
                    // M68 Octagon / BestFit (RE pass 5 s17, CONFIRMED): the fill expands each polygon corner (cell-local
                    // 0..1) exactly like a quad corner and computes the UVs as (cell + corner) x cellSize, so polygon and
                    // texture stay aligned. Octagon = the unit square minus corner triangles of leg 1 - 1/sqrt2; BestFit =
                    // the last polygon with Time <= particle age (3..12 vertices; any other count draws nothing).
                    // Triangulated as a fan (0, k, k + 1): identical to the native index lists for these convex shapes.
                    static const float kOct[8][2] = {{0.2929f, 0}, {0.7071f, 0}, {1, 0.2929f}, {1, 0.7071f},
                                                     {0.7071f, 1}, {0.2929f, 1}, {0, 0.7071f}, {0, 0.2929f}};
                    std::vector<std::array<float, 2>> poly;
                    if (EM.renderMode == 1) { for (const auto& k8 : kOct) poly.push_back({k8[0], k8[1]}); }
                    else {
                        const float age = q.oneOverLife > 0.0f ? q.relTime / q.oneOverLife : 0.0f;
                        const FxEmitter::Polygon* pg = nullptr;
                        for (const auto& cand : EM.polygons) if (cand.time <= age) pg = &cand;
                        if (!pg && !EM.polygons.empty()) pg = &EM.polygons[0];
                        if (!pg) { sp.push_back(s); continue; }        // no polygon data: the quad
                        if (pg->count < 3 || pg->count > 12 || (int)pg->v.size() < pg->count) continue;   // not drawn
                        poly.assign(pg->v.begin(), pg->v.begin() + pg->count);
                    }
                    // quad corner (u, v) -> position c + hx (2u - 1) - hy (2v - 1); UV / second cell by the same corner
                    auto cornerPos = [&](float u, float v) { return c + hx * (2.0f * u - 1.0f) - hy * (2.0f * v - 1.0f); };
                    float cu0 = 0, cv0 = 0, cu1 = 0, cv1 = 0, du = 1, dv = 1;
                    if (L.subMethod != 0 && L.subH * L.subV > 1) {
                        du = 1.0f / (float)L.subH; dv = 1.0f / (float)L.subV;
                        cu0 = (float)(q.subImage % L.subH) * du; cv0 = (float)(q.subImage / L.subH) * dv;
                        cu1 = (float)(q.subImage2 % L.subH) * du; cv1 = (float)(q.subImage2 / L.subH) * dv;
                        if (q.subDirect) {               // M71 SubUVDirect
                            cu0 = cu1 = q.subPos[0] * du; cv0 = cv1 = q.subPos[1] * dv;
                            du *= q.subSize[0]; dv *= q.subSize[1];
                        }
                    }
                    auto setCorner = [&](Sprite& t, int k, const std::array<float, 2>& pc) {
                        t.c[k] = cornerPos(pc[0], pc[1]);
                        t.uv[k][0] = cu0 + pc[0] * du; t.uv[k][1] = cv0 + pc[1] * dv;
                        t.uv2[k][0] = cu1 + pc[0] * du; t.uv2[k][1] = cv1 + pc[1] * dv;
                    };
                    for (size_t k = 1; k + 1 < poly.size(); ++k) {
                        Sprite t = s;
                        setCorner(t, 0, poly[0]); setCorner(t, 1, poly[k]); setCorner(t, 2, poly[k + 1]); setCorner(t, 3, poly[k + 1]);
                        sp.push_back(t);
                    }
                }
                if (rt.hasDyn) { std::copy(rt.dynParam, rt.dynParam + 4, dynParam_); }
                drawSprites(L.material.c_str(), sp.data(), sp.size(), camF * -1.0f);
                std::fill(dynParam_, dynParam_ + 4, 1.0f);
                sprites += (int)sp.size();
            }
        }
    }
    statFxSprites_ += sprites; statFxMeshes_ += meshes;
    flushTranslucency();                               // all opaque drawn: the sorted translucency pass
    statFxMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace wfc

core::Mat4 ueActorMatrix(const core::Vec3& p, const core::Vec3& r) {
    const float d2u = 65536.0f / 360.0f;
    float R[3][3];
    wfc::rotRows(r.x * d2u, r.y * d2u, r.z * d2u, R);
    const float T[3] = {p.x, p.y, p.z};
    return wfc::ueRowsToGltf(R, T);
}
} // namespace render
