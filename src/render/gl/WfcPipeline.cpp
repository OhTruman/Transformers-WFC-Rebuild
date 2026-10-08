#include "render/gl/WfcPipeline.h"
#include "core/LoadYield.h"
#include <tuple>

#include <emmintrin.h>
#include "core/Config.h"
#include "assets/Gltf.h"
#include "assets/Json.h"
#include "core/Log.h"
#include "platform/Image.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace glx;

namespace render {
namespace wfc {
Pipeline* gInstPipeline = nullptr;
unsigned long gShaderCompiles = 0, gTexCreates = 0;   // pending instanced character draws: flushed before any draw / blit
namespace {

std::string readText(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::string();
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

std::string exeDir() {
#if defined(_WIN32)
    char buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf, n);
    size_t k = s.find_last_of("\\/");
    return k == std::string::npos ? std::string(".") : s.substr(0, k);
#else
    return ".";
#endif
}

void replaceAll(std::string& s, const std::string& a, const std::string& b) {
    size_t p = 0;
    while ((p = s.find(a, p)) != std::string::npos) { s.replace(p, a.size(), b); p += b.size(); }
}

float srgbToLinear(float c) { return std::pow(c, 2.2f); }   // UE3 FLinearColor(FColor) table

#if __has_include("core/LoadYield.h")
#include "core/LoadYield.h"
#define WFC_HAS_CORE_LOADYIELD 1
#endif

// every draw / blit below first flushes pending instanced character draws (no pending draw can be observed)
static inline void wfcInstFlushHook() { if (gInstPipeline) gInstPipeline->flushInstances(); }
#define glDrawElements(...) (wfcInstFlushHook(), ::glDrawElements(__VA_ARGS__))
#define glDrawArrays(...) (wfcInstFlushHook(), ::glDrawArrays(__VA_ARGS__))
#define BlitFramebuffer(...) (wfcInstFlushHook(), glx::BlitFramebuffer(__VA_ARGS__))

// ------------------------------------------------------------------------- GLSL sources
const char* kVS = R"(#version 330 compatibility
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aTan;
layout(location=3) in vec2 aUV0;
layout(location=4) in vec2 aUV1;
layout(location=5) in vec4 aColor;   // particle colour (FX draws); constant white otherwise
layout(location=6) in vec3 aSubUV2;  // M67 sprites: second SubUV cell UV + blend; constant 0 otherwise
layout(location=7) in vec3 aPrevPos; // drawDynamicMeshBlended: the previous step's pose (uPoseBlend != 0)
layout(location=8) in vec3 aPrevNrm;
uniform int uPoseBlend;
uniform float uPoseAlpha;
layout(location=9) in vec4 aJoints;   // GPU skinning: 4 joint indices (exact small integers as float)
layout(location=10) in vec4 aWeights;
uniform int uSkin;                    // 0 off, 1 palette, 2 palette blended with the previous step's
uniform int uSkinRow, uSkinBones;
uniform float uSkinAlpha;
uniform sampler2D uBoneTex;           // RGBA32F: row per instance; cur palette at x = 4j, prev at x = 512 + 4j
mat4 wfcBone(int base, int j) {
    return mat4(texelFetch(uBoneTex, ivec2(base + j * 4, uSkinRow), 0), texelFetch(uBoneTex, ivec2(base + j * 4 + 1, uSkinRow), 0),
                texelFetch(uBoneTex, ivec2(base + j * 4 + 2, uSkinRow), 0), texelFetch(uBoneTex, ivec2(base + j * 4 + 3, uSkinRow), 0));
}
// assets::skinPose: sum of w * (M p), w * (M n), w * (M t) over the influences with w > 0 and a valid joint
void wfcSkin(int base, out vec3 p, out vec3 n, out vec3 t) {
    p = vec3(0.0); n = vec3(0.0); t = vec3(0.0);
    for (int k = 0; k < 4; ++k) {
        float w = aWeights[k];
        int j = int(aJoints[k] + 0.5);
        if (w <= 0.0 || j >= uSkinBones) continue;
        mat4 M = wfcBone(base, j);
        p += (M * vec4(aPos, 1.0)).xyz * w;
        n += (M * vec4(aNrm, 0.0)).xyz * w;
        t += (M * vec4(aTan.xyz, 0.0)).xyz * w;
    }
}
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform vec4 uShadowDepth;   // shadow caster pass: x on, y InvMaxSubjectDepth, z DepthBias
uniform vec4 uShadowZ;       // world -> shadow-space z row
uniform vec3 uCamPos;
uniform vec4 uLMCoord;
uniform int uFogOn;
uniform float uFogMaxH, uFogScale, uFogStart, uFogExt;
uniform vec3 uFogIn;
uniform int uVertexLM;       // LMT_1D vertex lightmap: coefficients per vertex from uVLM (rows 0..2)
uniform int uVLMBase;
uniform sampler2D uVLM;
out vec3 vPos; out vec3 vNrm; out vec4 vTan; out vec2 vUV0; out vec2 vUV1; out vec4 vFog; out vec4 vColor; out vec3 vSubUV2;
out vec3 vVLM0; out vec3 vVLM1; out vec3 vVLM2;
out vec2 vUV1Mat;            // raw second UV channel (material TexCoord[1]); vUV1 is the lightmap UV

// UE3 vertex height fog (THeightFogVertexShader / CalcHeightFog), single authored layer, UE units.
// Layer spans [-HALF_WORLD_MAX, FogMaxHeight]; scattering = exp2(FogDistanceScale *
// max(dist - FogStartDistance, 0) * fraction-of-ray-inside-layer); beyond ExtinctionDistance -> 0.
vec4 heightFog(vec3 p) {
    if (uFogOn == 0) return vec4(0.0, 0.0, 0.0, 1.0);
    float camZ = uCamPos.y * 100.0, z = p.y * 100.0, dz = z - camZ;
    float minH = -262144.0, maxH = uFogMaxH;
    float frac;
    if (abs(dz) < 1e-3) frac = (camZ > minH && camZ < maxH) ? 1.0 : 0.0;
    else {
        float t0 = clamp((minH - camZ) / dz, 0.0, 1.0);
        float t1 = clamp((maxH - camZ) / dz, 0.0, 1.0);
        frac = abs(t1 - t0);
    }
    float d = length(p - uCamPos) * 100.0;
    float ld = max(d - uFogStart, 0.0) * frac;
    float sc = exp2(uFogScale * ld);
    if (ld > uFogExt) sc = 0.0;
    return vec4(uFogIn * (1.0 - sc), sc);
}

void main() {
    vec3 pos = aPos, nrm = aNrm;
    vec4 tanIn = aTan;
    if (uSkin != 0) {
        vec3 p, n, t;
        wfcSkin(0, p, n, t);
        n = normalize(n);
        tanIn = vec4(normalize(t), aTan.w);
        if (uSkin == 2) {   // Character::blendedPose on the two skinned poses (tangents: the current pose's)
            vec3 pp, pn, pt;
            wfcSkin(512, pp, pn, pt);
            pn = normalize(pn);
            p = pp + (p - pp) * uSkinAlpha;
            n = normalize(pn + (n - pn) * uSkinAlpha);
        }
        pos = p; nrm = n;
    }
    if (uPoseBlend != 0) {   // presentation interpolation of a skinned pose: the CPU formula (Character::blendedPose)
        pos = aPrevPos + (aPos - aPrevPos) * uPoseAlpha;
        nrm = normalize(aPrevNrm + (aNrm - aPrevNrm) * uPoseAlpha);
    }
    vec4 wp = uModel * vec4(pos, 1.0);
    mat3 nm = mat3(uModel);
    vPos = wp.xyz;
    vNrm = nm * nrm;
    vTan = vec4(nm * tanIn.xyz, tanIn.w);
    vUV0 = aUV0;
    vSubUV2 = aSubUV2;
    vUV1 = aUV1 * uLMCoord.xy + uLMCoord.zw;
    vUV1Mat = aUV1;
    vColor = aColor;
    if (uVertexLM != 0) {
        int vi = gl_VertexID - uVLMBase;
        vVLM0 = texelFetch(uVLM, ivec2(vi, 0), 0).rgb;
        vVLM1 = texelFetch(uVLM, ivec2(vi, 1), 0).rgb;
        vVLM2 = texelFetch(uVLM, ivec2(vi, 2), 0).rgb;
    } else {
        vVLM0 = vVLM1 = vVLM2 = vec3(0.0);
    }
    vFog = heightFog(wp.xyz);
    gl_Position = uViewProj * wp;
    if (uShadowDepth.x > 0.5) {   // shadow depth VS: z = saturate(shadowZ * InvMaxSubjectDepth + DepthBias) * w
        float d = clamp(dot(uShadowZ, vec4(wp.xyz, 1.0)) * uShadowDepth.y + uShadowDepth.z, 0.0, 1.0);
        gl_Position.z = (d * 2.0 - 1.0) * gl_Position.w;
    }
}
)";

const char* kFSHead = R"(#version 330 compatibility
in vec3 vPos; in vec3 vNrm; in vec4 vTan; in vec2 vUV0; in vec2 vUV1; in vec4 vFog; in vec4 vColor; in vec3 vSubUV2;
in vec2 vUV1Mat;
layout(location=0) out vec4 oColor;
uniform vec3 uCamPos;
uniform float uTime;
uniform int uTwoSided;
uniform int uMasked;
uniform float uClip;
uniform int uLit;
uniform int uDebug;          // 1 = lighting only (diffuse 0.5 grey, no emissive)
uniform int uDecalClip;      // static decal: clip to the decal box (uv in [0,1])
uniform int uBlend;          // 0 opaque, 1 masked, 2 translucent, 3 additive, 4 modulate
uniform vec2 uNearFar;       // metres
uniform vec2 uViewport;      // pixels
uniform vec4 uDynParam;      // particle DynamicParameter (map FX emitters; 1 otherwise)
uniform sampler2D uSceneDepth;
uniform int uHasSceneDepth;
uniform sampler2D uSceneColor;   // MaterialExpressionSceneTexture: the resolved opaque scene colour (HDR)
// Screen UVs in UE3's (D3D) convention, v down (ScreenPositionScaleBias (0.5, -0.5)); the GL scene copy is bottom-up.
vec4 wfcSceneColor(vec2 uv) { return texture(uSceneColor, vec2(uv.x, 1.0 - uv.y)); }
vec2 wfcScreenUV() { vec2 s = gl_FragCoord.xy / vec2(textureSize(uSceneColor, 0)); return vec2(s.x, 1.0 - s.y); }
struct MatIn { vec2 uv0; vec2 uv1; vec3 subUV2; vec4 vertexColor; vec3 worldPosUE; vec3 cameraVector; vec3 reflectionVector;
               vec3 normal; mat3 tbnUE; float time; float pixelDepth; vec4 screenPos; float sceneDepth;
               vec4 dynParam; };
struct MatOut { vec3 Distortion; vec3 DiffuseColor; vec3 SpecularColor; float SpecularPower; vec3 Normal;
                vec3 EmissiveColor; float Opacity; float OpacityMask; vec3 CustomLighting; float ScreenAlpha; };
// window depth -> view-space Z in UE units (UE3 PixelDepth / SceneDepth are view Z)
float wfcLinearDepth(float d) {
    float z = d * 2.0 - 1.0, n = uNearFar.x, f = uNearFar.y;
    return 2.0 * n * f / (f + n - z * (f - n)) * 100.0;
}
// UE3 DepthBiasedAlpha: Alpha * saturate((SceneDepth - PixelDepth) / max((1 - Bias) * BiasScale, 0.001))
float wfcDepthBiasedAlpha(MatIn m, float a, float bias, float scale) {
    return a * clamp((m.sceneDepth - m.pixelDepth) / max((1.0 - bias) * scale, 0.001), 0.0, 1.0);
}
// Shipped Xenon base-pass PS (FogSheet_Parent_MAT, MP_IAC_Streets ART shader cache): translucent / additive pixels with
// Opacity < 1/255 are killed (kill_gt 0, Opacity - 0.00392), and additive output is Color * Opacity (oC0 = r4.xyz =
// colour * opacity * SceneColorBiasFactor) -- an additive material's Opacity (e.g. DepthBiasedAlpha) attenuates it.
uniform float uCanvasInvGamma;   // Canvas tiles: display gamma applied as the scene post does (PARTIAL)
uniform int uLegacyTrans;     // diagnostics (WFC_M05TRANS): the pre-M06 output (opacity ignored, no kill)
void wfcTranslucentOut(inout vec3 c, float opacity) {
    if (uLegacyTrans != 0) return;
    if ((uBlend == 2 || uBlend == 3) && opacity - 0.00392157 < 0.0) discard;
    if (uBlend == 3) c *= opacity;
}
vec3 wfcCanvasOut(vec3 c) { return uCanvasInvGamma > 0.0 ? pow(max(c, vec3(0.0)), vec3(uCanvasInvGamma)) : c; }
// UE3 base-pass fog per blend mode: additive keeps no in-scatter, modulate fades toward 1.
vec3 wfcFog(vec3 c) {
    if (uBlend == 3) return c * vFog.a;
    if (uBlend == 4) return mix(vec3(1.0), c, vFog.a);
    return c * vFog.a + vFog.rgb;
}
// UE3: ReflectionVector = -CameraVector + Normal * dot(Normal, CameraVector) * 2 (tangent space)
void wfcSetNormal(inout MatIn m, vec3 n) {
    m.normal = normalize(n);
    m.reflectionVector = -m.cameraVector + m.normal * dot(m.normal, m.cameraVector) * 2.0;
}
vec3 tangentToWorldUE(MatIn m, vec3 v) { return m.tbnUE * v; }
vec3 tangentToView(MatIn m, vec3 v) { return m.tbnUE * v; }
vec3 tangentToLocal(MatIn m, vec3 v) { return v; }
)";

const char* kFSPrologue = R"(
MatIn wfcBuildInput(out mat3 tbn) {
    vec3 N = normalize(vNrm);
    if (uTwoSided != 0 && !gl_FrontFacing) N = -N;
    vec3 T = vTan.xyz - N * dot(N, vTan.xyz);
    T = dot(T, T) > 1e-12 ? normalize(T) : normalize(abs(N.x) < 0.9 ? cross(N, vec3(1, 0, 0)) : cross(N, vec3(0, 1, 0)));
    vec3 B = cross(N, T) * (vTan.w < 0.0 ? -1.0 : 1.0);
    tbn = mat3(T, B, N);
    MatIn m;
    m.uv0 = vUV0; m.uv1 = vUV1Mat; m.subUV2 = vSubUV2; m.vertexColor = vColor;
    m.worldPosUE = vPos.xzy * 100.0;                     // glTF metres -> UE units/axes
    vec3 V = normalize(uCamPos - vPos);
    m.cameraVector = vec3(dot(V, T), dot(V, B), dot(V, N));
    m.normal = vec3(0.0, 0.0, 1.0);
    m.reflectionVector = -m.cameraVector + vec3(0.0, 0.0, 2.0 * m.cameraVector.z);
    m.tbnUE = mat3(T.xzy, B.xzy, N.xzy);
    m.time = uTime;
    m.pixelDepth = wfcLinearDepth(gl_FragCoord.z);
    m.sceneDepth = uHasSceneDepth != 0 ? wfcLinearDepth(texelFetch(uSceneDepth, ivec2(gl_FragCoord.xy), 0).r) : 1e9;
    m.dynParam = uDynParam;
    // UE3 ScreenPosition (bScreenAlign false) = clip-space position. UE3's infinite-far perspective
    // gives w = view Z and z = view Z - near (UE units).
    vec2 ndc = gl_FragCoord.xy / uViewport * 2.0 - 1.0;
    m.screenPos = vec4(ndc * m.pixelDepth, m.pixelDepth - uNearFar.x * 100.0, m.pixelDepth);
    return m;
}
)";

// FDirectionalTextureLightMapPolicy base pass (decoded from the original microcode).
// Distortion accumulate (Xenon microcode of the material's distortion PS, e.g. Ring_Distort_Add_MAT):
// s = Distortion.xy * 4; kill if dot(s,s) - 0.1 < 0; s = clamp(s, -255, 255) / 255;
// out = (max(s,0), |min(s,0)|) into an 8-bit target with additive blending.
const char* kFSMainDistort = R"(
void main() {
    mat3 tbn; MatIn m = wfcBuildInput(tbn);
    MatOut o; wfcMaterial(m, o);
    vec2 s = o.Distortion.xy * 4.0;
    if (dot(s, s) - 0.1 < 0.0) discard;
    s = clamp(s, -255.0, 255.0) / 255.0;
    oColor = vec4(max(s, 0.0), abs(min(s, 0.0)));
}
)";
// Shadow depth: opaque materials write depth only; masked materials clip on OpacityMaskClipValue.
const char* kFSMainShadow = R"(
void main() {
    mat3 tbn; MatIn m = wfcBuildInput(tbn);
    MatOut o; wfcMaterial(m, o);
    if (uMasked != 0 && o.OpacityMask - uClip < 0.0) discard;
    oColor = vec4(0.0);
}
)";
// HUD post-process chain MaterialEffect (UI_GFxHud_p LowHealth / StaticDischarge, RE 6bbf2cb): the material reads
// the finished frame through SceneTexture; its output replaces the frame weighted by the WFC ScreenAlpha input
// [HIGH: the exact ScreenAlpha combine is not traced - multiply darkens toward the edges (LowHealth tunnel vision)]
const char* kFSMainScreenEffect = R"(
void main() {
    mat3 tbn; MatIn m = wfcBuildInput(tbn);
    MatOut o; wfcMaterial(m, o);
    oColor = vec4(o.EmissiveColor * clamp(o.ScreenAlpha, 0.0, 1.0), 1.0);
}
)";
const char* kFSMainLM = R"(
uniform sampler2D uLM0; uniform sampler2D uLM1; uniform sampler2D uLM2;
uniform vec3 uLMScale[3];
uniform int uVertexLM;
in vec3 vVLM0; in vec3 vVLM1; in vec3 vVLM2;
const vec3 LMB0 = vec3(0.0, 0.81649658, 0.57735027);
const vec3 LMB1 = vec3(-0.70710678, -0.40824829, 0.57735027);
const vec3 LMB2 = vec3(0.70710678, -0.40824829, 0.57735027);
void main() {
    mat3 tbn; MatIn m = wfcBuildInput(tbn);
    if (uDecalClip != 0 && (any(lessThan(vUV0, vec2(0.0))) || any(greaterThan(vUV0, vec2(1.0))))) discard;
    MatOut o; wfcMaterial(m, o);
    if (uMasked != 0 && o.OpacityMask - uClip < 0.0) discard;
    if (uDebug == 1) { o.DiffuseColor = vec3(0.5); o.EmissiveColor = vec3(0.0); o.SpecularColor = vec3(0.0); }
    if (uDebug == 2) { oColor = vec4(o.DiffuseColor, 1.0); return; }
    vec3 c = o.EmissiveColor;
    if (uLit != 0) {
        vec3 n = normalize(o.Normal);
        float w0 = dot(n, LMB0), w1 = dot(n, LMB1), w2 = dot(n, LMB2);
        vec3 l0 = uVertexLM != 0 ? vVLM0 : texture(uLM0, vUV1).rgb;
        vec3 l1 = uVertexLM != 0 ? vVLM1 : texture(uLM1, vUV1).rgb;
        vec3 l2 = uVertexLM != 0 ? vVLM2 : texture(uLM2, vUV1).rgb;
        vec3 L = w0 * w0 * l0 * uLMScale[0] + w1 * w1 * l1 * uLMScale[1] + w2 * w2 * l2 * uLMScale[2];
        c += o.DiffuseColor * L;
    }
    wfcTranslucentOut(c, o.Opacity);
    c = wfcCanvasOut(wfcFog(c));
    oColor = vec4(c, clamp(o.Opacity, 0.0, 1.0));
}
)";

// WFC TLightPixelShader<N-LightUberLightPolicy> for dynamic objects (decoded microcode).
const char* kFSMainUber = R"(
uniform vec3 uAmb[6];
uniform int uNumLights;
uniform vec4 uLPos[3];   // xyz position (m), w = 1/radius (0 = directional)
uniform vec4 uLDir[3];   // xyz: directional -> towards light; spot -> spot axis (forward); w = isSpot
uniform vec4 uLCol[3];   // rgb linear colour * brightness, w = falloff exponent
uniform vec4 uLSpot[3];  // x cos(outer), y 1/(cos(inner)-cos(outer)), w = visibility (shadow) term
// FShadowMaskPolicy (TLightPixelShader<One/Two/ThreeLightUberLightPolicy,FShadowMaskPolicy>, Xenos microcode,
// ReverseEngineering 31f9a9b): mask = ShadowMaskTexture(screenUV).x; S = (1 - mask)(1 - DSLS);
// colour = ambient * (1 - DirectLightAmbientContribution * S) + direct * (1 - S) (+ other material terms).
uniform sampler2D uShadowMask;
uniform float uDSLS;     // DynamicShadowLuminanceScale (c6.x), shipped 0, CPU-clamped to [0,1]
uniform vec3 uDLAC;      // DirectLightAmbientContribution (c38.rgb), per light environment (0x82CCE0A8)
uniform vec2 uShadowMaskTexelOffset;   // ShadowMaskTexelOffset (0.5 / mask size) (0x82DDAEF0)
void main() {
    mat3 tbn; MatIn m = wfcBuildInput(tbn);
    if (uDecalClip != 0 && (any(lessThan(vUV0, vec2(0.0))) || any(greaterThan(vUV0, vec2(1.0))))) discard;
    MatOut o; wfcMaterial(m, o);
    if (uMasked != 0 && o.OpacityMask - uClip < 0.0) discard;
    if (uDebug == 1) { o.DiffuseColor = vec3(0.5); o.EmissiveColor = vec3(0.0); o.SpecularColor = vec3(0.0); }
    if (uDebug == 2) { oColor = vec4(o.DiffuseColor, 1.0); return; }
    vec3 c = o.EmissiveColor;
    if (uLit != 0) {
        vec3 Nw = normalize(tbn * normalize(o.Normal));
        vec3 n2 = Nw * Nw;
        vec3 amb = n2.x * (Nw.x >= 0.0 ? uAmb[0] : uAmb[1]) + n2.y * (Nw.y >= 0.0 ? uAmb[2] : uAmb[3])
                 + n2.z * (Nw.z >= 0.0 ? uAmb[4] : uAmb[5]);
        float mask = texture(uShadowMask, gl_FragCoord.xy / uViewport + uShadowMaskTexelOffset).x;
        float S = (1.0 - mask) * (1.0 - uDSLS);
        vec3 ambient = o.DiffuseColor * amb;
        vec3 direct = vec3(0.0);
        vec3 V = normalize(uCamPos - vPos);
        vec3 R = reflect(-V, Nw);
        for (int i = 0; i < 3; ++i) {
            if (i >= uNumLights) break;
            vec3 L; float att = 1.0;
            if (uLPos[i].w > 0.0) {
                vec3 d = uLPos[i].xyz - vPos;
                L = normalize(d);
                vec3 dn = d * uLPos[i].w;
                att = pow(clamp(1.0 - dot(dn, dn), 0.0, 1.0), uLCol[i].w);
                if (uLDir[i].w > 0.5) {
                    float s = clamp((dot(L, -uLDir[i].xyz) - uLSpot[i].x) * uLSpot[i].y, 0.0, 1.0);
                    att *= s * s;
                }
            } else {
                L = uLDir[i].xyz;
            }
            float wr = clamp(dot(Nw, L) * 0.6778 + 0.3333, 0.0, 1.0);
            float sp = pow(max(clamp(dot(R, L), 0.0, 1.0), 0.0001), max(o.SpecularPower, 0.0001));
            direct += (o.DiffuseColor * (wr * wr) + o.SpecularColor * sp) * uLCol[i].rgb * att * uLSpot[i].w;
        }
        c += ambient * (1.0 - uDLAC * S);      // r3 = r5 * (1 - DLAC*S) + r3
        c += direct * (1.0 - S);               // r3 = r4 * (1 - S) + r3: one mask for the summed lights
    }
    wfcTranslucentOut(c, o.Opacity);
    c = wfcCanvasOut(wfcFog(c));
    oColor = vec4(c, clamp(o.Opacity, 0.0, 1.0));
}
)";

const char* kPostVS = R"(#version 330 compatibility
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// UE3 DOFAndBloom gather (decoded TDOFAndBloomGatherPixelShader): 4 taps of scene colour clamped
// to [0,4] and their linear depths. Bloom = taps where any channel exceeds BloomThreshold, summed,
// * 0.25 * BloomScale. DOF weight a' = min(MaxBlur, pow(sat(|avgDepth - FocusDistance| * InvFalloff),
// FalloffExponent)) (near/far parameters by sign). Output rgb = avgScene * a' + bloom, alpha = a'.
const char* kBloomGatherFS = R"(#version 330 compatibility
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform vec2 uTexel;
uniform float uBloomScale, uBloomThreshold;
uniform vec4 uDofPacked;      // FocusDistance, 1/NearFalloff, FalloffExponent, 1/FarFalloff (UE units)
uniform vec2 uDofMaxBlur;     // MaxNearBlurAmount, MaxFarBlurAmount
uniform int uDofOn;
uniform vec2 uNearFar;        // camera near/far (m)
layout(location=0) out vec4 oColor;
float linDepthUE(vec2 uv) {
    float d = texture(uDepth, uv).r * 2.0 - 1.0;
    float n = uNearFar.x, f = uNearFar.y;
    return (2.0 * n * f / (f + n - d * (f - n))) * 100.0;
}
void main() {
    vec3 scene = vec3(0.0), bloom = vec3(0.0); float dsum = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 o = vec2((i & 1) == 0 ? -1.0 : 1.0, (i & 2) == 0 ? -1.0 : 1.0);
        vec2 uv = vUV + o * uTexel;
        vec3 c = clamp(texture(uScene, uv).rgb, 0.0, 4.0);
        scene += c;
        if (any(greaterThan(c, vec3(uBloomThreshold)))) bloom += c;
        dsum += linDepthUE(uv);
    }
    float a = 0.0;
    if (uDofOn != 0) {
        float dz = dsum * 0.25 - uDofPacked.x;
        float inv = dz >= 0.0 ? uDofPacked.w : uDofPacked.y;
        float mx = dz > 0.0 ? uDofMaxBlur.y : uDofMaxBlur.x;
        a = min(mx, pow(max(clamp(abs(dz) * inv, 0.0, 1.0), 0.0001), uDofPacked.z));
    }
    oColor = vec4(scene * 0.25 * a + bloom * 0.25 * uBloomScale, a);
}
)";

// Separable Gaussian (UE3 DOFAndBloom blur) over rgb + DOF weight. [PROV] kernel: radius =
// DOF_BlurKernelSize (16 px full-res) / 4 at quarter resolution, sigma = radius / 2.
const char* kBlurFS = R"(#version 330 compatibility
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uStep;
layout(location=0) out vec4 oColor;
void main() {
    const int R = 4;
    float sigma = float(R) * 0.5;
    vec4 acc = vec4(0.0); float wsum = 0.0;
    for (int i = -R; i <= R; ++i) {
        float w = exp(-0.5 * float(i * i) / (sigma * sigma));
        acc += texture(uSrc, vUV + uStep * float(i)) * w; wsum += w;
    }
    oColor = acc / wsum;
}
)";

// FUberPostProcessPixelShader + ColorCorrection variant (decoded):
//   a = DOF weight at full-res depth; c = ((1-a)*scene + blur.rgb) / ((1-a) + blur.a)
//   -> sat(c - SceneShadows) * InvHighLights -> pow(MidTones) -> x*(1-Desat)+lum*Desat+Overlay
//   -> sat(* ColorScale) -> pow(1/DisplayGamma) -> tex3D(CLUT, c * Scale + Bias).
// Settings: the map's TnWorldInfo.DefaultPostProcessSettings over Engine Default__WorldInfo.
const char* kPostFS = R"(#version 330 compatibility
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform sampler2D uDepth;
uniform sampler3D uClut;
uniform int uBloomOn, uClutOn, uDofOn;
uniform vec4 uDofPacked;
uniform vec2 uDofMaxBlur;
uniform vec2 uNearFar;
uniform vec2 uClutScaleBias;
uniform vec3 uShadows, uInvHighLights, uMidTones;
uniform float uDesat;
uniform float uInvGamma;
layout(location=0) out vec4 oColor;
void main() {
    vec3 scene = texture(uScene, vUV).rgb;
    float a = 0.0;
    if (uDofOn != 0) {
        float d = texture(uDepth, vUV).r * 2.0 - 1.0;
        float n = uNearFar.x, f = uNearFar.y;
        float dz = (2.0 * n * f / (f + n - d * (f - n))) * 100.0 - uDofPacked.x;
        float inv = dz >= 0.0 ? uDofPacked.w : uDofPacked.y;
        float mx = dz > 0.0 ? uDofMaxBlur.y : uDofMaxBlur.x;
        a = min(mx, pow(max(clamp(abs(dz) * inv, 0.0, 1.0), 0.0001), uDofPacked.z));
    }
    vec4 blur = uBloomOn != 0 ? texture(uBloom, vUV) : vec4(0.0);
    vec3 c = ((1.0 - a) * scene + blur.rgb) / max((1.0 - a) + blur.a, 0.0001);
    c = clamp(c - uShadows, 0.0, 1.0) * uInvHighLights;
    c = pow(max(c, vec3(0.0001)), uMidTones);
    c = c * (1.0 - uDesat) + vec3(dot(c, vec3(0.3, 0.59, 0.11)) * uDesat);
    c = clamp(c, 0.0, 1.0);
    c = pow(max(c, vec3(0.0001)), vec3(uInvGamma));
    if (uClutOn != 0) c = texture(uClut, c * uClutScaleBias.x + uClutScaleBias.y).rgb;
    oColor = vec4(c, 1.0);
}
)";

GLuint compile(GLenum type, const std::string& src0, const std::string& tag) {
    ++gShaderCompiles;
    GLuint s = CreateShader(type);
    // diagnostics: WFC_SHADERNONCE=<n> makes every shader source unique (a cold driver shader cache for this process,
    // as on the first run of a new build) without touching the driver's cache on disk
    static const char* nonce = std::getenv("WFC_SHADERNONCE");
    std::string src = src0;
    if (nonce) {
        const size_t nl = src.find('\n');
        if (nl != std::string::npos) src.insert(nl + 1, std::string("#define WFC_NONCE_") + nonce + "\n");
    }
    const char* p = src.c_str();
    ShaderSource(s, 1, &p, nullptr);
    CompileShader(s);
    GLint ok = 0; GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint n = 0; GetShaderiv(s, GL_INFO_LOG_LENGTH, &n);
        std::string log((size_t)std::max(n, 1), '\0');
        GetShaderInfoLog(s, n, nullptr, &log[0]);
        LOG_ERROR("shader compile failed (%s): %s", tag.c_str(), log.c_str());
        if (std::getenv("WFC_DUMPSHADER")) {
            std::ofstream f("shader_fail_" + std::to_string(s) + ".glsl"); f << src;
        }
        DeleteShader(s);
        return 0;
    }
    return s;
}

// M54: linked programs survive map / frontend-scene loads (revisits do not recompile), keyed by the fragment source
// (the vertex shader is shared). Bounded: at unload the least recently used programs beyond kProgCacheMax that the
// unloading pipeline did not use are deleted. Cleared with the renderer (Pipeline::clearProgramCache).
namespace {
struct CachedProg { GLuint id = 0; uint64_t lastUse = 0; };
std::map<std::string, CachedProg> gProgCache;
std::map<GLuint, const std::string*> gProgCacheById;
uint64_t gProgUse = 0;
constexpr size_t kProgCacheMax = 1500;
} // namespace

GLuint link(GLuint vs, GLuint fs, const std::string& tag) {
    GLuint p = CreateProgram();
    AttachShader(p, vs); AttachShader(p, fs);
    LinkProgram(p);
    GLint ok = 0; GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint n = 0; GetProgramiv(p, GL_INFO_LOG_LENGTH, &n);
        std::string log((size_t)std::max(n, 1), '\0');
        GetProgramInfoLog(p, n, nullptr, &log[0]);
        LOG_ERROR("program link failed (%s): %s", tag.c_str(), log.c_str());
        return 0;
    }
    return p;
}

GLuint makeTex1x1(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    GLuint id; glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    uint8_t px[4] = {r, g, b, a};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return id;
}

} // namespace

GLuint compileShader(GLenum type, const std::string& src, const std::string& tag) { return compile(type, src, tag); }
GLuint linkProgram(GLuint vs, GLuint fs, const std::string& tag) { return link(vs, fs, tag); }

std::string Pipeline::assetRoot() {
    if (const char* e = std::getenv("WFC_ASSETS")) return e;
    return core::config::kAssetRootDefault;
}

// The worktree's work/render above the executable, whatever the build layout (build/bin, build/release/bin, a
// copied bin/): a candidate counts only if it holds render data (a map's materials_glsl.json or the _ui data).
// M10 root cause: this was <exe>/../../work/render only, so the Release layout (build/release/bin) resolved to
// build/work/render (absent) and every map and frontend scene silently fell back to the legacy fixed-function
// renderer (human playtest M06: black world, malformed menu scene).
bool Pipeline::groundBelowUE(float x, float y, float zFrom, float& zHit) const {
    bool hit = false;
    for (size_t t = 0; t + 9 <= bspTris_.size(); t += 9) {
        const float* v = &bspTris_[t];
        // 2D barycentric containment of (x, y) in the triangle's XY projection, then the plane height there
        float x0 = v[0], y0 = v[1], x1 = v[3], y1 = v[4], x2 = v[6], y2 = v[7];
        float d = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
        if (std::fabs(d) < 1e-6f) continue;              // vertical / degenerate in plan view
        float a = ((y1 - y2) * (x - x2) + (x2 - x1) * (y - y2)) / d;
        float b = ((y2 - y0) * (x - x2) + (x0 - x2) * (y - y2)) / d;
        float c = 1.0f - a - b;
        if (a < -1e-5f || b < -1e-5f || c < -1e-5f) continue;
        float z = a * v[2] + b * v[5] + c * v[8];
        if (z <= zFrom + 0.01f && (!hit || z > zHit)) { zHit = z; hit = true; }
    }
    return hit;
}

std::string Pipeline::renderDataRoot() {
    if (const char* e = std::getenv("WFC_RENDER_DATA")) return e;
    static const std::string root = [] {
        auto holdsData = [](const std::string& c) {
            if (std::ifstream(c + "/_ui/hud_markers.json").good()) return true;
            for (const char* m : {"MP_IAC_Streets", "UI_FrontEnd"})
                if (std::ifstream(c + "/" + m + "/materials_glsl.json").good()) return true;
            return false;
        };
        std::string dir = exeDir();
        for (int up = 0; up <= 4; ++up) {
            if (holdsData(dir + "/work/render")) {
                LOG_INFO("wfc: render data root %s/work/render", dir.c_str());
                return dir + "/work/render";
            }
            dir += "/..";
        }
        LOG_ERROR("wfc: NO RENDER DATA found in work/render above %s (set WFC_RENDER_DATA or run "
                  "tools/render/build_render_data.ps1): maps and frontend scenes will use the legacy renderer",
                  exeDir().c_str());
        return exeDir() + "/../../work/render";
    }();
    return root;
}

std::string Pipeline::contentRoot() {
    if (const char* e = std::getenv("WFC_CONTENT")) return std::string(e) + "/";
    std::string a = assetRoot();
    size_t s = a.find_last_of("/\\");
    return (s == std::string::npos ? std::string(".") : a.substr(0, s)) + "/content/";
}

// Prewarm: build every compiled original material not yet used (effect, weapon materials) and decode its textures,
// as the original had them resident from the map's cooked packages before combat - instead of on first draw (the
// first-shot hitch). M54: run by the map loader with the load yields (it used to run on frame 2: a ~1.2 s stall after
// the loading screen); frontend scenes skip it (no weapons / effects of a match are drawn there).
void Pipeline::prewarmMaterials() {
    if (prewarmDone_) return;
    prewarmDone_ = true;
    auto t0 = std::chrono::steady_clock::now(), lastYield = t0;
    int built = 0;
    for (const auto& kv : mats_) {
        if (progIndex_.count(kv.first + "|UBER") || progIndex_.count(kv.first + "|LM")) continue;
        // roster character materials (TR_ packages) compile when their character is first drawn (or prewarmed by
        // prewarmDynamicMesh): the render data carries every MP chassis, only the ones in the match are needed
        if (kv.first.rfind("TR_", 0) == 0) continue;
        if (programFor(kv.first, nullptr, false) >= 0) ++built;
        auto now = std::chrono::steady_clock::now();     // time-sliced: each yield presents a loading frame
        if (std::chrono::duration<double, std::milli>(now - lastYield).count() >= 16.0) { yieldLoad(); lastYield = now; }
    }
    LOG_INFO("wfc: prewarmed %d material programs in %.0f ms (%d linked programs reused)", built,
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(), progCacheHits_);
}

void Pipeline::clearProgramCache() {
    const bool ctx = glGetString(GL_VERSION) != nullptr;    // no current context: the objects died with it
    for (auto& kv : gProgCache) if (ctx && kv.second.id) DeleteProgram(kv.second.id);
    gProgCache.clear();
    gProgCacheById.clear();
}

