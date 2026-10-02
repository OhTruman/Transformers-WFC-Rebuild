// WFC character shadows: projected composite shadow -> ShadowMask -> character UberLight pass.
//
// Native path (ReverseEngineering 13c0953, notes/MILESTONE03_RENDERING_CHARACTER_SHADOW_PATH.md):
//  * ShadowMask: A8R8G8B8 at the scene buffer size, SizeX / f x SizeY / f with f = (SizeX > 960) ? 2 : 1
//    (FSceneRenderTargets size setup 0x8302AE28, read from the image); cleared once per build to
//    (1,1,1,1); stores VISIBILITY (1 = lit). [CONFIRMED]
//  * Per projected shadow (RenderProjection 0x8304EF70): colour writes off, z-fail stencil volume of the
//    8 frustum corners (front zfail Inc, back zfail Dec, Always); then the frustum box drawn where
//    stencil != 0 with blend dest.rgb = dest.rgb * src.rgb, dest.a unchanged (0x82CF8048); stencil
//    cleared afterwards. Every shadow multiplies into the same mask. [CONFIRMED]
//  * Gate: light render flags bit 0x4 and the DPG bit (bit 5 + DPG). [CONFIRMED; flag source UNKNOWN]
//  * Resolve, then the 2-pass separable blur (H, V) only if something was drawn (0x82DDB070). Kernel
//    weights UNKNOWN: the blur is not run (no invented kernel). [placement CONFIRMED / weights UNKNOWN]
//  * Read by the character pass: .r at screen UV + ShadowMaskTexelOffset (0.5 / mask size). [CONFIRMED]
//  * BranchingPCF projection (bEnableBranchingPCFShadows = True): Res = clamp(MaxShadowResolution 1024,
//    1, 2048); Edge/Refining offsets = native tables * ShadowFilterRadius 6 / Res; ShadowDepthBias =
//    (Res * MaskedShadowsDepthBias 0.2 / ShadowFilterRadius)^2 = 1165.08 (parabolic); no slope / receiver
//    bias; DepthBias (0) unused. [CONFIRMED CPU side]
//  * Projection PS math (engine BranchingPCF microcode, decoded earlier): rotation from RandomAngles
//    (64x64, nearest; cos/sin = fetch.xy * 2 - 1); 4 rotated edge taps + centre, tap depth + |o|^2 *
//    ShadowDepthBias, receiver clamped 0.999, lit = mean of 5; when 0.0001 < lit < 0.9999, 12 refining
//    taps: lit = (5 lit + sum12) / 17; atten = spot^2 * (1 - saturate(|d/R|^2)^ShadowFalloffExponent);
//    out = lerp(lerp(1, ShadowModulateColor, atten), 1, lit). [microcode CONFIRMED; RE 13c0953 marks the
//    in-shader bias use PARTIAL]
//  * ShadowModulateColor = lerp(1, ModShadowColor, FadeAlpha) (0x82D077F8). [CONFIRMED]
//
// Still UNKNOWN / PARTIAL, so the projection stays opt-in (WFC_CHARSHADOWS=1; WFC_SHADOWTEST=<light>):
//  * the composite record's shadowFactor -> the projected shadow's light / FadeAlpha (§1.6): the opt-in
//    path uses the light's authored ModShadowColor with FadeAlpha 1 (WFC_SHADOWFADEALPHA overrides);
//  * ScreenToShadowMatrix construction and the subject fit (here: perspective through the caster's
//    bounding sphere, rendered into a Res x Res buffer at its per-shadow resolution) [PROV];
//  * the downsampled scene depth used by the mask's stencil test (here: point-sampled scene depth);
//  * projected-shadow creation gates (distance / resolution / cast flags) - none added.
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

const float kEdgeSampleOffsets[8] = {-0.09673f, 0.99531f, 0.26020f, -0.96556f, 0.90176f, 0.43224f, -0.97680f, -0.21415f};
const float kRefiningSampleOffsets[24] = {
    0.83831f, -0.53516f, 0.25047f, -0.22063f, 0.38774f, -0.54880f, -0.51237f, 0.57005f,
    -0.90728f, 0.11358f, -0.73834f, -0.50544f, 0.08519f, 0.30562f, -0.29311f, -0.01680f,
    0.02770f, 0.70874f, 0.05331f, -0.59401f, 0.34538f, -0.90947f, -0.58627f, -0.22920f};

