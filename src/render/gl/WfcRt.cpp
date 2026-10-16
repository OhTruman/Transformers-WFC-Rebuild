// Clean-room reconstruction — M7 stage 0 groundwork, the GL side (PC ADAPTATION; OFF by default). WFC_RT_ROUNDTRIP=1:
// after the world depth prepass (the depth RT shadows / AO will read), the scene depth goes to the D3D12 bridge as R32F
// (render/d3d12/D3D12RtBridge.h), the stand-in D3D12 job runs and the world shading waits for its mask on the GPU - the
// mid-frame round trip's cost, measured before any ray-traced pass exists (Systems' M7 plan: architecture A).
// WFC_RT_DEPTHCHECK=1 (implies the round trip): the static world as the depth prepass draws it (opaque MDI subs, no
// vertex-lightmapped runs) goes to D3D12 as BLAS / TLAS at load, and the job is one primary ray per pixel compared with
// this frame's prepass depth (stage 0's exit check; D3D12RtBridge logs the agreement per 300 frames).
#include "render/gl/WfcPipeline.h"
#include "render/d3d12/D3D12RtBridge.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <cstring>
#include <map>

namespace render {
namespace wfc {
using namespace glx;

GLuint compileShader(GLenum type, const std::string& src, const std::string& tag);   // WfcPipeline.cpp
GLuint linkProgram(GLuint vs, GLuint fs, const std::string& tag);

namespace {
const char* kRtVS = R"(#version 430
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }
)";
const char* kRtDepthFS = R"(#version 430
uniform sampler2D uDepth;
out float oDepth;
void main() { oDepth = texelFetch(uDepth, ivec2(gl_FragCoord.xy), 0).r; }
)";
// stage 1 composite on world pixels (multiplied into the scene colour): the characters' RT shadow with the sun's
// ModShadowColor (as the original projectors darken the world) and their contact AO; dynamic pixels and the sky stay 1
const char* kRtCompositeFS = R"(#version 430
uniform sampler2D uVis;     // denoised sun visibility (R8)
uniform sampler2D uSao;     // raw sun, AO, dynamic pixel
uniform vec3 uModShadow;
uniform float uAoStrength;
// (item 2B) the characters' lamp shadows on the world: per shadow light its visibility from the characters (uLVis),
// its direct irradiance here (calibrated colour x UE3 falloff x cone^2 x N.L x 0.66, the flat-normal decode of the
// directional basis) against the pixel's decoded lightmap irradiance (uLum, written by the world pass)
uniform int uLB;
uniform int uLVView;   // (diagnostics, WFC_RT_LVISVIEW=1: the per-light visibility, min of the 4 slots, on every pixel)
uniform sampler2D uLVis;
uniform sampler2D uLum;
uniform sampler2D uDepth;
uniform mat4 uInvVP;
uniform vec2 uSize;
uniform vec4 uSLPos[4];   // xyz, radius
uniform vec4 uSLCol[4];   // calibrated rgb, w: falloff exponent (< 0: linear)
uniform vec4 uSLDir[4];   // forward, w: cos(outer) (-2 = point)
uniform vec4 uSLCosI;
uniform int uTLN; uniform vec4 uTLPos[8]; uniform vec4 uTLCol[8];   // (item 4) the transient lights (xyz, 1 / radius; rgb, falloff)
vec3 wfcWorldAt(ivec2 q) { vec2 ndc = (vec2(q) + 0.5) / uSize * 2.0 - 1.0; float d = texelFetch(uDepth, q, 0).r;
    vec4 w = uInvVP * vec4(ndc, d * 2.0 - 1.0, 1.0); return w.xyz / w.w; }
out vec4 oFactor;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 s = texelFetch(uSao, p, 0);
    if (uLVView != 0) { vec4 v = mod(floor(texelFetch(uLVis, p, 0) * 255.0 + 0.5), 2.0); float m = min(min(v.x, v.y), min(v.z, v.w)); oFactor = vec4(m, m, s.z > 0.5 ? 1.0 : m, 1.0); return; }
    if (s.z > 0.5) { oFactor = vec4(1.0); return; }
    float vis = texelFetch(uVis, p, 0).r;
    vis = vis > 0.985 ? 1.0 : vis;                  // (the 8-bit denoised visibility tops out just under 1: unlit pixels exact)
    float ao = s.w > 0.985 ? 1.0 : s.w;          // (the filtered AO)
    vec3 f = mix(uModShadow, vec3(1.0), vis) * mix(1.0, ao, uAoStrength);
    if (uLB > 0) {
        vec4 lum = texelFetch(uLum, p, 0);
        float lw = dot(lum.rgb, vec3(0.2126, 0.7152, 0.0722));
        vec4 lv = mod(floor(texelFetch(uLVis, p, 0) * 255.0 + 0.5), 2.0);   // (bit 0: the shadow lights)
        if (lum.a > 0.5 && lw > 1e-4 && min(min(lv.x, lv.y), min(lv.z, lv.w)) < 0.5) {
            vec3 P = wfcWorldAt(p);
            ivec2 px1 = ivec2(min(p.x + 1, int(uSize.x) - 1), p.y), py1 = ivec2(p.x, min(p.y + 1, int(uSize.y) - 1));
            vec3 N = normalize(cross(wfcWorldAt(px1) - P, wfcWorldAt(py1) - P));
            vec3 E = vec3(0.0);
            for (int k = 0; k < 4; ++k) {
                if (k >= uLB || lv[k] > 0.5) continue;
                vec3 d = uSLPos[k].xyz - P; float dist = length(d);
                if (dist >= uSLPos[k].w || dist < 1e-3) continue;
                vec3 l = d / dist;
                float r = dist / uSLPos[k].w;
                float F = uSLCol[k].w < 0.0 ? max(0.0, 1.0 - r) : pow(max(0.0, 1.0 - r * r), uSLCol[k].w);
                if (uSLDir[k].w > -1.5) { float cn = clamp((dot(-l, uSLDir[k].xyz) - uSLDir[k].w) / max(uSLCosI[k] - uSLDir[k].w, 1e-4), 0.0, 1.0); F *= cn * cn; }
                E += uSLCol[k].rgb * F * abs(dot(N, l)) * 0.66;
            }
            float share = clamp(dot(E, vec3(0.2126, 0.7152, 0.0722)) / lw, 0.0, 1.0);
            f *= 1.0 - share;
        }
    }
    if (uTLN > 0) {   // (item 4) the transient lights blocked at this world pixel (LightVis bit 1, lights 0..3)
        vec4 lum = texelFetch(uLum, p, 0);
        vec4 tv = floor(mod(floor(texelFetch(uLVis, p, 0) * 255.0 + 0.5), 4.0) / 2.0);
        if (lum.a > 0.5 && min(min(tv.x, tv.y), min(tv.z, tv.w)) < 0.5) {
            vec3 P = wfcWorldAt(p);
            ivec2 px1 = ivec2(min(p.x + 1, int(uSize.x) - 1), p.y), py1 = ivec2(p.x, min(p.y + 1, int(uSize.y) - 1));
            vec3 N = normalize(cross(wfcWorldAt(px1) - P, wfcWorldAt(py1) - P));
            float eAll = 0.0, eBlocked = 0.0;
            for (int k = 0; k < 8; ++k) {
                if (k >= uTLN) break;
                vec3 d = uTLPos[k].xyz - P, dn = d * uTLPos[k].w;
                float att = pow(clamp(1.0 - dot(dn, dn), 0.0, 1.0), uTLCol[k].w);
                float e = dot(uTLCol[k].rgb, vec3(0.2126, 0.7152, 0.0722)) * att * abs(dot(N, normalize(d)));
                eAll += e;
                if (k < 4 && tv[k] < 0.5) eBlocked += e;
            }
            float lw = dot(lum.rgb, vec3(0.2126, 0.7152, 0.0722));
            if (eBlocked > 0.0) f *= clamp((lw + eAll - eBlocked) / max(lw + eAll, 1e-4), 0.0, 1.0);
        }
    }
    oFactor = vec4(f, 1.0);
}
)";
// R1 sun swap (Realistic + V_bake): dual-source - colour = colour x factor + add. World pixels with a sun share (the world
// pass's second target, alpha = V_bake >= 0): the RT AO on everything, the baked sun (V_bake x share) swapped for the ray-
// traced one (vis x share: world + characters, soft) - add = share x (vis - ao x V_bake). Other world pixels (vertex-lit,
// unlit): the AO only (their visibility is the full one now; the bake already shadows them from the world).
const char* kRtCompositeR1FS = R"(#version 430
uniform sampler2D uVis;
uniform sampler2D uSao;
uniform sampler2D uSun;
uniform vec3 uModShadow;
uniform float uAoStrength;
uniform int uR1View;        // WFC_RT_R1VIEW=1 (diagnostics): red = RT lights more than the bake, green = less, blue = the share
layout(location = 0, index = 0) out vec4 oAdd;
layout(location = 0, index = 1) out vec4 oFactor;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 s = texelFetch(uSao, p, 0);
    oAdd = vec4(0.0);
    if (s.z > 0.5) { oFactor = vec4(1.0); return; }
    float vis = texelFetch(uVis, p, 0).r;
    vis = vis > 0.985 ? 1.0 : vis;
    float ao = s.w > 0.985 ? 1.0 : s.w;
    float f = mix(1.0, ao, uAoStrength);
    vec4 sun = texelFetch(uSun, p, 0);
    if (sun.a >= 0.0) oAdd = vec4(sun.rgb * (vis - f * sun.a), 0.0);
    oFactor = vec4(vec3(f), 1.0);
    if (uR1View != 0) {
        oFactor = vec4(0.0, 0.0, 0.0, 1.0);
        oAdd = sun.a >= 0.0 ? vec4(max(vis - sun.a, 0.0), max(sun.a - vis, 0.0), min(dot(sun.rgb, vec3(0.3333)), 1.0), 0.0) : vec4(0.0);
    }
}
)";
bool depthCheckOn() { static const bool on = std::getenv("WFC_RT_DEPTHCHECK") != nullptr; return on; }
// stage 1 (measurement): sun shadow + AO rays per pixel (WFC_RT_AORAYS, default 2; WFC_RT_AORADIUS metres, default 1)
bool shadowAoOn() { static const bool on = std::getenv("WFC_RT_SHADOWAO") != nullptr; return on; }
bool reflectOn() { static const bool on = std::getenv("WFC_RT_REFLECT") != nullptr; return on; }
// stage 2 step 2b: the maps' compiled material reflection shaders (tools/render/matc_hlsl.py --compile output,
// <dir>/<map>/reflect_mat.cso + materials_rt.txt); unset = the proxy shading
const char* matDir() { static const char* d = std::getenv("WFC_RT_MATDIR"); return d; }

