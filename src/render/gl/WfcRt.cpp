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
    size_t subs = 0;
    for (const Sub& s : g.subs) {
        if (s.mdiRow < 0 || s.prog < 0 || s.vlmTex || (size_t)s.first + s.count > idx.size()) continue;
        const Program& P = progs_[(size_t)s.prog];
        if (P.mdiProg < 0) continue;
        const Program& M = progs_[(size_t)P.mdiProg];
        if (M.blend != 0) continue;
        std::vector<uint32_t>& out = M.twoSided ? W.twoSided : W.oneSided;
        out.insert(out.end(), idx.begin() + s.first, idx.begin() + s.first + s.count);
        ++subs;
    }
    const int set = meshId == bspMesh_ ? 1 : 0;
    LOG_INFO("RT depth check: %zu prepassed subs of the %s set (mesh %d) -> D3D12", subs, set ? "level BSP" : "world", meshId);
    if (!rtBridgeAddStaticWorld(W, 1u << set)) LOG_WARN("RT depth check: no static world on D3D12 (the round trip runs the copy)");
}

// once per frame, as a ray-traced frame would: the first MDI set's prepass (WFC_RT_ROUNDTRIP), or for the depth check
// the set WFC_RT_CHECKSET names (0 world, the default; 1 level BSP), traced against that set alone
void Pipeline::rtRoundTrip(bool bspSet) {
    static const bool on = std::getenv("WFC_RT_ROUNDTRIP") != nullptr || depthCheckOn() || shadowAoOn();
    if (!on || rtFailed_) return;
    static const int checkSet = [] { const char* e = std::getenv("WFC_RT_CHECKSET"); return e ? std::atoi(e) : 0; }();
    // shadow / AO: at the level BSP set's prepass when the map has one (the world set is drawn before it), else the world's
    const bool sao = shadowAoOn() && !depthCheckOn();
    if (sao ? bspSet != (bspMesh_ >= 0) : depthCheckOn() ? (bspSet ? 1 : 0) != checkSet : rtFrame_ == frameNo_) return;
    rtFrame_ = frameNo_;
    RtTargets t;
    if (!rtBridgePrepare(vpW_, vpH_, &t)) { rtFailed_ = true; LOG_WARN("RT round trip: bridge unavailable"); return; }
    if (!rtDepthProg_) {
        GLuint vs = compileShader(GL_VERTEX_SHADER, kRtVS, "rt.vs");
        GLuint fs = vs ? compileShader(GL_FRAGMENT_SHADER, kRtDepthFS, "rt_depth.fs") : 0;
        rtDepthProg_ = fs ? linkProgram(vs, fs, "rt_depth") : 0;
        GenFramebuffers(1, &rtFbo_);
        if (!rtDepthProg_) { rtFailed_ = true; return; }
        LOG_INFO("RT round trip: on (after the world depth prepass, %dx%d)", vpW_, vpH_);
    }
    glx::gpuStamp("RT: round trip");
    ensureSceneDepth();
    GLint prevProg = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
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
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    RtFrame fr;
    static const unsigned maskEnv = [] { const char* e = std::getenv("WFC_RT_CHECKMASK"); return e ? (unsigned)std::atoi(e) : 0u; }();
    fr.instanceMask = maskEnv ? maskEnv : 1u << (bspSet ? 1 : 0);   // (WFC_RT_CHECKMASK: other sets too, diagnostics)
    const bool frameOk = (depthCheckOn() || sao) && invert(viewProj_.m, fr.invViewProj);
    if (sao) {
        static const int aoRays = [] { const char* e = std::getenv("WFC_RT_AORAYS"); return e ? std::max(0, std::atoi(e)) : 2; }();
        static const float aoRadius = [] { const char* e = std::getenv("WFC_RT_AORADIUS"); return e ? (float)std::atof(e) : 1.0f; }();
        fr.mode = 2; fr.instanceMask = 0xFF; fr.frameIndex = frameNo_; fr.aoRays = aoRays; fr.aoRadius = aoRadius;
        const Light* sun = nullptr;                      // the brightest enabled directional light
        for (const Light& L : lights_)
            if (L.type == 2 && L.enabled && (!sun || L.brightness > sun->brightness)) sun = &L;
        if (sun) {
            const core::Vec3 d = core::normalize(sun->dir);
            fr.toSun[0] = -d.x; fr.toSun[1] = -d.y; fr.toSun[2] = -d.z; fr.toSun[3] = 1.0f;
        }
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
    glx::uniformCacheForgetCurrent();
}

}  // namespace wfc
}  // namespace render