int shadowDepthResolution(int maxShadowResolution) { return std::min(std::max(maxShadowResolution, 1), 2048); }

float shadowDepthBiasParabolic(int res, float maskedShadowsDepthBias, float shadowFilterRadius) {
    float b = (float)res * maskedShadowsDepthBias / shadowFilterRadius;
    return b * b;
}

bool shadowProjectionAllowed(uint32_t lightRenderFlags, int dpg) {
    return (lightRenderFlags & 0x4u) != 0 && (lightRenderFlags & (1u << (5 + dpg))) != 0;
}

namespace {
const char* kFullVS = R"(#version 330 core
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }
)";
const char* kVolVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uVolViewProj;
void main() { gl_Position = uVolViewProj * vec4(aPos, 1.0); }
)";
// scene depth at the mask's resolution, for the stencil volume test [PARTIAL: native downsampled depth]
const char* kMaskDepthFS = R"(#version 330 core
uniform sampler2D uSceneDepth;
uniform vec2 uMaskSize;
void main() { gl_FragDepth = texture(uSceneDepth, gl_FragCoord.xy / uMaskSize).r; }
)";
const char* kConstFS = R"(#version 330 core
uniform vec4 uColor;
out vec4 oColor;
void main() { oColor = uColor; }
)";
const char* kProjFS = R"(#version 330 core
out vec4 oColor;
uniform sampler2D uSceneDepth;
uniform sampler2D uShadowMap;
uniform sampler2D uRandomAngles;
uniform vec2 uMaskSize;
uniform mat4 uInvViewProj;
uniform mat4 uShadowViewProj;      // world -> shadow clip [PROV: ScreenToShadowMatrix not recovered]
uniform float uSubRect;            // per-shadow resolution / ShadowBufferSize
uniform vec4 uShadowZ;             // world -> shadow-space z (row)
uniform float uInvMaxSubjectDepth;
uniform float uShadowDepthBias;    // (Res * 0.2 / 6)^2
uniform vec2 uEdge[4];             // EdgeSampleOffsets * 6 / Res
uniform vec2 uRefine[12];          // RefiningSampleOffsets * 6 / Res
uniform float uInvRandomAngleTextureSize;
uniform vec4 uModColor;            // ShadowModulateColor
uniform vec4 uLightPosInvRadius;   // UE units
uniform vec3 uLightDirUE;
uniform vec2 uSpotAngles;          // x = cos outer, y = 1 / (cos inner - cos outer); spot only
uniform int uIsSpot;
uniform float uFalloffExponent;
float depthAt(vec2 uv) { return texture(uShadowMap, uv).r; }
void main() {
    vec2 suv = gl_FragCoord.xy / uMaskSize;
    float d = texture(uSceneDepth, suv).r;
    vec4 wp = uInvViewProj * vec4(suv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    wp /= wp.w;
    vec4 sp = uShadowViewProj * vec4(wp.xyz, 1.0);
    vec2 uv = (sp.xy / sp.w * 0.5 + 0.5) * uSubRect;
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

// The light casting the character's projected shadow: the WFC_SHADOWTEST light, else (WFC_CHARSHADOWS)
// the DirectLightEnv composite-shadow light (b52dca9; baked lights are eligible).
int Pipeline::shadowLightFor() const {
    static const char* test = std::getenv("WFC_SHADOWTEST");
    if (test) {
        for (size_t i = 0; i < lights_.size(); ++i)
            if (lights_[i].name == test && lights_[i].type != 3) return (int)i;
        return -1;
    }
    static const bool on = std::getenv("WFC_CHARSHADOWS") != nullptr;
    if (!on || envForm_ < 0) return -1;
    auto it = dle_.find(envForm_);
    return it == dle_.end() ? -1 : it->second.shadowLight;
}

bool Pipeline::ensureShadowPrograms() {
    if (shadowProjProg_) return true;
    GLuint fv = compileShader(GL_VERTEX_SHADER, kFullVS, "shadowfull.vs");
    GLuint vv = compileShader(GL_VERTEX_SHADER, kVolVS, "shadowvol.vs");
    GLuint pf = compileShader(GL_FRAGMENT_SHADER, kProjFS, "shadowproj.fs");
    GLuint df = compileShader(GL_FRAGMENT_SHADER, kMaskDepthFS, "maskdepth.fs");
    GLuint cf = compileShader(GL_FRAGMENT_SHADER, kConstFS, "shadowconst.fs");
    if (!fv || !vv || !pf || !df || !cf) return false;
    maskDepthProg_ = linkProgram(fv, df, "maskdepth");
    GLuint vv2 = compileShader(GL_VERTEX_SHADER, kVolVS, "shadowvol2.vs");
    constProg_ = linkProgram(vv2, cf, "shadowconst");
    shadowProjProg_ = linkProgram(vv, pf, "shadowproj");
    if (!shadowProjProg_ || !maskDepthProg_ || !constProg_) { shadowProjProg_ = 0; return false; }
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
    GenVertexArrays(1, &volVao_);
    GenBuffers(1, &volVbo_);
    BindVertexArray(volVao_);
    BindBuffer(GL_ARRAY_BUFFER, volVbo_);
    BufferData(GL_ARRAY_BUFFER, 36 * 3 * sizeof(float), nullptr, GL_STREAM_DRAW);
    EnableVertexAttribArray(0);
    VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
    BindVertexArray(0);
    return true;
}

// ShadowMask target: RGBA8 at SizeX / f x SizeY / f, f = (SizeX > 960) ? 2 : 1, with a depth-stencil
// buffer of the same size for the stencil volume.
void Pipeline::ensureShadowMask() {
    if (maskFbo_ && maskForW_ == fbW_ && maskForH_ == fbH_) return;
    int f = fbW_ > 960 ? 2 : 1;
    maskW_ = std::max(fbW_ / f, 1);
    maskH_ = std::max(fbH_ / f, 1);
    maskForW_ = fbW_; maskForH_ = fbH_;
    if (!maskFbo_) { GenFramebuffers(1, &maskFbo_); glGenTextures(1, &maskTex_); GenRenderbuffers(1, &maskDepthRb_); }
    glBindTexture(GL_TEXTURE_2D, maskTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, maskW_, maskH_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    BindRenderbuffer(GL_RENDERBUFFER, maskDepthRb_);
    RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, maskW_, maskH_);
    BindRenderbuffer(GL_RENDERBUFFER, 0);
    BindFramebuffer(GL_FRAMEBUFFER, maskFbo_);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, maskTex_, 0);
    FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, maskDepthRb_);
    if (CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) LOG_ERROR("wfc: ShadowMask framebuffer incomplete");
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    maskClearedFrame_ = maskDrawnFrame_ = -1;
}

