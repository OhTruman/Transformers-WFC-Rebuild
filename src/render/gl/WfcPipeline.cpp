#include "render/gl/WfcPipeline.h"
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
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform vec3 uCamPos;
uniform vec4 uLMCoord;
uniform int uFogOn;
uniform float uFogMaxH, uFogScale, uFogStart, uFogExt;
uniform vec3 uFogIn;
out vec3 vPos; out vec3 vNrm; out vec4 vTan; out vec2 vUV0; out vec2 vUV1; out vec4 vFog; out vec4 vColor;

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
    vColor = vec4(1.0);
    vFog = heightFog(wp.xyz);
    gl_Position = uViewProj * wp;
}
)";

const char* kFSHead = R"(#version 330 compatibility
in vec3 vPos; in vec3 vNrm; in vec4 vTan; in vec2 vUV0; in vec2 vUV1; in vec4 vFog; in vec4 vColor;
layout(location=0) out vec4 oColor;
uniform vec3 uCamPos;
uniform float uTime;
uniform int uTwoSided;
uniform int uMasked;
uniform float uClip;
uniform int uLit;
uniform int uDebug;          // 1 = lighting only (diffuse 0.5 grey, no emissive)
struct MatIn { vec2 uv0; vec2 uv1; vec4 vertexColor; vec3 worldPosUE; vec3 cameraVector; vec3 reflectionVector;
               vec3 normal; mat3 tbnUE; float time; float pixelDepth; vec4 screenPos; };
struct MatOut { vec3 DiffuseColor; vec3 SpecularColor; float SpecularPower; vec3 Normal; vec3 EmissiveColor;
                float Opacity; float OpacityMask; vec3 CustomLighting; };
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
    m.uv0 = vUV0; m.uv1 = vUV1; m.vertexColor = vColor;
    m.worldPosUE = vPos.xzy * 100.0;                     // glTF metres -> UE units/axes
    vec3 V = normalize(uCamPos - vPos);
    m.cameraVector = vec3(dot(V, T), dot(V, B), dot(V, N));
    m.normal = vec3(0.0, 0.0, 1.0);
    m.reflectionVector = -m.cameraVector + vec3(0.0, 0.0, 2.0 * m.cameraVector.z);
    m.tbnUE = mat3(T.xzy, B.xzy, N.xzy);
    m.time = uTime;
    m.pixelDepth = length(uCamPos - vPos) * 100.0;
    m.screenPos = vec4(gl_FragCoord.xy, 0.0, 1.0);
    return m;
}
)";

