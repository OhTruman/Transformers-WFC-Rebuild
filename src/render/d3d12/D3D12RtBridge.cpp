// Clean-room reconstruction — M7 stage 0 groundwork: the mid-frame GL -> D3D12 -> GL round trip (PC ADAPTATION; OFF by
// default; render/d3d12/D3D12RtBridge.h). Its own D3D12 device / queue (Agility selected at start-up when shipped; GL's
// adapter when the integration tree has D3D12Adapter.h), two textures shared with GL (depth in, mask out: R32F, render
// size) and one shared fence: GL signals N after writing the depth, D3D12 waits N, runs its job, signals N + 1, GL waits
// N + 1 before the shading that will sample the mask. The job: a stand-in copy (WFC_RT_ROUNDTRIP), or, with a static
// world built (WFC_RT_DEPTHCHECK), the primary-ray depth check (shaders/rt_depthcheck.hlsl): its per-pixel class counts
// come back through a readback buffer one frame later and are logged per 300 frames. Main-thread phases likewise.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <GL/gl.h>

#include "render/d3d12/D3D12RtBridge.h"
#include "core/Log.h"
#if __has_include("render/d3d12/D3D12Adapter.h")
#include "render/d3d12/D3D12Adapter.h"
#define WFC_RT_GL_ADAPTER 1
#endif
#if defined(WFC_RT_SHADERS)
#include "rt_depthcheck.h"
#include "rt_shadow_ao.h"
#endif
#if defined(WFC_RT_DENOISER)
#include "render/d3d12/D3D12RtDenoise.h"
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>