// BeginRenderingShadowMask: bind; the first build of the frame clears to (1,1,1,1) (visibility).
void Pipeline::beginShadowMask() {
    ensureShadowMask();
    BindFramebuffer(GL_FRAMEBUFFER, maskFbo_);
    glViewport(0, 0, maskW_, maskH_);
    if (maskClearedFrame_ != frameNo_) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        maskClearedFrame_ = frameNo_;
    }
}

void Pipeline::fillMaskDepth() {
    UseProgram(maskDepthProg_);
    Uniform1i(GetUniformLocation(maskDepthProg_, "uSceneDepth"), 0);
    Uniform2f(GetUniformLocation(maskDepthProg_, "uMaskSize"), (float)maskW_, (float)maskH_);
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, depthCopyTex_);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);
    glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_STENCIL_TEST);
    BindVertexArray(postVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    BindVertexArray(0);
    glDepthFunc(GL_LEQUAL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

// RenderProjection (0x8304EF70) for one shadow volume: z-fail stencil marking of the frustum box, then
// the box drawn once per pixel (back faces, depth test off) with stencil != 0 and the multiplicative
// mask blend; stencil cleared afterwards. `prog` is bound with its uniforms set by the caller and must
// read uVolViewProj.
void Pipeline::drawShadowVolume(const core::Vec3 corners[8], const core::Mat4& vp, GLuint prog) {
    static const int kFaces[6][4] = {{0, 2, 6, 4}, {1, 5, 7, 3}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 6, 7, 5}};
    core::Vec3 centre{0, 0, 0};
    for (int i = 0; i < 8; ++i) centre = centre + corners[i] * 0.125f;
    float tri[36 * 3];
    int n = 0;
    for (const auto& fc : kFaces) {
        core::Vec3 q[4] = {corners[fc[0]], corners[fc[1]], corners[fc[2]], corners[fc[3]]};
        core::Vec3 fcen = (q[0] + q[1] + q[2] + q[3]) * 0.25f;
        if (core::dot(core::cross(q[1] - q[0], q[2] - q[0]), fcen - centre) < 0.0f) { std::swap(q[1], q[3]); }   // CCW outward
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        for (int k : idx) { tri[n++] = q[k].x; tri[n++] = q[k].y; tri[n++] = q[k].z; }
    }
    BindVertexArray(volVao_);
    BindBuffer(GL_ARRAY_BUFFER, volVbo_);
    BufferSubData(GL_ARRAY_BUFFER, 0, sizeof(tri), tri);
    glEnable(GL_DEPTH_CLAMP);
    // stencil marking: colour writes off, two-sided z-fail Inc / Dec
    UseProgram(constProg_);
    UniformMatrix4fv(GetUniformLocation(constProg_, "uVolViewProj"), 1, GL_FALSE, vp.m);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    StencilOpSeparate(GL_FRONT, GL_KEEP, GL_INCR_WRAP, GL_KEEP);
    StencilOpSeparate(GL_BACK, GL_KEEP, GL_DECR_WRAP, GL_KEEP);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    // projection: stencil != 0, dest.rgb *= src.rgb, dest.a unchanged
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE); glCullFace(GL_FRONT);
    glEnable(GL_BLEND);
    BlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
    UseProgram(prog);
    UniformMatrix4fv(GetUniformLocation(prog, "uVolViewProj"), 1, GL_FALSE, vp.m);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    BindVertexArray(0);
    // stencil reset (RHIClear stencil = 0)
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_CLAMP);
    glDisable(GL_BLEND);
    glCullFace(GL_BACK);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
}