// FDirectionalTextureLightMapPolicy base pass (decoded from the original microcode).
const char* kFSMainLM = R"(
uniform sampler2D uLM0; uniform sampler2D uLM1; uniform sampler2D uLM2;
uniform vec3 uLMScale[3];
const vec3 LMB0 = vec3(0.0, 0.81649658, 0.57735027);
const vec3 LMB1 = vec3(-0.70710678, -0.40824829, 0.57735027);
const vec3 LMB2 = vec3(0.70710678, -0.40824829, 0.57735027);
void main() {
    mat3 tbn; MatIn m = wfcBuildInput(tbn);
    MatOut o; wfcMaterial(m, o);
    if (uMasked != 0 && o.OpacityMask - uClip < 0.0) discard;
    if (uDebug == 1) { o.DiffuseColor = vec3(0.5); o.EmissiveColor = vec3(0.0); o.SpecularColor = vec3(0.0); }
    if (uDebug == 2) { oColor = vec4(o.DiffuseColor, 1.0); return; }
    vec3 c = o.EmissiveColor;
    if (uLit != 0) {
        vec3 n = normalize(o.Normal);
        float w0 = dot(n, LMB0), w1 = dot(n, LMB1), w2 = dot(n, LMB2);
        vec3 L = w0 * w0 * texture(uLM0, vUV1).rgb * uLMScale[0]
               + w1 * w1 * texture(uLM1, vUV1).rgb * uLMScale[1]
               + w2 * w2 * texture(uLM2, vUV1).rgb * uLMScale[2];
        c += o.DiffuseColor * L;
    }
    c = c * vFog.a + vFog.rgb;
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
void main() {
    mat3 tbn; MatIn m = wfcBuildInput(tbn);
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
        c += o.DiffuseColor * amb;
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
            c += (o.DiffuseColor * (wr * wr) + o.SpecularColor * sp) * uLCol[i].rgb * att * uLSpot[i].w;
        }
    }
    c = c * vFog.a + vFog.rgb;
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
// to [0,4]; a tap contributes only if any channel exceeds BloomThreshold; sum * 0.25 * BloomScale.
const char* kBloomGatherFS = R"(#version 330 compatibility
in vec2 vUV;
uniform sampler2D uScene;
uniform vec2 uTexel;
uniform float uBloomScale, uBloomThreshold;
layout(location=0) out vec4 oColor;
vec3 tap(vec2 o) {
    vec3 c = clamp(texture(uScene, vUV + o * uTexel).rgb, 0.0, 4.0);
    return any(greaterThan(c, vec3(uBloomThreshold))) ? c : vec3(0.0);
}
void main() {
    vec3 b = tap(vec2(-1.0, -1.0)) + tap(vec2(1.0, -1.0)) + tap(vec2(-1.0, 1.0)) + tap(vec2(1.0, 1.0));
    oColor = vec4(b * 0.25 * uBloomScale, 1.0);
}
)";

// Separable Gaussian (UE3 DOFAndBloom blur). [PROV] kernel: radius = DOF_BlurKernelSize (16 px
// full-res, WorldInfo default) / 4 at quarter resolution, sigma = radius / 2.
const char* kBlurFS = R"(#version 330 compatibility
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uStep;
layout(location=0) out vec4 oColor;
void main() {
    const int R = 4;
    float sigma = float(R) * 0.5;
    vec3 acc = vec3(0.0); float wsum = 0.0;
    for (int i = -R; i <= R; ++i) {
        float w = exp(-0.5 * float(i * i) / (sigma * sigma));
        acc += texture(uSrc, vUV + uStep * float(i)).rgb * w; wsum += w;
    }
    oColor = vec4(acc / wsum, 1.0);
}
)";

