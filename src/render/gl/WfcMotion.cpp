// Clean-room reconstruction — optional motion vectors for temporal upscalers / frame generation (PC ADAPTATION,
// OFF by default; nothing here runs unless enabled, so the original presentation is untouched).
//
// Per-pixel velocity at the 3D render size, in UV units pointing from the current pixel to where its surface was in
// the previous frame (prevUV - curUV; the convention FSR 3 / DLSS accept with a scale). Static geometry: reprojected
// from the opaque depth with the previous frame's view-projection (one full-screen pass after the opaque passes).
// Moving objects (characters, movers, vehicles) overwrite their pixels with their own motion (object pass, next step).
// Translucent FX keep the background's velocity; temporal upscalers get a reactive mask for them (A3).
// WFC_MOTIONVECTORS=1 enables; WFC_MOTIONVIEW=1 draws |velocity| over the frame (diagnostics).
#include "render/gl/WfcPipeline.h"
#include "core/Log.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace render {
namespace wfc {
using namespace glx;

GLuint compileShader(GLenum type, const std::string& src, const std::string& tag);   // WfcPipeline.cpp
GLuint linkProgram(GLuint vs, GLuint fs, const std::string& tag);

namespace {
const char* kMotionVS = R"(#version 430
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }
)";
// static geometry: depth -> world (inverse current view-projection) -> previous clip -> previous UV
const char* kCameraFS = R"(#version 430
uniform sampler2D uDepth;
uniform mat4 uInvVP, uPrevVP;
uniform vec2 uSize;
out vec2 oVel;
void main() {
    vec2 uv = gl_FragCoord.xy / uSize;
    float d = texelFetch(uDepth, ivec2(gl_FragCoord.xy), 0).r;
    vec4 w = uInvVP * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    w /= w.w;
    vec4 p = uPrevVP * w;
    vec2 puv = p.xy / p.w * 0.5 + 0.5;
    oVel = puv - uv;
}
)";
const char* kViewFS = R"(#version 430
uniform sampler2D uVel;
uniform vec2 uSize;
out vec4 oColor;
void main() {
    vec2 v = texture(uVel, gl_FragCoord.xy / uSize).xy;
    oColor = vec4(abs(v) * 40.0, 0.0, 1.0);      // 2.5 % of the screen per frame = full intensity
}
)";

// characters: the displayed skinned pose now and in the previous frame (the main VS's skinning: assets::skinPose sum,
// blended with the previous simulation step by alpha), each through its frame's model and view-projection
const char* kObjectVS = R"(#version 430
layout(location=0) in vec3 aPos;
layout(location=9) in vec4 aJoints;
layout(location=10) in vec4 aWeights;
uniform sampler2D uCurTex, uPrevTex;
uniform int uRow, uBones, uMode, uPrevSrc, uPrevMode;
uniform float uAlpha, uPrevAlpha;
uniform mat4 uVP, uPrevVP, uModel, uPrevModel;
out vec4 vCur, vPrev;
mat4 bone(sampler2D t, int base, int j) {
    return mat4(texelFetch(t, ivec2(base + j * 4, uRow), 0), texelFetch(t, ivec2(base + j * 4 + 1, uRow), 0),
                texelFetch(t, ivec2(base + j * 4 + 2, uRow), 0), texelFetch(t, ivec2(base + j * 4 + 3, uRow), 0));
}
vec3 skin(sampler2D t, int base) {
    vec3 p = vec3(0.0);
    for (int k = 0; k < 4; ++k) {
        float w = aWeights[k];
        int j = int(aJoints[k] + 0.5);
        if (w <= 0.0 || j >= uBones) continue;
        p += (bone(t, base, j) * vec4(aPos, 1.0)).xyz * w;
    }
    return p;
}
vec3 pose(sampler2D t, int mode, float a) {
    vec3 p = skin(t, 0);
    if (mode == 2) { vec3 pp = skin(t, 512); p = pp + (p - pp) * a; }
    return p;
}
void main() {
    vec3 c = pose(uCurTex, uMode, uAlpha);
    vec3 pr = uPrevSrc == 1 ? pose(uPrevTex, uPrevMode, uPrevAlpha) : pose(uCurTex, uPrevMode, uPrevAlpha);
    vCur = uVP * (uModel * vec4(c, 1.0));
    vPrev = uPrevVP * (uPrevModel * vec4(pr, 1.0));
    gl_Position = vCur;
}
)";
const char* kObjectFS = R"(#version 430
in vec4 vCur, vPrev;
out vec2 oVel;
void main() { oVel = (vPrev.xy / vPrev.w * 0.5 + 0.5) - (vCur.xy / vCur.w * 0.5 + 0.5); }
)";

