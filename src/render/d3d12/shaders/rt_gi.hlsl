// Clean-room reconstruction — R4 (PC EXTENSION, RT Realistic): the runtime ray-traced lightmap. One thread per
// lightmap texel (render/gl/WfcRtGi.cpp rasterised them at load: world position + tangent frame) traces
//   the sun (soft: a jittered direction in R2's cone) with Systems' fitted share k x max(B_i . L, 0)^p x sun,
//   every static light in range (Systems' fit_lights.json: class scale, UE3 falloff max(0, 1 - (d/R)^2)^exp or linear,
//   spot cone^2, lighting channels BSP / Static per receiver) with a shadow ray,
//   hemisphere rays: a miss sees the fitted sky colour, a hit the surface there (albedo x its current RT lightmap
//   value, + emissive) - one bounce per pass, multi-bounce over passes,
// projected onto the bake's three directional coefficients (HL2 basis B_i in the texel's tangent frame; for a unit
// radiance from direction w the coefficient i gets max(B_i . w, 0), as Systems' sky share S_i), blended into the
// running estimate and written per coefficient page as value / the sub's decode scale (GL's decode tex x scale then
// returns the value). Built with GI_DILATE: the gutter texels copied from their chart neighbours (run twice: ring 2
// reads ring 1).
// Same root signature as the other RT passes (descriptor table: t1 .. t13 as rt_common, t14 the texels or the gutter
// list, t15 GiData; the GI pages as UAVs in space1).
#define RT_NO_FRAME_CB
#include "rt_common.hlsli"

struct GiTexel { uint pageXY; float3 pos; uint nrm, tan, sub, pad; };
#ifdef GI_DILATE
StructuredBuffer<uint4> Dilate : register(t14);       // (dilate) per gutter texel: dst, src, src sub, 0
#else
StructuredBuffer<GiTexel> Texels : register(t14);
#endif
StructuredBuffer<float4> GiData : register(t15);      // subs (4 each), lights (4 each), hit page -> GI page (4 per float4)
RWTexture2D<float4> Gi[512] : register(u0, space1);   // the GI pages (GL lightmap page index), RGBA16F
RWByteAddressBuffer Stats : register(u4);

cbuffer GiFrame : register(b0) {
    float4 ToSunG;      // xyz towards the sun, w: 1 = a sun
    float4 SunCol;      // rgb: sun_linear (x the texel's page k, GiData sub row 1 .w), w: tan(the cone's half angle)
    float4 SkyG;        // rgb: the fitted sky colour, w: hemisphere rays per texel
    uint4 Range;        // first texel, texel count, frame index, light count
    float4 Mix;         // minimum blend weight, tangent-y sign, the sun's exponent p, the bounce gain (0 = no bounce)
    uint4 Off;          // GiData offsets: subs, lights, hit page map; instance mask
};

static const float3 LMB[3] = { float3(0.0, 0.81649658, 0.57735027), float3(-0.70710678, -0.40824829, 0.57735027),
                               float3(0.70710678, -0.40824829, 0.57735027) };

