#include "render/gl/WfcPipeline.h"
#include "core/LoadYield.h"
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

// ------------------------------------------------------------------------- GLSL sources
const char* kVS = R"(#version 330 compatibility
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aTan;
layout(location=3) in vec2 aUV0;
layout(location=4) in vec2 aUV1;
layout(location=5) in vec4 aColor;   // particle colour (FX draws); constant white otherwise
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
out vec3 vPos; out vec3 vNrm; out vec4 vTan; out vec2 vUV0; out vec2 vUV1; out vec4 vFog; out vec4 vColor;
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
    vec4 wp = uModel * vec4(aPos, 1.0);
    mat3 nm = mat3(uModel);
    vPos = wp.xyz;
    vNrm = nm * aNrm;
    vTan = vec4(nm * aTan.xyz, aTan.w);
    vUV0 = aUV0;
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
in vec3 vPos; in vec3 vNrm; in vec4 vTan; in vec2 vUV0; in vec2 vUV1; in vec4 vFog; in vec4 vColor;
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
vec4 wfcSceneColor(vec2 uv) { return texture(uSceneColor, uv); }
vec2 wfcScreenUV() { return gl_FragCoord.xy / vec2(textureSize(uSceneColor, 0)); }
struct MatIn { vec2 uv0; vec2 uv1; vec4 vertexColor; vec3 worldPosUE; vec3 cameraVector; vec3 reflectionVector;
               vec3 normal; mat3 tbnUE; float time; float pixelDepth; vec4 screenPos; float sceneDepth;
               vec4 dynParam; };
struct MatOut { vec3 Distortion; vec3 DiffuseColor; vec3 SpecularColor; float SpecularPower; vec3 Normal;
                vec3 EmissiveColor; float Opacity; float OpacityMask; vec3 CustomLighting; };
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
    m.uv0 = vUV0; m.uv1 = vUV1Mat; m.vertexColor = vColor;
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