bool invert(const float* m, float* out) {        // column-major 4x4 (core::Mat4)
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const double det = (double)m[0] * inv[0] + (double)m[1] * inv[4] + (double)m[2] * inv[8] + (double)m[3] * inv[12];
    if (std::fabs(det) < 1e-30) return false;
    for (int i = 0; i < 16; ++i) out[i] = (float)(inv[i] / det);
    return true;
}
}  // namespace

bool Pipeline::motionVectorsOn() const {
    static const bool env = std::getenv("WFC_MOTIONVECTORS") != nullptr || std::getenv("WFC_MOTIONVIEW") != nullptr;
    return motionOn_ || env;
}

// after the opaque passes (depth complete): the static-geometry velocity into velTex_ (render size)
void Pipeline::motionCameraPass() {
    if (!motionVectorsOn() || !depthCopyFbo_) return;
    if (!velCameraProg_) {
        GLuint vs = compileShader(GL_VERTEX_SHADER, kMotionVS, "motion.vs");
        GLuint fc = vs ? compileShader(GL_FRAGMENT_SHADER, kCameraFS, "motion_camera.fs") : 0;
        GLuint fv = vs ? compileShader(GL_FRAGMENT_SHADER, kViewFS, "motion_view.fs") : 0;
        velCameraProg_ = fc ? linkProgram(vs, fc, "motion_camera") : 0;
        velViewProg_ = fv ? linkProgram(vs, fv, "motion_view") : 0;
        if (!velCameraProg_) { LOG_WARN("wfc: motion vector shaders unavailable"); return; }
        LOG_INFO("MOTION: motion vectors on (camera reprojection of static geometry; render size %dx%d)", vpW_, vpH_);
    }
    if (!velFbo_ || velW_ != vpW_ || velH_ != vpH_) {
        if (!velFbo_) { GenFramebuffers(1, &velFbo_); glGenTextures(1, &velTex_); }
        glBindTexture(GL_TEXTURE_2D, velTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, 0x822F /*GL_RG16F*/, vpW_, vpH_, 0, 0x8227 /*GL_RG*/, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        BindFramebuffer(GL_FRAMEBUFFER, velFbo_);
        FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, velTex_, 0);
        velW_ = vpW_; velH_ = vpH_;
    }
    ensureSceneDepth();
    float inv[16];
    if (!invert(viewProj_.m, inv)) return;
    // state: the caller (opaque -> translucency) restores what it needs; keep the scene FBO bound afterwards
    GLint prevProg = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    const GLboolean depthOn = glIsEnabled(GL_DEPTH_TEST), blendOn = glIsEnabled(GL_BLEND), cullOn = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    BindFramebuffer(GL_FRAMEBUFFER, velFbo_);
    glViewport(0, 0, vpW_, vpH_);
    UseProgram(velCameraProg_);
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, depthCopyTex_);
    Uniform1i(GetUniformLocation(velCameraProg_, "uDepth"), 0);
    UniformMatrix4fv(GetUniformLocation(velCameraProg_, "uInvVP"), 1, GL_FALSE, inv);
    const core::Mat4& pvp = havePrevVP_ ? prevViewProj_ : viewProj_;   // first frame: zero velocity
    UniformMatrix4fv(GetUniformLocation(velCameraProg_, "uPrevVP"), 1, GL_FALSE, pvp.m);
    Uniform2f(GetUniformLocation(velCameraProg_, "uSize"), (float)vpW_, (float)vpH_);
    BindVertexArray(postVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    BindVertexArray(0);
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    if (depthOn) glEnable(GL_DEPTH_TEST);
    if (blendOn) glEnable(GL_BLEND);
    if (cullOn) glEnable(GL_CULL_FACE);
    UseProgram((GLuint)prevProg);
    glx::uniformCacheForgetCurrent();
    velValidFrame_ = frameNo_;
}

