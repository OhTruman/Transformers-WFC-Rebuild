// Clean-room reconstruction — M7 stage 2 prototype (PC ADAPTATION; OFF by default; WFC_RT_REFLECT=1): one mirror
// reflection ray per pixel (closest hit, masked geometry alpha-tested) from GL's depth (position + depth-derived normal).
// Step 1 (this): the ray cost - the target holds (hit distance, hit, 0, 0); hit shading follows. Same root signature /
// table layout as rt_shadow_ao (descriptor table C: u0 = the reflection target). Compiled at build time by DXC (cs_6_5).
#include "rt_common.hlsli"
RWTexture2D<float4> Refl : register(u0);
RWByteAddressBuffer Stats : register(u4);     // [0] rays traced this frame

// the closest hit along the ray (masked candidates committed where opaque): hit distance, or -1
float closestHit(float3 o, float3 dir, float tMax, uint mask) {
    RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
    RayDesc r; r.Origin = o; r.Direction = dir; r.TMin = 0.0; r.TMax = tMax;
    q.TraceRayInline(Scene, RAY_FLAG_NONE, mask, r);
    while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            maskedOpaque(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    }
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? q.CommittedRayT() : -1.0;
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
        const float t = closestHit(o, rdir, 1000.0, (uint)Params.z);
        outv = float4(max(t, 0.0), t >= 0.0 ? 1.0 : 0.0, 0.0, 0.0);
        rays = 1;
    }
    Refl[px] = outv;
    const uint waveRays = WaveActiveSum(rays);
    if (WaveIsFirstLane()) Stats.InterlockedAdd(0, waveRays);
}
