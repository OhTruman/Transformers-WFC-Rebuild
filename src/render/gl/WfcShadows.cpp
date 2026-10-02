// WFC modulated projected shadows: the non-native stages, reproduced from the cooked Xenon shaders.
//
//  * Caster depth (shadow depth VS, BASE shader cache): depth = saturate(shadowZ * InvMaxSubjectDepth
//    + DepthBias), written as clip z = depth * w; opaque casters write depth only, masked casters clip on
//    OpacityMaskClipValue (material variant |SHADOW). [CONFIRMED microcode]
//  * Projection (engine PS, branching PCF, selected by Xe-TransEngine.ini bEnableBranchingPCFShadows=True,
//    ShadowFilterQuality=0): per-pixel rotation from EngineMaterials.RandomAngles (64x64, nearest,
//    SRGB False; cos/sin = fetch.xy * 2 - 1); 4 rotated edge samples + centre, each edge depth biased by
//    |offset|^2 * ShadowDepthBias, receiver depth clamped to 0.999, lit = average of 5; when
//    0.0001 < lit < 0.9999, 12 unrotated refining samples (same bias rule): lit = (5 lit + sum12) / 17;
//    atten = spot^2 * (1 - saturate(|d/R|^2)^ShadowFalloffExponent); output (modulate blend)
//    lerp(lerp(1, ShadowModulateColor, atten), 1, lit). [CONFIRMED microcode]
//  * Resolution clamp(…, MinShadowResolution 128, MaxShadowResolution 1024), ShadowTexelsPerPixel 1.
//    [CONFIRMED ini values; screen-size rule HIGH]
//
// Native inputs still UNKNOWN (ReVa): which light(s) cast each character's shadow (WFC
// LightEnvironmentComponent composite shadow), EdgeSampleOffsets / RefiningSampleOffsets tables,
// ShadowDepthBias / DepthBias values, the subject projection fit and the fade. Until they arrive no
// shadow is projected; WFC_SHADOWTEST=<light name> exercises the path with zero offsets and zero bias
// (a hard, unfiltered shadow) for testing only.
#include "render/gl/WfcPipeline.h"
#include "render/gl/GLExt.h"
#include "platform/Image.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace render {
namespace wfc {

using namespace glx;

GLuint compileShader(GLenum type, const std::string& src, const std::string& tag);
GLuint linkProgram(GLuint vs, GLuint fs, const std::string& tag);

namespace {
const char* kProjVS = R"(#version 330 core
out vec2 vUV;
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); vUV = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }
)";
const char* kProjFS = R"(#version 330 core
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uSceneDepth;
uniform sampler2D uShadowMap;
uniform sampler2D uRandomAngles;
uniform mat4 uInvViewProj;
uniform mat4 uShadowViewProj;      // world -> shadow clip (uv after perspective divide)
uniform vec4 uShadowZ;             // world -> shadow-space z (row)
uniform float uInvMaxSubjectDepth;
uniform float uShadowDepthBias;    // native value UNKNOWN
uniform vec2 uEdge[4];             // native EdgeSampleOffsets UNKNOWN
uniform vec2 uRefine[12];          // native RefiningSampleOffsets UNKNOWN
uniform float uInvRandomAngleTextureSize;
uniform vec4 uModColor;
uniform vec4 uLightPosInvRadius;   // UE units
uniform vec3 uLightDirUE;
uniform vec2 uSpotAngles;          // x = cos outer, y = 1 / (cos inner - cos outer); spot only
uniform int uIsSpot;
uniform float uFalloffExponent;
float depthAt(vec2 uv) { return texture(uShadowMap, uv).r; }
void main() {
    float d = texture(uSceneDepth, vUV).r;
    vec4 wp = uInvViewProj * vec4(vUV * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    wp /= wp.w;
    vec4 sp = uShadowViewProj * vec4(wp.xyz, 1.0);
    vec2 uv = sp.xy / sp.w * 0.5 + 0.5;
    // the projection is rasterized over the shadow frustum only [HIGH: UE3 projects the frustum volume]
    if (sp.w <= 0.0 || any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) { oColor = vec4(1.0); return; }
    float receiver = min(dot(uShadowZ, vec4(wp.xyz, 1.0)) * uInvMaxSubjectDepth, 0.999);
    vec2 r = texture(uRandomAngles, gl_FragCoord.xy * uInvRandomAngleTextureSize).xy * 2.0 - 1.0;   // (C, S)
    float sum = (depthAt(uv) >= receiver) ? 1.0 : 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 o = uEdge[i];
        vec2 ro = vec2(r.x * o.x + r.y * o.y, -r.y * o.x + r.x * o.y);
        sum += (depthAt(uv + ro) + dot(o, o) * uShadowDepthBias >= receiver) ? 1.0 : 0.0;
    }
    float lit = sum * 0.2;
    if (lit > 0.0001 && lit < 0.9999) {
        float s12 = 0.0;
        for (int i = 0; i < 12; ++i) {
            vec2 o = uRefine[i];
            s12 += (depthAt(uv + o) + dot(o, o) * uShadowDepthBias >= receiver) ? 1.0 : 0.0;
        }
        lit = (lit * 5.0 + s12) * (1.0 / 17.0);
    }
    vec3 wue = wp.xzy * 100.0;
    vec3 L = uLightPosInvRadius.xyz - wue;
    vec3 dn = L * uLightPosInvRadius.w;
    float radial = 1.0 - exp2(log2(max(clamp(dot(dn, dn), 0.0, 1.0), 0.0001)) * uFalloffExponent);
    float spot = 1.0;
    if (uIsSpot != 0) spot = clamp((dot(normalize(L), -uLightDirUE) - uSpotAngles.x) * uSpotAngles.y, 0.0, 1.0);
    float atten = spot * spot * radial;
    vec4 shadowed = mix(vec4(1.0), uModColor, atten);
    oColor = mix(shadowed, vec4(1.0), lit);
}
)";