bool invert(const float* m, float* out) {             // general 4x4 inverse (column-major in and out), in double
    double a[16], inv[16];
    for (int i = 0; i < 16; ++i) a[i] = m[i];
    inv[0] = a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
    inv[4] = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
    inv[8] = a[4]*a[9]*a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
    inv[12] = -a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
    inv[1] = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
    inv[5] = a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
    inv[9] = -a[0]*a[9]*a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
    inv[13] = a[0]*a[9]*a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
    inv[2] = a[1]*a[6]*a[15] - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7] - a[13]*a[3]*a[6];
    inv[6] = -a[0]*a[6]*a[15] + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7] + a[12]*a[3]*a[6];
    inv[10] = a[0]*a[5]*a[15] - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7] - a[12]*a[3]*a[5];
    inv[14] = -a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6] + a[12]*a[2]*a[5];
    inv[3] = -a[1]*a[6]*a[11] + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7] + a[9]*a[3]*a[6];
    inv[7] = a[0]*a[6]*a[11] - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7] - a[8]*a[3]*a[6];
    inv[11] = -a[0]*a[5]*a[11] + a[0]*a[7]*a[9] + a[4]*a[1]*a[11] - a[4]*a[3]*a[9] - a[8]*a[1]*a[7] + a[8]*a[3]*a[5];
    inv[15] = a[0]*a[5]*a[10] - a[0]*a[6]*a[9] - a[4]*a[1]*a[10] + a[4]*a[2]*a[9] + a[8]*a[1]*a[6] - a[8]*a[2]*a[5];
    const double det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (det == 0.0) return false;
    for (int i = 0; i < 16; ++i) out[i] = (float)(inv[i] / det);
    return true;
}
// column-major 4 x 4: c = a * b
void mul4(const float* a, const float* b, float* c) {
    for (int col = 0; col < 4; ++col)
        for (int r = 0; r < 4; ++r) {
            double v = 0.0;
            for (int k = 0; k < 4; ++k) v += (double)a[k * 4 + r] * (double)b[col * 4 + k];
            c[col * 4 + r] = (float)v;
        }
}
// FidelityFX ndc (x, 1 - 2v, depth 0 .. 1) -> GL ndc (x, -y, 2 depth - 1) and back (the shared textures keep GL's rows,
// so v runs up GL's image: y flips; depth is the window depth GL wrote)
const float kFfxToGl[16] = {1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 2, 0, 0, 0, -1, 1};
const float kGlToFfx[16] = {1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0.5f, 1};
}  // namespace

// the world mesh's static opaque triangles (every sub with an opaque program: MDI-drawn or not, lightmapped or vertex-lit;
// sky domes and CastShadow-false subs left out, actor-placed nodes kept as before; alpha-tested subs are the masked set), from its GL buffers
// (14 floats per vertex, positions first; base vertex 0), once per map. (Before 2026-10-13 only the depth prepass's set -
// MDI-drawn, no vertex-lit runs - went in: half of Gorge's occluders, 634 k of 1.2 M triangles, were missing, so sun rays
// escaped through roofs and rock that the bake - and Systems' V_bake trace - had as solid.)
void Pipeline::rtBuildWorld(int meshId) {
    if (!(depthCheckOn() || rtInfra_) || rtFailed_ || meshId < 0 || !GetBufferSubData) return;
    const bool keepHit = reflectOn() || rtInfra_;          // (reflection hits: Realistic)
    const GpuMesh& g = meshes_[(size_t)meshId];
    size_t nIdx = 0;                                     // (sizes from the subs: upload() bounds-checked every range)
    for (const Sub& s : g.subs) nIdx = std::max(nIdx, (size_t)s.first + s.count);
    std::vector<uint32_t> idx(nIdx);
    BindVertexArray(g.vao);                              // (the element buffer binding is VAO state)
    if (nIdx) GetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)(nIdx * 4), idx.data());
    BindVertexArray(0);
    size_t nVert = 0;
    for (uint32_t i : idx) nVert = std::max(nVert, (size_t)i + 1);
    std::vector<float> v(nVert * 14);
    BindBuffer(GL_ARRAY_BUFFER, g.vbo);
    if (nVert) GetBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(v.size() * 4), v.data());
    BindBuffer(GL_ARRAY_BUFFER, 0);
    giRasterMesh(meshId, idx, v);                        // (R4: this mesh's lightmap texels, while its buffers are here)
    RtStaticWorld W;
    RtHitKeep keep;
    W.pos.resize(v.size() / 14 * 3);
    for (size_t i = 0, n = v.size() / 14; i < n; ++i) std::memcpy(&W.pos[i * 3], &v[i * 14], 12);
    size_t subs = 0, skipped = 0, noShadow = 0, vlmSubs = 0, nonMdiSubs = 0, actorNodes = 0;
    std::vector<std::pair<float, std::string>> largest;      // (diagnostics) the biggest subs going into the TLAS
    for (const Sub& s : g.subs) {
        if (s.prog < 0 || (size_t)s.first + s.count > idx.size()) continue;
        const Program& P = progs_[(size_t)s.prog];
        const Program& M = P.mdiProg >= 0 ? progs_[(size_t)P.mdiProg] : P;
        if (M.blend != 0 || P.blend != 0) continue;
        if (s.noShadow) { ++noShadow; continue; }       // (authored CastShadow false: not an occluder)
        // actor-placed nodes (movers, authored-hidden and Gameplay-toggled props: the only subs carrying an actor name) are
        // not static: the TLAS would keep them at their load pose while GL hides or moves them. Molten's InterpActor_11529
        // (PROP_UND_RepairNodeB, 10 x 10 m, 7-25 cm above the central platform, hidden in TDM) blacked the floor out
        // through R3's world AO and R1's sun visibility - the user's "shadows that do not line up" (2026-10-13).
        if (!s.actor.empty()) { ++actorNodes; continue; }
        const core::Vec3 ext = s.bmax - s.bmin;
        const float size = std::max(ext.x, std::max(ext.y, ext.z));
        // sky domes / boxes are not occluders (every sun ray hit Rust's 4.5 km Spacedome): over 2 km, or named so
        std::string lower = s.matName;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (size > 2000.0f || lower.find("dome") != std::string::npos || lower.find("sky") != std::string::npos) { ++skipped; continue; }
        // (diagnostics) WFC_RT_EXCLUDE=<substr;substr>: material names left out of the TLAS
        static const std::string exclude = [] { const char* e = std::getenv("WFC_RT_EXCLUDE"); return std::string(e ? e : ""); }();
        if (!exclude.empty()) {
            bool hit = false;
            for (size_t a = 0; a <= exclude.size() && !hit;) {
                const size_t b = exclude.find(';', a);
                const std::string tok = exclude.substr(a, b == std::string::npos ? std::string::npos : b - a);
                hit = !tok.empty() && s.matName.find(tok) != std::string::npos;
                if (b == std::string::npos) break;
                a = b + 1;
            }
            if (hit) { ++skipped; continue; }
        }
        largest.push_back({size, s.matName + (M.lit ? "" : " (unlit)")});
        // (diagnostics) WFC_RT_CENSUS=x,y,z,r: every sub going into the TLAS whose box comes within r of the point
        static const float* census = [] {
            static float c[4]; const char* e = std::getenv("WFC_RT_CENSUS");
            return e && std::sscanf(e, "%f,%f,%f,%f", &c[0], &c[1], &c[2], &c[3]) == 4 ? c : (const float*)nullptr;
        }();
        if (census) {
            float d2 = 0.0f;
            const float pt[3] = {census[0], census[1], census[2]}, lo[3] = {s.bmin.x, s.bmin.y, s.bmin.z}, hi[3] = {s.bmax.x, s.bmax.y, s.bmax.z};
            for (int k = 0; k < 3; ++k) { const float q = std::max(lo[k] - pt[k], std::max(0.0f, pt[k] - hi[k])); d2 += q * q; }
            if (d2 <= census[3] * census[3])
                LOG_INFO("RT census: mesh %d sub %zu '%s' comp '%s' box (%.2f %.2f %.2f)..(%.2f %.2f %.2f) %s%s%s tris %u", meshId,
                         (size_t)(&s - g.subs.data()), s.matName.c_str(), s.comp.c_str(), s.bmin.x, s.bmin.y, s.bmin.z, s.bmax.x,
                         s.bmax.y, s.bmax.z, M.lit ? "lit" : "UNLIT", M.twoSided ? " two-sided" : "", s.lmTex[0] >= 0 ? " lightmapped" : " no-lightmap",
                         s.count / 3);
        }
        if (s.vlmTex) ++vlmSubs;
        if (s.mdiRow < 0) ++nonMdiSubs;
        std::vector<uint32_t>& out = M.twoSided ? W.twoSided : W.oneSided;
        out.insert(out.end(), idx.begin() + s.first, idx.begin() + s.first + s.count);
        if (keepHit) keep.sub[M.twoSided ? 1 : 0].insert(keep.sub[M.twoSided ? 1 : 0].end(), s.count / 3, (uint32_t)(&s - g.subs.data()));
        ++subs;
    }
    std::sort(largest.begin(), largest.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t k = 0; k < std::min<size_t>(6, largest.size()); ++k)
        LOG_INFO("RT world: large sub %.0f m: %s", largest[k].first, largest[k].second.c_str());
    const int set = meshId == bspMesh_ ? 1 : 0;
    static const bool noBsp = std::getenv("WFC_RT_NOBSP") != nullptr;   // (diagnostics: the level BSP set left out)
    if (set == 1 && noBsp) { LOG_INFO("RT world: level BSP set left out (WFC_RT_NOBSP)"); return; }
    LOG_INFO("RT world: %zu static opaque subs of the %s set (mesh %d; %zu vertex-lit, %zu not MDI-drawn) -> D3D12 (%zu sky subs, %zu CastShadow-false"
             " subs and %zu actor-placed (mover / toggled) subs left out)", subs, set ? "level BSP" : "world", meshId, vlmSubs, nonMdiSubs, skipped, noShadow, actorNodes);
    W.idBase = 0x10000u + (unsigned)set * 2u;           // (the reflection hits look their BLAS up by InstanceID)
    if (keepHit) {
        keep.mesh = meshId; keep.set = set; keep.idx[0] = W.oneSided; keep.idx[1] = W.twoSided;
        keep.uv1.resize(v.size() / 14 * 2);
        for (size_t i = 0, n = v.size() / 14; i < n; ++i) { keep.uv1[i * 2] = v[i * 14 + 12]; keep.uv1[i * 2 + 1] = v[i * 14 + 13]; }
        if (matDir()) {                                  // (materials) normal + UV0.x, tangent (x 2 where its sign is negative) + UV0.y
            keep.vtx.resize(v.size() / 14 * 8);
            for (size_t i = 0, n = v.size() / 14; i < n; ++i) {
                const float* q = &v[i * 14];
                float tx = q[6], ty = q[7], tz = q[8];
                const float tl = std::sqrt(tx * tx + ty * ty + tz * tz), k = (tl > 1e-12f ? 1.0f / tl : 0.0f) * (q[9] < 0.0f ? 2.0f : 1.0f);
                float* o = &keep.vtx[i * 8];
                o[0] = q[3]; o[1] = q[4]; o[2] = q[5]; o[3] = q[10]; o[4] = tx * k; o[5] = ty * k; o[6] = tz * k; o[7] = q[11];
            }
        }
    }
    if (rtBridgeAddStaticWorld(W, 1u << set)) {
        ++rtWorldSets_; rtSets_.push_back({meshId, set});
        if (keepHit) rtHitKeep_.push_back(std::move(keep));
    }
    else LOG_WARN("RT depth check: no static world on D3D12 (the round trip runs the copy)");
}

