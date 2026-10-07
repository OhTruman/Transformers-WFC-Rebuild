// WFC character shadows: light environment synthetic projector -> ShadowMask -> character UberLight pass.
//
// ReverseEngineering 13c0953 (notes/MILESTONE03_RENDERING_CHARACTER_SHADOW_PATH.md) and 7033f18
// (notes/MILESTONE03_RENDERING_CHARACTER_SHADOW_RUNTIME.md). Normal play, no opt-in:
//  * Projector: each DirectLightEnv owns ONE synthetic shadow light, rewritten from the composite shadow
//    record on every proxy build (type, LightToWorld, radius / falloff / cones, Min/MaxShadowResolution,
//    ModShadowColor = shadowFactor); switched off when there is no record (shadowFactor within 1e-4 of
//    white). Never the scene light. [CONFIRMED; slot creation / registration PARTIAL -> ShadowProjector
//    inside the environment state]
//  * Creation (InitDynamicShadows 0x83057028 / CreateProjectedShadow 0x83056A20), per frame: projector on,
//    ModShadowColor not white (1e-4), light in a view; subject CastShadow && bCastDynamicShadow, no
//    ShadowParent, relevant (IsShadowCast) or visible; initializer type 1/2/3; FadeAlpha = 1.0; resolution
//    clamp((int)(1.0 * ScreenRadius), min(Min, Buf - 10), min(Max - 10, Buf - 10)). [CONFIRMED]
//  * Projection gate: subject view relevance bit 0x4 (IsShadowCast) and bit 5 + DPG. [CONFIRMED]
//  * Shadow space: perspective for every type; origin = light position (point / spot) or
//    B.Origin - (2R + 300 UU) * axis (directional); pulled back to sqrt(2) R; MinLightW 0.1 UU, MaxLightW =
//    radius or 2 (2R + 300); DynamicShadowDepthBias input (0 for both forms); 5-texel border. [CONFIRMED]
//    Frustum fit and ScreenToShadowMatrix (VMX): PARTIAL / UNKNOWN -> sphere-tangent perspective [PROV].
//  * ShadowMask: RGBA8 at SizeX/f x SizeY/f (f = SizeX > 960 ? 2 : 1), cleared to 1 (visibility), z-fail
//    stencil frustum volume, dest.rgb *= src.rgb, alpha untouched. [CONFIRMED]
//  * Mod projection PS: atten = spot^2 * (1 - saturate(|d/R|^2)^ShadowFalloffExponent); out =
//    lerp(lerp(1, ShadowModulateColor, atten), 1, PCF), ShadowModulateColor = lerp(1, ModShadowColor,
//    FadeAlpha 1) = shadowFactor. [chain CONFIRMED; combine PARTIAL (spot variant decoded)]
//  * BranchingPCF: native Edge (4) / Refining (12) tables x 6 / Res, RandomAngles rotation, ShadowDepthBias
//    (Res * 0.2 / 6)^2 = 1165.08. [CONFIRMED CPU side]
//  * Blur (BlurShadowMask 0x82DDB070, FBlurShadowMaskPixelShader): only if a shadow was drawn; H then V;
//    6 bilinear clamped taps at +-0.5 / +-1.5 / +-2.5 texels, weights {4, 2, 1} * (4 - s), normalised; the
//    horizontal pass output is squared. [CONFIRMED; tie branch PARTIAL -> off unless WFC_BLURTIE=1]
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

// pending instanced character draws are flushed before any draw / blit here (WfcPipeline.cpp)
extern Pipeline* gInstPipeline;
static inline void wfcInstFlushHook() { if (gInstPipeline) gInstPipeline->flushInstances(); }
#define glDrawElements(...) (wfcInstFlushHook(), ::glDrawElements(__VA_ARGS__))
#define glDrawArrays(...) (wfcInstFlushHook(), ::glDrawArrays(__VA_ARGS__))
#define BlitFramebuffer(...) (wfcInstFlushHook(), glx::BlitFramebuffer(__VA_ARGS__))

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

// RenderProjection gate: the SUBJECT's view relevance (not a light flag), bit 0x4 and bit 5 + DPG.
bool shadowProjectionAllowed(uint32_t rel, int dpg) {
    return (rel & 0x4u) != 0 && (rel & (1u << (5 + dpg))) != 0;
}