GLuint compile(GLenum type, const std::string& src, const std::string& tag) {
    GLuint s = CreateShader(type);
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

// ------------------------------------------------------------------------- unloading (level travel)
void Pipeline::release() {
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
    for (GLuint id : progIds) { GLuint p = id; prog(p); }
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
    for (GLuint* v : {&dynVao_, &postVao_, &spriteVao_, &volVao_}) vao(*v);
    for (GLuint* b : {&dynVbo_, &dynIbo_, &spriteVbo_, &spriteCbo_, &spriteIbo_, &volVbo_}) buf(*b);
    for (GLuint* p : {&postProg_, &bloomGatherProg_, &blurProg_, &distApplyProg_, &shadowProjProg_, &maskDepthProg_,
                      &constProg_, &maskBlurProg_})
        prog(*p);
    BindFramebuffer(GL_FRAMEBUFFER, 0);
    LOG_INFO("wfc: released map render data (%zu meshes, %zu programs, %zu textures)", meshes_.size(), progIds.size(),
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
    lastLoadError().clear();

    assets::Json M, L;
    if (!assets::Json::parse(mj, M) || !assets::Json::parse(lj, L)) { LOG_ERROR("wfc: render data JSON parse failed"); return false; }

    for (const auto& kv : M.obj) {
        const assets::Json& e = kv.second;
        if (!e["glsl"].isString()) continue;
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
        if (!on || frame <= 2) return;
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
    return id;
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
    GLuint f = compile(GL_FRAGMENT_SHADER, fs, key);
    if (!vsShared || !f) return -1;
    GLuint id = link(vsShared, f, key);
    DeleteShader(f);
    if (!id) return -1;
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
    Uniform1i(U("uSceneDepth"), 12);
    Uniform1i(U("uSceneColor"), 16);
    Uniform1i(U("uVLM"), 11);
    Uniform1i(U("uShadowMask"), 10);
    UseProgram(0);
    progs_.push_back(P);
    progIndex_[key] = (int)progs_.size() - 1;
    return (int)progs_.size() - 1;
}

// Raw umodel glTF meshes (e.g. CP_OptimusArm_SKEL, RB_OptimusWeaponArm_SKEL) carry the UE3 material
// object NAME, not its path: resolve it against the compiled original materials.
std::string Pipeline::resolveBySourceName(const Material* m) const {
    if (!m || m->sourceName.empty()) return std::string();
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
            if (!lightmapped && s.blend <= 1) {          // caster variant for projected shadows
                mainOverride_ = kFSMainShadow;
                int sh = buildProgram(key + "|SHADOW", s.glsl, slots, s.cube, s.blend, s.twoSided, false, s.clip, false, s.rtParams);
                mainOverride_ = nullptr;
                if (sh >= 0) { progs_[(size_t)sh].original = true; progs_[(size_t)r].shadowProg = sh; }
            }
            return r;
        }
        LOG_WARN("wfc: material %s failed to build; using glTF fallback", matName.c_str());
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
struct RenderStats { int envCalls = 0, visCalls = 0, draws = 0; double envMs = 0, renderMs = 0, gpuMs = 0, dynBuildMs = 0, dynUploadMs = 0; } gStats;
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
void Pipeline::buildVertices(const MeshData& m, std::vector<float>& v) {
    size_t n = m.vertexCount();
    std::vector<core::Vec3> tan(n, {0, 0, 0}), bit(n, {0, 0, 0});
    bool uv = m.hasUV();
    if (uv) {
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
        N = core::normalize(N);
        o[3] = N.x; o[4] = N.y; o[5] = N.z;
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
        d.first = s.indexOffset; d.count = s.indexCount;
        const Material* mat = (s.material >= 0 && (size_t)s.material < m.mats.size()) ? &m.mats[(size_t)s.material] : nullptr;
        if (!s.component.empty()) g.world = true;
        if (bspMesh_ >= 0 && s.component.rfind("bsp:", 0) == 0) { g.drawsBsp = true; continue; }
        std::string key = s.component;
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        if (key.rfind("actor:", 0) == 0) d.actor = key.substr(6);
        // authored bHidden: actor-placed nodes stay resident (Gameplay may unhide them, e.g. SeqAct_ToggleHidden
        // by game rule); collection components without an actor identity are dropped as before
        if (hiddenComponents_.count(key) && !std::getenv("WFC_SHOWHIDDEN") && d.actor.empty()) continue;
        d.noLights = noLightComponents_.count(key) > 0;
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
            } else {
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
        d.prog = programFor(matName, mat, lm);
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
    meshes_.push_back(std::move(g));
    LOG_INFO("wfc: uploaded mesh %zu: %zu verts, %zu submeshes (%d lightmapped, %d programs, %zu total)",
             meshes_.size() - 1, m.vertexCount(), subs.size(), nLM, nProg, progs_.size());
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
    Uniform1f(P.uTime, time_);
    Uniform1i(P.uTwoSided, P.twoSided ? 1 : 0);
    Uniform1i(P.uMasked, P.blend == 1 ? 1 : 0);
    Uniform1f(P.uClip, P.clip);
    Uniform1i(P.uLit, P.lit ? 1 : 0);
    Uniform1i(uloc(P, "uBlend"), P.blend);
    Uniform1i(uloc(P, "uVertexLM"), 0);
    Uniform4f(uloc(P, "uShadowDepth"), 0.0f, 0.0f, 0.0f, 0.0f);
    Uniform4f(uloc(P, "uDynParam"), dynParam_[0], dynParam_[1], dynParam_[2], dynParam_[3]);
    {   // shadow-mask inputs (neutral mask = 1 unless a mask is bound for this draw)
        static const float dsls = std::getenv("WFC_DSLS") ? std::min(std::max((float)std::atof(std::getenv("WFC_DSLS")), 0.0f), 1.0f) : 0.0f;
        Uniform1f(uloc(P, "uDSLS"), dsls);
        Uniform3f(uloc(P, "uDLAC"), 0.0f, 0.0f, 0.0f);   // set per environment in drawSubs
        ActiveTexture(GL_TEXTURE0 + 10);
        glBindTexture(GL_TEXTURE_2D, shadowMaskTexFor(dynamicMaskDraw_));
        Uniform2f(uloc(P, "uShadowMaskTexelOffset"), maskTexelOffset_[0], maskTexelOffset_[1]);
    }
    Uniform2f(uloc(P, "uNearFar"), znear_, zfar_);
    Uniform2f(uloc(P, "uViewport"), (float)std::max(vpW_, 1), (float)std::max(vpH_, 1));
    Uniform1i(uloc(P, "uHasSceneDepth"), P.sceneDepth ? 1 : 0);
    Uniform1f(uloc(P, "uCanvasInvGamma"), canvasInvGamma_);
    // per-draw runtime parameters: Canvas tiles pass their own; otherwise the material's Matinee-driven values
    // (setMaterialParam on its MaterialInstanceActor); unset = authored
    const std::vector<std::pair<std::string, std::array<float, 4>>>* params = drawParams_;
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
    static const int legacyTrans = std::getenv("WFC_M05TRANS") ? 1 : 0;
    Uniform1i(uloc(P, "uLegacyTrans"), legacyTrans);
    if (P.sceneDepth) { ensureSceneDepth(); ActiveTexture(GL_TEXTURE0 + 12); glBindTexture(GL_TEXTURE_2D, depthCopyTex_); }
    if (P.sceneColor) { ensureSceneColor(); ActiveTexture(GL_TEXTURE0 + 16); glBindTexture(GL_TEXTURE_2D, sceneCopyTex_); }
    VertexAttrib4f(5, fxColor_[0], fxColor_[1], fxColor_[2], fxColor_[3]);   // current value when unbound
    static const int dbg = std::getenv("WFC_LIGHTINGONLY") ? 1 : std::getenv("WFC_ALBEDO") ? 2 : 0;
    Uniform1i(uloc(P, "uDebug"), dbg);
    Uniform1i(P.uFogOn, fogOn_ ? 1 : 0);
    Uniform1f(P.uFogMaxH, fogMaxH_); Uniform1f(P.uFogScale, fogScale_);
    Uniform1f(P.uFogStart, fogStart_); Uniform1f(P.uFogExt, fogExt_);
    Uniform3f(P.uFogIn, fogIn_.x, fogIn_.y, fogIn_.z);
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
    std::vector<TransItem> q;
    q.swap(transQueue_);
    for (TransItem& t : q) t.fn();
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
    for (int pass = onlySub >= 0 ? 1 : 0; pass < 2; ++pass) {          // 0: opaque + masked, 1: translucent
        for (size_t si = 0; si < g.subs.size(); ++si) {
            if (onlySub >= 0 && (int)si != onlySub) continue;
            Sub& s = g.subs[si];
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
                const core::Mat4 mdl = model;
                const int sub = (int)si;
                transQueue_.push_back({viewDepth(c), [this, meshIdx, mdl, dynamicObject, sub, col, dyn, fx]() {
                    std::copy(col, col + 4, fxColor_);
                    std::copy(dyn, dyn + 4, dynParam_);
                    frameFx_ = fx;
                    drawSubs(meshes_[(size_t)meshIdx], mdl, dynamicObject, sub);
                    frameFx_ = false;
                    std::fill(fxColor_, fxColor_ + 4, 1.0f);
                    std::fill(dynParam_, dynParam_ + 4, 1.0f);
                }});
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
            if (g.world && !moving && bakedPlacement && !noFrustum) {
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
            glDrawElements(GL_TRIANGLES, (GLsizei)s.count, GL_UNSIGNED_INT, (void*)(size_t)(s.first * 4));
            ++gStats.draws;
            ++counts_.draws;
            if (meshIdx >= 0 && meshIdx == bspMesh_) ++counts_.bspDraws;
            else if (g.world) ++counts_.worldDraws;
            if (dynamicObject) ++counts_.dynamicDraws;
            if (frameFx_) ++counts_.fxDraws;
            if (trans) ++counts_.translucent;
            else { ++counts_.opaque; if (!glIsEnabled(GL_DEPTH_TEST)) ++counts_.opaqueNoDepthTest; }
            if (s.lmTex[0] >= 0 || s.vlmTex != 0) ++counts_.lightmapped;
            frameMats_.insert(s.matName);
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

void Pipeline::draw(int id, const core::Mat4& model) {
    if (id < 0 || (size_t)id >= meshes_.size()) return;
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
}

void Pipeline::drawDynamic(const MeshData& m, const core::Mat4& model) {
    if (m.empty()) return;
    std::vector<float> v;
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
    bool weapon = false;
    for (const Material& mt : m.mats) {
        if (mt.wfcName.find("_VEH_p.") != std::string::npos) { envSamples_ = &kVehicleSamples; envForm_ = 1; break; }
        if (mt.wfcName.find("_ROBO_p.") != std::string::npos) { envSamples_ = &kRobotSamples; envForm_ = 0; break; }
        if (mt.wfcName.rfind("WEP_", 0) == 0) { envSamples_ = &kRobotSamples; envForm_ = 0; weapon = true; break; }
    }
    if (envForm_ >= 0) envForm_ += 16 * drawOwner_;   // per character instance (owner 0: keys 0 / 1)
    if (envSamples_ && m.vertexCount() > 0) {          // world-space bounds of the posed mesh
        core::Vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
        for (size_t i = 0; i < m.vertexCount(); ++i) {
            core::Vec3 p{m.positions[i * 3], m.positions[i * 3 + 1], m.positions[i * 3 + 2]};
            mn = {std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z)};
            mx = {std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z)};
        }
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
    GpuMesh g;
    g.vao = dynVao_;
    std::vector<SubMesh> subs = m.subs;
    if (subs.empty()) { SubMesh s; s.indexOffset = 0; s.indexCount = (uint32_t)m.indices.size(); subs.push_back(s); }
    for (const SubMesh& s : subs) {
        const Material* mat = (s.material >= 0 && (size_t)s.material < m.mats.size()) ? &m.mats[(size_t)s.material] : nullptr;
        Sub d;
        d.first = s.indexOffset; d.count = s.indexCount;
        std::string mk = materialKey(mat);
        auto it = dynProgCache_.find(mk);
        if (it == dynProgCache_.end())
            it = dynProgCache_.emplace(mk, programFor(mat ? mat->wfcName : std::string(), mat, false)).first;
        d.prog = it->second;
        d.matName = mat ? (mat->wfcName.empty() ? resolveBySourceName(mat) : mat->wfcName) : std::string();
        g.subs.push_back(d);
    }
    if (envSamples_ && !weapon && !std::getenv("WFC_NOCHARSHADOWS")) {   // the environment's projector -> ShadowMask
        ShadowProjector scratch;
        if (const ShadowProjector* p = projectorFor(envForm_, scratch)) castCharacterShadow(g, model, *p);
    }
    dynamicMaskDraw_ = envSamples_ != nullptr;
    drawSubs(g, model, true);
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

bool Pipeline::drawSprites(const char* material, const Sprite* sp, size_t n, const core::Vec3& facing) {
    if (!material || !sp || n == 0) return false;
    static const bool immediateTrans = std::getenv("WFC_IMMEDIATETRANS") != nullptr || std::getenv("WFC_M05TRANS") != nullptr;
    if (deferTrans_ && !flushingTrans_ && !immediateTrans) {
        if (spriteProg_.count(material) && spriteProg_[material] < 0) return false;   // known fallback material
        core::Vec3 c{0, 0, 0};
        for (size_t i = 0; i < n; ++i) c = c + (sp[i].c[0] + sp[i].c[2]) * 0.5f;
        c = c * (1.0f / (float)n);
        std::vector<Sprite> copy(sp, sp + n);
        std::string mat = material;
        float dyn[4];
        std::copy(dynParam_, dynParam_ + 4, dyn);
        transQueue_.push_back({viewDepth(c), [this, copy, mat, facing, dyn]() {
            std::copy(dyn, dyn + 4, dynParam_);
            drawSprites(mat.c_str(), copy.data(), copy.size(), facing);
            std::fill(dynParam_, dynParam_ + 4, 1.0f);
        }});
        return true;
    }
    auto it = spriteProg_.find(material);
    if (it == spriteProg_.end()) {
        std::string nm = resolveName(material);
        int prog = nm.empty() ? -1 : programFor(nm, nullptr, false);
        if (prog >= 0 && !progs_[(size_t)prog].original) prog = -1;
        if (prog < 0) LOG_WARN("wfc: particle material %s not compiled; textured fallback", material);
        it = spriteProg_.emplace(material, prog).first;
    }
    if (it->second < 0) return false;
    if (!spriteVao_) {
        GenVertexArrays(1, &spriteVao_);
        GenBuffers(1, &spriteVbo_); GenBuffers(1, &spriteCbo_); GenBuffers(1, &spriteIbo_);
    }
    std::vector<float> v(n * 4 * 14), col(n * 4 * 4);
    std::vector<uint32_t> idx(n * 6);
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
    BindBuffer(GL_ELEMENT_ARRAY_BUFFER, spriteIbo_);
    BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idx.size() * 4), idx.data(), GL_STREAM_DRAW);
    BindVertexArray(0);
    GpuMesh g;
    g.vao = spriteVao_;
    Sub d;
    d.first = 0; d.count = (uint32_t)idx.size(); d.prog = it->second;
    d.matName = material;                                  // diagnostics (WFC_SKIPMAT)
    g.subs.push_back(d);
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
    ++frameNo_;
    counts_ = FrameCounts();
    sceneColorCopied_ = false;
    frameMats_.clear();
    frameNoProg_.clear();
    progSeen_.assign(progs_.size(), 0);
    if (frameNo_ == 2) {
        // Prewarm: build every compiled original material not yet used (effect, weapon, character
        // materials) and decode its textures now, as the original had them resident from the map's
        // cooked packages before combat -- instead of on first draw (the first-shot hitch).
        auto t0 = std::chrono::steady_clock::now();
        int built = 0;
        for (const auto& kv : mats_) {
            if (progIndex_.count(kv.first + "|UBER") || progIndex_.count(kv.first + "|LM")) continue;
            // roster character materials (TR_ packages) compile when their character is first drawn: the render data
            // carries every MP chassis, only the ones in the match are needed
            if (kv.first.rfind("TR_", 0) == 0) continue;
            if (programFor(kv.first, nullptr, false) >= 0) ++built;
        }
        LOG_INFO("wfc: prewarmed %d material programs in %.0f ms", built,
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
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
    transQueue_.clear();
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
    if (uiTiles_.empty()) return;
    std::vector<IRenderer::MaterialTile> tiles;
    tiles.swap(uiTiles_);
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
    for (const IRenderer::MaterialTile& t : tiles) {
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
        counts_.materials = (int)frameMats_.size();
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
            LOG_INFO("wfc: dynamic meshes: vertex build %.2f ms, upload %.2f ms per frame", gStats.dynBuildMs / 120.0,
                     gStats.dynUploadMs / 120.0);
            LOG_INFO("wfc: DirectLightEnv per frame: %.2f updates (%.4f ms), %.2f volume queries (%.4f ms), %.2f shadow rays",
                     statEnvCalls_ / 120.0, statUpdateMs_ / 120.0, statLvvQueries_ / 120.0, statLvvMs_ / 120.0,
                     statVisCalls_ / 120.0);
            LOG_INFO("wfc: ShadowMask per frame: %.2f projections, %.2f gated", statShadowProj_ / 120.0, statShadowGated_ / 120.0);
            statShadowProj_ = statShadowGated_ = 0;
            LOG_INFO("wfc: map FX per frame: %.3f ms, %.1f sprites, %.1f mesh particles", statFxMs_ / 120.0,
                     statFxSprites_ / 120.0, statFxMeshes_ / 120.0);
            statFxMs_ = 0.0; statFxSprites_ = statFxMeshes_ = 0;
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