// Shadow depth of the current dynamic caster (in dynVao_): subject fit [PROV], rendered at its
// per-shadow resolution into the Res x Res shadow buffer (ShadowBufferSize = Res).
bool Pipeline::renderShadowDepth(GpuMesh& g, const core::Mat4& model, int li, ShadowRequest& rq) {
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
        if (dist <= radius * 1.01f) return false;   // light inside the subject: no projected shadow
        fwd = to * (1.0f / dist);
        float halfFov = std::asin(std::min(radius / dist, 0.99f));
        nearZ = dist - radius;
        proj = core::Mat4::perspective(2.0f * halfFov, 1.0f, std::max(nearZ, 0.01f), dist + radius);
    }
    core::Vec3 up = std::fabs(fwd.y) > 0.95f ? core::Vec3{1, 0, 0} : core::Vec3{0, 1, 0};
    core::Mat4 view = core::Mat4::lookAt(eye, eye + fwd, up);
    rq.light = li;
    rq.viewProj = proj * view;
    rq.zRow[0] = fwd.x; rq.zRow[1] = fwd.y; rq.zRow[2] = fwd.z;
    rq.zRow[3] = -core::dot(fwd, eye) - nearZ;
    rq.invMaxSubjectDepth = 1.0f / (2.0f * radius);
    rq.depthBias = 0.0f;                          // no depth-pass bias on this path (13c0953)
    // ShadowModulateColor = lerp(1, ModShadowColor, FadeAlpha); FadeAlpha source UNKNOWN (opt-in: 1)
    static const float fadeAlpha = std::getenv("WFC_SHADOWFADEALPHA") ? (float)std::atof(std::getenv("WFC_SHADOWFADEALPHA")) : 1.0f;
    for (int k = 0; k < 3; ++k) rq.modColor[k] = 1.0f + (l.modShadowColor[k] - 1.0f) * fadeAlpha;
    rq.modColor[3] = 1.0f + (l.modShadowColor[3] - 1.0f) * fadeAlpha;
    // per-shadow resolution: projected subject diameter * ShadowTexelsPerPixel 1, clamped [128, Res] [HIGH]
    const int Res = shadowDepthResolution(kMaxShadowResolution);
    float camDist = std::max(core::length(c - camPos_), 0.01f);
    float pixels = 2.0f * radius / camDist * (float)vpH_ / (2.0f * std::tan(0.5f * 1.3962634f));
    rq.res = std::min(std::max((int)std::ceil(pixels), 128), Res);
    if (!shadowFbo_) {
        GenFramebuffers(1, &shadowFbo_);
        glGenTextures(1, &shadowDepthTex_);
        glBindTexture(GL_TEXTURE_2D, shadowDepthTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, Res, Res, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        BindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
        FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepthTex_, 0);
        glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
    }
    BindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, Res, Res);
    glDepthMask(GL_TRUE);
    glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, rq.res, rq.res);
    glEnable(GL_DEPTH_TEST); glDisable(GL_BLEND);
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
    return true;
}

