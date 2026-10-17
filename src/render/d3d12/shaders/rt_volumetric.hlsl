// Clean-room reconstruction — X3 (PC ADAPTATION, Realistic only; WFC_X3=0 off): ray-traced volumetric light on a froxel
// grid. The view is cut into 16-pixel screen tiles x 64 exponential depth slices (0.5 m to the max distance); the grid is
// stored as a 2D atlas (8 x 8 slices of tilesX x tilesY) so it rides the existing GL <-> D3D12 2D sharing.
//   Mode 0 (inject + temporal): per froxel, at a jittered point, the medium (the map's authored height fog + a thin haze
//     floor) and the light it scatters toward the eye - the sun through one RT shadow ray (Henyey-Greenstein g, forward
//     shafts), the frame's in-view lamps through one ray each where the froxel is inside their radius, and the transient
//     lights (muzzle flashes, impacts, explosions, headlights) unshadowed. NO ambient in-scatter: a constant term is a
//     grey veil (v1 - v3). Blended with the previous frame's grid reprojected through its view-projection (history ping-pong
//     Hist0 / Hist1, rgb = sigma x L, a = sigma).
//   Mode 1 (integrate): per tile, front to back over the slices: rgb = the light scattered toward the eye up to the
//     slice's far edge, a = the transmittance - written to Vol (shared; GL: scene x a + rgb at the pixel's own depth).
// Compiled at build time by DXC (cs_6_5).
#define RT_NO_FRAME_CB
#include "rt_common.hlsli"

cbuffer Frame : register(b0) {
    float4x4 InvViewProj;
    float4 ToSun;          // xyz towards the sun, w 1 = a sun
    float4 Size;           // render width, height, frame index, the lamp core radius (m)
    float4 Params;         // 0, 0, instance mask, the cap on the integrated in-scatter (per channel; 0 = none)
    float4 VSun;           // the sun's linear colour x brightness (rgb), the sun phase anisotropy g (w)
    float4 VFog;           // density (1 / m) below the fog height, height falloff (1 / m above it), fog height (m), haze (1 / m)
    float4 VMisc;          // max distance (m), slices, tiles x, tiles y
    float4 VCtl;           // mode (0 inject, 1 integrate), history parity, previous frame valid, transient light count
    float4 Extra;          // znear, zfar, the ring slot's base, the lamp strength
    float4 Extra2;         // history weight, tile size (px), in-view lamp count, transient strength
};
RWTexture2D<float4> Vol : register(u0);     // integrated (shared with GL)
RWTexture2D<float4> Hist0 : register(u1);   // per froxel: sigma x L (rgb), sigma (a) - ping-pong with Hist1
RWTexture2D<float4> Hist1 : register(u2);

static const float kNear = 0.5;

float3 worldAt(float2 px, float d) {
    const float2 ndc = px / Size.xy * 2.0 - 1.0;
    float4 p = mul(InvViewProj, float4(ndc, d * 2.0 - 1.0, 1.0));
    return p.xyz / p.w;
}
float sliceZ(float s) { return kNear * pow(max(VMisc.x, kNear + 1.0) / kNear, s / VMisc.y); }   // s in [0, slices]
float zSlice(float z) { return log(max(z, 1e-3) / kNear) / log(max(VMisc.x, kNear + 1.0) / kNear) * VMisc.y; }
int2 atlasPx(uint2 t, uint s) { return int2((s % 8u) * (uint)VMisc.z + t.x, (s / 8u) * (uint)VMisc.w + t.y); }

bool blocked(float3 o, float3 dir, float tMax, uint mask) {
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> q;
    RayDesc r; r.Origin = o; r.Direction = dir; r.TMin = 0.02; r.TMax = tMax;
    q.TraceRayInline(Scene, RAY_FLAG_NONE, mask, r);
    while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            maskedOpaque(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    }
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}
float density(float3 x) {
    // (round 3: the haze follows the authored fog's height falloff too - a constant haze over 120 m of corridor was the
    // grey veil on Streets' match frames)
    const float above = max(x.y - VFog.z, 0.0);
    return (VFog.x + VFog.w) * exp(-above * VFog.y);
}
float hg(float c, float g) { return (1.0 - g * g) / (12.566371 * pow(max(1.0 + g * g - 2.0 * g * c, 1e-4), 1.5)); }

