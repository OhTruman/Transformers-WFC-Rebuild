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
out vec4 oFactor;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 s = texelFetch(uSao, p, 0);
    if (s.z > 0.5) { oFactor = vec4(1.0); return; }
    float vis = texelFetch(uVis, p, 0).r;
    vis = vis > 0.985 ? 1.0 : vis;                  // (the 8-bit denoised visibility tops out just under 1: unlit pixels exact)
    float ao = s.y > 0.985 ? 1.0 : s.y;
    vec3 f = mix(uModShadow, vec3(1.0), vis) * mix(1.0, ao, uAoStrength);
    oFactor = vec4(f, 1.0);
}
)";
bool depthCheckOn() { static const bool on = std::getenv("WFC_RT_DEPTHCHECK") != nullptr; return on; }
// stage 1 (measurement): sun shadow + AO rays per pixel (WFC_RT_AORAYS, default 2; WFC_RT_AORADIUS metres, default 1)
bool shadowAoOn() { static const bool on = std::getenv("WFC_RT_SHADOWAO") != nullptr; return on; }

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

// the depth prepass's triangle set (drawMdi's prepass: opaque MDI programs, VLM runs excluded), from the world mesh's
// GL buffers (14 floats per vertex, positions first; MDI base vertex 0), once per map
void Pipeline::rtBuildWorld(int meshId) {
    if (!(depthCheckOn() || shadowAoOn()) || rtFailed_ || meshId < 0 || !GetBufferSubData) return;
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
    RtStaticWorld W;
    W.pos.resize(v.size() / 14 * 3);
    for (size_t i = 0, n = v.size() / 14; i < n; ++i) std::memcpy(&W.pos[i * 3], &v[i * 14], 12);
    size_t subs = 0, skipped = 0;
    std::vector<std::pair<float, std::string>> largest;      // (diagnostics) the biggest subs going into the TLAS
    for (const Sub& s : g.subs) {
        if (s.mdiRow < 0 || s.prog < 0 || s.vlmTex || (size_t)s.first + s.count > idx.size()) continue;
        const Program& P = progs_[(size_t)s.prog];
        if (P.mdiProg < 0) continue;
        const Program& M = progs_[(size_t)P.mdiProg];
        if (M.blend != 0) continue;
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
        std::vector<uint32_t>& out = M.twoSided ? W.twoSided : W.oneSided;
        out.insert(out.end(), idx.begin() + s.first, idx.begin() + s.first + s.count);
        ++subs;
    }
    std::sort(largest.begin(), largest.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t k = 0; k < std::min<size_t>(6, largest.size()); ++k)
        LOG_INFO("RT world: large sub %.0f m: %s", largest[k].first, largest[k].second.c_str());
    const int set = meshId == bspMesh_ ? 1 : 0;
    static const bool noBsp = std::getenv("WFC_RT_NOBSP") != nullptr;   // (diagnostics: the level BSP set left out)
    if (set == 1 && noBsp) { LOG_INFO("RT world: level BSP set left out (WFC_RT_NOBSP)"); return; }
    LOG_INFO("RT world:%zu prepassed subs of the %s set (mesh %d) -> D3D12 (%zu sky subs left out)", subs, set ? "level BSP" : "world", meshId, skipped);
    if (rtBridgeAddStaticWorld(W, 1u << set)) ++rtWorldSets_;
    else LOG_WARN("RT depth check: no static world on D3D12 (the round trip runs the copy)");
}

// once per frame, as a ray-traced frame would: the first MDI set's prepass (WFC_RT_ROUNDTRIP), or for the depth check
// the set WFC_RT_CHECKSET names (0 world, the default; 1 level BSP), traced against that set alone. Shadow / AO
// (WFC_RT_SHADOWAO, stage 1 option b): at the end of the opaque pass (flushTranslucency, after the characters), where
// the depth is complete and this frame's poses exist; endOfOpaque marks that call.
bool Pipeline::rtShadowAoOn() const { return shadowAoOn() && !depthCheckOn(); }

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