// ------------------------------------------------------------------------- unloading (level travel)
void Pipeline::release() {
    evictPosed(true);                                  // drawDynamicMeshPosed buffers belong to the map / context
    evictSkin(true);
    instGroups_.clear(); instGroupIndex_.clear();
    if (gInstPipeline == this) gInstPipeline = nullptr;
    touchQueue_.clear();                               // its textures are deleted with the map
    if (mdiRowTex_) { glDeleteTextures(1, &mdiRowTex_); mdiRowTex_ = 0; }
    if (lmArray_) { glDeleteTextures(1, &lmArray_); lmArray_ = 0; }
    for (GLuint* b : {&mdiRowVbo_, &mdiCmdBuf_}) if (*b) { DeleteBuffers(1, b); *b = 0; }
    mdiBuckets_.clear(); mdiMesh_ = -1;
    progTouchQueue_.clear();
    if (!active_ && meshes_.empty() && !fbo_) return;
    auto tex = [](GLuint& t) { if (t) { glDeleteTextures(1, &t); t = 0; } };
    auto fbo = [](GLuint& f) { if (f) { DeleteFramebuffers(1, &f); f = 0; } };
    auto buf = [](GLuint& b) { if (b) { DeleteBuffers(1, &b); b = 0; } };
    auto vao = [](GLuint& v) { if (v) { DeleteVertexArrays(1, &v); v = 0; } };
    auto prog = [](GLuint& p) { if (p) { DeleteProgram(p); p = 0; } };
    for (GpuMesh& g : meshes_) {
        vao(g.vao); buf(g.vbo); buf(g.ibo);
        for (Sub& s : g.subs) tex(s.vlmTex);
    }
    std::set<GLuint> progIds;
    for (const Program& p : progs_) if (p.id) progIds.insert(p.id);
    for (GLuint id : progIds) if (!gProgCacheById.count(id)) { GLuint p = id; prog(p); }   // uncached (none expected)
    if (gProgCache.size() > kProgCacheMax) {      // trim: least recently used, not used by this pipeline
        std::vector<std::pair<uint64_t, const std::string*>> lru;
        for (const auto& kv : gProgCache) if (!progIds.count(kv.second.id)) lru.push_back({kv.second.lastUse, &kv.first});
        std::sort(lru.begin(), lru.end());
        size_t drop = std::min(lru.size(), gProgCache.size() - kProgCacheMax);
        for (size_t i = 0; i < drop; ++i) {
            auto it = gProgCache.find(*lru[i].second);
            GLuint p = it->second.id;
            gProgCacheById.erase(p);
            prog(p);
            gProgCache.erase(it);
        }
    }
    for (auto& kv : texCache_) tex(kv.second);
    for (GLuint& t : lmTextures_) tex(t);
    for (GLuint* t : {&clutTex_, &neutralMaskTex_, &testMaskTex_, &whiteTex_, &blackTex_, &flatNormalTex_, &blackCube_,
                      &colorTex_, &depthTex_, &depthCopyTex_, &distTex_, &sceneCopyTex_, &shadowDepthTex_,
                      &randomAnglesTex_, &maskTex_, &maskTmpTex_, &maskBlurTex_, &bloomTex_[0], &bloomTex_[1]})
        tex(*t);
    for (GLuint* f : {&fbo_, &depthCopyFbo_, &distFbo_, &sceneCopyFbo_, &shadowFbo_, &maskFbo_, &maskTmpFbo_,
                      &maskBlurFbo_, &bloomFbo_[0], &bloomFbo_[1]})
        fbo(*f);
    if (maskDepthRb_) { DeleteRenderbuffers(1, &maskDepthRb_); maskDepthRb_ = 0; }
    for (GLuint* v : {&dynVao_, &postVao_, &spriteVao_, &volVao_, &screenFxVao_}) vao(*v);
    for (GLuint* b : {&screenFxVbo_, &screenFxIbo_}) buf(*b);
    for (GLuint* b : {&dynVbo_, &dynIbo_, &spriteVbo_, &spriteCbo_, &spriteIbo_, &volVbo_, &spriteSubBo_}) buf(*b);
    for (GLuint* p : {&postProg_, &bloomGatherProg_, &blurProg_, &distApplyProg_, &shadowProjProg_, &maskDepthProg_,
                      &constProg_, &maskBlurProg_})
        prog(*p);
    BindFramebuffer(GL_FRAMEBUFFER, 0);
    LOG_INFO("wfc: released map render data (%zu meshes, %zu programs (%d reused from the program cache, %zu cached), "
             "%zu textures)", meshes_.size(), progIds.size(), progCacheHits_, gProgCache.size(),
             texCache_.size() + lmTextures_.size());
    std::function<void()> keepYield = std::move(loadYield_);
    const float keepGamma = displayGamma_;              // caller settings survive a map change
    *this = Pipeline();
    loadYield_ = std::move(keepYield);
    displayGamma_ = keepGamma;
}

// ------------------------------------------------------------------------- loading
bool Pipeline::load(const std::string& mapName) {
    if (std::getenv("WFC_LEGACYRENDER")) { LOG_INFO("wfc: WFC_LEGACYRENDER set; shader path disabled"); return false; }
    std::string root = renderDataRoot();
    dataDir_ = root + "/" + mapName;
    std::string mj = readText(dataDir_ + "/materials_glsl.json");
    yieldLoad();
    std::string lj = readText(dataDir_ + "/lighting.json");
    yieldLoad();
    loadMovers(dataDir_ + "/movers.json");
    yieldLoad();
    loadMapFx(dataDir_ + "/map_fx_runtime.json");
    {   // MaterialInstanceActor -> MIC (build_materials.py): Matinee material-parameter routing
        assets::Json MJ;
        std::string mt = readText(dataDir_ + "/material_instance_actors.json");
        if (!mt.empty() && assets::Json::parse(mt, MJ))
            for (const auto& kv : MJ["actors"].obj) {
                std::string a = kv.first, m = kv.second.asString();
                std::transform(a.begin(), a.end(), a.begin(), ::tolower);
                std::transform(m.begin(), m.end(), m.begin(), ::tolower);
                miaMaterial_[a] = m;
            }
    }
    yieldLoad();
    if (mj.empty() || lj.empty()) {
        lastLoadError() = "render data not found in " + dataDir_;
        LOG_ERROR("wfc: render data not found in %s (run tools/render/build_render_data.ps1): LEGACY RENDERER, "
                  "not the original presentation", dataDir_.c_str());
        return false;
    }
    if (!glx::load()) {
        lastLoadError() = "GL 3.3 entry points unavailable";
        LOG_ERROR("wfc: GL 3.3 entry points unavailable: LEGACY RENDERER");
        return false;
    }
    glx::installDebugOutput();
    lastLoadError().clear();

    assets::Json M, L;
    if (!assets::Json::parse(mj, M) || !assets::Json::parse(lj, L)) { LOG_ERROR("wfc: render data JSON parse failed"); return false; }

    for (const auto& kv : M.obj) {
        const assets::Json& e = kv.second;
        if (!e["glsl"].isString()) {      // compile error in build_materials: remembered for the draw-time warning
            if (e["error"].isString()) matErrors_[kv.first] = e["error"].asString();
            continue;
        }
        MatSrc s;
        s.glsl = e["glsl"].asString();
        for (size_t k = 0; k < e["info"]["runtime_params"].size(); ++k)
            s.rtParams.push_back(e["info"]["runtime_params"][k].asString());
        const assets::Json& info = e["info"];
        const std::string bm = info["blend_mode"].asString();
        s.blend = bm == "BLEND_Masked" ? 1 : bm == "BLEND_Translucent" ? 2 : bm == "BLEND_Additive" ? 3
                : bm == "BLEND_Modulate" ? 4 : 0;
        s.twoSided = info["two_sided"].asBool(false);
        s.lit = info["lighting_model"].asString() != "MLM_Unlit";
        s.clip = info["opacity_mask_clip"].asFloat(0.3333f);
        const assets::Json& tx = info["textures"];
        for (size_t i = 0; i < tx.size(); ++i) {
            const assets::Json& t = tx[i];
            s.files.push_back(t["file"].asString());
            s.srgb.push_back(t["srgb"].asBool(true));
            s.cube.push_back(t["kind"].asString() == "cube");
            std::vector<std::string> fc;
            for (size_t k = 0; k < t["faces"].size(); ++k) fc.push_back(t["faces"][k].asString());
            s.faces.push_back(fc);
            s.clampU.push_back(t["address_x"].asString() == "TA_Clamp");
            s.clampV.push_back(t["address_y"].asString() == "TA_Clamp");
            std::vector<float> mn(4, 0.0f), mx(4, 1.0f);
            if (t["unpack_min"].isArray()) for (int k = 0; k < 4; ++k) mn[k] = t["unpack_min"][(size_t)k].asFloat(0.0f);
            s.umin.push_back(mn); s.umax.push_back(mx);
        }
        mats_[kv.first] = std::move(s);
    }

    {   // M09 Beast light-probe grids (beast_probes.py; RE MILESTONE03 lightvis add. 2)
        assets::Json B;
        std::string bj = readText(dataDir_ + "/beast_probes.json");
        if (!bj.empty() && assets::Json::parse(bj, B))
            for (const assets::Json& v : B["volumes"].arr) {
                BeastVolume bv;
                for (int i = 0; i < 3; ++i) {
                    bv.loc[i] = v["location"][(size_t)i].asFloat(); bv.scale[i] = v["scale"][(size_t)i].asFloat(1.0f);
                    bv.n[i] = v["points"][(size_t)i].asInt(1);
                }
                bv.priority = v["priority"].asInt(0);
                bv.sh.reserve(v["sh"].size());
                for (const assets::Json& f : v["sh"].arr) bv.sh.push_back((float)f.asDouble());
                if (bv.sh.size() == (size_t)bv.n[0] * bv.n[1] * bv.n[2] * 27) beast_.push_back(std::move(bv));
            }
        if (!beast_.empty()) LOG_INFO("wfc: %zu Beast light-probe volume(s)", beast_.size());
    }
    {   // M74 energy-death instances by form-mesh package (build_materials energy_death.json)
        assets::Json E;
        std::string ej = readText(dataDir_ + "/energy_death.json");
        if (!ej.empty() && assets::Json::parse(ej, E))
            for (const auto& kv : E["by_package"].obj) energyDeath_[kv.first] = kv.second.asString();
    }
    {
        assets::Json S;
        std::string sj = readText(dataDir_ + "/slot_materials.json");
        if (!sj.empty() && assets::Json::parse(sj, S))
            for (const auto& kv : S.obj) slotMaterials_[kv.first] = kv.second.asString();
    }
    const assets::Json& lp = L["lightmaps"]["props"];
    for (const auto& kv : lp.obj) {
        LMRec r;
        for (int i = 0; i < 3; ++i) {
            r.coeff[i] = kv.second["coeffs"][(size_t)i].asString();
            for (int c = 0; c < 3; ++c) r.scale[i][c] = kv.second["scales"][(size_t)i][(size_t)c].asFloat(1.0f);
        }
        r.cs[0] = kv.second["coordScale"][0].asFloat(1); r.cs[1] = kv.second["coordScale"][1].asFloat(1);
        r.cb[0] = kv.second["coordBias"][0].asFloat(0); r.cb[1] = kv.second["coordBias"][1].asFloat(0);
        std::string key = kv.first;
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        lightmaps_[key] = r;
        // <level>.TheWorld.PersistentLevel.<Actor>.<Component>: index single-component actors
        size_t a = key.find(".persistentlevel.");
        if (a != std::string::npos) {
            std::string rest = key.substr(a + 17);
            size_t d = rest.find('.');
            if (d != std::string::npos) {
                std::string actor = rest.substr(0, d);
                auto ins = actorComponent_.emplace(actor, key);
                if (!ins.second) ins.first->second.clear();   // several components: ambiguous
            }
        }
    }

    // LMT_1D vertex lightmaps: A,R,G,B bytes per coefficient, per-channel normalized. Decode from the
    // Xenon vertex-lightmap VS (BASE shader cache, LightMapScale[3]): exp2(log2(max(|c|, 0.0001)) * 2.2)
    // * LightMapScale[k], per vertex [CONFIRMED microcode].
    const assets::Json& jv = L["vertex_lightmaps"];
    for (const auto& kv : jv.obj) {
        VertexLM v;
        v.count = kv.second["count"].asInt(0);
        const std::string hex = kv.second["samples_hex"].asString();
        if (v.count <= 0 || hex.size() != (size_t)v.count * 24) continue;
        for (int k = 0; k < 3; ++k)
            for (int c = 0; c < 3; ++c) v.scale[k][c] = kv.second["scales"][(size_t)k][(size_t)c].asFloat(1.0f);
        auto byteAt = [&](size_t i) { return (float)std::stoi(hex.substr(i * 2, 2), nullptr, 16); };
        v.rgb.resize((size_t)v.count * 9);   // row-major: coefficient k, vertex i, channel c
        for (int i = 0; i < v.count; ++i)
            for (int k = 0; k < 3; ++k)
                for (int c = 0; c < 3; ++c)
                    v.rgb[((size_t)k * v.count + i) * 3 + c] =
                        std::pow(std::max(byteAt(((size_t)i * 3 + k) * 4 + 1 + c) / 255.0f, 0.0001f), 2.2f);
        std::string key = kv.first;
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        vertexLMs_[key] = std::move(v);
    }

    {   // LightsVisibilitiesVolume: load + structural self-check; used once decode() lands (ReVa layout)
        const assets::Json& jv2 = L["light_visibility_volumes"];
        if (jv2.size() > 0) {
            const assets::Json& v = jv2[0];
            if (lvv_.load(dataDir_ + "/" + v["file"].asString())) {
                std::string why;
                bool ok = lvv_.selfCheck(why);
                LOG_INFO("wfc: LightsVisibilitiesVolume %s: %s", ok ? "decoded" : "FAILED", why.c_str());
            }
        }
    }

    const assets::Json& jf = L["component_flags"];
    for (const auto& kv : jf.obj) {
        std::string key = kv.first;
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        if (kv.second["hidden"].asBool(false)) hiddenComponents_.insert(key);
        if (kv.second["no_lights"].asBool(false)) noLightComponents_.insert(key);
        if (kv.second["dynamic_channel"].asBool(false)) dynChannelComponents_.insert(key);
    }

    const assets::Json& jl = L["lights"];
    for (size_t i = 0; i < jl.size(); ++i) {
        const assets::Json& e = jl[i];
        const std::string cls = e["class"].asString();
        Light l;
        l.type = cls == "SpotLightComponent" ? 1 : cls == "DirectionalLightComponent" ? 2 : cls == "PointLightComponent" ? 0
               : cls == "SkyLightComponent" ? 3 : -1;
        if (l.type < 0) continue;
        l.pos = {e["position"][0].asFloat(), e["position"][1].asFloat(), e["position"][2].asFloat()};
        l.dir = core::normalize(core::Vec3{e["direction"][0].asFloat(), e["direction"][1].asFloat(), e["direction"][2].asFloat()});
        float b = e["brightness"].asFloat(1.0f);
        const assets::Json& c = e["color_srgb8"];
        l.color = {srgbToLinear(c[0].asFloat(255) / 255.0f) * b, srgbToLinear(c[1].asFloat(255) / 255.0f) * b,
                   srgbToLinear(c[2].asFloat(255) / 255.0f) * b};
        {
            const assets::Json& lc = e["lower_color_srgb8"];
            float lb = e["lower_brightness"].asFloat(0.0f);
            l.lowerColor = {srgbToLinear(lc[0].asFloat(255) / 255.0f) * lb, srgbToLinear(lc[1].asFloat(255) / 255.0f) * lb,
                            srgbToLinear(lc[2].asFloat(255) / 255.0f) * lb};
        }
        l.radius = e["radius_m"].asFloat(10.24f);
        l.falloff = e["falloff_exponent"].asFloat(2.0f);
        float outer = core::radians(e["outer_cone_deg"].asFloat(44.0f));
        float inner = core::radians(e["inner_cone_deg"].asFloat(0.0f));
        l.cosOuter = std::cos(outer);
        float cin = std::cos(std::min(inner, outer));
        l.invConeRange = 1.0f / std::max(cin - l.cosOuter, 0.001f);
        l.name = e["name"].asString();
        l.castDynamicShadows = e["cast_dynamic_shadows"].asBool(true);
        l.castCompositeShadow = e["cast_composite_shadow"].asBool(false);
        l.castStaticShadows = e["cast_static_shadows"].asBool(true);
        l.radiusOfInfluence = e["radius_of_influence_m"].asFloat(-0.01f);
        {   // LightingChannels -> bits (MSB-first); an absent struct keeps the LightComponent default
            static const std::pair<const char*, uint32_t> kBits[] = {
                {"bInitialized", 0x80000000u}, {"BSP", 0x40000000u}, {"Static", 0x20000000u}, {"Dynamic", 0x10000000u},
                {"CompositeDynamic", 0x08000000u}, {"Skybox", 0x04000000u}, {"Unnamed_1", 0x02000000u},
                {"Unnamed_2", 0x01000000u}, {"Unnamed_3", 0x00800000u}, {"Unnamed_4", 0x00400000u},
                {"Unnamed_5", 0x00200000u}, {"Unnamed_6", 0x00100000u}, {"Cinematic_1", 0x00080000u},
                {"Cinematic_2", 0x00040000u}, {"Cinematic_3", 0x00020000u}, {"Cinematic_4", 0x00010000u},
                {"Cinematic_5", 0x00008000u}, {"Cinematic_6", 0x00004000u}, {"Gameplay_1", 0x00002000u},
                {"Gameplay_2", 0x00001000u}, {"Gameplay_3", 0x00000800u}, {"Gameplay_4", 0x00000400u},
                {"Crowd", 0x00000200u}, {"PlayerOnly", 0x00000100u}, {"NPCOnly", 0x00000080u}};
            const assets::Json& ch = e["channels"];
            uint32_t m = 0;
            bool any = false;
            for (const auto& kb : kBits)
                if (ch.has(kb.first)) { any = true; if (ch[kb.first].asBool(false)) m |= kb.second; }
            l.chMask = any ? m : 0xF8000000u;     // Default__LightComponent: bInitialized BSP Static Dynamic CompositeDynamic
        }
        {   // spot IntensityAt cones (0x82E2CFE0): inner clamp [0, 89] deg; outer clamp [inner + 0.001, 1.5543429] rad
            float inner = std::min(std::max(e["inner_cone_deg"].asFloat(0.0f), 0.0f), 89.0f) * 0.017453292f;
            float outer = std::min(std::max(e["outer_cone_deg"].asFloat(44.0f) * 0.017453292f, inner + 0.001f), 1.5543429f);
            l.spotCosI = std::cos(inner); l.spotCosO = std::cos(outer); l.spotOuterRad = outer;
        }
        l.hasLightFunction = e["has_light_function"].asBool(false);
        l.brightness = e["brightness"].asFloat(1.0f);
        l.colorByte = {e["color_srgb8"][0].asFloat(255) / 255.0f, e["color_srgb8"][1].asFloat(255) / 255.0f,
                       e["color_srgb8"][2].asFloat(255) / 255.0f};
        {
            const assets::Json& g = e["light_guid"];
            l.guid.a = (uint32_t)g[0].asDouble(0); l.guid.b = (uint32_t)g[1].asDouble(0);
            l.guid.c = (uint32_t)g[2].asDouble(0); l.guid.d = (uint32_t)g[3].asDouble(0);
        }
        for (int k = 0; k < 4; ++k) l.modShadowColor[k] = e["mod_shadow_color"][(size_t)k].asFloat(k == 3 ? 1.0f : 0.0f);
        l.shadowFalloffExponent = e["shadow_falloff_exponent"].asFloat(2.0f);
        l.minShadowResolution = (int)e["min_shadow_resolution"].asDouble(0);
        l.maxShadowResolution = (int)e["max_shadow_resolution"].asDouble(0);
        l.chStatic = e["channels"]["Static"].asBool(true);
        l.chDynamic = e["channels"]["Dynamic"].asBool(true) || e["channels"]["CompositeDynamic"].asBool(true);
        l.castShadows = e["cast_shadows"].asBool(true);
        l.enabled = e["enabled"].asBool(true);
        lights_.push_back(l);
    }

    const assets::Json& F = L["fog"];
    if (F.isObject() && F["bEnabled"].asBool(true)) {
        fogOn_ = !std::getenv("WFC_NOFOG");
        fogMaxH_ = F["Height"].asFloat(0.0f);
    if (lvv_.valid()) {                  // bind the octree light table by LightComponent.LightGuid
        std::vector<LightVisibilityVolume::Guid> g;
        for (const Light& l : lights_) g.push_back(l.guid);
        lvv_.bind(g);
        int bound = 0;
        for (size_t i = 0; i < lights_.size(); ++i) bound += lvv_.isBaked((int)i) ? 1 : 0;
        LOG_INFO("wfc: LightsVisibilitiesVolume: %d of %zu level lights bound by LightGuid (%zu table entries)", bound,
                 lights_.size(), lvv_.lightGuids().size());
        if (std::getenv("WFC_DLETEST")) runDirectLightEnvSelfTest();
        if (const char* dump = std::getenv("WFC_LVVDUMP")) {   // cross-check vs tools/render/lvv_decode.py
            FILE* df = std::fopen(dump, "w");
            uint32_t s = 2024u;
            auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (float)(s >> 8) / 16777216.0f; };
            for (int i = 0; i < 400 && df; ++i) {
                // 360 points around the playable area + 40 outside the root cube
                core::Vec3 p = i < 360 ? core::Vec3{-20000.0f + rnd() * 80000.0f, -70000.0f + rnd() * 50000.0f, -74000.0f + rnd() * 8000.0f}
                                       : core::Vec3{60000.0f + rnd() * 10000.0f, 0.0f, -72000.0f};
                std::vector<int> ls; std::vector<float> vs;
                bool hit = lvv_.query(p, ls, vs);
                std::fprintf(df, "%.3f %.3f %.3f %d", p.x, p.y, p.z, hit ? 1 : 0);
                for (size_t k = 0; k < ls.size(); ++k) std::fprintf(df, " %s:%d", lights_[(size_t)ls[k]].name.c_str(), (int)std::lround(vs[k] * 65535.0f));
                std::fprintf(df, "\n");
            }
            if (df) std::fclose(df);
        }
    }
        // [MED] UE3 FHeightFogSceneInfo: FogDistanceScale = -Density / ln(2) (exp2 in shader),
        // FogInScattering = FLinearColor(LightColor) * LightBrightness.
        fogScale_ = -F["Density"].asFloat(5e-5f) / std::log(2.0f);
        fogStart_ = F["StartDistance"].asFloat(0.0f);
        fogExt_ = F["ExtinctionDistance"].asFloat(1e8f);
        const assets::Json& c = F["LightColor"];
        float br = F["LightBrightness"].asFloat(0.1f);
        fogIn_ = {srgbToLinear(c[0].asFloat(255) / 255.0f) * br, srgbToLinear(c[1].asFloat(255) / 255.0f) * br,
                  srgbToLinear(c[2].asFloat(255) / 255.0f) * br};
    }

    {
        const assets::Json& P = L["postprocess"]["settings"];
        auto f = [&](const char* k, float d) { return P[k].asFloat(d); };
        post_.bloom = P["bEnableBloom"].asBool(true);
        post_.dof = P["bEnableDOF"].asBool(false);
        post_.bloomScale = f("Bloom_Scale", 1.0f);
        post_.bloomThreshold = f("Bloom_Threshold", 1.0f);
        // [MED] UE3 DOF PackedParameters = (FocusDistance, 1/FocusNearFalloff, FalloffExponent,
        // 1/FocusFarFalloff), MinMaxBlurClamp = (MaxNear, MaxFar): layout from the decoded gather/uber PS.
        post_.dofPacked[0] = f("DOF_FocusDistance", 0.0f);
        post_.dofPacked[1] = 1.0f / std::max(f("DOF_FocusNearFalloff", 2000.0f), 1.0f);
        post_.dofPacked[2] = f("DOF_FalloffExponent", 4.0f);
        post_.dofPacked[3] = 1.0f / std::max(f("DOF_FocusFarFalloff", 2000.0f), 1.0f);
        post_.dofMaxBlur[0] = f("DOF_MaxNearBlurAmount", 1.0f);
        post_.dofMaxBlur[1] = f("DOF_MaxFarBlurAmount", 1.0f);
        auto v3 = [&](const char* k, core::Vec3 d) {
            const assets::Json& a = P[k];
            return a.isArray() ? core::Vec3{a[0].asFloat(d.x), a[1].asFloat(d.y), a[2].asFloat(d.z)} : d;
        };
        post_.shadows = v3("Scene_Shadows", {0, 0, 0});
        post_.highlights = v3("Scene_HighLights", {1, 1, 1});
        post_.midtones = v3("Scene_MidTones", {1, 1, 1});
        post_.desat = f("Scene_Desaturation", 0.0f);
        if (!P["bEnableSceneEffect"].asBool(true)) {
            post_.shadows = {0, 0, 0}; post_.highlights = {1, 1, 1}; post_.midtones = {1, 1, 1}; post_.desat = 0;
        }
        const assets::Json& C = L["postprocess"]["clut"];
        ImageData strip;
        // The build writes the strip next to lighting.json; the recorded "file" is relative to the build's cwd, so it
        // only resolved when the exe ran from the worktree root (M25: the player route ran every map ungraded).
        std::string clutFile = C.isObject() ? C["file"].asString() : std::string();
        if (!clutFile.empty()) {
            size_t sl = clutFile.find_last_of("/\\");
            clutFile = dataDir_ + "/" + (sl == std::string::npos ? clutFile : clutFile.substr(sl + 1));
        }
        if (!clutFile.empty() && platform::decodeImage(clutFile, strip) && strip.valid()) {
            int n = C["size"][0].asInt(32);
            std::vector<uint8_t> vol((size_t)n * n * n * 4);
            for (int z = 0; z < n; ++z)
                for (int y = 0; y < n; ++y)
                    for (int x = 0; x < n; ++x)
                        std::memcpy(&vol[(((size_t)z * n + y) * n + x) * 4],
                                    &strip.rgba[((size_t)y * strip.w + (size_t)z * n + x) * 4], 4);
            glGenTextures(1, &clutTex_);
            glBindTexture(GL_TEXTURE_3D, clutTex_);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            TexImage3D(GL_TEXTURE_3D, 0, GL_RGBA, n, n, n, 0, GL_RGBA, GL_UNSIGNED_BYTE, vol.data());   // SRGB=False
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_3D, 0);
            clutSize_ = n;
        }
        LOG_INFO("wfc: post: bloom %d scale %.2f, DOF %d far max %.2f falloff %.0f, CLUT %s", (int)post_.bloom,
                 post_.bloomScale, (int)post_.dof, post_.dofMaxBlur[1], 1.0f / post_.dofPacked[3],
                 clutTex_ ? C["object"].asString().c_str() : "none");
    }
    whiteTex_ = makeTex1x1(255, 255, 255, 255);
    blackTex_ = makeTex1x1(0, 0, 0, 0);
    flatNormalTex_ = makeTex1x1(128, 128, 255, 255);
    glGenTextures(1, &blackCube_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, blackCube_);
    for (int f = 0; f < 6; ++f) {
        uint8_t px[4] = {0, 0, 0, 0};
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    // post program
    GLuint pv = compile(GL_VERTEX_SHADER, kPostVS, "post.vs"), pf = compile(GL_FRAGMENT_SHADER, kPostFS, "post.fs");
    postProg_ = (pv && pf) ? link(pv, pf, "post") : 0;
    GLuint bg = compile(GL_FRAGMENT_SHADER, kBloomGatherFS, "bloom.gather");
    GLuint bb = compile(GL_FRAGMENT_SHADER, kBlurFS, "bloom.blur");
    bloomGatherProg_ = (pv && bg) ? link(pv, bg, "bloom.gather") : 0;
    blurProg_ = (pv && bb) ? link(pv, bb, "bloom.blur") : 0;
    GenVertexArrays(1, &postVao_);
    GenVertexArrays(1, &dynVao_);
    GenBuffers(1, &dynVbo_); GenBuffers(1, &dynIbo_);
    if (!postProg_) return false;

    active_ = true;
    {
        MeshData bsp;
        if (assets::loadGlb(dataDir_ + "/bsp.glb", bsp)) {
            bspMesh_ = upload(bsp);
            bspTris_.clear();                          // glTF (x, y, z) m -> UE (x, z, y) * 100
            bspTris_.reserve(bsp.indices.size() * 3);
            for (uint32_t ix : bsp.indices) {
                const float* p = &bsp.positions[(size_t)ix * 3];
                bspTris_.push_back(p[0] * 100.0f); bspTris_.push_back(p[2] * 100.0f); bspTris_.push_back(p[1] * 100.0f);
            }
        }
        else LOG_WARN("wfc: bsp.glb missing; level BSP stays unlit");
        MeshData dec;
        if (assets::loadGlb(dataDir_ + "/decals.glb", dec)) {
            decalMesh_ = upload(dec);
            if (decalMesh_ >= 0) meshes_[(size_t)decalMesh_].decal = true;
        }
        else if (std::ifstream(dataDir_ + "/decals.glb").good())   // written empty: the map authors no DecalActors (Debris)
            LOG_INFO("wfc: decals.glb has no decals (none authored on this map)");
        else LOG_WARN("wfc: decals.glb missing; static decals not drawn");
    }
    if (const char* cc = std::getenv("WFC_CHARCOLORS")) {   // verification: "pr,pg,pb;sr,sg,sb;er,eg,eb"
        float v[9] = {};
        std::sscanf(cc, "%f,%f,%f;%f,%f,%f;%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]);
        CharacterColors& cc0 = charColorsBy_[0];
        for (int i = 0; i < 3; ++i) { cc0.primary[i] = v[i]; cc0.secondary[i] = v[3 + i]; cc0.energon[i] = v[6 + i]; }
    }
    if (const char* tm = std::getenv("WFC_TESTMESH")) {   // "file.glb|x,y,z|yawRad" (verification)
        std::string spec = tm;
        std::string file = spec.substr(0, spec.find('|'));
        float x = 0, y = 0, z = 0, yaw = 0;
        size_t a = spec.find('|');
        if (a != std::string::npos) std::sscanf(spec.c_str() + a + 1, "%f,%f,%f|%f", &x, &y, &z, &yaw);
        MeshData tmesh;
        if (assets::loadGlb(file, tmesh)) {
            testMesh_ = upload(tmesh);
            testModel_ = core::Mat4::translate(core::Vec3{x, y, z}) * core::Mat4::rotateY(yaw);
        }
    }
    LOG_INFO("wfc: shader path active: %zu materials, %zu lightmapped components, %zu lights, fog %s (%s)",
             mats_.size(), lightmaps_.size(), lights_.size(), fogOn_ ? "on" : "off", dataDir_.c_str());
    // [integration M06] Maps without a shipped runtime index (every map but Streets) get one converted from the AssetTools
    // generic index into the render data (tools/render/build_render_index.py); the shipped index wins when present.
    {
        const std::string shipped = assetRoot() + "/Maps/" + mapName + "/render_index.json";
        const std::string converted = dataDir_ + "/render_index.json";
        loadMapProps(std::ifstream(shipped).good() ? shipped : converted);
    }
    return true;
}

// First-use accounting (WFC_RENDERSTATS): resources created after the first frame are logged with
// their cost, to find mid-game hitches (e.g. the first shot).
struct FirstUseTimer {
    const char* kind; std::string name; int frame;
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    ~FirstUseTimer() {
        static const bool on = std::getenv("WFC_RENDERSTATS") != nullptr;
        if (!on || frame <= 0) return;   // frame 0 = the load; frames 1-2 are the first presented (M75: they were hidden)
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        LOG_INFO("wfc first-use: frame %d %s %s %.2f ms", frame, kind, name.c_str(), ms);
    }
};

// Xenos gamma texture fetch (UE3 SRGB textures use the D3D gamma formats): piecewise-linear degamma
// to 10-bit linear, as reconstructed by xenia from Source Engine X360GammaToLinear + D3D9 code in
// Xbox 360 executables [HIGH: hardware behaviour]. Four segments at 64/96/192 of 255 with slopes
// 1/2/4/8 (in 1/1024 units) plus a truncated correction term, then /1023. Not the sRGB curve (GL
// sRGB textures) and not pow 2.2 (that is the CPU FColor table PowOneOver255Table at 0x82251110).
static float pwlGammaToLinear(float gamma) {
    gamma = std::min(std::max(gamma, 0.0f), 1.0f);
    float scale, offset;
    if (gamma >= 96.0f / 255.0f) {
        if (gamma >= 192.0f / 255.0f) { scale = 8.0f / 1024.0f; offset = -1024.0f; }
        else { scale = 4.0f / 1024.0f; offset = -256.0f; }
    } else {
        if (gamma >= 64.0f / 255.0f) { scale = 2.0f / 1024.0f; offset = -64.0f; }
        else { scale = 1.0f / 1024.0f; offset = 0.0f; }
    }
    float linear = gamma * ((255.0f * 1024.0f) * scale) + offset;
    linear += std::trunc(linear * scale);
    return linear * (1.0f / 1023.0f);
}

// RGBA8 (gamma-encoded RGB) -> RGBA16 linear through the PWL table; alpha is not gamma-converted.
static std::vector<uint16_t> pwlToLinear16(const ImageData& img) {
    static uint16_t lut[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; ++i) lut[i] = (uint16_t)std::lround(pwlGammaToLinear(i / 255.0f) * 65535.0f);
        init = true;
    }
    std::vector<uint16_t> out(img.rgba.size());
    for (size_t i = 0; i < img.rgba.size(); i += 4) {
        out[i] = lut[img.rgba[i]]; out[i + 1] = lut[img.rgba[i + 1]]; out[i + 2] = lut[img.rgba[i + 2]];
        out[i + 3] = (uint16_t)(img.rgba[i + 3] * 257);
    }
    return out;
}

static void uploadTex(GLenum target, const ImageData& img, bool srgb) {
    static const bool srgbCurve = std::getenv("WFC_SRGBCURVE") != nullptr;   // A/B: GL sRGB curve
    if (srgb && !srgbCurve) {
        std::vector<uint16_t> lin = pwlToLinear16(img);
        glTexImage2D(target, 0, GL_RGBA16, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_SHORT, lin.data());
    } else {
        glTexImage2D(target, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     img.rgba.data());
    }
}

GLuint Pipeline::texture(const std::string& file, bool srgb, bool clampU, bool clampV) {
    std::string key = file + (srgb ? "|s" : "|l") + (clampU ? "c" : "w") + (clampV ? "c" : "w");
    auto it = texCache_.find(key);
    if (it != texCache_.end()) return it->second;
    FirstUseTimer fu{"texture", file, frameNo_};
    ImageData img;
    GLuint id = 0;
    if (!file.empty() && platform::decodeImage(file, img) && img.valid()) {
        ++gTexCreates;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        uploadTex(GL_TEXTURE_2D, img, srgb);
        GenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clampU ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clampV ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
    } else if (!file.empty()) {
        LOG_WARN("wfc: texture decode failed: %s", file.c_str());
    }
    texCache_[key] = id;
    if (id) touchQueue_.push_back({id, false});
    yieldLoad();                                       // after each texture decode / upload
    return id;
}

// Cooked Xbox TextureCube faces (decoded by tools/render/xbox_texture.py) in UE3/D3D face order
// +X -X +Y -Y +Z -Z. GL uses the same major-axis face convention; rows are uploaded top-first,
// matching D3D's top-left texel origin, and lookups use the UE world-space direction directly.
GLuint Pipeline::cubeTexture(const std::vector<std::string>& faces, bool srgb) {
    std::string key = "cube|" + faces[0] + (srgb ? "|s" : "|l");
    auto it = texCache_.find(key);
    if (it != texCache_.end()) return it->second;
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    bool ok = true;
    for (int f = 0; f < 6 && ok; ++f) {
        ImageData img;
        ok = platform::decodeImage(faces[(size_t)f], img) && img.valid();
        if (ok) uploadTex(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)f, img, srgb);
    }
    if (!ok) { LOG_WARN("wfc: cubemap decode failed: %s", faces[0].c_str()); glDeleteTextures(1, &id); id = 0; }
    else {
        GenerateMipmap(GL_TEXTURE_CUBE_MAP);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    texCache_[key] = id;
    if (id) touchQueue_.push_back({id, true});
    return id;
}

void Pipeline::loadStep(const char* where) {
    if (loadYield_) { yieldLoad(); return; }
#ifdef WFC_HAS_CORE_LOADYIELD
    core::loadYield(where);
    glx::uniformCacheForgetCurrent();                  // the loading frame may have bound other programs
#else
    (void)where;
#endif
}

