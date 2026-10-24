// Clean-room reconstruction — M7 stage 0 groundwork (PC ADAPTATION; OFF by default): the GL -> D3D12 -> GL round trip in
// the middle of the frame that ray-traced passes (architecture A: inline RayQuery reading GL's depth, masks back to GL)
// need. WFC_RT_ROUNDTRIP=1 measures it: after the world depth prepass the depth goes to D3D12, a stand-in D3D12 job
// (a copy into the "mask" texture) runs, and GL's shading waits for it on the GPU. WFC_RT_DEPTHCHECK=1: the job is
// instead one primary ray per pixel through the static world's TLAS, compared with GL's depth (stage 0's exit check).
#pragma once
#include <cstdint>
#include <vector>
#include <utility>
typedef unsigned int GLuint;

namespace render {

struct RtTargets { GLuint depth = 0, mask = 0, shadowAo = 0, shadowDn = 0, worldDepth = 0, refl = 0, shadowSoft = 0, lightVis = 0,
                   prevColor = 0, vol = 0; };   // (X3) vol: the integrated froxel grid, an 8 x 8 atlas of 64 slices of (w / 16) x (h / 16) tiles, RGBA16F: rgb in-scatter, a transmittance   // (item 3) GL writes the finished HDR scene at the frame end; reflections read it at character hits   // render size: depth / mask R32F,
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
    float aoRadiusDyn = 0.0f;   // (X5) the AO radius on dynamic pixels (characters, movers), m; 0 = aoRadius
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
    bool softShadow = false;  // (R2) contact-hardening penumbrae after the denoiser -> RtTargets::shadowSoft
    float penumbraTan = 0.0f; // (R2) tan(the sun's half angle) for the penumbra width
    float softMaxRadius = 24.0f;   // (R2) the widest penumbra, pixels
    bool debugDump = false;
    // X3 (Realistic): the volumetric light pass - the sun's colour x brightness (rgb) + phase g, the medium (density 1 / m,
    // height falloff 1 / m, fog height m, haze 1 / m), max distance m / steps, ambient in-scatter colour
    bool volumetric = false;
    float volSun[4] = {0, 0, 0, 0.6f}, volFog[4] = {0, 0, 0, 0}, volMisc[2] = {150.0f, 24.0f}, volAmb[3] = {0, 0, 0};
    float volLampK = 1.0f;   // (v2) the lamp halos' strength
    float volTransK = 1.0f, volHistory = 0.9f, volLampCore = 2.0f, volCap = 0.0f;   // (froxels) the transient lights' strength; the history weight
    float volLights[4][4] = {}, volColors[4][4] = {}; int volLightN = 0;   // (v3) the halo lamps: in view, by coverage x brightness   // (diagnostics) write the denoiser's tile buffers to rt_dump_tiles.bin
    // (item 2) up to 4 point / spot lights shadowed per pixel (xyz position, w radius): RtTargets::lightVis RGBA = each
    // one's visibility - on dynamic pixels from everything, on world pixels from the characters only
    float shadowLights[4][4] = {}; int shadowLightN = 0;
    float shadowColors[4][4] = {};   // (X3 v2: lamp halos) their linear colour x brightness (rgb), falloff exponent (w)
    // (item 4) up to 4 transient lights (xyz, w radius), traced against everything (world + characters): LightVis channel k
    // bit 1 (bit 0 = shadow light k) - each channel holds (static visible ? 1 : 0) + (transient visible ? 2 : 0), / 255
    float transLights[4][4] = {}; int transLightN = 0;
    float transClear = 0.05f;   // (user playtest 2026-10-23 #16) the lamp-side end of a transient shadow ray (m): the owner's hull
    float transColors[4][4] = {};   // (v3 row 6) their linear colour x brightness (rgb) and falloff exponent (w)
    // (item 3) the previous frame's view-projection (GL column-major) of RtTargets::prevColor; prevValid = it holds a frame
    float prevVP[16] = {}; bool prevValid = false;
};

