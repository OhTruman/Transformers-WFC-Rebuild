// Clean-room reconstruction — M7 stage 2 prototype (PC ADAPTATION; OFF by default; WFC_RT_REFLECT=1): one mirror
// reflection ray per pixel (closest hit, masked geometry alpha-tested) from GL's depth (position + depth-derived normal).
// Hit shading, static world hits:
//   built as is (step 2a): the sub's average albedo x its lightmap (flat normal) + emissive - a diffuse proxy;
//   with WFC_RT_MATERIALS (step 2b; the map's materials_rt.hlsl from tools/render/matc_hlsl.py, compiled per map into
//   reflect_mat.cso): the sub's own material evaluated at the hit (interpolated UV0 / UV1 / tangent frame, texture
//   copies on D3D12 sampled at a ray-cone LOD) and lit as GL's lightmapped base pass (kFSMainLM): emissive + diffuse x
//   the three directional lightmap coefficients weighted by dot(normal, basis)^2 (no lightmap specular yet).
// Other hits (characters, masked) a dim constant. Target: (radiance, hit). Same root signature / table layout as
// rt_shadow_ao (descriptor table C: u0 = the reflection target). Compiled at build time by DXC (cs_6_5).
#include "rt_common.hlsli"
RWTexture2D<float4> Refl : register(u0);
RWByteAddressBuffer Stats : register(u4);     // [0] rays traced this frame

static const float3 LMB0 = float3(0.0, 0.81649658, 0.57735027);
static const float3 LMB1 = float3(-0.70710678, -0.40824829, 0.57735027);
static const float3 LMB2 = float3(0.70710678, -0.40824829, 0.57735027);

struct HitGeom { uint sub; float2 uv1; uint i0, i1, i2; float3 bary; };

HitGeom hitGeom(uint id, uint prim, float2 bc) {
    HitGeom h;
    const uint4 b = HitInst[id - 0x10000u];
    const uint ib = b.z + prim * 3;
    h.i0 = b.x + HitIdx[ib]; h.i1 = b.x + HitIdx[ib + 1]; h.i2 = b.x + HitIdx[ib + 2];
    h.bary = float3(1.0 - bc.x - bc.y, bc.x, bc.y);
    h.uv1 = HitUv1[h.i0] * h.bary.x + HitUv1[h.i1] * h.bary.y + HitUv1[h.i2] * h.bary.z;
    h.sub = HitTriSub[b.y + prim];
    return h;
}

// the three lightmap coefficients at the hit (scaled; none: zero)
bool lightmapAt(uint sub, float2 uv1, out float3 l[3]) {
    const float4 pages = HitSub[sub * 8], coord = HitSub[sub * 8 + 1];
    const float2 luv = uv1 * coord.xy + coord.zw;
    [unroll] for (uint k = 0; k < 3; ++k) {
        const float pg = k == 0 ? pages.x : k == 1 ? pages.y : pages.z;
        l[k] = pg >= 0.0 ? Lightmaps[NonUniformResourceIndex((uint)pg)].SampleLevel(LinearClamp, luv, 0).rgb * HitSub[sub * 8 + 2 + k].xyz
                         : float3(0, 0, 0);
    }
    return pages.x >= 0.0;
}

float3 shadeProxy(HitGeom h) {
    float3 l[3];
    const float3 L = lightmapAt(h.sub, h.uv1, l) ? (l[0] + l[1] + l[2]) * (1.0 / 3.0) : float3(0.1, 0.1, 0.1);
    return HitSub[h.sub * 8 + 5].rgb * L + HitSub[h.sub * 8 + 6].rgb;
}

#ifdef WFC_RT_MATERIALS
// per material texture slot: texture index (~0 = none), flags (1 cube, 2 HD sRGB block: PWL correction, 4 clamp),
// lod bias (log2 of the copy's top-level size), unpack min, unpack scale
struct MatTexEntry { uint index; uint flags; float lodBias; float pad; float4 umin; float4 uscale; };
StructuredBuffer<MatTexEntry> MatTexTable : register(t14);
StructuredBuffer<float4> HitVtx : register(t15);      // 2 per vertex: (normal, uv0.x), (tangent x (sign < 0 ? 2 : 1), uv0.y)
Texture2D<float4> MatTex2D[2048] : register(t0, space2);
TextureCube<float4> MatTexCube[256] : register(t0, space3);
SamplerState LinearWrap : register(s2);