// the characters' own motion over the camera velocity: their visible pixels only (scene depth, LEQUAL with a small
// bias toward the camera so the separately compiled VS still passes where the main pass wrote the depth)
void Pipeline::motionObjectPass() {
    if (!motionVectorsOn() || motionDraws_.empty() || velValidFrame_ != frameNo_) return;
    if (!velObjProg_) {
        GLuint vs = compileShader(GL_VERTEX_SHADER, kObjectVS, "motion_object.vs");
        GLuint fs = vs ? compileShader(GL_FRAGMENT_SHADER, kObjectFS, "motion_object.fs") : 0;
        velObjProg_ = fs ? linkProgram(vs, fs, "motion_object") : 0;
        if (!velObjProg_) { LOG_WARN("wfc: character motion vector shader unavailable"); motionDraws_.clear(); return; }
    }
    GLint prevProg = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    const GLboolean depthOn = glIsEnabled(GL_DEPTH_TEST), blendOn = glIsEnabled(GL_BLEND), cullOn = glIsEnabled(GL_CULL_FACE);
    GLboolean depthMask = GL_TRUE; glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    BindFramebuffer(GL_FRAMEBUFFER, velFbo_);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex_, 0);   // read-only below
    glViewport(0, 0, vpW_, vpH_);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
    glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f, -4.0f);
    UseProgram(velObjProg_);
    auto U = [&](const char* n) { return GetUniformLocation(velObjProg_, n); };
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, skinTex_);
    ActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, skinPrevTex_ ? skinPrevTex_ : skinTex_);
    ActiveTexture(GL_TEXTURE0);
    Uniform1i(U("uCurTex"), 0); Uniform1i(U("uPrevTex"), 1);
    UniformMatrix4fv(U("uVP"), 1, GL_FALSE, viewProj_.m);
    UniformMatrix4fv(U("uPrevVP"), 1, GL_FALSE, (havePrevVP_ ? prevViewProj_ : viewProj_).m);
    for (const MotionDraw& d : motionDraws_) {
        Uniform1i(U("uRow"), d.row); Uniform1i(U("uBones"), d.bones); Uniform1i(U("uMode"), d.mode);
        Uniform1f(U("uAlpha"), d.alpha);
        Uniform1i(U("uPrevSrc"), d.prevSrc && skinPrevTex_ ? 1 : 0); Uniform1i(U("uPrevMode"), d.prevMode);
        Uniform1f(U("uPrevAlpha"), d.prevAlpha);
        UniformMatrix4fv(U("uModel"), 1, GL_FALSE, d.model.m);
        UniformMatrix4fv(U("uPrevModel"), 1, GL_FALSE, d.prevModel.m);
        BindVertexArray(d.vao);
        glDrawElements(GL_TRIANGLES, d.count, GL_UNSIGNED_INT, nullptr);
    }
    BindVertexArray(0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, vpW_, vpH_);
    glDepthMask(depthMask);
    if (!depthOn) glDisable(GL_DEPTH_TEST);
    if (blendOn) glEnable(GL_BLEND);
    if (cullOn) glEnable(GL_CULL_FACE);
    UseProgram((GLuint)prevProg);
    glx::uniformCacheForgetCurrent();
    motionDraws_.clear();
}

// WFC_MOTIONVIEW=1: |velocity| over the window (after post / upscaling, before the HUD)
void Pipeline::motionDebugView(int outW, int outH) {
    static const bool view = std::getenv("WFC_MOTIONVIEW") != nullptr;
    if (!view || !velViewProg_ || velValidFrame_ != frameNo_) return;
    BindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, outW, outH);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    UseProgram(velViewProg_);
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, velTex_);
    Uniform1i(GetUniformLocation(velViewProg_, "uVel"), 0);
    Uniform2f(GetUniformLocation(velViewProg_, "uSize"), (float)outW, (float)outH);
    BindVertexArray(postVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

}  // namespace wfc
}  // namespace render