// The receiver's depth must be in the scene depth before the mask is built (the original projects
// after the depth pass). The character is laid down depth-only, pushed back slightly so the lit pass
// that follows (LEQUAL) always overwrites it with its own depth.
void Pipeline::depthPrepass(GpuMesh& g, const core::Mat4& model) {
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, vpW_, vpH_);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.0f, 1.0f);
    BindVertexArray(g.vao);
    for (const Sub& s : g.subs) {
        if (s.prog < 0 || progs_[(size_t)s.prog].shadowProg < 0) continue;
        const Program& S = progs_[(size_t)progs_[(size_t)s.prog].shadowProg];
        bindCommon(S, model);
        if (S.twoSided) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
        glDrawElements(GL_TRIANGLES, (GLsizei)s.count, GL_UNSIGNED_INT, (void*)(size_t)(s.first * 4));
    }
    BindVertexArray(0);
    glDisable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(0.0f, 0.0f);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    depthDirty_ = true;
}

void Pipeline::castCharacterShadow(GpuMesh& g, const core::Mat4& model, int li) {
    if (!ensureShadowPrograms()) return;
    static const char* flagsOverride = std::getenv("WFC_LIGHTRENDERFLAGS");
    uint32_t flags = flagsOverride ? (uint32_t)std::strtoul(flagsOverride, nullptr, 16) : lights_[(size_t)li].renderFlags;
    if (!shadowProjectionAllowed(flags, 0)) { ++statShadowGated_; return; }   // characters: SDPG_World [HIGH]
    ShadowRequest rq;
    if (!renderShadowDepth(g, model, li, rq)) return;
    depthPrepass(g, model);
    ensureSceneDepth();
    const Light& l = lights_[(size_t)li];
    const int Res = shadowDepthResolution(kMaxShadowResolution);
    const float k = kShadowFilterRadius / (float)Res;
    float edge[8], refine[24];
    for (int i = 0; i < 8; ++i) edge[i] = kEdgeSampleOffsets[i] * k;
    for (int i = 0; i < 24; ++i) refine[i] = kRefiningSampleOffsets[i] * k;
    beginShadowMask();
    fillMaskDepth();
    core::Mat4 invVP = inverse4(viewProj_);
    UseProgram(shadowProjProg_);
    auto U = [&](const char* n) { return GetUniformLocation(shadowProjProg_, n); };
    Uniform1i(U("uSceneDepth"), 0); Uniform1i(U("uShadowMap"), 1); Uniform1i(U("uRandomAngles"), 2);
    Uniform2f(U("uMaskSize"), (float)maskW_, (float)maskH_);
    UniformMatrix4fv(U("uInvViewProj"), 1, GL_FALSE, invVP.m);
    Uniform1f(U("uInvRandomAngleTextureSize"), randomAnglesSize_ > 0 ? 1.0f / (float)randomAnglesSize_ : 0.0f);
    Uniform1f(U("uShadowDepthBias"), shadowDepthBiasParabolic(Res, kMaskedShadowsDepthBias, kShadowFilterRadius));
    Uniform2fv(U("uEdge"), 4, edge);
    Uniform2fv(U("uRefine"), 12, refine);
    UniformMatrix4fv(U("uShadowViewProj"), 1, GL_FALSE, rq.viewProj.m);
    Uniform1f(U("uSubRect"), (float)rq.res / (float)Res);
    Uniform4f(U("uShadowZ"), rq.zRow[0], rq.zRow[1], rq.zRow[2], rq.zRow[3]);
    Uniform1f(U("uInvMaxSubjectDepth"), rq.invMaxSubjectDepth);
    Uniform4f(U("uModColor"), rq.modColor[0], rq.modColor[1], rq.modColor[2], rq.modColor[3]);
    Uniform4f(U("uLightPosInvRadius"), l.pos.x * 100.0f, l.pos.z * 100.0f, l.pos.y * 100.0f,
              l.type == 2 ? 0.0f : 1.0f / (l.radius * 100.0f));
    Uniform3f(U("uLightDirUE"), l.dir.x, l.dir.z, l.dir.y);
    Uniform2f(U("uSpotAngles"), l.cosOuter, l.invConeRange);
    Uniform1i(U("uIsSpot"), l.type == 1 ? 1 : 0);
    Uniform1f(U("uFalloffExponent"), l.shadowFalloffExponent);
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, depthCopyTex_);
    ActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, shadowDepthTex_);
    ActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, randomAnglesTex_);
    // the 8 frustum corners of the shadow
    core::Mat4 invShadow = inverse4(rq.viewProj);
    core::Vec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        float x = (i & 1) ? 1.0f : -1.0f, y = (i & 2) ? 1.0f : -1.0f, z = (i & 4) ? 1.0f : -1.0f;
        const float* m = invShadow.m;
        float wx = m[0] * x + m[4] * y + m[8] * z + m[12], wy = m[1] * x + m[5] * y + m[9] * z + m[13];
        float wz = m[2] * x + m[6] * y + m[10] * z + m[14], ww = m[3] * x + m[7] * y + m[11] * z + m[15];
        corners[i] = core::Vec3{wx / ww, wy / ww, wz / ww};
    }
    drawShadowVolume(corners, viewProj_, shadowProjProg_);
    ActiveTexture(GL_TEXTURE0);
    UseProgram(0);
    // FinishRenderingShadowMask (resolve: the GL texture is the target). BlurShadowMask (2 passes) runs
    // natively when anything was drawn; kernel weights UNKNOWN -> not run.
    maskDrawnFrame_ = frameNo_;
    ++statShadowProj_;
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, vpW_, vpH_);
    glDepthFunc(GL_LEQUAL);
}