core::Mat4 inverse4(const core::Mat4& a) {
    const float* m = a.m;
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
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    core::Mat4 r;
    float id = std::fabs(det) > 1e-30f ? 1.0f / det : 0.0f;
    for (int i = 0; i < 16; ++i) r.m[i] = inv[i] * id;
    return r;
}
} // namespace

// Native light selection is blocked (ReVa). Test hook: WFC_SHADOWTEST=<light component name>.
int Pipeline::shadowLightFor() const {
    static const char* test = std::getenv("WFC_SHADOWTEST");
    if (test) {
        for (size_t i = 0; i < lights_.size(); ++i)
            if (lights_[i].name == test && lights_[i].type != 3) return (int)i;
        return -1;
    }
    // WFC composite-shadow light (DirectLightEnv, native b52dca9). Projection stays opt-in until the
    // native ShadowDepthBias / DepthBias / PCF offset tables are recovered (zero values self-shadow).
    static const bool on = std::getenv("WFC_CHARSHADOWS") != nullptr;
    if (!on || envForm_ < 0) return -1;
    auto it = dle_.find(envForm_);
    return it == dle_.end() ? -1 : it->second.shadowLight;
}

// Render the current dynamic caster (already uploaded in dynVao_) into a shadow depth map and queue its
// projection. Subject fit: perspective from the light through the caster's bounding sphere (point/spot),
// orthographic along the light for directional lights [PROV: native UE3 subject fit].
void Pipeline::renderShadowDepth(GpuMesh& g, const core::Mat4& model) {
    int li = shadowLightFor();
    if (li < 0 || shadowRequests_.size() >= 4) return;
    const Light& l = lights_[(size_t)li];
    core::Vec3 c = envBoundsCenter_;
    float radius = std::max(core::length(envBoundsExtent_), 0.05f);
    core::Vec3 eye, fwd;
    core::Mat4 proj;
    float nearZ;
    if (l.type == 2) {
        fwd = core::normalize(l.dir);
        eye = c - fwd * (radius * 4.0f);
        nearZ = radius * 3.0f;
        float a = 1.0f / radius, b = -2.0f / (2.0f * radius);
        proj = core::Mat4::identity();
        proj.m[0] = a; proj.m[5] = a; proj.m[10] = b; proj.m[14] = -(radius * 5.0f + radius * 3.0f) / (2.0f * radius);
    } else {
        eye = l.pos;
        core::Vec3 to = c - eye;
        float dist = core::length(to);
        if (dist <= radius * 1.01f) return;   // light inside the subject: no projected shadow
        fwd = to * (1.0f / dist);
        float halfFov = std::asin(std::min(radius / dist, 0.99f));
        nearZ = dist - radius;
        proj = core::Mat4::perspective(2.0f * halfFov, 1.0f, std::max(nearZ, 0.01f), dist + radius);
    }
    core::Vec3 up = std::fabs(fwd.y) > 0.95f ? core::Vec3{1, 0, 0} : core::Vec3{0, 1, 0};
    core::Mat4 view = core::Mat4::lookAt(eye, eye + fwd, up);
    ShadowRequest rq;
    rq.light = li;
    rq.viewProj = proj * view;
    // shadow-space z: distance along the light axis from the subject's near plane
    rq.zRow[0] = fwd.x; rq.zRow[1] = fwd.y; rq.zRow[2] = fwd.z;
    rq.zRow[3] = -core::dot(fwd, eye) - nearZ;
    rq.invMaxSubjectDepth = 1.0f / (2.0f * radius);
    rq.depthBias = 0.0f;                         // native DepthBias UNKNOWN
    if (std::getenv("WFC_SHADOWTEST")) {
        for (int k = 0; k < 4; ++k) rq.modColor[k] = l.modShadowColor[k];
    } else {   // shadow factor = 1 - strength (ShadowLuminanceScale 0) -> ShadowModulateColor [PARTIAL use]
        float fct = 1.0f - dle_.at(envForm_).shadowStrength;
        rq.modColor[0] = rq.modColor[1] = rq.modColor[2] = fct; rq.modColor[3] = 1.0f;
    }
    // resolution: projected subject diameter in pixels * ShadowTexelsPerPixel 1, clamped to [128, 1024]
    float camDist = std::max(core::length(c - camPos_), 0.01f);
    float pixels = 2.0f * radius / camDist * (float)vpH_ / (2.0f * std::tan(0.5f * 1.3962634f));
    rq.res = std::min(std::max((int)std::ceil(pixels), 128), 1024);
    // target
    if (!shadowFbo_) GenFramebuffers(1, &shadowFbo_);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, rq.res, rq.res, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    rq.tex = tex;
    BindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex, 0);
    glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
    glViewport(0, 0, rq.res, rq.res);
    glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    core::Mat4 savedVP = viewProj_;
    viewProj_ = rq.viewProj;
    BindVertexArray(g.vao);
    for (const Sub& s : g.subs) {
        if (s.prog < 0 || progs_[(size_t)s.prog].shadowProg < 0) continue;
        const Program& S = progs_[(size_t)progs_[(size_t)s.prog].shadowProg];
        bindCommon(S, model);
        Uniform4f(GetUniformLocation(S.id, "uShadowDepth"), 1.0f, rq.invMaxSubjectDepth, rq.depthBias, 0.0f);
        Uniform4f(GetUniformLocation(S.id, "uShadowZ"), rq.zRow[0], rq.zRow[1], rq.zRow[2], rq.zRow[3]);
        if (S.twoSided) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
        glDrawElements(GL_TRIANGLES, (GLsizei)s.count, GL_UNSIGNED_INT, (void*)(size_t)(s.first * 4));
        Uniform4f(GetUniformLocation(S.id, "uShadowDepth"), 0.0f, 0.0f, 0.0f, 0.0f);
    }
    BindVertexArray(0);
    viewProj_ = savedVP;
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, vpW_, vpH_);
    shadowRequests_.push_back(rq);
}