// flushDynQueue's RT path (stage 1 option b), once per frame in the main scene pass with a static world on D3D12:
// begin = the world depth (before the dynamic draws) copied, colour writes off for the dynamic depth pass
bool Pipeline::rtDynBegin() {
    if (!rtShadowAoOn() || rtFailed_ || rtWorldSets_ == 0 || rtFrame_ == frameNo_) return false;
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
    rtRoundTrip(false, true);
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
    BlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);   // colour *= factor, alpha kept
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    UseProgram(rtCompositeProg_);
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, t.shadowDn);
    ActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, t.shadowAo);
    ActiveTexture(GL_TEXTURE0);
    Uniform1i(cachedUniformLocation(rtCompositeProg_, "uVis"), 0);
    Uniform1i(cachedUniformLocation(rtCompositeProg_, "uSao"), 1);
    Uniform3f(cachedUniformLocation(rtCompositeProg_, "uModShadow"), sun ? sun->modShadowColor[0] : 0.0f,
              sun ? sun->modShadowColor[1] : 0.0f, sun ? sun->modShadowColor[2] : 0.0f);
    Uniform1f(cachedUniformLocation(rtCompositeProg_, "uAoStrength"), aoStrength);
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
    static const bool on = std::getenv("WFC_RT_ROUNDTRIP") != nullptr || depthCheckOn() || shadowAoOn();
    if (!on || rtFailed_) return;
    GLint drawFb = 0; glGetIntegerv(0x8CA6 /*GL_DRAW_FRAMEBUFFER_BINDING*/, &drawFb);
    if ((GLuint)drawFb != fbo_) return;                // the main scene pass only (not a shadow / warm-up pass's drawMdi)
    static const int checkSet = [] { const char* e = std::getenv("WFC_RT_CHECKSET"); return e ? std::atoi(e) : 0; }();
    const bool sao = shadowAoOn() && !depthCheckOn();
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
        // the sun's angular radius (WFC_RT_SUNCONE degrees; default 0.27, the real sun's): soft shadows for the denoiser
        static const float coneTan = [] { const char* e = std::getenv("WFC_RT_SUNCONE"); const float deg = e ? (float)std::atof(e) : 0.27f;
                                          return std::tan(deg * 3.14159265f / 180.0f); }();
        fr.sunConeTan = coneTan;
        const Light* sun = nullptr;                      // the brightest enabled directional light
        for (const Light& L : lights_)
            if (L.type == 2 && L.enabled && (!sun || L.brightness > sun->brightness)) sun = &L;
        if (sun) {
            const core::Vec3 d = core::normalize(sun->dir);
            static const bool worldShadows = std::getenv("WFC_RT_WORLDSHADOWS") != nullptr;   // (the later modern mode)
            fr.toSun[0] = -d.x; fr.toSun[1] = -d.y; fr.toSun[2] = -d.z; fr.toSun[3] = worldShadows ? 2.0f : 1.0f;
            rtSunDir_[0] = -d.x; rtSunDir_[1] = -d.y; rtSunDir_[2] = -d.z;
        }
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
            std::vector<float> rg((size_t)vpW_ * vpH_ * 2);
            glBindTexture(GL_TEXTURE_2D, t.shadowAo);
            glGetTexImage(GL_TEXTURE_2D, 0, 0x8227 /*GL_RG*/, GL_FLOAT, rg.data());
            if (FILE* f = std::fopen("rt_dump_shadowao.bin", "wb")) { std::fwrite(rg.data(), 4, rg.size(), f); std::fclose(f); }
            std::vector<unsigned char> dn((size_t)vpW_ * vpH_);
            glBindTexture(GL_TEXTURE_2D, t.shadowDn);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glGetTexImage(GL_TEXTURE_2D, 0, 0x1903 /*GL_RED*/, GL_UNSIGNED_BYTE, dn.data());
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            if (FILE* f = std::fopen("rt_dump_shadowdn.bin", "wb")) { std::fwrite(dn.data(), 1, dn.size(), f); std::fclose(f); }
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