void Pipeline::touchNewTextures() {
    // opt-in (WFC_PROGWARM=1): measured on a forced-cold cache (Streets 5 v 5) it adds 3-9 s of load for no in-match
    // gain (worst frames 39 / 44 ms with it, 40 / 42 ms without); kept for other drivers / machines
    static const bool offP = std::getenv("WFC_PROGWARM") == nullptr;
    if (!progTouchQueue_.empty() && (offP || !fbo_)) progTouchQueue_.clear();
    if (!progTouchQueue_.empty()) {
        // the driver finishes a program at its first draw, for the draw's state (render-target format, blend): each
        // new program draws one point into the scene target (same format; cleared at the frame start, never
        // presented as drawn here) with its own blend state
        GLint prevFbo = 0, prevProg = 0, vp[4];
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
        glGetIntegerv(GL_VIEWPORT, vp);
        const GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
        // a cold driver cache compiles here (~7 s for a whole map on the first run after a shader change): during a
        // load it is time-sliced, the loading screen presenting every ~25 ms (the stall watchdog fires at 5 s)
        const std::vector<int> queue = std::move(progTouchQueue_);
        progTouchQueue_.clear();
        auto bindTarget = [&] {
            BindFramebuffer(GL_FRAMEBUFFER, fbo_);
            glViewport(0, 0, 1, 1);
            glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
            BindVertexArray(postVao_);
        };
        bindTarget();
        const bool loading = warmup_ || queue.size() > 16;   // a load's batch (in-match stragglers are not waited on)
        auto slice = std::chrono::steady_clock::now();
        for (int pi : queue) {
            if (pi < 0 || (size_t)pi >= progs_.size() || !progs_[(size_t)pi].id) continue;
            const Program& Pg = progs_[(size_t)pi];
            bindCommon(Pg, core::Mat4::identity());
            switch (Pg.blend) {
                case 2: glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); break;
                case 3: glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); glDepthMask(GL_FALSE); break;
                case 4: glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR, GL_ZERO); glDepthMask(GL_FALSE); break;
                default: glDisable(GL_BLEND); glDepthMask(GL_TRUE); break;
            }
            glDrawArrays(GL_POINTS, 0, 1);
            ++touchedPrograms_;
            if (loading) glFinish();                   // the driver compiles on its own thread: wait here, inside the slice
            if (std::chrono::steady_clock::now() - slice > std::chrono::milliseconds(25)) {
                BindVertexArray(0);
                loadStep("Render: program warm-up");   // presents a loading frame (no-op outside a load)
                bindTarget();
                slice = std::chrono::steady_clock::now();
            }
        }
        BindVertexArray(0);
        glDepthMask(GL_TRUE);
        UseProgram((GLuint)prevProg);
        BindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
        glViewport(vp[0], vp[1], vp[2], vp[3]);
        if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (cull) glEnable(GL_CULL_FACE);
    }
    if (touchQueue_.empty()) return;
    static const bool off = std::getenv("WFC_NOTEXTOUCH") != nullptr;   // A/B
    if (off) { touchQueue_.clear(); return; }
    if (!touchProg2D_) {
        const char* vs = "#version 330 core\nvoid main(){ gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
        const char* f2 = "#version 330 core\nuniform sampler2D uT; out vec4 o; void main(){ o = textureLod(uT, vec2(0.5), 0.0); }\n";
        const char* fc = "#version 330 core\nuniform samplerCube uT; out vec4 o; void main(){ o = textureLod(uT, vec3(1.0, 0.0, 0.0), 0.0); }\n";
        GLuint v = compile(GL_VERTEX_SHADER, vs, "touch.vs");
        GLuint a = compile(GL_FRAGMENT_SHADER, f2, "touch2d.fs"), c = compile(GL_FRAGMENT_SHADER, fc, "touchcube.fs");
        if (v && a) touchProg2D_ = link(v, a, "touch2d");
        v = compile(GL_VERTEX_SHADER, vs, "touch.vs");
        if (v && c) touchProgCube_ = link(v, c, "touchcube");
        GenFramebuffers(1, &touchFbo_); glGenTextures(1, &touchTex_);
        glBindTexture(GL_TEXTURE_2D, touchTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        BindFramebuffer(GL_FRAMEBUFFER, touchFbo_);
        FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, touchTex_, 0);
        if (!touchProg2D_ || !touchProgCube_) { touchQueue_.clear(); BindFramebuffer(GL_FRAMEBUFFER, 0); return; }
    }
    GLint prevFbo = 0, prevProg = 0, vp[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_VIEWPORT, vp);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
    BindFramebuffer(GL_FRAMEBUFFER, touchFbo_);
    glViewport(0, 0, 1, 1);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    BindVertexArray(postVao_);
    ActiveTexture(GL_TEXTURE0);
    for (int pass = 0; pass < 2; ++pass) {
        UseProgram(pass ? touchProgCube_ : touchProg2D_);
        Uniform1i(GetUniformLocation(pass ? touchProgCube_ : touchProg2D_, "uT"), 0);
        for (const auto& t : touchQueue_) {
            if (t.second != (pass == 1)) continue;
            glBindTexture(t.second ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, t.first);
            glDrawArrays(GL_POINTS, 0, 1);
            ++touchedTextures_;
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0); glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    BindVertexArray(0);
    touchQueue_.clear();
    UseProgram((GLuint)prevProg);
    BindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (blend) glEnable(GL_BLEND);
    if (cull) glEnable(GL_CULL_FACE);
}

// ------------------------------------------------------------------------- programs
int Pipeline::buildProgram(const std::string& key, const std::string& body, const std::vector<Program::Slot>& slots,
                           const std::vector<bool>& slotIsCube, int blend, bool twoSided, bool lit, float clip,
                           bool lightmapped, const std::vector<std::string>& rtParams) {
    std::string fs = kFSHead;
    std::string code = body;
    for (const std::string& n : rtParams) fs += "uniform vec4 uRT_" + n + "; uniform int uRTSet_" + n + ";\n";
    for (size_t k = 0; k < slots.size(); ++k) {
        std::string n = std::to_string(k);
        if (slotIsCube[k]) {
            fs += "uniform samplerCube uTex" + n + ";\n";
            // UE world direction -> cube lookup (faces in UE/D3D order, see cubeTexture)
            fs += "vec4 wfcSC_" + n + "(vec3 d) { return texture(uTex" + n + ", d); }\n";
            replaceAll(code, "wfcSampleCube(" + n + ", ", "wfcSC_" + n + "(");
            // WFC LODBias input (e.g. Reflection_LOD_Scale): mip bias in levels, as tex2Dbias/texCUBEbias
            fs += "vec4 wfcSCB_" + n + "(vec3 d, float b) { return texture(uTex" + n + ", d, b); }\n";
            replaceAll(code, "wfcSampleCubeBias(" + n + ", ", "wfcSCB_" + n + "(");
        } else {
            fs += "uniform sampler2D uTex" + n + "; uniform vec4 uUnpackMin" + n + "; uniform vec4 uUnpackScale" + n + ";\n";
            fs += "vec4 wfcS2D_" + n + "(vec2 uv) { return texture(uTex" + n + ", uv) * uUnpackScale" + n +
                  " + uUnpackMin" + n + "; }\n";
            replaceAll(code, "wfcSample2D(" + n + ", ", "wfcS2D_" + n + "(");
        }
    }
    fs += "void wfcMaterial(in MatIn m, out MatOut o) {\n" + code + "\n}\n";
    fs += kFSPrologue;
    fs += mainOverride_ ? mainOverride_ : (lightmapped ? kFSMainLM : kFSMainUber);

    static GLuint vsShared = 0;
    if (!vsShared) vsShared = compile(GL_VERTEX_SHADER, kVS, "world.vs");
    GLuint vsUse = vsShared;
    int instRt = 0;
    if (instBuild_) {   // instanced character variant: per-instance uniforms become globals loaded from the instance row
        static GLuint vsInst = 0;
        if (!vsInst) {
            std::string vs = kVS;
            bool ok = true;
            auto sub = [&](const std::string& a, const std::string& b) { if (vs.find(a) == std::string::npos) ok = false; else replaceAll(vs, a, b); };
            sub("uniform mat4 uModel;", "mat4 uModel;");
            sub("uniform int uSkin;", "int uSkin;");
            sub("uniform int uSkinRow, uSkinBones;", "int uSkinRow, uSkinBones;");
            sub("uniform float uSkinAlpha;", "float uSkinAlpha;");
            sub("void main() {", "void wfcVSBody() {");
            vs += "uniform sampler2D uInstTex;\nuniform int uInstBase;\nflat out int vInstRow;\n"
                  "void main() {\n    vInstRow = uInstBase + gl_InstanceID;\n"
                  "    uModel = mat4(texelFetch(uInstTex, ivec2(0, vInstRow), 0), texelFetch(uInstTex, ivec2(1, vInstRow), 0),\n"
                  "                  texelFetch(uInstTex, ivec2(2, vInstRow), 0), texelFetch(uInstTex, ivec2(3, vInstRow), 0));\n"
                  "    vec4 sk = texelFetch(uInstTex, ivec2(4, vInstRow), 0);\n"
                  "    uSkin = int(sk.x + 0.5); uSkinRow = int(sk.y + 0.5); uSkinAlpha = sk.z; uSkinBones = int(sk.w + 0.5);\n"
                  "    wfcVSBody();\n}\n";
            vsInst = ok ? compile(GL_VERTEX_SHADER, vs, "world_inst.vs") : 0;
            if (!vsInst) LOG_WARN("wfc: instanced vertex shader unavailable (instancing off)");
        }
        if (!vsInst) return -1;
        vsUse = vsInst;
        bool ok = true;
        auto sub = [&](const std::string& a, const std::string& b) { if (fs.find(a) == std::string::npos) ok = false; else replaceAll(fs, a, b); };
        sub("uniform vec3 uAmb[6];", "vec3 uAmb[6];");
        sub("uniform int uNumLights;", "int uNumLights;");
        sub("uniform vec4 uLPos[3];", "vec4 uLPos[3];");
        sub("uniform vec4 uLDir[3];", "vec4 uLDir[3];");
        sub("uniform vec4 uLCol[3];", "vec4 uLCol[3];");
        sub("uniform vec4 uLSpot[3];", "vec4 uLSpot[3];");
        sub("uniform vec3 uDLAC;", "vec3 uDLAC;");
        sub("uniform vec4 uDynParam;", "vec4 uDynParam;");
        std::vector<std::string> rts = rtParams;
        std::sort(rts.begin(), rts.end());
        for (const std::string& n : rts) sub("uniform vec4 uRT_" + n + "; uniform int uRTSet_" + n + ";", "vec4 uRT_" + n + "; int uRTSet_" + n + ";");
        if (fs.find("void main()") == std::string::npos || 25 + 2 * (int)rts.size() > kInstW) ok = false;
        if (!ok) return -1;
        replaceAll(fs, "void main()", "void wfcMainBody()");
        std::string ld = "flat in int vInstRow;\nuniform sampler2D uInstTex;\n"
                         "vec4 wfcI(int k) { return texelFetch(uInstTex, ivec2(k, vInstRow), 0); }\n"
                         "void main() {\n    for (int i = 0; i < 6; ++i) uAmb[i] = wfcI(5 + i).xyz;\n"
                         "    vec4 t = wfcI(11); uNumLights = int(t.x + 0.5); uDLAC = t.yzw;\n"
                         "    for (int i = 0; i < 3; ++i) { uLPos[i] = wfcI(12 + i); uLDir[i] = wfcI(15 + i); uLCol[i] = wfcI(18 + i); uLSpot[i] = wfcI(21 + i); }\n"
                         "    uDynParam = wfcI(24);\n";
        for (size_t i = 0; i < rts.size(); ++i)
            ld += "    uRT_" + rts[i] + " = wfcI(" + std::to_string(25 + 2 * i) + "); uRTSet_" + rts[i] + " = int(wfcI(" +
                  std::to_string(26 + 2 * i) + ").x + 0.5);\n";
        ld += "    wfcMainBody();\n}\n";
        fs += ld;
        instRt = (int)rts.size();
    }
    if (mdiBuild_) {   // world MDI variant: per-draw constants from the row texture (row = aDrawRow)
        static GLuint vsMdi = 0;
        if (!vsMdi) {
            std::string vs = kVS;
            bool ok = vs.find("uniform vec4 uLMCoord;") != std::string::npos && vs.find("void main() {") != std::string::npos;
            replaceAll(vs, "uniform vec4 uLMCoord;", "vec4 uLMCoord;");
            replaceAll(vs, "void main() {", "void wfcVSBody() {");
            vs += "layout(location=11) in float aDrawRow;\nuniform sampler2D uRowTex;\nflat out int vRow;\n"
                  "void main() {\n    vRow = int(aDrawRow + 0.5);\n    uLMCoord = texelFetch(uRowTex, ivec2(0, vRow), 0);\n"
                  "    wfcVSBody();\n}\n";
            vsMdi = ok ? compile(GL_VERTEX_SHADER, vs, "world_mdi.vs") : 0;
            if (!vsMdi) LOG_WARN("wfc: MDI vertex shader unavailable (world MDI off)");
        }
        if (!vsMdi || fs.find("void main()") == std::string::npos) return -1;
        vsUse = vsMdi;
        const bool hasLMS = fs.find("uniform vec3 uLMScale[3];") != std::string::npos;
        const bool hasEnv = fs.find("uniform vec3 uAmb[6];") != std::string::npos;
        if (hasLMS) replaceAll(fs, "uniform vec3 uLMScale[3];", "vec3 uLMScale[3];");
        if (hasEnv) {
            replaceAll(fs, "uniform vec3 uAmb[6];", "vec3 uAmb[6];");
            replaceAll(fs, "uniform int uNumLights;", "int uNumLights;");
            replaceAll(fs, "uniform vec4 uLPos[3];", "vec4 uLPos[3];");
            replaceAll(fs, "uniform vec4 uLDir[3];", "vec4 uLDir[3];");
            replaceAll(fs, "uniform vec4 uLCol[3];", "vec4 uLCol[3];");
            replaceAll(fs, "uniform vec4 uLSpot[3];", "vec4 uLSpot[3];");
        }
        const bool hasDLAC = fs.find("uniform vec3 uDLAC;") != std::string::npos;
        if (hasDLAC) replaceAll(fs, "uniform vec3 uDLAC;", "vec3 uDLAC;");
        // lightmap pages from the shared array (layer per sub in row texels 1..3 .w) when the bucket says so
        const char* kLMDecl = "uniform sampler2D uLM0; uniform sampler2D uLM1; uniform sampler2D uLM2;";
        const bool hasLMArr = hasLMS && fs.find(kLMDecl) != std::string::npos;
        if (hasLMArr) {
            replaceAll(fs, kLMDecl, std::string(kLMDecl) +
                       "\nuniform sampler2DArray uLMArr; uniform int uLMUseArr; float wfcLMLayer[3];\n"
                       "vec4 wfcLMTex(int i, sampler2D s, vec2 uv) { return uLMUseArr != 0 ? texture(uLMArr, vec3(uv, wfcLMLayer[i])) : texture(s, uv); }");
            replaceAll(fs, "texture(uLM0, vUV1)", "wfcLMTex(0, uLM0, vUV1)");
            replaceAll(fs, "texture(uLM1, vUV1)", "wfcLMTex(1, uLM1, vUV1)");
            replaceAll(fs, "texture(uLM2, vUV1)", "wfcLMTex(2, uLM2, vUV1)");
        }
        replaceAll(fs, "void main()", "void wfcMainBody()");
        std::string ld = "flat in int vRow;\nuniform sampler2D uRowTex;\n"
                         "vec4 wfcR(int k) { return texelFetch(uRowTex, ivec2(k, vRow), 0); }\nvoid main() {\n";
        if (hasLMS) ld += "    for (int i = 0; i < 3; ++i) uLMScale[i] = wfcR(1 + i).xyz;\n";
        if (hasLMArr) ld += "    for (int i = 0; i < 3; ++i) wfcLMLayer[i] = wfcR(1 + i).w;\n";
        if (hasEnv) ld += "    for (int i = 0; i < 6; ++i) uAmb[i] = wfcR(4 + i).xyz;\n    uNumLights = int(wfcR(10).x + 0.5);\n"
                          "    for (int i = 0; i < 3; ++i) { uLPos[i] = wfcR(11 + i); uLDir[i] = wfcR(14 + i); uLCol[i] = wfcR(17 + i); uLSpot[i] = wfcR(20 + i); }\n";
        if (hasDLAC) ld += "    uDLAC = wfcR(10).yzw;\n";
        ld += "    wfcMainBody();\n}\n";
        fs += ld;
    }
    GLuint id = 0;
    auto cached = gProgCache.find(fs);
    if (cached != gProgCache.end()) {
        id = cached->second.id;
        cached->second.lastUse = ++gProgUse;
        ++progCacheHits_;
    } else {
        const auto tc0 = std::chrono::steady_clock::now();
        GLuint f = compile(GL_FRAGMENT_SHADER, fs, key);
        if (!vsUse || !f) return -1;
        id = link(vsUse, f, key);
        DeleteShader(f);
        if (!id) return -1;
        if (id < commonKeyById_.size()) commonKeyById_[id].valid = false;   // a new (or recycled) program object
        static const bool stats = std::getenv("WFC_RENDERSTATS") != nullptr;
        if (stats && frameNo_ > 0)            // diagnostics: a program compiled after the load (a hitch on that frame)
            LOG_INFO("wfc program built: frame %d %s %.1f ms", frameNo_, key.c_str(),
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tc0).count());
        auto ins = gProgCache.emplace(fs, CachedProg{id, ++gProgUse}).first;
        gProgCacheById[id] = &ins->first;
    }
    Program P;
    P.id = id;
    auto U = [&](const char* n) { return GetUniformLocation(id, n); };
    P.uViewProj = U("uViewProj"); P.uModel = U("uModel"); P.uCamPos = U("uCamPos"); P.uTime = U("uTime");
    P.uTwoSided = U("uTwoSided"); P.uClip = U("uClip"); P.uMasked = U("uMasked"); P.uLit = U("uLit");
    P.uLMCoord = U("uLMCoord"); P.uLMScale = U("uLMScale"); P.uAmb = U("uAmb"); P.uNumLights = U("uNumLights");
    P.uLPos = U("uLPos"); P.uLDir = U("uLDir"); P.uLCol = U("uLCol"); P.uLSpot = U("uLSpot");
    P.uFogOn = U("uFogOn"); P.uFogMaxH = U("uFogMaxH"); P.uFogScale = U("uFogScale"); P.uFogStart = U("uFogStart");
    P.uFogExt = U("uFogExt"); P.uFogIn = U("uFogIn");
    static const char* kRT[3] = {"Cust_Color_A", "Cust_COLOR_B", "EnergonColor"};
    for (int i = 0; i < 3; ++i) {
        P.uRT[i] = U((std::string("uRT_") + kRT[i]).c_str());
        (void)0;
        P.uRTSet[i] = U((std::string("uRTSet_") + kRT[i]).c_str());
    }
    for (const std::string& n : rtParams)
        P.rtLoc[n] = {U(("uRT_" + n).c_str()), U(("uRTSet_" + n).c_str())};
    P.slots = slots;
    P.blend = blend; P.twoSided = twoSided; P.lit = lit; P.clip = clip;
    P.sceneDepth = code.find("m.sceneDepth") != std::string::npos || code.find("wfcDepthBiasedAlpha(") != std::string::npos;
    P.sceneColor = code.find("wfcSceneColor(") != std::string::npos;
    if (slots.size() > 12) LOG_WARN("wfc: %s uses %zu texture slots (unit 12 is scene depth)", key.c_str(), slots.size());
    UseProgram(id);
    for (size_t k = 0; k < slots.size(); ++k) {
        std::string n = std::to_string(k);
        Uniform1i(U(("uTex" + n).c_str()), (GLint)k);
        if (!slotIsCube[k]) {
            Uniform4f(U(("uUnpackMin" + n).c_str()), slots[k].umin[0], slots[k].umin[1], slots[k].umin[2], slots[k].umin[3]);
            Uniform4f(U(("uUnpackScale" + n).c_str()), slots[k].uscale[0], slots[k].uscale[1], slots[k].uscale[2],
                      slots[k].uscale[3]);
        }
        P.slots[k].unit = (int)k;
    }
    Uniform1i(U("uLM0"), 13); Uniform1i(U("uLM1"), 14); Uniform1i(U("uLM2"), 15);
    Uniform1i(U("uLMArr"), 21);
    Uniform1i(U("uSceneDepth"), 12);
    Uniform1i(U("uSceneColor"), 16);
    Uniform1i(U("uVLM"), 11);
    Uniform1i(U("uShadowMask"), 10);
    if (instBuild_) { Uniform1i(U("uInstTex"), 19); Uniform1i(U("uBoneTex"), 18); }
    if (mdiBuild_) Uniform1i(U("uRowTex"), 20);
    UseProgram(0);
    P.instRtCount = instRt;
    progs_.push_back(P);
    progIndex_[key] = (int)progs_.size() - 1;
    progTouchQueue_.push_back((int)progs_.size() - 1);
    return (int)progs_.size() - 1;
}

// Raw umodel glTF meshes (e.g. CP_OptimusArm_SKEL, RB_OptimusWeaponArm_SKEL) carry the UE3 material
// object NAME, not its path: resolve it against the compiled original materials.
std::string Pipeline::resolveBySourceName(const Material* m) const {
    if (!m || m->sourceName.empty()) return std::string();
    auto hit = srcNameCache_.find(m->sourceName);
    if (hit != srcNameCache_.end()) return hit->second;
    std::string res = resolveBySourceNameUncached(m);
    srcNameCache_.emplace(m->sourceName, res);
    return res;
}

std::string Pipeline::resolveBySourceNameUncached(const Material* m) const {
    std::string want = "." + m->sourceName;
    std::transform(want.begin(), want.end(), want.begin(), ::tolower);
    for (const auto& kv : mats_) {     // first in path order; same-named MICs share their master
        std::string k = kv.first;
        std::transform(k.begin(), k.end(), k.begin(), ::tolower);
        if (k.size() >= want.size() && k.compare(k.size() - want.size(), want.size(), want) == 0) return kv.first;
    }
    return std::string();
}

static std::string materialKey(const Material* m) {
    if (!m) return "<none>";
    char buf[96];
    std::snprintf(buf, sizeof buf, "|%d|%d|%.4f,%.4f,%.4f", (int)m->tex, (int)m->emissiveTexHandle, m->color.x, m->color.y,
                  m->color.z);
    return m->wfcName + "|" + m->sourceName + "|" + m->baseColorUri + "|" + m->emissiveUri + "|" + m->normalUri + "|" +
           m->specularUri + buf;
}

int Pipeline::programFor(const std::string& matNameIn, const Material* gm, bool lightmapped) {
    std::string matName = matNameIn;
    if (matName.empty()) matName = resolveBySourceName(gm);
    std::string key = (matName.empty() ? std::string("<gltf>") : matName) + (lightmapped ? "|LM" : "|UBER");
    static const bool gltfOnly = std::getenv("WFC_GLTFMATERIALS") != nullptr;   // A/B: AssetTools bakes
    auto mit = (gltfOnly && !lightmapped) ? mats_.end() : mats_.find(matName);
    if (mit == mats_.end() && gm) {
        // No compiled original graph: build from the glTF material (character/weapon textures
        // baked by AssetTools from the original customization shader).
        key += "|" + materialKey(gm);
    }
    auto pit = progIndex_.find(key);
    if (pit != progIndex_.end()) return pit->second;
    FirstUseTimer fu{"program", key, frameNo_};

    if (mit != mats_.end()) {
        const MatSrc& s = mit->second;
        std::vector<Program::Slot> slots;
        for (size_t k = 0; k < s.files.size(); ++k) {
            Program::Slot sl{};
            sl.cube = s.cube[k];
            if (sl.cube) {
                GLuint c = s.faces[k].size() == 6 ? cubeTexture(s.faces[k], s.srgb[k]) : 0;
                sl.tex = c ? c : blackCube_;
            }
            else {
                GLuint t = texture(s.files[k], s.srgb[k], s.clampU[k], s.clampV[k]);
                sl.tex = t ? t : blackTex_;
            }
            for (int c = 0; c < 4; ++c) { sl.umin[c] = s.umin[k][(size_t)c]; sl.uscale[c] = s.umax[k][(size_t)c] - s.umin[k][(size_t)c]; }
            slots.push_back(sl);
        }
        int r = buildProgram(key, s.glsl, slots, s.cube, s.blend, s.twoSided, s.lit, s.clip, lightmapped, s.rtParams);
        if (r >= 0) {
            progs_[(size_t)r].original = true;
            {
                std::string ml = matNameIn;
                std::transform(ml.begin(), ml.end(), ml.begin(), ::tolower);
                progs_[(size_t)r].material = ml;
            }
            if (!lightmapped && s.glsl.find("o.Distortion = vec3(0.0);") == std::string::npos &&
                s.glsl.find("o.Distortion =") != std::string::npos) {
                mainOverride_ = kFSMainDistort;
                int d = buildProgram(key + "|DIST", s.glsl, slots, s.cube, 3, s.twoSided, false, s.clip, false, s.rtParams);
                mainOverride_ = nullptr;
                if (d >= 0) { progs_[(size_t)d].original = true; progs_[(size_t)r].distProg = d; }
            }
            if (!lightmapped && matName.find("ScreenEffect_M") != std::string::npos) {   // HUD chain material
                mainOverride_ = kFSMainScreenEffect;
                int sf = buildProgram(key + "|SCREENFX", s.glsl, slots, s.cube, 0, true, false, s.clip, false, s.rtParams);
                mainOverride_ = nullptr;
                if (sf >= 0) { progs_[(size_t)sf].original = true; progs_[(size_t)r].screenProg = sf; }
            }
            // opt-in: measured at 32 v 32 (Streets, fixed cam) pixel-identical but no frame-time gain - only same-chassis
            // bodies can share a draw (~1.4 instances per draw) and the flush state save / restore eats the saving
            static const bool mdiOn = [] { const char* e = std::getenv("WFC_MDI"); return !(e && e[0] == '0') && std::getenv("WFC_GL33") == nullptr; }();   // default on; WFC_MDI=0 off
            if (mdiWanted_ && mdiOn && s.blend <= 1 && progs_[(size_t)r].distProg < 0 && !progs_[(size_t)r].sceneDepth &&
                !progs_[(size_t)r].sceneColor && MultiDrawElementsIndirect && VertexAttribDivisor) {
                mdiBuild_ = true;
                int md = buildProgram(key + "|MDI", s.glsl, slots, s.cube, s.blend, s.twoSided, s.lit, s.clip, lightmapped, s.rtParams);
                mdiBuild_ = false;
                if (md >= 0) { progs_[(size_t)md].original = true; progs_[(size_t)md].material = progs_[(size_t)r].material;
                               progs_[(size_t)r].mdiProg = md; }
            }
            static const bool noInst = std::getenv("WFC_INSTANCING") == nullptr || std::getenv("WFC_NOINSTANCING") != nullptr;
            if (instWanted_ && !noInst && !lightmapped && s.blend <= 1 && progs_[(size_t)r].distProg < 0 &&
                !progs_[(size_t)r].sceneDepth && !progs_[(size_t)r].sceneColor && DrawElementsInstanced) {
                instBuild_ = true;
                int in = buildProgram(key + "|INST", s.glsl, slots, s.cube, s.blend, s.twoSided, s.lit, s.clip, false, s.rtParams);
                instBuild_ = false;
                if (in >= 0) { progs_[(size_t)in].original = true; progs_[(size_t)in].material = progs_[(size_t)r].material;
                               progs_[(size_t)r].instProg = in; }
            }
            if (!lightmapped && s.blend <= 1) {          // caster variant for projected shadows
                mainOverride_ = kFSMainShadow;
                int sh = buildProgram(key + "|SHADOW", s.glsl, slots, s.cube, s.blend, s.twoSided, false, s.clip, false, s.rtParams);
                mainOverride_ = nullptr;
                if (sh >= 0) { progs_[(size_t)sh].original = true; progs_[(size_t)r].shadowProg = sh; }
            }
            return r;
        }
        LOG_WARN("wfc: material %s failed to build; using glTF fallback", matName.c_str());
    } else if (!matName.empty()) {
        // the original material did not compile offline (build_materials error) - drawn with the glTF fallback;
        // say so once (Experimental: Debris' grey wreck sections were silent)
        auto er = matErrors_.find(matName);
        if (er != matErrors_.end() && warnedMatErrors_.insert(matName).second)
            LOG_WARN("wfc: material %s has no compiled original (%s); using glTF fallback", matName.c_str(),
                     er->second.substr(0, 160).c_str());
    }
    // glTF fallback material
    std::vector<Program::Slot> slots;
    std::vector<bool> cube;
    std::string body;
    auto addSlot = [&](GLuint t, float mn, float sc) {
        Program::Slot sl{}; sl.tex = t; sl.cube = false;
        for (int c = 0; c < 4; ++c) { sl.umin[c] = mn; sl.uscale[c] = sc; }
        sl.umin[3] = 0.0f; sl.uscale[3] = 1.0f;
        slots.push_back(sl); cube.push_back(false);
        return (int)slots.size() - 1;
    };
    char line[256];
    core::Vec3 tint = gm ? gm->color : core::Vec3{0.5f, 0.5f, 0.5f};
    GLuint bc = gm && !gm->baseColorUri.empty() ? texture(gm->baseColorUri, true, false, false) : 0;
    GLuint nm = gm && !gm->normalUri.empty() ? texture(gm->normalUri, false, false, false) : 0;
    GLuint sp = gm && !gm->specularUri.empty() ? texture(gm->specularUri, true, false, false) : 0;
    GLuint em = gm && !gm->emissiveUri.empty() ? texture(gm->emissiveUri, true, false, false) : 0;
    if (nm) {
        int s = addSlot(nm, -1.0f, 2.0f);
        std::snprintf(line, sizeof line, "    o.Normal = wfcSample2D(%d, m.uv0).xyz;\n", s); body += line;
    } else body += "    o.Normal = vec3(0.0, 0.0, 1.0);\n";
    body += "    wfcSetNormal(m, o.Normal);\n";
    if (bc) {
        int s = addSlot(bc, 0.0f, 1.0f);
        std::snprintf(line, sizeof line, "    o.DiffuseColor = wfcSample2D(%d, m.uv0).rgb * vec3(%f, %f, %f);\n", s, tint.x, tint.y, tint.z);
        body += line;
    } else {
        std::snprintf(line, sizeof line, "    o.DiffuseColor = vec3(%f, %f, %f);\n", tint.x, tint.y, tint.z); body += line;
    }
    if (sp) {
        int s = addSlot(sp, 0.0f, 1.0f);
        std::snprintf(line, sizeof line, "    o.SpecularColor = wfcSample2D(%d, m.uv0).rgb;\n", s); body += line;
    } else body += "    o.SpecularColor = vec3(0.0);\n";
    body += "    o.SpecularPower = 15.0;\n";   // [PROV] CHR SpecularPower not yet recovered
    if (em) {
        int s = addSlot(em, 0.0f, 1.0f);
        std::snprintf(line, sizeof line, "    o.EmissiveColor = wfcSample2D(%d, m.uv0).rgb;\n", s); body += line;
    } else body += "    o.EmissiveColor = vec3(0.0);\n";
    body += "    o.Opacity = 1.0; o.OpacityMask = 1.0; o.CustomLighting = vec3(0.0);\n";
    return buildProgram(key, body, slots, cube, 0, false, true, 0.3333f, lightmapped);
}

// ------------------------------------------------------------------------- light environment
namespace {
struct RenderStats { int envCalls = 0, visCalls = 0, draws = 0; double envMs = 0, renderMs = 0, gpuMs = 0, dynBuildMs = 0, dynUploadMs = 0, dynTotalMs = 0, dynShadowMs = 0; int dynCalls = 0, dynCulled = 0, dynReused = 0;
                     double skinMs = 0, skinBoundsMs = 0; int skinCalls = 0, skinUploads = 0;
                     double dynEnvMs = 0, dynSubsMs = 0, dynDrawMs = 0;
                     int instQueued = 0, instDraws = 0, instRejected = 0; } gStats;
std::chrono::steady_clock::time_point gFrameStart;
}

void Pipeline::computeEnv(const core::Vec3& p, bool dynamicObject, LightEnv& env) const {
    auto tEnv = std::chrono::steady_clock::now();
    struct EnvTimer { std::chrono::steady_clock::time_point t; ~EnvTimer() {
        gStats.envMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); } } envTimer{tEnv};
    ++gStats.envCalls;
    for (auto& c : env.cube) c = {0, 0, 0};
    // [CONF] TnRobotForm/TnVehicleForm LightEnvironmentComponent TotalLightCount = 2 (TransGame.xxx):
    // the brightest 2 lights stay direct, everything else is folded into the ambient cube.
    const int maxDirect = dynamicObject ? 2 : 3;
    struct Cand { const Light* l; core::Vec3 L; core::Vec3 I; float lum; float att; };
    std::vector<Cand> cands;
    for (const Light& l : lights_) {
        if (!l.enabled) continue;
        if (dynamicObject ? !l.chDynamic : !l.chStatic) continue;
        core::Vec3 L; float att = 1.0f;
        if (l.type == 3) {
            // UE3 SkyLight: hemisphere irradiance -> ambient cube (+Y upper, -Y lower, sides half each).
            env.cube[2] = env.cube[2] + l.color;
            env.cube[3] = env.cube[3] + l.lowerColor;
            core::Vec3 side = (l.color + l.lowerColor) * 0.5f;
            for (int f : {0, 1, 4, 5}) env.cube[f] = env.cube[f] + side;
            continue;
        }
        if (l.type == 2) {
            L = l.dir * -1.0f;
        } else {
            core::Vec3 d = l.pos - p;
            float dist = core::length(d);
            if (dist >= l.radius || dist < 1e-4f) continue;
            L = d * (1.0f / dist);
            float r = dist / l.radius;
            att = std::pow(std::max(1.0f - r * r, 0.0f), l.falloff);
            if (l.type == 1) {
                float s = core::clampf((core::dot(L, l.dir * -1.0f) - l.cosOuter) * l.invConeRange, 0.0f, 1.0f);
                att *= s * s;
            }
        }
        if (att <= 1e-4f) continue;
        core::Vec3 I = l.color * att;
        float lum = 0.3f * I.x + 0.59f * I.y + 0.11f * I.z;
        cands.push_back({&l, L, I, lum, att});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.lum > b.lum; });
    env.n = 0;
    const core::Vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const Cand& c : cands) {
        // Visibility trace (UE3 light environments trace each light to the primitive).
        float vis = 1.0f;
        if (false) {                          // (superseded: characters use WfcDirectLightEnv.cpp)
            // TnRobotForm / TnVehicleForm LightEnvironmentComponent NormalizedSampleOffsets [CONF data]:
            // visibility = fraction of samples (bounds origin + offset * extent) with a clear path to
            // the light [HIGH: UE3 light-environment sampling].
            int clear = 0;
            for (const core::Vec3& o : *envSamples_) {
                core::Vec3 from = envBoundsCenter_ + core::Vec3{o.x * envBoundsExtent_.x, o.y * envBoundsExtent_.y,
                                                                o.z * envBoundsExtent_.z};
                core::Vec3 to = c.l->type == 2 ? from + c.L * 300.0f : c.l->pos;
                auto q = [](float v) { return (uint64_t)(uint32_t)(int32_t)std::floor(v * 4.0f) & 0xFFFFull; };
                uint64_t key = ((uint64_t)(c.l - lights_.data()) << 48) | (q(from.x) << 32) | (q(from.y) << 16) | q(from.z);
                auto hit = visMemo_.find(key);
                bool occluded;
                if (hit != visMemo_.end()) occluded = hit->second;
                else { ++gStats.visCalls; occluded = vis_(from, to); visMemo_.emplace(key, occluded); }
                if (!occluded) ++clear;
            }
            vis = (float)clear / (float)envSamples_->size();
        } else if (vis_ && c.l->castShadows) {
            core::Vec3 from = p + core::Vec3{0, 0.05f, 0};
            core::Vec3 to = c.l->type == 2 ? from + c.L * 300.0f : c.l->pos;
            auto q = [](float v) { return (uint64_t)(uint32_t)(int32_t)std::floor(v * 4.0f) & 0xFFFFull; };
            uint64_t key = ((uint64_t)(c.l - lights_.data()) << 48) | (q(from.x) << 32) | (q(from.y) << 16) | q(from.z);
            auto hit = visMemo_.find(key);
            bool occluded;
            if (hit != visMemo_.end()) occluded = hit->second;
            else { ++gStats.visCalls; occluded = vis_(from, to); visMemo_.emplace(key, occluded); }
            if (occluded) vis = 0.0f;
        }
        if (vis <= 0.0f) continue;
        if (env.n < maxDirect) {
            int i = env.n++;
            const Light& l = *c.l;
            env.light[i] = (int)(c.l - lights_.data());
            env.pos[i][0] = l.pos.x; env.pos[i][1] = l.pos.y; env.pos[i][2] = l.pos.z;
            env.pos[i][3] = l.type == 2 ? 0.0f : 1.0f / l.radius;
            core::Vec3 d = l.type == 2 ? c.L : l.dir;
            env.dir[i][0] = d.x; env.dir[i][1] = d.y; env.dir[i][2] = d.z; env.dir[i][3] = l.type == 1 ? 1.0f : 0.0f;
            env.col[i][0] = l.color.x; env.col[i][1] = l.color.y; env.col[i][2] = l.color.z; env.col[i][3] = l.falloff;
            env.spot[i][0] = l.cosOuter; env.spot[i][1] = l.invConeRange; env.spot[i][2] = 0; env.spot[i][3] = vis;
            if (l.type != 2) {
                // point/spot evaluated per pixel: per-light attenuation is applied in the shader
            }
        } else {
            for (int f = 0; f < 6; ++f) {
                float w = std::max(core::dot(c.L, axes[f]), 0.0f);
                env.cube[f] = env.cube[f] + c.I * (w * vis);
            }
        }
    }
}