// GL's wfcPwlFromSrgb (WfcPipeline.cpp buildProgram): filtered sRGB-linear -> its sRGB code -> the Xenos PWL degamma
float3 wfcPwlFromSrgb(float3 x) {
    x = saturate(x);
    const float3 g = lerp(x * 12.92, 1.055 * pow(x, 1.0 / 2.4) - 0.055, step(0.0031308, x));
    const float3 hi = step(96.0 / 255.0, g);
    const float3 sc = lerp(lerp(1.0, 2.0, step(64.0 / 255.0, g)), lerp(4.0, 8.0, step(192.0 / 255.0, g)), hi) / 1024.0;
    const float3 off = lerp(lerp(0.0, -64.0, step(64.0 / 255.0, g)), lerp(-256.0, -1024.0, step(192.0 / 255.0, g)), hi);
    const float3 lin = g * (255.0 * 1024.0) * sc + off;
    return clamp(lin * (1.0 + sc), 0.0, 1023.0) / 1023.0;
}
float4 rtMatTex2D(uint texBase, float lod, uint slot, float2 uv) {
    const MatTexEntry e = MatTexTable[texBase + slot];
    if (e.index == 0xFFFFFFFFu) return float4(0, 0, 0, 1) * e.uscale + e.umin;
    const float l = max(lod + e.lodBias, 0.0);
    float4 t = (e.flags & 4u) ? MatTex2D[NonUniformResourceIndex(e.index)].SampleLevel(LinearClamp, uv, l)
                              : MatTex2D[NonUniformResourceIndex(e.index)].SampleLevel(LinearWrap, uv, l);
    if (e.flags & 2u) t.rgb = wfcPwlFromSrgb(t.rgb);
    return t * e.uscale + e.umin;
}
float4 rtMatTexCube(uint texBase, uint slot, float3 d, float bias) {
    const MatTexEntry e = MatTexTable[texBase + slot];
    if (e.index == 0xFFFFFFFFu) return float4(0, 0, 0, 0);
    return MatTexCube[NonUniformResourceIndex(e.index)].SampleLevel(LinearClamp, d, max(bias, 0.0));
}
#define WFC_MAT_TEX2D(m, slot, uv) rtMatTex2D(m.texBase, m.lod, slot, uv)
#define WFC_MAT_TEXCUBE(m, slot, d, bias) rtMatTexCube(m.texBase, slot, d, bias)
#include "materials_rt.hlsl"

// the sub's own material at the hit; false = no translated material (the proxy shades it)
bool shadeMaterial(HitGeom h, float3 P, float3 rayDir, float coneWidth, out float3 radiance) {
    radiance = float3(0, 0, 0);
    const float4 info = HitSub[h.sub * 8 + 7];         // material id (-1 none), texture base, log2(uv per metre), flags
    if (info.x < 0.0) return false;
    const uint flags = (uint)info.w;                   // 1 lit, 2 two-sided, 4 lightmapped
    const float4 a0 = HitVtx[h.i0 * 2], a1 = HitVtx[h.i1 * 2], a2 = HitVtx[h.i2 * 2];
    const float4 b0 = HitVtx[h.i0 * 2 + 1], b1 = HitVtx[h.i1 * 2 + 1], b2 = HitVtx[h.i2 * 2 + 1];
    float3 N = normalize(a0.xyz * h.bary.x + a1.xyz * h.bary.y + a2.xyz * h.bary.z);
    const float3 tr = b0.xyz * h.bary.x + b1.xyz * h.bary.y + b2.xyz * h.bary.z;
    const float sgn = length(b0.xyz) > 1.5 ? -1.0 : 1.0;
    if ((flags & 2u) && dot(N, rayDir) > 0.0) N = -N;   // a two-sided material seen from its back
    float3 T = tr - N * dot(N, tr);
    T = dot(T, T) > 1e-12 ? normalize(T) : normalize(abs(N.x) < 0.9 ? cross(N, float3(1, 0, 0)) : cross(N, float3(0, 1, 0)));
    const float3 B = cross(N, T) * sgn;
    MatIn m = (MatIn)0;
    m.uv0 = float2(a0.w, b0.w) * h.bary.x + float2(a1.w, b1.w) * h.bary.y + float2(a2.w, b2.w) * h.bary.z;
    m.uv1 = h.uv1;
    m.vertexColor = float4(1, 1, 1, 1);
    m.worldPosUE = P.xzy * 100.0;
    const float3 V = -rayDir;
    m.cameraVector = float3(dot(V, T), dot(V, B), dot(V, N));
    m.normal = float3(0, 0, 1);
    m.reflectionVector = -m.cameraVector + float3(0.0, 0.0, 2.0 * m.cameraVector.z);
    m.tbnT = T.xzy; m.tbnB = B.xzy; m.tbnN = N.xzy;
    m.time = Extra2.x;
    m.pixelDepth = 1e4; m.sceneDepth = 1e9;
    m.dynParam = float4(1, 1, 1, 1);
    m.texBase = (uint)info.y;
    // ray-cone footprint in UV units of a one-texel texture (cone width x uv per metre); each texture adds its size
    m.lod = log2(max(coneWidth, 1e-6)) + info.z;
    MatOut o;
    wfcEvalMaterial((uint)info.x, m, o);
    float3 c = o.EmissiveColor;
    if (flags & 1u) {
        float3 l[3];
        if ((flags & 4u) && lightmapAt(h.sub, h.uv1, l)) {
            const float3 n = normalize(o.Normal);
            const float w0 = dot(n, LMB0), w1 = dot(n, LMB1), w2 = dot(n, LMB2);
            c += o.DiffuseColor * (w0 * w0 * l[0] + w1 * w1 * l[1] + w2 * w2 * l[2]);
        }
        else c += o.DiffuseColor * 0.1;               // (vertex-lit / dynamic channel: a flat proxy)
    }
    radiance = max(c, 0.0);
    return true;
}
#endif

