// Clean-room reconstruction — M7 stage 1 (PC ADAPTATION; OFF by default): a depth-aware 7 x 7 filter of rt_shadow_ao's raw
// AO (2 rays per pixel are noisy). Reads the raw AO (.y) of the neighbours, writes the filtered AO into .w of its own
// texel (the other channels kept): no texel's .y is written, so the read / write in one dispatch is race-free. Weights:
// a 7 x 7 tent, times the relative view-distance similarity (depth edges stop it; the sky and the dynamic flag split it).
Texture2D<float> RasterDepth : register(t1);
RWTexture2D<float4> Mask : register(u0);      // sun, AO (raw), dynamic flag, AO (filtered)

cbuffer Frame : register(b0) {
    float4x4 InvViewProj;   // GL clip -> world (column-major)
    float4 ToSun;
    float4 Size;            // width, height, frame, AO rays
    float4 Params;
    float4x4 Reproj;
    float4 Lin;             // near, far (the GL perspective): view depth from window depth, analytically
};

float viewDist(int2 px, float d) {   // (view depth: enough for an edge-stopping weight, 49 taps without matrices)
    const float n = Lin.x, f = Lin.y;
    return 2.0 * n * f / (f + n - (d * 2.0 - 1.0) * (f - n));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const int2 px = int2(id.xy);
    if (id.x >= (uint)Size.x || id.y >= (uint)Size.y) return;
    const float4 c = Mask[px];
    const float d = RasterDepth[px];
    if (d >= 1.0) { Mask[px] = float4(c.xyz, 1.0); return; }
    const float z = viewDist(px, d);
    float sum = 0.0, wsum = 0.0;
    for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx) {
            const int2 q = clamp(px + int2(dx, dy), int2(0, 0), int2((int)Size.x - 1, (int)Size.y - 1));
            const float dq = RasterDepth[q];
            if (dq >= 1.0) continue;
            const float4 n = Mask[q];
            if (abs(n.z - c.z) > 0.5) continue;               // dynamic / world pixels kept apart
            const float zq = viewDist(q, dq);
            const float w = (4.0 - abs(dx)) * (4.0 - abs(dy)) * exp(-abs(zq - z) / max(0.02 * z, 0.01));
            sum += n.y * w; wsum += w;
        }
    Mask[px] = float4(c.xyz, wsum > 0.0 ? sum / wsum : c.y);
}