// once per frame, as a ray-traced frame would: the first MDI set's prepass (WFC_RT_ROUNDTRIP), or for the depth check
// the set WFC_RT_CHECKSET names (0 world, the default; 1 level BSP), traced against that set alone. Shadow / AO
// (WFC_RT_SHADOWAO, stage 1 option b): at the end of the opaque pass (flushTranslucency, after the characters), where
// the depth is complete and this frame's poses exist; endOfOpaque marks that call.
bool Pipeline::rtShadowAoOn() const { return rtInfra_ && !depthCheckOn(); }
// the composite's shader text: every RT-scene map (Realistic switches it on live); its uniform gate is rtReflLive
bool Pipeline::rtReflComposite() const { return rtShadowAoOn(); }
bool Pipeline::rtReflPass() const { return rtMode_ == 2 || (rtMode_ >= 1 && reflectOn()); }
bool Pipeline::rtReflLive() const {
    static const bool envComposite = std::getenv("WFC_RT_REFLECT_COMPOSITE") != nullptr;
    static const bool noRefl = std::getenv("WFC_RT_NOREFL") != nullptr;   // A/B: Realistic with the authored cubes (proof captures)
    if (noRefl) return false;
    return rtMode_ == 2 || (rtMode_ >= 1 && reflectOn() && envComposite);
}
// the starting mode: WFC_RT_MODE (0 / 1 / 2), else WFC_RT_SHADOWAO = Faithful (+ WFC_RT_WORLDSHADOWS = Realistic), else Off
void Pipeline::rtInitMode() {
    if (rtMode_ >= 0) return;
    if (const char* e = std::getenv("WFC_RT_MODE")) rtMode_ = std::min(2, std::max(0, std::atoi(e)));
    else if (shadowAoOn()) rtMode_ = std::getenv("WFC_RT_WORLDSHADOWS") ? 2 : 1;
    else rtMode_ = 0;
}
bool Pipeline::rtAvailable() const {
    if (rtAvailKnown_ < 0) rtAvailKnown_ = rtBridgeAvailable() ? 1 : 0;
    return rtAvailKnown_ == 1 && !rtFailed_;
}
bool Pipeline::rtSetMode(int mode) {
    if (mode < 0 || mode > 2) return false;
    rtInitMode();
    if (mode > 0 && !rtAvailable()) return false;
    if (mode == rtMode_) return true;
    rtMode_ = mode;
    rtHavePrevVP_ = false; rtReflValid_ = false;       // (the denoiser / reflection history restarts)
    LOG_INFO("renderer: Ray Tracing %s%s", mode == 0 ? "Off" : mode == 1 ? "Faithful" : "Realistic",
             mode > 0 && !rtInfra_ ? " (from the next map load: this map loaded without the RT scene)" : "");
    return true;
}
// after the round trip: this frame's reflections + view-projection kept for the next frame's world draws (a GL-owned
// RGBA16F copy with mips: the materials' LOD bias blurs it; the shared target is D3D12's again next frame)
void Pipeline::rtReflKeep() {
    const RtTargets& t = rtTargets_;
    if (!t.refl || !CopyImageSubData) { rtReflValid_ = false; return; }
    if (!rtReflPrev_ || rtReflPrevW_ != vpW_ || rtReflPrevH_ != vpH_) {
        if (rtReflPrev_) glDeleteTextures(1, &rtReflPrev_);
        glGenTextures(1, &rtReflPrev_);
        glBindTexture(GL_TEXTURE_2D, rtReflPrev_);
        int levels = 1; while ((std::max(vpW_, vpH_) >> levels) > 0) ++levels;
        for (int l = 0; l < levels; ++l)
            glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA16F, std::max(1, vpW_ >> l), std::max(1, vpH_ >> l), 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        rtReflPrevW_ = vpW_; rtReflPrevH_ = vpH_;
    }
    CopyImageSubData(t.refl, GL_TEXTURE_2D, 0, 0, 0, 0, rtReflPrev_, GL_TEXTURE_2D, 0, 0, 0, 0, vpW_, vpH_, 1);
    glBindTexture(GL_TEXTURE_2D, rtReflPrev_);
    GenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    glx::textureCacheInvalidate();
    std::memcpy(rtReflVP_, viewProj_.m, sizeof rtReflVP_);
    rtReflValid_ = true;
}

// the shared depth -> R32F copy (the scene depth as of now) into tex
void Pipeline::rtCopyDepth(GLuint tex) {
    ensureSceneDepth();
    GLint drawFb = 0; glGetIntegerv(0x8CA6, &drawFb);
    GLint prevProg = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    const GLboolean depthOn = glIsEnabled(GL_DEPTH_TEST), blendOn = glIsEnabled(GL_BLEND), cullOn = glIsEnabled(GL_CULL_FACE);
    GLboolean cm[4]; glGetBooleanv(GL_COLOR_WRITEMASK, cm);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    BindFramebuffer(GL_FRAMEBUFFER, rtFbo_);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glViewport(0, 0, vpW_, vpH_);
    UseProgram(rtDepthProg_);
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, depthCopyTex_);
    Uniform1i(cachedUniformLocation(rtDepthProg_, "uDepth"), 0);
    BindVertexArray(postVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    BindVertexArray(0);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    BindFramebuffer(GL_FRAMEBUFFER, (GLuint)drawFb);
    glColorMask(cm[0], cm[1], cm[2], cm[3]);
    if (depthOn) glEnable(GL_DEPTH_TEST);
    if (blendOn) glEnable(GL_BLEND);
    if (cullOn) glEnable(GL_CULL_FACE);
    UseProgram((GLuint)prevProg);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glx::uniformCacheForgetCurrent();
}

// masked (alpha-tested) world subs in the TLAS: each sub's opacity, evaluated by its own material program (the GLSL
// OpacityMask and clip), rendered once into a tile of a shared R8 atlas in UV0 space (the tile spans the sub's UV0 box:
// tiling materials get the cap's resolution); D3D12 tests a masked triangle's candidate hits against its tile. Built on
// the first RT frame of the map (the materials' textures are resident then).
void Pipeline::rtBuildMasked() {
    rtMaskedBuilt_ = true;
    if (!GetBufferSubData) return;
    constexpr int kAtlas = 4096, kMaxTile = 512, kTexelsPerUv = 256;
    struct Job { int mesh; size_t sub; float u0, v0, eu, ev; int x, y, w, h; };
    std::vector<Job> jobs;
    RtMaskedWorld M;
    int shelfX = 0, shelfY = 0, shelfH = 0;
    size_t skipped = 0;
    for (const auto& ms : rtSets_) {
        const GpuMesh& g = meshes_[(size_t)ms.first];
        size_t nIdx = 0;
        for (const Sub& s : g.subs) nIdx = std::max(nIdx, (size_t)s.first + s.count);
        std::vector<uint32_t> idx(nIdx);
        BindVertexArray(g.vao);
        if (nIdx) GetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)(nIdx * 4), idx.data());
        BindVertexArray(0);
        size_t nVert = 0;
        for (uint32_t i : idx) nVert = std::max(nVert, (size_t)i + 1);
        std::vector<float> v(nVert * 14);
        BindBuffer(GL_ARRAY_BUFFER, g.vbo);
        if (nVert) GetBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(v.size() * 4), v.data());
        BindBuffer(GL_ARRAY_BUFFER, 0);
        RtMaskedSet S;
        for (size_t si = 0; si < g.subs.size(); ++si) {
            const Sub& s = g.subs[si];
            if (s.prog < 0 || progs_[(size_t)s.prog].blend != 1 || !s.actor.empty() || s.noShadow || (size_t)s.first + s.count > idx.size()) continue;
            float u0 = 1e30f, v0 = 1e30f, u1 = -1e30f, v1 = -1e30f;
            for (uint32_t k = 0; k < s.count; ++k) {
                const float* vv = &v[(size_t)idx[s.first + k] * 14];
                u0 = std::min(u0, vv[10]); u1 = std::max(u1, vv[10]); v0 = std::min(v0, vv[11]); v1 = std::max(v1, vv[11]);
            }
            const float eu = std::max(u1 - u0, 1e-4f), ev = std::max(v1 - v0, 1e-4f);
            const int w = std::min(kMaxTile, std::max(8, (int)std::ceil(eu * kTexelsPerUv)));
            const int h = std::min(kMaxTile, std::max(8, (int)std::ceil(ev * kTexelsPerUv)));
            if (shelfX + w > kAtlas) { shelfX = 0; shelfY += shelfH; shelfH = 0; }
            if (shelfY + h > kAtlas) { ++skipped; continue; }     // (atlas full: this sub stays out of the TLAS)
            jobs.push_back({ms.first, si, u0, v0, eu, ev, shelfX, shelfY, w, h});
            const uint32_t subId = (uint32_t)(M.subs.size() / 8);
            const float r[8] = {u0, v0, 1.0f / eu, 1.0f / ev, (float)shelfX / kAtlas, (float)shelfY / kAtlas, (float)w / kAtlas, (float)h / kAtlas};
            M.subs.insert(M.subs.end(), r, r + 8);
            S.idx.insert(S.idx.end(), idx.begin() + s.first, idx.begin() + s.first + s.count);
            S.triSub.insert(S.triSub.end(), s.count / 3, subId);
            shelfX += w; shelfH = std::max(shelfH, h);
        }
        S.pos.resize(nVert * 3); S.uv.resize(nVert * 2);
        for (size_t i = 0; i < nVert; ++i) {
            std::memcpy(&S.pos[i * 3], &v[i * 14], 12);
            S.uv[i * 2] = v[i * 14 + 10]; S.uv[i * 2 + 1] = v[i * 14 + 11];
        }
        S.set = ms.second;
        M.sets.push_back(std::move(S));
    }
    if (jobs.empty()) { LOG_INFO("RT masked: no masked world subs"); return; }
    const GLuint atlas = rtBridgeMaskAtlas(kAtlas, kAtlas);
    if (!atlas) { LOG_WARN("RT masked: no atlas"); return; }
    // the atlas pass: each sub drawn alone by its own program, positions = its UV0 box -> its tile, output 1 where kept
    GLint drawFb = 0; glGetIntegerv(0x8CA6, &drawFb);
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    GLfloat cc[4]; glGetFloatv(GL_COLOR_CLEAR_VALUE, cc);
    const GLboolean depthOn = glIsEnabled(GL_DEPTH_TEST), blendOn = glIsEnabled(GL_BLEND), cullOn = glIsEnabled(GL_CULL_FACE);
    if (!rtAtlasFbo_) GenFramebuffers(1, &rtAtlasFbo_);
    BindFramebuffer(GL_FRAMEBUFFER, rtAtlasFbo_);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, atlas, 0);
    glViewport(0, 0, kAtlas, kAtlas);
    glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    const core::Mat4 identity;
    for (const Job& j : jobs) {
        glViewport(j.x, j.y, j.w, j.h);
        rtUvSpace_[0] = j.u0; rtUvSpace_[1] = j.v0; rtUvSpace_[2] = 1.0f / j.eu; rtUvSpace_[3] = 1.0f / j.ev;
        rtUvAtlas_ = true;
        drawSubs(meshes_[(size_t)j.mesh], identity, false, (int)j.sub);
        rtUvAtlas_ = false;
        glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    }
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    BindFramebuffer(GL_FRAMEBUFFER, (GLuint)drawFb);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glClearColor(cc[0], cc[1], cc[2], cc[3]);
    if (depthOn) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blendOn) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cullOn) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glx::uniformCacheForgetCurrent(); glx::textureCacheInvalidate();
    size_t tris = 0; for (const auto& S : M.sets) tris += S.idx.size() / 3;
    const bool ok = rtBridgeAddMasked(M);
    LOG_INFO("RT masked: %zu masked subs (%zu triangles) in a %dx%d opacity atlas (%zu left out: atlas full) -> D3D12 %s",
             jobs.size(), tris, kAtlas, kAtlas, skipped, ok ? "ok" : "FAILED");
}