// FPrimitiveSceneProxy::IsShadowCast(View) 0x82DD8D58 + GetDepthPriorityGroup(View) 0x82C8BCF0.
uint32_t shadowViewRelevance(const ShadowSubject& s) {
    uint32_t rel = 0x2u;                          // dynamic relevance (drawn this frame)
    bool cast;
    if (!s.castShadow) cast = false;              // PSI 0x40000000|0x20000000 both need CastShadow
    else if (s.hidden) cast = s.castHiddenShadow;
    else cast = s.maxDrawDistance <= 0.0f || s.distSq < s.maxDrawDistance * s.maxDrawDistance;
    if (cast) rel |= 0x4u;
    int dpg = (s.useViewOwnerDPG && s.viewIsOwner) ? s.viewOwnerDPG : s.dpg;
    rel |= 1u << (5 + dpg);
    return rel;
}

int shadowResolution(float screenRadius, int lightMin, int lightMax) {
    const int buf = shadowDepthResolution(kMaxShadowResolution);
    int minR = lightMin > 0 ? lightMin : 128;      // SystemSettings MinShadowResolution
    int maxR = lightMax > 0 ? lightMax : kMaxShadowResolution;
    int lo = std::min(minR, buf - 10), hi = std::min(maxR - 10, buf - 10);
    int r = (int)(1.0f * screenRadius);           // ShadowTexelsPerPixel 1.0
    return std::min(std::max(r, lo), hi);
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
// FBlurShadowMaskPixelShader (Engine ShaderCache_10303 + 367646): Offsets c3 = (A, B) along the pass axis,
// Options c4 = (4, 0, 0, 0); C = 2A + B.
const char* kBlurFS = R"(#version 330 core
out vec4 oColor;
uniform sampler2D uMask;             // bilinear, clamp
uniform vec4 uOffsets;               // pass 0: (0.5/W, 0, 1.5/W, 0); pass 1: (0, 0.5/H, 0, 1.5/H)
uniform vec2 uMaskSize;
uniform int uTie;                    // decoded tie branch (PARTIAL), off by default
void main() {
    vec2 uv = gl_FragCoord.xy / uMaskSize;
    vec2 A = uOffsets.xy, B = uOffsets.zw, C = 2.0 * A + B;
    float sA0 = texture(uMask, uv - A).x, sA1 = texture(uMask, uv + A).x;
    float sB0 = texture(uMask, uv - B).x, sB1 = texture(uMask, uv + B).x;
    float sC0 = texture(uMask, uv - C).x, sC1 = texture(uMask, uv + C).x;
    float wA0 = 4.0 * (4.0 - sA0), wA1 = 4.0 * (4.0 - sA1);
    float wB0 = 2.0 * (4.0 - sB0), wB1 = 2.0 * (4.0 - sB1);
    float wC0 = 1.0 * (4.0 - sC0), wC1 = 1.0 * (4.0 - sC1);
    float x = (wA0 * sA0 + wA1 * sA1 + wB0 * sB0 + wB1 * sB1 + wC0 * sC0 + wC1 * sC1) /
              (wA0 + wA1 + wB0 + wB1 + wC0 + wC1);
    if (uTie != 0) {
        float f = (sB0 == sA0 && sB1 > sB0) ? 1.0 : 0.0;
        x = x * (1.0 - 2.0 * f) + f * (sA0 + sA1);
    }
    if (uOffsets.y == 0.0) x = x * x;    // horizontal pass (Offsets.y == Options.y) squares
    oColor = vec4(x, x, x, 1.0);
}
)";
const char* kProjFS = R"(#version 330 core
out vec4 oColor;
uniform sampler2D uSceneDepth;
uniform sampler2D uShadowMap;
uniform sampler2D uRandomAngles;
uniform vec2 uMaskSize;
uniform mat4 uInvViewProj;
uniform mat4 uShadowViewProj;      // world -> shadow clip [PROV: ScreenToShadowMatrix not recovered]
uniform vec2 uSubRect;             // x = per-shadow resolution / ShadowBufferSize, y = border 5 / ShadowBufferSize
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
    vec2 uv = (sp.xy / sp.w * 0.5 + 0.5) * uSubRect.x + uSubRect.y;
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

