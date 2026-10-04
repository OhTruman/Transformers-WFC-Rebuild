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
            em.maxPeak = std::max(1, es[e]["max_peak_count"].asInt(1));
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
                    const assets::Json& d = ms[m]["dists"];
                    for (const auto& dv : d.obj) {
                        bool vec = dv.second["kind"].asString().rfind("vector", 0) == 0;
                        mod.dists[dv.first] = parseDist(dv.second, vec ? 3 : 1);
                    }
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
        id = upload(md);
    }
    fxMeshes_[L.meshGltf] = id;
    return id;
}

// ---- simulation (UE3 FParticleEmitterInstance order: kill/update, then spawn) ----
void Pipeline::tickMapFx(float dt) {
    if (dt <= 0.0f || std::getenv("WFC_NOMAPFX")) return;
    auto t0 = std::chrono::steady_clock::now();
    float camUE[3] = {camPos_.x * 100.0f, camPos_.z * 100.0f, camPos_.y * 100.0f};
    for (const PickupMeshRT& pm : pickupMeshes_)          // PHYS_Rotating only while available (Pickup state)
        if (pm.yawRate != 0.0f && !pickupMeshHidden_.count(pm.owner))
            pickupSpin_[pm.owner] = std::fmod(pickupSpin_[pm.owner] + pm.yawRate * dt, 65536.0f);
    for (FxInstance& in : fxInstances_) {
        const FxSystem& sys = fxSystems_[in.system];
        if (!in.requiredRule.empty() && !ruleActive(in.requiredRule)) continue;   // factory not in this mode
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
            // update / kill
            for (size_t p = 0; p < rt.parts.size();) {
                FxParticle& q = rt.parts[p];
                q.relTime += dt * q.oneOverLife;
                if (q.oneOverLife > 0.0f && q.relTime >= 1.0f) { rt.parts[p] = rt.parts.back(); rt.parts.pop_back(); continue; }
                for (int c = 0; c < 3; ++c) q.vel[c] = q.baseVel[c], q.size[c] = q.baseSize[c];
                for (int c = 0; c < 4; ++c) q.color[c] = q.baseColor[c];
                for (const FxModule& m : L.modules) {
                    if (m.flagA == 0) continue;               // disabled module (RE: flagA = bEnabled, HIGH)
                    if (m.name == "PMI_SizeMultiplyLife") {
                        float s[3]; m.dists.at("LifeMultiplier").eval(q.relTime, in.rng, s);
                        for (int c = 0; c < 3; ++c) q.size[c] *= s[c];
                    } else if (m.name == "PMI_ColorScaleOverLife") {
                        float cs[3], as[1];
                        auto ic = m.dists.find("ColorScaleOverLife"), ia = m.dists.find("AlphaScaleOverLife");
                        if (ic != m.dists.end()) { ic->second.eval(q.relTime, in.rng, cs); for (int c = 0; c < 3; ++c) q.color[c] *= cs[c]; }
                        if (ia != m.dists.end()) { ia->second.eval(q.relTime, in.rng, as); q.color[3] *= as[0]; }
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
            rt.burstFired.resize(L.bursts.size(), false);
            for (size_t b = 0; b < L.bursts.size(); ++b) {
                if (rt.burstFired[b] || efrac < L.bursts[b].time) continue;
                rt.burstFired[b] = true;
                int lo = L.bursts[b].countLow, hi = L.bursts[b].count;
                if (lo > 0 && hi > lo) { in.rng = in.rng * 1664525u + 1013904223u; spawn += lo + (int)((in.rng >> 8) % (uint32_t)(hi - lo + 1)); }
                else spawn += hi;
            }
            for (int s = 0; s < spawn && (int)rt.parts.size() < em.maxPeak; ++s) {
                FxParticle q;
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
                        m.dists.at("Lifetime").eval(efrac, in.rng, v1);
                        q.oneOverLife = v1[0] > 0.0f ? 1.0f / v1[0] : 0.0f;   // 0 = lives until killed
                    } else if (m.name == "PMI_Size") {
                        m.dists.at("StartSize").eval(efrac, in.rng, v3);
                        for (int c = 0; c < 3; ++c) q.baseSize[c] += v3[c];
                    } else if (m.name == "PMI_Color") {
                        m.dists.at("StartColor").eval(efrac, in.rng, v3);
                        m.dists.at("StartAlpha").eval(efrac, in.rng, v1);
                        for (int c = 0; c < 3; ++c) q.baseColor[c] = v3[c];
                        q.baseColor[3] = v1[0];
                    } else if (m.name == "PMI_Location") {
                        m.dists.at("StartLocation").eval(efrac, in.rng, v3);
                        toWorldDir(v3, w3);
                        for (int c = 0; c < 3; ++c) q.pos[c] += w3[c];
                    } else if (m.name == "PMI_LocationPrimitiveSphere") {   // [PARTIAL] sampling rule
                        float rad[1], vs[1], off[3], d[3];
                        m.dists.at("StartRadius").eval(efrac, in.rng, rad);
                        m.dists.at("VelocityScale").eval(efrac, in.rng, vs);
                        m.dists.at("StartLocation").eval(efrac, in.rng, off);
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
                        m.dists.at("StartVelocity").eval(efrac, in.rng, v3);
                        toWorldDir(v3, w3);
                        for (int c = 0; c < 3; ++c) q.baseVel[c] += w3[c];
                        m.dists.at("StartVelocityRadial").eval(efrac, in.rng, v1);
                        radial += v1[0];
                    } else if (m.name == "PMI_Rotation") {
                        m.dists.at("StartRotation").eval(efrac, in.rng, v1);
                        q.rot += v1[0] * 6.2831853f;                     // turns -> radians
                    } else if (m.name == "PMI_MeshRotation") {
                        m.dists.at("StartRotation").eval(efrac, in.rng, v3);
                        for (int c = 0; c < 3; ++c) q.meshRot[c] += v3[c] * 360.0f;   // turns -> degrees
                    } else if (m.name == "PMI_MeshRotationRate") {
                        m.dists.at("StartRotationRate").eval(efrac, in.rng, v3);
                        for (int c = 0; c < 3; ++c) q.meshRotRate[c] += v3[c] * 360.0f;
                    } else if (m.name == "PMI_ColorByParameter") {
                        // Color = BaseColor = the component's colour InstanceParameter (Steam_Sm_FX: 'SteamColor',
                        // the FName in the compiled LOD stream), else DefaultColor (CDO white; the stream carries
                        // FFFFFFFF). Single colour parameter per Streets instance.
                        float c4[4] = {1, 1, 1, 1};
                        auto it = in.colorParams.find("SteamColor");
                        if (it != in.colorParams.end()) std::copy(it->second.begin(), it->second.end(), c4);
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
                        float s3[3]; m.dists.at("LifeMultiplier").eval(0.0f, in.rng, s3);
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
    statFxMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
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
                    kothMesh_ = upload(md);
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
                p.stateMesh[k] = upload(md);
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
                    pm.meshId = upload(md);
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
            if (pickupSpin_.count(in.owner)) {               // the factory actor's spin carries its components
                float spin[3][3];
                rotRows(0.0f, pickupYaw(in.owner), 0.0f, spin);
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        IR[r][c] = in.R[r][0] * spin[0][c] + in.R[r][1] * spin[1][c] + in.R[r][2] * spin[2][c];
            }
            auto worldPos = [&](const float p[3], float o[3]) {
                if (!L.localSpace) { std::copy(p, p + 3, o); return; }
                for (int c = 0; c < 3; ++c) o[c] = in.T[c] + p[0] * IR[0][c] + p[1] * IR[1][c] + p[2] * IR[2][c];
            };
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
                            float s = q.size[r];
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
                    float w = q.size[0] * 0.01f, h = (L.rectangle ? q.size[1] : q.size[0]) * 0.01f;
                    float cr = std::cos(q.rot), sr = std::sin(q.rot);
                    core::Vec3 ax = camR * cr + camU * sr, ay = camU * cr - camR * sr;
                    core::Vec3 hx = ax * (w * 0.5f), hy = ay * (h * 0.5f);
                    Sprite s;
                    s.c[0] = c - hx - hy; s.c[1] = c + hx - hy; s.c[2] = c + hx + hy; s.c[3] = c - hx + hy;
                    const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
                    std::memcpy(s.uv, uv, sizeof(uv));
                    std::copy(q.color, q.color + 4, s.color);
                    sp.push_back(s);
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
} // namespace render