float3 shadeHit(uint id, uint prim, float2 bc, float3 P, float3 rayDir, float coneWidth) {
    if (id < 0x10000u || id >= 0x20000u) return float3(0.05, 0.05, 0.05);   // characters / masked: a dim constant (prototype)
    const HitGeom h = hitGeom(id, prim, bc);
#ifdef WFC_RT_MATERIALS
    float3 r;
    if (shadeMaterial(h, P, rayDir, coneWidth, r)) return r;
#endif
    return shadeProxy(h);
}

// the closest hit along the ray (masked candidates committed where opaque): hit distance (-1 = miss) + its radiance
float closestHit(float3 o, float3 dir, float tMax, uint mask, float coneAtOrigin, out float3 radiance) {
    RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
    RayDesc r; r.Origin = o; r.Direction = dir; r.TMin = 0.0; r.TMax = tMax;
    q.TraceRayInline(Scene, RAY_FLAG_NONE, mask, r);
    while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            maskedOpaque(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    }
    radiance = float3(0, 0, 0);
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return -1.0;
    const float t = q.CommittedRayT();
    if (Extra.z > 0.5)
        radiance = shadeHit(q.CommittedInstanceID(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(), o + dir * t,
                            dir, coneAtOrigin + t * Extra2.y);
    return t;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const int2 px = int2(id.xy);
    const bool inside = id.x < (uint)Size.x && id.y < (uint)Size.y;
    if (!inside) return;
    const float d = RasterDepth[px];
    uint rays = 0;
    float4 outv = float4(0, 0, 0, 0);
    if (d < 1.0) {
        const float3 p = worldAt(px, d);
        const int2 mx = int2(max(px.x - 1, 0), px.y), Px = int2(min(px.x + 1, (int)Size.x - 1), px.y);
        const int2 my = int2(px.x, max(px.y - 1, 0)), Py = int2(px.x, min(px.y + 1, (int)Size.y - 1));
        const float3 a = worldAt(mx, RasterDepth[mx]), b = worldAt(Px, RasterDepth[Px]);
        const float3 c = worldAt(my, RasterDepth[my]), e = worldAt(Py, RasterDepth[Py]);
        const float3 dx = length(b - p) < length(p - a) ? b - p : p - a;
        const float3 dy = length(e - p) < length(p - c) ? e - p : p - c;
        float3 n = normalize(cross(dx, dy));
        const float3 eye = worldAt(px, 0.0);
        if (dot(n, eye - p) < 0.0) n = -n;
        const float dist = length(p - eye);
        const float3 v = (p - eye) / dist;
        const float3 rdir = reflect(v, n);
        const float3 o = p + n * (0.01 + Params.y * dist);
        float3 rad;
        const float t = closestHit(o, rdir, 1000.0, (uint)Params.z, dist * Extra2.y, rad);
        outv = float4(rad, t >= 0.0 ? 1.0 : 0.0);
        rays = 1;
    }
    Refl[px] = outv;
    const uint waveRays = WaveActiveSum(rays);
    if (WaveIsFirstLane()) Stats.InterlockedAdd(0, waveRays);
}
