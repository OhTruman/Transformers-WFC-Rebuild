// Enhanced smoke (the Smoke row, user 2026-10-17; default Original = the authored sprites, byte-identical): stage 1 lights the
// smoke emitters' sprites by their surroundings (the sun with its shadow, the ambient light environment at the particle, and the
// local / transient lights where the mode has them) and gives them a flowing churn; stage 2 (Realistic) splats the big plumes into
// the X3 froxel volume. The smoke emitters come from Systems' template list (SMOKE_TEMPLATES.json, agents/systems e6d42c2b).
//
// Stage 1 model: the authored colour is what the smoke looks like under the map's typical light. Enhanced keeps that colour and
// scales it per channel by E / E_ref:
//   E     = the light at the smoke: the light environment of the drawn group's 1 m cell (the ambient cube's mean + its direct
//           lights with their visibility trace, each x 0.5 x a Henyey-Greenstein phase, g = 0.3) + the transient lights per
//           pixel (muzzle flashes, explosions, headlights: the same phase), all modes; Realistic's ambient is the RT probe
//   E_ref = the same quantity averaged over the map's Beast probe grid (the playable space), per mode
// so smoke in shade darkens, smoke by a fire or a muzzle flash takes its colour, and smoke backlit by the sun glows. The flow is a
// world-anchored swirl of the sprite's texture coordinates (faded to zero at the quad's edges), for the non-flipbook materials.
#include "render/gl/WfcPipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/Log.h"
#include "render/gl/GLExt.h"

