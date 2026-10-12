// Clean-room reconstruction — M7 stage 0 groundwork (PC ADAPTATION; OFF by default): the GL -> D3D12 -> GL round trip in
// the middle of the frame that ray-traced passes (architecture A: inline RayQuery reading GL's depth, masks back to GL)
// need. WFC_RT_ROUNDTRIP=1 measures it: after the world depth prepass the depth goes to D3D12, a stand-in D3D12 job
// (a copy into the "mask" texture) runs, and GL's shading waits for it on the GPU.
#pragma once
typedef unsigned int GLuint;

namespace render {

struct RtTargets { GLuint depth = 0, mask = 0; };            // render size, R32F both (shared with D3D12)

bool rtBridgePrepare(int w, int h, RtTargets* t);           // (re)create at this size; false: unavailable
bool rtBridgeRoundTrip();                                   // GL signal -> D3D12 job -> GL wait (GPU side)

}  // namespace render