// ------------------------------------------------------------------------- meshes
void Pipeline::buildVertices(const MeshData& m, std::vector<float>& v, bool rawNormals) {
    size_t n = m.vertexCount();
    // scratch reused across calls (bot counts: per-draw allocations of the tangent frames were a measurable part of
    // the character vertex build); same values as before
    static thread_local std::vector<core::Vec3> tan, bit;
    const bool given = m.tangents.size() == n * 4;   // caller-supplied (skinned) tangent frames
    if (!given) { tan.assign(n, {0, 0, 0}); bit.assign(n, {0, 0, 0}); }
    bool uv = m.hasUV();
    if (uv && !given) {
        for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
            uint32_t i0 = m.indices[t], i1 = m.indices[t + 1], i2 = m.indices[t + 2];
            if (i0 >= n || i1 >= n || i2 >= n) continue;
            core::Vec3 p0{m.positions[i0 * 3], m.positions[i0 * 3 + 1], m.positions[i0 * 3 + 2]};
            core::Vec3 p1{m.positions[i1 * 3], m.positions[i1 * 3 + 1], m.positions[i1 * 3 + 2]};
            core::Vec3 p2{m.positions[i2 * 3], m.positions[i2 * 3 + 1], m.positions[i2 * 3 + 2]};
            float du1 = m.uv[i1 * 2] - m.uv[i0 * 2], dv1 = m.uv[i1 * 2 + 1] - m.uv[i0 * 2 + 1];
            float du2 = m.uv[i2 * 2] - m.uv[i0 * 2], dv2 = m.uv[i2 * 2 + 1] - m.uv[i0 * 2 + 1];
            float det = du1 * dv2 - du2 * dv1;
            if (std::fabs(det) < 1e-12f) continue;
            float r = 1.0f / det;
            core::Vec3 e1 = p1 - p0, e2 = p2 - p0;
            core::Vec3 T = (e1 * dv2 - e2 * dv1) * r;
            core::Vec3 B = (e2 * du1 - e1 * du2) * r;
            for (uint32_t i : {i0, i1, i2}) { tan[i] = tan[i] + T; bit[i] = bit[i] + B; }
        }
    }
    bool hasN = m.normals.size() == m.positions.size();
    bool hasUV1 = m.hasUV1();
    v.resize(n * 14);
    for (size_t i = 0; i < n; ++i) {
        float* o = &v[i * 14];
        o[0] = m.positions[i * 3]; o[1] = m.positions[i * 3 + 1]; o[2] = m.positions[i * 3 + 2];
        core::Vec3 N = hasN ? core::Vec3{m.normals[i * 3], m.normals[i * 3 + 1], m.normals[i * 3 + 2]} : core::Vec3{0, 1, 0};
        const core::Vec3 Nraw = N;
        N = core::normalize(N);
        if (rawNormals) { o[3] = Nraw.x; o[4] = Nraw.y; o[5] = Nraw.z; }   // blended in the VS, then normalized
        else { o[3] = N.x; o[4] = N.y; o[5] = N.z; }
        if (given) {
            o[6] = m.tangents[i * 4]; o[7] = m.tangents[i * 4 + 1]; o[8] = m.tangents[i * 4 + 2]; o[9] = m.tangents[i * 4 + 3];
            o[10] = uv ? m.uv[i * 2] : 0.0f; o[11] = uv ? m.uv[i * 2 + 1] : 0.0f;
            o[12] = hasUV1 ? m.uv1[i * 2] : o[10]; o[13] = hasUV1 ? m.uv1[i * 2 + 1] : o[11];
            continue;
        }
        core::Vec3 T = tan[i] - N * core::dot(N, tan[i]);
        float w = 1.0f;
        if (core::dot(T, T) < 1e-20f) {
            T = std::fabs(N.x) < 0.9f ? core::cross(N, core::Vec3{1, 0, 0}) : core::cross(N, core::Vec3{0, 1, 0});
        } else if (core::dot(core::cross(N, T), bit[i]) < 0.0f) {
            w = -1.0f;
        }
        T = core::normalize(T);
        o[6] = T.x; o[7] = T.y; o[8] = T.z; o[9] = w;
        o[10] = uv ? m.uv[i * 2] : 0.0f; o[11] = uv ? m.uv[i * 2 + 1] : 0.0f;
        // UE3 FLocalVertexFactory binds the last available texcoord channel for missing ones, so
        // TexCoord[1] on a single-UV mesh (e.g. Light_Cylinder_STAT) reads UV0.
        o[12] = hasUV1 ? m.uv1[i * 2] : o[10]; o[13] = hasUV1 ? m.uv1[i * 2 + 1] : o[11];
    }
}

static void setupAttribs() {
    const GLsizei st = 14 * sizeof(float);
    EnableVertexAttribArray(0); VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)0);
    EnableVertexAttribArray(1); VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, st, (void*)(3 * sizeof(float)));
    EnableVertexAttribArray(2); VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, st, (void*)(6 * sizeof(float)));
    EnableVertexAttribArray(3); VertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, st, (void*)(10 * sizeof(float)));
    EnableVertexAttribArray(4); VertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, st, (void*)(12 * sizeof(float)));
}

// M43 (AMD stability): a draw whose index range leaves the index buffer, or whose indices reach past the vertex buffer,
// makes the GPU fetch out of bounds; an AMD driver may page-fault and reset on it. Such sub-meshes are never submitted.
static bool subInBounds(const MeshData& m, uint32_t off, uint32_t cnt, const char* where) {
    const size_t nv = m.vertexCount();
    bool ok = (size_t)off + cnt <= m.indices.size();
    if (ok)
        for (uint32_t k = 0; k < cnt; ++k)
            if (m.indices[off + k] >= nv) { ok = false; break; }
    if (!ok) {
        static int logged = 0;
        if (logged++ < 20)
            LOG_ERROR("wfc: %s: sub-mesh [%u, +%u) out of bounds (%zu indices, %zu vertices); not drawn", where, off, cnt,
                      m.indices.size(), nv);
    }
    return ok;
}

int Pipeline::upload(const MeshData& m) {
    if (!active_ || m.empty()) return -1;
    FirstUseTimer fu{"mesh", std::to_string(m.vertexCount()) + " verts", frameNo_};
    GpuMesh g;
    std::vector<float> v;
    buildVertices(m, v);
    GenVertexArrays(1, &g.vao);
    BindVertexArray(g.vao);
    GenBuffers(1, &g.vbo); BindBuffer(GL_ARRAY_BUFFER, g.vbo);
    BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
    GenBuffers(1, &g.ibo); BindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ibo);
    BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(m.indices.size() * 4), m.indices.data(), GL_STATIC_DRAW);
    setupAttribs();
    BindVertexArray(0);

    std::vector<SubMesh> subs = m.subs;
    if (subs.empty()) { SubMesh s; s.indexOffset = 0; s.indexCount = (uint32_t)m.indices.size(); subs.push_back(s); }
    {   // world extent for the far plane (world.glb / BSP positions are world space)
        bool worldMesh = false;
        for (const SubMesh& s : subs) if (!s.component.empty()) { worldMesh = true; break; }
        if (worldMesh)
            for (size_t i = 0; i + 2 < m.positions.size(); i += 3) {
                float r2 = m.positions[i] * m.positions[i] + m.positions[i + 1] * m.positions[i + 1] + m.positions[i + 2] * m.positions[i + 2];
                if (r2 > worldRadius_ * worldRadius_) worldRadius_ = std::sqrt(r2);
            }
    }
    int nLM = 0, nProg = 0;
    // Vertex lightmaps (FLightMap1D) cover the component's whole cooked LOD0 vertex buffer, all sections in order; the
    // glTF splits a component into one submesh per section. Per component: the sections' vertex ranges in submesh
    // order, so a section indexes the shared samples at its cumulative offset (M24: multi-section components on
    // Gorge / Rust never bound - "2415 samples vs 954 vertices").
    std::map<std::string, std::vector<std::pair<uint32_t, uint32_t>>> vlmRanges;
    auto componentKey = [&](const SubMesh& s) {
        std::string key = s.component;
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        if (key.rfind("actor:", 0) == 0) {
            auto ac = actorComponent_.find(key.substr(6));
            if (ac != actorComponent_.end() && !ac->second.empty()) key = ac->second;
        }
        return key;
    };
    if (!vertexLMs_.empty())
        for (const SubMesh& s : subs) {
            if (s.indexCount == 0) continue;
            std::string key = componentKey(s);
            if (!vertexLMs_.count(key)) continue;
            uint32_t lo = UINT32_MAX, hi = 0;
            for (uint32_t k = 0; k < s.indexCount; ++k) {
                uint32_t vi = m.indices[s.indexOffset + k];
                lo = std::min(lo, vi); hi = std::max(hi, vi);
            }
            vlmRanges[key].push_back({lo, hi});
        }
    for (const SubMesh& s : subs) {
        Sub d;
        if (!subInBounds(m, s.indexOffset, s.indexCount, "upload")) continue;
        d.first = s.indexOffset; d.count = s.indexCount;
        const Material* mat = (s.material >= 0 && (size_t)s.material < m.mats.size()) ? &m.mats[(size_t)s.material] : nullptr;
        if (!s.component.empty()) g.world = true;
        if (bspMesh_ >= 0 && s.component.rfind("bsp:", 0) == 0) { g.drawsBsp = true; continue; }
        std::string key = s.component;
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        if (key.rfind("actor:", 0) == 0) d.actor = key.substr(6);
        // authored bHidden: actor-placed nodes stay resident (Gameplay may unhide them, e.g. SeqAct_ToggleHidden
        // by game rule); collection components without an actor identity are dropped as before
        // AssetTools world.glb extras.hidden_game (PrimitiveComponent.HiddenGame; e.g. Seed's 29 tubelight
        // components whose section material is null): not drawn in game, as authored-hidden collection components
        if ((hiddenComponents_.count(key) || s.hiddenGame) && !std::getenv("WFC_SHOWHIDDEN") && d.actor.empty()) {
            if (s.hiddenGame) ++hiddenGameSkipped_;
            continue;
        }
        d.noLights = noLightComponents_.count(key) > 0;
        d.dynChannel = dynChannelComponents_.count(key) > 0;
        if (key.rfind("actor:", 0) == 0) {
            auto ac = actorComponent_.find(key.substr(6));
            if (ac != actorComponent_.end() && !ac->second.empty()) key = ac->second;
        }
        auto lit = lightmaps_.find(key);
        bool lm = lit != lightmaps_.end() && m.hasUV1() && !std::getenv("WFC_NOLIGHTMAP");
        auto vit = vertexLMs_.find(key);
        if (!lm && vit != vertexLMs_.end() && !std::getenv("WFC_NOLIGHTMAP") && !std::getenv("WFC_NOVERTEXLM") &&
            s.indexCount > 0) {
            uint32_t lo = UINT32_MAX, hi = 0;
            for (uint32_t k = 0; k < s.indexCount; ++k) {
                uint32_t vi = m.indices[s.indexOffset + k];
                lo = std::min(lo, vi); hi = std::max(hi, vi);
            }
            const VertexLM& v = vit->second;
            // M62: the export duplicated vertices (_WFC_SRCVERT carries each vertex's cooked index): samples are
            // re-ordered into this section's glTF vertex order, so the shader's (gl_VertexID - base) fetch is exact
            bool remap = m.srcVert.size() == m.vertexCount() && hi < m.srcVert.size();
            for (uint32_t k = lo; remap && k <= hi; ++k) if (m.srcVert[k] >= (uint32_t)v.count) remap = false;
            if (remap) {
                const int n = (int)(hi - lo + 1);
                std::vector<float> rgb((size_t)n * 3 * 3);
                for (int r = 0; r < 3; ++r)
                    for (int j = 0; j < n; ++j)
                        for (int c = 0; c < 3; ++c)
                            rgb[((size_t)r * n + j) * 3 + c] = v.rgb[((size_t)r * v.count + m.srcVert[lo + (uint32_t)j]) * 3 + c];
                glGenTextures(1, &d.vlmTex);
                glBindTexture(GL_TEXTURE_2D, d.vlmTex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, n, 3, 0, GL_RGB, GL_FLOAT, rgb.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glBindTexture(GL_TEXTURE_2D, 0);
                d.vlmBase = (int)lo;
                for (int k = 0; k < 3; ++k)
                    for (int c = 0; c < 3; ++c) d.lmScale[k][c] = v.scale[k][c];
                lm = true;
                ++nLM;
                ++vlmRemapped_;
                LOG_INFO("wfc: vertex lightmap %s: section of %d vertices bound through _WFC_SRCVERT (%d cooked samples)",
                         key.c_str(), n, v.count);
            }
            int cum = 0, total = 0;
            {
                const auto& rs = vlmRanges[key];
                bool before = true;
                for (const auto& rg : rs) {
                    int n = (int)(rg.second - rg.first + 1);
                    total += n;
                    if (rg.first == lo && rg.second == hi) before = false;
                    else if (before) cum += n;
                }
                if ((int)(hi - lo + 1) == v.count) cum = 0;          // single-section (or self-contained) component
                else if (total != v.count) cum = -1;                // layout not reproducible: not bound
            }
            static GLint maxTex = 0;
            if (!maxTex) glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
            if (cum >= 0 && v.count > maxTex) {      // M43: a (count x 3) texture wider than the driver allows is invalid
                LOG_WARN("wfc: vertex lightmap %s: %d samples exceed GL_MAX_TEXTURE_SIZE %d; not bound", key.c_str(), v.count, maxTex);
                cum = -2;
            }
            if (remap) cum = -3;                                // bound above
            if (cum >= 0) {
                glGenTextures(1, &d.vlmTex);
                glBindTexture(GL_TEXTURE_2D, d.vlmTex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, v.count, 3, 0, GL_RGB, GL_FLOAT, v.rgb.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glBindTexture(GL_TEXTURE_2D, 0);
                d.vlmBase = (int)lo - cum;
                for (int k = 0; k < 3; ++k)
                    for (int c = 0; c < 3; ++c) d.lmScale[k][c] = v.scale[k][c];
                lm = true;
                ++nLM;
            } else if (cum == -1) {
                LOG_WARN("wfc: vertex lightmap %s: %d samples vs %u vertices (sections total %d); not bound", key.c_str(),
                         v.count, hi - lo + 1, total);
            }
        }
        if (lm && !d.vlmTex) {
            const LMRec& r = lit->second;
            for (int i = 0; i < 3; ++i) {
                auto li = lmIndex_.find(r.coeff[i]);
                if (li == lmIndex_.end()) {
                    GLuint t = texture(dataDir_ + "/lightmaps/" + r.coeff[i] + ".png", true, true, true);
                    lmTextures_.push_back(t);
                    li = lmIndex_.emplace(r.coeff[i], (int)lmTextures_.size() - 1).first;
                }
                d.lmTex[i] = li->second;
                for (int c = 0; c < 3; ++c) d.lmScale[i][c] = r.scale[i][c];
            }
            d.lmCoord[0] = r.cs[0]; d.lmCoord[1] = r.cs[1]; d.lmCoord[2] = r.cb[0]; d.lmCoord[3] = r.cb[1];
            ++nLM;
        }
        std::string matName = mat ? mat->wfcName : std::string();
        if (matName.empty() && !s.sourceMesh.empty()) {   // section left unresolved by the extractor
            auto it = slotMaterials_.find(s.sourceMesh + "|" + std::to_string(s.sourceSection));
            if (it != slotMaterials_.end()) matName = it->second;
        }
        if (matName.empty()) matName = resolveBySourceName(mat);
        mdiWanted_ = g.world;                         // world programs get the MDI variant (WFC_MDI)
        d.prog = programFor(matName, mat, lm);
        mdiWanted_ = false;
        d.matName = matName;
        d.comp = s.component;
        if (d.prog >= 0) ++nProg;
        if (const char* dump = std::getenv("WFC_AUDIT_DUMP")) {   // ACTIVE side of tools/render/audit_map.py
            static FILE* f = std::fopen(dump, "w");
            if (f) {
                const Program* P = d.prog >= 0 ? &progs_[(size_t)d.prog] : nullptr;
                std::fprintf(f, "{\"mesh_id\":%zu,\"component\":\"%s\",\"source_mesh\":\"%s\",\"section\":%d,"
                             "\"material\":\"%s\",\"program\":\"%s\",\"blend\":%d,\"lit\":%d,\"lightmapped\":%d,"
                             "\"scene_depth\":%d,\"tris\":%u,\"authored_hidden\":%d,\"mover\":%d}\n",
                             meshes_.size(), s.component.c_str(), s.sourceMesh.c_str(), s.sourceSection, matName.c_str(),
                             !P ? "none" : P->original ? "original" : "gltf_fallback", P ? P->blend : -1, P && P->lit ? 1 : 0,
                             lm ? 1 : 0, P && P->sceneDepth ? 1 : 0, d.count / 3,
                             !d.actor.empty() && authoredHiddenActors_.count(d.actor) ? 1 : 0,
                             (int)std::count_if(movers_.begin(), movers_.end(), [&](const MoverRT& mv) { return mv.actor == d.actor; }));
                std::fflush(f);
            }
        }
        // bounds
        bool init = false;
        for (uint32_t k = 0; k < d.count; ++k) {
            uint32_t vi = m.indices[d.first + k];
            core::Vec3 p{m.positions[vi * 3], m.positions[vi * 3 + 1], m.positions[vi * 3 + 2]};
            if (!init) { d.bmin = d.bmax = p; init = true; }
            else {
                d.bmin = {std::min(d.bmin.x, p.x), std::min(d.bmin.y, p.y), std::min(d.bmin.z, p.z)};
                d.bmax = {std::max(d.bmax.x, p.x), std::max(d.bmax.y, p.y), std::max(d.bmax.z, p.z)};
            }
        }
        g.subs.push_back(d);
        yieldLoad();                                   // loading presentation: no GL binding held here
    }
    const bool worldUpload = g.world;
    if (hiddenGameSkipped_) { LOG_INFO("wfc: %d HiddenGame component section(s) not drawn", hiddenGameSkipped_); hiddenGameSkipped_ = 0; }
    meshes_.push_back(std::move(g));
    LOG_INFO("wfc: uploaded mesh %zu: %zu verts, %zu submeshes (%d lightmapped, %d programs, %zu total)",
             meshes_.size() - 1, m.vertexCount(), subs.size(), nLM, nProg, progs_.size());
    // M54: the map's world is resident (its own programs built): prewarm the rest under the loading screen
    if (worldUpload && prewarmPending_) { prewarmPending_ = false; prewarmMaterials(); }
    return (int)meshes_.size() - 1;
}

// Cached per-program uniform location for a string literal (per-draw state: ~2200 draws a frame).
static GLint uloc(const Program& P, const char* name) {
    for (const auto& e : P.locCache) if (e.first == name) return e.second;
    GLint l = GetUniformLocation(P.id, name);
    P.locCache.emplace_back(name, l);
    return l;
}

void Pipeline::bindCommon(const Program& P, const core::Mat4& model) {
    UseProgram(P.id);
    UniformMatrix4fv(P.uViewProj, 1, GL_FALSE, viewProj_.m);
    UniformMatrix4fv(P.uModel, 1, GL_FALSE, model.m);
    Uniform3f(P.uCamPos, camPos_.x, camPos_.y, camPos_.z);
    // the uniforms only bindCommon writes: set when their inputs differ from what this program object last received
    static const float dsls = std::getenv("WFC_DSLS") ? std::min(std::max((float)std::atof(std::getenv("WFC_DSLS")), 0.0f), 1.0f) : 0.0f;
    static const int legacyTrans = std::getenv("WFC_M05TRANS") ? 1 : 0;
    static const int dbg = std::getenv("WFC_LIGHTINGONLY") ? 1 : std::getenv("WFC_ALBEDO") ? 2 : 0;
    static const bool noSkip = std::getenv("WFC_NOCOMMONSKIP") != nullptr;   // A/B
    const float key[34] = {time_, P.twoSided ? 1.0f : 0.0f, P.blend == 1 ? 1.0f : 0.0f, P.clip, P.lit ? 1.0f : 0.0f, (float)P.blend,
                           dynParam_[0], dynParam_[1], dynParam_[2], dynParam_[3], (float)poseBlend_, poseAlpha_, dsls,
                           maskTexelOffset_[0], maskTexelOffset_[1], znear_, zfar_, (float)std::max(vpW_, 1), (float)std::max(vpH_, 1),
                           P.sceneDepth ? 1.0f : 0.0f, canvasInvGamma_, (float)legacyTrans, (float)dbg, fogOn_ ? 1.0f : 0.0f,
                           fogMaxH_, fogScale_, fogStart_, fogExt_, fogIn_.x, fogIn_.y, fogIn_.z, 0, 0, 0};
    if (P.id >= commonKeyById_.size()) commonKeyById_.resize((size_t)P.id + 256);
    CommonKey& ck = commonKeyById_[P.id];
    const bool setCommon = noSkip || !ck.valid || std::memcmp(key, ck.v, sizeof key) != 0;
    if (setCommon) { std::memcpy(ck.v, key, sizeof key); ck.valid = true; }
    if (setCommon) {
        Uniform1f(P.uTime, time_);
        Uniform1i(P.uTwoSided, P.twoSided ? 1 : 0);
        Uniform1i(P.uMasked, P.blend == 1 ? 1 : 0);
        Uniform1f(P.uClip, P.clip);
        Uniform1i(P.uLit, P.lit ? 1 : 0);
        Uniform1i(uloc(P, "uBlend"), P.blend);
        Uniform4f(uloc(P, "uDynParam"), dynParam_[0], dynParam_[1], dynParam_[2], dynParam_[3]);
        Uniform1i(uloc(P, "uPoseBlend"), poseBlend_);
        Uniform1f(uloc(P, "uPoseAlpha"), poseAlpha_);
        Uniform1f(uloc(P, "uDSLS"), dsls);
        Uniform2f(uloc(P, "uShadowMaskTexelOffset"), maskTexelOffset_[0], maskTexelOffset_[1]);
        Uniform2f(uloc(P, "uNearFar"), znear_, zfar_);
        Uniform2f(uloc(P, "uViewport"), (float)std::max(vpW_, 1), (float)std::max(vpH_, 1));
        Uniform1i(uloc(P, "uHasSceneDepth"), P.sceneDepth ? 1 : 0);
        Uniform1f(uloc(P, "uCanvasInvGamma"), canvasInvGamma_);
        Uniform1i(uloc(P, "uLegacyTrans"), legacyTrans);
        Uniform1i(uloc(P, "uDebug"), dbg);
        Uniform1i(P.uFogOn, fogOn_ ? 1 : 0);
        Uniform1f(P.uFogMaxH, fogMaxH_); Uniform1f(P.uFogScale, fogScale_);
        Uniform1f(P.uFogStart, fogStart_); Uniform1f(P.uFogExt, fogExt_);
        Uniform3f(P.uFogIn, fogIn_.x, fogIn_.y, fogIn_.z);
    }
    Uniform1i(uloc(P, "uVertexLM"), 0);
    Uniform4f(uloc(P, "uShadowDepth"), 0.0f, 0.0f, 0.0f, 0.0f);
    Uniform1i(uloc(P, "uSkin"), skinMode_);
    if (skinMode_) {
        Uniform1i(uloc(P, "uSkinRow"), skinRow_);
        Uniform1i(uloc(P, "uSkinBones"), skinBones_);
        Uniform1f(uloc(P, "uSkinAlpha"), skinAlpha_);
        Uniform1i(uloc(P, "uBoneTex"), 18);
    }
    {   // shadow-mask inputs (neutral mask = 1 unless a mask is bound for this draw)
        Uniform3f(uloc(P, "uDLAC"), 0.0f, 0.0f, 0.0f);   // set per environment in drawSubs
        ActiveTexture(GL_TEXTURE0 + 10);
        glBindTexture(GL_TEXTURE_2D, shadowMaskTexFor(dynamicMaskDraw_));
    }
    // per-draw runtime parameters: Canvas tiles pass their own; otherwise the material's Matinee-driven values
    // (setMaterialParam on its MaterialInstanceActor); unset = authored
    const std::vector<std::pair<std::string, std::array<float, 4>>>* params = drawParams_;
    if (!params && inDynamicDraw_ && !P.rtLoc.empty()) {   // M70: the draw owner's parameters (held weapon)
        auto op = ownerParams_.find(drawOwner_);
        if (op != ownerParams_.end() && !op->second.empty()) params = &op->second;
        static std::vector<std::pair<std::string, std::array<float, 4>>> diag = [] {
            std::vector<std::pair<std::string, std::array<float, 4>>> d;   // diagnostics: WFC_DRAWPARAM=name,value
            if (const char* e = std::getenv("WFC_DRAWPARAM")) {
                std::string t = e; size_t c = t.find(',');
                if (c != std::string::npos) { float v = (float)std::atof(t.c_str() + c + 1); d.push_back({t.substr(0, c), {v, v, v, 1}}); }
            }
            return d;
        }();
        if (!params && !diag.empty()) params = &diag;
    }
    if (!params && !P.rtLoc.empty() && !matParams_.empty()) {
        auto mp = matParams_.find(P.material);
        if (mp != matParams_.end()) params = &mp->second;
    }
    for (const auto& kv : P.rtLoc) {
        const std::array<float, 4>* v = nullptr;
        if (params)
            for (const auto& pv : *params) if (pv.first == kv.first) v = &pv.second;
        if (kv.second.second >= 0) Uniform1i(kv.second.second, v ? 1 : 0);
        if (v && kv.second.first >= 0) Uniform4f(kv.second.first, (*v)[0], (*v)[1], (*v)[2], (*v)[3]);
    }
    if (P.sceneDepth) { ensureSceneDepth(); ActiveTexture(GL_TEXTURE0 + 12); glBindTexture(GL_TEXTURE_2D, depthCopyTex_); }
    if (P.sceneColor) { ensureSceneColor(); ActiveTexture(GL_TEXTURE0 + 16); glBindTexture(GL_TEXTURE_2D, sceneCopyTex_); }
    VertexAttrib4f(5, fxColor_[0], fxColor_[1], fxColor_[2], fxColor_[3]);   // current value when unbound
    for (const Program::Slot& s : P.slots) {
        ActiveTexture(GL_TEXTURE0 + s.unit);
        glBindTexture(s.cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, s.tex);
    }
}

float Pipeline::viewDepth(const core::Vec3& p) const {
    const float* v = camView_.m;
    return -(v[2] * p.x + v[6] * p.y + v[10] * p.z + v[14]);
}

void Pipeline::flushTranslucency() {
    if (transQueue_.empty()) return;
    std::stable_sort(transQueue_.begin(), transQueue_.end(),
                     [](const TransItem& a, const TransItem& b) { return a.key > b.key; });   // far -> near
    flushingTrans_ = true;
    std::vector<TransItem>& q = transQueue_;          // nothing is queued while flushing (flushingTrans_)
    // Adjacent sprite batches (after the far -> near sort) with the same material, dynamic parameters and facing are
    // drawn as one call with their sprites in queue order: the same primitives in the same order with the same state,
    // so the result is identical for every blend mode, with fewer draw calls (bot firefights: hundreds of
    // per-emitter batches). WFC_NOSPRITEMERGE=1 = A/B.
    static const bool noMerge = std::getenv("WFC_NOSPRITEMERGE") != nullptr;
    auto same = [](const SpriteBatch& a, const SpriteBatch& b) {
        return a.mat == b.mat && a.facing.x == b.facing.x && a.facing.y == b.facing.y && a.facing.z == b.facing.z &&
               std::equal(a.dyn, a.dyn + 4, b.dyn);
    };
    auto runItem = [&](const TransItem& it) {
        if (it.kind == 0) {
            const TransSub& t = transSubs_[(size_t)it.idx];
            std::copy(t.col, t.col + 4, fxColor_);
            std::copy(t.dyn, t.dyn + 4, dynParam_);
            frameFx_ = t.fx;
            drawSubs(meshes_[(size_t)t.meshIdx], t.mdl, t.dynamicObject, t.sub);
            frameFx_ = false;
            std::fill(fxColor_, fxColor_ + 4, 1.0f);
            std::fill(dynParam_, dynParam_ + 4, 1.0f);
        } else {
            SpriteBatch& b = spritePool_[(size_t)it.idx];
            std::copy(b.dyn, b.dyn + 4, dynParam_);
            drawSprites(b.mat.c_str(), b.sprites.data(), b.sprites.size(), b.facing);
            std::fill(dynParam_, dynParam_ + 4, 1.0f);
        }
    };
    auto batchOf = [&](size_t i) -> SpriteBatch* { return q[i].kind == 1 ? &spritePool_[(size_t)q[i].idx] : nullptr; };
    // Frame sprite stream (300+ fps lobbies: one BufferData set + per-batch setup per sprite draw was ~19 % of the main
    // thread in 64-player firefights). Every sprite group (the merge rule below, unchanged) is appended in draw order
    // to one vertex stream, uploaded once, and drawn as its index range: the same vertices, primitives, order and
    // state as the per-batch upload. WFC_NOSPRITESTREAM=1 = per-batch uploads (A/B).
    static const bool noStream = std::getenv("WFC_NOSPRITESTREAM") != nullptr;
    struct Group { size_t i, j; int prog; size_t quad0, quads; };
    static std::vector<Group> groups;
    groups.clear();
    if (!noStream) {
        spriteFrameV_.clear(); spriteFrameCol_.clear(); spriteFrameSub_.clear();
        for (size_t i = 0; i < q.size(); ++i) {
            if (!batchOf(i)) continue;
            size_t j = i + 1;
            if (!noMerge) while (j < q.size() && batchOf(j) && same(*batchOf(j), *batchOf(i))) ++j;
            SpriteBatch& b = *batchOf(i);
            const int prog = spriteProgram(b.mat);
            Group gr{i, j, prog, spriteFrameV_.size() / 56, 0};
            if (prog >= 0)
                for (size_t k = i; k < j; ++k) {
                    SpriteBatch& bk = *batchOf(k);
                    spriteCoverage(b.mat.c_str(), bk.sprites.data(), bk.sprites.size());
                    spriteAppend(bk.sprites.data(), bk.sprites.size(), b.facing, spriteFrameV_, spriteFrameCol_, spriteFrameSub_);
                    gr.quads += bk.sprites.size();
                }
            if (prog >= 0 && j > i + 1) frameRecs_[frameNo_ & 3].draws -= (int)(j - i - 1);   // one draw per group
            groups.push_back(gr);
            i = j - 1;
        }
        const size_t quads = spriteFrameV_.size() / 56;
        if (quads > 0) {
            if (!spriteFrameVao_) {
                GenVertexArrays(1, &spriteFrameVao_);
                GenBuffers(1, &spriteFrameVbo_); GenBuffers(1, &spriteFrameCbo_); GenBuffers(1, &spriteFrameSbo_); GenBuffers(1, &spriteFrameIbo_);
            }
            BindVertexArray(spriteFrameVao_);
            BindBuffer(GL_ARRAY_BUFFER, spriteFrameVbo_);
            BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(spriteFrameV_.size() * sizeof(float)), spriteFrameV_.data(), GL_STREAM_DRAW);
            setupAttribs();
            BindBuffer(GL_ARRAY_BUFFER, spriteFrameCbo_);
            BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(spriteFrameCol_.size() * sizeof(float)), spriteFrameCol_.data(), GL_STREAM_DRAW);
            EnableVertexAttribArray(5); VertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, 0, (void*)0);
            BindBuffer(GL_ARRAY_BUFFER, spriteFrameSbo_);
            BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(spriteFrameSub_.size() * sizeof(float)), spriteFrameSub_.data(), GL_STREAM_DRAW);
            EnableVertexAttribArray(6); VertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
            BindBuffer(GL_ELEMENT_ARRAY_BUFFER, spriteFrameIbo_);
            if (quads > spriteFrameIboQuads_) {               // quad index pattern, grown on demand (static contents)
                size_t cap = std::max<size_t>(quads, spriteFrameIboQuads_ * 2);
                std::vector<uint32_t> idx(cap * 6);
                for (size_t qd = 0; qd < cap; ++qd) {
                    const uint32_t b4 = (uint32_t)(qd * 4);
                    const uint32_t qq[6] = {b4, b4 + 1, b4 + 2, b4, b4 + 2, b4 + 3};
                    std::copy(qq, qq + 6, &idx[qd * 6]);
                }
                BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
                spriteFrameIboQuads_ = cap;
            }
            BindVertexArray(0);
        }
    }
    size_t gi = 0;
    for (size_t i = 0; i < q.size(); ++i) {
        if (!noStream && batchOf(i)) {                     // a sprite group from the frame stream
            const Group& gr = groups[gi++];
            if (gr.prog >= 0 && gr.quads > 0) {
                SpriteBatch& b = *batchOf(i);
                std::copy(b.dyn, b.dyn + 4, dynParam_);
                GpuMesh& g = spriteFrameMesh_;
                g.vao = spriteFrameVao_;
                if (g.subs.empty()) g.subs.emplace_back();
                Sub& d = g.subs[0];
                d.first = (uint32_t)(gr.quad0 * 6); d.count = (uint32_t)(gr.quads * 6); d.prog = gr.prog;
                d.matName = b.mat;                                 // diagnostics (WFC_SKIPMAT)
                frameFx_ = true;
                drawSubs(g, core::Mat4::identity(), true);
                frameFx_ = false;
                std::fill(dynParam_, dynParam_ + 4, 1.0f);
            }
            ++statSpriteBatches_; statSpriteMerged_ += (int)(gr.j - gr.i - 1);
            i = gr.j - 1;
            continue;
        }
        if (!batchOf(i) || noMerge) { runItem(q[i]); continue; }
        size_t j = i + 1;
        while (j < q.size() && batchOf(j) && same(*batchOf(j), *batchOf(i))) ++j;
        if (j == i + 1) { runItem(q[i]); ++statSpriteBatches_; continue; }
        SpriteBatch& b = *batchOf(i);
        for (size_t k = i + 1; k < j; ++k) b.sprites.insert(b.sprites.end(), batchOf(k)->sprites.begin(), batchOf(k)->sprites.end());
        std::copy(b.dyn, b.dyn + 4, dynParam_);
        drawSprites(b.mat.c_str(), b.sprites.data(), b.sprites.size(), b.facing);
        std::fill(dynParam_, dynParam_ + 4, 1.0f);
        ++statSpriteBatches_; statSpriteMerged_ += (int)(j - i - 1);
        i = j - 1;
    }
    q.clear(); transSubs_.clear(); spriteUsed_ = 0;     // capacities kept for the next frame
    flushingTrans_ = false;
}

