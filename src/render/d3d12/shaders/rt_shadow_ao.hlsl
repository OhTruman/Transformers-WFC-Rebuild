// Clean-room reconstruction — M7 stage 1 (PC ADAPTATION; OFF by default; WFC_RT_SHADOWAO=1): ray-traced sun shadow +
// ambient occlusion per pixel against the TLAS (inline RayQuery, DXR 1.1), from GL's depth (position and a depth-derived
// normal). Writes the raw mask (R = sun visibility 0 / 1, G = AO openness 0 .. 1; sky 1, 1) and the FidelityFX shadow
// denoiser's inputs: the hit mask (one uint per 8 x 4 tile, bit (y % 4) * 8 + x % 8 set = occluded), world normals and
// the camera-reprojection velocity (previous uv - uv, FidelityFX's top-down uv). Compiled at build time by DXC (cs_6_5).
#include "rt_common.hlsli"
RWTexture2D<float4> Mask : register(u0);        // sun visibility, AO openness, dynamic pixel (1 / 0), 0
RWTexture2D<uint> HitMask : register(u1);
RWTexture2D<float4> Normal : register(u2);
RWTexture2D<float2> Velocity : register(u3);
RWByteAddressBuffer Stats : register(u4);     // [0] rays traced this frame
RWTexture2D<float4> LightVis : register(u0, space2);   // (item 2) per shadow light: visibility (dynamic: all; world: characters)

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
    float blockT = 0.0;                                  // (R2) the sun ray's blocker distance, m (0 = lit)
    // dynamic pixels (characters, movers: drawn after the world) are shadowed by everything. World pixels only by the
    // characters, and only where the sun would otherwise reach them: their world shadows are baked into the lightmaps
    // (indoors, under a roof, a character casts no sun shadow). AO on world pixels: the characters' contact occlusion.
    const bool dynamicPx = inside && d < WorldDepth[px];
    // (R3, Realistic: ToSun.w >= 2) AO everywhere - world pixels are occluded by the world too (contact shadows in corners
    // and under props, 1 m radius), not only by the characters; Faithful keeps the characters-only contact AO. (A map
    // without a directional sun has ToSun.w 0: characters-only AO there in both modes.)
    // (0x14: the characters 0x4 and the death bodies 0x10 - one class everywhere but the X3 sun rays)
    const uint traceMask = (dynamicPx || ToSun.w > 1.5) ? (uint)Params.z : 0x14u;
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
            // faithful (default): the world's occlusion of the sun stays the original's - baked into the lightmaps on world
            // pixels, the per-object light visibility on dynamic objects - so RT darkens only where characters (and not the
            // world) block the sun: character shadows on everything, self-shadowing included. ToSun.w = 2 (WFC_RT_WORLDSHADOWS,
            // the later modern mode): dynamic pixels shadowed by the world too.
            // ToSun.w = 3 (R1 sun swap, Realistic with the map's V_bake): world pixels too - the world composite swaps the
            // lightmaps' baked sun (V_bake) for this visibility, so it must be the full one (world + characters)
            if ((dynamicPx && ToSun.w > 1.5) || ToSun.w > 2.5) {
                float t; outv.x = occludedT(o, sd, 10000.0, (uint)Params.z, t) ? 0.0 : 1.0; ++rays;
                if (outv.x < 0.5) blockT = t;
            }
            else {
                const bool any = occluded(o, sd, 10000.0, (uint)Params.z); ++rays;
                if (any) { outv.x = occluded(o, sd, 10000.0, (uint)Params.z & ~0x14u) ? 1.0 : 0.0; ++rays; }
                // (R2) the character's distance (only where a character is the sun's blocker)
                if (outv.x < 0.5) { float t; if (occludedT(o, sd, 10000.0, 0x14u, t)) blockT = t; ++rays; }
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
                hit += occluded(o, dir, dynamicPx && Extra.w > 0.0 ? Extra.w : Params.x, traceMask) ? 1u : 0u;
            }
            outv.y = 1.0 - (float)hit / (float)nAo;
            rays += nAo;
        }
    }
    // (item 2) the point / spot shadow lights (Extra2.z of them at ring offset Extra2.w): a hard shadow ray each - on
    // dynamic pixels from everything (a lamp behind a wall no longer lights a robot), on world pixels from the characters
    // only (the RT lightmap holds the world's own lamp shadows; the robots' lamp shadows on the world)
    float4 lv = float4(1, 1, 1, 1), tv = float4(1, 1, 1, 1);
    const float nShadow = fmod(Extra2.z, 8.0), nTrans = floor(Extra2.z / 8.0);
    if (inside && d < 1.0 && nTrans > 0.5) {   // (item 4) the transient lights: shadowed by everything
        const float3 p = worldAt(px, d);
        const float3 eye = worldAt(px, 0.0);
        const float3 o = p + n * (0.01 + Params.y * length(p - eye));
        [unroll] for (uint k = 0; k < 4; ++k) {
            if ((float)k >= nTrans) break;
            const float4 L = ShadowLights[(uint)Extra2.w + 4 + k];
            const float3 dl = L.xyz - o;
            const float dist = length(dl);
            if (dist >= L.w || dist < 0.05) continue;
            tv[k] = occluded(o, dl / dist, dist - 0.05, (uint)Params.z | 0x14u) ? 0.0 : 1.0;
            ++rays;
        }
    }
    if (inside && d < 1.0 && nShadow > 0.5) {
        const float3 p = worldAt(px, d);
        const float3 eye = worldAt(px, 0.0);
        const float3 o = p + n * (0.01 + Params.y * length(p - eye));
        const uint lmask = dynamicPx ? (uint)Params.z : 0x14u;
        [unroll] for (uint k = 0; k < 4; ++k) {
            if ((float)k >= nShadow) break;
            const float4 L = ShadowLights[(uint)Extra2.w + k];
            const float3 dl = L.xyz - o;
            const float dist = length(dl);
            if (dist >= L.w || dist < 0.05) continue;
            lv[k] = occluded(o, dl / dist, dist - 0.05, lmask) ? 0.0 : 1.0;
            ++rays;
        }
    }
    if (inside) LightVis[px] = (step(0.5, lv) + 2.0 * step(0.5, tv)) / 255.0;   // (bit 0: shadow light k, bit 1: transient k)
    if (inside) {
        Mask[px] = float4(outv, dynamicPx ? 1.0 : 0.0, 0.0);
        Normal[px] = float4(n, blockT);
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
