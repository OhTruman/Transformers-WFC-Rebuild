// Clean-room reconstruction — M7 R2 (PC ADAPTATION; RT modes only): contact-hardening sun shadows. After the FidelityFX
// shadow denoiser, a percentage-closer-soft-shadow style filter widens each penumbra by the occluder's distance: the
// sun ray stored its first blocker's distance (Normal.w, metres; 0 = lit); a blocker search around the pixel averages
// those distances, the penumbra is distance x 2 tan(the sun's half angle) wide in the world, projected to pixels by the
// pixel's spread at its view distance, and the denoised visibility is averaged over a disc of that size with depth /
// normal-aware weights. Sharp where a character meets the ground, wider as the shadow falls further from its caster.
// Half angle: the real sun's 0.27 degrees, or a larger "area light" (WFC_RT_SUNAREA). Same root signature as the RT passes,
// its own descriptor table D (D3D12RtBridge.cpp): t1 depth; u0 the soft output, u1 the denoised visibility, u2 the
// normals (read as UAVs: the shared output and the denoiser's inputs stay in UAV-compatible states). DXC cs_6_5.
cbuffer Frame : register(b0) {
    float4x4 InvViewProj;
    float4 ToSun;
    float4 Size;            // width, height, frame, -
    float4 Params;
    float4x4 Reproj;
    float4 Extra;           // znear, zfar, -, -
    float4 Extra2;          // -, pixel spread (radians), tan(the sun's half angle for penumbrae), max radius (pixels)
};
Texture2D<float> RasterDepth : register(t1);
RWTexture2D<float> Soft : register(u0);
RWTexture2D<float> Denoised : register(u1);        // the FidelityFX output (sun visibility 0..1)
RWTexture2D<float4> Normals : register(u2);        // world normal, w = the sun ray's first blocker distance (m), 0 lit

float3 worldAt(int2 px, float d) {
    const float2 ndc = (float2(px) + 0.5) / Size.xy * 2.0 - 1.0;
    float4 p = mul(InvViewProj, float4(ndc, d * 2.0 - 1.0, 1.0));
    return p.xyz / p.w;
}

// 16-tap Poisson disc (unit radius)
static const float2 kDisc[16] = {
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725), float2(-0.09418410, -0.92938870), float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464), float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420), float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590), float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790)};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const int2 px = int2(id.xy);
    if (id.x >= (uint)Size.x || id.y >= (uint)Size.y) return;
    const float v0 = Denoised[px];
    const float d = RasterDepth[px];
    if (d >= 1.0) { Soft[px] = v0; return; }
    const float3 p = worldAt(px, d), eye = worldAt(px, 0.0);
    const float dist = length(p - eye);
    const float pxWorld = max(dist * Extra2.y, 1e-5);          // one pixel's width at this depth (m)
    const float3 n0 = Normals[px].xyz;
    const float maxR = Extra2.w;
    // blocker search: the widest penumbra this pixel could be in (max radius), sparse taps
    float bSum = 0.0, bN = 0.0;
    const float rot = frac(sin(dot(float2(px), float2(12.9898, 78.233))) * 43758.5453) * 6.2831853;   // per-pixel rotation
    const float cr = cos(rot), sr = sin(rot);
    [unroll] for (uint k = 0; k < 16; ++k) {
        const float2 o = float2(kDisc[k].x * cr - kDisc[k].y * sr, kDisc[k].x * sr + kDisc[k].y * cr) * maxR;
        const int2 q = clamp(px + int2(o), int2(0, 0), int2(Size.xy) - 1);
        const float b = Normals[q].w;
        if (b > 0.0) { bSum += b; bN += 1.0; }
    }
    if (Normals[px].w > 0.0) { bSum += Normals[px].w * 4.0; bN += 4.0; }
    if (bN <= 0.0) { Soft[px] = v0; return; }                  // no blocker near: lit (or the denoiser's value)
    const float blocker = bSum / bN;
    const float penumbra = blocker * 2.0 * Extra2.z;            // world width (m)
    const float r = clamp(0.5 * penumbra / pxWorld, 0.0, maxR);
    if (r < 0.75) { Soft[px] = v0; return; }                   // contact: as sharp as the denoiser left it
    // the disc: same-surface taps (plane distance, normal) only
    float sum = v0, wsum = 1.0;
    [unroll] for (uint k2 = 0; k2 < 16; ++k2) {
        const float2 o = float2(kDisc[k2].x * cr - kDisc[k2].y * sr, kDisc[k2].x * sr + kDisc[k2].y * cr) * r;
        const int2 q = clamp(px + int2(round(o)), int2(0, 0), int2(Size.xy) - 1);
        const float dq = RasterDepth[q];
        if (dq >= 1.0) continue;
        const float3 pq = worldAt(q, dq);
        const float plane = abs(dot(pq - p, n0)) / pxWorld;     // off the receiver's plane, in pixels of depth
        const float w = exp(-plane * 0.5) * saturate(dot(Normals[q].xyz, n0) * 2.0 - 1.0);
        sum += Denoised[q] * w; wsum += w;
    }
    Soft[px] = sum / wsum;
}