void Pipeline::drawSubs(GpuMesh& g, const core::Mat4& model, bool dynamicObject, int onlySub) {
    BindVertexArray(g.vao);
    core::Vec3 origin{model.m[12], model.m[13], model.m[14]};
    LightEnv dynEnv;
    bool dynEnvReady = false;
    static const long reportAt = std::getenv("WFC_FRAMEREPORT") && std::getenv("WFC_SMOKE_FRAMES")
                                     ? std::atol(std::getenv("WFC_SMOKE_FRAMES")) : -1;
    const bool reportFrame = reportAt > 0 && frameNo_ == (int)reportAt;
    // small dynamic object (world-space bounding radius < 0.5 m): shares a per-cell environment
    bool smallDynamic = false;
    if (dynamicObject && !g.subs.empty()) {
        core::Vec3 mn = g.subs[0].bmin, mx = g.subs[0].bmax;
        for (const Sub& s : g.subs) {
            mn = {std::min(mn.x, s.bmin.x), std::min(mn.y, s.bmin.y), std::min(mn.z, s.bmin.z)};
            mx = {std::max(mx.x, s.bmax.x), std::max(mx.y, s.bmax.y), std::max(mx.z, s.bmax.z)};
        }
        float sc = std::max(core::length(core::Vec3{model.m[0], model.m[1], model.m[2]}),
                   std::max(core::length(core::Vec3{model.m[4], model.m[5], model.m[6]}),
                            core::length(core::Vec3{model.m[8], model.m[9], model.m[10]})));
        smallDynamic = core::length(mx - mn) * 0.5f * sc < 0.5f && core::length(mx - mn) > 0.0f;
    }
    // persistent mesh (index into meshes_): its translucent subs can be queued for the sorted translucency pass
    const long meshIdx = (&g >= meshes_.data() && &g < meshes_.data() + meshes_.size()) ? (long)(&g - meshes_.data()) : -1;
    static const bool immediateTrans = std::getenv("WFC_IMMEDIATETRANS") != nullptr || std::getenv("WFC_M05TRANS") != nullptr;   // diagnostics: old order
    const bool canDefer = deferTrans_ && !flushingTrans_ && !immediateTrans && meshIdx >= 0 && !g.decal;
    const bool mdiMesh = meshIdx >= 0 && meshIdx == mdiMesh_ && !warmup_ && onlySub < 0 && !mdiBuckets_.empty();
    if (mdiMesh) drawMdi(g);
    for (int pass = onlySub >= 0 ? 1 : 0; pass < 2; ++pass) {          // 0: opaque + masked, 1: translucent
        // one sub (a queued translucent draw): index it directly - scanning every sub of the world mesh per queued
        // item was ~10 % of the main thread at 10 v 10
        const size_t siBegin = onlySub >= 0 ? (size_t)onlySub : 0;
        const size_t siEnd = onlySub >= 0 ? std::min((size_t)onlySub + 1, g.subs.size()) : g.subs.size();
        for (size_t si = siBegin; si < siEnd; ++si) {
            if (onlySub >= 0 && (int)si != onlySub) continue;
            Sub& s = g.subs[si];
            if (mdiMesh && pass == 0 && s.mdiRow >= 0) continue;   // drawn by drawMdi
            if (s.prog < 0) {
                ++counts_.noProgram;
                if (frameNoProg_.size() < 64) frameNoProg_.insert(s.matName.empty() ? std::string("<none>") : s.matName);
                continue;
            }
            static const char* skipMat = std::getenv("WFC_SKIPMAT");   // diagnostics: hide by material
            if (skipMat) {                    // ';'-separated substrings, "<none>" = no material identity
                bool hide = false;
                std::string list = skipMat;
                size_t a = 0;
                while (a <= list.size() && !hide) {
                    size_t b = list.find(';', a);
                    std::string tok = list.substr(a, b == std::string::npos ? std::string::npos : b - a);
                    if (tok.rfind("comp:", 0) == 0) { if (s.comp.find(tok.substr(5)) != std::string::npos) hide = true; }
                    else if (tok == "<none>" ? s.matName.empty() : (!tok.empty() && s.matName.find(tok) != std::string::npos))
                        hide = true;
                    if (b == std::string::npos) break;
                    a = b + 1;
                }
                if (hide) continue;
            }
            const Program& P = progs_[(size_t)s.prog];
            bool trans = P.blend >= 2;
            if ((pass == 1) != trans) continue;
            if (trans && canDefer) {
                core::Vec3 c = core::transformPoint(model, (s.bmin + s.bmax) * 0.5f);
                float col[4], dyn[4];
                std::copy(fxColor_, fxColor_ + 4, col);
                std::copy(dynParam_, dynParam_ + 4, dyn);
                const bool fx = frameFx_;
                TransSub t;
                t.meshIdx = meshIdx; t.sub = (int)si; t.dynamicObject = dynamicObject; t.fx = fx; t.mdl = model;
                std::copy(col, col + 4, t.col); std::copy(dyn, dyn + 4, t.dyn);
                transSubs_.push_back(t);
                transQueue_.push_back({viewDepth(c), 0, (int)transSubs_.size() - 1});
                continue;
            }
            core::Mat4 subModel = model;
            bool moving = false;
            if (!s.actor.empty()) {
                if (actorHidden(s.actor)) continue;
                auto mv = moverDelta_.find(s.actor);
                if (mv != moverDelta_.end()) { subModel = mv->second * model; moving = true; }
            }
            // frustum cull on the sub's bounds: valid only for baked world geometry (identity model); placed
            // meshes with authored components (map props) and movers keep their local / moving bounds
            const bool bakedPlacement = model.m[0] == 1.0f && model.m[5] == 1.0f && model.m[10] == 1.0f &&
                                        model.m[12] == 0.0f && model.m[13] == 0.0f && model.m[14] == 0.0f;
            static const bool noFrustum = std::getenv("WFC_NOFRUSTUMCULL") != nullptr;   // diagnostics: culling regression test
            if (g.world && !moving && bakedPlacement && !noFrustum && !warmup_) {
                bool out = false;
                for (int f = 0; f < 6 && !out; ++f) {
                    const float* pl = frustum_[f];
                    core::Vec3 pv{pl[0] >= 0 ? s.bmax.x : s.bmin.x, pl[1] >= 0 ? s.bmax.y : s.bmin.y,
                                  pl[2] >= 0 ? s.bmax.z : s.bmin.z};
                    if (pl[0] * pv.x + pl[1] * pv.y + pl[2] * pv.z + pl[3] < 0) out = true;
                }
                if (out) { ++counts_.culled; continue; }
            }
            bindCommon(P, subModel);
            Uniform1i(uloc(P, "uDecalClip"), g.decal ? 1 : 0);
            {
                // TnCharacterApplier params: dynamic (character) draws only; all-zero RGB skips.
                const CharacterColors& cc = charColorsBy_[drawOwner_];
                const float* src[3] = {cc.primary, cc.secondary, cc.energon};
                for (int i = 0; i < 3; ++i) {
                    if (P.uRTSet[i] < 0) continue;
                    bool set = dynamicObject && (src[i][0] != 0.0f || src[i][1] != 0.0f || src[i][2] != 0.0f);
                    Uniform1i(P.uRTSet[i], set ? 1 : 0);
                    if (set) Uniform4f(P.uRT[i], src[i][0], src[i][1], src[i][2], src[i][3]);
                }
            }
            static const bool noCull = std::getenv("WFC_NOCULL") != nullptr;   // diagnostics: winding check
            if (P.twoSided || noCull) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
            switch (P.blend) {
                case 2: glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); break;
                case 3: glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); glDepthMask(GL_FALSE); break;
                case 4: glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR, GL_ZERO); glDepthMask(GL_FALSE); break;
                default: glDisable(GL_BLEND); glDepthMask(GL_TRUE); break;
            }
            if (s.vlmTex) {
                Uniform4f(P.uLMCoord, 1, 1, 0, 0);
                Uniform3fv(P.uLMScale, 3, &s.lmScale[0][0]);
                Uniform1i(uloc(P, "uVertexLM"), 1);
                Uniform1i(uloc(P, "uVLMBase"), s.vlmBase);
                ActiveTexture(GL_TEXTURE0 + 11);
                glBindTexture(GL_TEXTURE_2D, s.vlmTex);
            } else if (s.lmTex[0] >= 0) {
                Uniform4f(P.uLMCoord, s.lmCoord[0], s.lmCoord[1], s.lmCoord[2], s.lmCoord[3]);
                Uniform3fv(P.uLMScale, 3, &s.lmScale[0][0]);
                for (int i = 0; i < 3; ++i) {
                    ActiveTexture(GL_TEXTURE0 + 13 + i);
                    glBindTexture(GL_TEXTURE_2D, lmTextures_[(size_t)s.lmTex[i]] ? lmTextures_[(size_t)s.lmTex[i]] : blackTex_);
                }
            } else if (!P.lit) {
                Uniform4f(P.uLMCoord, 1, 1, 0, 0);
            } else {
                Uniform4f(P.uLMCoord, 1, 1, 0, 0);
                const LightEnv* env;
                if (dynamicObject && !dynEnvReady && smallDynamic) {
                    core::Vec3 p = origin;
                    auto q = [](float v) { return (uint64_t)(uint32_t)(int32_t)std::floor(v) & 0x1FFFFFull; };
                    uint64_t key = (q(p.x) << 42) | (q(p.y) << 21) | q(p.z);
                    CellEnv& ce = cellEnv_[key];
                    constexpr int kCellEnvFrames = 60;
                    if (frameNo_ - ce.frame > kCellEnvFrames) {
                        core::Vec3 c{std::floor(p.x) + 0.5f, std::floor(p.y) + 0.5f, std::floor(p.z) + 0.5f};
                        computeEnv(c, true, ce.env);
                        ce.frame = frameNo_;
                    }
                    dynEnv = ce.env;
                    dynEnvReady = true;
                }
                if (dynamicObject && !dynEnvReady && envForm_ >= 0 && dle_.count(envForm_) &&
                    dle_[envForm_].initialized && !std::getenv("WFC_OLDCHARENV")) {
                    dynEnv = dle_[envForm_].env;
                    dynEnvReady = true;
                }
                if (dynamicObject) {
                    if (!dynEnvReady) {
                        core::Vec3 p = origin + core::Vec3{0, 2.0f, 0};
                        EnvCache* best = nullptr;
                        for (EnvCache& c : envCache_)
                            if (core::length(c.pos - p) < 0.3f) { best = &c; break; }
                        if (!best) {
                            // reuse the nearest stale slot (same object moved) or add one
                            for (EnvCache& c : envCache_)
                                if (!best || core::length(c.pos - p) < core::length(best->pos - p)) best = &c;
                            if (!best || envCache_.size() < 4) { envCache_.push_back({}); best = &envCache_.back(); }
                            if (best->lastFrame == frameNo_ && envCache_.size() < 8) { envCache_.push_back({}); best = &envCache_.back(); }
                            best->pos = p;
                            computeEnv(p, true, best->env);
                        }
                        best->lastFrame = frameNo_;
                        dynEnv = best->env;
                        dynEnvReady = true;
                    }
                    env = &dynEnv;
                } else if (s.dynChannel && !s.noLights) {
                    // Dynamic-channel primitive (movable actor with a light environment): the Dynamic-channel lights
                    // at its current position, re-evaluated per frame as it moves (rotating debris, Matinee ships)
                    core::Vec3 c = core::transformPoint(subModel, (s.bmin + s.bmax) * 0.5f);
                    computeEnv(c, true, s.env);
                    env = &s.env;
                } else {
                    if (!s.envReady) {
                        if (s.noLights) s.env = LightEnv{};    // no overlapping lighting channel: emissive only
                        else computeEnv((s.bmin + s.bmax) * 0.5f, false, s.env);
                        s.envReady = true;
                    }
                    env = &s.env;
                }
                if (reportFrame && dynamicObject && !frameFx_) {
                    char buf[512];
                    std::string ls;
                    for (int i = 0; i < env->n; ++i) {
                        int li = env->light[i];
                        std::snprintf(buf, sizeof buf, "%s%s(vis %.2f)", i ? ", " : "",
                                      li >= 0 ? lights_[(size_t)li].name.c_str() : "?", env->spot[i][3]);
                        ls += buf;
                    }
                    std::snprintf(buf, sizeof buf, "%s: %d direct [%s]; ambient cube +Y (%.3f %.3f %.3f) -Y (%.3f %.3f %.3f); samples %s",
                                  s.matName.c_str(), env->n, ls.c_str(), env->cube[2].x, env->cube[2].y, env->cube[2].z,
                                  env->cube[3].x, env->cube[3].y, env->cube[3].z,
                                  envSamples_ ? (envSamples_->size() == 6 ? "robot(6)" : "vehicle(5)") : "centre(1)");
                    frameEnvs_.push_back(buf);
                }
                Uniform3fv(P.uAmb, 6, &env->cube[0].x);
                Uniform1i(P.uNumLights, env->n);
                Uniform4fv(P.uLPos, 3, &env->pos[0][0]);
                Uniform4fv(P.uLDir, 3, &env->dir[0][0]);
                Uniform4fv(P.uLCol, 3, &env->col[0][0]);
                Uniform4fv(P.uLSpot, 3, &env->spot[0][0]);
                static const char* dlacOverride = std::getenv("WFC_DLAC");   // test override only
                if (dlacOverride) { float v = (float)std::atof(dlacOverride); Uniform3f(uloc(P, "uDLAC"), v, v, v); }
                else Uniform3f(uloc(P, "uDLAC"), env->dlac[0], env->dlac[1], env->dlac[2]);
            }
            const bool queued = dynamicObject && skinMode_ != 0 && P.instProg >= 0 && !trans && P.distProg < 0 && !g.decal &&
                                !frameFx_ && !reportFrame && s.vlmTex == 0 && s.lmTex[0] < 0 && dynamicMaskDraw_ &&
                                fxColor_[0] == 1.0f && fxColor_[1] == 1.0f && fxColor_[2] == 1.0f && fxColor_[3] == 1.0f &&
                                queueInstance(P, s.first, s.count, g.vao);
            if (!queued) {
                if (dynamicObject && skinMode_ != 0) ++gStats.instRejected;
                glDrawElements(GL_TRIANGLES, (GLsizei)s.count, GL_UNSIGNED_INT, (void*)(size_t)(s.first * 4));
            }
            ++gStats.draws;
            ++counts_.draws;
            if (meshIdx >= 0 && meshIdx == bspMesh_) ++counts_.bspDraws;
            else if (g.world) ++counts_.worldDraws;
            if (dynamicObject) ++counts_.dynamicDraws;
            if (frameFx_) ++counts_.fxDraws;
            if (trans) ++counts_.translucent;
            else { ++counts_.opaque; if (!glIsEnabled(GL_DEPTH_TEST)) ++counts_.opaqueNoDepthTest; }
            if (s.lmTex[0] >= 0 || s.vlmTex != 0) ++counts_.lightmapped;
            if (s.matKey < 0) {
                auto mk = matKeys_.emplace(s.matName, (int)matKeys_.size());
                s.matKey = mk.first->second;
                if ((size_t)s.matKey >= matSeenFrame_.size()) matSeenFrame_.resize((size_t)s.matKey + 1, -1);
            }
            if (matSeenFrame_[(size_t)s.matKey] != frameNo_) { matSeenFrame_[(size_t)s.matKey] = frameNo_; ++frameMatCount_; }
            if ((size_t)s.prog < progSeen_.size() && !progSeen_[(size_t)s.prog]) { progSeen_[(size_t)s.prog] = 1; ++counts_.programs; }
            if (reportFrame) {
                FrameDraw& fd = frameDraws_[s.matName.empty() ? std::string("<gltf>") : s.matName];
                ++fd.draws; fd.blend = P.blend; fd.lit = P.lit; fd.lightmapped |= s.lmTex[0] >= 0;
                fd.vertexLM |= s.vlmTex != 0; fd.distortion |= P.distProg >= 0; fd.dynamic |= dynamicObject;
                fd.fx |= frameFx_;
            }
            if (!trans) depthDirty_ = true;
            if (P.distProg >= 0 && distFbo_ && !std::getenv("WFC_NODISTORTION")) {
                const Program& D = progs_[(size_t)P.distProg];
                BindFramebuffer(GL_FRAMEBUFFER, distFbo_);
                if (!distUsed_) { glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT); distUsed_ = true; }
                bindCommon(D, model);
                glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); glDepthMask(GL_FALSE);
                glDrawElements(GL_TRIANGLES, (GLsizei)s.count, GL_UNSIGNED_INT, (void*)(size_t)(s.first * 4));
                BindFramebuffer(GL_FRAMEBUFFER, fbo_);
                bindCommon(P, model);   // restore the colour program's state for the next sub
            }
        }
    }
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    BindVertexArray(0);
    UseProgram(0);
    ActiveTexture(GL_TEXTURE0);
}

// AMD stability guard (M09): non-finite transforms / vertices are skipped and reported, never submitted (a NaN / INF
// position or matrix yields undefined primitives - one suspect for the RX 7900 XTX resets / freezes).
static bool finiteMat(const core::Mat4& m) {
    for (float v : m.m) if (!std::isfinite(v)) return false;
    return true;
}
static void reportNonFinite(const char* what, const std::string& detail) {
    static int n = 0;
    if (n++ < 20) LOG_ERROR("render guard: non-finite %s (%s) - draw skipped", what, detail.c_str());
}

void Pipeline::draw(int id, const core::Mat4& model) {
    if (id < 0 || (size_t)id >= meshes_.size()) return;
    if (!finiteMat(model)) { reportNonFinite("model matrix", "mesh " + std::to_string(id)); return; }
    GpuMesh& g = meshes_[(size_t)id];
    drawSubs(g, model, !g.world);
    if (g.drawsBsp && bspMesh_ >= 0 && bspMesh_ != id) drawSubs(meshes_[(size_t)bspMesh_], model, false);
    if (g.drawsBsp && testMesh_ >= 0) drawSubs(meshes_[(size_t)testMesh_], testModel_, true);
    if (g.drawsBsp && decalMesh_ >= 0 && decalMesh_ != id && !std::getenv("WFC_NODECALS")) {
        // DecalComponent DepthBias (-0.0002): pull decals toward the camera over their receivers.
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -4.0f);
        drawSubs(meshes_[(size_t)decalMesh_], model, false);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    if (g.drawsBsp && !rtDecals_.empty()) {
        updateRuntimeDecals();
        if (rtDecalMesh_ >= 0 && !rtDecals_.empty()) {   // M73: the same bias as the static decals [HIGH]
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -4.0f);
            drawSubs(meshes_[(size_t)rtDecalMesh_], model, false);
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
    }
}

// M75 (Integration 08n: the first match frame read 150-770 ms on the GPU timer, CPU span equal: the GPU waiting on
// submission). The world's share is the driver's first-draw work over ~2,800 draws: 21-23 ms of the first frame on
// Molten / Debris, ~3 ms after this warm-up, which costs 30-40 ms of load (~380 ms with a cold driver shader cache).
// The rest of that frame is the player character's materials (programs + texture decode, ~160 ms) unless the caller
// prewarms the body (prewarmDynamicMesh). The world is drawn once, unculled and hidden, right after its upload and
// the material prewarm, while the loading screen still presents; glFinish lets the GPU-side residency complete there
// too. Frame state (frame number, map clock, camera, counters) is saved and restored: the next frame is unchanged.
void Pipeline::warmupWorld(int id, int w, int h) {
    if (id >= 0 && (size_t)id < meshes_.size()) buildMdi(id);
    if (!active_ || id < 0 || (size_t)id >= meshes_.size() || std::getenv("WFC_NOWARMUP")) return;
    const auto t0 = std::chrono::steady_clock::now();
    w = w > 0 ? w : 1280; h = h > 0 ? h : 720;
    const core::Mat4 vp = viewProj_, cp = camProj_, cv = camView_;
    const core::Vec3 pos = camPos_;
    const float zn = znear_, zf = zfar_;
    const int vw = vpW_, vh = vpH_;
    const bool defer = deferTrans_, dirty = depthDirty_, copied = sceneColorCopied_;
    const FrameCounts counts = counts_;
    float fr[6][4]; std::memcpy(fr, frustum_, sizeof fr);
    vpW_ = w; vpH_ = h;
    ensureTargets(w, h);
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Camera cam;                                    // any view: nothing is culled and the target is never presented
    cam.pos = {0, 0, 0}; cam.zfar = std::max(cam.zfar, worldRadius_ * 2.0f + 1000.0f);
    camPos_ = cam.pos; znear_ = cam.znear; zfar_ = cam.zfar;
    camProj_ = cam.proj(); camView_ = cam.view(); viewProj_ = camProj_ * camView_;
    counts_ = FrameCounts();
    deferTrans_ = false;                           // translucent draws immediately (no queue to flush)
    depthDirty_ = true;
    touchNewTextures();                                // the load's programs (time-sliced) and textures, then the world
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w, h);
    warmup_ = true;
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
    draw(id, core::Mat4::identity());
    warmup_ = false;
    // the static light environments it cached were evaluated before the first frame's movers / Matinee light state:
    // dropped, so the first frame computes them exactly as without the warm-up
    for (GpuMesh& gm : meshes_)
        for (Sub& sb : gm.subs) sb.envReady = false;
    const int draws = counts_.draws;
    if (std::getenv("WFC_BATCHSTATS")) {   // diagnostics: how far static batching could merge the world's draws
        int total = 0, trans = 0, movers = 0, vlm = 0, lm = 0, litEnv = 0, unlit = 0, decals = 0, noProg = 0;
        std::set<std::tuple<int, int, int, int>> bucketTex;                 // program + lightmap page textures
        std::set<std::string> bucketExact;                                 // + every per-draw constant
        std::set<int> bucketProg;                                          // program only (lightmap pages in an array)
        int runsTex = 0, runsProg = 0;                                     // consecutive same-bucket runs (draw order kept)
        std::tuple<int, int, int, int> lastTex{-2, -2, -2, -2}; int lastProg = -2; GLint depthFunc = 0;
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
        for (size_t mi = 0; mi < meshes_.size(); ++mi) {
            const GpuMesh& gm = meshes_[mi];
            const bool worldish = gm.world || (long)mi == bspMesh_ || (long)mi == decalMesh_;
            if (!worldish) continue;
            for (const Sub& sb : gm.subs) {
                ++total;
                if (sb.prog < 0) { ++noProg; continue; }
                const Program& Pg = progs_[(size_t)sb.prog];
                if (gm.decal || (long)mi == decalMesh_) { ++decals; continue; }
                if (Pg.blend >= 2) { ++trans; continue; }
                if (!sb.actor.empty()) { ++movers; continue; }
                if (sb.vlmTex) { ++vlm; }
                else if (sb.lmTex[0] >= 0) ++lm;
                else if (Pg.lit && !sb.noLights) ++litEnv;
                else ++unlit;
                bucketTex.insert({sb.prog, sb.lmTex[0], sb.lmTex[1], sb.lmTex[2]});
                bucketProg.insert(sb.prog);
                const std::tuple<int, int, int, int> tk{sb.prog, sb.lmTex[0], sb.lmTex[1], sb.lmTex[2]};
                if (tk != lastTex) { ++runsTex; lastTex = tk; }
                if (sb.prog != lastProg) { ++runsProg; lastProg = sb.prog; }
                char buf[512];
                std::snprintf(buf, sizeof buf, "%d|%d,%d,%d|%u|%.6g,%.6g,%.6g,%.6g|%.6g,%.6g,%.6g|%d|%s", sb.prog, sb.lmTex[0], sb.lmTex[1],
                              sb.lmTex[2], sb.vlmTex, sb.lmCoord[0], sb.lmCoord[1], sb.lmCoord[2], sb.lmCoord[3], sb.lmScale[0][0],
                              sb.lmScale[1][0], sb.lmScale[2][0], sb.noLights ? 1 : 0,
                              (Pg.lit && sb.lmTex[0] < 0 && !sb.vlmTex) ? (std::to_string((long long)(sb.bmin.x * 10)) + "," +
                                                                           std::to_string((long long)(sb.bmin.z * 10))).c_str() : "");
                bucketExact.insert(buf);
            }
        }
        LOG_INFO("wfc batch stats: %d world subs = %d translucent (sorted, kept), %d movers (kept), %d decals (kept), %d no program; "
                 "mergeable %d (lightmapped %d, vertex-lightmapped %d, lit by a static light env %d, unlit %d); buckets: "
                 "program only %zu, program + lightmap page %zu, every per-draw constant identical %zu",
                 total, trans, movers, decals, noProg, lm + vlm + litEnv + unlit, lm, vlm, litEnv, unlit, bucketProg.size(), bucketTex.size(),
                 bucketExact.size());
        LOG_INFO("wfc batch stats: in draw order, consecutive runs: same program %d, same program + lightmap page %d (depth func 0x%x)",
                 runsProg, runsTex, (unsigned)depthFunc);
    }
    touchNewTextures();                                // anything the world draw created
    glFinish();
    viewProj_ = vp; camProj_ = cp; camView_ = cv; camPos_ = pos; znear_ = zn; zfar_ = zf;
    vpW_ = vw; vpH_ = vh;
    deferTrans_ = defer; depthDirty_ = dirty; sceneColorCopied_ = copied;
    counts_ = counts;
    std::memcpy(frustum_, fr, sizeof fr);
    BindFramebuffer(GL_FRAMEBUFFER, 0);
    LOG_INFO("wfc: warm-up draw of the world: %d draws at %dx%d in %.0f ms (first-use touch: %d programs, %d textures so far)",
             draws, w, h, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
             touchedPrograms_, touchedTextures_);
}

std::string Pipeline::frameRecordText(int frame) const {
    const FrameRec& fr = frameRecs_[frame & 3];
    if (fr.frame != frame) return "no record";
    std::vector<std::pair<double, std::string>> top;
    for (const auto& kv : fr.matCov) top.push_back({kv.second, kv.first});
    std::sort(top.rbegin(), top.rend());
    char buf[160];
    std::snprintf(buf, sizeof buf, "%d sprites in %d draws covering %.1f screens", fr.sprites, fr.draws, fr.coverage);
    std::string s = buf;
    for (size_t i = 0; i < top.size() && i < 4; ++i) {
        std::snprintf(buf, sizeof buf, "%s %s %.1f", i ? "," : "; top:", top[i].second.substr(top[i].second.rfind('.') + 1).c_str(), top[i].first);
        s += buf;
    }
    return s;
}

int Pipeline::addRuntimeDecal(MeshData&& mesh, float lifetime) {
    if (!active_ || mesh.empty()) return -1;
    // DecalManager MaxActiveDecals 50, shared by all dynamic decals: when full the OLDEST active one is recycled
    if (rtDecals_.size() >= 50) rtDecals_.erase(rtDecals_.begin());
    RuntimeDecal d; d.born = time_; d.life = lifetime; d.mesh = std::move(mesh);
    rtDecals_.push_back(std::move(d));
    rtDecalsDirty_ = true;
    return (int)rtDecals_.size();
}

void Pipeline::updateRuntimeDecals() {
    // expiry at the lifetime, no fade (RE s12 add. 28: no script / material fade; stock lifetime countdown [HIGH])
    const size_t n0 = rtDecals_.size();
    rtDecals_.erase(std::remove_if(rtDecals_.begin(), rtDecals_.end(),
                                   [&](const RuntimeDecal& d) { return d.life > 0 && time_ - d.born >= d.life; }),
                    rtDecals_.end());
    if (rtDecals_.size() != n0) rtDecalsDirty_ = true;
    if (!rtDecalsDirty_) return;
    rtDecalsDirty_ = false;
    MeshData all;
    std::map<std::string, int> matIdx;
    for (const RuntimeDecal& d : rtDecals_) {
        const uint32_t base = (uint32_t)all.vertexCount();
        all.positions.insert(all.positions.end(), d.mesh.positions.begin(), d.mesh.positions.end());
        all.normals.insert(all.normals.end(), d.mesh.normals.begin(), d.mesh.normals.end());
        all.uv.insert(all.uv.end(), d.mesh.uv.begin(), d.mesh.uv.end());
        const std::string& mn = d.mesh.mats.empty() ? std::string() : d.mesh.mats[0].wfcName;
        auto it = matIdx.find(mn);
        if (it == matIdx.end()) { it = matIdx.emplace(mn, (int)all.mats.size()).first; all.mats.push_back(d.mesh.mats[0]); }
        SubMesh s; s.indexOffset = (uint32_t)all.indices.size(); s.indexCount = (uint32_t)d.mesh.indices.size();
        s.material = it->second;
        for (uint32_t i : d.mesh.indices) all.indices.push_back(base + i);
        all.subs.push_back(s);
    }
    auto drop = [&](int idx) {
        GpuMesh& g = meshes_[(size_t)idx];
        if (g.vao) DeleteVertexArrays(1, &g.vao);
        if (g.vbo) DeleteBuffers(1, &g.vbo);
        if (g.ibo) DeleteBuffers(1, &g.ibo);
        g = GpuMesh();
    };
    if (all.empty()) { if (rtDecalMesh_ >= 0) drop(rtDecalMesh_); return; }
    const int fresh = upload(all);
    if (fresh < 0) return;
    meshes_[(size_t)fresh].decal = true;
    if (rtDecalMesh_ >= 0 && rtDecalMesh_ != fresh) {   // reuse the slot: the mesh list never grows per spawn
        drop(rtDecalMesh_);
        meshes_[(size_t)rtDecalMesh_] = std::move(meshes_[(size_t)fresh]);
        meshes_.pop_back();
    } else {
        rtDecalMesh_ = fresh;
    }
}

// Beast probe ambient (RE CONFIRMED: volume pick 0x82CCB228, trilinear lookup 0x82FC6DB8, SH -> cube 0x82CBE520):
// enabled volumes containing the point (max |local| <= 1, local = (P - Location) / DrawScale3D); the highest
// Priority wins, equal priorities averaged. Grid g = (N - 1)(local + 1) / 2 per axis (EdgePolicy 0), 8 corners.
// Face = dot(SH_rgb, B(dir)) / |B(dir)|^2 (plain radiance, no cosine convolution), stock UE3 L2 basis.
bool Pipeline::beastAmbient(const core::Vec3& P, core::Vec3 cube[6]) const {
    if (beast_.empty()) return false;
    static const float kB[6][9] = {      // basis at +X, -X, +Y, -Y, +Z, -Z (UE axes)
        {0.282095f, 0, 0, -0.488603f, 0, 0, -0.315392f, 0, 0.546274f},
        {0.282095f, 0, 0, 0.488603f, 0, 0, -0.315392f, 0, 0.546274f},
        {0.282095f, -0.488603f, 0, 0, 0, 0, -0.315392f, 0, -0.546274f},
        {0.282095f, 0.488603f, 0, 0, 0, 0, -0.315392f, 0, -0.546274f},
        {0.282095f, 0, 0.488603f, 0, 0, 0, 0.630784f, 0, 0},
        {0.282095f, 0, -0.488603f, 0, 0, 0, 0.630784f, 0, 0}};
    const float p[3] = {P.x, P.y, P.z};
    int best = -0x7fffffff, count = 0;
    float sh[27] = {};
    for (const BeastVolume& v : beast_) {
        float local[3]; bool in = true;
        for (int a = 0; a < 3; ++a) { local[a] = (p[a] - v.loc[a]) / v.scale[a]; in &= std::fabs(local[a]) <= 1.0f; }
        if (!in || v.priority < best) continue;
        if (v.priority > best) { best = v.priority; count = 0; std::fill(sh, sh + 27, 0.0f); }
        int i0[3], i1[3]; float f[3];
        for (int a = 0; a < 3; ++a) {
            const float g = (float)(v.n[a] - 1) * (local[a] + 1.0f) * 0.5f;
            i0[a] = std::max(0, (int)std::floor(g)); i1[a] = std::min(i0[a] + 1, v.n[a] - 1);
            f[a] = std::min(std::max(g - (float)i0[a], 0.0f), 1.0f);
        }
        for (int c = 0; c < 8; ++c) {
            const int x = (c & 1) ? i1[0] : i0[0], y = (c & 2) ? i1[1] : i0[1], z = (c & 4) ? i1[2] : i0[2];
            const float w = ((c & 1) ? f[0] : 1 - f[0]) * ((c & 2) ? f[1] : 1 - f[1]) * ((c & 4) ? f[2] : 1 - f[2]);
            const float* s = &v.sh[(((size_t)z * v.n[1] + y) * v.n[0] + x) * 27];
            for (int k = 0; k < 27; ++k) sh[k] += w * s[k];
        }
        ++count;
    }
    if (count == 0) return false;
    float ue[6][3];
    for (int fc = 0; fc < 6; ++fc) {
        float nb = 0; for (int k = 0; k < 9; ++k) nb += kB[fc][k] * kB[fc][k];
        for (int ch = 0; ch < 3; ++ch) {
            float d = 0; for (int k = 0; k < 9; ++k) d += sh[ch * 9 + k] * kB[fc][k];
            ue[fc][ch] = d / nb / (float)count;
        }
    }
    const int toGltf[6] = {0, 1, 4, 5, 2, 3};   // UE +X,-X,+Y,-Y,+Z,-Z -> glTF faces (+Y up = UE +Z)
    for (int fc = 0; fc < 6; ++fc) cube[toGltf[fc]] = {ue[fc][0], ue[fc][1], ue[fc][2]};
    return true;
}

const std::string* Pipeline::energyDeathFor(const std::string& material) const {
    if (energyDeath_.empty() || material.empty()) return nullptr;
    std::string pkg = material.substr(0, material.find('.'));
    std::transform(pkg.begin(), pkg.end(), pkg.begin(), ::tolower);
    auto it = energyDeath_.find(pkg);
    return it == energyDeath_.end() ? nullptr : &it->second;
}

int Pipeline::dynamicProgram(const Material* mat) {
    if (mat) {
        auto mm = dynProgMemo_.find(mat);
        if (mm != dynProgMemo_.end()) {
            const DynProgMemo& d = mm->second;
            if (d.tex == mat->tex && d.emissiveTex == mat->emissiveTexHandle && d.color.x == mat->color.x &&
                d.color.y == mat->color.y && d.color.z == mat->color.z && d.wfcName == mat->wfcName &&
                d.sourceName == mat->sourceName && d.baseColorUri == mat->baseColorUri && d.emissiveUri == mat->emissiveUri &&
                d.normalUri == mat->normalUri && d.specularUri == mat->specularUri)
                return d.prog;
        }
    }
    const int prog = dynamicProgramUncached(mat);
    if (mat)
        dynProgMemo_[mat] = DynProgMemo{mat->wfcName, mat->sourceName, mat->baseColorUri, mat->emissiveUri, mat->normalUri,
                                        mat->specularUri, mat->color, mat->tex, mat->emissiveTexHandle, prog};
    return prog;
}

int Pipeline::dynamicProgramUncached(const Material* mat) {
    std::string mk = materialKey(mat);
    auto it = dynProgCache_.find(mk);
    if (it == dynProgCache_.end()) {
        instWanted_ = true;                            // dynamic (character / weapon) programs get the instanced variant
        const int p = programFor(mat ? mat->wfcName : std::string(), mat, false);
        instWanted_ = false;
        it = dynProgCache_.emplace(mk, p).first;
    }
    return it->second;
}

void Pipeline::prewarmDynamic(const MeshData& m) {
    auto t0 = std::chrono::steady_clock::now();
    size_t before = dynProgCache_.size();
    auto lastYield = t0;
    for (const SubMesh& s : m.subs) {
        if (s.material >= 0 && (size_t)s.material < m.mats.size()) {
            const Material& mt = m.mats[(size_t)s.material];
            dynamicProgram(&mt);
            if (const std::string* ed = energyDeathFor(mt.wfcName)) {   // M74: its energy death, with the body
                Material dm; dm.wfcName = *ed;
                dynamicProgram(&dm);
            }
        }
        auto now = std::chrono::steady_clock::now();     // under a loading screen: keep it presenting
        if (std::chrono::duration<double, std::milli>(now - lastYield).count() >= 16.0) { yieldLoad(); lastYield = now; }
    }
    if (m.subs.empty()) dynamicProgram(m.mats.empty() ? nullptr : &m.mats[0]);
    if (dynProgCache_.size() != before)
        LOG_INFO("wfc: prewarmed %zu dynamic material(s) in %.1f ms", dynProgCache_.size() - before,
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
}

void Pipeline::drawHudScreenEffect() {
    static const char* kMat[2] = {"UI_GFxHud_p.StaticDischargeScreenEffect_M", "UI_GFxHud_p.LowHealth.LowHealthScreenEffect_M"};
    if (hudEffect_ < 0 || std::getenv("WFC_NOHUDFX")) return;
    const int base = programFor(kMat[hudEffect_], nullptr, false);
    if (base < 0 || progs_[(size_t)base].screenProg < 0) {
        static bool logged[2] = {false, false};
        if (!logged[hudEffect_]) { logged[hudEffect_] = true; LOG_WARN("wfc: HUD screen effect %s not in the render data", kMat[hudEffect_]); }
        return;
    }
    const Program& P = progs_[(size_t)progs_[(size_t)base].screenProg];
    if (!screenFxVao_) {   // full-screen quad in clip space (identity transforms), UV0 = screen UV
        const float q[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        std::vector<float> v(4 * 14, 0.0f);
        for (int k = 0; k < 4; ++k) {
            float* o = &v[(size_t)k * 14];
            o[0] = q[k][0]; o[1] = q[k][1]; o[2] = 0.0f;
            o[3] = 0; o[4] = 0; o[5] = 1;
            o[6] = 1; o[7] = 0; o[8] = 0; o[9] = 1;
            o[10] = q[k][0] * 0.5f + 0.5f; o[11] = 0.5f - q[k][1] * 0.5f; o[12] = o[10]; o[13] = o[11];
        }
        const uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
        GenVertexArrays(1, &screenFxVao_); GenBuffers(1, &screenFxVbo_); GenBuffers(1, &screenFxIbo_);
        BindVertexArray(screenFxVao_);
        BindBuffer(GL_ARRAY_BUFFER, screenFxVbo_);
        BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, screenFxIbo_);
        BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
        setupAttribs();
        BindVertexArray(0);
    }
    if (!sceneCopyFbo_) return;
    // the finished frame (default framebuffer) -> the scene-colour copy the material's SceneTexture reads
    BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneCopyFbo_);
    BlitFramebuffer(0, 0, vpW_, vpH_, 0, 0, vpW_, vpH_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    BindFramebuffer(GL_FRAMEBUFFER, 0);
    sceneColorCopied_ = true;                         // bindCommon must not re-copy the HDR scene over it
    const core::Mat4 vp = viewProj_;
    viewProj_ = core::Mat4::identity();
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    BindVertexArray(screenFxVao_);
    bindCommon(P, core::Mat4::identity());
    ActiveTexture(GL_TEXTURE0 + 16); glBindTexture(GL_TEXTURE_2D, sceneCopyTex_);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (void*)0);
    BindVertexArray(0);
    viewProj_ = vp;
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
}

// Exact skinned bounds, cheaply: a rigid vertex (one influence, weight exactly 1) skins to M p, so under any palette
// the box over a joint's rigid vertices is set by vertices on the convex hull of that set. Kept: every rigid vertex not
// strictly inside (by a margin far above rounding) the hull of the set's extreme points along 26 fixed directions
// (a subset of the true hull, so dropping its interior is safe), and every other vertex. The box over the kept
// vertices with skinPose's own expression is bitwise the box over all of them.
// Incremental 3D convex hull of a small point set (the extremes of a joint's rigid vertices): outward unit planes.
// Returns false when it cannot be built reliably (degenerate input, or a validation failure: every input point must
// lie on the inner side, within eps, of every plane) - the caller then keeps every vertex.
static bool convexHullPlanes(const std::vector<core::Vec3>& E, float eps, std::vector<std::pair<core::Vec3, float>>& planes) {
    planes.clear();
    const size_t m = E.size();
    if (m < 4) return false;
    // initial tetrahedron: two far points, the farthest from their line, the farthest from that plane
    size_t i0 = 0, i1 = 0;
    for (size_t k = 1; k < m; ++k) if (E[k].x < E[i0].x) i0 = k;
    float best = -1;
    for (size_t k = 0; k < m; ++k) { const float d = core::length(E[k] - E[i0]); if (d > best) { best = d; i1 = k; } }
    if (best <= eps) return false;
    size_t i2 = 0; best = -1;
    for (size_t k = 0; k < m; ++k) {
        const float d = core::length(core::cross(E[k] - E[i0], E[i1] - E[i0]));
        if (d > best) { best = d; i2 = k; }
    }
    if (best <= eps * core::length(E[i1] - E[i0])) return false;
    core::Vec3 n0 = core::normalize(core::cross(E[i1] - E[i0], E[i2] - E[i0]));
    size_t i3 = 0; best = -1;
    for (size_t k = 0; k < m; ++k) { const float d = std::fabs(core::dot(n0, E[k] - E[i0])); if (d > best) { best = d; i3 = k; } }
    if (best <= eps) return false;
    struct Face { size_t a, b, c; core::Vec3 n; float d; bool alive; };
    std::vector<Face> F;
    const core::Vec3 centre = (E[i0] + E[i1] + E[i2] + E[i3]) * 0.25f;
    auto addFace = [&](size_t a, size_t b, size_t c) {
        core::Vec3 n = core::cross(E[b] - E[a], E[c] - E[a]);
        const float len = core::length(n);
        if (len <= 0.0f) return false;
        n = n * (1.0f / len);
        float d = core::dot(n, E[a]);
        if (core::dot(n, centre) - d > 0.0f) { std::swap(b, c); n = n * -1.0f; d = -d; }   // outward
        F.push_back({a, b, c, n, d, true});
        return true;
    };
    if (!addFace(i0, i1, i2) || !addFace(i0, i1, i3) || !addFace(i0, i2, i3) || !addFace(i1, i2, i3)) return false;
    for (size_t q = 0; q < m; ++q) {
        if (q == i0 || q == i1 || q == i2 || q == i3) continue;
        std::vector<size_t> vis;
        for (size_t f = 0; f < F.size(); ++f) if (F[f].alive && core::dot(F[f].n, E[q]) - F[f].d > eps) vis.push_back(f);
        if (vis.empty()) continue;
        // horizon: directed edges of visible faces whose reverse edge is not on a visible face
        std::vector<std::pair<size_t, size_t>> edges;
        for (size_t f : vis) {
            const size_t e[3][2] = {{F[f].a, F[f].b}, {F[f].b, F[f].c}, {F[f].c, F[f].a}};
            for (const auto& ed : e) edges.push_back({ed[0], ed[1]});
        }
        std::vector<std::pair<size_t, size_t>> horizon;
        for (const auto& ed : edges) {
            bool rev = false;
            for (const auto& o : edges) if (o.first == ed.second && o.second == ed.first) { rev = true; break; }
            if (!rev) horizon.push_back(ed);
        }
        for (size_t f : vis) F[f].alive = false;
        for (const auto& h : horizon) {
            core::Vec3 n = core::cross(E[h.second] - E[h.first], E[q] - E[h.first]);
            const float len = core::length(n);
            if (len <= 0.0f) continue;
            n = n * (1.0f / len);
            F.push_back({h.first, h.second, q, n, core::dot(n, E[h.first]), true});
        }
    }
    for (const Face& f : F) if (f.alive) planes.push_back({f.n, f.d});
    for (const auto& pl : planes)                       // validation: no input point outside any plane
        for (const core::Vec3& p : E) if (core::dot(pl.first, p) - pl.second > eps * 4.0f) { planes.clear(); return false; }
    return planes.size() >= 4;
}