// The projector for an environment form: the environment's synthetic shadow light. WFC_SHADOWTEST=<light>
// substitutes a projector built from that scene light (diagnostics only).
const ShadowProjector* Pipeline::projectorFor(int form, ShadowProjector& scratch) const {
    static const char* test = std::getenv("WFC_SHADOWTEST");
    if (test) {
        for (size_t i = 0; i < lights_.size(); ++i)
            if (lights_[i].name == test && lights_[i].type != 3) {
                updateShadowProjector(scratch, &lights_[i], (int)i, 0.0f);
                return &scratch;
            }
        return nullptr;
    }
    auto it = dle_.find(form);
    if (it == dle_.end() || !it->second.projector.on) return nullptr;
    return &it->second.projector;
}

bool Pipeline::ensureShadowPrograms() {
    if (shadowProjProg_) return true;
    GLuint fv = compileShader(GL_VERTEX_SHADER, kFullVS, "shadowfull.vs");
    GLuint fv2 = compileShader(GL_VERTEX_SHADER, kFullVS, "shadowfull2.vs");
    GLuint vv = compileShader(GL_VERTEX_SHADER, kVolVS, "shadowvol.vs");
    GLuint vv2 = compileShader(GL_VERTEX_SHADER, kVolVS, "shadowvol2.vs");
    GLuint pf = compileShader(GL_FRAGMENT_SHADER, kProjFS, "shadowproj.fs");
    GLuint df = compileShader(GL_FRAGMENT_SHADER, kMaskDepthFS, "maskdepth.fs");
    GLuint cf = compileShader(GL_FRAGMENT_SHADER, kConstFS, "shadowconst.fs");
    GLuint bf = compileShader(GL_FRAGMENT_SHADER, kBlurFS, "shadowblur.fs");
    if (!fv || !fv2 || !vv || !vv2 || !pf || !df || !cf || !bf) return false;
    maskDepthProg_ = linkProgram(fv, df, "maskdepth");
    maskBlurProg_ = linkProgram(fv2, bf, "shadowblur");
    constProg_ = linkProgram(vv2, cf, "shadowconst");
    shadowProjProg_ = linkProgram(vv, pf, "shadowproj");
    if (!shadowProjProg_ || !maskDepthProg_ || !constProg_ || !maskBlurProg_) { shadowProjProg_ = 0; return false; }
    ImageData img;
    std::string path = contentRoot() + "EngineMaterials/RandomAngles.png";
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
// buffer of the same size for the stencil volume. The projections accumulate in maskTex_; the blur
// passes (H -> maskTmpTex_, V -> maskBlurTex_) stand in for the in-place resolve-and-redraw of the
// original, which GL cannot sample and write at once.
void Pipeline::ensureShadowMask() {
    if (maskFbo_ && maskForW_ == fbW_ && maskForH_ == fbH_) return;
    int f = fbW_ > 960 ? 2 : 1;
    maskW_ = std::max(fbW_ / f, 1);
    maskH_ = std::max(fbH_ / f, 1);
    maskForW_ = fbW_; maskForH_ = fbH_;
    if (!maskFbo_) {
        GenFramebuffers(1, &maskFbo_); glGenTextures(1, &maskTex_); GenRenderbuffers(1, &maskDepthRb_);
        GenFramebuffers(1, &maskTmpFbo_); glGenTextures(1, &maskTmpTex_);
        GenFramebuffers(1, &maskBlurFbo_); glGenTextures(1, &maskBlurTex_);
    }
    auto tex = [&](GLuint t, GLint filter) {      // blur sampler: bilinear, clamp; lighting read: point
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, maskW_, maskH_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    tex(maskTex_, GL_LINEAR);
    tex(maskTmpTex_, GL_LINEAR);
    tex(maskBlurTex_, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    BindRenderbuffer(GL_RENDERBUFFER, maskDepthRb_);
    RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, maskW_, maskH_);
    BindRenderbuffer(GL_RENDERBUFFER, 0);
    BindFramebuffer(GL_FRAMEBUFFER, maskFbo_);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, maskTex_, 0);
    FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, maskDepthRb_);
    if (CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) LOG_ERROR("wfc: ShadowMask framebuffer incomplete");
    BindFramebuffer(GL_FRAMEBUFFER, maskTmpFbo_);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, maskTmpTex_, 0);
    BindFramebuffer(GL_FRAMEBUFFER, maskBlurFbo_);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, maskBlurTex_, 0);
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

