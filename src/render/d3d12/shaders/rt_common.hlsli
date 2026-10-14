// Clean-room reconstruction — M7 (PC ADAPTATION): the ray-traced passes' shared resources and helpers (descriptor
// table t1 .. t8 + the root TLAS / constants / point sampler; render/d3d12/D3D12RtBridge.cpp): positions from GL's depth,
// the masked (alpha-tested) geometry's opacity test, an occlusion ray. Each pass declares its own UAVs.
#ifndef RT_COMMON_HLSLI
#define RT_COMMON_HLSLI
RaytracingAccelerationStructure Scene : register(t0);
Texture2D<float> RasterDepth : register(t1);
Texture2D<float> WorldDepth : register(t2);     // the depth before the dynamic draws (dynamic pixel = closer now)
// masked (alpha-tested) world geometry (instance mask 0x8, InstanceID = its set): the opacity atlas + lookups
Texture2D<float> MaskAtlas : register(t3);
StructuredBuffer<float2> MaskUv : register(t4);      // UV0 per vertex, all masked sets
StructuredBuffer<uint> MaskIdx : register(t5);       // 3 per masked triangle (vertex indices within its set)
StructuredBuffer<uint> MaskTriSub : register(t6);    // the triangle's sub
StructuredBuffer<float4> MaskSub : register(t7);     // 2 per sub: (uv min, 1 / uv extent), atlas tile (u, v, w, h)
StructuredBuffer<uint4> MaskInst : register(t8);     // per set: uv base, triangle base
SamplerState PointClamp : register(s0);
SamplerState LinearClamp : register(s1);
// stage 2 prototype (reflection hits): static BLAS (InstanceID 0x10000 + set * 2 + side) -> triangles -> subs
StructuredBuffer<uint4> HitInst : register(t9);       // per BLAS: uv1 base, triangle base, index base
StructuredBuffer<uint> HitIdx : register(t10);
StructuredBuffer<float2> HitUv1 : register(t11);
StructuredBuffer<uint> HitTriSub : register(t12);
StructuredBuffer<float4> HitSub : register(t13);      // 8 per sub: pages, lightmap UV transform, 3 scales, albedo, emissive, 0
Texture2D<float4> Lightmaps[512] : register(t0, space1);

#ifndef RT_NO_FRAME_CB                               // (rt_gi.hlsl declares its own constants)
cbuffer Frame : register(b0) {
    float4x4 InvViewProj;   // GL clip -> world (column-major, as GL's matrices)
    float4 ToSun;           // xyz: unit vector towards the sun (-light forward); w: 1 = a sun exists
    float4 Size;            // width, height, frame index, AO rays per pixel
    float4 Params;          // AO radius (m), normal-offset bias (m per m of distance), instance mask, sun cone tan(half angle)
    float4x4 Reproj;        // FidelityFX ndc (x, 1 - 2v, depth) now -> FidelityFX clip space of the previous frame
    float4 Extra;           // znear, zfar, reflection hit shading (1 / 0), 0
    float4 Extra2;          // (reflections) material time (s), pixel spread angle (radians per metre of distance), 0, 0
};

float3 worldAt(int2 px, float d) {
    const float2 ndc = (float2(px) + 0.5) / Size.xy * 2.0 - 1.0;
    float4 p = mul(InvViewProj, float4(ndc, d * 2.0 - 1.0, 1.0));
    return p.xyz / p.w;
}
#endif

uint hash(uint x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
float rnd(inout uint s) { s = hash(s); return (s >> 8) * (1.0 / 16777216.0); }

// a masked triangle's candidate hit: kept where its material's opacity (the atlas tile at the hit's UV0) passed the clip
bool maskedOpaque(uint set, uint prim, float2 bc) {
    const uint4 inst = MaskInst[set - 0x20000u];
    const uint tri = inst.y + prim;
    const float2 uv = MaskUv[inst.x + MaskIdx[tri * 3]] * (1.0 - bc.x - bc.y) + MaskUv[inst.x + MaskIdx[tri * 3 + 1]] * bc.x
                    + MaskUv[inst.x + MaskIdx[tri * 3 + 2]] * bc.y;
    const uint sub = MaskTriSub[tri];
    const float4 box = MaskSub[sub * 2], tile = MaskSub[sub * 2 + 1];
    const float2 t = saturate((uv - box.xy) * box.zw);
    return MaskAtlas.SampleLevel(PointClamp, tile.xy + t * tile.zw, 0) > 0.5;
}

// an occlusion ray that also reports the NEAREST blocker's distance (closest committed hit: a far wall must not widen the
// penumbra - the soft pass runs after the denoiser, so an any-hit distance would flicker as penumbra width)
bool occludedT(float3 o, float3 dir, float tMax, uint mask, out float t) {
    RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
    RayDesc r; r.Origin = o; r.Direction = dir; r.TMin = 0.0; r.TMax = tMax;
    q.TraceRayInline(Scene, RAY_FLAG_NONE, mask, r);
    while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            maskedOpaque(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    }
    t = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? q.CommittedRayT() : 0.0;
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

bool occluded(float3 o, float3 dir, float tMax, uint mask) {
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
    RayDesc r; r.Origin = o; r.Direction = dir; r.TMin = 0.0; r.TMax = tMax;
    q.TraceRayInline(Scene, RAY_FLAG_NONE, mask, r);
    while (q.Proceed()) {                                 // (opaque geometry commits by itself; masked candidates here)
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            maskedOpaque(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    }
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

#endif
