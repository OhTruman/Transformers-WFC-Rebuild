// Clean-room reconstruction — M7 stage 1 (PC ADAPTATION; OFF by default): the AMD FidelityFX shadow denoiser (SDK v1.1.4,
// MIT; tools/render/fetch_ffx_denoiser.py) driven directly: its five compute passes, compiled by the pinned DXC with their
// embedded root signatures, on the RT bridge's device and command list. Inputs come from rt_shadow_ao.hlsl (the per-tile
// hit mask, world normals, camera-reprojection velocity) and the shared GL depth; the output is the denoised sun
// visibility in an R8 target the bridge shares with GL.
#pragma once
#if defined(_WIN32)
#include <d3d12.h>

namespace render {

struct RtDenoiseInputs {
    ID3D12Resource* depth = nullptr;      // R32F, GL window depth (shared, COMMON)
    ID3D12Resource* hitMask = nullptr;    // R32_UINT, one bit per pixel of an 8 x 4 tile (set = occluded), UAV state
    ID3D12Resource* normal = nullptr;     // RGBA16F world normal, UAV state
    ID3D12Resource* velocity = nullptr;   // RG16F previous uv - uv (top-down uv), UAV state
    ID3D12Resource* output = nullptr;     // R8_UNORM denoised visibility (shared, COMMON)
};

struct RtDenoiseParams {                   // all FidelityFX conventions (ndc y up = 1 - 2v, depth 0 .. 1), column-major
    float eye[3];
    float projectionInverse[16];
    float reprojection[16];
    float viewProjectionInverse[16];
    unsigned frameIndex = 0;
    bool firstFrame = false;
};

class RtShadowDenoiser {
public:
    bool init(ID3D12Device* dev);                                       // PSOs, root signatures (false: unavailable)
    bool resize(int w, int h, const RtDenoiseInputs& in);               // internal targets + descriptors at this size
    void record(ID3D12GraphicsCommandList* list, const RtDenoiseParams& p, int slot);
    void release();
    // (diagnostics) copy the tile buffer and the tile metadata into a readback buffer (2 x tiles x 4 bytes), then scratch 0 and
    // scratch 1 (RG16F, rows of debugRowPitch() bytes)
    unsigned debugRowPitch() const { return ((unsigned)w_ * 4 + 255) & ~255u; }
    void debugCopy(ID3D12GraphicsCommandList* list, ID3D12Resource* readback);
    unsigned tiles() const { return tiles_; }
    bool ready() const { return ready_; }

private:
    void block(int pass, int variant, const ID3D12Resource* const* srv, const DXGI_FORMAT* srvFmt, int nSrv,
               const ID3D12Resource* const* uav, const DXGI_FORMAT* uavFmt, int nUav);
    ID3D12Device* dev_ = nullptr;
    ID3D12PipelineState* pso_[5] = {};
    ID3D12RootSignature* rs_[5] = {};
    ID3D12DescriptorHeap* heap_ = nullptr;
    ID3D12Resource* moments_[2] = {}; ID3D12Resource* scratch_[2] = {}; ID3D12Resource* prevDepth_ = nullptr;
    ID3D12Resource* tileBuffer_ = nullptr; ID3D12Resource* tileMeta_ = nullptr;
    ID3D12Resource* cb_ = nullptr;                                      // upload: 2 slots x 3 constant blocks x 256 B
    unsigned char* cbPtr_ = nullptr;
    RtDenoiseInputs in_;
    int w_ = 0, h_ = 0;
    unsigned tiles_ = 0;
    bool ready_ = false, cleared_ = false;
};

}  // namespace render
#endif
