// Clean-room reconstruction — optional FSR 3.1 temporal upscaling, the GL side (PC ADAPTATION, M5 B; OFF by default).
// WFC_FSR3=1 (test switch; the settings row comes later) turns the FSR render scale (WFC_FSR=<scale>[,<sharpness>] /
// IRenderer::setUpscaling) into FSR 3.1: the 3D frame renders at the reduced size with projection jitter, motion
// vectors and the reactive mask on (WfcMotion.cpp); after translucency the HDR scene colour, depth, velocity and reactive
// mask go to D3D12 (render/d3d12/D3D12Upscaler.cpp), FSR upscales to the window size, and the post pass (bloom / DOF
// gathered at the render size, tonemap / CLUT) runs at the window size on the upscaled colour: upscaling before post.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "render/gl/WfcPipeline.h"
#include "render/d3d12/D3D12Upscaler.h"
#include "core/Log.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>

namespace render {
namespace wfc {
using namespace glx;

GLuint compileShader(GLenum type, const std::string& src, const std::string& tag);   // WfcPipeline.cpp
GLuint linkProgram(GLuint vs, GLuint fs, const std::string& tag);

namespace {
const char* kFsr3VS = R"(#version 430
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }
)";
const char* kDepthToR32FS = R"(#version 430
uniform sampler2D uDepth;
out float oDepth;
void main() { oDepth = texelFetch(uDepth, ivec2(gl_FragCoord.xy), 0).r; }
)";
typedef void(APIENTRY* PFN_CopyImageSubData)(GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint, GLint, GLint, GLint,
                                             GLsizei, GLsizei, GLsizei);
}  // namespace

bool Pipeline::fsr3Mode() const {
    static const bool env = [] { const char* e = std::getenv("WFC_FSR3"); return e && e[0] != '0'; }();
    return (env || fsr3On_) && !fsr3Failed_;
}