float3 octDecode(uint e) {
    float2 f = float2(e & 0xFFFFu, e >> 16) / 65535.0 * 2.0 - 1.0;
    float3 n = float3(f, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return normalize(n);
}

void basisFrame(float3 n, out float3 t, out float3 b) {
    const float3 a = abs(n.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    t = normalize(cross(a, n)); b = cross(n, t);
}

// the GI page index of a hit lightmap page (the reflection hit data's D3D12 page numbering -> GL's)
int giPageOfHit(float hitPage) {
    if (hitPage < 0.0) return -1;
    const uint p = (uint)hitPage;
    const float4 m = GiData[Off.z + p / 4];
    const float k = (p & 3) == 0 ? m.x : (p & 3) == 1 ? m.y : (p & 3) == 2 ? m.z : m.w;
    return (int)k;
}

// the radiance leaving a hit surface towards the texel (Lambertian, flat normal: GL's L = sum w_i^2 l_i with
// w_i^2 = 1/3): albedo x the current RT lightmap there (the bake where the hit has none) + emissive
float3 hitRadiance(uint id, uint prim, float2 bc) {
    if (id < 0x10000u || id >= 0x20000u) return float3(0.02, 0.02, 0.02);   // (characters / masked: dim)
    const uint4 b = HitInst[id - 0x10000u];
    const uint ib = b.z + prim * 3;
    const float3 bary = float3(1.0 - bc.x - bc.y, bc.x, bc.y);
    const float2 uv1 = HitUv1[b.x + HitIdx[ib]] * bary.x + HitUv1[b.x + HitIdx[ib + 1]] * bary.y + HitUv1[b.x + HitIdx[ib + 2]] * bary.z;
    const uint sub = HitTriSub[b.y + prim];
    const float4 pages = HitSub[sub * 8], coord = HitSub[sub * 8 + 1];
    const float2 luv = uv1 * coord.xy + coord.zw;
    float3 L = float3(0, 0, 0);
    [unroll] for (uint k = 0; k < 3; ++k) {
        const float pg = k == 0 ? pages.x : k == 1 ? pages.y : pages.z;
        const float3 sc = HitSub[sub * 8 + 2 + k].xyz;
        const int gp = giPageOfHit(pg);
        if (gp >= 0) {
            uint w, h; Gi[NonUniformResourceIndex(gp)].GetDimensions(w, h);
            const int2 tx = clamp(int2(luv * float2(w, h)), int2(0, 0), int2(w - 1, h - 1));
            L += Gi[NonUniformResourceIndex(gp)][tx].rgb * sc;
        } else if (pg >= 0.0) {
            L += Lightmaps[NonUniformResourceIndex((uint)pg)].SampleLevel(LinearClamp, luv, 0).rgb * sc;
        }
    }
    // the albedo x the map's bounce gain (the bake's GI was far stronger than these dark albedos bounce: Beast's intensity
    // settings were not cooked), each channel capped at 0.8 so bright surfaces cannot run away over the passes
    const float3 refl = min(HitSub[sub * 8 + 5].rgb * max(Mix.w, 0.0), 0.8);
    return refl * L * (1.0 / 3.0) + HitSub[sub * 8 + 6].rgb;
}

// (diagnostics: the bounce rays' fate, summed per wave into Stats bytes 192 ..: hits on static geometry, of them with a
// GI page / the bake fallback / neither, the hit albedo and radiance luminance x 1e4)
static uint gStatic = 0, gGi = 0, gBake = 0, gNone = 0; static float gAlb = 0, gRad = 0;
float3 traceRadiance(float3 o, float3 dir, uint mask) {
    RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
    RayDesc r; r.Origin = o; r.Direction = dir; r.TMin = 0.0; r.TMax = 2000.0;
    q.TraceRayInline(Scene, RAY_FLAG_NONE, mask, r);
    while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            maskedOpaque(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    }
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return SkyG.rgb;
    {
        const uint id = q.CommittedInstanceID();
        if (id >= 0x10000u && id < 0x20000u) {
            ++gStatic;
            const uint4 b = HitInst[id - 0x10000u];
            const uint sub = HitTriSub[b.y + q.CommittedPrimitiveIndex()];
            const float pg = HitSub[sub * 8].x;
            if (giPageOfHit(pg) >= 0) ++gGi; else if (pg >= 0.0) ++gBake; else ++gNone;
            gAlb += dot(HitSub[sub * 8 + 5].rgb, float3(0.2126, 0.7152, 0.0722));
        }
    }
    if (Mix.w <= 0.0) return float3(0, 0, 0);
    // x the map's bounce gain (WfcRtGi.cpp: fitted at load so the RT lightmap's mean matches the bake's - the bake's
    // GI intensity settings were not cooked)
    const float3 hr = hitRadiance(q.CommittedInstanceID(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());
    gRad += dot(hr, float3(0.2126, 0.7152, 0.0722));
    return hr;                                            // (the gain is in hitRadiance's capped reflectance)
}

#ifndef GI_DILATE
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= Range.y) return;
    const GiTexel T = Texels[Range.x + id.x];
    const uint page = T.pageXY & 511u;
    const bool vtx = page == 511u;                        // a vertex-lit sub's vertex (the MDI sample layout)
    const int2 xy = int2((T.pageXY >> 9) & 2047u, T.pageXY >> 20);
    const uint vi = T.pageXY >> 9;
    const uint sub = T.sub & 0x7FFFFFFFu;
    const float sgn = (T.sub >> 31) != 0 ? -1.0 : 1.0;
    const float3 N = octDecode(T.nrm);
    float3 Tg = octDecode(T.tan);
    Tg = normalize(Tg - N * dot(N, Tg) + 1e-6);
    const float3 Bt = cross(N, Tg) * sgn * Mix.y;
    float3 Bw[3];
    [unroll] for (uint i = 0; i < 3; ++i) Bw[i] = normalize(Tg * LMB[i].x + Bt * LMB[i].y + N * LMB[i].z);
    const float4 S0 = GiData[Off.x + sub * 4];           // pages, BSP receiver
    const float3 sc[3] = { GiData[Off.x + sub * 4 + 1].xyz, GiData[Off.x + sub * 4 + 2].xyz, GiData[Off.x + sub * 4 + 3].xyz };
    const bool bspRecv = S0.w > 0.5;
    const float3 P = T.pos + N * 0.02;
    const uint mask = Off.w;
    uint seed = hash(Range.x + id.x + Range.z * 0x9E3779B9u);
    float3 acc[3] = { float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0) };
    uint rays = 0;
    // the sun
    if (ToSunG.w > 0.5 && dot(N, ToSunG.xyz) > -0.2) {
        float3 t, b; basisFrame(ToSunG.xyz, t, b);
        const float a = rnd(seed) * 6.2831853, r = sqrt(rnd(seed)) * SunCol.w;
        const float3 dir = normalize(ToSunG.xyz + (t * cos(a) + b * sin(a)) * r);
        ++rays;
        if (!occluded(P, dir, 5000.0, mask))
            [unroll] for (uint i = 0; i < 3; ++i) acc[i] += SunCol.rgb * GiData[Off.x + sub * 4 + 1].w * pow(max(dot(Bw[i], ToSunG.xyz), 0.0), Mix.z);
    }
    // the static lights in range
    for (uint j = 0; j < Range.w; ++j) {
        const float4 L0 = GiData[Off.y + j * 4], L1 = GiData[Off.y + j * 4 + 1], L2 = GiData[Off.y + j * 4 + 2], L3 = GiData[Off.y + j * 4 + 3];
        const uint chan = (uint)L3.z;
        if ((bspRecv ? (chan & 1u) : (chan & 2u)) == 0) continue;
        const float3 d = L0.xyz - P;
        const float dist = length(d);
        if (dist >= L0.w || dist < 1e-4) continue;
        const float3 ldir = d / dist;
        float F = L3.w > 0.5 ? max(0.0, 1.0 - dist / L0.w) : pow(max(0.0, 1.0 - (dist * dist) / (L0.w * L0.w)), L1.w);
        if (L3.y > 0.5) {                                 // spot: UE3's cone^2
            const float cone = saturate((dot(-ldir, L2.xyz) - L2.w) / max(L3.x - L2.w, 1e-4));
            F *= cone * cone;
        }
        const float3 c = L1.rgb * F;
        if (max(c.r, max(c.g, c.b)) < 1e-4 || dot(N, ldir) < -0.2) continue;
        ++rays;
        if (occluded(P, ldir, dist - 0.02, mask)) continue;
        [unroll] for (uint i = 0; i < 3; ++i) acc[i] += c * max(dot(Bw[i], ldir), 0.0);
    }
    // sky + bounce: uniform hemisphere rays (estimate of (1/pi) integral of L max(B_i . w, 0) dw = 2 / n sum)
    const uint n = (uint)SkyG.w;
    if (n > 0) {
        float3 t, b; basisFrame(N, t, b);
        for (uint k = 0; k < n; ++k) {
            const float u = rnd(seed), a = rnd(seed) * 6.2831853;
            const float s = sqrt(max(0.0, 1.0 - u * u));
            const float3 w = t * (s * cos(a)) + b * (s * sin(a)) + N * u;
            const float3 R = traceRadiance(P, w, mask);
            ++rays;
            [unroll] for (uint i = 0; i < 3; ++i) acc[i] += R * (2.0 / n) * max(dot(Bw[i], w), 0.0);
        }
    }
    // blend: the running mean (1 / (count + 1)) down to the minimum weight; the count lives in coefficient 0's alpha
    int lm[3] = { (int)S0.x, (int)S0.y, (int)S0.z };
    int2 at[3] = { xy, xy, xy };
    if (vtx) {                                            // entries off + row x count + vertex, 4096 per row of page 511
        const uint cnt = (uint)GiData[Off.x + sub * 4 + 2].w;
        [unroll] for (uint r = 0; r < 3; ++r) { const uint e = T.pad + r * cnt + vi; lm[r] = 511; at[r] = int2(e & 4095u, e >> 12); }
    }
    const float4 old0 = lm[0] >= 0 ? Gi[NonUniformResourceIndex(lm[0])][at[0]] : float4(0, 0, 0, 0);
    const float wgt = max(Mix.x, 1.0 / (old0.a + 1.0));
    [unroll] for (uint i = 0; i < 3; ++i) {
        if (lm[i] < 0) continue;
        const float3 inv = float3(sc[i].x > 1e-6 ? 1.0 / sc[i].x : 0.0, sc[i].y > 1e-6 ? 1.0 / sc[i].y : 0.0, sc[i].z > 1e-6 ? 1.0 / sc[i].z : 0.0);
        const float4 old = i == 0 ? old0 : Gi[NonUniformResourceIndex(lm[i])][at[i]];
        const float3 v = lerp(old.rgb, acc[i] * inv, wgt);
        // (Mix.x = 1: a propagation pass - the new estimate replaces the old and the running mean restarts)
        Gi[NonUniformResourceIndex(lm[i])][at[i]] = float4(v, i == 0 ? (Mix.x > 0.999 ? 1.0 : min(old0.a + 1.0, 4096.0)) : 1.0);
    }
    const uint waveRays = WaveActiveSum(rays);
    if (WaveIsFirstLane()) Stats.InterlockedAdd(0, waveRays);
    const uint s0 = WaveActiveSum(n), s1 = WaveActiveSum(gStatic), s2 = WaveActiveSum(gGi), s3 = WaveActiveSum(gBake), s4 = WaveActiveSum(gNone);
    const uint s5 = WaveActiveSum((uint)(gAlb * 1000.0)), s6 = WaveActiveSum((uint)(gRad * 10000.0));
    if (WaveIsFirstLane()) {
        Stats.InterlockedAdd(192, s0); Stats.InterlockedAdd(196, s1); Stats.InterlockedAdd(200, s2); Stats.InterlockedAdd(204, s3);
        Stats.InterlockedAdd(208, s4); Stats.InterlockedAdd(212, s5); Stats.InterlockedAdd(216, s6);
    }
}

#else
// the gutter: each listed texel takes its chart neighbour's value on all three coefficient pages (in list order: the
// second ring reads the first)
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= Range.y) return;
    const uint4 e = Dilate[Range.x + id.x];
    const int2 dst = int2((e.x >> 9) & 2047u, e.x >> 20), src = int2((e.y >> 9) & 2047u, e.y >> 20);
    const float4 S0 = GiData[Off.x + e.z * 4];
    const int lm[3] = { (int)S0.x, (int)S0.y, (int)S0.z };
    [unroll] for (uint i = 0; i < 3; ++i)
        if (lm[i] >= 0) Gi[NonUniformResourceIndex(lm[i])][dst] = Gi[NonUniformResourceIndex(lm[i])][src];
}
#endif