namespace render {
namespace wfc {

using namespace glx;

// The smoke sprites' main (kFSMainUber's inputs and outputs; unlit translucent materials): the authored emissive x E / E_ref.
// The transient lights come through wfcTransientLights (injected before main for this variant, its N . L replaced by the phase).
const char* kFSMainSmoke = R"(
uniform vec3 uSmokeAmb;       // the group's light environment: the ambient cube's mean
uniform int uSmokeDirN;
uniform vec3 uSmokeDirL[3];   // its direct lights: towards the light
uniform vec3 uSmokeDirC[3];   // colour x attenuation x visibility
uniform vec3 uSmokeInvRef;    // 1 / E_ref per channel
uniform vec4 uSmokeFlow;      // x amplitude (UV), y spatial frequency (1/m), z speed, w = 1 on
uniform int uSmokeDebug;      // WFC_SMOKEDEBUG=1: the smoke pixels show E / E_ref x 0.5 (opaque), the lighting response itself
float wfcSmokePhase(vec3 L) { // Henyey-Greenstein, g = 0.3, x 4 pi (isotropic = 1): forward scattering towards the eye
    const float g = 0.3;
    float c = dot(-L, normalize(uCamPos - vPos));
    return (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * c, 1.5);
}
void main() {
    mat3 tbn; MatIn m = wfcBuildInput(tbn);
    if (uSmokeFlow.w > 0.5) {
        vec2 e2 = min(m.uv0, vec2(1.0) - m.uv0);
        float edge = smoothstep(0.0, 0.2, min(e2.x, e2.y));
        vec2 q = vPos.xz * uSmokeFlow.y + vec2(vPos.y * 0.5 * uSmokeFlow.y);
        float t = uTime * uSmokeFlow.z;
        vec2 f = vec2(sin(q.x * 1.7 + t + sin(q.y * 2.3 - t * 0.7)), cos(q.y * 1.9 - t * 0.8 + sin(q.x * 2.1 + t * 0.6)));
        m.uv0 += f * (uSmokeFlow.x * edge);
    }
    MatOut o; wfcMaterial(m, o);
    vec3 E = uSmokeAmb;
    for (int i = 0; i < 3; ++i) {
        if (i >= uSmokeDirN) break;
        E += uSmokeDirC[i] * (0.5 * wfcSmokePhase(uSmokeDirL[i]));
    }
    MatOut w = o; w.DiffuseColor = vec3(1.0); w.SpecularColor = vec3(0.0);
    if (uTLN > 0 || uTLAllN > 0) E += wfcTransientLights(normalize(uCamPos - vPos), w);
    vec3 c = o.EmissiveColor * (E * uSmokeInvRef);
    if (uSmokeDebug != 0) { if (o.Opacity < 0.02) discard; oColor = vec4(E * uSmokeInvRef * 0.5, 1.0); return; }
    wfcTranslucentOut(c, o.Opacity);
    c = wfcCanvasOut(wfcFog(c));
    oColor = vec4(c, clamp(o.Opacity, 0.0, 1.0));
}
)";

namespace {
std::string leafName(const std::string& m) {
    const size_t d = m.rfind('.');
    return d == std::string::npos ? m : m.substr(d + 1);
}
}  // namespace

// the translucent smoke sprite materials of Systems' list (additive steam / booster puffs glow by themselves: authored; the
// distortion clouds, sparks and embers the name rule caught are not smoke; SmokeThick_LIT_Mat is authored lit)
bool Pipeline::isSmokeMaterial(const std::string& material) {
    static const char* const kNames[] = {"SmokeCoolDepth_Mat", "Steam_Mat", "Steam_Shade_Mat", "Clouds_Anim_MAT", "SmokeThick_Mat",
                                         "SmokeThick_Light_Center_Mat", "Smoke_Lrg_Mat_INST", "Tracer_Smoke_MAT"};
    const std::string leaf = leafName(material);
    for (const char* n : kNames) if (leaf == n) return true;
    return false;
}

// the flow is for single-image sprites: a flipbook (Clouds_Anim_MAT, 4 x 4) would sample its neighbour cells
bool Pipeline::smokeFlowMaterial(const std::string& material) { return leafName(material) != "Clouds_Anim_MAT"; }

// the light at p (glTF metres) as the smoke main takes it: the cell environment (shared per 1 m cell, refreshed every 60
// frames, as the small dynamic meshes)
void Pipeline::smokeLight(const core::Vec3& p, core::Vec3& amb, int& n, core::Vec3 dirL[3], core::Vec3 dirC[3]) {
    auto q = [](float v) { return (uint64_t)(uint32_t)(int32_t)std::floor(v) & 0x1FFFFFull; };
    const uint64_t key = (q(p.x) << 42) | (q(p.y) << 21) | q(p.z);
    CellEnv& ce = cellEnv_[key];
    if (frameNo_ - ce.frame > 60) {
        computeEnv({std::floor(p.x) + 0.5f, std::floor(p.y) + 0.5f, std::floor(p.z) + 0.5f}, true, ce.env);
        ce.frame = frameNo_;
    }
    const LightEnv& env = ce.env;
    amb = {0, 0, 0};
    for (const core::Vec3& c : env.cube) amb = amb + c;
    amb = amb * (1.0f / 6.0f);
    n = 0;
    for (int i = 0; i < env.n && i < 3; ++i) {
        core::Vec3 L{env.dir[i][0], env.dir[i][1], env.dir[i][2]};
        float att = 1.0f;
        if (env.pos[i][3] > 0.0f) {                      // point / spot: towards the light from p, the UE3 falloff
            const core::Vec3 d = core::Vec3{env.pos[i][0], env.pos[i][1], env.pos[i][2]} - p;
            const float dist = core::length(d);
            if (dist < 1e-4f) continue;
            L = d * (1.0f / dist);
            const float r = dist * env.pos[i][3];
            att = std::pow(std::max(1.0f - r * r, 0.0f), env.col[i][3]);
            if (env.dir[i][3] > 0.5f) {
                const float s = std::min(std::max((core::dot(L, core::Vec3{-env.dir[i][0], -env.dir[i][1], -env.dir[i][2]}) - env.spot[i][0]) *
                                                  env.spot[i][1], 0.0f), 1.0f);
                att *= s * s;
            }
        }
        const float k = att * env.spot[i][3];
        if (k <= 0.0f) continue;
        dirL[n] = L; dirC[n] = core::Vec3{env.col[i][0], env.col[i][1], env.col[i][2]} * k;
        ++n;
    }
}

// per drawn smoke group (drawSubs, after bindCommon): its light environment, E_ref and the flow
void Pipeline::bindSmoke(const Program& P) {
    if (smokeRef_.x < 0.0f || smokeRefMode_ != rtMode_) {   // E_ref: once per map and RT mode
        // the ambient over the level itself (first match sheet: with the direct lights in it, smoke sat at E / E_ref 0.4-0.6, darker
        // than authored nearly everywhere): up to 128 world sub-mesh centres (floors, walls, corridors), 1 m up - the places smoke
        // drifts through, on every map (Streets' Beast probe file has no volumes); the same light the smoke main sees
        std::vector<core::Vec3> pts;
        for (const GpuMesh& g : meshes_)
            if (g.world)
                for (const Sub& s : g.subs) pts.push_back((s.bmin + s.bmax) * 0.5f + core::Vec3{0, 1, 0});
        const size_t step = std::max<size_t>(1, pts.size() / 128);
        core::Vec3 sum{0, 0, 0};
        int cnt = 0;
        for (size_t k = 0; k < pts.size(); k += step) {
            core::Vec3 amb, dl[3], dc[3]; int n = 0;
            smokeLight(pts[k], amb, n, dl, dc);
            core::Vec3 e = amb;
            // (the ambient alone: smoke in ordinary shade keeps its authored look; sun / lamps / flashes add to it)
            sum = sum + e; ++cnt;
        }
        smokeRef_ = cnt > 0 ? sum * (1.0f / (float)cnt) : core::Vec3{0, 0, 0};
        smokeRefMode_ = rtMode_;
        LOG_INFO("smoke (Enhanced): reference irradiance (%.3f %.3f %.3f) over %d level points (RT mode %d)", smokeRef_.x,
                 smokeRef_.y, smokeRef_.z, cnt, rtMode_);
    }
    core::Vec3 amb, dl[3], dc[3]; int n = 0;
    smokeLight(smokeCenter_, amb, n, dl, dc);
    core::Vec3 inv{1, 1, 1};
    if (smokeRef_.x > 1e-4f && smokeRef_.y > 1e-4f && smokeRef_.z > 1e-4f) inv = {1.0f / smokeRef_.x, 1.0f / smokeRef_.y, 1.0f / smokeRef_.z};
    else {                                               // (no level points: this group's own ambient is the reference)
        core::Vec3 e = amb;
        inv = {1.0f / std::max(e.x, 1e-4f), 1.0f / std::max(e.y, 1e-4f), 1.0f / std::max(e.z, 1e-4f)};
    }
    float L3[9] = {}, C3[9] = {};
    for (int i = 0; i < n; ++i) { L3[i * 3] = dl[i].x; L3[i * 3 + 1] = dl[i].y; L3[i * 3 + 2] = dl[i].z; C3[i * 3] = dc[i].x; C3[i * 3 + 1] = dc[i].y; C3[i * 3 + 2] = dc[i].z; }
    Uniform3f(cachedUniformLocation(P.id, "uSmokeAmb"), amb.x, amb.y, amb.z);
    Uniform1i(cachedUniformLocation(P.id, "uSmokeDirN"), n);
    Uniform3fv(cachedUniformLocation(P.id, "uSmokeDirL"), 3, L3);
    Uniform3fv(cachedUniformLocation(P.id, "uSmokeDirC"), 3, C3);
    Uniform3f(cachedUniformLocation(P.id, "uSmokeInvRef"), inv.x, inv.y, inv.z);
    static const float flowAmp = std::getenv("WFC_SMOKEFLOW") ? (float)std::atof(std::getenv("WFC_SMOKEFLOW")) : 0.035f;
    Uniform4f(cachedUniformLocation(P.id, "uSmokeFlow"), flowAmp, 0.45f, 0.6f, P.smokeFlow && flowAmp > 0.0f ? 1.0f : 0.0f);
    static const int debug = std::getenv("WFC_SMOKEDEBUG") ? 1 : 0;
    Uniform1i(cachedUniformLocation(P.id, "uSmokeDebug"), debug);
}

// the Smoke row; WFC_SMOKE=0|1 overrides it (harness runs)
int Pipeline::smokeMode() const {
    static const int env = std::getenv("WFC_SMOKE") ? std::min(std::max(std::atoi(std::getenv("WFC_SMOKE")), 0), 1) : -1;
    return env >= 0 ? env : smokeMode_;
}

}  // namespace wfc
}  // namespace render
