// Clean-room reconstruction — M7 stage 2 (PC ADAPTATION): the material interface of the HLSL hit-shading functions
// (tools/render/matc_hlsl.py output, materials_rt.hlsl per map). Mirrors the GLSL MatIn / MatOut / helpers in
// render/gl/WfcPipeline.cpp (kFSHead) with GLSL semantics kept: the tangent frame is three UE-space columns
// (tangentToWorldUE(v) = T * v.x + B * v.y + N * v.z, GLSL's mat3(T, B, N) * v). The texture fetches are supplied by
// the including pass (WFC_MAT_TEX2D / WFC_MAT_TEXCUBE: a bindless table per material, sampled at a ray-cone LOD).
#ifndef RT_MATERIAL_HLSLI
#define RT_MATERIAL_HLSLI
struct MatIn {
    float2 uv0; float2 uv1; float3 subUV2; float4 vertexColor; float3 worldPosUE; float3 cameraVector;
    float3 reflectionVector; float3 normal; float3 tbnT; float3 tbnB; float3 tbnN; float time; float pixelDepth;
    float4 screenPos; float sceneDepth; float4 dynParam;
    uint texBase;           // this material's first entry in the texture table
    float lod;              // the hit's texture LOD (ray cone)
};
struct MatOut {
    float3 Distortion; float3 DiffuseColor; float3 SpecularColor; float SpecularPower; float3 Normal;
    float3 EmissiveColor; float Opacity; float OpacityMask; float3 CustomLighting; float ScreenAlpha;
};
// GLSL mod (floor), not HLSL fmod (truncation)
float wfcMod(float a, float b) { return a - b * floor(a / b); }
float2 wfcMod(float2 a, float2 b) { return a - b * floor(a / b); }
float3 wfcMod(float3 a, float3 b) { return a - b * floor(a / b); }
float4 wfcMod(float4 a, float4 b) { return a - b * floor(a / b); }
float3 tangentToWorldUE(MatIn m, float3 v) { return m.tbnT * v.x + m.tbnB * v.y + m.tbnN * v.z; }
float3 tangentToView(MatIn m, float3 v) { return tangentToWorldUE(m, v); }
float3 tangentToLocal(MatIn m, float3 v) { return v; }
float3 wfcNormalNode(MatIn m) { return tangentToWorldUE(m, m.normal); }
// UE3: ReflectionVector = -CameraVector + Normal * dot(Normal, CameraVector) * 2 (tangent space)
void wfcSetNormal(inout MatIn m, float3 n) {
    m.normal = normalize(n);
    m.reflectionVector = -m.cameraVector + m.normal * dot(m.normal, m.cameraVector) * 2.0;
}
// screen-space inputs have no meaning at a ray hit: depth-biased alpha passes alpha, the scene colour is black
float wfcDepthBiasedAlpha(MatIn m, float a, float bias, float scale) { return a; }
float4 wfcSceneColor(MatIn m, float2 uv) { return float4(0, 0, 0, 0); }
float2 wfcScreenUV(MatIn m) { return float2(0.5, 0.5); }
#ifndef WFC_MAT_TEX2D
#define WFC_MAT_TEX2D(m, slot, uv) float4(1, 1, 1, 1)
#define WFC_MAT_TEXCUBE(m, slot, d, bias) float4(0, 0, 0, 0)
#endif
float4 wfcTex2D(MatIn m, uint slot, float2 uv) { return WFC_MAT_TEX2D(m, slot, uv); }
float4 wfcTexCube(MatIn m, uint slot, float3 d, float bias) { return WFC_MAT_TEXCUBE(m, slot, d, bias); }
#endif
