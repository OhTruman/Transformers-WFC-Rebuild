// Clean-room reconstruction — see D3D12RtDenoise.h. The pass sequence follows AMD's host code (ffx_denoiser.cpp,
// SDK v1.1.4, MIT): prepare shadow mask (8 x 4 tiles -> tile buffer), tile classification (moments ping-pong by frame
// parity, reprojection into scratch 0), copy depth -> previous depth, then three soft-shadow filter passes ping-ponging
// scratch 0 / 1, the last writing the output. Each pass's root signature is the one embedded in its shader:
// [0] UAV table (45 slots from u0), [1] SRV table (45 slots from t0), [2] root CBV b0, static linear-clamp sampler s0.
#if defined(_WIN32) && defined(WFC_RT_DENOISER)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

#include "render/d3d12/D3D12RtDenoise.h"
#include "core/Log.h"

#include "ffx_dn_prepare.h"
#include "ffx_dn_tile.h"
#include "ffx_dn_filter0.h"
#include "ffx_dn_filter1.h"
#include "ffx_dn_filter2.h"

#include <algorithm>
#include <cstring>

namespace render {
namespace {
constexpr int kSlots = 45;                 // FFX_DENOISER_RESOURCE_IDENTIFIER_COUNT
constexpr int kBlocks = 6;                 // prepare, tile (even), tile (odd), filter 0, 1, 2
constexpr int kBlockSize = 2 * kSlots;     // UAV table, then SRV table
constexpr int kClearBase = kBlocks * kBlockSize;   // 4 UAVs for the first-frame clears (moments 0 / 1, scratch 0 / 1)
template <class T> void rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

ID3D12Resource* tex(ID3D12Device* d, int w, int h, DXGI_FORMAT f, D3D12_RESOURCE_STATES s) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = (UINT64)w; rd.Height = (UINT)h;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.Format = f; rd.SampleDesc.Count = 1; rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* r = nullptr;
    return SUCCEEDED(d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, s, nullptr, __uuidof(ID3D12Resource), (void**)&r)) ? r : nullptr;
}
ID3D12Resource* buf(ID3D12Device* d, UINT64 bytes, D3D12_HEAP_TYPE t, D3D12_RESOURCE_STATES s, D3D12_RESOURCE_FLAGS fl) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = t;
    D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = std::max<UINT64>(bytes, 256); rd.Height = 1;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; rd.Flags = fl;
    ID3D12Resource* r = nullptr;
    return SUCCEEDED(d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, s, nullptr, __uuidof(ID3D12Resource), (void**)&r)) ? r : nullptr;
}
void transition(ID3D12GraphicsCommandList* l, ID3D12Resource* const* rs, int n, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER bar[8]{};
    for (int i = 0; i < n; ++i) {
        bar[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; bar[i].Transition.pResource = rs[i];
        bar[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        bar[i].Transition.StateBefore = a; bar[i].Transition.StateAfter = b;
    }
    l->ResourceBarrier((UINT)n, bar);
}
void uavBarrier(ID3D12GraphicsCommandList* l) { D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; l->ResourceBarrier(1, &b); }
}  // namespace

bool RtShadowDenoiser::init(ID3D12Device* dev) {
    dev_ = dev;
    const void* code[5] = {g_ffx_dn_prepare, g_ffx_dn_tile, g_ffx_dn_filter0, g_ffx_dn_filter1, g_ffx_dn_filter2};
    const size_t size[5] = {sizeof g_ffx_dn_prepare, sizeof g_ffx_dn_tile, sizeof g_ffx_dn_filter0, sizeof g_ffx_dn_filter1, sizeof g_ffx_dn_filter2};
    for (int i = 0; i < 5; ++i) {
        if (FAILED(dev_->CreateRootSignature(0, code[i], size[i], __uuidof(ID3D12RootSignature), (void**)&rs_[i]))) {
            LOG_WARN("RT denoiser: root signature %d (embedded) failed", i); return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature = rs_[i]; pd.CS.pShaderBytecode = code[i]; pd.CS.BytecodeLength = size[i];
        if (FAILED(dev_->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&pso_[i]))) {
            LOG_WARN("RT denoiser: pass %d PSO failed (shader model 6.6: the Agility runtime)", i); return false;
        }
    }
    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = kClearBase + 4;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev_->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&heap_))) return false;
    cb_ = buf(dev_, 2 * 3 * 256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE);
    if (!cb_ || FAILED(cb_->Map(0, nullptr, (void**)&cbPtr_))) return false;
    LOG_INFO("RT denoiser: FidelityFX shadow denoiser (5 passes, SDK v1.1.4) ready");
    return true;
}