void Pipeline::buildSkinBoundsSets(SkinModel& sm, const MeshData& bind, const std::vector<uint16_t>& joints,
                                   const std::vector<float>& weights) {
    const size_t n = bind.vertexCount();
    std::vector<std::vector<uint32_t>> rigid((size_t)sm.joints);
    sm.blended.clear();
    for (size_t i = 0; i < n; ++i) {
        int inf = 0, j = -1; float w = 0.0f;
        for (int k = 0; k < 4; ++k) if (weights[i * 4 + k] > 0.0f) { ++inf; j = joints[i * 4 + k]; w = weights[i * 4 + k]; }
        if (inf == 1 && w == 1.0f && j >= 0 && j < sm.joints) rigid[(size_t)j].push_back((uint32_t)i);
        else if (inf > 0) sm.blended.push_back((uint32_t)i);
    }
    static const core::Vec3 kDirs[26] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
        {1, 1, 0}, {1, -1, 0}, {-1, 1, 0}, {-1, -1, 0}, {1, 0, 1}, {1, 0, -1}, {-1, 0, 1}, {-1, 0, -1},
        {0, 1, 1}, {0, 1, -1}, {0, -1, 1}, {0, -1, -1},
        {1, 1, 1}, {1, 1, -1}, {1, -1, 1}, {1, -1, -1}, {-1, 1, 1}, {-1, 1, -1}, {-1, -1, 1}, {-1, -1, -1}};
    auto P = [&](uint32_t i) { return core::Vec3{bind.positions[i * 3], bind.positions[i * 3 + 1], bind.positions[i * 3 + 2]}; };
    sm.hullPts.assign((size_t)sm.joints, {});
    sm.boundsPts = sm.blended.size();
    for (int j = 0; j < sm.joints; ++j) {
        const std::vector<uint32_t>& R = rigid[(size_t)j];
        std::vector<core::Vec3>& out = sm.hullPts[(size_t)j];
        if (R.size() < 48) { for (uint32_t i : R) out.push_back(P(i)); sm.boundsPts += out.size(); continue; }
        std::vector<core::Vec3> E;
        core::Vec3 lo = P(R[0]), hi = P(R[0]);
        static const std::vector<core::Vec3> kAllDirs = [] {   // the 26 axis / edge / corner directions + 150 Fibonacci
            std::vector<core::Vec3> d(std::begin(kDirs), std::end(kDirs));
            const int N = 150; const float ga = 2.39996323f;
            for (int k = 0; k < N; ++k) {
                const float y = 1.0f - 2.0f * ((float)k + 0.5f) / (float)N, rr = std::sqrt(std::max(0.0f, 1.0f - y * y));
                d.push_back({std::cos(ga * (float)k) * rr, y, std::sin(ga * (float)k) * rr});
            }
            return d;
        }();
        for (const core::Vec3& d : kAllDirs) {
            uint32_t best = R[0]; float bv = -1e30f;
            for (uint32_t i : R) { const float v = core::dot(P(i), d); if (v > bv) { bv = v; best = i; } }
            const core::Vec3 p = P(best);
            bool dup = false;
            for (const core::Vec3& e : E) dup |= (e.x == p.x && e.y == p.y && e.z == p.z);
            if (!dup) E.push_back(p);
        }
        for (uint32_t i : R) { const core::Vec3 p = P(i); lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
                               hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)}; }
        const float ext = std::max(core::length(hi - lo), 1e-6f);
        const float eps = ext * 1e-4f;                  // interior margin (bind units); rounding is ~1e-7 of the extent
        std::vector<std::pair<core::Vec3, float>> planes;   // outward unit normal, offset
        convexHullPlanes(E, eps, planes);                 // empty on failure: every vertex kept
        for (uint32_t i : R) {
            const core::Vec3 p = P(i);
            bool inside = !planes.empty();
            for (size_t k = 0; k < planes.size() && inside; ++k) inside = core::dot(planes[k].first, p) - planes[k].second < -eps;
            if (!inside) out.push_back(p);
        }
        sm.boundsPts += out.size();
    }
    sm.hullSoA.assign((size_t)sm.joints, {});
    for (int j = 0; j < sm.joints; ++j) {
        const std::vector<core::Vec3>& h = sm.hullPts[(size_t)j];
        if (h.empty()) continue;
        const size_t n4 = (h.size() + 3) & ~(size_t)3;       // padded with the last point (min / max unchanged)
        std::vector<float>& v = sm.hullSoA[(size_t)j];
        v.resize(n4 * 3);
        for (size_t k = 0; k < n4; ++k) {
            const core::Vec3& p = h[std::min(k, h.size() - 1)];
            v[k] = p.x; v[n4 + k] = p.y; v[2 * n4 + k] = p.z;
        }
    }
    LOG_INFO("wfc gpu skin: model %s: %zu verts, %d joints; exact bounds over %zu (%zu blended + rigid hull candidates)",
             bind.mats.empty() ? "?" : bind.mats[0].wfcName.c_str(), n, sm.joints, sm.boundsPts, sm.blended.size());
}

bool Pipeline::drawSkinned(const MeshData& bind, const std::vector<uint16_t>& joints, const std::vector<float>& weights,
                           const std::vector<core::Mat4>& palette, const std::vector<core::Mat4>* prevPalette, float alpha,
                           const core::Mat4& model, const void* key, uint64_t serial) {
    const size_t n = bind.vertexCount();
    if (n == 0 || joints.size() != n * 4 || weights.size() != n * 4 || palette.empty() || (int)palette.size() > kMaxBones)
        return false;
    struct SkinTimer { std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
        ~SkinTimer() { gStats.skinMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); ++gStats.skinCalls; } } skinTimer;
    // ---- the model: static bind-pose vertices (raw normals: renormalised after skinning, as skinPose), influences
    SkinModel& sm = skinModels_[&bind];
    sm.lastFrame = frameNo_;
    if (!sm.vao || sm.verts != n || sm.idx != bind.indices.size()) {
        if (!sm.vao) { GenVertexArrays(1, &sm.vao); GenBuffers(1, &sm.vbo); GenBuffers(1, &sm.jwVbo); GenBuffers(1, &sm.ibo); }
        std::vector<float> v;
        buildVertices(bind, v, true);
        std::vector<float> jw(n * 8);
        int maxJ = 0;
        std::vector<core::Vec3> sum(palette.size() > 0 ? (size_t)kMaxBones : 0, core::Vec3{0, 0, 0});
        std::vector<int> cnt((size_t)kMaxBones, 0);
        for (size_t i = 0; i < n; ++i) {
            for (int k = 0; k < 4; ++k) {
                jw[i * 8 + k] = (float)joints[i * 4 + k];
                jw[i * 8 + 4 + k] = weights[i * 4 + k];
                if (weights[i * 4 + k] > 0.0f && joints[i * 4 + k] < kMaxBones) {
                    const int j = joints[i * 4 + k];
                    maxJ = std::max(maxJ, j + 1);
                    sum[(size_t)j] = sum[(size_t)j] + core::Vec3{bind.positions[i * 3], bind.positions[i * 3 + 1], bind.positions[i * 3 + 2]};
                    ++cnt[(size_t)j];
                }
            }
        }
        sm.joints = maxJ;
        sm.jc.assign((size_t)maxJ, core::Vec3{0, 0, 0});
        sm.jr.assign((size_t)maxJ, -1.0f);
        for (int j = 0; j < maxJ; ++j) if (cnt[(size_t)j]) sm.jc[(size_t)j] = sum[(size_t)j] * (1.0f / (float)cnt[(size_t)j]);
        for (size_t i = 0; i < n; ++i)
            for (int k = 0; k < 4; ++k)
                if (weights[i * 4 + k] > 0.0f && joints[i * 4 + k] < maxJ) {
                    const int j = joints[i * 4 + k];
                    const core::Vec3 p{bind.positions[i * 3], bind.positions[i * 3 + 1], bind.positions[i * 3 + 2]};
                    sm.jr[(size_t)j] = std::max(sm.jr[(size_t)j], core::length(p - sm.jc[(size_t)j]));
                }
        BindVertexArray(sm.vao);
        BindBuffer(GL_ARRAY_BUFFER, sm.vbo);
        BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, sm.ibo);
        BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(bind.indices.size() * 4), bind.indices.data(), GL_STATIC_DRAW);
        setupAttribs();
        BindBuffer(GL_ARRAY_BUFFER, sm.jwVbo);
        BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(jw.size() * sizeof(float)), jw.data(), GL_STATIC_DRAW);
        EnableVertexAttribArray(9);  VertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        EnableVertexAttribArray(10); VertexAttribPointer(10, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
        BindVertexArray(0);
        sm.verts = n; sm.idx = bind.indices.size();
        buildSkinBoundsSets(sm, bind, joints, weights);
        ++statSkinRebuilds_;
    }
    // ---- the instance: its palettes in a texture row (uploaded when the serial changes), bounds from the palette
    if (!skinTex_) {
        glGenTextures(1, &skinTex_);
        glBindTexture(GL_TEXTURE_2D, skinTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1024, kSkinRows, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    SkinInst& si = skinInsts_[key];
    si.lastFrame = frameNo_;
    if (si.row < 0) {
        if (!freeSkinRows_.empty()) { si.row = freeSkinRows_.back(); freeSkinRows_.pop_back(); }
        else if (skinRowsUsed_ < kSkinRows) si.row = skinRowsUsed_++;
        else { skinInsts_.erase(key); return false; }
    }
    const bool usePrev = prevPalette && alpha < 1.0f && prevPalette->size() == palette.size();
    // exact bounds (pixel-identical lighting / shadow fit): the skinned positions of assets::skinPose, positions only,
    // once per serial - the same transformPoint / weight sum, so the box is bitwise the CPU path's
    static const bool sphereBounds = std::getenv("WFC_SKINSPHEREBOUNDS") != nullptr;   // A/B: conservative per-joint spheres
    auto fullBounds = [&](const std::vector<core::Mat4>& pal, core::Vec3& mn, core::Vec3& mx) {
        mn = {1e30f, 1e30f, 1e30f}; mx = {-1e30f, -1e30f, -1e30f};
        for (size_t i = 0; i < n; ++i) {
            const core::Vec3 p{bind.positions[i * 3], bind.positions[i * 3 + 1], bind.positions[i * 3 + 2]};
            core::Vec3 sp{0, 0, 0};
            for (int k = 0; k < 4; ++k) {
                const float wt = weights[i * 4 + k];
                if (wt <= 0) continue;
                const uint16_t ji = joints[i * 4 + k];
                if (ji >= pal.size()) continue;
                sp += core::transformPoint(pal[ji], p) * wt;
            }
            mn = {std::min(mn.x, sp.x), std::min(mn.y, sp.y), std::min(mn.z, sp.z)};
            mx = {std::max(mx.x, sp.x), std::max(mx.y, sp.y), std::max(mx.z, sp.z)};
        }
        return std::isfinite(mn.x) && std::isfinite(mx.x) && std::isfinite(mn.y) && std::isfinite(mx.y) &&
               std::isfinite(mn.z) && std::isfinite(mx.z) && mn.x <= mx.x;
    };
    auto exactBounds = [&](const std::vector<core::Mat4>& pal, core::Vec3& mn, core::Vec3& mx) {
        mn = {1e30f, 1e30f, 1e30f}; mx = {-1e30f, -1e30f, -1e30f};
        auto grow = [&](const core::Vec3& sp) {
            mn = {std::min(mn.x, sp.x), std::min(mn.y, sp.y), std::min(mn.z, sp.z)};
            mx = {std::max(mx.x, sp.x), std::max(mx.y, sp.y), std::max(mx.z, sp.z)};
        };
        // rigid hull candidates, 4 at a time: core::transformPoint's exact operation order ((m0 x + m4 y) + m8 z) + m12
        // (SSE2, no fused multiply-add), then 0 + v as skinPose's sum (-0 -> +0): the same bits as the scalar loop
        {
            const __m128 zero = _mm_setzero_ps();
            __m128 lo[3] = {_mm_set1_ps(1e30f), _mm_set1_ps(1e30f), _mm_set1_ps(1e30f)};
            __m128 hi[3] = {_mm_set1_ps(-1e30f), _mm_set1_ps(-1e30f), _mm_set1_ps(-1e30f)};
            for (int j = 0; j < sm.joints && j < (int)pal.size(); ++j) {
                const std::vector<float>& v = sm.hullSoA[(size_t)j];
                if (v.empty()) continue;
                const size_t n4 = v.size() / 3;
                const float* M = pal[(size_t)j].m;
                for (int r3 = 0; r3 < 3; ++r3) {
                    const __m128 a = _mm_set1_ps(M[r3]), b = _mm_set1_ps(M[4 + r3]), c = _mm_set1_ps(M[8 + r3]), d = _mm_set1_ps(M[12 + r3]);
                    for (size_t k = 0; k < n4; k += 4) {
                        const __m128 x = _mm_loadu_ps(&v[k]), y = _mm_loadu_ps(&v[n4 + k]), z = _mm_loadu_ps(&v[2 * n4 + k]);
                        __m128 t = _mm_add_ps(_mm_add_ps(_mm_add_ps(_mm_mul_ps(a, x), _mm_mul_ps(b, y)), _mm_mul_ps(c, z)), d);
                        t = _mm_add_ps(zero, _mm_mul_ps(t, _mm_set1_ps(1.0f)));
                        lo[r3] = _mm_min_ps(t, lo[r3]);
                        hi[r3] = _mm_max_ps(t, hi[r3]);
                    }
                }
            }
            float l[3][4], h[3][4];
            for (int r3 = 0; r3 < 3; ++r3) { _mm_storeu_ps(l[r3], lo[r3]); _mm_storeu_ps(h[r3], hi[r3]); }
            for (int q = 0; q < 4; ++q) {
                mn = {std::min(mn.x, l[0][q]), std::min(mn.y, l[1][q]), std::min(mn.z, l[2][q])};
                mx = {std::max(mx.x, h[0][q]), std::max(mx.y, h[1][q]), std::max(mx.z, h[2][q])};
            }
        }
        for (uint32_t i : sm.blended) {
            const core::Vec3 p{bind.positions[i * 3], bind.positions[i * 3 + 1], bind.positions[i * 3 + 2]};
            core::Vec3 sp{0, 0, 0};
            for (int k = 0; k < 4; ++k) {
                const float wt = weights[i * 4 + k];
                if (wt <= 0) continue;
                const uint16_t ji = joints[i * 4 + k];
                if (ji >= pal.size()) continue;
                sp += core::transformPoint(pal[ji], p) * wt;
            }
            grow(sp);
        }
        const bool ok = std::isfinite(mn.x) && std::isfinite(mx.x) && std::isfinite(mn.y) && std::isfinite(mx.y) &&
                        std::isfinite(mn.z) && std::isfinite(mx.z) && mn.x <= mx.x;
        static const bool check = std::getenv("WFC_SKINBOUNDSCHECK") != nullptr;   // A/B: bitwise against the full loop
        if (check && ok) {
            core::Vec3 fm, fx;
            fullBounds(pal, fm, fx);
            static int bad = 0, good = 0;
            const bool same = fm.x == mn.x && fm.y == mn.y && fm.z == mn.z && fx.x == mx.x && fx.y == mx.y && fx.z == mx.z;
            (same ? good : bad)++;
            if (!same && bad <= 5) LOG_WARN("wfc gpu skin: reduced bounds differ from the full loop (%g %g %g / %g %g %g vs %g %g %g / %g %g %g)",
                                            mn.x, mn.y, mn.z, mx.x, mx.y, mx.z, fm.x, fm.y, fm.z, fx.x, fx.y, fx.z);
            if (((good + bad) & 1023) == 0) LOG_INFO("wfc gpu skin: bounds check %d identical, %d different", good, bad);
        }
        return ok;
    };
    auto boundsOf = [&](const std::vector<core::Mat4>& pal, core::Vec3& mn, core::Vec3& mx) {
        static const bool fullLoop = std::getenv("WFC_SKINFULLBOUNDS") != nullptr;   // A/B: every vertex
        if (!sphereBounds) return fullLoop ? fullBounds(pal, mn, mx) : exactBounds(pal, mn, mx);
        mn = {1e30f, 1e30f, 1e30f}; mx = {-1e30f, -1e30f, -1e30f};
        for (int j = 0; j < sm.joints && j < (int)pal.size(); ++j) {
            if (sm.jr[(size_t)j] < 0.0f) continue;
            const core::Mat4& M = pal[(size_t)j];
            const core::Vec3 c = core::transformPoint(M, sm.jc[(size_t)j]);
            const float sc = std::max(core::length(core::Vec3{M.m[0], M.m[1], M.m[2]}),
                             std::max(core::length(core::Vec3{M.m[4], M.m[5], M.m[6]}), core::length(core::Vec3{M.m[8], M.m[9], M.m[10]})));
            const float rr = sm.jr[(size_t)j] * sc;
            mn = {std::min(mn.x, c.x - rr), std::min(mn.y, c.y - rr), std::min(mn.z, c.z - rr)};
            mx = {std::max(mx.x, c.x + rr), std::max(mx.y, c.y + rr), std::max(mx.z, c.z + rr)};
        }
        return mn.x <= mx.x && std::isfinite(mn.x) && std::isfinite(mx.x) && std::isfinite(mn.y) && std::isfinite(mx.y) &&
               std::isfinite(mn.z) && std::isfinite(mx.z);
    };
    const bool skipPrep = pawnOccPrepOn() && si.serial != 0 && si.mx.x >= si.mn.x && pawnOcc_.count(drawOwner_) &&
                          pawnOcc_[drawOwner_].occluded;
    if (skipPrep) {                                     // hidden body + shadow: light env tick + query box only
        SkinDraw d0;
        d0.vao = sm.vao; d0.mn = si.mn; d0.mx = si.mx;   // the last exact bounds (pose of the last prepared frame)
        skinDraw_ = &d0;
        skinPrepSkipped_ = true;
        ++statOccPrepSkipped_;
        drawDynamic(bind, model);
        skinPrepSkipped_ = false;
        skinDraw_ = nullptr;
        return true;
    }
    if (si.serial != serial || si.prev != usePrev) {
        const auto tb = std::chrono::steady_clock::now();
        struct BTimer { std::chrono::steady_clock::time_point t; ~BTimer() { gStats.skinBoundsMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); ++gStats.skinUploads; } } bt{tb};
        for (const core::Mat4& M : palette) if (!finiteMat(M)) { reportNonFinite("bone palette", bind.mats.empty() ? std::string("?") : bind.mats[0].wfcName); return true; }
        if (!boundsOf(palette, si.mn, si.mx)) return true;
        if (usePrev && !boundsOf(*prevPalette, si.pmn, si.pmx)) return true;
        static std::vector<float> row;
        row.assign(1024 * 4, 0.0f);
        for (size_t j = 0; j < palette.size(); ++j) std::memcpy(&row[j * 16], palette[j].m, 16 * sizeof(float));
        if (usePrev) for (size_t j = 0; j < prevPalette->size(); ++j) std::memcpy(&row[(512 + j * 4) * 4], (*prevPalette)[j].m, 16 * sizeof(float));
        glBindTexture(GL_TEXTURE_2D, skinTex_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, si.row, 1024, 1, GL_RGBA, GL_FLOAT, row.data());
        si.serial = serial; si.prev = usePrev;
    }
    SkinDraw d;
    d.vao = sm.vao;
    d.mn = si.mn; d.mx = si.mx;
    if (usePrev) { d.mn = si.pmn + (si.mn - si.pmn) * alpha; d.mx = si.pmx + (si.mx - si.pmx) * alpha; }
    ActiveTexture(GL_TEXTURE0 + 18); glBindTexture(GL_TEXTURE_2D, skinTex_); ActiveTexture(GL_TEXTURE0);
    skinDraw_ = &d;
    skinMode_ = usePrev ? 2 : 1; skinRow_ = si.row; skinBones_ = (int)palette.size(); skinAlpha_ = usePrev ? alpha : 1.0f;
    drawDynamic(bind, model);
    skinDraw_ = nullptr;
    skinMode_ = 0;
    return true;
}

bool Pipeline::queueInstance(const Program& P, uint32_t first, uint32_t count, GLuint vao) {
    if (instCursor_ >= kInstRows || inInstFlush_) return false;
    const Program& I = progs_[(size_t)P.instProg];
    float row[kInstW * 4] = {};
    auto get = [&](GLint loc, float* dst, unsigned words, bool isInt) {
        if (loc < 0) return true;                                  // not used by this program: stays 0
        uint32_t tmp[24];
        const int g = glx::uniformCacheGet(P.id, loc, tmp, words);
        if (g < 0) return false;
        if (g == 0) { for (unsigned k = 0; k < words; ++k) dst[k] = 0.0f; return true; }   // GL default
        for (unsigned k = 0; k < words; ++k) {
            if (isInt) { int32_t v; std::memcpy(&v, &tmp[k], 4); dst[k] = (float)v; }
            else std::memcpy(&dst[k], &tmp[k], 4);
        }
        return true;
    };
    float amb[18], lpos[12], ldir[12], lcol[12], lspot[12], dlac[3], dyn[4], nl[1], sk[4];
    if (!get(P.uModel, &row[0], 16, false) || !get(P.uAmb, amb, 18, false) || !get(P.uNumLights, nl, 1, true) ||
        !get(P.uLPos, lpos, 12, false) || !get(P.uLDir, ldir, 12, false) || !get(P.uLCol, lcol, 12, false) ||
        !get(P.uLSpot, lspot, 12, false) || !get(uloc(P, "uDLAC"), dlac, 3, false) || !get(uloc(P, "uDynParam"), dyn, 4, false) ||
        !get(uloc(P, "uSkin"), &sk[0], 1, true) || !get(uloc(P, "uSkinRow"), &sk[1], 1, true) ||
        !get(uloc(P, "uSkinAlpha"), &sk[2], 1, false) || !get(uloc(P, "uSkinBones"), &sk[3], 1, true))
        return false;
    std::memcpy(&row[4 * 4], sk, sizeof sk);
    for (int i = 0; i < 6; ++i) { row[(5 + i) * 4] = amb[i * 3]; row[(5 + i) * 4 + 1] = amb[i * 3 + 1]; row[(5 + i) * 4 + 2] = amb[i * 3 + 2]; }
    row[11 * 4] = nl[0]; row[11 * 4 + 1] = dlac[0]; row[11 * 4 + 2] = dlac[1]; row[11 * 4 + 3] = dlac[2];
    std::memcpy(&row[12 * 4], lpos, sizeof lpos); std::memcpy(&row[15 * 4], ldir, sizeof ldir);
    std::memcpy(&row[18 * 4], lcol, sizeof lcol); std::memcpy(&row[21 * 4], lspot, sizeof lspot);
    std::memcpy(&row[24 * 4], dyn, sizeof dyn);
    int k = 0;
    for (const auto& kv : P.rtLoc) {                               // std::map: sorted by name, as the shader's layout
        if (k >= I.instRtCount) return false;
        float set1[1];
        if (!get(kv.second.first, &row[(25 + 2 * k) * 4], 4, false) || !get(kv.second.second, set1, 1, true)) return false;
        row[(26 + 2 * k) * 4] = set1[0];
        ++k;
    }
    if (k != I.instRtCount) return false;
    const auto key = std::make_tuple(P.instProg, vao, first, count);
    auto gi = instGroupIndex_.find(key);
    if (gi == instGroupIndex_.end()) {
        InstGroup gr;
        gr.instProg = P.instProg; gr.vao = vao; gr.first = first; gr.count = count;
        float vp[16], cp[3];
        if (!get(P.uViewProj, vp, 16, false) || !get(P.uCamPos, cp, 3, false)) return false;
        std::memcpy(gr.viewProj, vp, sizeof vp); std::memcpy(gr.camPos, cp, sizeof cp);
        glGetIntegerv(GL_DEPTH_FUNC, &gr.depthFunc);
        gi = instGroupIndex_.emplace(key, instGroups_.size()).first;
        instGroups_.push_back(std::move(gr));
    }
    InstGroup& gr = instGroups_[gi->second];
    ++gStats.instQueued;
    gr.rows.insert(gr.rows.end(), row, row + kInstW * 4);
    ++gr.n;
    ++instCursor_;
    return true;
}

void Pipeline::flushInstances() {
    if (instGroups_.empty() || inInstFlush_) return;
    inInstFlush_ = true;
    if (!instTex_) {
        glGenTextures(1, &instTex_);
        glBindTexture(GL_TEXTURE_2D, instTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kInstW, kInstRows, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    // the caller's state (a flush can come from inside any pass)
    GLint fb = 0, prog = 0, vao = 0, vp[4], active = 0, df = 0, bsrc = 0, bdst = 0, bsa = 0, bda = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb); glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glGetIntegerv(0x85B5 /*GL_VERTEX_ARRAY_BINDING*/, &vao); glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(0x84E0 /*GL_ACTIVE_TEXTURE*/, &active); glGetIntegerv(GL_DEPTH_FUNC, &df);
    glGetIntegerv(0x80C9 /*GL_BLEND_SRC_RGB*/, &bsrc); glGetIntegerv(0x80C8 /*GL_BLEND_DST_RGB*/, &bdst);
    glGetIntegerv(0x80CB /*GL_BLEND_SRC_ALPHA*/, &bsa); glGetIntegerv(0x80CA /*GL_BLEND_DST_ALPHA*/, &bda);
    GLboolean dmask = GL_TRUE, cmask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    glGetBooleanv(GL_DEPTH_WRITEMASK, &dmask); glGetBooleanv(GL_COLOR_WRITEMASK, cmask);
    const GLboolean blendOn = glIsEnabled(GL_BLEND), depthOn = glIsEnabled(GL_DEPTH_TEST), cullOn = glIsEnabled(GL_CULL_FACE),
                    stencilOn = glIsEnabled(GL_STENCIL_TEST), polyOn = glIsEnabled(GL_POLYGON_OFFSET_FILL);
    GLint tex2d[20], texCube[20];
    for (int u = 0; u < 20; ++u) {
        ActiveTexture(GL_TEXTURE0 + u);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex2d[u]); glGetIntegerv(0x8514 /*GL_TEXTURE_BINDING_CUBE_MAP*/, &texCube[u]);
    }
    // rows of this flush, uploaded once
    int total = 0;
    for (const InstGroup& gr : instGroups_) total += gr.n;
    const int base0 = instCursor_ - total;
    {
        std::vector<float> all;
        all.reserve((size_t)total * kInstW * 4);
        for (const InstGroup& gr : instGroups_) all.insert(all.end(), gr.rows.begin(), gr.rows.end());
        glBindTexture(GL_TEXTURE_2D, instTex_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, base0, kInstW, total, GL_RGBA, GL_FLOAT, all.data());
    }
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, vpW_, vpH_);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_STENCIL_TEST); glDisable(GL_POLYGON_OFFSET_FILL); glEnable(GL_DEPTH_TEST);
    const bool keepMask = dynamicMaskDraw_, keepDyn = inDynamicDraw_;
    dynamicMaskDraw_ = true; inDynamicDraw_ = false;
    int skinModeKeep = skinMode_; skinMode_ = 0;               // per instance (row); bindCommon must not set uSkin
    int base = base0;
    for (const InstGroup& gr : instGroups_) {
        const Program& I = progs_[(size_t)gr.instProg];
        BindVertexArray(gr.vao);
        bindCommon(I, core::Mat4::identity());
        UniformMatrix4fv(I.uViewProj, 1, GL_FALSE, gr.viewProj);
        Uniform3f(I.uCamPos, gr.camPos[0], gr.camPos[1], gr.camPos[2]);
        Uniform1i(uloc(I, "uInstBase"), base);
        Uniform4f(I.uLMCoord, 1, 1, 0, 0);                     // drawSubs' lit dynamic value
        Uniform1i(uloc(I, "uDecalClip"), 0);
        ActiveTexture(GL_TEXTURE0 + 19); glBindTexture(GL_TEXTURE_2D, instTex_);
        ActiveTexture(GL_TEXTURE0 + 18); glBindTexture(GL_TEXTURE_2D, skinTex_);
        static const bool noCull = std::getenv("WFC_NOCULL") != nullptr;
        if (I.twoSided || noCull) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND); glDepthMask(GL_TRUE);              // opaque / masked only
        glDepthFunc((GLenum)gr.depthFunc);
        DrawElementsInstanced(GL_TRIANGLES, (GLsizei)gr.count, GL_UNSIGNED_INT, (void*)(size_t)(gr.first * 4), gr.n);
        ++gStats.instDraws;
        base += gr.n;
    }
    skinMode_ = skinModeKeep;
    dynamicMaskDraw_ = keepMask; inDynamicDraw_ = keepDyn;
    instGroups_.clear(); instGroupIndex_.clear();
    // restore
    for (int u = 0; u < 20; ++u) {
        ActiveTexture(GL_TEXTURE0 + u);
        glBindTexture(GL_TEXTURE_2D, (GLuint)tex2d[u]); glBindTexture(GL_TEXTURE_CUBE_MAP, (GLuint)texCube[u]);
    }
    ActiveTexture((GLenum)active);
    BindFramebuffer(GL_FRAMEBUFFER, (GLuint)fb);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    UseProgram((GLuint)prog);
    BindVertexArray((GLuint)vao);
    glDepthFunc((GLenum)df); glDepthMask(dmask); glColorMask(cmask[0], cmask[1], cmask[2], cmask[3]);
    BlendFuncSeparate((GLenum)bsrc, (GLenum)bdst, (GLenum)bsa, (GLenum)bda);
    if (blendOn) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (depthOn) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cullOn) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (stencilOn) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    if (polyOn) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL);
    inInstFlush_ = false;
}

void Pipeline::buildMdi(int meshId) {
    static const bool on = [] { const char* e = std::getenv("WFC_MDI"); return !(e && e[0] == '0') && std::getenv("WFC_GL33") == nullptr; }();   // default on; WFC_MDI=0 off
    if (!on || mdiMesh_ >= 0 || !MultiDrawElementsIndirect || !VertexAttribDivisor) return;
    GpuMesh& g = meshes_[(size_t)meshId];
    if (!g.world) return;
    std::map<std::tuple<int, int, int, int>, size_t> idx;
    mdiRows_.clear(); mdiEnvFilled_.clear(); mdiBuckets_.clear();
    buildLmArray();
    size_t arrayed = 0;
    uint32_t row = 0;
    for (size_t si = 0; si < g.subs.size(); ++si) {
        Sub& s = g.subs[si];
        s.mdiRow = -1;
        if (s.prog < 0) continue;
        const Program& P = progs_[(size_t)s.prog];
        if (P.mdiProg < 0 || P.blend >= 2 || !s.actor.empty() || s.vlmTex || s.dynChannel || g.decal) continue;
        s.mdiRow = (int)row++;
        mdiRows_.resize((size_t)row * kMdiW * 4, 0.0f);
        float* rw = &mdiRows_[(size_t)s.mdiRow * kMdiW * 4];
        if (s.lmTex[0] >= 0) {
            std::memcpy(rw, s.lmCoord, sizeof s.lmCoord);
            for (int i = 0; i < 3; ++i) for (int c = 0; c < 3; ++c) rw[(1 + i) * 4 + c] = s.lmScale[i][c];
        } else { rw[0] = 1; rw[1] = 1; rw[2] = 0; rw[3] = 0; }
        // all three pages in the shared array, and the MDI program samples it: bucket by program alone
        bool inArray = s.lmTex[0] >= 0 && lmArray_ && uloc(progs_[(size_t)P.mdiProg], "uLMUseArr") >= 0;
        for (int i = 0; i < 3 && inArray; ++i)
            inArray = (size_t)s.lmTex[i] < lmLayer_.size() && lmLayer_[(size_t)s.lmTex[i]] >= 0;
        for (int i = 0; i < 3; ++i) rw[(1 + i) * 4 + 3] = inArray ? (float)lmLayer_[(size_t)s.lmTex[i]] : -1.0f;
        arrayed += inArray ? 1 : 0;
        // light environments (lit, non-lightmapped): filled the first time the sub is drawn, as drawSubs does
        mdiEnvFilled_.push_back(P.lit && s.lmTex[0] < 0 ? 0 : 1);
        const int k0 = inArray ? -2 : s.lmTex[0], k1 = inArray ? -2 : s.lmTex[1], k2 = inArray ? -2 : s.lmTex[2];
        const auto key = std::make_tuple(s.prog, k0, k1, k2);
        auto it = idx.find(key);
        if (it == idx.end()) { it = idx.emplace(key, mdiBuckets_.size()).first; mdiBuckets_.push_back({s.prog, {k0, k1, k2}, {}}); }
        mdiBuckets_[it->second].subs.push_back((uint32_t)si);
    }
    if (row == 0) return;
    glGenTextures(1, &mdiRowTex_);
    glBindTexture(GL_TEXTURE_2D, mdiRowTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kMdiW, (GLsizei)row, 0, GL_RGBA, GL_FLOAT, mdiRows_.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    std::vector<float> ids(row);
    for (uint32_t k = 0; k < row; ++k) ids[k] = (float)k;
    GenBuffers(1, &mdiRowVbo_); GenBuffers(1, &mdiCmdBuf_);
    BindVertexArray(g.vao);
    BindBuffer(GL_ARRAY_BUFFER, mdiRowVbo_);
    BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(ids.size() * sizeof(float)), ids.data(), GL_STATIC_DRAW);
    EnableVertexAttribArray(11); VertexAttribPointer(11, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    VertexAttribDivisor(11, 1);
    BindVertexArray(0);
    mdiMesh_ = meshId;
    LOG_INFO("wfc: world MDI: %u subs in %zu buckets (program + lightmap page; %zu subs via the lightmap array)", row,
             mdiBuckets_.size(), arrayed);
}

void Pipeline::buildLmArray() {
    lmLayer_.assign(lmTextures_.size(), -1);
    if (lmArray_ || !TexStorage3D || !CopyImageSubData || !TextureView || std::getenv("WFC_NOLMARRAY")) return;
    constexpr GLenum kArr = 0x8C1A;                            // GL_TEXTURE_2D_ARRAY
    constexpr GLint kSize = 256, kLevels = 9;                  // the common page size; full mip chain (GenerateMipmap)
    GLint refFmt = 0;
    int layers = 0;
    for (size_t k = 0; k < lmTextures_.size(); ++k) {
        if (!lmTextures_[k]) continue;
        glBindTexture(GL_TEXTURE_2D, lmTextures_[k]);
        GLint w = 0, h = 0, fmt = 0, comp = 0, w8 = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, 0x1003 /*GL_TEXTURE_INTERNAL_FORMAT*/, &fmt);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, 0x86A1 /*GL_TEXTURE_COMPRESSED*/, &comp);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, kLevels - 1, GL_TEXTURE_WIDTH, &w8);   // mip chain present
        if (w != kSize || h != kSize || comp || w8 != 1) continue;
        if (!refFmt) refFmt = fmt;
        if (fmt == refFmt) lmLayer_[k] = layers++;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    if (layers < 2) { lmLayer_.assign(lmTextures_.size(), -1); return; }
    glGenTextures(1, &lmArray_);
    glBindTexture(kArr, lmArray_);
    TexStorage3D(kArr, kLevels, (GLenum)refFmt, kSize, kSize, layers);
    // the pages' sampler state (texture(): trilinear, clamp, 8x anisotropy)
    auto params = [](GLenum target) {
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
    };
    params(kArr);
    glBindTexture(kArr, 0);
    std::map<GLuint, GLuint> viewOf;
    for (size_t k = 0; k < lmTextures_.size(); ++k) {
        if (lmLayer_[k] < 0) continue;
        for (GLint l = 0; l < kLevels; ++l)                    // exact texel copy of every level (no re-filtering)
            CopyImageSubData(lmTextures_[k], GL_TEXTURE_2D, l, 0, 0, 0, lmArray_, kArr, l, 0, 0, lmLayer_[k],
                             kSize >> l, kSize >> l, 1);
        GLuint v = 0;
        glGenTextures(1, &v);
        TextureView(v, GL_TEXTURE_2D, lmArray_, (GLenum)refFmt, 0, kLevels, (GLuint)lmLayer_[k], 1);
        glBindTexture(GL_TEXTURE_2D, v);
        params(GL_TEXTURE_2D);
        viewOf[lmTextures_[k]] = v;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    // every holder of an old page name now holds its view; then the old storage goes
    for (auto& kv : texCache_) { auto it = viewOf.find(kv.second); if (it != viewOf.end()) kv.second = it->second; }
    for (auto& t : touchQueue_) { auto it = viewOf.find(t.first); if (it != viewOf.end()) t.first = it->second; }
    for (const auto& kv : viewOf) {
        for (GLuint& t : lmTextures_) if (t == kv.first) t = kv.second;
        glDeleteTextures(1, &kv.first);
    }
    LOG_INFO("wfc: lightmap array: %d of %zu pages (%dx%d, format 0x%x) as views of one texture array", layers,
             lmTextures_.size(), kSize, kSize, refFmt);
}