// FUberPostProcessPixelShader (decoded): (scene + bloom) -> sat(x - SceneShadows) * InvHighLights
// -> pow(MidTones) -> x*(1-Desat) + lum*Desat + Overlay -> sat(* ColorScale) -> pow(1/DisplayGamma).
// Streets has no PostProcessVolume and the persistent level is a stub in the dump, so WorldInfo
// defaults apply: Shadows 0, HighLights 1, MidTones 1, Desaturation 0, identity CLUT, gamma 2.2.
const char* kPostFS = R"(#version 330 compatibility
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform int uBloomOn;
uniform vec3 uShadows, uInvHighLights, uMidTones;
uniform float uDesat;
uniform float uInvGamma;
layout(location=0) out vec4 oColor;
void main() {
    vec3 c = texture(uScene, vUV).rgb;
    if (uBloomOn != 0) c += texture(uBloom, vUV).rgb;
    c = clamp(c - uShadows, 0.0, 1.0) * uInvHighLights;
    c = pow(max(c, vec3(0.0001)), uMidTones);
    c = c * (1.0 - uDesat) + vec3(dot(c, vec3(0.3, 0.59, 0.11)) * uDesat);
    c = clamp(c, 0.0, 1.0);
    c = pow(max(c, vec3(0.0001)), vec3(uInvGamma));
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

// ------------------------------------------------------------------------- loading
bool Pipeline::load(const std::string& mapName) {
    if (std::getenv("WFC_LEGACYRENDER")) { LOG_INFO("wfc: WFC_LEGACYRENDER set; shader path disabled"); return false; }
    std::string root;
    if (const char* e = std::getenv("WFC_RENDER_DATA")) root = e;
    else root = exeDir() + "/../../work/render";
    dataDir_ = root + "/" + mapName;
    std::string mj = readText(dataDir_ + "/materials_glsl.json");
    std::string lj = readText(dataDir_ + "/lighting.json");
    if (mj.empty() || lj.empty()) {
        LOG_WARN("wfc: render data not found in %s (run tools/render/*.py); legacy renderer", dataDir_.c_str());
        return false;
    }
    if (!glx::load()) { LOG_WARN("wfc: GL 3.3 entry points unavailable; legacy renderer"); return false; }

    assets::Json M, L;
    if (!assets::Json::parse(mj, M) || !assets::Json::parse(lj, L)) { LOG_ERROR("wfc: render data JSON parse failed"); return false; }

    for (const auto& kv : M.obj) {
        const assets::Json& e = kv.second;
        if (!e["glsl"].isString()) continue;
        MatSrc s;
        s.glsl = e["glsl"].asString();
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
        if (assets::loadGlb(dataDir_ + "/bsp.glb", bsp)) bspMesh_ = upload(bsp);
        else LOG_WARN("wfc: bsp.glb missing; level BSP stays unlit");
    }
    LOG_INFO("wfc: shader path active: %zu materials, %zu lightmapped components, %zu lights, fog %s (%s)",
             mats_.size(), lightmaps_.size(), lights_.size(), fogOn_ ? "on" : "off", dataDir_.c_str());
    return true;
}

GLuint Pipeline::texture(const std::string& file, bool srgb, bool clampU, bool clampV) {
    std::string key = file + (srgb ? "|s" : "|l") + (clampU ? "c" : "w") + (clampV ? "c" : "w");
    auto it = texCache_.find(key);
    if (it != texCache_.end()) return it->second;
    ImageData img;
    GLuint id = 0;
    if (!file.empty() && platform::decodeImage(file, img) && img.valid()) {
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA, img.w, img.h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, img.rgba.data());
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
        if (ok) glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA, img.w, img.h, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
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
                           bool lightmapped) {
    std::string fs = kFSHead;
    std::string code = body;
    for (size_t k = 0; k < slots.size(); ++k) {
        std::string n = std::to_string(k);
        if (slotIsCube[k]) {
            fs += "uniform samplerCube uTex" + n + ";\n";
            // UE world direction -> cube lookup (faces in UE/D3D order, see cubeTexture)
            fs += "vec4 wfcSC_" + n + "(vec3 d) { return texture(uTex" + n + ", d); }\n";
            replaceAll(code, "wfcSampleCube(" + n + ", ", "wfcSC_" + n + "(");
        } else {
            fs += "uniform sampler2D uTex" + n + "; uniform vec4 uUnpackMin" + n + "; uniform vec4 uUnpackScale" + n + ";\n";
            fs += "vec4 wfcS2D_" + n + "(vec2 uv) { return texture(uTex" + n + ", uv) * uUnpackScale" + n +
                  " + uUnpackMin" + n + "; }\n";
            replaceAll(code, "wfcSample2D(" + n + ", ", "wfcS2D_" + n + "(");
        }
    }
    fs += "void wfcMaterial(in MatIn m, out MatOut o) {\n" + code + "\n}\n";
    fs += kFSPrologue;
    fs += lightmapped ? kFSMainLM : kFSMainUber;

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
    P.slots = slots;
    P.blend = blend; P.twoSided = twoSided; P.lit = lit; P.clip = clip;
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
    UseProgram(0);
    progs_.push_back(P);
    progIndex_[key] = (int)progs_.size() - 1;
    return (int)progs_.size() - 1;
}

int Pipeline::programFor(const std::string& matName, const Material* gm, bool lightmapped) {
    std::string key = (matName.empty() ? std::string("<gltf>") : matName) + (lightmapped ? "|LM" : "|UBER");
    static const bool gltfOnly = std::getenv("WFC_GLTFMATERIALS") != nullptr;   // A/B: AssetTools bakes
    auto mit = (gltfOnly && !lightmapped) ? mats_.end() : mats_.find(matName);
    if (mit == mats_.end() && gm) {
        // No compiled original graph: build from the glTF material (character/weapon textures
        // baked by AssetTools from the original customization shader).
        char buf[64]; std::snprintf(buf, sizeof buf, "|%p", (const void*)gm);
        key += buf;
    }
    auto pit = progIndex_.find(key);
    if (pit != progIndex_.end()) return pit->second;

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
        int r = buildProgram(key, s.glsl, slots, s.cube, s.blend, s.twoSided, s.lit, s.clip, lightmapped);
        if (r >= 0) return r;
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
void Pipeline::computeEnv(const core::Vec3& p, bool dynamicObject, LightEnv& env) const {
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
        if (vis_ && c.l->castShadows) {
            core::Vec3 from = p + core::Vec3{0, 0.05f, 0};
            core::Vec3 to = c.l->type == 2 ? from + c.L * 300.0f : c.l->pos;
            if (vis_(from, to)) vis = 0.0f;
        }
        if (vis <= 0.0f) continue;
        if (env.n < maxDirect) {
            int i = env.n++;
            const Light& l = *c.l;
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
        o[12] = hasUV1 ? m.uv1[i * 2] : 0.0f; o[13] = hasUV1 ? m.uv1[i * 2 + 1] : 0.0f;
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
    for (const SubMesh& s : subs) {
        Sub d;
        d.first = s.indexOffset; d.count = s.indexCount;
        const Material* mat = (s.material >= 0 && (size_t)s.material < m.mats.size()) ? &m.mats[(size_t)s.material] : nullptr;
        if (!s.component.empty()) g.world = true;
        if (bspMesh_ >= 0 && s.component.rfind("bsp:", 0) == 0) { g.drawsBsp = true; continue; }
        std::string key = s.component;
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        auto lit = lightmaps_.find(key);
        bool lm = lit != lightmaps_.end() && m.hasUV1() && !std::getenv("WFC_NOLIGHTMAP");
        if (lm) {
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
        d.prog = programFor(mat ? mat->wfcName : std::string(), mat, lm);
        if (d.prog >= 0) ++nProg;
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
    }
    meshes_.push_back(std::move(g));
    LOG_INFO("wfc: uploaded mesh %zu: %zu verts, %zu submeshes (%d lightmapped, %d programs, %zu total)",
             meshes_.size() - 1, m.vertexCount(), subs.size(), nLM, nProg, progs_.size());
    return (int)meshes_.size() - 1;
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
    static const int dbg = std::getenv("WFC_LIGHTINGONLY") ? 1 : std::getenv("WFC_ALBEDO") ? 2 : 0;
    Uniform1i(GetUniformLocation(P.id, "uDebug"), dbg);
    Uniform1i(P.uFogOn, fogOn_ ? 1 : 0);
    Uniform1f(P.uFogMaxH, fogMaxH_); Uniform1f(P.uFogScale, fogScale_);
    Uniform1f(P.uFogStart, fogStart_); Uniform1f(P.uFogExt, fogExt_);
    Uniform3f(P.uFogIn, fogIn_.x, fogIn_.y, fogIn_.z);
    for (const Program::Slot& s : P.slots) {
        ActiveTexture(GL_TEXTURE0 + s.unit);
        glBindTexture(s.cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, s.tex);
    }
}

void Pipeline::drawSubs(GpuMesh& g, const core::Mat4& model, bool dynamicObject) {
    BindVertexArray(g.vao);
    core::Vec3 origin{model.m[12], model.m[13], model.m[14]};
    LightEnv dynEnv;
    bool dynEnvReady = false;
    for (int pass = 0; pass < 2; ++pass) {          // 0: opaque + masked, 1: translucent
        for (Sub& s : g.subs) {
            if (s.prog < 0) continue;
            const Program& P = progs_[(size_t)s.prog];
            bool trans = P.blend >= 2;
            if ((pass == 1) != trans) continue;
            if (g.world) {   // frustum cull (world-space bounds)
                bool out = false;
                for (int f = 0; f < 6 && !out; ++f) {
                    const float* pl = frustum_[f];
                    core::Vec3 pv{pl[0] >= 0 ? s.bmax.x : s.bmin.x, pl[1] >= 0 ? s.bmax.y : s.bmin.y,
                                  pl[2] >= 0 ? s.bmax.z : s.bmin.z};
                    if (pl[0] * pv.x + pl[1] * pv.y + pl[2] * pv.z + pl[3] < 0) out = true;
                }
                if (out) continue;
            }
            bindCommon(P, model);
            if (P.twoSided) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
            switch (P.blend) {
                case 2: glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); break;
                case 3: glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); glDepthMask(GL_FALSE); break;
                case 4: glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR, GL_ZERO); glDepthMask(GL_FALSE); break;
                default: glDisable(GL_BLEND); glDepthMask(GL_TRUE); break;
            }
            if (s.lmTex[0] >= 0) {
                Uniform4f(P.uLMCoord, s.lmCoord[0], s.lmCoord[1], s.lmCoord[2], s.lmCoord[3]);
                Uniform3fv(P.uLMScale, 3, &s.lmScale[0][0]);
                for (int i = 0; i < 3; ++i) {
                    ActiveTexture(GL_TEXTURE0 + 13 + i);
                    glBindTexture(GL_TEXTURE_2D, lmTextures_[(size_t)s.lmTex[i]] ? lmTextures_[(size_t)s.lmTex[i]] : blackTex_);
                }
            } else {
                Uniform4f(P.uLMCoord, 1, 1, 0, 0);
                const LightEnv* env;
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
                    if (!s.envReady) { computeEnv((s.bmin + s.bmax) * 0.5f, false, s.env); s.envReady = true; }
                    env = &s.env;
                }
                Uniform3fv(P.uAmb, 6, &env->cube[0].x);
                Uniform1i(P.uNumLights, env->n);
                Uniform4fv(P.uLPos, 3, &env->pos[0][0]);
                Uniform4fv(P.uLDir, 3, &env->dir[0][0]);
                Uniform4fv(P.uLCol, 3, &env->col[0][0]);
                Uniform4fv(P.uLSpot, 3, &env->spot[0][0]);
            }
            glDrawElements(GL_TRIANGLES, (GLsizei)s.count, GL_UNSIGNED_INT, (void*)(size_t)(s.first * 4));
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
}

void Pipeline::drawDynamic(const MeshData& m, const core::Mat4& model) {
    if (m.empty()) return;
    std::vector<float> v;
    buildVertices(m, v);
    BindVertexArray(dynVao_);
    BindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW);
    BindBuffer(GL_ELEMENT_ARRAY_BUFFER, dynIbo_);
    BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(m.indices.size() * 4), m.indices.data(), GL_STREAM_DRAW);
    setupAttribs();
    BindVertexArray(0);
    GpuMesh g;
    g.vao = dynVao_;
    std::vector<SubMesh> subs = m.subs;
    if (subs.empty()) { SubMesh s; s.indexOffset = 0; s.indexCount = (uint32_t)m.indices.size(); subs.push_back(s); }
    for (const SubMesh& s : subs) {
        const Material* mat = (s.material >= 0 && (size_t)s.material < m.mats.size()) ? &m.mats[(size_t)s.material] : nullptr;
        Sub d;
        d.first = s.indexOffset; d.count = s.indexCount;
        auto it = dynProgCache_.find(mat);
        if (it == dynProgCache_.end())
            it = dynProgCache_.emplace(mat, programFor(mat ? mat->wfcName : std::string(), mat, false)).first;
        d.prog = it->second;
        g.subs.push_back(d);
    }
    drawSubs(g, model, true);
}