void RtShadowDenoiser::block(int pass, int variant, const ID3D12Resource* const* srv, const DXGI_FORMAT* srvFmt, int nSrv,
                             const ID3D12Resource* const* uav, const DXGI_FORMAT* uavFmt, int nUav) {
    (void)pass;
    const UINT inc = dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = heap_->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)variant * kBlockSize * inc;
    for (int i = 0; i < kSlots; ++i, h.ptr += inc) {   // UAV table
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
        ID3D12Resource* r = i < nUav ? const_cast<ID3D12Resource*>(uav[i]) : nullptr;
        if (r && uavFmt[i] == DXGI_FORMAT_UNKNOWN) {     // structured uint buffer (one per tile)
            u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; u.Format = DXGI_FORMAT_UNKNOWN;
            u.Buffer.NumElements = tiles_; u.Buffer.StructureByteStride = 4;
        } else { u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; u.Format = r ? uavFmt[i] : DXGI_FORMAT_R32_FLOAT; }
        dev_->CreateUnorderedAccessView(r, nullptr, &u, h);
    }
    for (int i = 0; i < kSlots; ++i, h.ptr += inc) {   // SRV table
        D3D12_SHADER_RESOURCE_VIEW_DESC s{}; s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; s.Texture2D.MipLevels = 1;
        ID3D12Resource* r = i < nSrv ? const_cast<ID3D12Resource*>(srv[i]) : nullptr;
        s.Format = r ? srvFmt[i] : DXGI_FORMAT_R32_FLOAT;
        dev_->CreateShaderResourceView(r, &s, h);
    }
}

bool RtShadowDenoiser::resize(int w, int h, const RtDenoiseInputs& in) {
    release();
    in_ = in; w_ = w; h_ = h;
    tiles_ = (unsigned)(((w + 7) / 8) * ((h + 3) / 4));
    const D3D12_RESOURCE_STATES srvS = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    for (int i = 0; i < 2; ++i) {
        moments_[i] = tex(dev_, w, h, DXGI_FORMAT_R11G11B10_FLOAT, srvS);
        scratch_[i] = tex(dev_, w, h, DXGI_FORMAT_R16G16_FLOAT, srvS);
    }
    prevDepth_ = tex(dev_, w, h, DXGI_FORMAT_R32_FLOAT, srvS);
    const D3D12_RESOURCE_FLAGS uavF = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    tileBuffer_ = buf(dev_, 4ull * tiles_, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, uavF);
    tileMeta_ = buf(dev_, 4ull * tiles_, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, uavF);
    if (!moments_[0] || !moments_[1] || !scratch_[0] || !scratch_[1] || !prevDepth_ || !tileBuffer_ || !tileMeta_) {
        LOG_WARN("RT denoiser: targets failed"); release(); return false;
    }
    const DXGI_FORMAT U = DXGI_FORMAT_UNKNOWN, R32 = DXGI_FORMAT_R32_FLOAT, RG16 = DXGI_FORMAT_R16G16_FLOAT,
                      R11 = DXGI_FORMAT_R11G11B10_FLOAT, N16 = DXGI_FORMAT_R16G16B16A16_FLOAT, R8 = DXGI_FORMAT_R8_UNORM;
    {   // prepare: t0 hit mask; u0 shadow mask (= tile buffer)
        const ID3D12Resource* s[] = {in.hitMask}; const DXGI_FORMAT sf[] = {DXGI_FORMAT_R32_UINT};
        const ID3D12Resource* u[] = {tileBuffer_}; const DXGI_FORMAT uf[] = {U};
        block(0, 0, s, sf, 1, u, uf, 1);
    }
    for (int odd = 0; odd < 2; ++odd) {   // tile classification: t0 depth, t1 velocity, t2 normal, t3 history (scratch 1),
        // t4 previous depth, t5 previous moments; u0 tile metadata, u1 reprojection (scratch 0), u2 current moments,
        // u3 raytracer result (= tile buffer). Even frames: previous = moments 0, current = moments 1.
        const ID3D12Resource* s[] = {in.depth, in.velocity, in.normal, scratch_[1], prevDepth_, moments_[odd ? 1 : 0]};
        const DXGI_FORMAT sf[] = {R32, RG16, N16, RG16, R32, R11};
        const ID3D12Resource* u[] = {tileMeta_, scratch_[0], moments_[odd ? 0 : 1], tileBuffer_};
        const DXGI_FORMAT uf[] = {U, RG16, R11, U};
        block(1, 1 + odd, s, sf, 6, u, uf, 4);
    }
    for (int k = 0; k < 3; ++k) {   // filters: t0 depth, t1 normal, t2 input; u0 tile metadata, u1 history / output
        ID3D12Resource* inp = k == 1 ? scratch_[1] : scratch_[0];
        ID3D12Resource* out = k == 0 ? scratch_[1] : k == 1 ? scratch_[0] : in.output;
        const ID3D12Resource* s[] = {in.depth, in.normal, inp}; const DXGI_FORMAT sf[] = {R32, N16, RG16};
        const ID3D12Resource* u[] = {tileMeta_, out}; const DXGI_FORMAT uf[] = {U, k == 2 ? R8 : RG16};
        block(2 + k, 3 + k, s, sf, 3, u, uf, 2);
    }
    {   // first-frame clears: shader-visible copies + a CPU heap would be needed; the shader-visible ones double as both
        const UINT inc = dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE hc = heap_->GetCPUDescriptorHandleForHeapStart(); hc.ptr += (SIZE_T)kClearBase * inc;
        ID3D12Resource* r[4] = {moments_[0], moments_[1], scratch_[0], scratch_[1]};
        const DXGI_FORMAT f[4] = {R11, R11, RG16, RG16};
        for (int i = 0; i < 4; ++i, hc.ptr += inc) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC u{}; u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; u.Format = f[i];
            dev_->CreateUnorderedAccessView(r[i], nullptr, &u, hc);
        }
    }
    cleared_ = false; ready_ = true;
    LOG_INFO("RT denoiser: %dx%d, %u tiles", w, h, tiles_);
    return true;
}