bool rtBridgeAvailable();                                  // a DXR 1.1 device on GL's adapter (a throwaway probe)
void rtBridgeShutdown();                                   // process exit, GL context current: queue idle, GL imports released
bool rtBridgePrepare(int w, int h, RtTargets* t);           // (re)create at this size; false: unavailable
void rtBridgeSelectView(int view);                          // (split screen) view 0 / 1: its own targets + history
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
// 8 RGB10A2, 9 RGBA16, 10 RGBA16F, 11 BC4, 12 BC7 (M6 modern maps, linear)
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
bool rtBridgeSetStaticMask(unsigned idBase, unsigned mask);   // (chapter streaming) a static set's mask, 0 = untraced
bool rtBridgeRemoveStaticSet(unsigned idBase);                // (chapter streaming) a static set freed (BLAS + instances)
bool rtBridgeAddMasked(const RtMaskedWorld& m);             // non-opaque BLAS (instance mask 0x8) + lookup buffers
// GL buffers written before the signal that D3D12 reads (Systems' skinned positions): signalled and reacquired with
// the targets on every round trip until changed (n = 0 clears)
void rtBridgeSetSignalBuffers(const GLuint* bufs, int n);
// R4: the runtime ray-traced lightmap (render/gl/WfcRtGi.cpp, shaders/rt_gi.hlsl). Setup: the texels (8 x u32 each, as
// GiTexel), the gutter list (4 x u32: dst, src, src sub, 0), GiData (float4s: subs, lights, the hit page map), per GL
// lightmap page its size (0 = none) -> per page a shared GL texture (RGBA16F; 0 = none). A job: constants of one
// texel range (+ the gutter); queued into the next round trip, or run now in whole passes (load time).
struct RtGiSetup { std::vector<uint32_t> texels, dilate; std::vector<float> data; std::vector<std::pair<int, int>> pageSize;
                   int floatPage = -1;      // (floatPage: RGBA32F - the vertex-lit subs' page)
                   std::vector<int> hitToGi; };   // (item 3) reflection hit page -> its GI page: reflections see the RT lightmap
struct RtGiJob {
    float toSun[4] = {0, 0, 0, 0}, sunCol[4] = {0, 0, 0, 0}, sky[4] = {0, 0, 0, 0};
    uint32_t first = 0, count = 0, frame = 0, lights = 0;
    float mix[4] = {0.05f, 1.0f, 1.0f, 1.0f};   // minimum blend weight, tangent-y sign, sun exponent p, bounce on
    uint32_t subOff = 0, lightOff = 0, pageMapOff = 0, mask = 0x0Bu;
    bool dilate = false;
    float lightSize = 0.0f;                     // (item 5) the static lights' source radius, m (0: hard)
    float emissiveGain = 1.0f;                  // (Molten plan a) emissive surfaces as GI light sources
    float bounceSat = 1.0f;                     // (X4) the bounce reflectance's saturation (luminance kept; 1 = the albedo's)
};
bool rtBridgeGiSetup(const RtGiSetup& s, std::vector<GLuint>& glTex);
double rtBridgeVramMB();                                  // (leak diagnostics) the process's local video memory, MB (0: unknown)
void rtBridgeGiQueue(const RtGiJob& j);
bool rtBridgeGiRunNow(const RtGiJob& j, int passes, unsigned chunk);
// S3: the probe grid (origin, spacing, dims) traced from the current RT lightmap with the job's offsets / mask / bounce
// gain, rays per probe; out: 24 floats per probe (6 x (rgb, w): the ambient cube +X -X +Y -Y +Z -Z; w of axis 0 = the
// share of front-face hits, of axis 1 = the share of rays that hit)
bool rtBridgeGiProbes(const RtGiJob& j, const float origin[3], float spacing, const int dims[3], int rays,
                      const std::vector<uint32_t>& cells, std::vector<float>& out);   // (cells: the sparse grid's cells, x fastest)

}  // namespace render