// Project the queued shadows onto the scene (modulate blend), after the scene and before post.
void Pipeline::applyShadows() {
    if (shadowRequests_.empty()) return;
    if (!shadowProjProg_) {
        GLuint v = compileShader(GL_VERTEX_SHADER, kProjVS, "shadowproj.vs");
        GLuint f = compileShader(GL_FRAGMENT_SHADER, kProjFS, "shadowproj.fs");
        if (v && f) shadowProjProg_ = linkProgram(v, f, "shadowproj");
        if (!shadowProjProg_) { shadowRequests_.clear(); return; }
        ImageData img;
        std::string path = std::string("F:/Transformers Rebuild/ExtractedAssets/content/EngineMaterials/RandomAngles.png");
        if (platform::decodeImage(path, img)) {
            glGenTextures(1, &randomAnglesTex_);
            glBindTexture(GL_TEXTURE_2D, randomAnglesTex_);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);   // TF_Nearest, SRGB False
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            randomAnglesSize_ = img.w;
        } else {
            LOG_WARN("wfc shadows: %s not found", path.c_str());
        }
    }
    depthDirty_ = true;
    ensureSceneDepth();
    core::Mat4 invVP = inverse4(viewProj_);
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, vpW_, vpH_);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR, GL_ZERO);   // modulated shadow
    UseProgram(shadowProjProg_);
    auto U = [&](const char* n) { return GetUniformLocation(shadowProjProg_, n); };
    Uniform1i(U("uSceneDepth"), 0); Uniform1i(U("uShadowMap"), 1); Uniform1i(U("uRandomAngles"), 2);
    UniformMatrix4fv(U("uInvViewProj"), 1, GL_FALSE, invVP.m);
    Uniform1f(U("uInvRandomAngleTextureSize"), randomAnglesSize_ > 0 ? 1.0f / (float)randomAnglesSize_ : 0.0f);
    Uniform1f(U("uShadowDepthBias"), 0.0f);      // native value UNKNOWN
    float zeros[24] = {0};                        // native offset tables UNKNOWN
    Uniform2fv(U("uEdge"), 4, zeros);
    Uniform2fv(U("uRefine"), 12, zeros);
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, depthCopyTex_);
    ActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, randomAnglesTex_);
    BindVertexArray(postVao_);
    for (const ShadowRequest& rq : shadowRequests_) {
        const Light& l = lights_[(size_t)rq.light];
        UniformMatrix4fv(U("uShadowViewProj"), 1, GL_FALSE, rq.viewProj.m);
        Uniform4f(U("uShadowZ"), rq.zRow[0], rq.zRow[1], rq.zRow[2], rq.zRow[3]);
        Uniform1f(U("uInvMaxSubjectDepth"), rq.invMaxSubjectDepth);
        Uniform4f(U("uModColor"), rq.modColor[0], rq.modColor[1], rq.modColor[2], rq.modColor[3]);
        Uniform4f(U("uLightPosInvRadius"), l.pos.x * 100.0f, l.pos.z * 100.0f, l.pos.y * 100.0f,
                  l.type == 2 ? 0.0f : 1.0f / (l.radius * 100.0f));
        Uniform3f(U("uLightDirUE"), l.dir.x, l.dir.z, l.dir.y);
        Uniform2f(U("uSpotAngles"), l.cosOuter, l.invConeRange);
        Uniform1i(U("uIsSpot"), l.type == 1 ? 1 : 0);
        Uniform1f(U("uFalloffExponent"), l.shadowFalloffExponent);
        ActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, rq.tex);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    BindVertexArray(0);
    UseProgram(0);
    ActiveTexture(GL_TEXTURE0);
    glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
    for (const ShadowRequest& rq : shadowRequests_) glDeleteTextures(1, &rq.tex);
    shadowRequests_.clear();
}

} // namespace wfc
} // namespace render