// stage 2 prototype: what the reflection hits are shaded with - per sub its lightmap (pages, UV transform, scales) and an
// average albedo / emissive evaluated by its own material in UV0 space (8 x 8 tiles, averaged where the material keeps
// fragments); the lightmap pages read back from GL. View-independent, diffuse only: a radiance proxy for the B / C data.
void Pipeline::rtBuildHit() {
    rtHitBuilt_ = true;
    LOG_INFO("RT hit shading: %zu world sets kept, GetBufferSubData %s", rtHitKeep_.size(), GetBufferSubData ? "yes" : "no");
    if (rtHitKeep_.empty() || !GetBufferSubData) return;
    RtHitData D;
    std::vector<std::pair<int, size_t>> subRef;                // global sub -> (mesh, local sub)
    std::map<std::pair<int, size_t>, uint32_t> subId;
    std::map<int, int> lmIndex;                                 // GL lightmap page -> D3D12 page
    for (size_t k = 0; k < rtHitKeep_.size(); ++k) {
        RtHitKeep& K = rtHitKeep_[k];
        RtHitSet hs; hs.uv1 = K.uv1; hs.vtx = K.vtx; D.sets.push_back(std::move(hs));
        for (int side = 0; side < 2; ++side) {
            RtHitBlas b; b.setIndex = (int)k; b.idx = K.idx[side];
            for (uint32_t local : K.sub[side]) {
                auto key = std::make_pair(K.mesh, (size_t)local);
                auto it = subId.find(key);
                if (it == subId.end()) { it = subId.emplace(key, (uint32_t)subRef.size()).first; subRef.push_back(key); }
                b.triSub.push_back(it->second);
            }
            D.blas.push_back(std::move(b));                     // (blas index = set * 2 + side = InstanceID - 0x10000)
        }
    }
    // albedo + emissive: two 8 x 8 tiles per sub in a 1024 x 1024 RGBA16F target, drawn by the sub's own program
    const int kTile = 8, kSize = 1024, perRow = kSize / kTile;
    const size_t nSub = subRef.size();
    if (nSub * 2 > (size_t)perRow * perRow) { LOG_WARN("RT hit: %zu subs exceed the albedo atlas", nSub); return; }
    GLuint tex = 0, fbo = 0;
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, kSize, kSize, 0, GL_RGBA, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    GenFramebuffers(1, &fbo);
    GLint drawFb = 0; glGetIntegerv(0x8CA6, &drawFb);
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    GLfloat cc[4]; glGetFloatv(GL_COLOR_CLEAR_VALUE, cc);
    const GLboolean depthOn = glIsEnabled(GL_DEPTH_TEST), blendOn = glIsEnabled(GL_BLEND), cullOn = glIsEnabled(GL_CULL_FACE);
    BindFramebuffer(GL_FRAMEBUFFER, fbo);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glViewport(0, 0, kSize, kSize); glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
    const core::Mat4 identity;
    // every sub's UV0 box, from one read of each mesh's buffers
    std::vector<float> box(nSub * 4), uvPerM(nSub, 1.0f);
    {
        std::map<int, std::pair<std::vector<uint32_t>, std::vector<float>>> data;
        for (size_t i = 0; i < nSub; ++i) {
            const int mesh = subRef[i].first;
            auto it = data.find(mesh);
            if (it == data.end()) {
                const GpuMesh& g = meshes_[(size_t)mesh];
                size_t nIdx = 0;
                for (const Sub& s : g.subs) nIdx = std::max(nIdx, (size_t)s.first + s.count);
                std::vector<uint32_t> ix(nIdx);
                BindVertexArray(g.vao);
                if (nIdx) GetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)(nIdx * 4), ix.data());
                BindVertexArray(0);
                size_t nVert = 0; for (uint32_t vi : ix) nVert = std::max(nVert, (size_t)vi + 1);
                std::vector<float> vv(nVert * 14);
                BindBuffer(GL_ARRAY_BUFFER, g.vbo);
                if (nVert) GetBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(vv.size() * 4), vv.data());
                BindBuffer(GL_ARRAY_BUFFER, 0);
                it = data.emplace(mesh, std::make_pair(std::move(ix), std::move(vv))).first;
            }
            const Sub& s = meshes_[(size_t)mesh].subs[subRef[i].second];
            float u0 = 1e30f, v0 = 1e30f, u1 = -1e30f, v1 = -1e30f;
            for (uint32_t k = 0; k < s.count; ++k) {
                const float* q = &it->second.second[(size_t)it->second.first[s.first + k] * 14];
                u0 = std::min(u0, q[10]); u1 = std::max(u1, q[10]); v0 = std::min(v0, q[11]); v1 = std::max(v1, q[11]);
            }
            double uvA = 0, wA = 0;                      // (materials) UV0 area per world area: the ray cone's texel scale
            for (uint32_t k = 0; k + 2 < s.count; k += 3) {
                const float* a = &it->second.second[(size_t)it->second.first[s.first + k] * 14];
                const float* b = &it->second.second[(size_t)it->second.first[s.first + k + 1] * 14];
                const float* c = &it->second.second[(size_t)it->second.first[s.first + k + 2] * 14];
                uvA += 0.5 * std::fabs((double)(b[10] - a[10]) * (c[11] - a[11]) - (double)(c[10] - a[10]) * (b[11] - a[11]));
                const double ex = b[0] - a[0], ey = b[1] - a[1], ez = b[2] - a[2], fx = c[0] - a[0], fy = c[1] - a[1], fz = c[2] - a[2];
                const double cx = ey * fz - ez * fy, cy = ez * fx - ex * fz, cz = ex * fy - ey * fx;
                wA += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
            }
            uvPerM[i] = wA > 1e-9 && uvA > 0 ? (float)std::sqrt(uvA / wA) : 1.0f;
            box[i * 4] = u0; box[i * 4 + 1] = v0; box[i * 4 + 2] = 1.0f / std::max(u1 - u0, 1e-4f); box[i * 4 + 3] = 1.0f / std::max(v1 - v0, 1e-4f);
        }
    }
    for (int mode = 1; mode <= 2; ++mode)
        for (size_t i = 0; i < nSub; ++i) {
            const size_t tile = (size_t)(mode - 1) * nSub + i;
            const int tx = (int)(tile % perRow) * kTile, ty = (int)(tile / perRow) * kTile;
            glViewport(tx, ty, kTile, kTile);
            glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
            std::memcpy(rtUvSpace_, &box[i * 4], 16);
            rtUvAtlas_ = true; rtUvMode_ = mode;
            drawSubs(meshes_[(size_t)subRef[i].first], identity, false, (int)subRef[i].second);
            rtUvAtlas_ = false; rtUvMode_ = 0;
        }
    std::vector<float> px((size_t)kSize * kSize * 4);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, px.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    BindFramebuffer(GL_FRAMEBUFFER, (GLuint)drawFb);
    DeleteFramebuffers(1, &fbo); glDeleteTextures(1, &tex);
    glViewport(vp[0], vp[1], vp[2], vp[3]); glClearColor(cc[0], cc[1], cc[2], cc[3]);
    if (depthOn) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (blendOn) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cullOn) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    glx::uniformCacheForgetCurrent(); glx::textureCacheInvalidate();
    auto average = [&](size_t tile, float* rgb) {
        const int tx = (int)(tile % perRow) * kTile, ty = (int)(tile / perRow) * kTile;
        double s3[3] = {0, 0, 0}; double n = 0;
        for (int y = 0; y < kTile; ++y)
            for (int x = 0; x < kTile; ++x) {
                const float* q = &px[(((size_t)(ty + y)) * kSize + (size_t)(tx + x)) * 4];
                if (q[3] <= 0.0f) continue;
                for (int c = 0; c < 3; ++c) s3[c] += std::max(0.0f, q[c]);
                n += 1;
            }
        for (int c = 0; c < 3; ++c) rgb[c] = n > 0 ? (float)(s3[c] / n) : -1.0f;   // (-1: the tile got no fragment - filled below)
    };
    // (materials) the map's compiled reflection shader + material ids; per program its slot table; textures read back
    // from GL from the cap level down (WFC_RT_TEXCAP, default 256)
    std::map<std::string, int> matId;
    if (matDir()) {
        std::string dir = dataDir_;
        const size_t sl = dir.find_last_of("/\\");
        const std::string mapName = sl == std::string::npos ? dir : dir.substr(sl + 1);
        const std::string base = std::string(matDir()) + "/" + mapName + "/";
        if (FILE* f = std::fopen((base + "reflect_mat.cso").c_str(), "rb")) {
            std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
            D.materialShader.resize(n > 0 ? (size_t)n : 0);
            if (n > 0 && std::fread(D.materialShader.data(), 1, (size_t)n, f) != (size_t)n) D.materialShader.clear();
            std::fclose(f);
        }
        if (FILE* f = std::fopen((base + "materials_rt.txt").c_str(), "rb")) {
            char line[1024];
            while (std::fgets(line, sizeof line, f)) {
                char* tab = std::strchr(line, '\t');
                if (!tab) continue;
                std::string nm(tab + 1);
                while (!nm.empty() && (nm.back() == '\n' || nm.back() == '\r')) nm.pop_back();
                std::transform(nm.begin(), nm.end(), nm.begin(), ::tolower);
                matId[nm] = std::atoi(line);
            }
            std::fclose(f);
        }
        if (D.materialShader.empty() || matId.empty()) { LOG_WARN("RT hit: no material reflection shader in %s (the proxy shades)", base.c_str()); matId.clear(); D.materialShader.clear(); }
    }
    std::map<GLuint, int> tex2dIndex, cubeIndex;
    std::map<int, int> progTexBase;
    static const int texCap = [] { const char* e = std::getenv("WFC_RT_TEXCAP"); return e ? std::max(4, std::atoi(e)) : 256; }();
    auto copyTexture = [&](GLuint t, bool cube) -> int {
        auto& idx = cube ? cubeIndex : tex2dIndex;
        auto it = idx.find(t);
        if (it != idx.end()) return it->second;
        const int k = (int)(cube ? D.cubes.size() : D.textures.size());
        idx.emplace(t, k);
        RtHitTexture X; X.cube = cube;
        const GLenum target = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        const GLenum face0 = cube ? 0x8515 /*GL_TEXTURE_CUBE_MAP_POSITIVE_X*/ : GL_TEXTURE_2D;
        glBindTexture(target, t);
        GLint w0 = 0, h0 = 0, ifmt = 0, compressed = 0, maxLevel = 1000;
        glGetTexLevelParameteriv(face0, 0, GL_TEXTURE_WIDTH, &w0);
        glGetTexLevelParameteriv(face0, 0, GL_TEXTURE_HEIGHT, &h0);
        glGetTexLevelParameteriv(face0, 0, 0x1003 /*GL_TEXTURE_INTERNAL_FORMAT*/, &ifmt);
        glGetTexLevelParameteriv(face0, 0, 0x86A1 /*GL_TEXTURE_COMPRESSED*/, &compressed);
        glGetTexParameteriv(target, 0x813D /*GL_TEXTURE_MAX_LEVEL*/, &maxLevel);
        int first = 0;
        while (std::max(w0 >> first, h0 >> first) > texCap) ++first;
        int levels = 0;
        for (int l = first; l <= maxLevel; ++l) {
            GLint lw = 0; glGetTexLevelParameteriv(face0, l, GL_TEXTURE_WIDTH, &lw);
            if (lw <= 0) break;
            ++levels;
            if (std::max(w0 >> l, h0 >> l) <= 1) break;
        }
        // format: the GL storage kept where D3D12 has it (blocks, sRGB, 10:10:10:2, 16-bit), else read back as RGBA8
        int fmt = 0; GLenum rf = GL_RGBA, rt = GL_UNSIGNED_BYTE; int bpp = 4;
        if (compressed && GetCompressedTexImage) {
            switch (ifmt) {
            case 0x83F0: case 0x83F1: fmt = 2; break;       // S3TC DXT1 (RGB / RGBA)
            case 0x8C4C: case 0x8C4D: fmt = 3; break;       // sRGB DXT1
            case 0x83F2: fmt = 4; break; case 0x8C4E: fmt = 5; break;
            case 0x83F3: fmt = 6; break; case 0x8C4F: fmt = 7; break;
            case 0x8DBB: fmt = 11; break;                    // RGTC1 = BC4 (M6 roughness / height)
            case 0x8E8C: fmt = 12; break;                    // BPTC unorm = BC7 (M6 ORM)
            default: compressed = 0; break;                  // (other block formats: decoded to RGBA8)
            }
        }
        else compressed = 0;
        if (!compressed) {
            if (ifmt == 0x8C43 || ifmt == 0x8C41) fmt = 1;                                           // SRGB8(_ALPHA8)
            else if (ifmt == 0x8059) { fmt = 8; rt = 0x8368 /*GL_UNSIGNED_INT_2_10_10_10_REV*/; }   // RGB10_A2
            else if (ifmt == 0x805B) { fmt = 9; rt = GL_UNSIGNED_SHORT; bpp = 8; }                   // RGBA16
            else if (ifmt == 0x881A) { fmt = 10; rt = 0x140B /*GL_HALF_FLOAT*/; bpp = 8; }          // RGBA16F
        }
        // (BC needs a top level of whole blocks)
        if (compressed && (((w0 >> first) & 3) || ((h0 >> first) & 3))) { compressed = 0; fmt = (fmt == 3 || fmt == 5 || fmt == 7) ? 1 : 0; }   // (BC4 / BC7 too: RGBA8)
        X.w = std::max(1, w0 >> first); X.h = std::max(1, h0 >> first); X.levels = levels; X.format = fmt;
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        for (int f = 0; f < (cube ? 6 : 1) && levels > 0; ++f)
            for (int l = first; l < first + levels; ++l) {
                const GLenum face = cube ? (GLenum)(0x8515 + f) : GL_TEXTURE_2D;
                GLint lw = 0, lh = 0; glGetTexLevelParameteriv(face, l, GL_TEXTURE_WIDTH, &lw); glGetTexLevelParameteriv(face, l, GL_TEXTURE_HEIGHT, &lh);
                std::vector<unsigned char> buf;
                if (compressed) {
                    const int bs = (fmt == 2 || fmt == 3 || fmt == 11) ? 8 : 16;
                    buf.resize((size_t)((lw + 3) / 4) * (size_t)((lh + 3) / 4) * (size_t)bs);
                    GetCompressedTexImage(face, l, buf.data());
                }
                else {
                    buf.resize((size_t)lw * (size_t)lh * (size_t)bpp);
                    glGetTexImage(face, l, rf, rt, buf.data());
                }
                X.data.push_back(std::move(buf));
            }
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glBindTexture(target, 0);
        if (cube) D.cubes.push_back(std::move(X)); else D.textures.push_back(std::move(X));
        return k;
    };
    size_t matSubs = 0;
    double albSum = 0.0, emiSum = 0.0; size_t albZero = 0, albN = 0;   // (R4: the hit albedo the bounce sees)
    // the sub table: 8 float4 per sub
    for (size_t i = 0; i < nSub; ++i) {
        const Sub& s = meshes_[(size_t)subRef[i].first].subs[subRef[i].second];
        float r[32] = {};
        for (int k = 0; k < 3; ++k) {
            int page = -1;
            if (s.lmTex[k] >= 0 && (size_t)s.lmTex[k] < lmTextures_.size() && lmTextures_[(size_t)s.lmTex[k]]) {
                auto it = lmIndex.find(s.lmTex[k]);
                if (it == lmIndex.end()) it = lmIndex.emplace(s.lmTex[k], (int)lmIndex.size()).first;
                page = it->second;
            }
            r[k] = (float)page;
            r[8 + k * 4] = s.lmScale[k][0]; r[8 + k * 4 + 1] = s.lmScale[k][1]; r[8 + k * 4 + 2] = s.lmScale[k][2];
        }
        std::memcpy(r + 4, s.lmCoord, 16);
        if (s.vlmTex && s.vlmMdiOff >= 0 && subRef[i].first == giVlmMesh_) {   // (R4: a vertex-lit sub - its RT vertex samples)
            r[0] = -2.0f; r[3] = (float)s.vlmMdiOff; r[4] = (float)s.vlmMdiCnt; r[5] = (float)s.vlmBase;
        }
        average(i, r + 20);
        average(nSub + i, r + 24);
        if (r[24] < 0.0f) r[24] = r[25] = r[26] = 0.0f;   // (an empty emissive tile: none)
        if (r[20] >= 0.0f) { const double a = (r[20] + r[21] + r[22]) / 3.0; albSum += a; ++albN; } else ++albZero;
        emiSum += (r[24] + r[25] + r[26]) / 3.0;
        r[28] = -1.0f;                                   // (materials) id, texture base, log2(uv per metre), flags
        if (!matId.empty() && s.prog >= 0 && (size_t)s.prog < progs_.size() && progs_[(size_t)s.prog].original) {
            std::string ml = s.matName;
            std::transform(ml.begin(), ml.end(), ml.begin(), ::tolower);
            auto mi = matId.find(ml);
            if (mi != matId.end()) {
                const Program& P = progs_[(size_t)s.prog];
                auto pb = progTexBase.find(s.prog);
                if (pb == progTexBase.end()) {
                    pb = progTexBase.emplace(s.prog, (int)(D.matTex.size() / 12)).first;
                    for (const Program::Slot& sl : P.slots) {
                        float e[12] = {};
                        int ti = sl.tex ? copyTexture(sl.tex, sl.cube) : -1;
                        uint32_t idxBits = ti >= 0 ? (uint32_t)ti : 0xFFFFFFFFu, flags = (sl.cube ? 1u : 0u) | (!sl.cube && srgbBlockTex_.count(sl.tex) ? 2u : 0u);
                        std::memcpy(&e[0], &idxBits, 4); std::memcpy(&e[1], &flags, 4);
                        const RtHitTexture* X = ti < 0 ? nullptr : sl.cube ? &D.cubes[(size_t)ti] : &D.textures[(size_t)ti];
                        e[2] = X ? std::log2((float)std::max(X->w, X->h)) : 0.0f;
                        for (int c = 0; c < 4; ++c) { e[4 + c] = sl.umin[c]; e[8 + c] = sl.uscale[c]; }
                        D.matTex.insert(D.matTex.end(), e, e + 12);
                    }
                }
                const bool lightmapped = s.lmTex[0] >= 0 && !s.vlmTex;
                r[28] = (float)mi->second; r[29] = (float)pb->second; r[30] = std::log2(std::max(uvPerM[i], 1e-6f));
                r[31] = (float)((P.lit ? 1 : 0) | (P.twoSided ? 2 : 0) | (lightmapped ? 4 : 0));
                ++matSubs;
            }
        }
        D.subs.insert(D.subs.end(), r, r + 32);
    }
    // the lightmap pages (read back as RGBA8; sRGB kept as the GL texture's format said)
    D.lightmaps.resize(lmIndex.size());
    giHitPageGl_.assign(lmIndex.size(), -1);                    // (R4: the GI pages' numbering at hits)
    for (const auto& kv : lmIndex) giHitPageGl_[(size_t)kv.second] = kv.first;
    for (const auto& kv : lmIndex) {
        RtLightmap& L = D.lightmaps[(size_t)kv.second];
        const GLuint t = lmTextures_[(size_t)kv.first];
        glBindTexture(GL_TEXTURE_2D, t);
        GLint w = 0, h = 0, fmt = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, 0x1003 /*GL_TEXTURE_INTERNAL_FORMAT*/, &fmt);
        L.w = w; L.h = h;
        L.srgb = fmt == 0x8C43 || fmt == 0x8C41 || (fmt >= 0x8C4C && fmt <= 0x8C4F);   // SRGB8(_ALPHA8), sRGB S3TC
        L.rgba.resize((size_t)w * h * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        if (w > 0 && h > 0) glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, L.rgba.data());
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glx::textureCacheInvalidate();
    // subs whose 8 x 8 tile got no fragment (UV islands thinner than a texel: half of Molten's) take the mean of the rest
    // (they read black before: the bounce rays saw albedo 0.03 on average)
    {
        const float fill = albN ? (float)(albSum / (double)albN) : 0.25f;
        for (size_t i = 0; i < nSub && (i + 1) * 32 <= D.subs.size(); ++i)
            if (D.subs[i * 32 + 20] < 0.0f) D.subs[i * 32 + 20] = D.subs[i * 32 + 21] = D.subs[i * 32 + 22] = fill;
    }
    size_t bytes = 0; for (const auto& L : D.lightmaps) bytes += L.rgba.size();
    const bool ok = rtBridgeAddHitData(D);
    size_t texBytes = 0;
    for (const auto* L : {&D.textures, &D.cubes}) for (const auto& X : *L) for (const auto& d : X.data) texBytes += d.size();
    LOG_INFO("RT hit shading: %zu subs (albedo / emissive from their materials), %zu lightmap pages (%.1f MB) -> D3D12 %s;"
             " materials: %zu subs with a translated material (%zu ids), %zu + %zu cube textures copied (cap %d, %.1f MB), %zu slots",
             nSub, D.lightmaps.size(), (double)bytes / 1048576.0, ok ? "ok" : "FAILED", matSubs, matId.size(), D.textures.size(),
             D.cubes.size(), texCap, (double)texBytes / 1048576.0, D.matTex.size() / 12);
    if (nSub) LOG_INFO("RT hit shading: sub albedo mean %.3f over %zu subs with fragments; %zu of %zu subs had none (given that mean),"
                       " emissive mean %.3f", albN ? albSum / (double)albN : 0.0, albN, albZero, nSub, emiSum / (double)nSub);
    rtHitKeep_.clear(); rtHitKeep_.shrink_to_fit();
}

