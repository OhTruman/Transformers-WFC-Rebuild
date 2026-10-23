// Enhanced water (the Water row, user playtest 2026-10-23 #14, Integration-approved; default Original = the authored water
// materials, byte-identical). Original already carries every authored feature (RE's shipped-PS decode: the two-speed pans, the
// cube reflection, DepthBiasedAlpha, the distortion pass - restored on the vertex-lightmapped waterfalls in 567e61fe).
// Enhanced, on top of the authored result: a finer moving ripple octave on the authored panned normal, screen-space reflections
// of the scene along it (Fresnel-weighted, faded at the screen edges), and for the translucent liquids an absorption that
// thickens with the depth of liquid behind the surface. Injected into the water materials' programs behind uWaterOn, so the
// row switches without a reload; Original draws exactly as before (the function returns first, no scene copies are made).
#include "render/gl/WfcPipeline.h"

#include <algorithm>
#include <cstdlib>

namespace render {
namespace wfc {

// declared before main() in the water programs; called right after wfcMaterial(m, o)
const char* kFSWaterEnhance = R"(
uniform int uWaterOn;         // the Water row: 1 Enhanced (bound per draw); 0 = the authored material only
uniform mat4 uViewProj;
void wfcWaterEnhance(MatIn m, inout MatOut o, mat3 tbn) {
    if (uWaterOn == 0 || uBlend <= 1) return;   // (opaque water - CoolantFloor - draws in the opaque pass: no finished scene to reflect)
    // a finer moving ripple octave (world xz, metres) on the authored panned normal
    vec2 q = vPos.xz;
    float t = uTime;
    vec2 g = vec2(cos(q.x * 3.1 + t * 1.7) + 0.6 * cos(q.x * 5.3 - q.y * 2.9 + t * 2.3),
                  cos(q.y * 2.7 - t * 1.3) + 0.6 * cos(q.y * 6.1 + q.x * 1.9 - t * 2.9)) * 0.035;
    vec3 N = normalize(normalize(tbn * normalize(o.Normal)) + vec3(g.x, 0.0, g.y));
    vec3 V = normalize(uCamPos - vPos);
    float F = 0.02 + 0.98 * pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 5.0);
    // screen-space reflection: the reflected ray marched in world space, projected, tested against the scene depth (UU)
    vec3 R = reflect(-V, N);
    vec3 refl = vec3(0.0);
    float hitW = 0.0, stepLen = 0.3;
    vec3 p = vPos + N * 0.05;
    for (int i = 0; i < 24; ++i) {
        p += R * stepLen; stepLen *= 1.18;
        vec4 c = uViewProj * vec4(p, 1.0);
        if (c.w <= 0.0) break;
        vec2 uv = c.xy / c.w * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) break;
        float sceneZ = wfcLinearDepth(texture(uSceneDepth, uv).r);
        float rayZ = wfcLinearDepth(c.z / c.w * 0.5 + 0.5);
        if (rayZ > sceneZ + 2.0) {
            if (rayZ < sceneZ + 30.0 + stepLen * 100.0) {
                vec2 e = min(uv, 1.0 - uv);
                hitW = clamp(min(e.x, e.y) * 12.0, 0.0, 1.0) * (1.0 - float(i) / 24.0);
                refl = wfcSceneColor(vec2(uv.x, 1.0 - uv.y)).rgb;
            }
            break;
        }
    }
    o.EmissiveColor += refl * F * hitW;
    // the translucent liquids: absorption thickens with the liquid behind the surface (scene depth - this depth)
    if (uBlend == 2) {
        float thick = max(m.sceneDepth - m.pixelDepth, 0.0) * 0.01;
        o.Opacity = mix(o.Opacity, 1.0, 1.0 - exp(-thick * 0.6));
    }
}
)";

// the MP translucent / additive water and liquid materials (AssetTools PLAYTEST_20261023_WATER, RE #14): the waterfalls, the coolant
// puddles (Debris' opaque CoolantFloor draws before the scene is complete: not here),
// the Omega battery liquid and its surface, Molten's rain puddles
bool Pipeline::isWaterMaterial(const std::string& material) {
    static const char* const kNames[] = {"Waterfall_Slow_MATINST", "Waterfall1_MATINST", "Waterfall2_MATINST", "ComplexWaterfall_MATINST",
                                         "CoolantPuddles_MATINST", "OmegaLiquid_MATINST",
                                         "OmegaLiquidSurface_MATINST", "MoltenRainPuddle_MatInst"};
    const size_t d = material.rfind('.');
    const std::string leaf = d == std::string::npos ? material : material.substr(d + 1);
    for (const char* n : kNames) if (leaf == n) return true;
    return false;
}

// the Water row; WFC_WATER=0|1 overrides it (harness runs)
int Pipeline::waterMode() const {
    static const int env = std::getenv("WFC_WATER") ? std::min(std::max(std::atoi(std::getenv("WFC_WATER")), 0), 1) : -1;
    return env >= 0 ? env : waterMode_;
}

}  // namespace wfc
}  // namespace render
