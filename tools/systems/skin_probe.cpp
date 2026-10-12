// M7 stage-0 probe 2 (Systems, prototype; not part of the build): per-frame cost of ray-traceable skinned characters.
// 64 characters (8 robot bodies x 8, real meshes / joints / weights via the game's skinned loader), per frame:
//   1. upload this frame's joint matrices (random small rotations: the pose content does not change the cost),
//   2. compute skinning (D3D12 cs_5_0, 4 weights per vertex) into one positions buffer,
//   3. per character BLAS: refit in place (ALLOW_UPDATE | PREFER_FAST_BUILD, PERFORM_UPDATE) or, as the alternative, a full
//      rebuild (PREFER_FAST_BUILD),
//   4. TLAS rebuild over the 64 instances.
// GPU timestamps around each step, averaged over N frames (after 30 warm-up frames).
// Usage: skin_probe <robot.glb> ... [-frames N]
#include "assets/SkinnedModel.h"

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <random>
#include <string>
#include <vector>

#define CK(x) do { HRESULT hr_ = (x); if (FAILED(hr_)) { std::printf("FAILED %s: 0x%08lx (line %d)\n", #x, (unsigned long)hr_, __LINE__); return 1; } } while (0)

namespace {
ID3D12Device5* dev = nullptr;
ID3D12CommandQueue* q = nullptr;
ID3D12CommandAllocator* alloc = nullptr;
ID3D12GraphicsCommandList4* cl = nullptr;
ID3D12Fence* fence = nullptr;
UINT64 fenceVal = 0;
HANDLE ev = nullptr;

ID3D12Resource* buffer(UINT64 size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES st, D3D12_RESOURCE_FLAGS fl = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = heap;
    D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = size < 256 ? 256 : size; rd.Height = 1;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; rd.Flags = fl;
    ID3D12Resource* r = nullptr;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, st, nullptr, IID_PPV_ARGS(&r)))) return nullptr;
    return r;
}
void execWait() {
    cl->Close();
    ID3D12CommandList* l[] = {cl};
    q->ExecuteCommandLists(1, l);
    q->Signal(fence, ++fenceVal);
    fence->SetEventOnCompletion(fenceVal, ev);
    WaitForSingleObject(ev, INFINITE);
    alloc->Reset(); cl->Reset(alloc, nullptr);
}
void uavBarrier() { D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; cl->ResourceBarrier(1, &b); }

const char* kCS = R"(
cbuffer C : register(b0) { uint vCount; uint vBase; uint jBase; uint pad; };
StructuredBuffer<float3> bindPos : register(t0);
StructuredBuffer<uint4> joints : register(t1);
StructuredBuffer<float4> weights : register(t2);
StructuredBuffer<float4x4> mats : register(t3);
RWByteAddressBuffer outPos : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint v = id.x;
    if (v >= vCount) return;
    uint g = vBase + v;
    float3 p = bindPos[g];
    uint4 j = joints[g];
    float4 w = weights[g];
    float4x4 m = mats[jBase + j.x] * w.x + mats[jBase + j.y] * w.y + mats[jBase + j.z] * w.z + mats[jBase + j.w] * w.w;
    float3 s = mul(m, float4(p, 1.0)).xyz;
    outPos.Store3(g * 12, asuint(s));
}
)";