bool Pipeline::fsr3RuntimeShipped() {
    static const bool shipped = [] {
#if !defined(WFC_FFX_SDK)
        return false;                                    // (built without the FFX SDK: FSR 3.1 can never start)
#endif
        char exe[MAX_PATH * 2] = "";
        GetModuleFileNameA(nullptr, exe, sizeof exe);
        std::string dir(exe);
        dir = dir.substr(0, dir.find_last_of("\\/") + 1);
        return GetFileAttributesA((dir + "amd_fidelityfx_loader_dx12.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
               GetFileAttributesA((dir + "amd_fidelityfx_upscaler_dx12.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
               GetFileAttributesA((dir + "D3D12\\D3D12Core.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
    }();
    return shipped;
}

// after translucency (all inputs final): -> true with fsr3Out_ = the window-size upscaled HDR colour
bool Pipeline::fsr3Run(int outW, int outH) {
    if (!fsr3Mode() || !velTex_ || !reactiveTex_ || velValidFrame_ != frameNo_) return false;
    static PFN_CopyImageSubData copyImage = (PFN_CopyImageSubData)(void*)wglGetProcAddress("glCopyImageSubData");
    if (!copyImage) { LOG_WARN("FSR3: glCopyImageSubData unavailable"); fsr3Failed_ = true; return false; }
    Fsr3Targets t;
    if (!fsr3Prepare(vpW_, vpH_, outW, outH, &t)) {
        LOG_WARN("FSR3: unavailable (see above): FSR 1 kept"); fsr3Failed_ = true; return false;
    }
    if (!fsr3DepthProg_) {
        GLuint vs = compileShader(GL_VERTEX_SHADER, kFsr3VS, "fsr3.vs");
        GLuint fs = vs ? compileShader(GL_FRAGMENT_SHADER, kDepthToR32FS, "fsr3_depth.fs") : 0;
        fsr3DepthProg_ = fs ? linkProgram(vs, fs, "fsr3_depth") : 0;
        if (vs) DeleteShader(vs);
        if (fs) DeleteShader(fs);                        // (the program keeps them; per map otherwise leaked)
        if (!fsr3DepthProg_) { fsr3Failed_ = true; return false; }
        GenFramebuffers(1, &fsr3DepthFbo_);
    }
    glx::gpuStamp("FSR3: inputs");
    const auto tIn = std::chrono::steady_clock::now();
    // colour / velocity / reactive: same-format copies; depth: the depth texture into R32F
    copyImage(colorTex_, GL_TEXTURE_2D, 0, 0, 0, 0, t.color, GL_TEXTURE_2D, 0, 0, 0, 0, vpW_, vpH_, 1);
    copyImage(velTex_, GL_TEXTURE_2D, 0, 0, 0, 0, t.velocity, GL_TEXTURE_2D, 0, 0, 0, 0, vpW_, vpH_, 1);
    copyImage(reactiveTex_, GL_TEXTURE_2D, 0, 0, 0, 0, t.reactive, GL_TEXTURE_2D, 0, 0, 0, 0, vpW_, vpH_, 1);
    ensureSceneDepth();
    GLint prevProg = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    const GLboolean depthOn = glIsEnabled(GL_DEPTH_TEST), blendOn = glIsEnabled(GL_BLEND), cullOn = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    BindFramebuffer(GL_FRAMEBUFFER, fsr3DepthFbo_);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.depth, 0);
    glViewport(0, 0, vpW_, vpH_);
    UseProgram(fsr3DepthProg_);
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, depthCopyTex_);
    Uniform1i(cachedUniformLocation(fsr3DepthProg_, "uDepth"), 0);
    BindVertexArray(postVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    BindVertexArray(0);
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    if (depthOn) glEnable(GL_DEPTH_TEST);
    if (blendOn) glEnable(GL_BLEND);
    if (cullOn) glEnable(GL_CULL_FACE);
    UseProgram((GLuint)prevProg);
    glx::uniformCacheForgetCurrent();
    // the shared textures keep GL's row order: FSR's +y (down its rows) is GL's +y (up), so the jitter (image offset,
    // +x right / +y up in GL) and the velocity (prevUV - curUV) carry over without a sign change
    Fsr3Params p;
    p.jitterX = jitterPx_[0]; p.jitterY = jitterPx_[1];
    p.mvScaleX = (float)vpW_; p.mvScaleY = (float)vpH_;
    const auto now = std::chrono::steady_clock::now();
    static const bool lockstep = std::getenv("WFC_LOCKSTEP") != nullptr;
    p.frameTimeMs = lockstep || fsr3LastFrame_.time_since_epoch().count() == 0 ? 1000.0f / 60.0f
                  : std::chrono::duration<float, std::milli>(now - fsr3LastFrame_).count();
    fsr3LastFrame_ = now;
    p.reset = cameraCut_;
    p.nearM = znear_; p.farM = zfar_;
    p.fovY = 2.0f * std::atan(1.0f / std::max(camProj_.m[5], 1e-6f));
    p.sharpness = std::max(0.0f, fsrSharpness_);
    glx::gpuStamp("FSR3: dispatch");
    {
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tIn).count();
        fsr3InSum_ += ms; fsr3InMax_ = std::max(fsr3InMax_, ms);
        if (++fsr3InN_ == 300) {
            LOG_INFO("FSR3 inputs (copies + depth -> R32F; avg / max ms over 300): %.3f / %.2f", fsr3InSum_ / 300, fsr3InMax_);
            fsr3InSum_ = fsr3InMax_ = 0; fsr3InN_ = 0;
        }
    }
    if (!fsr3Dispatch(p)) return false;
    fsr3Out_ = t.output;
    static bool logged = false;
    if (!logged) { LOG_INFO("FSR3: upscaling %dx%d -> %dx%d (before post; sharpness %.2f)", vpW_, vpH_, outW, outH, p.sharpness); logged = true; }
    return true;
}

}  // namespace wfc
}  // namespace render