namespace render {
void d3d12SelectAgility();   // D3D12Agility.cpp
namespace {
typedef uint64_t GLuint64_;
typedef void(APIENTRY* PFN_CreateMemoryObjectsEXT)(GLsizei, GLuint*);
typedef void(APIENTRY* PFN_DeleteMemoryObjectsEXT)(GLsizei, const GLuint*);
typedef void(APIENTRY* PFN_ImportMemoryWin32HandleEXT)(GLuint, GLuint64_, GLenum, void*);
typedef void(APIENTRY* PFN_MemoryObjectParameterivEXT)(GLuint, GLenum, const GLint*);
typedef void(APIENTRY* PFN_TexStorageMem2DEXT)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, GLuint64_);
typedef void(APIENTRY* PFN_GenSemaphoresEXT)(GLsizei, GLuint*);
typedef void(APIENTRY* PFN_ImportSemaphoreWin32HandleEXT)(GLuint, GLenum, void*);
typedef void(APIENTRY* PFN_SemaphoreParameterui64vEXT)(GLuint, GLenum, const GLuint64_*);
typedef void(APIENTRY* PFN_WaitSemaphoreEXT)(GLuint, GLuint, const GLuint*, GLuint, const GLuint*, const GLenum*);
typedef void(APIENTRY* PFN_SignalSemaphoreEXT)(GLuint, GLuint, const GLuint*, GLuint, const GLuint*, const GLenum*);
constexpr GLenum kHandleD3D12Resource = 0x958A, kHandleD3D12Fence = 0x9594, kDedicated = 0x9581, kFenceValue = 0x9595;
constexpr GLenum kLayoutShaderRead = 0x958F, kLayoutGeneral = 0x958D;
constexpr int kStatN = 23;   // rt_depthcheck.hlsl's classes (7) + its ratio histogram (16)
template <class T> void rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

struct Shared { ID3D12Resource* res = nullptr; HANDLE handle = nullptr; GLuint mem = 0, tex = 0; int w = 0, h = 0; };

ID3D12Resource* makeBuffer(ID3D12Device* dev, UINT64 bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                           D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = heap;
    D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = std::max<UINT64>(bytes, 256);
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = flags;
    ID3D12Resource* r = nullptr;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr, __uuidof(ID3D12Resource), (void**)&r))) return nullptr;
    return r;
}
ID3D12Resource* uploadBuffer(ID3D12Device* dev, const void* data, size_t bytes) {
    ID3D12Resource* r = makeBuffer(dev, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    void* p = nullptr;
    if (r && SUCCEEDED(r->Map(0, nullptr, &p))) { std::memcpy(p, data, bytes); r->Unmap(0, nullptr); }
    return r;
}

class Bridge {
public:
    bool init() {
        auto g = [](const char* n) { return (void*)wglGetProcAddress(n); };
        createMem_ = (PFN_CreateMemoryObjectsEXT)g("glCreateMemoryObjectsEXT"); deleteMem_ = (PFN_DeleteMemoryObjectsEXT)g("glDeleteMemoryObjectsEXT");
        importMem_ = (PFN_ImportMemoryWin32HandleEXT)g("glImportMemoryWin32HandleEXT"); memParam_ = (PFN_MemoryObjectParameterivEXT)g("glMemoryObjectParameterivEXT");
        texStorageMem_ = (PFN_TexStorageMem2DEXT)g("glTexStorageMem2DEXT"); genSem_ = (PFN_GenSemaphoresEXT)g("glGenSemaphoresEXT");
        importSem_ = (PFN_ImportSemaphoreWin32HandleEXT)g("glImportSemaphoreWin32HandleEXT");
        semParam_ = (PFN_SemaphoreParameterui64vEXT)g("glSemaphoreParameterui64vEXT"); waitSem_ = (PFN_WaitSemaphoreEXT)g("glWaitSemaphoreEXT");
        signalSem_ = (PFN_SignalSemaphoreEXT)g("glSignalSemaphoreEXT");
        if (!createMem_ || !importMem_ || !texStorageMem_ || !genSem_ || !importSem_ || !semParam_ || !waitSem_ || !signalSem_) {
            LOG_WARN("RT bridge: GL external-object entry points missing"); return false;
        }
        d3d12SelectAgility();
        HMODULE d3d = LoadLibraryW(L"d3d12.dll");
        auto create = d3d ? (PFN_D3D12_CREATE_DEVICE)(void*)GetProcAddress(d3d, "D3D12CreateDevice") : nullptr;
        serializeRs_ = d3d ? (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)(void*)GetProcAddress(d3d, "D3D12SerializeRootSignature") : nullptr;
#if defined(WFC_RT_GL_ADAPTER)
        IDXGIAdapter1* adapter = d3d12AdapterForGL();   // GL's adapter (hybrid laptops); nullptr = the default
#else
        IUnknown* adapter = nullptr;
#endif
        const bool made = create && SUCCEEDED(create(adapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&dev_));
        if (adapter) adapter->Release();
        if (!made) { LOG_WARN("RT bridge: no D3D12 device"); return false; }
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
        dev_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof o5);
        rtTier_ = (int)o5.RaytracingTier;
        {   // the device's adapter (VRAM: QueryVideoMemoryInfo, the process's local usage)
            HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
            typedef HRESULT(WINAPI * PFN_CF1)(REFIID, void**);
            auto cf1 = dxgi ? (PFN_CF1)(void*)GetProcAddress(dxgi, "CreateDXGIFactory1") : nullptr;
            IDXGIFactory4* f = nullptr;
            if (cf1 && SUCCEEDED(cf1(__uuidof(IDXGIFactory4), (void**)&f)) && f) {
                f->EnumAdapterByLuid(dev_->GetAdapterLuid(), __uuidof(IDXGIAdapter3), (void**)&adapter_);
                f->Release();
            }
        }
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(dev_->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&queue_))) return false;
        queue_->GetTimestampFrequency(&tsFreq_);
        for (auto& a : alloc_)
            if (FAILED(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&a))) return false;
        if (FAILED(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc_[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&list_))) return false;
        list_->Close();
        if (FAILED(dev_->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), (void**)&fence_))) return false;
        if (FAILED(dev_->CreateSharedHandle(fence_, nullptr, GENERIC_ALL, nullptr, &fenceHandle_))) return false;
        genSem_(1, &sem_);
        importSem_(sem_, kHandleD3D12Fence, fenceHandle_);
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        LOG_INFO("RT bridge: D3D12 device ready (raytracing tier %d.%d)", rtTier_ / 10, rtTier_ % 10);
        return true;
    }
    bool prepare(int w, int h, RtTargets* t) {
        if (w != depth_.w || h != depth_.h) {
            waitIdle();
            free(depth_); free(mask_); free(ao_); free(dnOut_);
            rel(hit_); rel(nrm_); rel(vel_);
            hit_ = localTex((w + 7) / 8, (h + 3) / 4, DXGI_FORMAT_R32_UINT);
            nrm_ = localTex(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT);
            vel_ = localTex(w, h, DXGI_FORMAT_R16G16_FLOAT);
            if (!make(depth_, w, h) || !make(mask_, w, h) || !make(ao_, w, h, DXGI_FORMAT_R16G16_FLOAT, 0x822F /*GL_RG16F*/) ||
                !make(dnOut_, w, h, DXGI_FORMAT_R8_UNORM, 0x8229 /*GL_R8*/) || !hit_ || !nrm_ || !vel_) {
                LOG_WARN("RT bridge: shared targets failed"); depth_.w = 0; return false;
            }
            if (heap_) writeDescriptors();
#if defined(WFC_RT_DENOISER)
            if (dn_.ready() || dnInit_) {
                RtDenoiseInputs in; in.depth = depth_.res; in.hitMask = hit_; in.normal = nrm_; in.velocity = vel_; in.output = dnOut_.res;
                dn_.resize(w, h, in);
            }
#endif
        }
        t->depth = depth_.tex; t->mask = mask_.tex; t->shadowAo = ao_.tex; t->shadowDn = dnOut_.tex;
        return true;
    }

    // ---- the static world: per MDI set (world, level BSP) a one-sided and a two-sided BLAS as identity instances with
    // the set's instance mask; the TLAS is rebuilt over all instances whenever a set is added (map load) ----
    bool addStaticWorld(const RtStaticWorld& W, unsigned mask) {
        if (tlas_) { waitIdle(); rel(tlas_); }
#if !defined(WFC_RT_SHADERS)
        LOG_WARN("RT bridge: built without the RT shaders (tools/render/fetch_dxc.py before configuring): no depth check");
        return false;
#else
        if (rtTier_ < 11) { LOG_WARN("RT bridge: raytracing tier %d.%d < 1.1 (inline RayQuery): no depth check", rtTier_ / 10, rtTier_ % 10); return false; }
        if (!dev5_ && FAILED(dev_->QueryInterface(__uuidof(ID3D12Device5), (void**)&dev5_))) return false;
        if (!pipeline()) return false;
        using clk = std::chrono::steady_clock;
        const clk::time_point t0 = clk::now();
        const UINT nv = (UINT)(W.pos.size() / 3);
        ID3D12Resource* vb = uploadBuffer(dev_, W.pos.data(), W.pos.size() * sizeof(float));
        ID3D12Resource* ib[2] = {W.oneSided.empty() ? nullptr : uploadBuffer(dev_, W.oneSided.data(), W.oneSided.size() * 4),
                                 W.twoSided.empty() ? nullptr : uploadBuffer(dev_, W.twoSided.data(), W.twoSided.size() * 4)};
        const UINT ni[2] = {(UINT)W.oneSided.size(), (UINT)W.twoSided.size()};
        std::vector<ID3D12Resource*> scratch;
        ID3D12CommandAllocator* al = nullptr; ID3D12GraphicsCommandList4* cl = nullptr;
        bool ok = vb && SUCCEEDED(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&al)) &&
                  SUCCEEDED(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, al, nullptr, __uuidof(ID3D12GraphicsCommandList4), (void**)&cl));
        UINT64 blasBytes = 0;
        for (int k = 0; k < 2 && ok; ++k) {
            if (!ib[k]) continue;
            D3D12_RAYTRACING_GEOMETRY_DESC gd{};
            gd.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES; gd.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            gd.Triangles.VertexBuffer.StartAddress = vb->GetGPUVirtualAddress(); gd.Triangles.VertexBuffer.StrideInBytes = 12;
            gd.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT; gd.Triangles.VertexCount = nv;
            gd.Triangles.IndexBuffer = ib[k]->GetGPUVirtualAddress(); gd.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
            gd.Triangles.IndexCount = ni[k];
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd{};
            bd.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            bd.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
            bd.Inputs.NumDescs = 1; bd.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY; bd.Inputs.pGeometryDescs = &gd;
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pi{};
            dev5_->GetRaytracingAccelerationStructurePrebuildInfo(&bd.Inputs, &pi);
            ID3D12Resource* bl = makeBuffer(dev_, pi.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                                  D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            ID3D12Resource* sc = makeBuffer(dev_, pi.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            if (!bl || !sc) { ok = false; rel(sc); rel(bl); break; }
            blas_.push_back(bl);
            scratch.push_back(sc);
            bd.DestAccelerationStructureData = bl->GetGPUVirtualAddress(); bd.ScratchAccelerationStructureData = sc->GetGPUVirtualAddress();
            cl->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);
            blasBytes += pi.ResultDataMaxSizeInBytes;
            D3D12_RAYTRACING_INSTANCE_DESC in{};
            in.Transform[0][0] = in.Transform[1][1] = in.Transform[2][2] = 1.0f;
            in.InstanceMask = mask & 0xFF;
            // GL's counter-clockwise front faces are DXR's default (clockwise) front with these world-space positions:
            // measured, FRONT_COUNTERCLOCKWISE culled the visible faces (depth check 10 % -> 99.98 %); two-sided: no culling
            in.Flags = k == 0 ? D3D12_RAYTRACING_INSTANCE_FLAG_NONE : D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
            in.AccelerationStructure = bl->GetGPUVirtualAddress();
            instances_.push_back(in);
        }
        ID3D12Resource* instBuf = nullptr;
        const UINT nInst = (UINT)instances_.size();
        if (ok && nInst) {
            D3D12_RESOURCE_BARRIER ub{}; ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;   // BLAS builds before the TLAS reads them
            cl->ResourceBarrier(1, &ub);
            instBuf = uploadBuffer(dev_, instances_.data(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * nInst);
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd{};
            bd.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            bd.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
            bd.Inputs.NumDescs = nInst; bd.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            bd.Inputs.InstanceDescs = instBuf ? instBuf->GetGPUVirtualAddress() : 0;
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pi{};
            dev5_->GetRaytracingAccelerationStructurePrebuildInfo(&bd.Inputs, &pi);
            tlas_ = makeBuffer(dev_, pi.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                               D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            ID3D12Resource* sc = makeBuffer(dev_, pi.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            ok = instBuf && tlas_ && sc;
            if (sc) scratch.push_back(sc);
            if (ok) {
                bd.DestAccelerationStructureData = tlas_->GetGPUVirtualAddress(); bd.ScratchAccelerationStructureData = sc->GetGPUVirtualAddress();
                cl->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);
            }
        }
        if (cl) {
            cl->Close();
            if (ok) {
                ID3D12CommandList* lists[] = {cl};
                queue_->ExecuteCommandLists(1, lists);
            }
            waitIdle();
        }
        const double ms = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
        rel(cl); rel(al); rel(vb); rel(ib[0]); rel(ib[1]); rel(instBuf);
        for (auto* s : scratch) s->Release();
        if (!ok || !nInst) { clearStaticWorld(); LOG_WARN("RT bridge: static set BLAS / TLAS build failed (all sets dropped)"); return false; }
        totalBlas_ += blasBytes;
        LOG_INFO("RT bridge: static set (mask 0x%02x): %u vertices, %zu one-sided + %zu two-sided triangles, BLAS %.1f MB (all sets %.1f MB, "
                 "%u instances), upload + build %.0f ms", mask, nv, W.oneSided.size() / 3, W.twoSided.size() / 3,
                 (double)blasBytes / 1048576.0, (double)totalBlas_ / 1048576.0, nInst, ms);
        statFrames_ = 0; std::fill(statSum_, statSum_ + kStatN, 0ull);
        return true;
#endif
    }
    void clearStaticWorld() {
        if (tlas_ || !blas_.empty()) waitIdle();
        rel(tlas_);
        for (auto* b : blas_) b->Release();
        blas_.clear(); instances_.clear(); totalBlas_ = 0;
        for (bool& v : rbValid_) v = false;
    }

    bool roundTrip(const RtFrame* fr) {
        using clk = std::chrono::steady_clock;
        const clk::time_point t0 = clk::now();
        const UINT64 n = ++value_;
        GLuint64_ v = n; semParam_(sem_, kFenceValue, &v);
        GLuint tx[2] = {depth_.tex, mask_.tex}; GLenum ly[2] = {kLayoutShaderRead, kLayoutGeneral};
        signalSem_(sem_, (GLuint)sigBufs_.size(), sigBufs_.empty() ? nullptr : sigBufs_.data(), 2, tx, ly);
        glFlush();
        const clk::time_point t1 = clk::now();
        const int a = (int)(frame_++ & 1);
        if (allocDone_[a] && fence_->GetCompletedValue() < allocDone_[a]) { fence_->SetEventOnCompletion(allocDone_[a], event_); WaitForSingleObject(event_, 1000); }
        if (rbValid_[a]) readStats(a);
        alloc_[a]->Reset(); list_->Reset(alloc_[a], nullptr);
        if (fr && tlas_ && pso_ && fr->mode == 2) shadowAo(*fr, a);
        else if (fr && tlas_ && pso_) depthCheck(*fr, a);
        else copyJob();
        list_->Close();
        queue_->Wait(fence_, n);
        ID3D12CommandList* lists[] = {list_};
        queue_->ExecuteCommandLists(1, lists);
        const UINT64 done = ++value_;
        queue_->Signal(fence_, done);
        allocDone_[a] = done;
        const clk::time_point t2 = clk::now();
        GLuint64_ w = done; semParam_(sem_, kFenceValue, &w);
        GLuint wt[3] = {mask_.tex, ao_.tex, dnOut_.tex}; GLenum wl[3] = {kLayoutShaderRead, kLayoutShaderRead, kLayoutShaderRead};
        waitSem_(sem_, (GLuint)sigBufs_.size(), sigBufs_.empty() ? nullptr : sigBufs_.data(), 3, wt, wl);
        const clk::time_point t3 = clk::now();
        auto ms = [](clk::time_point a_, clk::time_point b_) { return std::chrono::duration<double, std::milli>(b_ - a_).count(); };
        const double ph[3] = {ms(t0, t1), ms(t1, t2), ms(t2, t3)};
        for (int k = 0; k < 3; ++k) { sum_[k] += ph[k]; max_[k] = std::max(max_[k], ph[k]); }
        if (++n_ == 300) {
            LOG_INFO("RT bridge round trip (main thread, avg / max ms over 300): GL signal + flush %.3f / %.2f, D3D12 record + submit %.3f / %.2f, GL wait %.3f / %.2f",
                     sum_[0] / 300, max_[0], sum_[1] / 300, max_[1], sum_[2] / 300, max_[2]);
            for (int k = 0; k < 3; ++k) sum_[k] = max_[k] = 0;
            n_ = 0;
        }
        return true;
    }

private:
    void copyJob() {
        D3D12_RESOURCE_BARRIER b[2]{};
        for (auto& x : b) { x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; }
        b[0].Transition.pResource = depth_.res; b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b[1].Transition.pResource = mask_.res; b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        list_->ResourceBarrier(2, b);
        list_->CopyResource(mask_.res, depth_.res);          // the stand-in job (RT shadows / AO will write the mask)
        for (auto& x : b) std::swap(x.Transition.StateBefore, x.Transition.StateAfter);
        list_->ResourceBarrier(2, b);
    }
    // root: [0] 48 constants (b0), [1] TLAS root SRV (t0), [2] table {SRV t1 depth, UAV u0 mask}, [3] stats root UAV (u1)
    bool pipeline() {
#if defined(WFC_RT_SHADERS)
        if (pso_) return true;
        if (!serializeRs_) return false;
        D3D12_DESCRIPTOR_RANGE rg[2]{};
        rg[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; rg[0].NumDescriptors = 1; rg[0].BaseShaderRegister = 1;
        rg[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; rg[1].NumDescriptors = 4; rg[1].BaseShaderRegister = 0;
        rg[1].OffsetInDescriptorsFromTableStart = 1;
        D3D12_ROOT_PARAMETER rp[4]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[0].Constants.Num32BitValues = 48;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; rp[1].Descriptor.ShaderRegister = 0;
        rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[2].DescriptorTable.NumDescriptorRanges = 2; rp[2].DescriptorTable.pDescriptorRanges = rg;
        rp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; rp[3].Descriptor.ShaderRegister = 4;
        D3D12_ROOT_SIGNATURE_DESC rd{}; rd.NumParameters = 4; rd.pParameters = rp;
        ID3DBlob* blob = nullptr; ID3DBlob* err = nullptr;
        if (FAILED(serializeRs_(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
            LOG_WARN("RT bridge: root signature: %s", err ? (const char*)err->GetBufferPointer() : "?"); rel(err); return false;
        }
        const HRESULT hr = dev_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&rs_);
        rel(blob); rel(err);
        if (FAILED(hr)) return false;
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature = rs_;
        pd.CS.pShaderBytecode = g_rt_depthcheck; pd.CS.BytecodeLength = sizeof g_rt_depthcheck;
        if (FAILED(dev_->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&pso_))) { LOG_WARN("RT bridge: depth-check PSO failed"); return false; }
        pd.CS.pShaderBytecode = g_rt_shadow_ao; pd.CS.BytecodeLength = sizeof g_rt_shadow_ao;
        if (FAILED(dev_->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&psoSao_))) { LOG_WARN("RT bridge: shadow / AO PSO failed"); return false; }
        D3D12_QUERY_HEAP_DESC qh{}; qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qh.Count = 8;
        if (FAILED(dev_->CreateQueryHeap(&qh, __uuidof(ID3D12QueryHeap), (void**)&tsHeap_))) return false;
        for (auto& r : tsReadback_) r = makeBuffer(dev_, 32, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
#if defined(WFC_RT_DENOISER)
        dnInit_ = dn_.init(dev_);
        if (!dnInit_) LOG_WARN("RT bridge: no shadow denoiser (raw RT shadow)");
        else if (depth_.res) {                           // (the targets came first: size the denoiser now)
            RtDenoiseInputs in; in.depth = depth_.res; in.hitMask = hit_; in.normal = nrm_; in.velocity = vel_; in.output = dnOut_.res;
            dn_.resize(depth_.w, depth_.h, in);
        }
#endif
        D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 10;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(dev_->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&heap_))) return false;
        stats_ = makeBuffer(dev_, kStatN * 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        const uint32_t zeros[kStatN] = {};
        zeros_ = uploadBuffer(dev_, zeros, sizeof zeros);
        for (auto& r : readback_) r = makeBuffer(dev_, kStatN * 4, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!stats_ || !zeros_ || !readback_[0] || !readback_[1]) return false;
        if (depth_.res) writeDescriptors();
        return true;
#else
        return false;
#endif
    }
    void writeDescriptors() {
        const UINT inc = dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE h = heap_->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Format = DXGI_FORMAT_R32_FLOAT; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sd.Texture2D.MipLevels = 1;
        dev_->CreateShaderResourceView(depth_.res, &sd, h);
        h.ptr += inc;
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = DXGI_FORMAT_R32_FLOAT; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        dev_->CreateUnorderedAccessView(mask_.res, nullptr, &ud, h);
        auto uav = [&](ID3D12Resource* r, DXGI_FORMAT f) { h.ptr += inc; D3D12_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format = f;
                                                            u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; dev_->CreateUnorderedAccessView(r, nullptr, &u, h); };
        uav(hit_, DXGI_FORMAT_R32_UINT); uav(nrm_, DXGI_FORMAT_R16G16B16A16_FLOAT); uav(vel_, DXGI_FORMAT_R16G16_FLOAT);   // (A: unused)
        h.ptr += inc;                                    // table B (shadow / AO): depth SRV; u0 raw mask, u1 hit mask, u2 normal, u3 velocity
        dev_->CreateShaderResourceView(depth_.res, &sd, h);
        uav(ao_.res, DXGI_FORMAT_R16G16_FLOAT); uav(hit_, DXGI_FORMAT_R32_UINT); uav(nrm_, DXGI_FORMAT_R16G16B16A16_FLOAT); uav(vel_, DXGI_FORMAT_R16G16_FLOAT);
    }
    void depthCheck(const RtFrame& fr, int slot) {
        static const bool flip = std::getenv("WFC_RT_FLIPWINDING") != nullptr;   // (diagnostics: the other winding)
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = stats_;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        list_->ResourceBarrier(1, &b);
        list_->CopyBufferRegion(stats_, 0, zeros_, 0, kStatN * 4);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        list_->ResourceBarrier(1, &b);
        float c[48] = {};
        std::memcpy(c, fr.invViewProj, 64); std::memcpy(c + 16, fr.viewRow2, 16);
        c[20] = (float)depth_.w; c[21] = (float)depth_.h; c[22] = flip ? 1.0f : 0.0f; c[23] = (float)(fr.instanceMask & 0xFF);
        list_->SetComputeRootSignature(rs_);
        list_->SetPipelineState(pso_);
        list_->SetDescriptorHeaps(1, &heap_);
        list_->SetComputeRoot32BitConstants(0, 48, c, 0);
        list_->SetComputeRootShaderResourceView(1, tlas_->GetGPUVirtualAddress());
        list_->SetComputeRootDescriptorTable(2, heap_->GetGPUDescriptorHandleForHeapStart());
        list_->SetComputeRootUnorderedAccessView(3, stats_->GetGPUVirtualAddress());
        list_->Dispatch((UINT)(depth_.w + 7) / 8, (UINT)(depth_.h + 7) / 8, 1);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list_->ResourceBarrier(1, &b);
        list_->CopyBufferRegion(readback_[slot], 0, stats_, 0, kStatN * 4);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        list_->ResourceBarrier(1, &b);
        rbValid_[slot] = true; rbMode_[slot] = 1;
    }
    // stage 1: sun shadow + AO into the RG16F target, timestamps around the dispatch, rays counted by the shader
    void shadowAo(const RtFrame& fr, int slot) {
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = stats_;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        list_->ResourceBarrier(1, &b);
        list_->CopyBufferRegion(stats_, 0, zeros_, 0, kStatN * 4);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        list_->ResourceBarrier(1, &b);
        float c[48] = {};
        std::memcpy(c, fr.invViewProj, 64); std::memcpy(c + 16, fr.toSun, 16);
        c[20] = (float)depth_.w; c[21] = (float)depth_.h; c[22] = (float)fr.frameIndex; c[23] = (float)fr.aoRays;
        c[24] = fr.aoRadius; c[25] = fr.bias; c[26] = (float)(fr.instanceMask & 0xFF);
        std::memcpy(c + 28, fr.ffxReproj, 64);
        list_->SetComputeRootSignature(rs_);
        list_->SetPipelineState(psoSao_);
        list_->SetDescriptorHeaps(1, &heap_);
        list_->SetComputeRoot32BitConstants(0, 48, c, 0);
        list_->SetComputeRootShaderResourceView(1, tlas_->GetGPUVirtualAddress());
        D3D12_GPU_DESCRIPTOR_HANDLE t2 = heap_->GetGPUDescriptorHandleForHeapStart();
        t2.ptr += 5ull * dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        list_->SetComputeRootDescriptorTable(2, t2);
        list_->SetComputeRootUnorderedAccessView(3, stats_->GetGPUVirtualAddress());
        list_->EndQuery(tsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot * 4);
        list_->Dispatch((UINT)(depth_.w + 7) / 8, (UINT)(depth_.h + 3) / 4, 1);     // one 8 x 4 group per denoiser tile
        list_->EndQuery(tsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot * 4 + 1);
#if defined(WFC_RT_DENOISER)
        if (dn_.ready()) {
            D3D12_RESOURCE_BARRIER ub{}; ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; list_->ResourceBarrier(1, &ub);
            RtDenoiseParams dp;
            std::memcpy(dp.eye, fr.eye, 12); std::memcpy(dp.projectionInverse, fr.ffxProjInv, 64);
            std::memcpy(dp.reprojection, fr.ffxReproj, 64); std::memcpy(dp.viewProjectionInverse, fr.ffxViewProjInv, 64);
            dp.frameIndex = (unsigned)fr.frameIndex; dp.firstFrame = fr.resetHistory;
            dn_.record(list_, dp, slot);
            list_->SetDescriptorHeaps(1, &heap_);                // (the denoiser bound its own heap)
            if (fr.debugDump) {
                const UINT64 bytes = ((8ull * dn_.tiles() + 511) & ~511ull) + 2ull * dn_.debugRowPitch() * (UINT64)depth_.h;
                ID3D12Resource* rb = makeBuffer(dev_, bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
                if (rb) { dn_.debugCopy(list_, rb); dbgRb_ = rb; dbgSlot_ = slot; }
            }
        }
#endif
        list_->EndQuery(tsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot * 4 + 2);
        list_->ResolveQueryData(tsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot * 4, 3, tsReadback_[slot], 0);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list_->ResourceBarrier(1, &b);
        list_->CopyBufferRegion(readback_[slot], 0, stats_, 0, 4);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        list_->ResourceBarrier(1, &b);
        rbValid_[slot] = true; rbMode_[slot] = 2;
        saoRays_ = fr.aoRays; saoRadius_ = fr.aoRadius;
    }
    void readSao(int slot) {
        rbValid_[slot] = false;
        if (dbgRb_ && dbgSlot_ == slot) {                 // (diagnostics) the denoiser's tile buffers of the dump frame
            void* q = nullptr; D3D12_RANGE rr{0, (SIZE_T)dbgRb_->GetDesc().Width};
            if (SUCCEEDED(dbgRb_->Map(0, &rr, &q))) {
                if (FILE* f = std::fopen("rt_dump_tiles.bin", "wb")) { std::fwrite(q, 1, (size_t)rr.End, f); std::fclose(f); }
                D3D12_RANGE none{0, 0}; dbgRb_->Unmap(0, &none);
            }
            dbgRb_->Release(); dbgRb_ = nullptr;
        }
        uint32_t rays = 0; uint64_t ts[3] = {};
        void* p = nullptr; D3D12_RANGE r{0, 4}, none{0, 0};
        if (SUCCEEDED(readback_[slot]->Map(0, &r, &p))) { rays = *(const uint32_t*)p; readback_[slot]->Unmap(0, &none); }
        D3D12_RANGE r2{0, 24};
        if (SUCCEEDED(tsReadback_[slot]->Map(0, &r2, &p))) { std::memcpy(ts, p, 24); tsReadback_[slot]->Unmap(0, &none); }
        const double ms = tsFreq_ && ts[1] > ts[0] ? (double)(ts[1] - ts[0]) * 1000.0 / (double)tsFreq_ : 0.0;
        const double dnMs = tsFreq_ && ts[2] > ts[1] ? (double)(ts[2] - ts[1]) * 1000.0 / (double)tsFreq_ : 0.0;
        saoMs_.push_back(ms); saoRaySum_ += rays; dnSum_ += dnMs; dnMax_ = std::max(dnMax_, dnMs);
        if (saoMs_.size() < 300) return;
        std::vector<double> v = saoMs_;
        std::sort(v.begin(), v.end());
        double sum = 0; for (double x : v) sum += x;
        const double avg = sum / (double)v.size(), p95 = v[v.size() * 95 / 100];
        const double raysPerFrame = (double)saoRaySum_ / (double)v.size();
        unsigned long long vram = 0;
        if (adapter_) { DXGI_QUERY_VIDEO_MEMORY_INFO mi{}; if (SUCCEEDED(adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &mi))) vram = mi.CurrentUsage; }
        LOG_INFO("RT shadow + AO (300 frames, %dx%d, sun 1 + AO %d rays / pixel, radius %.2f m): GPU %.3f ms avg, %.3f p95, %.3f max; "
                 "%.2f M rays / frame, %.2f G rays / s; RT device VRAM %.1f MB (BLAS %.1f MB + targets)",
                 depth_.w, depth_.h, saoRays_, saoRadius_, avg, p95, v.back(), raysPerFrame / 1e6, avg > 0 ? raysPerFrame / (avg * 1e6) : 0.0,
                 (double)vram / 1048576.0, (double)totalBlas_ / 1048576.0);
        if (dnSum_ > 0) LOG_INFO("RT shadow denoiser (FidelityFX, 5 passes + depth copy): GPU %.3f ms avg, %.3f max", dnSum_ / (double)v.size(), dnMax_);
        saoMs_.clear(); saoRaySum_ = 0; dnSum_ = dnMax_ = 0;
    }
    void readStats(int slot) {
        if (rbMode_[slot] == 2) { readSao(slot); return; }
        rbValid_[slot] = false;
        void* p = nullptr;
        D3D12_RANGE r{0, kStatN * 4};
        if (FAILED(readback_[slot]->Map(0, &r, &p))) return;
        const uint32_t* s = (const uint32_t*)p;
        for (int k = 0; k < kStatN; ++k) statSum_[k] += s[k];
        D3D12_RANGE none{0, 0};
        readback_[slot]->Unmap(0, &none);
        if (++statFrames_ % 300) return;
        unsigned long long tot = 0;
        for (int k = 0; k < 7; ++k) tot += statSum_[k];
        if (!tot) return;
        auto pc = [&](unsigned long long v) { return 100.0 * (double)v / (double)tot; };
        const unsigned long long agree3 = statSum_[0] + statSum_[1] + statSum_[2];
        LOG_INFO("RT depth check (300 frames, %llu pixels): agree within 1e-3 (relative view depth, sky = sky) %.3f %% | "
                 "both sky %.2f, < 1e-4 %.2f, < 1e-3 %.3f, < 1e-2 %.3f, worse %.3f, raster hit / ray miss %.3f, raster sky / ray hit %.3f",
                 tot, pc(agree3), pc(statSum_[0]), pc(statSum_[1]), pc(statSum_[2]), pc(statSum_[3]), pc(statSum_[4]),
                 pc(statSum_[5]), pc(statSum_[6]));
        char hist[400]; int o = 0;
        for (int k = 7; k < kStatN && o < 380; ++k) o += std::snprintf(hist + o, sizeof hist - (size_t)o, " %.1f", pc(statSum_[k]));
        LOG_INFO("RT depth check: raster / ray distance, log2 quarter octaves -2 .. +2 (%%):%s", hist);
        std::fill(statSum_, statSum_ + kStatN, 0ull);
    }
    ID3D12Resource* localTex(int w, int h, DXGI_FORMAT f) {   // D3D12-only target, kept in the UAV state
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = (UINT64)w; rd.Height = (UINT)h;
        rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.Format = f; rd.SampleDesc.Count = 1; rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ID3D12Resource* r = nullptr;
        return SUCCEEDED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                       __uuidof(ID3D12Resource), (void**)&r)) ? r : nullptr;
    }
public:
    void setSignalBuffers(const GLuint* b, int n) { sigBufs_.assign(b, b + std::max(0, n)); }
private:
    bool make(Shared& s, int w, int h, DXGI_FORMAT fmt = DXGI_FORMAT_R32_FLOAT, GLenum glFmt = 0x822E /*GL_R32F*/) {
        while (glGetError() != GL_NO_ERROR) {}
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = (UINT64)w; rd.Height = (UINT)h;
        rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.Format = fmt; rd.SampleDesc.Count = 1;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                 __uuidof(ID3D12Resource), (void**)&s.res))) return false;
        if (FAILED(dev_->CreateSharedHandle(s.res, nullptr, GENERIC_ALL, nullptr, &s.handle))) return false;
        const D3D12_RESOURCE_ALLOCATION_INFO ai = dev_->GetResourceAllocationInfo(0, 1, &rd);
        createMem_(1, &s.mem);
        const GLint ded = 1; if (memParam_) memParam_(s.mem, kDedicated, &ded);
        importMem_(s.mem, ai.SizeInBytes, kHandleD3D12Resource, s.handle);
        glGenTextures(1, &s.tex);
        glBindTexture(GL_TEXTURE_2D, s.tex);
        texStorageMem_(GL_TEXTURE_2D, 1, glFmt, w, h, s.mem, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
        s.w = w; s.h = h;
        return glGetError() == GL_NO_ERROR;
    }
    void free(Shared& s) {
        if (s.tex) glDeleteTextures(1, &s.tex);
        if (s.mem && deleteMem_) deleteMem_(1, &s.mem);
        if (s.handle) CloseHandle(s.handle);
        rel(s.res);
        s = Shared{};
    }
    void waitIdle() {
        if (!queue_) return;
        const UINT64 v = ++value_; queue_->Signal(fence_, v);
        if (fence_->GetCompletedValue() < v) { fence_->SetEventOnCompletion(v, event_); WaitForSingleObject(event_, 5000); }
    }
    PFN_CreateMemoryObjectsEXT createMem_ = nullptr; PFN_DeleteMemoryObjectsEXT deleteMem_ = nullptr;
    PFN_ImportMemoryWin32HandleEXT importMem_ = nullptr; PFN_MemoryObjectParameterivEXT memParam_ = nullptr;
    PFN_TexStorageMem2DEXT texStorageMem_ = nullptr; PFN_GenSemaphoresEXT genSem_ = nullptr;
    PFN_ImportSemaphoreWin32HandleEXT importSem_ = nullptr; PFN_SemaphoreParameterui64vEXT semParam_ = nullptr;
    PFN_WaitSemaphoreEXT waitSem_ = nullptr; PFN_SignalSemaphoreEXT signalSem_ = nullptr;
    PFN_D3D12_SERIALIZE_ROOT_SIGNATURE serializeRs_ = nullptr;
    ID3D12Device* dev_ = nullptr; ID3D12Device5* dev5_ = nullptr; ID3D12CommandQueue* queue_ = nullptr;
    ID3D12CommandAllocator* alloc_[2] = {}; ID3D12GraphicsCommandList* list_ = nullptr;
    ID3D12Fence* fence_ = nullptr; HANDLE fenceHandle_ = nullptr, event_ = nullptr;
    GLuint sem_ = 0;
    int rtTier_ = 0;
    UINT64 value_ = 0, frame_ = 0, allocDone_[2] = {};
    Shared depth_, mask_, ao_, dnOut_;
    ID3D12Resource* hit_ = nullptr; ID3D12Resource* nrm_ = nullptr; ID3D12Resource* vel_ = nullptr;
    std::vector<GLuint> sigBufs_;
    ID3D12Resource* dbgRb_ = nullptr; int dbgSlot_ = -1;
    double dnSum_ = 0, dnMax_ = 0;
