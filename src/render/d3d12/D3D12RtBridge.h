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

struct RtTargets { GLuint depth = 0, mask = 0; };            // render size, R32F both (shared with D3D12)

// the static world as GL's depth prepass draws it: positions (xyz, world space), triangle lists for one-sided
// (back faces culled, GL counter-clockwise front) and two-sided geometry
struct RtStaticWorld { std::vector<float> pos; std::vector<uint32_t> oneSided, twoSided; };

struct RtFrame {
    float invViewProj[16];    // GL clip -> world, column-major
    float viewRow2[4];        // view matrix row 2 (view depth = -dot(row, (p, 1)))
    unsigned instanceMask;    // the sets the rays see (the set whose prepass depth is compared)
};

bool rtBridgePrepare(int w, int h, RtTargets* t);           // (re)create at this size; false: unavailable
bool rtBridgeAddStaticWorld(const RtStaticWorld& world, unsigned mask);   // add a set's BLAS, rebuild the TLAS (map load)
void rtBridgeClearStaticWorld();                            // map unload
// GL signal -> D3D12 job -> GL wait (GPU side). frame != nullptr and a static world: the depth check, else the copy.
bool rtBridgeRoundTrip(const RtFrame* frame);

}  // namespace render