void Pipeline::drawMdi(GpuMesh& g) {
    flushInstances();
    struct Cmd { uint32_t count, instances, first, baseVertex, baseInstance; };
    static std::vector<Cmd> cmds;
    static std::vector<std::pair<size_t, size_t>> ranges;     // per bucket: [begin, end) in cmds
    cmds.clear(); ranges.clear();
    bool rowsDirty = false;
    for (const MdiBucket& b : mdiBuckets_) {
        const size_t begin = cmds.size();
        for (uint32_t si : b.subs) {
            Sub& s = g.subs[si];
            bool out = false;                                  // drawSubs' baked-world frustum cull
            for (int f = 0; f < 6 && !out; ++f) {
                const float* pl = frustum_[f];
                core::Vec3 pv{pl[0] >= 0 ? s.bmax.x : s.bmin.x, pl[1] >= 0 ? s.bmax.y : s.bmin.y, pl[2] >= 0 ? s.bmax.z : s.bmin.z};
                if (pl[0] * pv.x + pl[1] * pv.y + pl[2] * pv.z + pl[3] < 0) out = true;
            }
            if (out) { ++counts_.culled; continue; }
            if (!mdiEnvFilled_[(size_t)s.mdiRow]) {              // the static light environment, first sight
                if (!s.envReady) {
                    if (s.noLights) s.env = LightEnv{};
                    else computeEnv((s.bmin + s.bmax) * 0.5f, false, s.env);
                    s.envReady = true;
                }
                float* rw = &mdiRows_[(size_t)s.mdiRow * kMdiW * 4];
                for (int i = 0; i < 6; ++i) { rw[(4 + i) * 4] = s.env.cube[i].x; rw[(4 + i) * 4 + 1] = s.env.cube[i].y; rw[(4 + i) * 4 + 2] = s.env.cube[i].z; }
                rw[10 * 4] = (float)s.env.n; rw[10 * 4 + 1] = s.env.dlac[0]; rw[10 * 4 + 2] = s.env.dlac[1]; rw[10 * 4 + 3] = s.env.dlac[2];
                std::memcpy(&rw[11 * 4], s.env.pos, sizeof s.env.pos); std::memcpy(&rw[14 * 4], s.env.dir, sizeof s.env.dir);
                std::memcpy(&rw[17 * 4], s.env.col, sizeof s.env.col); std::memcpy(&rw[20 * 4], s.env.spot, sizeof s.env.spot);
                mdiEnvFilled_[(size_t)s.mdiRow] = 1;
                rowsDirty = true;
            }
            cmds.push_back({s.count, 1u, s.first, 0u, (uint32_t)s.mdiRow});
            ++gStats.draws; ++counts_.draws; ++counts_.worldDraws; ++counts_.opaque;
            if (s.lmTex[0] >= 0) ++counts_.lightmapped;
            if (s.matKey < 0) {
                auto mk = matKeys_.emplace(s.matName, (int)matKeys_.size());
                s.matKey = mk.first->second;
                if ((size_t)s.matKey >= matSeenFrame_.size()) matSeenFrame_.resize((size_t)s.matKey + 1, -1);
            }
            if (matSeenFrame_[(size_t)s.matKey] != frameNo_) { matSeenFrame_[(size_t)s.matKey] = frameNo_; ++frameMatCount_; }
        }
        ranges.push_back({begin, cmds.size()});
    }
    if (rowsDirty) {
        glBindTexture(GL_TEXTURE_2D, mdiRowTex_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kMdiW, (GLsizei)(mdiRows_.size() / (kMdiW * 4)), GL_RGBA, GL_FLOAT, mdiRows_.data());
    }
    if (cmds.empty()) return;
    BindBuffer(0x8F3F /*GL_DRAW_INDIRECT_BUFFER*/, mdiCmdBuf_);
    BufferData(0x8F3F, (GLsizeiptr)(cmds.size() * sizeof(Cmd)), cmds.data(), GL_STREAM_DRAW);
    BindVertexArray(g.vao);
    // WFC_GPUBUCKETS=1 (diagnostics): every 240th frame, GPU timestamps around each bucket (read back at once - that
    // frame stalls, the others are untouched); the costliest buckets are logged with material and draw count
    static const bool gpuBuckets = std::getenv("WFC_GPUBUCKETS") != nullptr && QueryCounter && GetQueryObjectui64v;
    const bool profFrame = gpuBuckets && frameNo_ % 240 == 0;
    static std::vector<GLuint> bq;
    std::vector<std::pair<size_t, size_t>> profMarks;     // (bucket, query index)
    if (profFrame && bq.size() < mdiBuckets_.size() + 1) {
        const size_t old = bq.size();
        bq.resize(mdiBuckets_.size() + 1);
        GenQueries((GLsizei)(bq.size() - old), &bq[old]);
    }
    size_t qn = 0;
    if (profFrame) QueryCounter(bq[qn++], 0x8E28);
    for (size_t bi = 0; bi < mdiBuckets_.size(); ++bi) {
        const size_t n = ranges[bi].second - ranges[bi].first;
        if (!n) continue;
        if (profFrame) profMarks.push_back({bi, qn});
        const MdiBucket& b = mdiBuckets_[bi];
        const Program& M = progs_[(size_t)progs_[(size_t)b.prog].mdiProg];
        bindCommon(M, core::Mat4::identity());
        Uniform1i(uloc(M, "uDecalClip"), 0);
        for (int i = 0; i < 3; ++i) if (M.uRTSet[i] >= 0) Uniform1i(M.uRTSet[i], 0);   // static: no character colours
        static const bool noCull = std::getenv("WFC_NOCULL") != nullptr;
        if (M.twoSided || noCull) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND); glDepthMask(GL_TRUE);
        Uniform1i(uloc(M, "uLMUseArr"), b.lm[0] == -2 ? 1 : 0);
        if (b.lm[0] >= 0) {
            Uniform4f(M.uLMCoord, 1, 1, 0, 0);                 // (the VS reads the row; kept for stray readers)
            for (int i = 0; i < 3; ++i) {
                ActiveTexture(GL_TEXTURE0 + 13 + i);
                glBindTexture(GL_TEXTURE_2D, lmTextures_[(size_t)b.lm[i]] ? lmTextures_[(size_t)b.lm[i]] : blackTex_);
            }
        }
        if (lmArray_) { ActiveTexture(GL_TEXTURE0 + 21); glBindTexture(0x8C1A /*GL_TEXTURE_2D_ARRAY*/, lmArray_); }
        ActiveTexture(GL_TEXTURE0 + 20); glBindTexture(GL_TEXTURE_2D, mdiRowTex_);
        ActiveTexture(GL_TEXTURE0);
        MultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, (const void*)(ranges[bi].first * sizeof(Cmd)), (GLsizei)n, 0);
        depthDirty_ = true;
        if (profFrame) QueryCounter(bq[qn++], 0x8E28);
    }
    if (profFrame && qn > 1) {
        std::vector<unsigned long long> t(qn);
        for (size_t k = 0; k < qn; ++k) GetQueryObjectui64v(bq[k], 0x8866 /*GL_QUERY_RESULT*/, &t[k]);
        std::vector<std::tuple<double, size_t, size_t>> rows;   // (ms, bucket, draws)
        for (const auto& pm : profMarks) {
            const size_t bi = pm.first;
            rows.push_back({(double)(t[pm.second] - t[pm.second - 1]) / 1.0e6, bi, ranges[bi].second - ranges[bi].first});
        }
        std::sort(rows.rbegin(), rows.rend());
        std::string top;
        for (size_t k = 0; k < rows.size() && k < 15; ++k) {
            const MdiBucket& b = mdiBuckets_[std::get<1>(rows[k])];
            const std::string& m = progs_[(size_t)b.prog].material;
            char buf[200];
            std::snprintf(buf, sizeof buf, "\n    %.3f ms  %3zu draws  %s%s", std::get<0>(rows[k]), std::get<2>(rows[k]),
                          m.substr(m.size() > 70 ? m.size() - 70 : 0).c_str(), b.lm[0] == -1 ? "" : " (lightmapped)");
            top += buf;
        }
        LOG_INFO("GPUBUCKETS frame %d: world MDI %.3f ms GPU over %zu buckets; top:%s", frameNo_,
                 (double)(t[qn - 1] - t[0]) / 1.0e6, profMarks.size(), top.c_str());
    }
    BindBuffer(0x8F3F, 0);
    BindVertexArray(g.vao);                                    // drawSubs continues with this VAO
}

void Pipeline::evictSkin(bool all) {
    for (auto it = skinInsts_.begin(); it != skinInsts_.end();) {
        if (all || frameNo_ - it->second.lastFrame > 600) {
            if (it->second.row >= 0 && !all) freeSkinRows_.push_back(it->second.row);
            it = skinInsts_.erase(it);
        } else ++it;
    }
    for (auto it = skinModels_.begin(); it != skinModels_.end();) {
        // models (static buffers + bounds sets per bind mesh) are kept ~60 s at 300 fps: a body that is dead for its
        // respawn delay must not be rebuilt (vertex build + upload + per-joint hulls) when it spawns again
        if (all || frameNo_ - it->second.lastFrame > 18000) {
            if (it->second.vao) DeleteVertexArrays(1, &it->second.vao);
            for (GLuint* b : {&it->second.vbo, &it->second.jwVbo, &it->second.ibo}) if (*b) DeleteBuffers(1, b);
            it = skinModels_.erase(it);
        } else ++it;
    }
    if (all) {
        freeSkinRows_.clear(); skinRowsUsed_ = 0;
        if (skinTex_) { glDeleteTextures(1, &skinTex_); skinTex_ = 0; }
    }
}

void Pipeline::evictPosed(bool all) {
    for (auto it = posed_.begin(); it != posed_.end();) {
        if (all || frameNo_ - it->second.lastFrame > 600) {
            if (it->second.vao) DeleteVertexArrays(1, &it->second.vao);
            if (it->second.vbo) DeleteBuffers(1, &it->second.vbo);
            if (it->second.ibo) DeleteBuffers(1, &it->second.ibo);
            if (it->second.prevVbo) DeleteBuffers(1, &it->second.prevVbo);
            it = posed_.erase(it);
        } else ++it;
    }
}

bool Pipeline::pawnOccPrepOn() {
    static const bool on = std::getenv("WFC_NOPAWNOCCPREP") == nullptr;   // WFC_NOPAWNOCCPREP=1: culling skips draws only
    return on && pawnOcclusionOn();
}

bool Pipeline::pawnOcclusionOn() {
    // default on with the prep skip (pawnOccPrepOn): seeded lockstep 64-player A/B, verified Streets cam, p50 5.31 ->
    // 4.74 ms (draw-only culling alone measured no gain). WFC_NOPAWNOCCLUSION=1 (or WFC_PAWNOCCLUSION=0) = reference.
    static const bool on = [] { const char* e = std::getenv("WFC_PAWNOCCLUSION");
                                return std::getenv("WFC_NOPAWNOCCLUSION") == nullptr && !(e && e[0] == '0'); }();
    return on && BeginQuery && EndQuery && GenQueries && GetQueryObjectiv;
}

void Pipeline::pawnOcclusionResults() {
    for (auto it = pawnOcc_.begin(); it != pawnOcc_.end();) {
        PawnOcc& po = it->second;
        if (frameNo_ - po.lastSeen > 600) {               // owner gone: free its queries
            for (GLuint& q : po.q) if (q) { DeleteQueries(1, &q); q = 0; }
            it = pawnOcc_.erase(it);
            continue;
        }
        auto hidden = [&](int f) {                         // the query of frame f found no sample (and is ready)
            if (f < 0) return false;
            const int slot = f % 4;
            if (po.qFrame[slot] != f) return false;
            GLint avail = 0;
            GetQueryObjectiv(po.q[slot], 0x8867 /*GL_QUERY_RESULT_AVAILABLE*/, &avail);
            if (!avail) return false;
            GLint any = 1;
            GetQueryObjectiv(po.q[slot], 0x8866 /*GL_QUERY_RESULT*/, &any);
            return any == 0;
        };
        po.occluded = po.boxFrame >= frameNo_ - 2 && hidden(frameNo_ - 2) && hidden(frameNo_ - 3);
        ++it;
    }
}

void Pipeline::pawnOcclusionQueries() {
    if (!occProg_) {
        const char* vs = "#version 330 core\nlayout(location=0) in vec3 aPos; uniform mat4 uVP; uniform vec3 uMin; uniform vec3 uMax;\n"
                         "void main(){ gl_Position = uVP * vec4(mix(uMin, uMax, aPos), 1.0); }\n";
        const char* fs = "#version 330 core\nout vec4 oColor; void main(){ oColor = vec4(0.0); }\n";
        GLuint v = compile(GL_VERTEX_SHADER, vs, "pawnocc.vs"), f = compile(GL_FRAGMENT_SHADER, fs, "pawnocc.fs");
        if (v && f) occProg_ = link(v, f, "pawnocc");
        if (!occProg_) return;
        occUVP_ = GetUniformLocation(occProg_, "uVP"); occUMin_ = GetUniformLocation(occProg_, "uMin"); occUMax_ = GetUniformLocation(occProg_, "uMax");
        const float cube[8][3] = {{0,0,0},{1,0,0},{0,1,0},{1,1,0},{0,0,1},{1,0,1},{0,1,1},{1,1,1}};
        const uint32_t idx[36] = {0,1,3, 0,3,2, 4,6,7, 4,7,5, 0,4,5, 0,5,1, 2,3,7, 2,7,6, 0,2,6, 0,6,4, 1,5,7, 1,7,3};
        GenVertexArrays(1, &occVao_); GenBuffers(1, &occVbo_); GenBuffers(1, &occIbo_);
        BindVertexArray(occVao_);
        BindBuffer(GL_ARRAY_BUFFER, occVbo_);
        BufferData(GL_ARRAY_BUFFER, sizeof cube, cube, GL_STATIC_DRAW);
        EnableVertexAttribArray(0); VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, occIbo_);
        BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof idx, idx, GL_STATIC_DRAW);
        BindVertexArray(0);
    }
    flushInstances();
    // the state this pass touches is saved and restored exactly (no effect on any later draw)
    GLint saveDepthFunc = GL_LESS; GLboolean saveDepthMask = GL_TRUE, saveColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    glGetIntegerv(GL_DEPTH_FUNC, &saveDepthFunc);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &saveDepthMask);
    glGetBooleanv(GL_COLOR_WRITEMASK, saveColorMask);
    const GLboolean saveDepthTest = glIsEnabled(GL_DEPTH_TEST), saveCull = glIsEnabled(GL_CULL_FACE), saveBlend = glIsEnabled(GL_BLEND);
    UseProgram(occProg_);
    UniformMatrix4fv(occUVP_, 1, GL_FALSE, viewProj_.m);
    BindVertexArray(occVao_);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    const int slot = frameNo_ % 4;
    for (auto& kv : pawnOcc_) {
        PawnOcc& po = kv.second;
        if (po.boxFrame != frameNo_) { po.qFrame[slot] = -1; continue; }
        po.lastSeen = frameNo_;
        const core::Vec3 mn = po.mn - core::Vec3{0.5f, 0.5f, 0.5f}, mx = po.mx + core::Vec3{0.5f, 0.5f, 0.5f};
        const float nearPad = znear_ * 4.0f + 0.1f;     // the camera in or at the box: visible, no query
        if (camPos_.x > mn.x - nearPad && camPos_.x < mx.x + nearPad && camPos_.y > mn.y - nearPad && camPos_.y < mx.y + nearPad &&
            camPos_.z > mn.z - nearPad && camPos_.z < mx.z + nearPad) { po.qFrame[slot] = -1; continue; }
        if (!po.q[slot]) GenQueries(1, &po.q[slot]);
        Uniform3f(occUMin_, mn.x, mn.y, mn.z);
        Uniform3f(occUMax_, mx.x, mx.y, mx.z);
        BeginQuery(0x8C2F /*GL_ANY_SAMPLES_PASSED*/, po.q[slot]);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, (void*)0);
        EndQuery(0x8C2F);
        po.qFrame[slot] = frameNo_;
        ++statOccTested_;
    }
    BindVertexArray(0);
    glColorMask(saveColorMask[0], saveColorMask[1], saveColorMask[2], saveColorMask[3]);
    glDepthMask(saveDepthMask);
    glDepthFunc((GLenum)saveDepthFunc);
    if (saveDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (saveCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (saveBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if ((frameNo_ % 600) == 0 && statOccTested_)
        LOG_INFO("wfc pawn occlusion: %d tested, %d part draws skipped, %d skinned preps skipped in the last 600 frames",
                 statOccTested_, statOccCulled_, statOccPrepSkipped_);
    if ((frameNo_ % 600) == 0) statOccTested_ = statOccCulled_ = statOccPrepSkipped_ = 0;
}

void Pipeline::drawDynamic(const MeshData& m, const core::Mat4& model, const void* cacheKey, uint64_t serial,
                           const std::vector<float>* prevP, const std::vector<float>* prevN, float alpha) {
    if (m.empty()) return;
    const SkinDraw* sk = skinDraw_;                     // GPU-skinned: static buffers, bounds from the palette
    const bool blend = !sk && cacheKey && prevP && alpha < 1.0f && prevP->size() == m.positions.size();
    struct DynTimer { std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
        ~DynTimer() { gStats.dynTotalMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); ++gStats.dynCalls; } } dynTimer;
    if (!finiteMat(model)) {
        reportNonFinite("dynamic model matrix", m.mats.empty() ? std::string("?") : m.mats[0].wfcName);
        return;
    }
    // model-space bounds (one pass with the non-finite guard). A posed buffer whose serial is unchanged reuses the
    // bounds of its last scan (its vertices did not change: the drawDynamicMeshPosed contract).
    auto scan = [&](const std::vector<float>& pos, core::Vec3& mn, core::Vec3& mx) {
        mn = {1e30f, 1e30f, 1e30f}; mx = {-1e30f, -1e30f, -1e30f};
        for (size_t i = 0; i + 2 < pos.size(); i += 3) {
            const float x = pos[i], y = pos[i + 1], z = pos[i + 2];
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
            mn = {std::min(mn.x, x), std::min(mn.y, y), std::min(mn.z, z)};
            mx = {std::max(mx.x, x), std::max(mx.y, y), std::max(mx.z, z)};
        }
        return true;
    };
    PosedBuf* pbp = nullptr;
    bool same = false;
    if (cacheKey) {
        pbp = &posed_[cacheKey];
        pbp->lastFrame = frameNo_;
        same = pbp->vao && pbp->serial == serial && pbp->verts == m.vertexCount() && pbp->idx == m.indices.size() && pbp->blended == blend;
    }
    core::Vec3 bmn, bmx;
    if (sk) {
        bmn = sk->mn; bmx = sk->mx;
    } else if (same) {
        bmn = pbp->mn; bmx = pbp->mx;
    } else {
        core::Vec3 pmn, pmx;
        if (!scan(m.positions, bmn, bmx) || (blend && !scan(*prevP, pmn, pmx))) {
            reportNonFinite("dynamic vertex position", m.mats.empty() ? std::string("?") : m.mats[0].wfcName);
            return;
        }
        if (pbp) { pbp->mn = bmn; pbp->mx = bmx; if (blend) { pbp->pmn = pmn; pbp->pmx = pmx; } }
    }
    if (blend) {   // the blended pose's bounds: per-axis lerp of the two poses' bounds (each blended vertex lies inside)
        auto lerp = [&](const core::Vec3& a, const core::Vec3& b) { return a + (b - a) * alpha; };
        const core::Vec3 lo = lerp(pbp->pmn, bmn), hi = lerp(pbp->pmx, bmx);
        bmn = lo; bmx = hi;
    }
    // Frustum cull (bot counts: off-screen characters were skinned-vertex built + uploaded + drawn every frame). The
    // world-space box of the posed mesh, grown by half its diagonal + 3 m (projected shadows, effects of a character
    // just outside the view), against the frame's planes. A culled mesh still ticks its light environment below (no
    // lighting pop when it comes into view); only the vertex build, upload, shadow and draw are skipped.
    bool offscreen = false;
    static const bool noDynCull = std::getenv("WFC_NODYNCULL") != nullptr;   // A/B
    if (!noDynCull && !warmup_) {
        const core::Vec3 c = core::transformPoint(model, (bmn + bmx) * 0.5f);
        const float rad = core::length(bmx - bmn) * 0.75f + 3.0f;   // model scale 1 (characters / weapons / gibs)
        for (int f = 0; f < 6 && !offscreen; ++f) {
            const float* pl = frustum_[f];
            const float len = std::sqrt(pl[0] * pl[0] + pl[1] * pl[1] + pl[2] * pl[2]);
            if (len > 0 && pl[0] * c.x + pl[1] * c.y + pl[2] * c.z + pl[3] < -rad * len) offscreen = true;
        }
    }
    if (offscreen) { ++counts_.culled; ++gStats.dynCulled; }
    GLuint drawVao = sk ? sk->vao : dynVao_;
    if (!offscreen && cacheKey && !sk) {   // drawDynamicMeshPosed: persistent buffers per MeshData, rebuilt on a new pose serial
        PosedBuf& pb = *pbp;
        const bool fresh = !pb.vao;
        if (fresh) { GenVertexArrays(1, &pb.vao); GenBuffers(1, &pb.vbo); GenBuffers(1, &pb.ibo); }
        if (!same) {
            static std::vector<float> v;
            auto tb0 = std::chrono::steady_clock::now();
            buildVertices(m, v, blend);
            auto tb1 = std::chrono::steady_clock::now();
            BindVertexArray(pb.vao);
            BindBuffer(GL_ARRAY_BUFFER, pb.vbo);
            BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW);
            if (fresh || pb.idx != m.indices.size()) {   // indices: once per mesh shape
                BindBuffer(GL_ELEMENT_ARRAY_BUFFER, pb.ibo);
                BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(m.indices.size() * 4), m.indices.data(), GL_STATIC_DRAW);
            }
            setupAttribs();
            if (blend) {   // prev pose stream: position + raw normal (cur's normal when the previous has none)
                static std::vector<float> pv;
                const size_t n = m.vertexCount();
                const bool hasPN = prevN && prevN->size() == m.positions.size();
                const bool hasN = m.normals.size() == m.positions.size();
                pv.resize(n * 6);
                for (size_t i = 0; i < n; ++i) {
                    float* o = &pv[i * 6];
                    o[0] = (*prevP)[i * 3]; o[1] = (*prevP)[i * 3 + 1]; o[2] = (*prevP)[i * 3 + 2];
                    const std::vector<float>* nsrc = hasPN ? prevN : (hasN ? &m.normals : nullptr);
                    o[3] = nsrc ? (*nsrc)[i * 3] : 0.0f; o[4] = nsrc ? (*nsrc)[i * 3 + 1] : 1.0f; o[5] = nsrc ? (*nsrc)[i * 3 + 2] : 0.0f;
                }
                if (!pb.prevVbo) GenBuffers(1, &pb.prevVbo);
                BindBuffer(GL_ARRAY_BUFFER, pb.prevVbo);
                BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(pv.size() * sizeof(float)), pv.data(), GL_STREAM_DRAW);
                EnableVertexAttribArray(7); VertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
                EnableVertexAttribArray(8); VertexAttribPointer(8, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
            } else {
                DisableVertexAttribArray(7); DisableVertexAttribArray(8);
            }
            BindVertexArray(0);
            pb.blended = blend;
            gStats.dynBuildMs += std::chrono::duration<double, std::milli>(tb1 - tb0).count();
            gStats.dynUploadMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tb1).count();
            pb.serial = serial; pb.verts = m.vertexCount(); pb.idx = m.indices.size();
        } else {
            ++gStats.dynReused;
        }
        drawVao = pb.vao;
    }
    if (!offscreen && !cacheKey && !sk) {
        static std::vector<float> v;   // reused: a character's interleaved vertices are ~0.5 MB per draw
        auto tb0 = std::chrono::steady_clock::now();
        buildVertices(m, v);
        auto tb1 = std::chrono::steady_clock::now();
        BindVertexArray(dynVao_);
        BindBuffer(GL_ARRAY_BUFFER, dynVbo_);
        BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, dynIbo_);
        BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(m.indices.size() * 4), m.indices.data(), GL_STREAM_DRAW);
        gStats.dynBuildMs += std::chrono::duration<double, std::milli>(tb1 - tb0).count();
        gStats.dynUploadMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tb1).count();
        setupAttribs();
        BindVertexArray(0);
    }
    const auto tEnv0 = std::chrono::steady_clock::now();
    // Which LightEnvironmentComponent draws this mesh: the Optimus robot (and its weapon, which uses the
    // owner's environment) or vehicle form, identified by the cooked packages of its materials.
    static const std::vector<core::Vec3> kRobotSamples = [] {   // UE (x,y,z) -> glTF (x,z,y)
        const float u[6][3] = {{0, 0, 0.9f}, {0, 0, -0.7f}, {0.7f, 0.7f, 0.7f}, {-0.7f, -0.7f, 0.7f},
                               {0.7f, -0.7f, -0.7f}, {-0.7f, 0.7f, -0.7f}};
        std::vector<core::Vec3> v; for (auto& o : u) v.push_back({o[0], o[2], o[1]}); return v; }();
    static const std::vector<core::Vec3> kVehicleSamples = [] {
        const float u[5][3] = {{0, 0, 0.7f}, {0.8f, 0.8f, 0}, {0.8f, -0.8f, 0}, {-0.8f, 0.8f, 0}, {-0.8f, -0.8f, 0}};
        std::vector<core::Vec3> v; for (auto& o : u) v.push_back({o[0], o[2], o[1]}); return v; }();
    envSamples_ = nullptr;
    envForm_ = -1;
    // the cached draw list for this mesh (built once per mesh / material set / defrag state - see DynSubs)
    static const bool noDynCache = std::getenv("WFC_NODYNSUBCACHE") != nullptr;   // A/B: rebuild per call
    auto od = ownerDefrag_.find(drawOwner_);
    static const bool diagDefrag = std::getenv("WFC_DEFRAG") != nullptr;   // diagnostics: every owner
    const bool defrag = od != ownerDefrag_.end() || diagDefrag;
    DynSubs& ds = dynSubs_[&m];
    ds.lastFrame = frameNo_;
    {
        static thread_local std::vector<uint64_t> sig;
        sig.clear();
        sig.push_back((uint64_t)m.subs.size() << 32 | (uint64_t)m.mats.size());
        sig.push_back((uint64_t)m.indices.size() << 1 | (defrag ? 1u : 0u));
        for (const SubMesh& s : m.subs) sig.push_back((uint64_t)s.indexOffset << 32 ^ (uint64_t)s.indexCount << 8 ^ (uint64_t)(uint32_t)s.material);
        for (const Material& mt : m.mats) sig.push_back((uint64_t)(uintptr_t)mt.wfcName.data() ^ (uint64_t)mt.wfcName.size() << 48);
        if (noDynCache || ds.sig != sig) {
            ds.sig = sig;
            ds.g = GpuMesh{};
            ds.envKind = -1; ds.weapon = false;
            for (const Material& mt : m.mats) {
                // the resolved name: Gameplay's weapon (and some body) meshes carry only source names - with the raw
                // wfcName those were never classified, so weapons were lit by a world cell / cache environment
                // (computeEnv per weapon per frame at 64 players) instead of their owner's, as intended below
                static const bool rawNames = std::getenv("WFC_ENVRAWNAMES") != nullptr;   // A/B: the previous test
                const std::string nm = mt.wfcName.empty() && !rawNames ? resolveBySourceName(&mt) : mt.wfcName;
                if (nm.find("_VEH_p.") != std::string::npos) { ds.envKind = 1; break; }
                if (nm.find("_ROBO_p.") != std::string::npos) { ds.envKind = 0; break; }
                if (nm.rfind("WEP_", 0) == 0) { ds.envKind = 0; ds.weapon = true; break; }
            }
            std::vector<SubMesh> subs = m.subs;
            if (subs.empty()) { SubMesh s; s.indexOffset = 0; s.indexCount = (uint32_t)m.indices.size(); subs.push_back(s); }
            bool complete = true;
            for (const SubMesh& s : subs) {
                if (!subInBounds(m, s.indexOffset, s.indexCount, "dynamic")) continue;
                const Material* mat = (s.material >= 0 && (size_t)s.material < m.mats.size()) ? &m.mats[(size_t)s.material] : nullptr;
                Sub d;
                d.first = s.indexOffset; d.count = s.indexCount;
                d.prog = dynamicProgram(mat);
                d.matName = mat ? (mat->wfcName.empty() ? resolveBySourceName(mat) : mat->wfcName) : std::string();
                if (defrag && mat) {   // M74: the form's EnergyDeathMaterial replaces the material
                    if (const std::string* ed = energyDeathFor(mat->wfcName)) {
                        Material dm; dm.wfcName = *ed;
                        const int p = dynamicProgram(&dm);
                        if (p >= 0) { d.prog = p; d.matName = *ed; }
                    }
                }
                if (d.prog < 0) complete = false;
                ds.g.subs.push_back(d);
            }
            if (!complete) ds.sig.clear();       // a program still missing: rebuild next call (as before)
        }
    }
    bool weapon = ds.weapon;
    if (ds.envKind == 1) { envSamples_ = &kVehicleSamples; envForm_ = 1; }
    else if (ds.envKind == 0) { envSamples_ = &kRobotSamples; envForm_ = 0; }
    if (envForm_ >= 0) envForm_ += 16 * drawOwner_;   // per character instance (owner 0: keys 0 / 1)
    if (envSamples_ && m.vertexCount() > 0) {          // world-space bounds of the posed mesh (the cull scan's bounds)
        const core::Vec3 mn = bmn, mx = bmx;
        core::Vec3 c = core::transformPoint(model, (mn + mx) * 0.5f), e = (mx - mn) * 0.5f;
        envBoundsCenter_ = c;
        envBoundsExtent_ = {e.x, e.y, e.z};   // model scale is 1 for characters; yaw-only: axis-aligned extent kept
        // the weapon is lit by its owner's environment (no update from the weapon's own bounds)
        if (dleRobotSamples_.empty()) { dleRobotSamples_ = kRobotSamples; dleVehicleSamples_ = kVehicleSamples; }
        DirectLightEnvState& st = dle_[envForm_];
        if (!weapon && st.lastFrame != frameNo_ && !std::getenv("WFC_OLDCHARENV")) {
            st.lastFrame = frameNo_;
            tickDirectLightEnv(envForm_, c, envBoundsExtent_, core::Vec3{model.m[12], model.m[13], model.m[14]});
        }
    }
    const auto tEnv1 = std::chrono::steady_clock::now();
    gStats.dynEnvMs += std::chrono::duration<double, std::milli>(tEnv1 - tEnv0).count();
    if (offscreen) { envSamples_ = nullptr; envForm_ = -1; return; }   // light environment ticked above
    bool occluded = false;
    if (envSamples_ && !warmup_ && pawnOcclusionOn()) {   // character / weapon parts: the owner's occlusion box
        PawnOcc& po = pawnOcc_[drawOwner_];
        if (po.boxFrame != frameNo_) { po.mn = {1e30f, 1e30f, 1e30f}; po.mx = {-1e30f, -1e30f, -1e30f}; po.boxFrame = frameNo_; }
        for (int k = 0; k < 8; ++k) {
            const core::Vec3 p{(k & 1) ? bmx.x : bmn.x, (k & 2) ? bmx.y : bmn.y, (k & 4) ? bmx.z : bmn.z};
            const core::Vec3 w = core::transformPoint(model, p);
            po.mn = {std::min(po.mn.x, w.x), std::min(po.mn.y, w.y), std::min(po.mn.z, w.z)};
            po.mx = {std::max(po.mx.x, w.x), std::max(po.mx.y, w.y), std::max(po.mx.z, w.z)};
        }
        if (pawnOccPrepOn() && !weapon && !std::getenv("WFC_NOCHARSHADOWS")) {   // + the composite-shadow volume
            ShadowProjector scratch;
            if (const ShadowProjector* sp = projectorFor(envForm_, scratch); sp && sp->on && sp->type >= 1 && sp->type <= 3) {
                const core::Vec3 B = envBoundsCenter_;
                const float R = std::max(core::length(envBoundsExtent_), 0.05f);
                core::Vec3 farC; float farR;
                if (sp->type == 1) {                    // directional: receivers up to D past the subject, cone radius 2R
                    const float D = 2.0f * R + 3.0f;
                    farC = B + core::normalize(sp->dir) * D; farR = 2.0f * R;
                } else {                                // point / spot: up to the light radius from the light
                    core::Vec3 v = B - sp->pos; const float dist = std::max(core::length(v), 1e-3f);
                    farC = sp->pos + v * (sp->radius / dist); farR = R * sp->radius / dist;
                }
                auto addSphere = [&](const core::Vec3& c, float rr) {
                    po.mn = {std::min(po.mn.x, c.x - rr), std::min(po.mn.y, c.y - rr), std::min(po.mn.z, c.z - rr)};
                    po.mx = {std::max(po.mx.x, c.x + rr), std::max(po.mx.y, c.y + rr), std::max(po.mx.z, c.z + rr)};
                };
                addSphere(B, R);
                addSphere(farC, farR);
            }
        }
        occluded = po.occluded;
        if (occluded) ++statOccCulled_;
    }
    if (skinPrepSkipped_) { envSamples_ = nullptr; envForm_ = -1; return; }   // light env ticked, box recorded
    // Mesh.LastRenderTime: set only for owners actually rendered (after frustum AND occlusion culling, RE 8ec2c46 /
    // stock UE3, HIGH): the TransformerHealthBar marker (now - LastRenderTime < 0.25 s, CONFIRMED script) hides behind
    // walls as in WFC. WFC_OCCMARKERREFRESH=1 = refresh for occlusion-culled owners too (previous behaviour)
    static const bool occRefresh = std::getenv("WFC_OCCMARKERREFRESH") != nullptr;
    if (!warmup_ && (!occluded || occRefresh)) ownerRendered_[drawOwner_] = std::chrono::steady_clock::now();
    if (!warmup_ && !occluded) ownerRenderedTime_[drawOwner_] = time_;
    GpuMesh& g = ds.g;
    g.vao = drawVao;
    const auto tSubs1 = std::chrono::steady_clock::now();
    gStats.dynSubsMs += std::chrono::duration<double, std::milli>(tSubs1 - tEnv1).count();
    // the owner's runtime parameters apply to its shadow caster / depth pre-pass too (M74: a dissolving Defrag body
    // must not cast or depth-write its whole silhouette)
    inDynamicDraw_ = true;
    poseBlend_ = blend ? 1 : 0; poseAlpha_ = blend ? alpha : 1.0f;
    if (envSamples_ && !weapon && !std::getenv("WFC_NOCHARSHADOWS")) {   // the environment's projector -> ShadowMask
        ShadowProjector scratch;
        const auto ts0 = std::chrono::steady_clock::now();
        if (const ShadowProjector* p = projectorFor(envForm_, scratch)) castCharacterShadow(g, model, *p);
        gStats.dynShadowMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ts0).count();
    }
    dynamicMaskDraw_ = envSamples_ != nullptr;
    if (!occluded) {
        const auto td = std::chrono::steady_clock::now();
        drawSubs(g, model, true);
        gStats.dynDrawMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - td).count();
    }
    poseBlend_ = 0; poseAlpha_ = 1.0f;
    inDynamicDraw_ = false;
    dynamicMaskDraw_ = false;
    envSamples_ = nullptr;
    envForm_ = -1;
}

// ------------------------------------------------------------------------- effects
// Scene depth as seen by translucency: copied from the HDR target whenever opaque geometry was drawn
// since the last copy (UE3 resolves scene depth before the translucent pass).
// Distortion apply (Xenon microcode, engine shader with AccumulatedDistortionTexture/SceneColorTexture):
// uv' = uv + (acc.rg - acc.ba) * (0.25, -0.25) in D3D texture space (y down) -> +0.25 in GL; out =
// SceneColor(uv'). Applied full-screen (zero offset elsewhere reproduces the scene unchanged).
void Pipeline::applyDistortion() {
    if (!distApplyProg_) {
        const char* vs = "#version 330 core\nout vec2 vUV;\nvoid main(){ vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);"
                         " vUV = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
        const char* fs = "#version 330 core\nin vec2 vUV; out vec4 oColor; uniform sampler2D uScene; uniform sampler2D uAcc;\n"
                         "void main(){ vec4 a = texture(uAcc, vUV); vec2 uv = vUV + (a.rg - a.ba) * vec2(0.25, 0.25);"
                         " oColor = texture(uScene, uv); }\n";
        GLuint v = compile(GL_VERTEX_SHADER, vs, "distort.vs"), f = compile(GL_FRAGMENT_SHADER, fs, "distort.fs");
        if (v && f) distApplyProg_ = link(v, f, "distort");
        if (!distApplyProg_) { distUsed_ = false; return; }
    }
    BindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneCopyFbo_);
    BlitFramebuffer(0, 0, vpW_, vpH_, 0, 0, vpW_, vpH_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDepthMask(GL_FALSE);
    UseProgram(distApplyProg_);
    ActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, distTex_);
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, sceneCopyTex_);
    Uniform1i(GetUniformLocation(distApplyProg_, "uScene"), 0);
    Uniform1i(GetUniformLocation(distApplyProg_, "uAcc"), 1);
    BindVertexArray(postVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    BindVertexArray(0);
    UseProgram(0);
    glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
}

