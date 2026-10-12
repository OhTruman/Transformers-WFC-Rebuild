// Clean-room reconstruction — optional FSR 3 frame generation, the GL side (PC ADAPTATION, M5 C; OFF by default).
// With the presenter's FG swapchain active (render/d3d12/D3D12FrameGen.h; WFC_FRAMEGEN=1 for now) the renderer writes,
// per real frame and after post + the HUD screen effects (3D-space post, interpolated with the scene) but before the
// Canvas tiles and the GFx UI (after Pipeline::endFrame): the HUD-less window image, the depth and the motion vectors,
// all flipped into D3D's top-down rows (motion y negated to match), plus the camera / timing / cut flag. FSR 3
// extracts the UI as the difference between the presented frame and the HUD-less one and composites it unchanged on
// the generated frames. Motion vectors run while FG is on; projection jitter only with temporal upscaling (FSR 3.1).
#include "render/gl/WfcPipeline.h"
#include "render/d3d12/D3D12FrameGen.h"
#include "core/Log.h"

#include <chrono>
#include <cmath>
#include <cstdlib>

namespace render {
namespace wfc {
using namespace glx;

GLuint compileShader(GLenum type, const std::string& src, const std::string& tag);   // WfcPipeline.cpp
GLuint linkProgram(GLuint vs, GLuint fs, const std::string& tag);

namespace {
const char* kFgVS = R"(#version 430
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }
)";
// render-size depth / motion -> the same size, rows flipped (D3D top-down); motion +y down
const char* kFgDepthFS = R"(#version 430
uniform sampler2D uSrc;
uniform int uH;
out float oDepth;
void main() { ivec2 p = ivec2(gl_FragCoord.xy); oDepth = texelFetch(uSrc, ivec2(p.x, uH - 1 - p.y), 0).r; }
)";
const char* kFgVelFS = R"(#version 430
uniform sampler2D uSrc;
uniform int uH;
out vec2 oVel;
void main() { ivec2 p = ivec2(gl_FragCoord.xy); vec2 v = texelFetch(uSrc, ivec2(p.x, uH - 1 - p.y), 0).xy; oVel = vec2(v.x, -v.y); }
)";
}  // namespace

bool Pipeline::framegenOn() const { return framegenActive(); }

void Pipeline::framegenCapture(int outW, int outH) {
    if (!framegenActive()) return;
    FgInputs in;
    if (velValidFrame_ != frameNo_ || !velTex_ || !depthCopyTex_) return;   // no 3D frame (menus): real frames only
    if (!framegenInputs(vpW_, vpH_, &in)) return;
    if (!fgDepthProg_) {
        GLuint vs = compileShader(GL_VERTEX_SHADER, kFgVS, "fg.vs");
        GLuint fd = vs ? compileShader(GL_FRAGMENT_SHADER, kFgDepthFS, "fg_depth.fs") : 0;
        GLuint fv = vs ? compileShader(GL_FRAGMENT_SHADER, kFgVelFS, "fg_vel.fs") : 0;
        fgDepthProg_ = fd ? linkProgram(vs, fd, "fg_depth") : 0;
        fgVelProg_ = fv ? linkProgram(vs, fv, "fg_vel") : 0;
        GenFramebuffers(1, &fgFbo_);
        if (!fgDepthProg_ || !fgVelProg_) { LOG_WARN("FRAMEGEN: input shaders unavailable"); return; }
    }
    glx::gpuStamp("FRAMEGEN: inputs");
    GLint prevProg = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    GLint prevRead = 0, prevDraw = 0;
    glGetIntegerv(0x8CAA /*GL_READ_FRAMEBUFFER_BINDING*/, &prevRead); glGetIntegerv(0x8CA6 /*GL_DRAW_FRAMEBUFFER_BINDING*/, &prevDraw);
    const GLboolean depthOn = glIsEnabled(GL_DEPTH_TEST), blendOn = glIsEnabled(GL_BLEND), cullOn = glIsEnabled(GL_CULL_FACE),
                    scissorOn = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
    // HUD-less: the window as it stands now (post + HUD screen effects), flipped
    BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER, fgFbo_);
    FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, in.hudless, 0);
    BlitFramebuffer(0, 0, outW, outH, 0, outH, outW, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    // depth / motion: flipped full-screen passes at the render size
    ensureSceneDepth();
    BindFramebuffer(GL_FRAMEBUFFER, fgFbo_);
    glViewport(0, 0, vpW_, vpH_);
    BindVertexArray(postVao_);
    ActiveTexture(GL_TEXTURE0);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, in.depth, 0);
    UseProgram(fgDepthProg_);
    glBindTexture(GL_TEXTURE_2D, depthCopyTex_);
    Uniform1i(cachedUniformLocation(fgDepthProg_, "uSrc"), 0);
    Uniform1i(cachedUniformLocation(fgDepthProg_, "uH"), vpH_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, in.velocity, 0);
    UseProgram(fgVelProg_);
    glBindTexture(GL_TEXTURE_2D, velTex_);
    Uniform1i(cachedUniformLocation(fgVelProg_, "uSrc"), 0);
    Uniform1i(cachedUniformLocation(fgVelProg_, "uH"), vpH_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    BindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    BindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead); BindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevDraw);
    glViewport(0, 0, outW, outH);
    if (depthOn) glEnable(GL_DEPTH_TEST);
    if (blendOn) glEnable(GL_BLEND);
    if (cullOn) glEnable(GL_CULL_FACE);
    if (scissorOn) glEnable(GL_SCISSOR_TEST);
    UseProgram((GLuint)prevProg);
    glx::uniformCacheForgetCurrent();
    // camera / timing (camView_ = lookAt: rows 0 / 1 / 2 = right / up / -forward, column-major m[col * 4 + row])
    FgParams p;
    p.renderW = vpW_; p.renderH = vpH_;
    if (temporalActive()) { p.jitterX = jitterPx_[0]; p.jitterY = -jitterPx_[1]; }
    const auto now = std::chrono::steady_clock::now();
    static const bool lockstep = std::getenv("WFC_LOCKSTEP") != nullptr;
    p.frameTimeMs = lockstep || fgLastFrame_.time_since_epoch().count() == 0 ? 1000.0f / 60.0f
                  : std::chrono::duration<float, std::milli>(now - fgLastFrame_).count();
    fgLastFrame_ = now;
    p.reset = cameraCut_;
    p.nearM = znear_; p.farM = zfar_;
    p.fovY = 2.0f * std::atan(1.0f / std::max(camProj_.m[5], 1e-6f));
    const float* v = camView_.m;
    for (int k = 0; k < 3; ++k) {
        p.camRight[k] = v[k * 4 + 0]; p.camUp[k] = v[k * 4 + 1]; p.camFwd[k] = -v[k * 4 + 2];
    }
    p.camPos[0] = camPos_.x; p.camPos[1] = camPos_.y; p.camPos[2] = camPos_.z;
    framegenParams(p);
    static bool logged = false;
    if (!logged) { LOG_INFO("FRAMEGEN: inputs %dx%d (render) / %dx%d (HUD-less)", vpW_, vpH_, outW, outH); logged = true; }
}

}  // namespace wfc
}  // namespace render