// the camera: eye, view forward, and the unit view ray through pixel px
void camera(float2 px, out float3 eye, out float3 fwd, out float3 dir) {
    const float3 c0 = worldAt(Size.xy * 0.5, 0.0), c1 = worldAt(Size.xy * 0.5, 1.0);
    fwd = normalize(c1 - c0);
    const float3 p0 = worldAt(px, 0.0), p1 = worldAt(px, 1.0);
    dir = normalize(p1 - p0);
    eye = p0 - dir * (Extra.x / max(dot(dir, fwd), 1e-3));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint2 tiles = uint2(VMisc.z, VMisc.w);
    if (id.x >= tiles.x || id.y >= tiles.y) return;
    const float tilePx = Extra2.y;
    const uint parity = (uint)VCtl.y;

    if (VCtl.x > 0.5) {   // ---- mode 1: integrate the tile's column front to back
        float3 eye, fwd, dir;
        camera(min((float2(id.xy) + 0.5) * tilePx, Size.xy - 1.0), eye, fwd, dir);
        const float stretch = 1.0 / max(dot(dir, fwd), 1e-3);    // view depth -> distance along the ray
        float3 S = 0.0; float T = 1.0;
        const uint n = (uint)VMisc.y;
        for (uint s = 0; s < n; ++s) {
            const int2 a = atlasPx(id.xy, s);
            const float4 h = parity == 0 ? Hist0[a] : Hist1[a];
            const float sig = h.a;
            if (sig > 1e-7) {
                const float dl = (sliceZ(s + 1.0) - sliceZ((float)s)) * stretch;
                const float tr = exp(-sig * dl);
                S += T * (h.rgb / sig) * (1.0 - tr);             // (energy-conserving: sigma dl -> 1 - e^-sigma dl)
                T *= tr;
            }
            Vol[a] = float4(Params.w > 0.0 ? min(S, Params.w) : S, T);   // (round 3: no path length greys the frame)
        }
        return;
    }

    // ---- mode 0: inject one froxel (id.z = its slice)
    const uint sl = id.z;
    uint seed = hash(id.x * 1973u + id.y * 9277u + sl * 26699u + (uint)Size.z * 39119u);
    const float2 px = min((float2(id.xy) + float2(rnd(seed), rnd(seed))) * tilePx, Size.xy - 1.0);
    float3 eye, fwd, dir;
    camera(px, eye, fwd, dir);
    const int2 a = atlasPx(id.xy, sl);
    // the tile's farthest surface (5 taps): froxels well behind it are never seen - no rays, no medium
    float zFar = 0.0;
    [unroll] for (int k = 0; k < 5; ++k) {
        const float2 o = k == 4 ? float2(0.5, 0.5) : float2((k & 1) ? 0.95 : 0.05, (k & 2) ? 0.95 : 0.05);
        const int2 q = min(int2((float2(id.xy) + o) * tilePx), int2(Size.xy) - 1);
        const float d = RasterDepth[q];
        zFar = max(zFar, d >= 1.0 ? VMisc.x : dot(worldAt(float2(q) + 0.5, d) - eye, fwd));
    }
    if (sliceZ((float)sl) > zFar + 1.0) {
        if (parity == 0) Hist0[a] = 0.0; else Hist1[a] = 0.0;
        return;
    }
    const float z = sliceZ((float)sl + rnd(seed));
    const float3 x = eye + dir * (z / max(dot(dir, fwd), 1e-3));
    const float sig = density(x);
    float3 L = 0.0;
    if (sig > 0.0) {
        const uint mask = (uint)Params.z;
        // (round 1: over a long sunlit path the in-scatter saturates toward the sun's radiance x phase - open sky went white;
        // the phase is normalised to its forward peak, so VSun is the brightness of fully lit air looking into the sun)
        // (package 7: the death bodies (0x10) are left out - a full froxel grid of sun rays through every body's BLAS is the
        // suspect for af4d0731's ~1.8 ms; the live characters (0x4) keep their shadows in the air)
        if (ToSun.w > 0.5 && !blocked(x, ToSun.xyz, 2000.0, mask & ~0x10u))
            L += VSun.rgb * (hg(dot(dir, ToSun.xyz), VSun.w) / hg(1.0, VSun.w));
        const uint base = (uint)Extra.z;
        const uint nl = (uint)Extra2.z;                          // the in-view lamps (ring + 20 positions / + 24 colours)
        [loop] for (uint k = 0; k < 4; ++k) {
            if (k >= nl) break;
            const float4 Lp = ShadowLights[base + 20 + k], Lc = ShadowLights[base + 24 + k];
            const float3 dl = Lp.xyz - x;
            const float d2 = dot(dl, dl), r2 = max(Lp.w * Lp.w, 1e-4);
            if (d2 >= r2) continue;
            const float att = pow(saturate(1.0 - d2 / r2), max(Lc.w, 0.5));
            if (att < 1e-3) continue;
            const float dlen = sqrt(d2);
            const float3 ldir = dl / max(dlen, 1e-4);
            // (phase as the sun's: ldir points at the light, so dot(dir, ldir) = 1 looks straight into it)
            // (round 1: the UE falloff alone stays high over a lamp's whole radius - every lamp filled its room with an even
            // glow, the grey veil on Gorge; the inverse-square core keeps the scattered light at the bulb, a halo)
            const float r0 = max(Size.w, 0.05), core = r0 * r0 / (r0 * r0 + d2);
            if (!blocked(x, ldir, dlen, mask & ~0x14u)) L += Lc.rgb * att * core * hg(dot(dir, ldir), 0.3) * Extra.w;
        }
        const uint nt = (uint)VCtl.w;                            // transient lights (ring + 4 positions / + 12 colours)
        [loop] for (uint t = 0; t < 4; ++t) {
            if (t >= nt) break;
            const float4 Tp = ShadowLights[base + 4 + t], Tc = ShadowLights[base + 12 + t];
            const float3 dl = Tp.xyz - x;
            const float x2 = dot(dl, dl) / max(Tp.w * Tp.w, 1e-6);
            if (x2 >= 1.0) continue;
            // (round 3: unshadowed over a whole effect light's radius, a firefight's flashes filled the air - the same
            // inverse-square core as the lamps keeps the glow at the flash)
            const float r0 = max(Size.w, 0.05), core = r0 * r0 / (r0 * r0 + dot(dl, dl));
            L += Tc.rgb * pow(saturate(1.0 - x2), max(Tc.w, 0.5)) * core * (1.0 / 12.566371) * Extra2.w;
        }
    }
    float4 cur = float4(sig * L, sig);
    // temporal: the same point in the previous frame's grid (its view-projection columns at ring + 8)
    if (VCtl.z > 0.5 && sig > 0.0) {
        const uint b = (uint)Extra.z + 8;
        const float4 clip = ShadowLights[b] * x.x + ShadowLights[b + 1] * x.y + ShadowLights[b + 2] * x.z + ShadowLights[b + 3];
        if (clip.w > kNear) {
            const float2 uv = clip.xy / clip.w * 0.5 + 0.5;
            const float ps = zSlice(clip.w);
            if (all(uv >= 0.0) && all(uv < 1.0) && ps >= 0.0 && ps < VMisc.y) {
                const uint2 pt = min(uint2(uv * Size.xy / tilePx), tiles - 1);
                const int2 pa = atlasPx(pt, (uint)ps);
                const float4 prev = parity == 0 ? Hist1[pa] : Hist0[pa];
                if (prev.a > 0.0) cur = lerp(cur, prev, Extra2.x);
            }
        }
    }
    if (parity == 0) Hist0[a] = cur; else Hist1[a] = cur;
}
