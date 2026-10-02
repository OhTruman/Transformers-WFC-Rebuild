// WFC DirectLightEnv: the character light environment, from the native recovery
// (RE-Workspace notes/MILESTONE03_RENDERING_LIGHTVIS_SHADOW.md §2 + §3, ReverseEngineering b52dca9 + c95dadd):
//   ULightEnvironmentComponent::Tick 0x82DD19F0, GetUpdateMode 0x82DD17C8, GatherLights 0x82DF5068,
//   LightAffectsEnv 0x82CC2608, Point/Spot AffectsBounds 0x82DD3EF0 / 0x82E2CDF8, shadow-ray gate
//   0x82CC2700, FDirectLightEnv::Update 0x82CE4E78, BuildSamplePoints 0x82CCACE0, AddLight 0x82CE1918,
//   SmoothAndRank 0x82CDDE70, inserts 0x82CD4108 / 0x82CD4038 / 0x82CD42B8, BoundaryCrossfade 0x82CCB110,
//   BuildCompositeShadow 0x82CD4540, IntensityAt point 0x82DC1F60 / spot 0x82E2CFE0 / directional 0x82DBEAC0,
//   update queue 0x83833084 (enqueue 0x82CD2538, service 0x82CDD438: deadlines 0x82CD26F8, budget 0x82CD2798),
//   TickTransitions 0x82CD3840 (no-op for WFC pawns).
//
// CONFIRMED: everything above. HIGH: the environments' channels (robot = LightingChannelContainer default
// Dynamic + authored PlayerOnly; vehicle = default Dynamic), the queue service running before the frame is
// drawn, and the sphere-vs-cone relevance test shape (the report gives the inputs, not the arithmetic).
// PARTIAL / UNKNOWN (explicit): scene light-list insertion order (ties keep gather order; the original
// walks global lists then a hash grid) -> level light order; the main loop passes the 2 ms budget only
// when frame time remains -> always available here; how the render thread uses the proxy colour and the
// shadow factor (DynamicShadowLuminanceScale shader math UNKNOWN) -> direct lights keep the CONFIRMED
// per-pixel UberLight shading scaled by visibility x crossfade; the ambient term is the renderer's ambient
// cube (the original builds SH; Streets has no BeastLightEnvironmentVolume probes).
#include "render/gl/WfcPipeline.h"
#include "core/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace render {
namespace wfc {

namespace {
float lum(const core::Vec3& c) { return 0.3f * c.x + 0.59f * c.y + 0.11f * c.z; }
core::Vec3 toUE(const core::Vec3& g) { return {g.x * 100.0f, g.z * 100.0f, g.y * 100.0f}; }
// LightingChannelContainer bits (MSB-first bool packing)
constexpr uint32_t kChDynamic = 0x10000000u, kChCompositeDynamic = 0x08000000u, kChPlayerOnly = 0x00000100u;
constexpr uint32_t kChSpecial = 0x03FFFFFFu;
} // namespace

uint32_t Pipeline::envChannels(int form) const {
    return form == 0 ? (kChDynamic | kChPlayerOnly) : kChDynamic;
}

// IntensityAt: point = Brightness * max(0, 1 - (d/R)^2)^FalloffExponent; spot = point * cone^2 with
// cone = saturate((dot(axis, normalize(P - pos)) - cosO) / (cosI - cosO)); directional = Brightness.
float Pipeline::intensityAt(const Light& l, const core::Vec3& pGltf) const {
    if (l.type == 2) return l.brightness;
    float d = core::length(pGltf - l.pos);
    float r = d / std::max(l.radius, 1e-4f);
    float point = l.brightness * std::pow(std::max(0.0f, 1.0f - r * r), l.falloff);
    if (l.type != 1) return point;
    core::Vec3 dir = d > 1e-6f ? (pGltf - l.pos) * (1.0f / d) : l.dir;
    float cone = std::min(std::max((core::dot(core::normalize(l.dir), dir) - l.spotCosO) /
                                   std::max(l.spotCosI - l.spotCosO, 1e-6f), 0.0f), 1.0f);
    return point * cone * cone;
}

// LightAffectsEnv (0x82CC2608) for an env with channels `envCh` and bounds sphere (centre, radius).
bool Pipeline::lightAffectsEnv(const Light& l, uint32_t envCh, const core::Vec3& c, float radius) const {
    if (!l.enabled) return false;
    if (l.hasLightFunction && !l.castCompositeShadow) return false;
    uint32_t ch = l.chMask;
    if (ch & kChCompositeDynamic) ch = (ch & ~0x18000000u) | kChDynamic;
    if ((envCh & ch & 0x7FFFFFFFu) == 0) return false;          // must share a channel
    if ((ch & ~envCh & kChSpecial) != 0) return false;          // special channels must be on the env
    // bOnlyAffectSameAndSpecifiedLevels / bUseVolumes: not set on any Streets light (data) -> not needed
    if (l.type == 0 || l.type == 1) {                             // Point.AffectsBounds
        float dist = core::length(c - l.pos);
        if (dist > l.radiusOfInfluence + radius) return false;
        if (l.type == 1) {                                        // Spot: sphere vs clamped outer cone
            core::Vec3 axis = core::normalize(l.dir);
            core::Vec3 v = c - l.pos;
            float along = core::dot(v, axis);
            float perp = core::length(v - axis * along);
            float sinO = std::sin(l.spotOuterRad), cosO = std::cos(l.spotOuterRad);
            float dc = perp * cosO - along * sinO;               // signed distance to the cone surface
            if (dc > radius) return false;
        }
    }
    return true;
}

// Tick + GetUpdateMode + queue, once per drawn character per frame.
void Pipeline::tickDirectLightEnv(int form, const core::Vec3& boundsCenter, const core::Vec3& boundsExtent,
                                  const core::Vec3& actorPos) {
    DirectLightEnvState& st = dle_[form];
    float dtTick = st.lastTickTime < 0 ? 0.0f : std::max(time_ - st.lastTickTime, 0.0f);
    float notRendered = dtTick;
    st.velUE = (st.lastTickTime >= 0 && dtTick > 0) ? (toUE(actorPos) - toUE(st.lastActorPos)) * (1.0f / dtTick)
                                                    : core::Vec3{0, 0, 0};
    st.lastTickTime = time_;
    st.lastActorPos = actorPos;
    st.curCenter = boundsCenter;
    st.curExtent = boundsExtent;
    st.pendingDt += dtTick;
    if (!st.initialized) {
        // first update after attach (0x82DBEF90): owner rendered -> ForceNextUpdateTime 0.001 -> immediate full
        doDirectLightEnvUpdate(form, true);
        return;
    }
    const float T = 3.0f * 30.0f;                 // DetailScale[DetailMode 2] = 3 x UpdateDistanceThreshold 30
    float k = notRendered > 0.1f ? 100.0f : 1.0f;
    core::Vec3 dPos = toUE(boundsCenter) - st.lastUpdatePosUE;
    int mode;
    if (core::dot(dPos, dPos) > T * T * k) { st.wasMoving = true; mode = 1; }
    else if (st.wasMoving && std::fabs(st.velUE.x) < 1e-4f && std::fabs(st.velUE.y) < 1e-4f && std::fabs(st.velUE.z) < 1e-4f) {
        st.wasMoving = false; mode = 1;
    } else mode = st.known.empty() ? 0 : 2;
    auto queued = std::find_if(dleQueue_.begin(), dleQueue_.end(), [&](const DleQueueEntry& e) { return e.form == form; });
    if (mode == 1) {                              // enqueue (0x82CD2538)
        if (queued != dleQueue_.end()) { if (queued->mode == 2) queued->mode = 1; }
        else { dleQueue_.push_back(DleQueueEntry{form, 1, frameNo_ + 2}); ++st.queuedFull; }
    } else if (mode == 2) {
        if (queued == dleQueue_.end()) doDirectLightEnvUpdate(form, false);   // incremental now
    }
    // per-frame service (0x82CDD438): entries at their deadline (no budget), then FIFO within 2 ms
    auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < dleQueue_.size();) {
        if (dleQueue_[i].deadline <= frameNo_) {
            DleQueueEntry e = dleQueue_[i];
            dleQueue_.erase(dleQueue_.begin() + (long)i);
            doDirectLightEnvUpdate(e.form, e.mode == 1);
        } else ++i;
    }
    static const bool noBudget = std::getenv("WFC_DLE_NOBUDGET") != nullptr;   // test: deadline-only service
    while (!dleQueue_.empty() && !noBudget) {
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms >= 2.0) break;                     // budget 0.002 s
        DleQueueEntry e = dleQueue_.front();
        dleQueue_.erase(dleQueue_.begin());
        doDirectLightEnvUpdate(e.form, e.mode == 1);
    }
}

