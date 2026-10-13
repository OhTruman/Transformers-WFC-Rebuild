// Clean-room reconstruction — M7 stage 0 groundwork (PC ADAPTATION; OFF by default): the GL -> D3D12 -> GL round trip in
// the middle of the frame that ray-traced passes (architecture A: inline RayQuery reading GL's depth, masks back to GL)
// need. WFC_RT_ROUNDTRIP=1 measures it: after the world depth prepass the depth goes to D3D12, a stand-in D3D12 job
// (a copy into the "mask" texture) runs, and GL's shading waits for it on the GPU. WFC_RT_DEPTHCHECK=1: the job is
// instead one primary ray per pixel through the static world's TLAS, compared with GL's depth (stage 0's exit check).
#pragma once
#include <cstdint>
#include <vector>
typedef unsigned int GLuint;

namespace render {

struct RtTargets { GLuint depth = 0, mask = 0, shadowAo = 0, shadowDn = 0, worldDepth = 0, refl = 0; };   // render size: depth / mask R32F,
                                                                 // shadowAo RGBA16F (raw sun, AO, dynamic), shadowDn R8
                                                                 // (denoised sun), worldDepth R32F (before the dynamic draws)

// the static world as GL's depth prepass draws it: positions (xyz, world space), triangle lists for one-sided
// (back faces culled, GL counter-clockwise front) and two-sided geometry
struct RtStaticWorld { std::vector<float> pos; std::vector<uint32_t> oneSided, twoSided; unsigned idBase = 0; };   // InstanceID = idBase + side

struct RtFrame {
    float invViewProj[16];    // GL clip -> world, column-major
    float viewRow2[4];        // view matrix row 2 (view depth = -dot(row, (p, 1)))
    unsigned instanceMask;    // the sets the rays see (the set whose prepass depth is compared)
    int mode = 1;             // 1 depth check, 2 sun shadow + AO (stage 1)
    float toSun[4] = {0, 0, 0, 0};   // unit vector towards the sun, w = 1 when there is one
    int frameIndex = 0, aoRays = 2;
    float aoRadius = 1.0f, bias = 0.0005f;
    float sunConeTan = 0.0f;  // tan(the sun cone's half angle): soft shadows (0 = hard)
    // the FidelityFX conventions (ndc (x, 1 - 2v, depth 0 .. 1)), column-major: for the denoiser and the velocity
    float eye[3] = {0, 0, 0};
    float ffxProjInv[16] = {}, ffxReproj[16] = {}, ffxViewProjInv[16] = {};
    bool resetHistory = false;
    float znear = 0.1f, zfar = 1000.0f;   // (the AO filter's depth linearisation)
    bool reflect = false;     // stage 2 prototype: the reflection pass (WFC_RT_REFLECT)
    bool reflectShade = true; // ... with hit shading (false: hit distance only - the A / B for the shading cost)
    float time = 0.0f;        // (reflection hits) the materials' time, s
    float pixelSpread = 0.0f; // (reflection hits) a pixel's angle (radians): the ray cone's spread
    bool debugDump = false;   // (diagnostics) write the denoiser's tile buffers to rt_dump_tiles.bin
};

bool rtBridgeAvailable();                                  // a DXR 1.1 device on GL's adapter (creates it once)
bool rtBridgePrepare(int w, int h, RtTargets* t);           // (re)create at this size; false: unavailable
bool rtBridgeAddStaticWorld(const RtStaticWorld& world, unsigned mask);   // add a set's BLAS, rebuild the TLAS (map load)
void rtBridgeClearStaticWorld();                            // map unload
// GL signal -> D3D12 job -> GL wait (GPU side). frame != nullptr and a static world: the depth check, else the copy.
bool rtBridgeRoundTrip(const RtFrame* frame);
// masked (alpha-tested) world geometry: per static set its masked triangles (indices into the set's vertices), the UV0
// per vertex, each triangle's sub; per sub (8 floats): uv min, 1 / uv extent, its atlas tile (u, v, width, height in 0..1)
struct RtMaskedSet { int set = 0; std::vector<float> pos, uv; std::vector<uint32_t> idx, triSub; };
struct RtMaskedWorld { std::vector<RtMaskedSet> sets; std::vector<float> subs; };
// stage 2 prototype - the reflection hits' shading: per static BLAS (InstanceID - 0x10000 = set * 2 + side) its triangles
// (indices into the set's vertices, as built) and their subs; per set the lightmap UV per vertex; per sub 8 float4
// (lightmap pages, lightmap UV transform, 3 scales, albedo, emissive, 0); the lightmap pages (RGBA8, sRGB or not)
struct RtHitBlas { int setIndex = 0; std::vector<uint32_t> idx, triSub; };
struct RtHitSet { std::vector<float> uv1; std::vector<float> vtx; };   // vtx (materials): 8 floats per vertex, see rt_reflect
// (materials) a texture copied from GL, mips from the cap down: data[face * levels + level], tightly packed rows
// (blocks for BC). format: 0 RGBA8, 1 RGBA8 sRGB, 2 BC1, 3 BC1 sRGB, 4 BC2, 5 BC2 sRGB, 6 BC3, 7 BC3 sRGB,
// 8 RGB10A2, 9 RGBA16, 10 RGBA16F
struct RtHitTexture { bool cube = false; int w = 0, h = 0, levels = 0, format = 0; std::vector<std::vector<unsigned char>> data; };
struct RtLightmap { int w = 0, h = 0; bool srgb = true; std::vector<unsigned char> rgba; };
struct RtHitData {
    std::vector<RtHitSet> sets; std::vector<RtHitBlas> blas; std::vector<float> subs; std::vector<RtLightmap> lightmaps;
    // materials (WFC_RT_MATDIR): the map's reflection shader (reflect_mat.cso), the 2D / cube textures (separate
    // index spaces), per material slot 12 floats (index, flags, lod bias, 0, unpack min, unpack scale)
    std::vector<unsigned char> materialShader; std::vector<RtHitTexture> textures, cubes; std::vector<float> matTex;
};
bool rtBridgeAddHitData(const RtHitData& d);
GLuint rtBridgeMaskAtlas(int w, int h);                     // the shared R8 opacity atlas (GL texture), made once
bool rtBridgeAddMasked(const RtMaskedWorld& m);             // non-opaque BLAS (instance mask 0x8) + lookup buffers
// GL buffers written before the signal that D3D12 reads (Systems' skinned positions): signalled and reacquired with
// the targets on every round trip until changed (n = 0 clears)
void rtBridgeSetSignalBuffers(const GLuint* bufs, int n);

}  // namespace render
