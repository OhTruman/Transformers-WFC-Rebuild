// Clean-room reconstruction — M7 stage 1 (PC ADAPTATION; OFF by default; WFC_RT_SHADOWAO=1): ray-traced sun shadow +
// ambient occlusion per pixel against the TLAS (inline RayQuery, DXR 1.1), from GL's depth (position and a depth-derived
// normal). Writes the raw mask (R = sun visibility 0 / 1, G = AO openness 0 .. 1; sky 1, 1) and the FidelityFX shadow
// denoiser's inputs: the hit mask (one uint per 8 x 4 tile, bit (y % 4) * 8 + x % 8 set = occluded), world normals and
// the camera-reprojection velocity (previous uv - uv, FidelityFX's top-down uv). Compiled at build time by DXC (cs_6_5).
RaytracingAccelerationStructure Scene : register(t0);
Texture2D<float> RasterDepth : register(t1);
Texture2D<float> WorldDepth : register(t2);     // the depth before the dynamic draws (dynamic pixel = closer now)
RWTexture2D<float4> Mask : register(u0);        // sun visibility, AO openness, dynamic pixel (1 / 0), 0
RWTexture2D<uint> HitMask : register(u1);
RWTexture2D<float4> Normal : register(u2);
RWTexture2D<float2> Velocity : register(u3);
RWByteAddressBuffer Stats : register(u4);     // [0] rays traced this frame

cbuffer Frame : register(b0) {
    float4x4 InvViewProj;   // GL clip -> world (column-major, as GL's matrices)
    float4 ToSun;           // xyz: unit vector towards the sun (-light forward); w: 1 = a sun exists
    float4 Size;            // width, height, frame index, AO rays per pixel
    float4 Params;          // AO radius (m), normal-offset bias (m per m of distance), instance mask, sun cone tan(half angle)
    float4x4 Reproj;        // FidelityFX ndc (x, 1 - 2v, depth) now -> FidelityFX clip space of the previous frame
};

float3 worldAt(int2 px, float d) {
    const float2 ndc = (float2(px) + 0.5) / Size.xy * 2.0 - 1.0;
    float4 p = mul(InvViewProj, float4(ndc, d * 2.0 - 1.0, 1.0));
    return p.xyz / p.w;
}