// BlurShadowMask (0x82DDB070): horizontal then vertical pass over the resolved mask.
void Pipeline::blurShadowMask() {
    static const int tie = std::getenv("WFC_BLURTIE") ? 1 : 0;
    UseProgram(maskBlurProg_);
    Uniform1i(GetUniformLocation(maskBlurProg_, "uMask"), 0);
    Uniform2f(GetUniformLocation(maskBlurProg_, "uMaskSize"), (float)maskW_, (float)maskH_);
    Uniform1i(GetUniformLocation(maskBlurProg_, "uTie"), tie);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(0, 0, maskW_, maskH_);
    BindVertexArray(postVao_);
    ActiveTexture(GL_TEXTURE0);
    BindFramebuffer(GL_FRAMEBUFFER, maskTmpFbo_);                  // pass 0 (horizontal)
    glBindTexture(GL_TEXTURE_2D, maskTex_);
    Uniform4f(GetUniformLocation(maskBlurProg_, "uOffsets"), 0.5f / (float)maskW_, 0.0f, 1.5f / (float)maskW_, 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    BindFramebuffer(GL_FRAMEBUFFER, maskBlurFbo_);                 // pass 1 (vertical)
    glBindTexture(GL_TEXTURE_2D, maskTmpTex_);
    Uniform4f(GetUniformLocation(maskBlurProg_, "uOffsets"), 0.0f, 0.5f / (float)maskH_, 0.0f, 1.5f / (float)maskH_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    BindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
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
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_CLAMP);
    glDisable(GL_BLEND);
    glCullFace(GL_BACK);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
}

// GetProjectedShadowInitializer (0x82E22A38) + shadow depth of the current dynamic caster (dynVao_).
// Bounds B = the environment's bounds (origin, extent, sphere radius R = |extent|).
bool Pipeline::renderShadowDepth(GpuMesh& g, const core::Mat4& model, const ShadowProjector& p, ShadowRequest& rq) {
    if (p.type < 1 || p.type > 3) return false;
    const float kUU = 0.01f;                      // metres per UE unit
    core::Vec3 B = envBoundsCenter_;
    float R = std::max(core::length(envBoundsExtent_), 0.05f);
    core::Vec3 axis = core::normalize(p.dir);
    float D = 2.0f * R + 300.0f * kUU;
    core::Vec3 O = p.type == 1 ? B - axis * D : p.pos;              // GetShadowOrigin 0x82DBF678
    core::Vec3 v = B - O;
    float dist = core::length(v);
    core::Vec3 n = dist > 1e-6f ? v * (1.0f / dist) : axis;
    const float kSqrt2 = std::sqrt(2.0f);
    if (dist <= kSqrt2 * R) { dist = kSqrt2 * R; O = B - n * dist; }   // keep the subject inside the frustum
    float maxW = p.type == 1 ? 2.0f * D : p.radius;                  // GetMaxLightW 0x82DBF728
    float minW = 0.1f * kUU;                                          // MinLightW 0.1
    // frustum fit [PARTIAL / PROV]: perspective tangent to the bounding sphere, depth clamped to [MinLightW, MaxLightW]
    float zn = std::max(dist - R, minW), zf = std::min(dist + R, maxW);
    if (zf <= zn) return false;
    float halfFov = std::asin(std::min(R / dist, 0.99f));
    core::Mat4 proj = core::Mat4::perspective(2.0f * halfFov, 1.0f, zn, zf);
    core::Vec3 up = std::fabs(n.y) > 0.95f ? core::Vec3{1, 0, 0} : core::Vec3{0, 1, 0};
    core::Mat4 view = core::Mat4::lookAt(O, O + n, up);
    rq.light = p.source;
    rq.origin = O;
    rq.viewProj = proj * view;
    rq.zRow[0] = n.x; rq.zRow[1] = n.y; rq.zRow[2] = n.z;
    rq.zRow[3] = -core::dot(n, O) - zn;
    rq.invMaxSubjectDepth = 1.0f / std::max(zf - zn, 1e-4f);
    rq.depthBias = 0.0f;                          // DynamicShadowDepthBias: 0 on both forms (PrimitiveComponent default)
    for (int k = 0; k < 4; ++k) rq.modColor[k] = 1.0f + (p.modShadowColor[k] - 1.0f) * 1.0f;   // FadeAlpha = 1.0
    // resolution (0x83056A20): ScreenRadius = max(0.5 SizeX P00, 0.5 SizeY P11) * R / max(clipW(B), 1 UU)
    const float* vp = viewProj_.m;
    float clipW = vp[3] * B.x + vp[7] * B.y + vp[11] * B.z + vp[15];
    float screenRadius = std::max(0.5f * (float)vpW_ * camProj_.m[0], 0.5f * (float)vpH_ * camProj_.m[5]) * R /
                         std::max(clipW, 1.0f * kUU);
    rq.res = shadowResolution(screenRadius, p.minRes, p.maxRes);
    const int Res = shadowDepthResolution(kMaxShadowResolution);
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
    glViewport(5, 5, rq.res, rq.res);             // 10-texel border (5 per side)
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

// InitDynamicShadows / CreateProjectedShadow / RenderProjection for the drawn form's projector.
void Pipeline::castCharacterShadow(GpuMesh& g, const core::Mat4& model, const ShadowProjector& p) {
    if (!ensureShadowPrograms()) return;
    // light side: projector on, ModShadowColor not white within 1e-4, light in a view
    bool white = true;
    for (int k = 0; k < 4; ++k) white &= std::fabs(p.modShadowColor[k] - 1.0f) < 1e-4f;
    if (!p.on || white) return;
    if (p.type != 1) {                            // point / spot: sphere(pos, radius) against the view frustum
        const float* m = viewProj_.m;
        float planes[6][4];
        for (int i = 0; i < 3; ++i)
            for (int sgn = 0; sgn < 2; ++sgn)
                for (int c = 0; c < 4; ++c)
                    planes[i * 2 + sgn][c] = m[c * 4 + 3] + (sgn ? -1.0f : 1.0f) * m[c * 4 + i];
        for (auto& pl : planes) {
            float len = std::sqrt(pl[0] * pl[0] + pl[1] * pl[1] + pl[2] * pl[2]);
            if (pl[0] * p.pos.x + pl[1] * p.pos.y + pl[2] * p.pos.z + pl[3] < -p.radius * len) { ++statShadowGated_; return; }
        }
    }
    // subject: authored Optimus robot / vehicle mesh components (TransGame Default__TnRobotForm.RobotMeshComponent0,
    // Default__TnTruckForm.VehicleMesh -> Default__SkeletalMeshComponent / MeshComponent / PrimitiveComponent):
    // CastShadow True, bCastDynamicShadow True, bCastHiddenShadow False, SDPG_World, no view-owner DPG,
    // CachedCullDistance 0; drawn this frame (not hidden); no ShadowParent.
    ShadowSubject subj;
    core::Vec3 dv = envBoundsCenter_ - camPos_;
    subj.distSq = core::dot(dv, dv);
    if (!subj.castShadow || !subj.castDynamicShadow || subj.hasShadowParent) return;   // interaction not PROJECTED
    uint32_t rel = shadowViewRelevance(subj);
    if (const char* ov = std::getenv("WFC_SUBJECTRELEVANCE")) rel = (uint32_t)std::strtoul(ov, nullptr, 16);   // gate tests
    if ((rel & 7u) == 0) { ++statShadowGated_; return; }               // neither relevant nor visible
    ShadowRequest rq;
    if (!renderShadowDepth(g, model, p, rq)) return;
    if (!shadowProjectionAllowed(rel, 0)) { ++statShadowGated_; return; }   // World DPG pass
    depthPrepass(g, model);
    ensureSceneDepth();
    const int Res = shadowDepthResolution(kMaxShadowResolution);
    const float k = kShadowFilterRadius / (float)Res;
    float edge[8], refine[24];
    for (int i = 0; i < 8; ++i) edge[i] = kEdgeSampleOffsets[i] * k;
    for (int i = 0; i < 24; ++i) refine[i] = kRefiningSampleOffsets[i] * k;
    beginShadowMask();
    fillMaskDepth();
    core::Mat4 invVP = inverse4(viewProj_);
    UseProgram(shadowProjProg_);
    auto U = [&](const char* nm) { return GetUniformLocation(shadowProjProg_, nm); };
    Uniform1i(U("uSceneDepth"), 0); Uniform1i(U("uShadowMap"), 1); Uniform1i(U("uRandomAngles"), 2);
    Uniform2f(U("uMaskSize"), (float)maskW_, (float)maskH_);
    UniformMatrix4fv(U("uInvViewProj"), 1, GL_FALSE, invVP.m);
    Uniform1f(U("uInvRandomAngleTextureSize"), randomAnglesSize_ > 0 ? 1.0f / (float)randomAnglesSize_ : 0.0f);
    Uniform1f(U("uShadowDepthBias"), shadowDepthBiasParabolic(Res, kMaskedShadowsDepthBias, kShadowFilterRadius));
    Uniform2fv(U("uEdge"), 4, edge);
    Uniform2fv(U("uRefine"), 12, refine);
    UniformMatrix4fv(U("uShadowViewProj"), 1, GL_FALSE, rq.viewProj.m);
    Uniform2f(U("uSubRect"), (float)rq.res / (float)Res, 5.0f / (float)Res);
    Uniform4f(U("uShadowZ"), rq.zRow[0], rq.zRow[1], rq.zRow[2], rq.zRow[3]);
    Uniform1f(U("uInvMaxSubjectDepth"), rq.invMaxSubjectDepth);
    Uniform4f(U("uModColor"), rq.modColor[0], rq.modColor[1], rq.modColor[2], rq.modColor[3]);
    core::Vec3 lp = p.type == 1 ? rq.origin : p.pos;     // directional variant not decoded [PARTIAL]
    Uniform4f(U("uLightPosInvRadius"), lp.x * 100.0f, lp.z * 100.0f, lp.y * 100.0f, 1.0f / (p.radius * 100.0f));
    Uniform3f(U("uLightDirUE"), p.dir.x, p.dir.z, p.dir.y);
    Uniform2f(U("uSpotAngles"), p.cosOuter, p.invConeRange);
    Uniform1i(U("uIsSpot"), p.type == 3 ? 1 : 0);
    Uniform1f(U("uFalloffExponent"), p.falloff);
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, depthCopyTex_);
    ActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, shadowDepthTex_);
    ActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, randomAnglesTex_);
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
    // FinishRenderingShadowMask (resolve) + BlurShadowMask: the blurred mask is rebuilt from the full
    // product of this frame's projections each time one is added
    blurShadowMask();
    UseProgram(0);
    maskDrawnFrame_ = frameNo_;
    ++statShadowProj_;
    ShadowFrameInfo fi;
    fi.form = envForm_; fi.source = p.source; fi.type = p.type; fi.res = rq.res; fi.factor = rq.modColor[0];
    shadowFrame_.push_back(fi);
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
    // resolution
    check(shadowResolution(5000.0f, 0, 0) == 1014 && shadowResolution(10.0f, 0, 0) == 128 && shadowResolution(300.7f, 0, 0) == 300,
          "resolution = clamp((int)ScreenRadius, min(128, 1014), min(1024 - 10, 1014))");
    check(shadowResolution(10.0f, 256, 512) == 256 && shadowResolution(900.0f, 256, 512) == 502, "light Min/MaxShadowResolution override");
    // subject relevance / gates
    ShadowSubject s;
    check(shadowViewRelevance(s) & 0x4u, "Optimus mesh (CastShadow, bCastDynamicShadow, shown): IsShadowCast");
    check(shadowProjectionAllowed(shadowViewRelevance(s), 0), "SDPG_World subject projects in the World DPG pass");
    ShadowSubject h = s; h.hidden = true;
    check(!(shadowViewRelevance(h) & 0x4u), "hidden form without bCastHiddenShadow: no shadow relevance");
    h.castHiddenShadow = true;
    check(shadowViewRelevance(h) & 0x4u, "hidden form with bCastHiddenShadow: shadow relevance");
    ShadowSubject nc = s; nc.castShadow = false;
    check(!(shadowViewRelevance(nc) & 0x4u), "CastShadow False: no shadow relevance");
    ShadowSubject distant = s; distant.maxDrawDistance = 10.0f; distant.distSq = 200.0f;
    check(!(shadowViewRelevance(distant) & 0x4u), "beyond MaxDrawDistance: no shadow relevance");
    ShadowSubject fg = s; fg.useViewOwnerDPG = true; fg.viewOwnerDPG = 2; fg.viewIsOwner = true;
    check(!shadowProjectionAllowed(shadowViewRelevance(fg), 0) && shadowProjectionAllowed(shadowViewRelevance(fg), 2),
          "bUseViewOwnerDPG for the owner's view: relevance in ViewOwnerDPG only");
    fg.viewIsOwner = false;
    check(shadowProjectionAllowed(shadowViewRelevance(fg), 0), "bUseViewOwnerDPG, other view: component DPG");
    check(!shadowProjectionAllowed(0x2u | 0x20u, 0), "relevance without bit 0x4: not projected");
    // synthetic projector
    {
        Light pt; pt.type = 0; pt.pos = {1, 2, 3}; pt.radius = 7.0f; pt.shadowFalloffExponent = 3.0f;
        Light sp = pt; sp.type = 1; sp.cosOuter = 0.5f; sp.invConeRange = 4.0f;
        Light dl = pt; dl.type = 2; dl.dir = {0, -1, 0};
        ShadowProjector p;
        updateShadowProjector(p, &pt, 5, 0.25f);
        check(p.on && p.type == 2 && p.source == 5 && p.radius == 7.0f && p.falloff == 3.0f && p.modShadowColor[0] == 0.25f &&
              p.modShadowColor[3] == 1.0f, "point composite -> projector type 2, ModShadowColor = shadowFactor");
        updateShadowProjector(p, &sp, 6, 0.5f);
        check(p.on && p.type == 3 && p.source == 6 && p.sourceChanges == 2 && p.cosOuter == 0.5f,
              "spot composite overwrites the same slot (no second projector)");
        updateShadowProjector(p, &dl, 7, 0.5f);
        check(p.type == 1 && std::fabs(p.radius - 3276.8f) < 1e-3f && p.falloff == 2.0f, "directional: Radius 327680 UU, falloff 2.0");
        updateShadowProjector(p, &dl, 7, 1.0f - 5e-5f);
        check(!p.on, "shadowFactor within 1e-4 of white: no record, slot switched off");
        updateShadowProjector(p, nullptr, -1, 0.0f);
        check(!p.on, "no composite light: slot switched off");
    }
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
    // GPU: clear, one / two overlapping multiplicative projections, stencil bounding, alpha untouched, blur
    if (!ensureShadowPrograms()) { check(false, "shadow programs"); return; }
    int savedFrame = frameNo_;
    frameNo_ = -1000;
    beginShadowMask();
    std::vector<unsigned char> px((size_t)maskW_ * maskH_ * 4);
    glReadPixels(0, 0, maskW_, maskH_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    check(std::all_of(px.begin(), px.end(), [](unsigned char c) { return c == 255; }), "mask clear = (1,1,1,1) everywhere");
    check(maskW_ == (fbW_ > 960 ? fbW_ / 2 : fbW_) && maskH_ == (fbW_ > 960 ? fbH_ / 2 : fbH_), "mask size = scene / (SizeX > 960 ? 2 : 1)");
    glDepthMask(GL_TRUE); glClearDepth(0.5); glClear(GL_DEPTH_BUFFER_BIT); glClearDepth(1.0);
    auto box = [](float x0, float x1, float y0, float y1, float z0, float z1, core::Vec3 c[8]) {
        for (int i = 0; i < 8; ++i) c[i] = {(i & 1) ? x1 : x0, (i & 2) ? y1 : y0, (i & 4) ? z1 : z0};
    };
    core::Mat4 I = core::Mat4::identity();
    core::Vec3 A[8], Bx[8], C[8];
    box(-0.5f, 0.2f, -0.5f, 0.5f, -0.2f, 0.6f, A);
    box(0.0f, 0.4f, -0.5f, 0.5f, -0.2f, 0.6f, Bx);
    box(0.5f, 0.9f, -0.5f, 0.5f, -0.9f, -0.5f, C);
    UseProgram(constProg_);
    Uniform4f(GetUniformLocation(constProg_, "uColor"), 0.5f, 0.5f, 0.5f, 0.0f);
    drawShadowVolume(A, I, constProg_);
    UseProgram(constProg_);
    drawShadowVolume(Bx, I, constProg_);
    UseProgram(constProg_);
    drawShadowVolume(C, I, constProg_);
    BindFramebuffer(GL_FRAMEBUFFER, maskFbo_);
    glReadPixels(0, 0, maskW_, maskH_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    auto at = [&](const std::vector<unsigned char>& b, float nx, float ny, int ch) {
        int x = std::min((int)((nx * 0.5f + 0.5f) * maskW_), maskW_ - 1), y = std::min((int)((ny * 0.5f + 0.5f) * maskH_), maskH_ - 1);
        return (int)b[((size_t)y * maskW_ + x) * 4 + ch];
    };
    LOG_INFO("shadow-test: A-only %d, overlap %d, B-only %d, in-front %d, untouched %d", at(px, -0.25f, 0, 0), at(px, 0.1f, 0, 0),
             at(px, 0.3f, 0, 0), at(px, 0.7f, 0, 0), at(px, -0.8f, -0.8f, 0));
    check(std::abs(at(px, -0.25f, 0, 0) - 128) <= 1, "one projection: mask.rgb = 1 * 0.5");
    check(std::abs(at(px, 0.1f, 0, 0) - 64) <= 1, "two overlapping projections multiply (0.25), not replace / min / add");
    check(std::abs(at(px, 0.3f, 0, 0) - 128) <= 1, "second projection alone: 0.5");
    check(at(px, 0.7f, 0, 0) == 255, "volume entirely in front of the receiver: stencil 0, mask untouched");
    check(at(px, -0.8f, -0.8f, 0) == 255 && at(px, 0.95f, 0.95f, 0) == 255, "pixels outside every volume remain 1");
    bool alpha = true;
    for (size_t i = 3; i < px.size(); i += 4) alpha &= px[i] == 255;
    check(alpha, "alpha untouched by the projection blend");
    // blur against the closed form on the same mask (CPU reference with bilinear clamped taps)
    blurShadowMask();
    std::vector<unsigned char> bl((size_t)maskW_ * maskH_ * 4);
    BindFramebuffer(GL_FRAMEBUFFER, maskBlurFbo_);
    glReadPixels(0, 0, maskW_, maskH_, GL_RGBA, GL_UNSIGNED_BYTE, bl.data());
    {
        auto texel = [&](const std::vector<float>& img, int x, int y) {
            x = std::min(std::max(x, 0), maskW_ - 1); y = std::min(std::max(y, 0), maskH_ - 1);
            return img[(size_t)y * maskW_ + x];
        };
        auto bilin = [&](const std::vector<float>& img, float u, float v) {   // texel-space coords of the sample
            float fx = u - 0.5f, fy = v - 0.5f;
            int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
            float ax = fx - x0, ay = fy - y0;
            return (texel(img, x0, y0) * (1 - ax) + texel(img, x0 + 1, y0) * ax) * (1 - ay) +
                   (texel(img, x0, y0 + 1) * (1 - ax) + texel(img, x0 + 1, y0 + 1) * ax) * ay;
        };
        std::vector<float> raw((size_t)maskW_ * maskH_), h((size_t)maskW_ * maskH_);
        for (size_t i = 0; i < raw.size(); ++i) raw[i] = px[i * 4] / 255.0f;
        auto pass = [&](const std::vector<float>& src, int x, int y, bool horiz) {
            float w[3] = {4, 2, 1}, o[3] = {0.5f, 1.5f, 2.5f}, num = 0, den = 0;
            for (int k = 0; k < 3; ++k)
                for (int sg = -1; sg <= 1; sg += 2) {
                    float u = x + 0.5f + (horiz ? sg * o[k] : 0), v = y + 0.5f + (horiz ? 0 : sg * o[k]);
                    float sv = bilin(src, u, v), wt = w[k] * (4 - sv);
                    num += wt * sv; den += wt;
                }
            float r = num / den;
            return horiz ? r * r : r;
        };
        for (int y = 0; y < maskH_; ++y)
            for (int x = 0; x < maskW_; ++x) h[(size_t)y * maskW_ + x] = std::round(pass(raw, x, y, true) * 255.0f) / 255.0f;
        int worst = 0;
        for (int y = 0; y < maskH_; y += 3)
            for (int x = 0; x < maskW_; x += 3) {
                int ref = (int)std::lround(pass(h, x, y, false) * 255.0f);
                worst = std::max(worst, std::abs(ref - (int)bl[((size_t)y * maskW_ + x) * 4]));
            }
        LOG_INFO("shadow-test: blur max |GPU - closed form| = %d / 255", worst);
        check(worst <= 2, "blur = 6-tap {4,2,1}(4 - s) normalised, H squared then V (closed form)");
        check(at(bl, -0.8f, -0.8f, 0) == 255, "blur keeps unshadowed regions at 1");
    }
    frameNo_ = savedFrame;
    maskClearedFrame_ = maskDrawnFrame_ = -1;
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, vpW_, vpH_);
    UseProgram(0);
    LOG_INFO("shadow-test: %d passed, %d failed", pass, fail);
}

} // namespace wfc
} // namespace render