// ------------------------------------------------------------------------- frame
void Pipeline::ensureTargets(int w, int h) {
    if (fbo_ && w == fbW_ && h == fbH_) return;
    if (!fbo_) { GenFramebuffers(1, &fbo_); glGenTextures(1, &colorTex_); GenRenderbuffers(1, &depthRb_); }
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    BindRenderbuffer(GL_RENDERBUFFER, depthRb_);
    RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);
    FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb_);
    if (CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) LOG_ERROR("wfc: HDR framebuffer incomplete");
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
    static auto t0 = std::chrono::steady_clock::now();
    time_ = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
    vpW_ = w; vpH_ = h;
    ensureTargets(std::max(w, 1), std::max(h, 1));
    BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w, h);
    // Clear to the fog in-scatter colour: what an infinitely distant ray converges to.
    glClearColor(fogIn_.x, fogIn_.y, fogIn_.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    camPos_ = cam.pos;
    viewProj_ = cam.proj() * cam.view();
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

void Pipeline::endFrame() {
    if (std::getenv("WFC_RENDERSTATS")) {          // CPU frame-to-frame time, logged every 120 frames
        static auto last = std::chrono::steady_clock::now();
        static int frames = 0; static double acc = 0;
        auto now = std::chrono::steady_clock::now();
        acc += std::chrono::duration<double, std::milli>(now - last).count(); last = now;
        if (++frames == 120) { LOG_INFO("wfc: avg frame %.2f ms (%.0f fps)", acc / frames, 1000.0 * frames / acc); frames = 0; acc = 0; }
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_FOG);
    BindVertexArray(postVao_);
    ActiveTexture(GL_TEXTURE0);
    // [CONF] WorldInfo DefaultPostProcessSettings: bEnableBloom, Bloom_Scale 1.0, Bloom_Threshold 1.0.
    bool bloom = bloomGatherProg_ && blurProg_ && !std::getenv("WFC_NOBLOOM");
    if (bloom) {
        BindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[0]);
        glViewport(0, 0, bloomW_, bloomH_);
        UseProgram(bloomGatherProg_);
        glBindTexture(GL_TEXTURE_2D, colorTex_);
        Uniform1i(GetUniformLocation(bloomGatherProg_, "uScene"), 0);
        Uniform2f(GetUniformLocation(bloomGatherProg_, "uTexel"), 1.0f / (float)fbW_, 1.0f / (float)fbH_);
        Uniform1f(GetUniformLocation(bloomGatherProg_, "uBloomScale"), 1.0f);
        Uniform1f(GetUniformLocation(bloomGatherProg_, "uBloomThreshold"), 1.0f);
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
    ActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, bloomTex_[0]);
    ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    auto U = [&](const char* n) { return GetUniformLocation(postProg_, n); };
    Uniform1i(U("uScene"), 0);
    Uniform1i(U("uBloom"), 1);
    Uniform1i(U("uBloomOn"), bloom ? 1 : 0);
    Uniform3f(U("uShadows"), 0.0f, 0.0f, 0.0f);          // Scene_Shadows
    Uniform3f(U("uInvHighLights"), 1.0f, 1.0f, 1.0f);    // 1 / Scene_HighLights
    Uniform3f(U("uMidTones"), 1.0f, 1.0f, 1.0f);         // Scene_MidTones
    Uniform1f(U("uDesat"), 0.0f);                        // Scene_Desaturation
    Uniform1f(U("uInvGamma"), 1.0f / 2.2f);              // Xe-TransEngine.ini DisplayGamma=2.2
    glDrawArrays(GL_TRIANGLES, 0, 3);
    BindVertexArray(0);
    UseProgram(0);
    ActiveTexture(GL_TEXTURE0 + 1); glBindTexture(GL_TEXTURE_2D, 0);
    ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

} // namespace wfc
} // namespace render