#if defined(WFC_RT_DENOISER)
    RtShadowDenoiser dn_; bool dnInit_ = false;
#endif
    std::vector<ID3D12Resource*> blas_; std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances_; ID3D12Resource* tlas_ = nullptr;
    UINT64 totalBlas_ = 0;
    ID3D12RootSignature* rs_ = nullptr; ID3D12PipelineState* pso_ = nullptr; ID3D12DescriptorHeap* heap_ = nullptr;
    ID3D12PipelineState* psoSao_ = nullptr; ID3D12QueryHeap* tsHeap_ = nullptr; ID3D12Resource* tsReadback_[2] = {};
    IDXGIAdapter3* adapter_ = nullptr; UINT64 tsFreq_ = 0; int rbMode_[2] = {};
    std::vector<double> saoMs_; unsigned long long saoRaySum_ = 0; int saoRays_ = 0; float saoRadius_ = 0.0f;
    ID3D12Resource* stats_ = nullptr; ID3D12Resource* zeros_ = nullptr; ID3D12Resource* readback_[2] = {};
    bool rbValid_[2] = {};
    unsigned long long statSum_[kStatN] = {}; int statFrames_ = 0;
    double sum_[3] = {}, max_[3] = {}; int n_ = 0;
};

Bridge* g_bridge = nullptr;
bool g_failed = false;
bool ensure() {
    if (g_failed) return false;
    if (!g_bridge) { g_bridge = new Bridge(); if (!g_bridge->init()) { delete g_bridge; g_bridge = nullptr; g_failed = true; return false; } }
    return true;
}
}  // namespace

bool rtBridgePrepare(int w, int h, RtTargets* t) { return ensure() && g_bridge->prepare(w, h, t); }
bool rtBridgeAddStaticWorld(const RtStaticWorld& world, unsigned mask) { return ensure() && g_bridge->addStaticWorld(world, mask); }
void rtBridgeClearStaticWorld() { if (g_bridge) g_bridge->clearStaticWorld(); }
bool rtBridgeRoundTrip(const RtFrame* frame) { return g_bridge && g_bridge->roundTrip(frame); }
void rtBridgeSetSignalBuffers(const GLuint* bufs, int n) { if (g_bridge) g_bridge->setSignalBuffers(bufs, n); }

}  // namespace render
#else
#include "render/d3d12/D3D12RtBridge.h"
namespace render {
bool rtBridgePrepare(int, int, RtTargets*) { return false; }
bool rtBridgeAddStaticWorld(const RtStaticWorld&, unsigned) { return false; }
void rtBridgeClearStaticWorld() {}
bool rtBridgeRoundTrip(const RtFrame*) { return false; }
void rtBridgeSetSignalBuffers(const GLuint*, int) {}
}  // namespace render
#endif