void RtShadowDenoiser::record(ID3D12GraphicsCommandList* l, const RtDenoiseParams& p, int slot) {
    if (!ready_) return;
    // constants: cb0 (dims), cb1 (tile classification), cb2 (filters), HLSL packing, 256-byte blocks per ring slot
    unsigned char* base = cbPtr_ + (size_t)slot * 3 * 256;
    const D3D12_GPU_VIRTUAL_ADDRESS gva = cb_->GetGPUVirtualAddress() + (UINT64)slot * 3 * 256;
    std::memset(base, 0, 3 * 256);
    const int dims[2] = {w_, h_};
    const float inv[2] = {1.0f / (float)w_, 1.0f / (float)h_}, mv[2] = {1.0f, 1.0f}, unpack[2] = {1.0f, 0.0f};
    std::memcpy(base, dims, 8);
    unsigned char* c1 = base + 256;
    const int first = p.firstFrame || !cleared_ ? 1 : 0;
    std::memcpy(c1 + 0, p.eye, 12); std::memcpy(c1 + 12, &first, 4); std::memcpy(c1 + 16, dims, 8); std::memcpy(c1 + 24, inv, 8);
    std::memcpy(c1 + 32, mv, 8); std::memcpy(c1 + 40, unpack, 8);
    std::memcpy(c1 + 48, p.projectionInverse, 64); std::memcpy(c1 + 112, p.reprojection, 64); std::memcpy(c1 + 176, p.viewProjectionInverse, 64);
    unsigned char* c2 = base + 512;
    const float sigma = 1.0f;
    std::memcpy(c2 + 0, p.projectionInverse, 64); std::memcpy(c2 + 64, inv, 8); std::memcpy(c2 + 72, unpack, 8);
    std::memcpy(c2 + 80, dims, 8); std::memcpy(c2 + 88, &sigma, 4);

    const UINT inc = dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const D3D12_GPU_DESCRIPTOR_HANDLE g0 = heap_->GetGPUDescriptorHandleForHeapStart();
    const D3D12_CPU_DESCRIPTOR_HANDLE c0 = heap_->GetCPUDescriptorHandleForHeapStart();
    l->SetDescriptorHeaps(1, &heap_);
    // the inputs rt_shadow_ao wrote (UAV) are read as SRVs here
    ID3D12Resource* inputs[3] = {in_.hitMask, in_.normal, in_.velocity};
    transition(l, inputs, 3, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12Resource* hist[4] = {moments_[0], moments_[1], scratch_[0], scratch_[1]};
    if (!cleared_) {                                     // first frame: zero the moments and the scratch (history)
        transition(l, hist, 4, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const float z[4] = {0, 0, 0, 0};
        for (int i = 0; i < 4; ++i) {
            D3D12_GPU_DESCRIPTOR_HANDLE gh = g0; gh.ptr += (UINT64)(kClearBase + i) * inc;
            D3D12_CPU_DESCRIPTOR_HANDLE ch = c0; ch.ptr += (SIZE_T)(kClearBase + i) * inc;
            l->ClearUnorderedAccessViewFloat(gh, ch, hist[i], z, 0, nullptr);
        }
        transition(l, hist, 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cleared_ = true;
    }
    auto run = [&](int pass, int variant, D3D12_GPU_VIRTUAL_ADDRESS cb, UINT gx, UINT gy) {
        D3D12_GPU_DESCRIPTOR_HANDLE u = g0; u.ptr += (UINT64)variant * kBlockSize * inc;
        D3D12_GPU_DESCRIPTOR_HANDLE s = u; s.ptr += (UINT64)kSlots * inc;
        l->SetComputeRootSignature(rs_[pass]);
        l->SetPipelineState(pso_[pass]);
        l->SetComputeRootDescriptorTable(0, u);
        l->SetComputeRootDescriptorTable(1, s);
        l->SetComputeRootConstantBufferView(2, cb);
        l->Dispatch(gx, gy, 1);
        uavBarrier(l);
    };
    const UINT tx = (UINT)(w_ + 7) / 8, ty = (UINT)(h_ + 3) / 4;
    run(0, 0, gva, (UINT)(w_ + 31) / 32, (UINT)(h_ + 15) / 16);                 // prepare: 4 x 4 tiles per group
    const int odd = (int)(p.frameIndex & 1);
    ID3D12Resource* tw[2] = {scratch_[0], moments_[odd ? 0 : 1]};               // tile classification writes these
    transition(l, tw, 2, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    run(1, 1 + odd, gva + 256, tx, ty);
    transition(l, tw, 2, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    {   // copy current depth -> previous depth
        D3D12_RESOURCE_BARRIER b[2]{};
        for (auto& x : b) { x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; }
        b[0].Transition.pResource = in_.depth; b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b[1].Transition.pResource = prevDepth_; b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        l->ResourceBarrier(2, b);
        l->CopyResource(prevDepth_, in_.depth);
        for (auto& x : b) std::swap(x.Transition.StateBefore, x.Transition.StateAfter);
        l->ResourceBarrier(2, b);
    }
    for (int k = 0; k < 3; ++k) {
        ID3D12Resource* out = k == 0 ? scratch_[1] : k == 1 ? scratch_[0] : nullptr;   // (the output is shared: COMMON promotes)
        if (out) transition(l, &out, 1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        run(2 + k, 3 + k, gva + 512, tx, ty);
        if (out) transition(l, &out, 1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    transition(l, inputs, 3, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void RtShadowDenoiser::debugCopy(ID3D12GraphicsCommandList* l, ID3D12Resource* rb) {
    ID3D12Resource* b[2] = {tileBuffer_, tileMeta_};
    transition(l, b, 2, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    l->CopyBufferRegion(rb, 0, tileBuffer_, 0, 4ull * tiles_);
    l->CopyBufferRegion(rb, 4ull * tiles_, tileMeta_, 0, 4ull * tiles_);
    transition(l, b, 2, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const UINT64 base = (8ull * tiles_ + 511) & ~511ull;
    for (int k = 0; k < 2; ++k) {
        transition(l, &scratch_[k], 1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Offset = base + (UINT64)k * debugRowPitch() * (UINT64)h_;
        dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R16G16_FLOAT, (UINT)w_, (UINT)h_, 1, debugRowPitch()};
        D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = scratch_[k]; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        l->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        transition(l, &scratch_[k], 1, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
}

void RtShadowDenoiser::release() {
    for (auto& r : moments_) rel(r);
    for (auto& r : scratch_) rel(r);
    rel(prevDepth_); rel(tileBuffer_); rel(tileMeta_);
    ready_ = false;
}

}  // namespace render
#endif
