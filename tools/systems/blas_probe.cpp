// M7 stage-0 probe (Systems, prototype; not part of the build): BLAS sizes and build times for a map's static world.
// Loads world.glb with the game's loader (all nodes baked to one world-space mesh + material slices), uploads it to D3D12
// and builds ray-tracing acceleration structures two ways: (a) one BLAS over the whole world, (b) one BLAS per material
// slice (the opaque / alpha-tested split will group slices). Flags PREFER_FAST_TRACE | ALLOW_COMPACTION. Reports the
// prebuild sizes, the compacted sizes (postbuild info) and GPU build times (timestamps), and the vertex / index memory.
// Usage: blas_probe <world.glb> [<more.glb> ...]
#include "assets/Gltf.h"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#define CK(x) do { HRESULT hr_ = (x); if (FAILED(hr_)) { std::printf("FAILED %s: 0x%08lx (line %d)\n", #x, (unsigned long)hr_, __LINE__); return false; } } while (0)

namespace {
ID3D12Device5* dev = nullptr;
ID3D12CommandQueue* q = nullptr;
ID3D12CommandAllocator* alloc = nullptr;
ID3D12GraphicsCommandList4* cl = nullptr;
ID3D12Fence* fence = nullptr;
UINT64 fenceVal = 0;
HANDLE ev = nullptr;
UINT64 tsFreq = 0;

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
bool init() {
    CK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev)));
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
    dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof o5);
    std::printf("raytracing tier %d (%s)\n", (int)o5.RaytracingTier, o5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1 ? "DXR 1.1" : o5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0 ? "DXR 1.0" : "none");
    if (o5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0) return false;
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    CK(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q)));
    CK(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)));
    CK(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&cl)));
    CK(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    q->GetTimestampFrequency(&tsFreq);
    return true;
}

struct Geo { UINT64 vOff, iOff; UINT vCount, iCount; };

