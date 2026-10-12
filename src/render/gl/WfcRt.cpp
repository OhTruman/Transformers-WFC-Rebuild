// Clean-room reconstruction — M7 stage 0 groundwork, the GL side (PC ADAPTATION; OFF by default). WFC_RT_ROUNDTRIP=1:
// after the world depth prepass (the depth RT shadows / AO will read), the scene depth goes to the D3D12 bridge as R32F
// (render/d3d12/D3D12RtBridge.h), the stand-in D3D12 job runs and the world shading waits for its mask on the GPU - the
// mid-frame round trip's cost, measured before any ray-traced pass exists (Systems' M7 plan: architecture A).
#include "render/gl/WfcPipeline.h"
#include "render/d3d12/D3D12RtBridge.h"
#include "core/Log.h"

#include <cstdlib>

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
}  // namespace

void Pipeline::rtRoundTrip() {
    static const bool on = std::getenv("WFC_RT_ROUNDTRIP") != nullptr;
    if (!on || rtFailed_) return;
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
    rtBridgeRoundTrip();                                   // GL -> D3D12 job -> GL (the shading below waits for the mask)
    glColorMask(cm[0], cm[1], cm[2], cm[3]);
    if (depthOn) glEnable(GL_DEPTH_TEST);
    if (blendOn) glEnable(GL_BLEND);
    if (cullOn) glEnable(GL_CULL_FACE);
    UseProgram((GLuint)prevProg);
    glx::uniformCacheForgetCurrent();
}

}  // namespace wfc
}  // namespace render