// flushDynQueue's RT path (stage 1 option b), once per frame in the main scene pass with a static world on D3D12:
// begin = the world depth (before the dynamic draws) copied, colour writes off for the dynamic depth pass
bool Pipeline::rtDynBegin() {
    if (!rtShadowAoOn() || rtMode_ <= 0 || rtFailed_ || rtWorldSets_ == 0 || rtFrame_ == frameNo_) return false;
    GLint drawFb = 0; glGetIntegerv(0x8CA6, &drawFb);
    if ((GLuint)drawFb != fbo_) return false;
    RtTargets t;
    if (!rtBridgePrepare(vpW_, vpH_, &t)) { rtFailed_ = true; LOG_WARN("RT: bridge unavailable"); return false; }
    if (t.depth != rtLastDepthTex_) { glx::textureCacheInvalidate(); rtLastDepthTex_ = t.depth; }
    if (!rtDepthProg_) {
        GLuint vs = compileShader(GL_VERTEX_SHADER, kRtVS, "rt.vs");
        GLuint fs = vs ? compileShader(GL_FRAGMENT_SHADER, kRtDepthFS, "rt_depth.fs") : 0;
        GLuint cf = vs ? compileShader(GL_FRAGMENT_SHADER, kRtCompositeFS, "rt_composite.fs") : 0;
        rtDepthProg_ = fs ? linkProgram(vs, fs, "rt_depth") : 0;
        rtCompositeProg_ = cf ? linkProgram(vs, cf, "rt_composite") : 0;
        for (GLuint sh : {vs, fs, cf}) if (sh) DeleteShader(sh);
        GenFramebuffers(1, &rtFbo_);
        if (!rtDepthProg_ || !rtCompositeProg_) { rtFailed_ = true; return false; }
    }
    if (!rtMaskedBuilt_) rtBuildMasked();                // (once per map, textures resident by now)
    if (!rtHitBuilt_ && rtReflPass()) rtBuildHit();
    rtCopyDepth(t.worldDepth);
    rtTargets_ = t;
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    rtDepthOnly_ = true;
    return true;
}