void Pipeline::writeFrameReport() {
    FILE* f = std::fopen(std::getenv("WFC_FRAMEREPORT"), "w");
    if (!f) return;
    static const char* kBlend[] = {"opaque", "masked", "translucent", "additive", "modulate"};
    std::fprintf(f, "frame %d camera (%.2f %.2f %.2f) fog %s post bloom=%d dof=%d clut=%d distortion_pass=%d\n", frameNo_,
                 camPos_.x, camPos_.y, camPos_.z, fogOn_ ? "on" : "off", post_.bloom ? 1 : 0, post_.dof ? 1 : 0,
                 clutTex_ ? 1 : 0, distUsed_ ? 1 : 0);
    std::fprintf(f, "materials drawn (draws, blend, lit, lightmap, dynamic, fx, distortion):\n");
    for (const auto& kv : frameDraws_) {
        const FrameDraw& d = kv.second;
        std::fprintf(f, "  %-70s %4d %-11s %s %s %s %s %s\n", kv.first.c_str(), d.draws, kBlend[std::min(std::max(d.blend, 0), 4)],
                     d.lit ? "lit" : "unlit", d.vertexLM ? "vertexLM" : d.lightmapped ? "lightmap" : "-",
                     d.dynamic ? "dynamic" : "static", d.fx ? "fx" : "-", d.distortion ? "distortion" : "-");
    }
    for (const auto& kv : dle_) {
        const DirectLightEnvState& st = kv.second;
        std::fprintf(f, "DirectLightEnv %s: full updates %d (queued %d, last at frame %d), known %zu, direct %d (crossfade %.3f),"
                        " composite shadow %s (strength %.3f, %d candidates)\n", kv.first == 0 ? "robot" : "vehicle",
                     st.fullUpdates, st.queuedFull, st.lastFullFrame, st.known.size(), st.directCount, st.crossfade,
                     st.shadowLight >= 0 ? lights_[(size_t)st.shadowLight].name.c_str() : "none", st.shadowStrength,
                     st.shadowCandidates);
        std::fprintf(f, "  DirectLightAmbientContribution (%.4f %.4f %.4f), ShadowMask %s this frame\n", st.env.dlac[0],
                     st.env.dlac[1], st.env.dlac[2], maskDrawnFrame_ == frameNo_ ? "projected" : "clear (1,1,1,1)");
        const ShadowProjector& pj = st.projector;
        static const char* kPT[] = {"-", "directional", "point", "spot"};
        std::fprintf(f, "  shadow projector: %s, type %s, source %s, ModShadowColor (%.3f %.3f %.3f %.3f), updates %d, "
                        "source changes %d\n", pj.on ? "ON" : "off", kPT[std::min(std::max(pj.type, 0), 3)],
                     pj.source >= 0 ? lights_[(size_t)pj.source].name.c_str() : "-", pj.modShadowColor[0],
                     pj.modShadowColor[1], pj.modShadowColor[2], pj.modShadowColor[3], pj.updates, pj.sourceChanges);
        if (st.shadowTop >= 0)
            std::fprintf(f, "  composite top %s (%s): lum %.4f, smoothed vis %.3f, runner-up lum %.4f\n",
                         lights_[(size_t)st.shadowTop].name.c_str(), lvv_.isBaked(st.shadowTop) ? "baked" : "unbaked",
                         st.shadowTopScore, st.shadowTopVis, st.shadowNextScore);
    }
    if (const char* md = std::getenv("WFC_MASKDUMP")) {     // ShadowMask R channel as PGM (top row first)
        if (maskDrawnFrame_ == frameNo_ && maskFbo_) {
            std::vector<unsigned char> px((size_t)maskW_ * maskH_ * 4);
            BindFramebuffer(GL_READ_FRAMEBUFFER, maskBlurFbo_);   // what the character pass reads
            glReadPixels(0, 0, maskW_, maskH_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            BindFramebuffer(GL_FRAMEBUFFER, fbo_);
            if (FILE* pf = std::fopen(md, "wb")) {
                std::fprintf(pf, "P5\n%d %d\n255\n", maskW_, maskH_);
                for (int y = maskH_ - 1; y >= 0; --y)
                    for (int x = 0; x < maskW_; ++x) std::fputc(px[((size_t)y * maskW_ + x) * 4], pf);
                std::fclose(pf);
            }
        }
        std::fprintf(f, "ShadowMask %dx%d, projected this frame: %s\n", maskW_, maskH_, maskDrawnFrame_ == frameNo_ ? "yes" : "no");
    }
    for (const ShadowFrameInfo& si : shadowFrame_)
        std::fprintf(f, "projected shadow: %s subject, projector type %d from %s, resolution %d, ShadowModulateColor %.3f\n",
                     si.form == 0 ? "robot" : "vehicle", si.type, si.source >= 0 ? lights_[(size_t)si.source].name.c_str() : "-",
                     si.res, si.factor);
    std::fprintf(f, "dynamic light environments (UberLight, TotalLightCount 2):\n");
    for (const std::string& e : frameEnvs_) std::fprintf(f, "  %s\n", e.c_str());
    std::fclose(f);
}

// ShadowMaskTexture for a draw: the frame's mask once a shadow was projected into it; otherwise the mask
// is all (1,1,1,1) (cleared, nothing drawn, blur skipped - 0x82DEC190), represented by a neutral 1x1
// texture. Test hook WFC_SHADOWMASKTEST=<v> binds a uniform mask value to character draws.
GLuint Pipeline::shadowMaskTexFor(bool character) {
    maskTexelOffset_[0] = maskTexelOffset_[1] = 0.0f;
    if (character && maskDrawnFrame_ == frameNo_ && maskBlurTex_) {
        maskTexelOffset_[0] = 0.5f / (float)maskW_;
        maskTexelOffset_[1] = 0.5f / (float)maskH_;
        return maskBlurTex_;                      // resolved + blurred mask
    }
    auto make = [](float v) {
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        unsigned char px[4] = {(unsigned char)std::lround(std::min(std::max(v, 0.0f), 1.0f) * 255.0f), 0, 0, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        return t;
    };
    if (!neutralMaskTex_) neutralMaskTex_ = make(1.0f);
    static const char* test = std::getenv("WFC_SHADOWMASKTEST");
    if (character && test) {
        if (!testMaskTex_) testMaskTex_ = make((float)std::atof(test));
        return testMaskTex_;
    }
    return neutralMaskTex_;
}

// UE3 resolves scene colour once before the translucent pass; SceneTexture reads that copy (first use per frame).
void Pipeline::ensureSceneColor() {
    if (sceneColorCopied_ || !sceneCopyFbo_) return;
    BindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneCopyFbo_);
    BlitFramebuffer(0, 0, vpW_, vpH_, 0, 0, vpW_, vpH_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    sceneColorCopied_ = true;
}

void Pipeline::ensureSceneDepth() {
    if (!depthDirty_ || !depthCopyFbo_) return;
    BindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    BindFramebuffer(GL_DRAW_FRAMEBUFFER, depthCopyFbo_);
    BlitFramebuffer(0, 0, vpW_, vpH_, 0, 0, vpW_, vpH_, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    depthDirty_ = false;
}

std::string Pipeline::resolveName(const std::string& name) const {
    if (mats_.count(name)) return name;
    Material tmp;
    tmp.sourceName = name.substr(name.rfind('.') == std::string::npos ? 0 : name.rfind('.') + 1);
    return resolveBySourceName(&tmp);
}

bool Pipeline::drawFx(int id, const core::Mat4& model, const float color[4]) {
    if (id < 0 || (size_t)id >= meshes_.size()) return false;
    GpuMesh& g = meshes_[(size_t)id];
    for (const Sub& s : g.subs)
        if (s.prog < 0 || !progs_[(size_t)s.prog].original) return false;
    std::copy(color, color + 4, fxColor_);
    frameFx_ = true;
    drawSubs(g, model, true);
    frameFx_ = false;
    std::fill(fxColor_, fxColor_ + 4, 1.0f);
    VertexAttrib4f(5, 1, 1, 1, 1);
    return true;
}

int Pipeline::spriteProgram(const std::string& material) {
    auto it = spriteProg_.find(material);
    if (it == spriteProg_.end()) {
        std::string nm = resolveName(material);
        int prog = nm.empty() ? -1 : programFor(nm, nullptr, false);
        if (prog >= 0 && !progs_[(size_t)prog].original) prog = -1;
        if (prog < 0) LOG_WARN("wfc: particle material %s not compiled; textured fallback", material.c_str());
        it = spriteProg_.emplace(material, prog).first;
    }
    return it->second;
}

// M58: the placed particle components' materials / meshes, resolved during the load (frontend scenes skip the
// effect / weapon prewarm; the title's emitters compiled on their first drawn frames: Lightning_Anim_03 45 ms,
// Lightning_02 texture 43 ms, ...)
void Pipeline::prewarmPlacedFx() {
    auto t0 = std::chrono::steady_clock::now(), lastYield = t0;
    size_t progs = spriteProg_.size(), meshes = fxMeshes_.size();
    for (const FxInstance& in : fxInstances_) {
        auto sit = fxSystems_.find(in.system);
        if (sit == fxSystems_.end()) continue;
        for (const FxEmitter& em : sit->second.emitters) {
            if (!em.renderable) continue;
            for (const FxLod& L : em.lods) {
                if (!L.meshGltf.empty()) fxMeshFor(L);
                else if (!L.material.empty()) spriteProgram(L.material);
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration<double, std::milli>(now - lastYield).count() >= 16.0) { yieldLoad(); lastYield = now; }
            }
        }
    }
    LOG_INFO("wfc: prewarmed placed effects: %zu sprite materials, %zu meshes in %.0f ms", spriteProg_.size() - progs,
             fxMeshes_.size() - meshes, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
}

void Pipeline::spriteCoverage(const char* material, const Sprite* sp, size_t n) {
    // GPU-spike evidence: screen coverage of this batch (sum of projected quad areas, in screens)
        FrameRec& fr = frameRecs_[frameNo_ & 3];
        if (fr.frame != frameNo_) fr = FrameRec{}, fr.frame = frameNo_;
        double cov = 0;
        for (size_t i = 0; i < n; ++i) {
            float px[4], py[4]; bool ok = true;
            for (int k = 0; k < 4 && ok; ++k) {
                const core::Vec3& p = sp[i].c[k];
                const float* m = viewProj_.m;
                const float cx = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], cy = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
                const float cw = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
                if (cw <= 0.05f) { ok = false; break; }   // at / behind the near plane: count as one screen
                px[k] = std::min(std::max(cx / cw, -1.0f), 1.0f); py[k] = std::min(std::max(cy / cw, -1.0f), 1.0f);
            }
            if (!ok) { cov += 1.0; continue; }
            const double a = 0.5 * std::fabs((px[0] * py[1] - px[1] * py[0]) + (px[1] * py[2] - px[2] * py[1]) +
                                             (px[2] * py[3] - px[3] * py[2]) + (px[3] * py[0] - px[0] * py[3]));
            cov += a / 4.0;                              // NDC square area 4 = one screen
        }
        fr.sprites += (int)n; ++fr.draws; fr.coverage += cov; fr.matCov[material] += cov;
}

// Appends n sprites' vertices (the drawSprites layout: 14 floats per vertex, colour, second SubUV + blend).
void Pipeline::spriteAppend(const Sprite* sp, size_t n, const core::Vec3& facing, std::vector<float>& v,
                            std::vector<float>& col, std::vector<float>& sub) {
    const size_t v0 = v.size(), c0 = col.size(), s0 = sub.size();
    v.resize(v0 + n * 4 * 14); col.resize(c0 + n * 4 * 4); sub.resize(s0 + n * 4 * 3);
    core::Vec3 N = core::normalize(facing);
    for (size_t i = 0; i < n; ++i) {
        const Sprite& s = sp[i];
        core::Vec3 T = core::normalize(s.c[1] - s.c[0]);   // +U across the quad
        for (int k = 0; k < 4; ++k) {
            float* o = &v[v0 + (i * 4 + (size_t)k) * 14];
            o[0] = s.c[k].x; o[1] = s.c[k].y; o[2] = s.c[k].z;
            o[3] = N.x; o[4] = N.y; o[5] = N.z;
            o[6] = T.x; o[7] = T.y; o[8] = T.z; o[9] = 1.0f;
            o[10] = s.uv[k][0]; o[11] = s.uv[k][1]; o[12] = 0; o[13] = 0;
            std::copy(s.color, s.color + 4, &col[c0 + (i * 4 + (size_t)k) * 4]);
            float* u = &sub[s0 + (i * 4 + (size_t)k) * 3];
            u[0] = s.uv2[k][0]; u[1] = s.uv2[k][1]; u[2] = s.blend;
        }
    }
}

bool Pipeline::drawSprites(const char* material, const Sprite* sp, size_t n, const core::Vec3& facing) {
    // AMD stability (Milestone E): never upload non-finite sprite corners or an unbounded batch
    {
        constexpr size_t kMaxSprites = 262144;
        bool bad = n > kMaxSprites;
        for (size_t i = 0; i < n && !bad; ++i)
            for (int k = 0; k < 4 && !bad; ++k) bad = !std::isfinite(sp[i].c[k].x) || !std::isfinite(sp[i].c[k].y) || !std::isfinite(sp[i].c[k].z);
        if (bad) {
            static std::set<std::string> warned;
            if (warned.insert(material ? material : "?").second)
                LOG_WARN("render guard: sprite batch %s: %zu sprites (non-finite corners dropped, capped at %zu)", material ? material : "?", n, kMaxSprites);
            std::vector<Sprite> ok;
            ok.reserve(std::min(n, kMaxSprites));
            for (size_t i = 0; i < n && ok.size() < kMaxSprites; ++i) {
                bool fin = true;
                for (int k = 0; k < 4; ++k) fin = fin && std::isfinite(sp[i].c[k].x) && std::isfinite(sp[i].c[k].y) && std::isfinite(sp[i].c[k].z);
                if (fin) ok.push_back(sp[i]);
            }
            return ok.empty() ? false : drawSprites(material, ok.data(), ok.size(), facing);
        }
    }
    if (!material || !sp || n == 0) return false;
    for (size_t i = 0; i < n; ++i)
        for (const core::Vec3& c : sp[i].c)
            if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z)) {
                reportNonFinite("sprite corner", material);
                return false;
            }
    static const bool immediateTrans = std::getenv("WFC_IMMEDIATETRANS") != nullptr || std::getenv("WFC_M05TRANS") != nullptr;
    if (deferTrans_ && !flushingTrans_ && !immediateTrans) {
        if (spriteProg_.count(material) && spriteProg_[material] < 0) return false;   // known fallback material
        core::Vec3 c{0, 0, 0};
        for (size_t i = 0; i < n; ++i) c = c + (sp[i].c[0] + sp[i].c[2]) * 0.5f;
        c = c * (1.0f / (float)n);
        if (spriteUsed_ == spritePool_.size()) spritePool_.emplace_back();
        SpriteBatch& batch = spritePool_[spriteUsed_];
        batch.mat = material; batch.facing = facing; batch.sprites.assign(sp, sp + n);
        std::copy(dynParam_, dynParam_ + 4, batch.dyn);
        transQueue_.push_back({viewDepth(c), 1, (int)spriteUsed_});
        ++spriteUsed_;
        return true;
    }
    if (spriteProgram(material) < 0) return false;
    spriteCoverage(material, sp, n);
    auto it = spriteProg_.find(material);
    if (!spriteVao_) {
        GenVertexArrays(1, &spriteVao_);
        GenBuffers(1, &spriteVbo_); GenBuffers(1, &spriteCbo_); GenBuffers(1, &spriteIbo_);
    }
    std::vector<float>& v = spriteV_;
    std::vector<float>& col = spriteCol_;
    std::vector<uint32_t>& idx = spriteIdx_;
    v.resize(n * 4 * 14); col.resize(n * 4 * 4); idx.resize(n * 6);   // reused (every element written below)
    core::Vec3 N = core::normalize(facing);
    for (size_t i = 0; i < n; ++i) {
        const Sprite& s = sp[i];
        core::Vec3 T = core::normalize(s.c[1] - s.c[0]);   // +U across the quad
        for (int k = 0; k < 4; ++k) {
            float* o = &v[(i * 4 + (size_t)k) * 14];
            o[0] = s.c[k].x; o[1] = s.c[k].y; o[2] = s.c[k].z;
            o[3] = N.x; o[4] = N.y; o[5] = N.z;
            o[6] = T.x; o[7] = T.y; o[8] = T.z; o[9] = 1.0f;
            o[10] = s.uv[k][0]; o[11] = s.uv[k][1]; o[12] = 0; o[13] = 0;
            std::copy(s.color, s.color + 4, &col[(i * 4 + (size_t)k) * 4]);
        }
        uint32_t b = (uint32_t)(i * 4);
        uint32_t q[6] = {b, b + 1, b + 2, b, b + 2, b + 3};
        std::copy(q, q + 6, &idx[i * 6]);
    }
    BindVertexArray(spriteVao_);
    BindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
    BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW);
    setupAttribs();
    BindBuffer(GL_ARRAY_BUFFER, spriteCbo_);
    BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(col.size() * sizeof(float)), col.data(), GL_STREAM_DRAW);
    EnableVertexAttribArray(5); VertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, 0, (void*)0);
    {   // M67 second SubUV cell + blend per vertex
        std::vector<float>& sub = spriteSub_;
        sub.resize(n * 4 * 3);
        for (size_t i = 0; i < n; ++i)
            for (int k = 0; k < 4; ++k) {
                float* o = &sub[(i * 4 + (size_t)k) * 3];
                o[0] = sp[i].uv2[k][0]; o[1] = sp[i].uv2[k][1]; o[2] = sp[i].blend;
            }
        if (!spriteSubBo_) GenBuffers(1, &spriteSubBo_);
        BindBuffer(GL_ARRAY_BUFFER, spriteSubBo_);
        BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sub.size() * sizeof(float)), sub.data(), GL_STREAM_DRAW);
        EnableVertexAttribArray(6); VertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    }
    BindBuffer(GL_ELEMENT_ARRAY_BUFFER, spriteIbo_);
    BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idx.size() * 4), idx.data(), GL_STREAM_DRAW);
    BindVertexArray(0);
    GpuMesh& g = spriteMesh_;                              // reused one-sub mesh
    g.vao = spriteVao_;
    if (g.subs.empty()) g.subs.emplace_back();
    Sub& d = g.subs[0];
    d.first = 0; d.count = (uint32_t)idx.size(); d.prog = it->second;
    d.matName = material;                                  // diagnostics (WFC_SKIPMAT)
    frameFx_ = true;
    drawSubs(g, core::Mat4::identity(), true);
    frameFx_ = false;
    return true;
}

// ------------------------------------------------------------------------- frame
void Pipeline::ensureTargets(int w, int h) {
    if (fbo_ && w == fbW_ && h == fbH_) return;
    if (!fbo_) { GenFramebuffers(1, &fbo_); glGenTextures(1, &colorTex_); glGenTextures(1, &depthTex_); }
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindTexture(GL_TEXTURE_2D, depthTex_);       // sampled by DOF
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex_, 0);
    if (CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) LOG_ERROR("wfc: HDR framebuffer incomplete");
    // distortion accumulation (RGBA8) sharing the scene depth buffer, and a scene-colour copy
    if (!distFbo_) { GenFramebuffers(1, &distFbo_); glGenTextures(1, &distTex_); GenFramebuffers(1, &sceneCopyFbo_); glGenTextures(1, &sceneCopyTex_); }
    glBindTexture(GL_TEXTURE_2D, distTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, sceneCopyTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    BindFramebuffer(GL_FRAMEBUFFER, distFbo_);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, distTex_, 0);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex_, 0);
    if (CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) LOG_ERROR("wfc: distortion framebuffer incomplete");
    BindFramebuffer(GL_FRAMEBUFFER, sceneCopyFbo_);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneCopyTex_, 0);
    if (!depthCopyFbo_) { GenFramebuffers(1, &depthCopyFbo_); glGenTextures(1, &depthCopyTex_); }
    glBindTexture(GL_TEXTURE_2D, depthCopyTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    BindFramebuffer(GL_FRAMEBUFFER, depthCopyFbo_);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthCopyTex_, 0);
    glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
    // quarter-res bloom ping-pong targets
    int bw = std::max(w / 4, 1), bh = std::max(h / 4, 1);
    for (int i = 0; i < 2; ++i) {
        if (!bloomFbo_[i]) { GenFramebuffers(1, &bloomFbo_[i]); glGenTextures(1, &bloomTex_[i]); }
        glBindTexture(GL_TEXTURE_2D, bloomTex_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, bw, bh, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        BindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i]);
        FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex_[i], 0);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    BindFramebuffer(GL_FRAMEBUFFER, 0);
    bloomW_ = bw; bloomH_ = bh;
    fbW_ = w; fbH_ = h;
}

void Pipeline::beginFrame(const Camera& cam, int w, int h) {
    flushInstances();
    instCursor_ = 0;
    gInstPipeline = this;
    touchNewTextures();                                // textures created since the last frame (loads, prewarms)
    ++frameNo_;
    if (pawnOcclusionOn()) pawnOcclusionResults();
    {   // WFC_RENDERSTATS: frame intervals over WFC_HITCH_MS (default 20) are logged by frame number, next to the
        // "wfc first-use" lines (programs / textures / meshes created that frame) - first-appearance hitch evidence
        static const bool on = std::getenv("WFC_RENDERSTATS") != nullptr;
        static const double thr = std::getenv("WFC_HITCH_MS") ? std::atof(std::getenv("WFC_HITCH_MS")) : 20.0;
        static std::chrono::steady_clock::time_point last;
        const auto now = std::chrono::steady_clock::now();
        if (on && frameNo_ > 3) {
            const double ms = std::chrono::duration<double, std::milli>(now - last).count();
            if (ms > thr) LOG_INFO("wfc hitch: frame %d took %.1f ms", frameNo_ - 1, ms);
        }
        last = now;
    }
    counts_ = FrameCounts();
    sceneColorCopied_ = false;
    frameMats_.clear();
    frameMatCount_ = 0;
    frameNoProg_.clear();
    progSeen_.assign(progs_.size(), 0);
    if ((frameNo_ & 255) == 0 && !posed_.empty()) evictPosed(false);   // meshes no longer drawn (despawned bodies)
    if (frameNo_ % 600 == 0 && statSkinRebuilds_) {   // skinned-model builds (expected: first sight / respawns only)
        LOG_INFO("wfc gpu skin: %d model builds in the last 600 frames (%zu models, %zu instances live)", statSkinRebuilds_,
                 skinModels_.size(), skinInsts_.size());
        statSkinRebuilds_ = 0;
    }
    if ((frameNo_ & 255) == 64)
        for (auto it = dynSubs_.begin(); it != dynSubs_.end();)
            it = frameNo_ - it->second.lastFrame > 600 ? dynSubs_.erase(it) : std::next(it);
    if ((frameNo_ & 255) == 128 && (!skinInsts_.empty() || !skinModels_.empty())) evictSkin(false);
    if (frameNo_ == 2 && !prewarmDone_) prewarmMaterials();   // fallback: no world upload during the load
    gFrameStart = std::chrono::steady_clock::now();
    static auto t0 = std::chrono::steady_clock::now();
    static const bool lockstep = std::getenv("WFC_LOCKSTEP") != nullptr;   // deterministic captures
    time_ = lockstep ? (float)frameNo_ / 60.0f : std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
    vpW_ = w; vpH_ = h;
    ensureTargets(std::max(w, 1), std::max(h, 1));
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w, h);
    // Clear to the fog in-scatter colour: what an infinitely distant ray converges to.
    glClearColor(fogIn_.x, fogIn_.y, fogIn_.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    depthDirty_ = true;
    transQueue_.clear(); transSubs_.clear(); spriteUsed_ = 0;
    deferTrans_ = true;
    distUsed_ = false;
    camPos_ = cam.pos;
    znear_ = cam.znear; zfar_ = cam.zfar;
    viewProj_ = cam.proj() * cam.view();
    camProj_ = cam.proj();
    camView_ = cam.view();
    updateMovers();
    if (lastFxTime_ >= 0.0f) tickMapFx(std::min(time_ - lastFxTime_, 0.25f));
    lastFxTime_ = time_;
    shadowFrame_.clear();
    // frustum planes (Gribb/Hartmann, column-major m[col*4+row])
    const float* m = viewProj_.m;
    auto row = [&](int r, int c) { return m[c * 4 + r]; };
    for (int i = 0; i < 3; ++i) {
        for (int s = 0; s < 2; ++s) {
            float* pl = frustum_[i * 2 + s];
            float sg = s == 0 ? 1.0f : -1.0f;
            for (int c = 0; c < 4; ++c) pl[c] = row(3, c) + sg * row(i, c);
        }
    }
}

// Canvas material tiles: screen quads in pixels (top-left origin) shaded by their compiled material, no depth,
// after the post pass, in submission order. Material params arrive per tile (runtime uniforms).
void Pipeline::drawCanvasTiles() {
    if (uiTileCount_ == 0) return;
    const size_t nTiles = uiTileCount_;
    uiTileCount_ = 0;
    const std::vector<IRenderer::MaterialTile>& tiles = uiTiles_;
    const core::Mat4 saveVP = viewProj_;
    const bool saveFog = fogOn_;
    float W = (float)std::max(vpW_, 1), Hh = (float)std::max(vpH_, 1);
    core::Mat4 ortho = core::Mat4::identity();         // pixels -> NDC, y down
    ortho.m[0] = 2.0f / W; ortho.m[5] = -2.0f / Hh; ortho.m[10] = -1.0f; ortho.m[12] = -1.0f; ortho.m[13] = 1.0f;
    viewProj_ = ortho;
    fogOn_ = false;
    canvasInvGamma_ = 1.0f / displayGamma_;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glViewport(0, 0, (GLsizei)W, (GLsizei)Hh);
    for (size_t ti = 0; ti < nTiles; ++ti) {
        const IRenderer::MaterialTile& t = tiles[ti];
        float cx = t.x + t.w * 0.5f, cy = t.y + t.h * 0.5f, c = std::cos(t.rotation), sn = std::sin(t.rotation);
        Sprite s;
        const float cr[4][2] = {{-0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, -0.5f}, {-0.5f, -0.5f}};   // CCW after the y-down ortho
        for (int k = 0; k < 4; ++k) {
            float px = cr[k][0] * t.w, py = cr[k][1] * t.h;
            s.c[k] = core::Vec3{cx + px * c - py * sn, cy + px * sn + py * c, 0.0f};
            s.uv[k][0] = cr[k][0] < 0 ? t.u0 : t.u1;
            s.uv[k][1] = cr[k][1] < 0 ? t.v0 : t.v1;
            s.color[k == 0 ? 0 : 0] = 1.0f;
        }
        std::fill(s.color, s.color + 4, 1.0f);
        drawParams_ = &t.params;
        bool ok = drawSprites(t.material.c_str(), &s, 1, core::Vec3{0, 0, 1});
        static int logged = 0;
        if (std::getenv("WFC_TILELOG") && logged++ < 8) LOG_INFO("canvas tile %s -> %d", t.material.c_str(), ok ? 1 : 0);
        drawParams_ = nullptr;
    }
    canvasInvGamma_ = 0.0f;
    fogOn_ = saveFog;
    viewProj_ = saveVP;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void Pipeline::endFrame() {
    flushTranslucency();                               // nothing queued normally: drawMapPresentation flushed it
    {
        counts_.materials = frameMatCount_;
        counts_.noProgramMats.assign(frameNoProg_.begin(), frameNoProg_.end());
        lastCounts_ = counts_;
    }
    deferTrans_ = false;
    double thisRenderMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - gFrameStart).count();
    gStats.renderMs += thisRenderMs;
    if (std::getenv("WFC_RENDERSTATS")) {             // hitch attribution: whole frame vs render span
        static auto lastEnd = std::chrono::steady_clock::now();
        auto nowT = std::chrono::steady_clock::now();
        double frameMs = std::chrono::duration<double, std::milli>(nowT - lastEnd).count();
        lastEnd = nowT;
        if (frameMs > 50.0 && frameNo_ > 3)
            LOG_INFO("wfc spike: frame %d total %.1f ms, render span %.1f ms", frameNo_, frameMs, thisRenderMs);
    }
    if (std::getenv("WFC_RENDERSTATS")) {          // CPU frame-to-frame time, logged every 120 frames
        auto g0 = std::chrono::steady_clock::now();   // diagnostics only: wait for the GPU on the scene
        glFinish();
        double gms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g0).count();
        gStats.gpuMs += gms;
        if (gms > 20.0 && frameNo_ > 3) LOG_INFO("wfc spike: frame %d gpu/driver wait %.1f ms", frameNo_, gms);
        static auto last = std::chrono::steady_clock::now();
        static int frames = 0; static double acc = 0;
        auto now = std::chrono::steady_clock::now();
        acc += std::chrono::duration<double, std::milli>(now - last).count(); last = now;
        if (++frames == 120) {
            LOG_INFO("wfc: dynamic meshes: %.1f calls (%.1f culled, %.1f unchanged poses reused), total %.2f ms = vertex build "
                     "%.2f + upload %.2f + shadow %.2f + rest per frame", gStats.dynCalls / 120.0, gStats.dynCulled / 120.0,
                     gStats.dynReused / 120.0, gStats.dynTotalMs / 120.0,
                     gStats.dynBuildMs / 120.0, gStats.dynUploadMs / 120.0, gStats.dynShadowMs / 120.0);
            {
                unsigned long long sent = 0, skipped = 0;
                glx::uniformCacheStats(sent, skipped);
                if (sent + skipped)
                    LOG_INFO("wfc: uniform calls per frame: %.0f sent, %.0f skipped as redundant (%.0f%%)", sent / 120.0,
                             skipped / 120.0, 100.0 * (double)skipped / (double)(sent + skipped));
            }
            LOG_INFO("wfc: dynamic draw split per frame: light env %.2f ms, sub setup %.2f ms, drawSubs %.2f ms (shadow %.2f)",
                     gStats.dynEnvMs / 120.0, gStats.dynSubsMs / 120.0, gStats.dynDrawMs / 120.0, gStats.dynShadowMs / 120.0);
            if (gStats.instQueued || gStats.instRejected)
                LOG_INFO("wfc: instanced character subs per frame: %.1f queued in %.1f instanced draws, %.1f drawn singly",
                         gStats.instQueued / 120.0, gStats.instDraws / 120.0, gStats.instRejected / 120.0);
            if (gStats.skinCalls)
                LOG_INFO("wfc: GPU-skinned draws: %.1f calls, %.2f ms per frame (incl. the dynamic draw), palette uploads %.1f "
                         "(exact bounds + upload %.2f ms)", gStats.skinCalls / 120.0, gStats.skinMs / 120.0,
                         gStats.skinUploads / 120.0, gStats.skinBoundsMs / 120.0);
            LOG_INFO("wfc: DirectLightEnv per frame: %.2f updates (%.4f ms), %.2f volume queries (%.4f ms), %.2f shadow rays",
                     statEnvCalls_ / 120.0, statUpdateMs_ / 120.0, statLvvQueries_ / 120.0, statLvvMs_ / 120.0,
                     statVisCalls_ / 120.0);
            LOG_INFO("wfc: ShadowMask per frame: %.2f projections, %.2f gated", statShadowProj_ / 120.0, statShadowGated_ / 120.0);
            statShadowProj_ = statShadowGated_ = 0;
            LOG_INFO("wfc: map FX per frame: %.3f ms (simulation %.3f, draw %.3f), %.1f sprites, %.1f mesh particles",
                     statFxMs_ / 120.0, statFxTickMs_ / 120.0, (statFxMs_ - statFxTickMs_) / 120.0,
                     statFxSprites_ / 120.0, statFxMeshes_ / 120.0);
            {
                std::vector<std::pair<int, std::string>> top;
                for (const auto& kv : statFxSpawns_) top.push_back({kv.second, kv.first});
                std::sort(top.rbegin(), top.rend());
                std::string list;
                for (size_t i = 0; i < top.size() && i < 5; ++i)
                    list += (i ? ", " : "") + top[i].second.substr(top[i].second.rfind('.') + 1) + " x" + std::to_string(top[i].first);
                LOG_INFO("wfc: map FX instances live %zu; runtime spawns in 120 frames: %s; sprite draws %.1f (%.1f batches "
                     "merged into them) per frame", fxInstances_.size(), list.empty() ? "none" : list.c_str(),
                     statSpriteBatches_ / 120.0, statSpriteMerged_ / 120.0);
            statSpriteBatches_ = statSpriteMerged_ = 0;
                statFxSpawns_.clear();
            }
            statFxMs_ = 0.0; statFxTickMs_ = 0.0; statFxSprites_ = statFxMeshes_ = 0;
            statEnvCalls_ = statVisCalls_ = statLvvQueries_ = 0; statLvvMs_ = 0.0; statUpdateMs_ = 0.0;
            LOG_INFO("wfc: avg frame %.2f ms (%.0f fps); scene submit %.2f ms, gpu wait %.2f ms; per frame: %.1f draws, "
                     "%.1f light envs (%.2f ms), %.1f visibility traces",
                     acc / frames, 1000.0 * frames / acc, gStats.renderMs / 120.0, gStats.gpuMs / 120.0, gStats.draws / 120.0, gStats.envCalls / 120.0, gStats.envMs / 120.0,
                     gStats.visCalls / 120.0);
            frames = 0; acc = 0; gStats = RenderStats{};
        }
    }
    if (std::getenv("WFC_SHADOWSELFTEST") && frameNo_ == 3) runShadowMaskSelfTest();
    if (distUsed_) applyDistortion();
    if (std::getenv("WFC_FRAMEREPORT") && std::getenv("WFC_SMOKE_FRAMES") &&
        frameNo_ == (int)std::atol(std::getenv("WFC_SMOKE_FRAMES")))
        writeFrameReport();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_FOG);
    BindVertexArray(postVao_);
    ActiveTexture(GL_TEXTURE0);
    // Authored settings (TnWorldInfo over Default__WorldInfo), see load(). The quarter-res
    // gather/blur carries both bloom and the DOF-blurred scene, as in UE3's DOFAndBloom effect.
    bool dof = post_.dof && !std::getenv("WFC_NODOF");
    bool bloom = bloomGatherProg_ && blurProg_ && ((post_.bloom && !std::getenv("WFC_NOBLOOM")) || dof);
    if (bloom) {
        BindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[0]);
        glViewport(0, 0, bloomW_, bloomH_);
        UseProgram(bloomGatherProg_);
        auto G = [&](const char* n) { return GetUniformLocation(bloomGatherProg_, n); };
        ActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, depthTex_);
        ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, colorTex_);
        Uniform1i(G("uScene"), 0);
        Uniform1i(G("uDepth"), 1);
        Uniform2f(G("uTexel"), 1.0f / (float)fbW_, 1.0f / (float)fbH_);
        Uniform1f(G("uBloomScale"), (post_.bloom && !std::getenv("WFC_NOBLOOM")) ? post_.bloomScale : 0.0f);
        Uniform1f(G("uBloomThreshold"), post_.bloomThreshold);
        Uniform4f(G("uDofPacked"), post_.dofPacked[0], post_.dofPacked[1], post_.dofPacked[2], post_.dofPacked[3]);
        Uniform2f(G("uDofMaxBlur"), post_.dofMaxBlur[0], post_.dofMaxBlur[1]);
        Uniform1i(G("uDofOn"), dof ? 1 : 0);
        Uniform2f(G("uNearFar"), znear_, zfar_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        UseProgram(blurProg_);
        Uniform1i(GetUniformLocation(blurProg_, "uSrc"), 0);
        for (int pass = 0; pass < 2; ++pass) {
            BindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[1 - pass]);
            glBindTexture(GL_TEXTURE_2D, bloomTex_[pass]);
            Uniform2f(GetUniformLocation(blurProg_, "uStep"), pass == 0 ? 1.0f / (float)bloomW_ : 0.0f,
                      pass == 0 ? 0.0f : 1.0f / (float)bloomH_);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }
    BindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, vpW_, vpH_);
    UseProgram(postProg_);
    ActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, bloomTex_[0]);
    ActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, depthTex_);
    ActiveTexture(GL_TEXTURE0 + 3); glBindTexture(GL_TEXTURE_3D, clutTex_);
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, colorTex_);
    auto U = [&](const char* n) { return GetUniformLocation(postProg_, n); };
    Uniform1i(U("uScene"), 0);
    Uniform1i(U("uBloom"), 1);
    Uniform1i(U("uDepth"), 2);
    Uniform1i(U("uClut"), 3);
    Uniform1i(U("uBloomOn"), bloom ? 1 : 0);
    Uniform1i(U("uDofOn"), dof ? 1 : 0);
    Uniform4f(U("uDofPacked"), post_.dofPacked[0], post_.dofPacked[1], post_.dofPacked[2], post_.dofPacked[3]);
    Uniform2f(U("uDofMaxBlur"), post_.dofMaxBlur[0], post_.dofMaxBlur[1]);
    Uniform2f(U("uNearFar"), znear_, zfar_);
    bool clut = clutTex_ && !std::getenv("WFC_NOCLUT");
    Uniform1i(U("uClutOn"), clut ? 1 : 0);
    // UE3 ColorCorrectionTexCoordScaleBias for an N^3 LUT: scale (N-1)/N, bias 0.5/N
    Uniform2f(U("uClutScaleBias"), (float)(clutSize_ - 1) / (float)clutSize_, 0.5f / (float)clutSize_);
    Uniform3f(U("uShadows"), post_.shadows.x, post_.shadows.y, post_.shadows.z);
    Uniform3f(U("uInvHighLights"), 1.0f / std::max(post_.highlights.x, 1e-4f), 1.0f / std::max(post_.highlights.y, 1e-4f),
              1.0f / std::max(post_.highlights.z, 1e-4f));
    Uniform3f(U("uMidTones"), post_.midtones.x, post_.midtones.y, post_.midtones.z);
    Uniform1f(U("uDesat"), post_.desat);
    Uniform1f(U("uInvGamma"), 1.0f / displayGamma_);     // Xe-TransEngine.ini DisplayGamma=2.2; profile Brightness
    glDrawArrays(GL_TRIANGLES, 0, 3);
    BindVertexArray(0);
    UseProgram(0);
    drawHudScreenEffect();                             // HUD post-process chain (over the frame, under canvas / GFx)
    drawCanvasTiles();
    ActiveTexture(GL_TEXTURE0 + 3); glBindTexture(GL_TEXTURE_3D, 0);
    ActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, 0);
    ActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, 0);
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

} // namespace wfc
} // namespace render