// Builds one BLAS per entry of `groups` (each a list of index ranges into the shared buffers); returns totals.
bool buildSet(const char* label, ID3D12Resource* vb, ID3D12Resource* ib, UINT vCount, const std::vector<std::vector<Geo>>& groups) {
    UINT64 resTot = 0, scrMax = 0, scrTot = 0;
    std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS> ins(groups.size());
    std::vector<std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>> gds(groups.size());
    std::vector<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO> pre(groups.size());
    for (size_t g = 0; g < groups.size(); ++g) {
        for (const Geo& s : groups[g]) {
            D3D12_RAYTRACING_GEOMETRY_DESC d{};
            d.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            d.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            d.Triangles.VertexBuffer.StartAddress = vb->GetGPUVirtualAddress();
            d.Triangles.VertexBuffer.StrideInBytes = 12;
            d.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            d.Triangles.VertexCount = vCount;
            d.Triangles.IndexBuffer = ib->GetGPUVirtualAddress() + s.iOff * 4;
            d.Triangles.IndexCount = s.iCount;
            d.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
            gds[g].push_back(d);
        }
        auto& in = ins[g];
        in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
        in.NumDescs = (UINT)gds[g].size();
        in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        in.pGeometryDescs = gds[g].data();
        dev->GetRaytracingAccelerationStructurePrebuildInfo(&in, &pre[g]);
        resTot += (pre[g].ResultDataMaxSizeInBytes + 255) & ~255ull;
        scrTot += pre[g].ScratchDataSizeInBytes;
        if (pre[g].ScratchDataSizeInBytes > scrMax) scrMax = pre[g].ScratchDataSizeInBytes;
    }
    // build all (scratch: one buffer per BLAS, built in one command list), with timestamps around the whole set
    std::vector<ID3D12Resource*> res(groups.size()), scr(groups.size());
    for (size_t g = 0; g < groups.size(); ++g) {
        res[g] = buffer(pre[g].ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        scr[g] = buffer(pre[g].ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (!res[g] || !scr[g]) { std::printf("%s: allocation failed at BLAS %zu\n", label, g); return false; }
    }
    ID3D12QueryHeap* qh = nullptr;
    D3D12_QUERY_HEAP_DESC qhd{}; qhd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qhd.Count = 2;
    CK(dev->CreateQueryHeap(&qhd, IID_PPV_ARGS(&qh)));
    ID3D12Resource* rb = buffer(4096, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource* post = buffer(8 * groups.size(), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ID3D12Resource* postRb = buffer(8 * groups.size(), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    cl->EndQuery(qh, D3D12_QUERY_TYPE_TIMESTAMP, 0);
    for (size_t g = 0; g < groups.size(); ++g) {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd{};
        bd.Inputs = ins[g];
        bd.DestAccelerationStructureData = res[g]->GetGPUVirtualAddress();
        bd.ScratchAccelerationStructureData = scr[g]->GetGPUVirtualAddress();
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC pd{};
        pd.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
        pd.DestBuffer = post->GetGPUVirtualAddress() + 8 * g;
        cl->BuildRaytracingAccelerationStructure(&bd, 1, &pd);
    }
    D3D12_RESOURCE_BARRIER uav{}; uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    cl->ResourceBarrier(1, &uav);
    cl->EndQuery(qh, D3D12_QUERY_TYPE_TIMESTAMP, 1);
    cl->ResolveQueryData(qh, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, rb, 0);
    D3D12_RESOURCE_BARRIER tb{}; tb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; tb.Transition.pResource = post;
    tb.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; tb.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    tb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cl->ResourceBarrier(1, &tb);
    cl->CopyResource(postRb, post);
    execWait();
    UINT64* ts = nullptr; D3D12_RANGE r{0, 16};
    rb->Map(0, &r, (void**)&ts);
    const double ms = (double)(ts[1] - ts[0]) * 1000.0 / (double)tsFreq;
    rb->Unmap(0, nullptr);
    UINT64* cs = nullptr; D3D12_RANGE r2{0, 8 * groups.size()};
    postRb->Map(0, &r2, (void**)&cs);
    UINT64 compTot = 0; for (size_t g = 0; g < groups.size(); ++g) compTot += cs[g];
    postRb->Unmap(0, nullptr);
    std::printf("  %-26s %5zu BLAS: result %7.1f MB, compacted %7.1f MB, scratch %7.1f MB total / %6.1f MB max, GPU build %7.2f ms\n",
                label, groups.size(), resTot / 1048576.0, compTot / 1048576.0, scrTot / 1048576.0, scrMax / 1048576.0, ms);
    for (auto* x : res) x->Release();
    for (auto* x : scr) x->Release();
    qh->Release(); rb->Release(); post->Release(); postRb->Release();
    return true;
}

bool probe(const char* path) {
    render::MeshData m;
    const auto t0 = std::chrono::steady_clock::now();
    if (!assets::loadGlb(path, m) || m.empty()) { std::printf("%s: load failed\n", path); return false; }
    const double loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const UINT vCount = (UINT)m.vertexCount(), iCount = (UINT)m.indices.size();
    std::printf("%s\n  %u vertices, %u triangles, %zu material slices (load %.0f ms); positions %.1f MB + indices %.1f MB\n", path,
                vCount, iCount / 3, m.subs.size(), loadMs, vCount * 12 / 1048576.0, iCount * 4 / 1048576.0);
    ID3D12Resource* vb = buffer((UINT64)vCount * 12, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource* ib = buffer((UINT64)iCount * 4, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (!vb || !ib) { std::printf("  upload buffers failed\n"); return false; }
    void* p = nullptr;
    vb->Map(0, nullptr, &p); std::memcpy(p, m.positions.data(), (size_t)vCount * 12); vb->Unmap(0, nullptr);
    ib->Map(0, nullptr, &p); std::memcpy(p, m.indices.data(), (size_t)iCount * 4); ib->Unmap(0, nullptr);
    std::vector<std::vector<Geo>> one(1), perSlice;
    one[0].push_back({0, 0, vCount, iCount});
    for (const auto& s : m.subs) {
        if (s.indexCount < 3) continue;
        perSlice.push_back({Geo{0, (UINT64)s.indexOffset, vCount, (UINT)s.indexCount}});
    }
    buildSet("(a) one BLAS", vb, ib, vCount, one);
    if (!perSlice.empty()) buildSet("(b) one BLAS per slice", vb, ib, vCount, perSlice);
    vb->Release(); ib->Release();
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: blas_probe <world.glb> ...\n"); return 2; }
    if (!init()) return 1;
    DXGI_ADAPTER_DESC ad{};
    LUID luid = dev->GetAdapterLuid();
    IDXGIFactory4* f = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) { IDXGIAdapter* a = nullptr; if (SUCCEEDED(f->EnumAdapterByLuid(luid, IID_PPV_ARGS(&a)))) { a->GetDesc(&ad); a->Release(); } f->Release(); }
    std::printf("adapter: %ls\n", ad.Description);
    for (int i = 1; i < argc; ++i) probe(argv[i]);
    return 0;
}