struct Char { UINT vBase, vCount, iBase, iCount, jBase, jCount; ID3D12Resource* blas = nullptr; ID3D12Resource* scratch = nullptr; };
}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> files;
    int frames = 300;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-frames") == 0 && i + 1 < argc) { frames = std::atoi(argv[++i]); continue; }
        files.push_back(argv[i]);
    }
    if (files.empty()) { std::printf("usage: skin_probe <robot.glb> ... [-frames N]\n"); return 2; }
    CK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev)));
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    CK(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q)));
    CK(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)));
    CK(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&cl)));
    CK(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    UINT64 tsFreq = 0; q->GetTimestampFrequency(&tsFreq);

    // ---- load the bodies, replicate to 64 ----
    std::vector<assets::SkinnedModel> models(files.size());
    for (size_t i = 0; i < files.size(); ++i)
        if (!assets::loadSkinnedGlb(files[i], models[i]) || !models[i].valid()) { std::printf("load failed: %s\n", files[i].c_str()); return 1; }
    std::vector<float> pos; std::vector<uint32_t> jnt, idx; std::vector<float> wgt;
    std::vector<Char> chars;
    UINT jTotal = 0;
    for (int c = 0; c < 64; ++c) {
        const assets::SkinnedModel& m = models[(size_t)c % models.size()];
        Char ch;
        ch.vBase = (UINT)(pos.size() / 3); ch.vCount = (UINT)m.vertexCount();
        ch.iBase = (UINT)idx.size(); ch.iCount = (UINT)m.indices.size();
        ch.jBase = jTotal; ch.jCount = (UINT)m.skinJoints.size(); jTotal += ch.jCount;
        pos.insert(pos.end(), m.positions.begin(), m.positions.end());
        for (size_t k = 0; k < m.joints.size(); ++k) jnt.push_back(std::min<uint32_t>(m.joints[k], ch.jCount - 1));
        wgt.insert(wgt.end(), m.weights.begin(), m.weights.end());
        for (uint32_t ix : m.indices) idx.push_back(ix);   // per-character index buffer region (vertex indices are local)
        chars.push_back(ch);
    }
    const UINT vTotal = (UINT)(pos.size() / 3);
    std::printf("64 characters from %zu bodies: %u vertices, %zu triangles, %u joints\n", files.size(), vTotal, idx.size() / 3, jTotal);

    // ---- buffers ----
    auto upload = [&](const void* data, UINT64 size) {
        ID3D12Resource* r = buffer(size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        void* p = nullptr; r->Map(0, nullptr, &p); std::memcpy(p, data, (size_t)size); r->Unmap(0, nullptr); return r;
    };
    // GPU-local copies (an UPLOAD-heap input is read over PCIe every time: the first version measured that, not the work)
    auto local = [&](const void* data, UINT64 size) {
        ID3D12Resource* up = upload(data, size);
        ID3D12Resource* r = buffer(size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
        cl->CopyBufferRegion(r, 0, up, 0, size);
        D3D12_RESOURCE_BARRIER t{}; t.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; t.Transition.pResource = r;
        t.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; t.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        t.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cl->ResourceBarrier(1, &t);
        execWait();
        up->Release();
        return r;
    };
    ID3D12Resource* bPos = local(pos.data(), pos.size() * 4);
    ID3D12Resource* bJnt = local(jnt.data(), jnt.size() * 4);
    ID3D12Resource* bWgt = local(wgt.data(), wgt.size() * 4);
    ID3D12Resource* bIdx = local(idx.data(), idx.size() * 4);
    ID3D12Resource* bMat = buffer((UINT64)jTotal * 64, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource* bMatGpu = buffer((UINT64)jTotal * 64, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12Resource* bOut = buffer((UINT64)vTotal * 12, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    float* matPtr = nullptr; bMat->Map(0, nullptr, (void**)&matPtr);

    // ---- compute skinning pipeline ----
    ID3DBlob* cs = nullptr; ID3DBlob* err = nullptr;
    if (FAILED(D3DCompile(kCS, std::strlen(kCS), "skin", nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &cs, &err))) {
        std::printf("compile: %s\n", err ? (const char*)err->GetBufferPointer() : "?"); return 1;
    }
    D3D12_ROOT_PARAMETER rp[6]{};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[0].Constants.Num32BitValues = 4;
    for (int k = 0; k < 4; ++k) { rp[1 + k].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; rp[1 + k].Descriptor.ShaderRegister = (UINT)k; }
    rp[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC rsd{}; rsd.NumParameters = 6; rsd.pParameters = rp;
    ID3DBlob* rsb = nullptr;
    CK(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsb, &err));
    ID3D12RootSignature* rs = nullptr;
    CK(dev->CreateRootSignature(0, rsb->GetBufferPointer(), rsb->GetBufferSize(), IID_PPV_ARGS(&rs)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature = rs; pd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
    ID3D12PipelineState* pso = nullptr;
    CK(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)));

    // ---- per-character BLAS inputs ----
    auto blasInputs = [&](const Char& ch, D3D12_RAYTRACING_GEOMETRY_DESC& gd, bool refit, bool update) {
        gd = {};
        gd.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        gd.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        gd.Triangles.VertexBuffer.StartAddress = bOut->GetGPUVirtualAddress() + (UINT64)ch.vBase * 12;
        gd.Triangles.VertexBuffer.StrideInBytes = 12;
        gd.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        gd.Triangles.VertexCount = ch.vCount;
        gd.Triangles.IndexBuffer = bIdx->GetGPUVirtualAddress() + (UINT64)ch.iBase * 4;
        gd.Triangles.IndexCount = ch.iCount;
        gd.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
        in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD |
                   (refit ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE) |
                   (update ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE);
        in.NumDescs = 1; in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY; in.pGeometryDescs = &gd;
        return in;
    };
    UINT64 blasMem = 0;
    for (Char& ch : chars) {
        D3D12_RAYTRACING_GEOMETRY_DESC gd;
        auto in = blasInputs(ch, gd, true, false);
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pi{};
        dev->GetRaytracingAccelerationStructurePrebuildInfo(&in, &pi);
        ch.blas = buffer(pi.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ch.scratch = buffer(std::max(pi.ScratchDataSizeInBytes, pi.UpdateScratchDataSizeInBytes), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        blasMem += pi.ResultDataMaxSizeInBytes;
    }
    // ---- TLAS ----
    ID3D12Resource* bInst = buffer(64 * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    D3D12_RAYTRACING_INSTANCE_DESC* inst = nullptr; bInst->Map(0, nullptr, (void**)&inst);
    for (int c = 0; c < 64; ++c) {
        D3D12_RAYTRACING_INSTANCE_DESC& d = inst[c];
        std::memset(&d, 0, sizeof d);
        d.Transform[0][0] = d.Transform[1][1] = d.Transform[2][2] = 1.0f;
        d.Transform[0][3] = (float)(c % 8) * 4.0f; d.Transform[2][3] = (float)(c / 8) * 4.0f;
        d.InstanceMask = 0xFF; d.AccelerationStructure = chars[(size_t)c].blas->GetGPUVirtualAddress();
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tin{};
    tin.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tin.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tin.NumDescs = 64; tin.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY; tin.InstanceDescs = bInst->GetGPUVirtualAddress();
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tpi{};
    dev->GetRaytracingAccelerationStructurePrebuildInfo(&tin, &tpi);
    ID3D12Resource* tlas = buffer(tpi.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ID3D12Resource* tscr = buffer(tpi.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    std::printf("BLAS memory (64, refittable, fast build): %.1f MB; skinned positions %.1f MB; TLAS %.2f MB\n", blasMem / 1048576.0, vTotal * 12 / 1048576.0, tpi.ResultDataMaxSizeInBytes / 1048576.0);

    // ---- timestamps ----
    ID3D12QueryHeap* qh = nullptr;
    D3D12_QUERY_HEAP_DESC qhd{}; qhd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qhd.Count = 4;
    CK(dev->CreateQueryHeap(&qhd, IID_PPV_ARGS(&qh)));
    ID3D12Resource* rb = buffer(64, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);

    std::mt19937 rng(7);
    std::uniform_real_distribution<float> U(-0.15f, 0.15f);
    auto fillMats = [&] {   // small rotations about random axes (row-major 4x4; HLSL float4x4 reads row-major from a structured buffer as column vectors * matrix order; cost-only)
        for (UINT j = 0; j < jTotal; ++j) {
            const float a = U(rng), b = U(rng);
            const float ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b);
            float m[16] = {ca, -sa * cb, sa * sb, U(rng), sa, ca * cb, -ca * sb, U(rng), 0, sb, cb, U(rng), 0, 0, 0, 1};
            std::memcpy(matPtr + (size_t)j * 16, m, sizeof m);
        }
    };
    auto runMode = [&](bool refit, const char* label) {
        double tSkin = 0, tBlas = 0, tTlas = 0; int n = 0;
        // initial builds (refit mode needs a built source)
        for (int f = -30 - 1; f < frames; ++f) {
            fillMats();
            cl->EndQuery(qh, D3D12_QUERY_TYPE_TIMESTAMP, 0);
            {
                D3D12_RESOURCE_BARRIER t{}; t.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; t.Transition.pResource = bMatGpu;
                t.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                t.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; t.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
                cl->ResourceBarrier(1, &t);
                cl->CopyBufferRegion(bMatGpu, 0, bMat, 0, (UINT64)jTotal * 64);
                std::swap(t.Transition.StateBefore, t.Transition.StateAfter);
                cl->ResourceBarrier(1, &t);
            }
            cl->SetComputeRootSignature(rs); cl->SetPipelineState(pso);
            cl->SetComputeRootShaderResourceView(1, bPos->GetGPUVirtualAddress());
            cl->SetComputeRootShaderResourceView(2, bJnt->GetGPUVirtualAddress());
            cl->SetComputeRootShaderResourceView(3, bWgt->GetGPUVirtualAddress());
            cl->SetComputeRootShaderResourceView(4, bMatGpu->GetGPUVirtualAddress());
            cl->SetComputeRootUnorderedAccessView(5, bOut->GetGPUVirtualAddress());
            for (const Char& ch : chars) {
                const UINT c4[4] = {ch.vCount, ch.vBase, ch.jBase, 0};
                cl->SetComputeRoot32BitConstants(0, 4, c4, 0);
                cl->Dispatch((ch.vCount + 63) / 64, 1, 1);
            }
            uavBarrier();
            cl->EndQuery(qh, D3D12_QUERY_TYPE_TIMESTAMP, 1);
            for (const Char& ch : chars) {
                D3D12_RAYTRACING_GEOMETRY_DESC gd;
                const bool update = refit && f > -30 - 1;   // the first frame builds; refit mode updates after that
                D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd{};
                bd.Inputs = blasInputs(ch, gd, true, update);
                if (!refit) bd.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
                bd.DestAccelerationStructureData = ch.blas->GetGPUVirtualAddress();
                if (update) bd.SourceAccelerationStructureData = ch.blas->GetGPUVirtualAddress();
                bd.ScratchAccelerationStructureData = ch.scratch->GetGPUVirtualAddress();
                cl->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);
            }
            uavBarrier();
            cl->EndQuery(qh, D3D12_QUERY_TYPE_TIMESTAMP, 2);
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tbd{};
            tbd.Inputs = tin; tbd.DestAccelerationStructureData = tlas->GetGPUVirtualAddress(); tbd.ScratchAccelerationStructureData = tscr->GetGPUVirtualAddress();
            cl->BuildRaytracingAccelerationStructure(&tbd, 0, nullptr);
            uavBarrier();
            cl->EndQuery(qh, D3D12_QUERY_TYPE_TIMESTAMP, 3);
            cl->ResolveQueryData(qh, D3D12_QUERY_TYPE_TIMESTAMP, 0, 4, rb, 0);
            execWait();
            if (f >= 0) {
                UINT64* ts = nullptr; D3D12_RANGE r{0, 32}; rb->Map(0, &r, (void**)&ts);
                tSkin += (double)(ts[1] - ts[0]); tBlas += (double)(ts[2] - ts[1]); tTlas += (double)(ts[3] - ts[2]); ++n;
                rb->Unmap(0, nullptr);
            }
        }
        const double k = 1000.0 / (double)tsFreq / n;
        std::printf("%-34s per frame (%d frames): skinning %.3f ms, 64 BLAS %.3f ms, TLAS %.3f ms, total %.3f ms\n", label, n,
                    tSkin * k, tBlas * k, tTlas * k, (tSkin + tBlas + tTlas) * k);
    };
    runMode(true, "refit (ALLOW_UPDATE, fast build)");
    runMode(false, "rebuild (PREFER_FAST_BUILD)");
    return 0;
}