// FDirectLightEnv::Update: full = re-gather at the current bounds; incremental = re-evaluate known lights.
// Smoothing uses the dt accumulated since this env's last update.
void Pipeline::doDirectLightEnvUpdate(int form, bool full) {
    auto tUpd = std::chrono::steady_clock::now();
    DirectLightEnvState& st = dle_[form];
    const std::vector<core::Vec3>& offsets = form == 1 ? dleVehicleSamples_ : dleRobotSamples_;
    const float kTransition = 0.5f;          // EnvironmentTransitionTime
    const int kTotalLightCount = 2;
    float dt = st.pendingDt;
    st.pendingDt = 0.0f;
    ++statEnvCalls_;
    if (full) {
        st.initialized = true;
        st.lastUpdatePosUE = toUE(st.curCenter);
        st.originGltf = st.curCenter;
        st.extentGltf = st.curExtent;
        st.known.clear();
        float radius = core::length(st.curExtent);
        uint32_t ch = envChannels(form);
        for (size_t i = 0; i < lights_.size(); ++i)
            if (lightAffectsEnv(lights_[i], ch, st.curCenter, radius)) st.known.push_back((int)i);
        ++st.fullUpdates;
        st.lastFullFrame = frameNo_;
    }
    const core::Vec3 origin = st.originGltf;
    std::vector<core::Vec3> samples;              // BuildSamplePoints: origin + offset_i * extent (max 8)
    for (size_t i = 0; i < offsets.size() && i < 8; ++i)
        samples.push_back(origin + core::Vec3{offsets[i].x * st.extentGltf.x, offsets[i].y * st.extentGltf.y,
                                              offsets[i].z * st.extentGltf.z});
    std::vector<int> bakedLights;
    std::vector<float> bakedVis;
    auto tq = std::chrono::steady_clock::now();
    bool hit = lvv_.valid() && lvv_.query(toUE(origin), bakedLights, bakedVis);
    statLvvMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tq).count();
    ++statLvvQueries_;
    struct Ranked { int light; float score; float vis; core::Vec3 color; };
    std::vector<Ranked> direct, overflow, shadow, shadowOverflow;
    LightEnv env;
    for (auto& c : env.cube) c = {0, 0, 0};
    core::Vec3 lightsSH[6];               // LightsSH (+0x108): direct lights folded into the ambient cube
    for (auto& c : lightsSH) c = {0, 0, 0};
    const core::Vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int li : st.known) {
        const Light& l = lights_[(size_t)li];
        if (l.type == 3) {                    // SkyLight -> ambient upper / lower
            env.cube[2] = env.cube[2] + l.color;
            env.cube[3] = env.cube[3] + l.lowerColor;
            core::Vec3 side = (l.color + l.lowerColor) * 0.5f;
            for (int f : {0, 1, 4, 5}) env.cube[f] = env.cube[f] + side;
            continue;
        }
        float I, visTarget;
        if (lvv_.isBaked(li)) {               // baked: volume only, no rays (0 outside volumes)
            I = intensityAt(l, origin);
            visTarget = 0.0f;
            if (hit)
                for (size_t k2 = 0; k2 < bakedLights.size(); ++k2)
                    if (bakedLights[k2] == li) { visTarget = std::min(1.0f, bakedVis[k2]); break; }
        } else {                              // unbaked: N-of-M sample rays
            core::Vec3 mn = origin - st.extentGltf, mx = origin + st.extentGltf;
            bool inside = l.type != 2 && l.pos.x > mn.x && l.pos.x < mx.x && l.pos.y > mn.y && l.pos.y < mx.y &&
                          l.pos.z > mn.z && l.pos.z < mx.z;
            if (inside) { I = intensityAt(l, origin); visTarget = 1.0f; }
            else {
                float Imax = 0.0f;
                std::vector<float> Is;
                for (const core::Vec3& s : samples) { Is.push_back(intensityAt(l, s)); Imax = std::max(Imax, Is.back()); }
                int n = (int)std::lround(15.0f * std::sqrt(lum(l.colorByte * Imax)) + 0.5f);
                n = std::min(std::max(n, 1), std::max((int)samples.size(), 1));
                auto& ls = st.lights[li];
                if (n > 1 && ls.hasVis && ls.vis <= 0.0f && ls.stagger > 0) { --ls.stagger; n = 1; }
                int hits = 0;
                for (int s = 0; s < n && s < (int)samples.size(); ++s) {
                    if (Is[(size_t)s] >= 0.03f) {
                        // shadow-ray gate (0x82CC2700): only CastShadows && CastStaticShadows trace; the ray
                        // ends 15 UU short of the light
                        if (!vis_ || !l.castShadows || !l.castStaticShadows) { ++hits; continue; }
                        core::Vec3 from = samples[(size_t)s];
                        core::Vec3 tgt = l.type == 2 ? from - core::normalize(l.dir) * 300.0f : l.pos;
                        core::Vec3 dv = tgt - from;
                        float dl = core::length(dv);
                        core::Vec3 to = dl > 0.15f ? tgt - dv * (0.15f / dl) : from;
                        ++statVisCalls_;
                        if (!vis_(from, to)) ++hits;
                    } else if (Is[(size_t)s] > 0.01f) ++hits;
                }
                visTarget = (float)hits / (float)n;
                I = Imax;
            }
        }
        core::Vec3 color = l.colorByte * I;   // LightColor / 255 * IntensityAt
        auto& ls = st.lights[li];             // SmoothAndRank: linear, no overshoot, inside updates only
        float vis = visTarget;
        if (ls.hasVis) {
            float rate = std::min(std::max(core::length(st.velUE) * 0.002f, 0.2f), 1.0f) / kTransition;
            float step = rate * dt;
            vis = ls.vis + std::max(-step, std::min(step, visTarget - ls.vis));
        }
        ls.vis = vis; ls.hasVis = true;
        if (vis > 0.0f) {
            st.rng = st.rng * 1664525u + 1013904223u;          // stagger slot rand % 5
            ls.stagger = (int)((st.rng >> 16) % 5u);
            if (!l.hasLightFunction) {
                Ranked r{li, lum(color) * vis, vis, color};
                auto it = std::find_if(direct.begin(), direct.end(), [&](const Ranked& a) { return a.score < r.score; });
                direct.insert(it, r);
                if ((int)direct.size() > kTotalLightCount) {
                    Ranked tail = direct.back(); direct.pop_back();
                    auto it2 = std::find_if(overflow.begin(), overflow.end(), [&](const Ranked& a) { return a.score < tail.score; });
                    overflow.insert(it2, tail);
                }
            }
        }
        if (l.castCompositeShadow) {          // composite shadow: luminance WITHOUT visibility
            Ranked r{li, lum(color), vis, color};
            auto it = std::find_if(shadow.begin(), shadow.end(), [&](const Ranked& a) { return a.score < r.score; });
            shadow.insert(it, r);
            if (shadow.size() > 1) {          // DirectShadowLightCount = 1
                Ranked tail = shadow.back(); shadow.pop_back();
                auto it2 = std::find_if(shadowOverflow.begin(), shadowOverflow.end(), [&](const Ranked& a) { return a.score < tail.score; });
                shadowOverflow.insert(it2, tail);
            }
        }
    }
    // BoundaryCrossfade
    float f = 1.0f;
    int N = (int)direct.size();
    if (N == kTotalLightCount && !overflow.empty()) {
        float o = overflow[0].score, last = direct[(size_t)N - 1].score;
        f = N < 2 ? 1.0f - o / std::max(last, 1e-12f)
                  : (last - o) / std::max(direct[(size_t)N - 2].score - o, 1e-12f);
        f = std::min(std::max(f, 0.0f), 1.0f);
    }
    auto addLobe = [&](const Ranked& r, float scale) {        // overflow -> ambient (directional lobe)
        const Light& l = lights_[(size_t)r.light];
        core::Vec3 L = l.type == 2 ? core::normalize(l.dir) * -1.0f : core::normalize(l.pos - origin);
        float att = l.brightness > 0 ? intensityAt(l, origin) / l.brightness : 0.0f;
        for (int fc = 0; fc < 6; ++fc) {
            core::Vec3 add = l.color * (std::max(core::dot(L, axes[fc]), 0.0f) * att * r.vis * scale);
            env.cube[fc] = env.cube[fc] + add;
            lightsSH[fc] = lightsSH[fc] + add;
        }
    };
    for (const Ranked& r : overflow) addLobe(r, 1.0f);
    env.n = 0;
    for (int i = 0; i < N && i < 3; ++i) {
        const Ranked& r = direct[(size_t)i];
        const Light& l = lights_[(size_t)r.light];
        float scale = (i == N - 1) ? f : 1.0f;
        if (i == N - 1 && f < 1.0f) addLobe(r, 1.0f - f);
        int k2 = env.n++;
        env.light[k2] = r.light;
        env.pos[k2][0] = l.pos.x; env.pos[k2][1] = l.pos.y; env.pos[k2][2] = l.pos.z;
        env.pos[k2][3] = l.type == 2 ? 0.0f : 1.0f / l.radius;
        core::Vec3 d = l.type == 2 ? core::normalize(l.dir) * -1.0f : l.dir;
        env.dir[k2][0] = d.x; env.dir[k2][1] = d.y; env.dir[k2][2] = d.z; env.dir[k2][3] = l.type == 1 ? 1.0f : 0.0f;
        env.col[k2][0] = l.color.x; env.col[k2][1] = l.color.y; env.col[k2][2] = l.color.z; env.col[k2][3] = l.falloff;
        env.spot[k2][0] = l.cosOuter; env.spot[k2][1] = l.invConeRange; env.spot[k2][2] = 0; env.spot[k2][3] = r.vis * scale;
    }
    directLightAmbientContribution(env.cube, lightsSH, env.dlac);   // T = AmbientSH + LightsSH = env.cube
    st.env = env;
    st.crossfade = f;
    st.directCount = N;
    // BuildCompositeShadow
    st.shadowLight = -1; st.shadowStrength = 0.0f; st.shadowTop = -1; st.shadowCandidates = 0;
    if (!shadow.empty()) {
        const Ranked& S = shadow[0];
        st.shadowTop = S.light; st.shadowTopScore = S.score; st.shadowTopVis = S.vis;
        st.shadowNextScore = shadowOverflow.empty() ? -1.0f : shadowOverflow[0].score;
        float fade = 1.0f;
        if (!shadowOverflow.empty() && S.score - shadowOverflow[0].score < 0.2f)
            fade *= (S.score - shadowOverflow[0].score) / 0.2f;
        fade *= S.vis;                        // FadeShadowsWithLights = 1
        float factor = 1.0f - fade;           // lerp(1, ShadowLuminanceScale = 0, fade)
        if (std::fabs(factor - 1.0f) > 0.05f) { st.shadowLight = S.light; st.shadowStrength = fade; }
        st.shadowCandidates = (int)shadow.size() + (int)shadowOverflow.size();
    }
    statUpdateMs_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tUpd).count();
}

void directLightAmbientContribution(const core::Vec3 total[6], const core::Vec3 lights[6], float out[3]) {
    core::Vec3 sl{0, 0, 0}, st{0, 0, 0};   // CubeSum (0x82CED980): per channel, sum of the 6 faces
    for (int i = 0; i < 6; ++i) { sl = sl + lights[i]; st = st + total[i]; }
    out[0] = sl.x / (st.x + 0.001f);
    out[1] = sl.y / (st.y + 0.001f);
    out[2] = sl.z / (st.z + 0.001f);
}

} // namespace wfc
} // namespace render