// WFC_SHADOWSELFTEST=1: ShadowMask machinery and native constants (logged PASS / FAIL).
void Pipeline::runShadowMaskSelfTest() {
    int pass = 0, fail = 0;
    auto check = [&](bool ok, const char* what) { (ok ? pass : fail)++; LOG_INFO("shadow-test %s: %s", ok ? "PASS" : "FAIL", what); };
    // constants
    check(shadowDepthResolution(kMaxShadowResolution) == 1024 && shadowDepthResolution(4096) == 2048 &&
          shadowDepthResolution(0) == 1, "Res = clamp(MaxShadowResolution, 1, 2048): 1024 shipped");
    float bias = shadowDepthBiasParabolic(1024, kMaskedShadowsDepthBias, kShadowFilterRadius);
    LOG_INFO("shadow-test: ShadowDepthBias = %.3f", bias);
    check(std::fabs(bias - 1165.08f) < 0.05f, "ShadowDepthBias = (1024 * 0.2 / 6)^2 = 1165.08");
    check(sizeof(kEdgeSampleOffsets) / sizeof(float) == 8 && sizeof(kRefiningSampleOffsets) / sizeof(float) == 24,
          "BranchingPCF: 4 edge + 12 refining offsets");
    check(std::fabs(kEdgeSampleOffsets[1] * 6.0f / 1024.0f - 0.0058319f) < 1e-6f, "offset scale = table * 6 / Res");
    check(shadowProjectionAllowed(0x4u | 0x20u, 0), "gate: flags 0x4 + DPG0 bit -> projected");
    check(!shadowProjectionAllowed(0x20u, 0), "gate: missing render flag 0x4 -> not projected");
    check(!shadowProjectionAllowed(0x4u | 0x40u, 0), "gate: DPG bit of another group only -> not projected");
    check(shadowProjectionAllowed(0x4u | 0x40u, 1), "gate: DPG1 bit for DPG1 -> projected");
    // DLAC
    {
        core::Vec3 amb[6], lsh[6], tot[6];
        for (int i = 0; i < 6; ++i) { amb[i] = {0.2f, 0.1f, 0.05f}; lsh[i] = {0, 0, 0}; tot[i] = amb[i]; }
        float d[3];
        directLightAmbientContribution(tot, lsh, d);
        check(d[0] == 0.0f && d[1] == 0.0f && d[2] == 0.0f, "DLAC = 0 with no lights folded into SH");
        lsh[0] = {0.6f, 0.3f, 0.0f};
        for (int i = 0; i < 6; ++i) tot[i] = amb[i] + lsh[i];
        directLightAmbientContribution(tot, lsh, d);
        check(std::fabs(d[0] - 0.6f / (1.8f + 0.001f)) < 1e-6f && std::fabs(d[1] - 0.3f / (0.9f + 0.001f)) < 1e-6f &&
              d[2] == 0.0f, "DLAC = CubeSum(L) / (CubeSum(A + L) + 0.001) per channel");
    }
    // GPU: clear, one / two overlapping multiplicative projections, stencil bounding, alpha untouched
    if (!ensureShadowPrograms()) { check(false, "shadow programs"); return; }
    int savedFrame = frameNo_;
    frameNo_ = -1000;                            // a fresh mask build
    beginShadowMask();
    std::vector<unsigned char> px((size_t)maskW_ * maskH_ * 4);
    glReadPixels(0, 0, maskW_, maskH_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    bool allOne = std::all_of(px.begin(), px.end(), [](unsigned char c) { return c == 255; });
    check(allOne, "mask clear = (1,1,1,1) everywhere");
    check(maskW_ == (fbW_ > 960 ? fbW_ / 2 : fbW_) && maskH_ == (fbW_ > 960 ? fbH_ / 2 : fbH_), "mask size = scene / (SizeX > 960 ? 2 : 1)");
    glDepthMask(GL_TRUE); glClearDepth(0.5); glClear(GL_DEPTH_BUFFER_BIT); glClearDepth(1.0);   // receiver plane at depth 0.5
    auto box = [](float x0, float x1, float y0, float y1, float z0, float z1, core::Vec3 c[8]) {
        for (int i = 0; i < 8; ++i) c[i] = {(i & 1) ? x1 : x0, (i & 2) ? y1 : y0, (i & 4) ? z1 : z0};
    };
    core::Mat4 I = core::Mat4::identity();
    core::Vec3 A[8], B[8], C[8];
    box(-0.5f, 0.2f, -0.5f, 0.5f, -0.2f, 0.6f, A);    // straddles the receiver
    box(0.0f, 0.4f, -0.5f, 0.5f, -0.2f, 0.6f, B);     // straddles, overlaps A on x in [0, 0.2]
    box(0.5f, 0.9f, -0.5f, 0.5f, -0.9f, -0.5f, C);    // entirely in front of the receiver
    UseProgram(constProg_);
    Uniform4f(GetUniformLocation(constProg_, "uColor"), 0.5f, 0.5f, 0.5f, 0.0f);
    drawShadowVolume(A, I, constProg_);
    UseProgram(constProg_);
    drawShadowVolume(B, I, constProg_);
    UseProgram(constProg_);
    drawShadowVolume(C, I, constProg_);
    BindFramebuffer(GL_FRAMEBUFFER, maskFbo_);
    glReadPixels(0, 0, maskW_, maskH_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    auto at = [&](float nx, float ny, int ch) {
        int x = std::min((int)((nx * 0.5f + 0.5f) * maskW_), maskW_ - 1), y = std::min((int)((ny * 0.5f + 0.5f) * maskH_), maskH_ - 1);
        return (int)px[((size_t)y * maskW_ + x) * 4 + ch];
    };
    LOG_INFO("shadow-test: A-only %d, overlap %d, B-only %d, in-front %d, untouched %d", at(-0.25f, 0, 0), at(0.1f, 0, 0),
             at(0.3f, 0, 0), at(0.7f, 0, 0), at(-0.8f, -0.8f, 0));
    check(std::abs(at(-0.25f, 0, 0) - 128) <= 1 && at(-0.25f, 0, 1) == at(-0.25f, 0, 0), "one projection: mask.rgb = 1 * 0.5");
    check(std::abs(at(0.1f, 0, 0) - 64) <= 1, "two overlapping projections multiply (0.25), not replace / min / add");
    check(std::abs(at(0.3f, 0, 0) - 128) <= 1, "second projection alone: 0.5");
    check(at(0.7f, 0, 0) == 255, "volume entirely in front of the receiver: stencil 0, mask untouched");
    check(at(-0.8f, -0.8f, 0) == 255 && at(0.95f, 0.95f, 0) == 255, "pixels outside every volume remain 1");
    bool alpha = true;
    for (size_t i = 3; i < px.size(); i += 4) alpha &= px[i] == 255;
    check(alpha, "alpha untouched by the projection blend");
    // the character pass reads .r: a mask with r = 0.25 and g = b = 0 (WFC_SHADOWMASKTEST texture) gives
    // the r-channel response in the DSLS matrix; checked there.
    frameNo_ = savedFrame;
    maskClearedFrame_ = maskDrawnFrame_ = -1;
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, vpW_, vpH_);
    UseProgram(0);
    LOG_INFO("shadow-test: %d passed, %d failed", pass, fail);
}

} // namespace wfc
} // namespace render
