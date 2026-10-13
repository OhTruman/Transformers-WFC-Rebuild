// Clean-room reconstruction — M7 stage 0 (PC ADAPTATION; OFF by default; WFC_RT_DEPTHCHECK=1): one primary ray per
// pixel through the static-world TLAS (inline RayQuery, DXR 1.1), compared with GL's depth prepass. Compiled at build
// time by DXC (cs_6_5) into rt_depthcheck.h. The GL depth arrives as window depth [0, 1] in an R32F texture whose
// memory rows are GL's (row 0 = the bottom row), so pixel (x, y) here is GL's (x, y) with y up: no flip.
RaytracingAccelerationStructure Scene : register(t0);
Texture2D<float> RasterDepth : register(t1);
RWTexture2D<float> OutDepth : register(u0);   // the ray's view depth (0 = miss), for a debug view
RWByteAddressBuffer Stats : register(u4);     // counters, see below

cbuffer Frame : register(b0) {
    float4x4 InvViewProj;   // GL clip -> world (column-major, as GL's matrices)
    float4 ViewRow2;        // view matrix row 2: view depth = -dot(ViewRow2, (p, 1))
    float4 Size;            // width, height, 1 = winding flipped (diagnostics), instance mask
};

// counters (relative difference of the distance along the pixel ray): 0 both sky, 1 agree < 1e-4, 2 < 1e-3, 3 < 1e-2, 4 worse, 5 raster hit / ray miss, 6 raster sky / ray hit
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= (uint)Size.x || id.y >= (uint)Size.y) return;
    const float2 ndc = (float2(id.xy) + 0.5) / Size.xy * 2.0 - 1.0;
    float4 pn = mul(InvViewProj, float4(ndc, -1.0, 1.0)); pn /= pn.w;
    float4 pf = mul(InvViewProj, float4(ndc, 1.0, 1.0)); pf /= pf.w;
    const float3 seg = pf.xyz - pn.xyz;
    const float len = length(seg);
    RayDesc ray;
    ray.Origin = pn.xyz; ray.Direction = seg / len; ray.TMin = 0.0; ray.TMax = len;   // GL's near / far clipping
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(Scene, Size.z > 0.5 ? RAY_FLAG_CULL_FRONT_FACING_TRIANGLES : RAY_FLAG_CULL_BACK_FACING_TRIANGLES, (uint)Size.w, ray);
    q.Proceed();
    const bool hit = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    const float rayT = hit ? q.CommittedRayT() : 0.0;   // distance from the near-plane point along the pixel ray
    OutDepth[id.xy] = hit ? -dot(ViewRow2, float4(ray.Origin + ray.Direction * rayT, 1.0)) : 0.0;

    const float d = RasterDepth[id.xy];
    const bool sky = d >= 1.0;
    uint c;
    if (sky) c = hit ? 6 : 0;
    else if (!hit) c = 5;
    else {
        float4 p = mul(InvViewProj, float4(ndc, d * 2.0 - 1.0, 1.0)); p /= p.w;
        const float rasterT = length(p.xyz - ray.Origin);   // the same distance to the raster depth's point
        const float rel = abs(rayT - rasterT) / max(max(rayT, rasterT), 1e-6);
        c = rel < 1e-4 ? 1 : rel < 1e-3 ? 2 : rel < 1e-2 ? 3 : 4;
        // (diagnostics) counters 7 .. 22: log2(raster / ray distance) in quarter octaves, -2 .. +2
        Stats.InterlockedAdd((7 + (uint)clamp((int)floor(log2(max(rasterT, 1e-6) / max(rayT, 1e-6)) * 4.0) + 8, 0, 15)) * 4, 1);
    }
    Stats.InterlockedAdd(c * 4, 1);
}