// mid = the dynamic depth is in: the round trip (shadow / AO / denoise), the world composite, and the RT mask + AO bound
// for the dynamic draws that follow
void Pipeline::rtDynMid() {
    rtDepthOnly_ = false;
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    depthDirty_ = true;
    giFrame();                                           // (R4: this frame's slice of the RT lightmap)
    rtRoundTrip(false, true);
    giAfterFrame();
    if (rtReflComposite() && rtReflLive()) { glx::gpuStamp("RT: reflections kept"); rtReflKeep(); }
    glx::gpuStamp("RT: world composite");
    const RtTargets& t = rtTargets_;
    const Light* sun = nullptr;
    for (const Light& L : lights_) if (L.type == 2 && L.enabled && (!sun || L.brightness > sun->brightness)) sun = &L;
    static const float aoStrength = [] { const char* e = std::getenv("WFC_RT_AOSTRENGTH"); return e ? (float)std::atof(e) : 1.0f; }();
    GLint prevProg = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    const GLboolean depthOn = glIsEnabled(GL_DEPTH_TEST), blendOn = glIsEnabled(GL_BLEND), cullOn = glIsEnabled(GL_CULL_FACE);
    GLint bs = 0, bd = 0; glGetIntegerv(0x80C9 /*GL_BLEND_SRC_RGB*/, &bs); glGetIntegerv(0x80C8 /*GL_BLEND_DST_RGB*/, &bd);
    GLint bsa = 0, bda = 0; glGetIntegerv(0x80CB /*GL_BLEND_SRC_ALPHA*/, &bsa); glGetIntegerv(0x80CA /*GL_BLEND_DST_ALPHA*/, &bda);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_BLEND);
    bool r1 = r1Active() && r1ClearedFrame_ == frameNo_ && r1SunTex_;
    if (r1 && !rtCompositeR1Prog_) {                     // (R1: built on first use; a failure keeps the plain composite)
        GLuint vs = compileShader(GL_VERTEX_SHADER, kRtVS, "rt.vs");
        GLuint cf = vs ? compileShader(GL_FRAGMENT_SHADER, kRtCompositeR1FS, "rt_composite_r1.fs") : 0;
        rtCompositeR1Prog_ = cf ? linkProgram(vs, cf, "rt_composite_r1") : 0;
        for (GLuint sh : {vs, cf}) if (sh) DeleteShader(sh);
        if (!rtCompositeR1Prog_) { LOG_WARN("RT R1: the sun-swap composite failed to build: R1 off"); r1Map_ = false; }
    }
    r1 = r1 && rtCompositeR1Prog_;
    if (rtMode_ == 2 && !r1StateLogged_ && (r1 || !r1Active())) {   // (the R1 state as the player sees it: once per map)
        r1StateLogged_ = true;
        size_t pages = 0;
        for (GLuint t : vbTex_) pages += t ? 1 : 0;
        LOG_INFO("RT R1 (first Realistic frame): %s (V_bake pages %zu, arrays %zu)", r1 ? "sun swap ON" :
                 "sun swap OFF (no usable V_bake fit / GL entry points / WFC_RT_R1=0)",
                 pages, vbArrays_.size());
    }
    const GLuint cp = r1 ? rtCompositeR1Prog_ : rtCompositeProg_;
    if (r1) BlendFuncSeparate(GL_ONE, 0x88F9 /*GL_SRC1_COLOR*/, GL_ZERO, GL_ONE);   // colour = colour x factor + add
    else BlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);   // colour *= factor, alpha kept
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    UseProgram(cp);
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, rtSoftActive_ ? t.shadowSoft : t.shadowDn);
    ActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, t.shadowAo);
    if (r1) { ActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, r1SunTex_); }
    ActiveTexture(GL_TEXTURE0);
    Uniform1i(cachedUniformLocation(cp, "uVis"), 0);
    Uniform1i(cachedUniformLocation(cp, "uSao"), 1);
    if (r1) Uniform1i(cachedUniformLocation(cp, "uSun"), 2);
    static const bool r1View = std::getenv("WFC_RT_R1VIEW") != nullptr;
    if (r1) Uniform1i(cachedUniformLocation(cp, "uR1View"), r1View ? 1 : 0);
    Uniform3f(cachedUniformLocation(cp, "uModShadow"), sun ? sun->modShadowColor[0] : 0.0f,
              sun ? sun->modShadowColor[1] : 0.0f, sun ? sun->modShadowColor[2] : 0.0f);
    Uniform1f(cachedUniformLocation(cp, "uAoStrength"), aoStrength);
    {   // (item 2B) the characters' lamp shadows on the world (the plain composite; R1 off under R4)
        int nb = 0;
        if (!r1 && rtLampShadowsWorld() && r1SunTex_ && r1ClearedFrame_ == frameNo_ && t.lightVis) {
            float pos[16] = {}, col[16] = {}, dir[16] = {}, cosI[4] = {};
            for (int k = 0; k < 4; ++k) {
                const int li = rtShadowLight_[k];
                if (li < 0) break;
                const Light& L = lights_[(size_t)li];
                auto cal = giLightCal_.find(li);
                if (cal == giLightCal_.end()) break;
                pos[k * 4] = L.pos.x; pos[k * 4 + 1] = L.pos.y; pos[k * 4 + 2] = L.pos.z; pos[k * 4 + 3] = L.radius;
                col[k * 4] = cal->second[0]; col[k * 4 + 1] = cal->second[1]; col[k * 4 + 2] = cal->second[2];
                col[k * 4 + 3] = cal->second[3] > 0.5f ? -1.0f : L.falloff;
                const core::Vec3 fw = core::normalize(L.dir);
                dir[k * 4] = fw.x; dir[k * 4 + 1] = fw.y; dir[k * 4 + 2] = fw.z; dir[k * 4 + 3] = L.type == 1 ? L.spotCosO : -2.0f;
                cosI[k] = L.type == 1 ? L.spotCosI : 1.0f;
                ++nb;
            }
            if (nb) {
                ActiveTexture(GL_TEXTURE0 + 3); glBindTexture(GL_TEXTURE_2D, t.lightVis);
                ActiveTexture(GL_TEXTURE0 + 4); glBindTexture(GL_TEXTURE_2D, r1SunTex_);
                ActiveTexture(GL_TEXTURE0 + 5); glBindTexture(GL_TEXTURE_2D, t.worldDepth);
                ActiveTexture(GL_TEXTURE0);
                Uniform1i(cachedUniformLocation(cp, "uLVis"), 3); Uniform1i(cachedUniformLocation(cp, "uLum"), 4);
                Uniform1i(cachedUniformLocation(cp, "uDepth"), 5);
                float inv[16];
                if (invert(viewProj_.m, inv)) UniformMatrix4fv(cachedUniformLocation(cp, "uInvVP"), 1, GL_FALSE, inv); else nb = 0;
                Uniform2f(cachedUniformLocation(cp, "uSize"), (float)vpW_, (float)vpH_);
                Uniform4fv(cachedUniformLocation(cp, "uSLPos"), 4, pos); Uniform4fv(cachedUniformLocation(cp, "uSLCol"), 4, col);
                Uniform4fv(cachedUniformLocation(cp, "uSLDir"), 4, dir); Uniform4f(cachedUniformLocation(cp, "uSLCosI"), cosI[0], cosI[1], cosI[2], cosI[3]);
            }
        }
        Uniform1i(cachedUniformLocation(cp, "uLB"), nb);
        static const bool lvView = std::getenv("WFC_RT_LVISVIEW") != nullptr;
        if (lvView && t.lightVis) { ActiveTexture(GL_TEXTURE0 + 3); glBindTexture(GL_TEXTURE_2D, t.lightVis); ActiveTexture(GL_TEXTURE0); Uniform1i(cachedUniformLocation(cp, "uLVis"), 3); }
        Uniform1i(cachedUniformLocation(cp, "uLVView"), lvView && t.lightVis ? 1 : 0);
        rtLampWorldN_ = nb;
        // (item 4) the transient lights' RT shadows on the world: the same inputs (bound here when 2B did not)
        const bool tlOk = !r1 && tlN_ > 0 && rtMrtWorld() && r1SunTex_ && r1ClearedFrame_ == frameNo_ && t.lightVis;
        if (tlOk && nb == 0) {
            ActiveTexture(GL_TEXTURE0 + 3); glBindTexture(GL_TEXTURE_2D, t.lightVis);
            ActiveTexture(GL_TEXTURE0 + 4); glBindTexture(GL_TEXTURE_2D, r1SunTex_);
            ActiveTexture(GL_TEXTURE0 + 5); glBindTexture(GL_TEXTURE_2D, t.worldDepth);
            ActiveTexture(GL_TEXTURE0);
            Uniform1i(cachedUniformLocation(cp, "uLVis"), 3); Uniform1i(cachedUniformLocation(cp, "uLum"), 4);
            Uniform1i(cachedUniformLocation(cp, "uDepth"), 5);
            float inv[16];
            if (invert(viewProj_.m, inv)) UniformMatrix4fv(cachedUniformLocation(cp, "uInvVP"), 1, GL_FALSE, inv);
            Uniform2f(cachedUniformLocation(cp, "uSize"), (float)vpW_, (float)vpH_);
        }
        Uniform1i(cachedUniformLocation(cp, "uTLN"), tlOk ? tlN_ : 0);
        if (tlOk) { Uniform4fv(cachedUniformLocation(cp, "uTLPos"), 8, &tlPos_[0][0]); Uniform4fv(cachedUniformLocation(cp, "uTLCol"), 8, &tlCol_[0][0]); }
    }
    BindVertexArray(postVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    BindVertexArray(0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    BlendFuncSeparate((GLenum)bs, (GLenum)bd, (GLenum)bsa, (GLenum)bda);
    if (!blendOn) glDisable(GL_BLEND);
    if (depthOn) glEnable(GL_DEPTH_TEST);
    if (cullOn) glEnable(GL_CULL_FACE);
    UseProgram((GLuint)prevProg);
    glx::uniformCacheForgetCurrent();
    glx::textureCacheInvalidate();
    rtMaskActive_ = true;                                // shadowMaskTexFor / the AO unit: the RT results
}

void Pipeline::rtDynEnd() { rtMaskActive_ = false; }

// the AO sampler of the RT-on dynamic shader variant (unit 25): the RT target while the RT path draws, else white
GLuint Pipeline::rtAoTexture() {
    if (rtMaskActive_) return rtTargets_.shadowAo;
    if (!rtWhiteTex_) {
        glGenTextures(1, &rtWhiteTex_);
        glBindTexture(GL_TEXTURE_2D, rtWhiteTex_);
        const unsigned char px[4] = {255, 255, 255, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
        glx::textureCacheInvalidate();
    }
    return rtWhiteTex_;
}

void Pipeline::rtRoundTrip(bool bspSet, bool endOfOpaque) {
    static const bool envOn = std::getenv("WFC_RT_ROUNDTRIP") != nullptr || depthCheckOn();
    if (!(envOn || rtInfra_) || rtFailed_) return;
    GLint drawFb = 0; glGetIntegerv(0x8CA6 /*GL_DRAW_FRAMEBUFFER_BINDING*/, &drawFb);
    if ((GLuint)drawFb != fbo_) return;                // the main scene pass only (not a shadow / warm-up pass's drawMdi)
    static const int checkSet = [] { const char* e = std::getenv("WFC_RT_CHECKSET"); return e ? std::atoi(e) : 0; }();
    const bool sao = rtInfra_ && !depthCheckOn();
    if (sao ? (!endOfOpaque || rtFrame_ == frameNo_ || rtWorldSets_ == 0) : endOfOpaque ? true
            : depthCheckOn() ? (bspSet ? 1 : 0) != checkSet : rtFrame_ == frameNo_) return;
    rtFrame_ = frameNo_;
    RtTargets t;
    if (!rtBridgePrepare(vpW_, vpH_, &t)) { rtFailed_ = true; LOG_WARN("RT round trip: bridge unavailable"); return; }
    if (t.depth != rtLastDepthTex_) { glx::textureCacheInvalidate(); rtLastDepthTex_ = t.depth; }   // (the bridge's raw binds)
    if (!rtDepthProg_) {
        GLuint vs = compileShader(GL_VERTEX_SHADER, kRtVS, "rt.vs");
        GLuint fs = vs ? compileShader(GL_FRAGMENT_SHADER, kRtDepthFS, "rt_depth.fs") : 0;
        rtDepthProg_ = fs ? linkProgram(vs, fs, "rt_depth") : 0;
        if (vs) DeleteShader(vs);
        if (fs) DeleteShader(fs);                        // (the program keeps them)
        GenFramebuffers(1, &rtFbo_);
        if (!rtDepthProg_) { rtFailed_ = true; return; }
        LOG_INFO("RT round trip: on (after the world depth prepass, %dx%d)", vpW_, vpH_);
    }
    glx::gpuStamp("RT: round trip");
    ensureSceneDepth();
    GLint prevProg = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    GLint prevVp[4]; glGetIntegerv(GL_VIEWPORT, prevVp);
    const GLboolean depthOn = glIsEnabled(GL_DEPTH_TEST), blendOn = glIsEnabled(GL_BLEND), cullOn = glIsEnabled(GL_CULL_FACE);
    GLboolean cm[4]; glGetBooleanv(GL_COLOR_WRITEMASK, cm);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    BindFramebuffer(GL_FRAMEBUFFER, rtFbo_);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.depth, 0);
    glViewport(0, 0, vpW_, vpH_);
    UseProgram(rtDepthProg_);
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, depthCopyTex_);
    Uniform1i(cachedUniformLocation(rtDepthProg_, "uDepth"), 0);
    BindVertexArray(postVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    BindVertexArray(0);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    BindFramebuffer(GL_FRAMEBUFFER, (GLuint)drawFb);
    RtFrame fr;
    static const unsigned maskEnv = [] { const char* e = std::getenv("WFC_RT_CHECKMASK"); return e ? (unsigned)std::atoi(e) : 0u; }();
    fr.instanceMask = maskEnv ? maskEnv : 1u << (bspSet ? 1 : 0);   // (WFC_RT_CHECKMASK: other sets too, diagnostics)
    const bool frameOk = (depthCheckOn() || sao) && invert(viewProj_.m, fr.invViewProj);
    if (sao) {
        static const int aoRays = [] { const char* e = std::getenv("WFC_RT_AORAYS"); return e ? std::max(0, std::atoi(e)) : 2; }();
        static const float aoRadius = [] { const char* e = std::getenv("WFC_RT_AORADIUS"); return e ? (float)std::atof(e) : 1.0f; }();
        fr.mode = 2; fr.instanceMask = 0xFF; fr.frameIndex = frameNo_; fr.aoRays = aoRays; fr.aoRadius = aoRadius;
        fr.reflect = rtReflPass();                       // (M7 stage 2: Realistic, or WFC_RT_REFLECT)
        static const bool shadeAb = std::getenv("WFC_RT_REFLECT_AB") != nullptr;   // (hit shading off in every other 30-frame block)
        fr.reflectShade = !shadeAb || (frameNo_ / 30) % 2 == 0;
        fr.time = time_;
        {                                                // a pixel's angle at the screen centre (the ray cone's spread)
            auto unproj = [&](float x, float y, float* o) {
                const float* M = fr.invViewProj; float q[4];
                for (int r = 0; r < 4; ++r) q[r] = M[r] * x + M[4 + r] * y + M[8 + r] * 1.0f + M[12 + r];
                for (int r = 0; r < 3; ++r) o[r] = q[r] / q[3];
            };
            float a[3], b[3], e0[3];
            unproj(0.0f, 0.0f, a); unproj(2.0f / (float)std::max(vpH_, 1), 0.0f, b);
            const float* Mi = fr.invViewProj; float qe[4];
            for (int r = 0; r < 4; ++r) qe[r] = -Mi[8 + r] + Mi[12 + r];
            for (int r = 0; r < 3; ++r) e0[r] = qe[r] / qe[3];
            float da[3], db[3], la = 0, lb = 0;
            for (int r = 0; r < 3; ++r) { da[r] = a[r] - e0[r]; db[r] = b[r] - e0[r]; la += da[r] * da[r]; lb += db[r] * db[r]; }
            float d2 = 0;
            for (int r = 0; r < 3; ++r) { const float x = da[r] / std::sqrt(la) - db[r] / std::sqrt(lb); d2 += x * x; }
            fr.pixelSpread = std::sqrt(d2);
        }
        // the sun's angular radius (WFC_RT_SUNCONE degrees; default 0.27, the real sun's): soft shadows for the denoiser
        static const float coneTan = [] { const char* e = std::getenv("WFC_RT_SUNCONE"); const float deg = e ? (float)std::atof(e) : 0.27f;
                                          return std::tan(deg * 3.14159265f / 180.0f); }();
        fr.sunConeTan = coneTan;
        // (R2) contact-hardening penumbrae (WFC_RT_SOFT=0: off): hard rays, the width from the blocker distance and the
        // sun's half angle. Faithful: the real sun's 0.27 degrees. Realistic: kRealisticSunArea (the user: stage-1 shadows
        // "way too hard ... softer and more natural") - the sun's disc plus the forward-scattering haze around it, which is
        // what makes real penumbrae read soft. WFC_RT_SUNAREA (degrees) overrides both. The widest penumbra: WFC_RT_SOFTMAX
        // pixels at 1080 lines, scaled with the viewport height (the same world width at 1440p / 4K).
        static const bool soft = [] { const char* e = std::getenv("WFC_RT_SOFT"); return !e || std::atoi(e) != 0; }();
        static const float areaEnv = [] { const char* e = std::getenv("WFC_RT_SUNAREA"); return e ? (float)std::atof(e) : -1.0f; }();
        constexpr float kRealisticSunArea = 1.5f;
        const float areaDeg = areaEnv >= 0.0f ? areaEnv : (rtMode_ == 2 ? kRealisticSunArea : 0.27f);
        const float areaTan = std::tan(areaDeg * 3.14159265f / 180.0f);
        static const float softMax1080 = [] { const char* e = std::getenv("WFC_RT_SOFTMAX"); return e ? (float)std::atof(e) : 24.0f; }();
        const float softMax = std::min(softMax1080 * (float)std::max(vpH_, 1) / 1080.0f, 64.0f);
        fr.softShadow = soft; fr.penumbraTan = areaTan; fr.softMaxRadius = softMax;
        if (soft) fr.sunConeTan = 0.0f;
        rtSoftActive_ = soft;
        const Light* sun = nullptr;                      // the brightest enabled directional light
        for (const Light& L : lights_)
            if (L.type == 2 && L.enabled && (!sun || L.brightness > sun->brightness)) sun = &L;
        if (sun) {
            const core::Vec3 d = core::normalize(sun->dir);
            static const bool worldShadows = std::getenv("WFC_RT_WORLDSHADOWS") != nullptr;   // (Realistic, or forced)
            fr.toSun[0] = -d.x; fr.toSun[1] = -d.y; fr.toSun[2] = -d.z; fr.toSun[3] = (worldShadows || rtMode_ == 2) ? 2.0f : 1.0f;
            if (r1Active() && r1ClearedFrame_ == frameNo_) fr.toSun[3] = 3.0f;   // (R1: the world's own sun visibility too; R4 keeps 2: its lightmap holds the world's sun shadows, world pixels take the characters' only)
            rtSunDir_[0] = -d.x; rtSunDir_[1] = -d.y; rtSunDir_[2] = -d.z;
        }
        // (item 2, Realistic) this frame's shadow lights: the point / spot lights the characters used most as direct lights
        // last frame (up to 4), traced per pixel; the characters drawn after this pass map their env lights to them
        for (int& k : rtShadowLight_) k = -1;
        if (rtShadowLightsOn()) {
            std::vector<std::pair<int, int>> use(rtLightUse_.begin(), rtLightUse_.end());
            std::sort(use.begin(), use.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
            int n = 0;
            for (const auto& u : use) {
                if (n >= 4) break;
                if (u.first < 0 || (size_t)u.first >= lights_.size()) continue;
                const Light& L = lights_[(size_t)u.first];
                if (L.type != 0 && L.type != 1) continue;
                rtShadowLight_[n] = u.first;
                fr.shadowLights[n][0] = L.pos.x; fr.shadowLights[n][1] = L.pos.y; fr.shadowLights[n][2] = L.pos.z; fr.shadowLights[n][3] = L.radius;
                ++n;
            }
            fr.shadowLightN = n;
            for (int k = 0; k < tlN_ && k < 4; ++k) { std::memcpy(fr.transLights[k], tlPos_[k], 16); fr.transLights[k][3] = 1.0f / std::max(tlPos_[k][3], 1e-6f); }
            fr.transLightN = std::min(tlN_, 4);
            static int frames = 0; static double sumUsed = 0, sumN = 0, sumWorld = 0;   // (diagnostics, every 600 frames)
            sumUsed += (double)use.size(); sumN += n; sumWorld += rtLampWorldN_;
            if (++frames == 600) {
                LOG_INFO("RT light shadows (600 frames): %.1f point / spot lights used by characters per frame, %.2f traced, %.2f on the world (item 2B; GI %s, calibrated lights %zu)",
                         sumUsed / 600.0, sumN / 600.0, sumWorld / 600.0, giActive() ? "on" : "off", giLightCal_.size());
                frames = 0; sumUsed = sumN = sumWorld = 0;
            }
        }
        rtLightUse_.clear();
        // the denoiser's matrices (FidelityFX conventions) and the camera-reprojection velocity; history reset on a cut
        float glInvP[16], glInvVP[16], tmp[16], tmp2[16];
        invert(camProj_.m, glInvP); invert(viewProj_.m, glInvVP);
        mul4(glInvP, kFfxToGl, fr.ffxProjInv);
        mul4(glInvVP, kFfxToGl, fr.ffxViewProjInv);
        const float* prev = rtHavePrevVP_ ? rtPrevVP_.m : viewProj_.m;
        mul4(prev, fr.ffxViewProjInv, tmp);              // previous GL clip of this pixel's point
        mul4(kGlToFfx, tmp, tmp2);
        std::memcpy(fr.ffxReproj, tmp2, sizeof tmp2);
        fr.eye[0] = camPos_.x; fr.eye[1] = camPos_.y; fr.eye[2] = camPos_.z;
        fr.znear = znear_; fr.zfar = zfar_;
        fr.resetHistory = !rtHavePrevVP_ || cameraCut_;
        { static const int dumpAt = [] { const char* e = std::getenv("WFC_RT_DUMPFRAME"); return e ? std::atoi(e) : -1; }();
          fr.debugDump = frameNo_ == dumpAt; }
        rtPrevVP_ = viewProj_; rtHavePrevVP_ = true;
        static bool told = false;
        if (!told) {
            told = true;
            LOG_INFO("RT shadow + AO: on (%dx%d, sun %s%s, AO %d rays radius %.2f m)", vpW_, vpH_, sun ? "'" : "none",
                     sun ? (sun->name + "'").c_str() : "", aoRays, aoRadius);
        }
    }
    static int logged = 0;
    if (depthCheckOn() && logged++ < 2)
        LOG_INFO("RT depth check: frame %d znear %g zfar %g proj diag %g %g %g %g, proj[14] %g, view row2 %g %g %g %g", frameNo_, znear_, zfar_,
                 camProj_.m[0], camProj_.m[5], camProj_.m[10], camProj_.m[11], camProj_.m[14], camView_.m[2], camView_.m[6], camView_.m[10], camView_.m[14]);
    fr.viewRow2[0] = camView_.m[2]; fr.viewRow2[1] = camView_.m[6]; fr.viewRow2[2] = camView_.m[10]; fr.viewRow2[3] = camView_.m[14];
    if (frameOk) rtSkinFeed(endOfOpaque);                 // (WFC_RT_CHARS: the characters' positions, before the signal)
    rtBridgeRoundTrip(frameOk ? &fr : nullptr);            // GL -> D3D12 job -> GL (the shading below waits for the mask)
    // (diagnostics) WFC_RT_DUMPFRAME=<n>: the raster depth and the ray's view depth of that frame as raw R32F files
    static const int dumpAt = [] { const char* e = std::getenv("WFC_RT_DUMPFRAME"); return e ? std::atoi(e) : -1; }();
    if (frameNo_ == dumpAt) {
        std::vector<float> px((size_t)vpW_ * vpH_);
        for (int k = 0; k < 2; ++k) {
            glBindTexture(GL_TEXTURE_2D, k ? t.mask : t.depth);
            glGetTexImage(GL_TEXTURE_2D, 0, 0x1903 /*GL_RED*/, GL_FLOAT, px.data());
            if (FILE* f = std::fopen(k ? "rt_dump_ray.bin" : "rt_dump_depth.bin", "wb")) { std::fwrite(px.data(), 4, px.size(), f); std::fclose(f); }
        }
        if (sao) {                                       // (shadow, AO) pairs
            std::vector<float> rg((size_t)vpW_ * vpH_ * 4);   // (sun, raw AO, dynamic, filtered AO)
            glBindTexture(GL_TEXTURE_2D, t.shadowAo);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, rg.data());
            if (FILE* f = std::fopen("rt_dump_shadowao.bin", "wb")) { std::fwrite(rg.data(), 4, rg.size(), f); std::fclose(f); }
            std::vector<unsigned char> dn((size_t)vpW_ * vpH_);
            glBindTexture(GL_TEXTURE_2D, t.shadowDn);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glGetTexImage(GL_TEXTURE_2D, 0, 0x1903 /*GL_RED*/, GL_UNSIGNED_BYTE, dn.data());
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            if (FILE* f = std::fopen("rt_dump_shadowdn.bin", "wb")) { std::fwrite(dn.data(), 1, dn.size(), f); std::fclose(f); }
            {                                            // the reflection target (hit distance, hit, ...)
                std::vector<float> rf((size_t)vpW_ * vpH_ * 4);
                glBindTexture(GL_TEXTURE_2D, t.refl);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, rf.data());
                if (FILE* f = std::fopen("rt_dump_refl.bin", "wb")) { std::fwrite(rf.data(), 4, rf.size(), f); std::fclose(f); }
            }
            if (rtAtlasFbo_) {                           // the masked-opacity atlas (4096 x 4096 R8)
                std::vector<unsigned char> at((size_t)4096 * 4096);
                glBindTexture(GL_TEXTURE_2D, rtBridgeMaskAtlas(4096, 4096));
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glGetTexImage(GL_TEXTURE_2D, 0, 0x1903 /*GL_RED*/, GL_UNSIGNED_BYTE, at.data());
                glPixelStorei(GL_PACK_ALIGNMENT, 4);
                if (FILE* f = std::fopen("rt_dump_atlas.bin", "wb")) { std::fwrite(at.data(), 1, at.size(), f); std::fclose(f); }
            }
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        std::string m;
        for (int i = 0; i < 16; ++i) m += " " + std::to_string(viewProj_.m[i]);
        for (int i = 0; i < 16; ++i) m += " " + std::to_string(camView_.m[i]);
        LOG_INFO("RT dump frame %d (%dx%d): viewProj + view (column-major):%s", frameNo_, vpW_, vpH_, m.c_str());
    }
    glColorMask(cm[0], cm[1], cm[2], cm[3]);
    if (depthOn) glEnable(GL_DEPTH_TEST);
    if (blendOn) glEnable(GL_BLEND);
    if (cullOn) glEnable(GL_CULL_FACE);
    UseProgram((GLuint)prevProg);
    glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
    glx::uniformCacheForgetCurrent();
}

}  // namespace wfc
}  // namespace render