uint hash(uint x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
float rnd(inout uint s) { s = hash(s); return (s >> 8) * (1.0 / 16777216.0); }

bool occluded(float3 o, float3 dir, float tMax, uint mask) {
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
    RayDesc r; r.Origin = o; r.Direction = dir; r.TMin = 0.0; r.TMax = tMax;
    q.TraceRayInline(Scene, RAY_FLAG_NONE, mask, r);
    q.Proceed();
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

groupshared uint gTileMask;

[numthreads(8, 4, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID) {
    if (all(gtid.xy == 0)) gTileMask = 0;
    GroupMemoryBarrierWithGroupSync();
    const int2 px = int2(id.xy);
    const bool inside = id.x < (uint)Size.x && id.y < (uint)Size.y;
    const float d = inside ? RasterDepth[px] : 1.0;
    uint rays = 0;
    float2 outv = float2(1.0, 1.0);
    float3 n = float3(0, 0, 1);
    // dynamic pixels (characters, movers: drawn after the world) are shadowed by everything. World pixels only by the
    // characters, and only where the sun would otherwise reach them: their world shadows are baked into the lightmaps
    // (indoors, under a roof, a character casts no sun shadow). AO on world pixels: the characters' contact occlusion.
    const bool dynamicPx = inside && d < WorldDepth[px];
    const uint traceMask = dynamicPx ? (uint)Params.z : 0x4u;
    if (inside && d < 1.0) {
        const float3 p = worldAt(px, d);
        // normal from depth: per axis the neighbour with the smaller step (keeps silhouettes from bending the normal)
        const int2 mx = int2(max(px.x - 1, 0), px.y), Px = int2(min(px.x + 1, (int)Size.x - 1), px.y);
        const int2 my = int2(px.x, max(px.y - 1, 0)), Py = int2(px.x, min(px.y + 1, (int)Size.y - 1));
        const float3 a = worldAt(mx, RasterDepth[mx]), b = worldAt(Px, RasterDepth[Px]);
        const float3 c = worldAt(my, RasterDepth[my]), e = worldAt(Py, RasterDepth[Py]);
        const float3 dx = length(b - p) < length(p - a) ? b - p : p - a;
        const float3 dy = length(e - p) < length(p - c) ? e - p : p - c;
        n = normalize(cross(dx, dy));
        const float3 eye = worldAt(px, 0.0);                       // the near-plane point of this pixel's ray
        if (dot(n, eye - p) < 0.0) n = -n;
        const float dist = length(p - eye);
        const float3 o = p + n * (0.01 + Params.y * dist);
        uint s = hash(id.x * 1973u + id.y * 9277u + (uint)Size.z * 26699u);   // per pixel and frame (the denoisers average)
        if (ToSun.w > 0.5) {                             // soft sun: one ray in the sun's cone (a uniform disc of directions)
            float3 sd = ToSun.xyz;
            if (Params.w > 0.0) {
                const float3 su = normalize(abs(sd.y) < 0.99 ? cross(sd, float3(0, 1, 0)) : cross(sd, float3(1, 0, 0)));
                const float3 sv = cross(sd, su);
                const float r = sqrt(rnd(s)) * Params.w, phi = 6.2831853 * rnd(s);
                sd = normalize(sd + su * (r * cos(phi)) + sv * (r * sin(phi)));
            }
            if (dynamicPx) { outv.x = occluded(o, sd, 10000.0, (uint)Params.z) ? 0.0 : 1.0; ++rays; }
            else {
                const bool any = occluded(o, sd, 10000.0, (uint)Params.z); ++rays;
                // occluded: by the world (baked: no extra darkening) or by characters only (the RT character shadow)
                if (any) { outv.x = occluded(o, sd, 10000.0, (uint)Params.z & ~0x4u) ? 1.0 : 0.0; ++rays; }
            }
        }
        const uint nAo = (uint)Size.w;
        if (nAo > 0) {
            const float3 t = normalize(abs(n.y) < 0.99 ? cross(n, float3(0, 1, 0)) : cross(n, float3(1, 0, 0)));
            const float3 bt = cross(n, t);
            uint hit = 0;
            for (uint k = 0; k < nAo; ++k) {                        // cosine-weighted hemisphere
                const float u1 = rnd(s), u2 = rnd(s);
                const float r = sqrt(u1), phi = 6.2831853 * u2;
                const float3 dir = t * (r * cos(phi)) + bt * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u1));
                hit += occluded(o, dir, Params.x, traceMask) ? 1u : 0u;
            }
            outv.y = 1.0 - (float)hit / (float)nAo;
            rays += nAo;
        }
    }
    if (inside) {
        Mask[px] = float4(outv, dynamicPx ? 1.0 : 0.0, 0.0);
        Normal[px] = float4(n, 0.0);
        const float2 uv = (float2(px) + 0.5) / Size.xy;
        const float4 clip = mul(Reproj, float4(uv.x * 2.0 - 1.0, 1.0 - 2.0 * uv.y, d, 1.0));
        const float2 prev = clip.xy / clip.w;
        Velocity[px] = float2(prev.x * 0.5 + 0.5, 0.5 - prev.y * 0.5) - uv;
        if (outv.x < 0.5) InterlockedOr(gTileMask, 1u << (gtid.y * 8 + gtid.x));
    }
    GroupMemoryBarrierWithGroupSync();
    if (all(gtid.xy == 0)) HitMask[gid.xy] = gTileMask;
    const uint waveRays = WaveActiveSum(rays);
    if (WaveIsFirstLane()) Stats.InterlockedAdd(0, waveRays);
}
