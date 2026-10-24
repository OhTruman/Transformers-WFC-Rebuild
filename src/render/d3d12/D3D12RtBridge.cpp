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

#include "render/gl/WfcRtDefaults.h"
#include "render/d3d12/D3D12RtBridge.h"
#include "render/d3d12/D3D12RtDynamic.h"
#include "core/Log.h"
#if __has_include("render/d3d12/D3D12Device.h")
#include "render/d3d12/D3D12Device.h"             // (the presenter's / FSR 3.1's shared device, when the tree has it)
#define WFC_RT_SHARED_DEVICE 1
#endif
#if __has_include("render/d3d12/D3D12Adapter.h")
#include "render/d3d12/D3D12Adapter.h"
#define WFC_RT_GL_ADAPTER 1
#endif
#if defined(WFC_RT_SHADERS)
#include "rt_depthcheck.h"
#include "rt_shadow_ao.h"
#include "rt_ao_filter.h"
#include "rt_reflect.h"
#include "rt_volumetric.h"
#include "rt_shadow_soft.h"
#include "rt_gi.h"
#include "rt_gi_dilate.h"
#include "rt_gi_probe.h"
#endif
#if defined(WFC_RT_DENOISER)
#include "render/d3d12/D3D12RtDenoise.h"
#endif

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
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
constexpr int kSrvN = 16, kTableN = kSrvN + 4;  // descriptor table: t1 .. t16, u0 .. u3
constexpr int kMaxTex2D = 2048, kMaxTexCube = 256;   // reflection-hit material textures (space2 / space3)
constexpr int kMaxLm = 512;                      // lightmap pages (root table 4: t0 .. space1)
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
        deleteSem_ = (PFN_DeleteSemaphoresEXT_)g("glDeleteSemaphoresEXT");
        if (!createMem_ || !importMem_ || !texStorageMem_ || !genSem_ || !importSem_ || !semParam_ || !waitSem_ || !signalSem_) {
            LOG_WARN("RT bridge: GL external-object entry points missing"); return false;
        }
#if defined(WFC_RT_SHARED_DEVICE)
        dev_ = d3d12SharedDevice();                      // one device with the presenter and FSR 3.1 (D3D12Device.h)
        const bool made = dev_ != nullptr;
        HMODULE d3d = LoadLibraryW(L"d3d12.dll");
        serializeRs_ = d3d ? (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)(void*)GetProcAddress(d3d, "D3D12SerializeRootSignature") : nullptr;
#else
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
#endif
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
    // (split screen) the other view's view-sized set - targets, froxel history, hit / normal / velocity, denoiser - swapped
    // with the active one; the descriptor tables follow the active set, rewritten after the GPU went idle (the previous
    // view's round trip may still read them). The world, the TLAS rings and the GI stay shared
    void selectView(int v) {
        if (v == view_) return;
        if ((depth_.res || other_.depth.res) && !waitIdle()) return;
        using std::swap;
        swap(depth_, other_.depth); swap(mask_, other_.mask); swap(ao_, other_.ao); swap(dnOut_, other_.dnOut);
        swap(wdepth_, other_.wdepth); swap(refl_, other_.refl); swap(soft_, other_.soft); swap(lvis_, other_.lvis);
        swap(pcol_, other_.pcol); swap(vol_, other_.vol);
        swap(volH_[0], other_.volH[0]); swap(volH_[1], other_.volH[1]);
        swap(volTx_, other_.volTx); swap(volTy_, other_.volTy); swap(volParity_, other_.volParity); swap(volHistValid_, other_.volHistValid);
        swap(hit_, other_.hit); swap(nrm_, other_.nrm); swap(vel_, other_.vel);
#if defined(WFC_RT_DENOISER)
        swap(dn_, other_.dn); swap(dnViewInit_, other_.dnViewInit);
        if (dnInit_ && !dnViewInit_) dnViewInit_ = dn_.init(dev_);   // (the second view's denoiser, first use)
#endif
        view_ = v;
        if (heap_ && depth_.res) writeDescriptors();
    }

#if defined(WFC_RT_DENOISER)
    bool denoised() const { return dn_.ready(); }
#else
    bool denoised() const { return false; }
#endif

    bool prepare(int w, int h, RtTargets* t) {
        if (w != depth_.w || h != depth_.h) {
            if (!waitIdle()) return false;               // (in flight: the old targets are not freed)
            free(depth_); free(mask_); free(ao_); free(dnOut_); free(wdepth_); free(refl_); free(soft_); free(lvis_); free(pcol_); free(vol_);
            rel(hit_); rel(nrm_); rel(vel_); rel(volH_[0]); rel(volH_[1]);
            hit_ = localTex((w + 7) / 8, (h + 3) / 4, DXGI_FORMAT_R32_UINT);
            // (X3) the froxel grid: 16-pixel tiles x 64 slices as an 8 x 8 atlas of slices; the integrated grid is shared
            // with GL, the injected grid's history ping-pong stays in D3D12
            volTx_ = (w + 15) / 16; volTy_ = (h + 15) / 16;
            volH_[0] = localTex(volTx_ * 8, volTy_ * 8, DXGI_FORMAT_R16G16B16A16_FLOAT);
            volH_[1] = localTex(volTx_ * 8, volTy_ * 8, DXGI_FORMAT_R16G16B16A16_FLOAT);
            volParity_ = 0; volHistValid_ = false;
            nrm_ = localTex(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT);
            vel_ = localTex(w, h, DXGI_FORMAT_R16G16_FLOAT);
            if (!make(depth_, w, h) || !make(mask_, w, h) || !make(ao_, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, 0x881A /*GL_RGBA16F*/) || !make(wdepth_, w, h) ||
                !make(dnOut_, w, h, DXGI_FORMAT_R8_UNORM, 0x8229 /*GL_R8*/) || !make(refl_, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, 0x881A /*GL_RGBA16F*/) || !make(soft_, w, h, DXGI_FORMAT_R8_UNORM, 0x8229 /*GL_R8*/) ||
                !make(lvis_, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0x8058 /*GL_RGBA8*/) ||
                !make(pcol_, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, 0x881A /*GL_RGBA16F*/) ||
                !make(vol_, volTx_ * 8, volTy_ * 8, DXGI_FORMAT_R16G16B16A16_FLOAT, 0x881A /*GL_RGBA16F*/) || !hit_ || !nrm_ || !vel_ ||
                !volH_[0] || !volH_[1]) {
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
        t->depth = depth_.tex; t->mask = mask_.tex; t->shadowAo = ao_.tex; t->shadowDn = dnOut_.tex; t->worldDepth = wdepth_.tex; t->refl = refl_.tex; t->shadowSoft = soft_.tex; t->lightVis = lvis_.tex; t->prevColor = pcol_.tex; t->vol = vol_.tex;
        return true;
    }

    // ---- the static world: per MDI set (world, level BSP) a one-sided and a two-sided BLAS as identity instances with
    // the set's instance mask; the TLAS is rebuilt over all instances whenever a set is added (map load) ----
    bool addStaticWorld(const RtStaticWorld& W, unsigned mask) {
        if (tlas_) { if (!waitIdle()) return false; rel(tlas_); }
#if !defined(WFC_RT_SHADERS)
        LOG_WARN("RT bridge: built without the RT shaders (tools/render/fetch_dxc.py before configuring): no depth check");
        return false;
#else
        if (rtTier_ < 11) { LOG_WARN("RT bridge: raytracing tier %d.%d < 1.1 (inline RayQuery): no depth check", rtTier_ / 10, rtTier_ % 10); return false; }
        if (!dev5_ && FAILED(dev_->QueryInterface(__uuidof(ID3D12Device5), (void**)&dev5_))) return false;
        rtDynamicAttach(dev5_, tsFreq_);                  // (M7 RT characters: D3D12RtDynamic.h)
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
            in.InstanceID = W.idBase + (UINT)k;             // (0x10000 + set * 2 + side: the reflection hits' lookups)
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
            if (!(ok ? submitTimed(cl, "static BLAS + TLAS") : waitIdle())) { LOG_WARN("RT bridge: BLAS build not finished: its upload / scratch buffers leaked, set dropped"); return false; }
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
    GLuint maskAtlas(int w, int h) {
        if (!atlas_.tex || atlas_.w != w || atlas_.h != h) {
            if (!waitIdle()) return 0;
            free(atlas_);
            if (!make(atlas_, w, h, DXGI_FORMAT_R8_UNORM, 0x8229 /*GL_R8*/)) { free(atlas_); return 0; }
            if (heap_) writeDescriptors();
        }
        return atlas_.tex;
    }
    bool addMasked(const RtMaskedWorld& M) {
#if !defined(WFC_RT_SHADERS)
        (void)M; return false;
#else
        if (!dev5_ || rtTier_ < 11 || M.sets.empty() || !waitIdle()) return false;
        releaseMasked();
        ID3D12CommandAllocator* al = nullptr; ID3D12GraphicsCommandList4* cl = nullptr;
        if (FAILED(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&al)) ||
            FAILED(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, al, nullptr, __uuidof(ID3D12GraphicsCommandList4), (void**)&cl))) {
            rel(al); return false;
        }
        std::vector<ID3D12Resource*> temp;               // uploads + scratch, freed after the build
        auto upload = [&](const void* d, size_t bytes) { ID3D12Resource* r = uploadBuffer(dev_, d, bytes); if (r) temp.push_back(r); return r; };
        auto toDefault = [&](const void* d, size_t bytes) -> ID3D12Resource* {   // a default-heap copy the shader reads
            ID3D12Resource* up = upload(d, std::max<size_t>(bytes, 16));
            ID3D12Resource* r = makeBuffer(dev_, std::max<size_t>(bytes, 16), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
            if (!up || !r) { rel(r); return nullptr; }
            cl->CopyBufferRegion(r, 0, up, 0, std::max<size_t>(bytes, 16));
            D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = r;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            cl->ResourceBarrier(1, &b);
            return r;
        };
        std::vector<float> uv; std::vector<uint32_t> idx, triSub, inst;
        bool ok = true;
        for (size_t k = 0; k < M.sets.size() && ok; ++k) {
            const RtMaskedSet& S = M.sets[k];
            const uint32_t uvBase = (uint32_t)(uv.size() / 2), triBase = (uint32_t)(idx.size() / 3);
            inst.insert(inst.end(), {uvBase, triBase, 0u, 0u});
            uv.insert(uv.end(), S.uv.begin(), S.uv.end());
            idx.insert(idx.end(), S.idx.begin(), S.idx.end());
            triSub.insert(triSub.end(), S.triSub.begin(), S.triSub.end());
            if (S.idx.empty()) continue;
            ID3D12Resource* vb = upload(S.pos.data(), S.pos.size() * 4);
            ID3D12Resource* ib = upload(S.idx.data(), S.idx.size() * 4);
            if (!vb || !ib) { ok = false; break; }
            D3D12_RAYTRACING_GEOMETRY_DESC gd{};
            gd.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES; gd.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;   // non-opaque: alpha-tested
            gd.Triangles.VertexBuffer.StartAddress = vb->GetGPUVirtualAddress(); gd.Triangles.VertexBuffer.StrideInBytes = 12;
            gd.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT; gd.Triangles.VertexCount = (UINT)(S.pos.size() / 3);
            gd.Triangles.IndexBuffer = ib->GetGPUVirtualAddress(); gd.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
            gd.Triangles.IndexCount = (UINT)S.idx.size();
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
            if (sc) temp.push_back(sc);
            if (!bl || !sc) { rel(bl); ok = false; break; }
            bd.DestAccelerationStructureData = bl->GetGPUVirtualAddress(); bd.ScratchAccelerationStructureData = sc->GetGPUVirtualAddress();
            cl->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);
            mBlas_.push_back(bl); totalBlas_ += pi.ResultDataMaxSizeInBytes;
            D3D12_RAYTRACING_INSTANCE_DESC in{};
            in.Transform[0][0] = in.Transform[1][1] = in.Transform[2][2] = 1.0f;
            in.InstanceID = 0x20000u + (UINT)k;          // -> mInst_[k]: this set's uv / triangle bases
            in.InstanceMask = 0x8;                       // masked world: its own bit (the depth check leaves it out)
            in.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;   // (cards / grates: both sides)
            in.AccelerationStructure = bl->GetGPUVirtualAddress();
            instances_.push_back(in);
        }
        if (ok) {
            mUv_ = toDefault(uv.data(), uv.size() * 4); mIdx_ = toDefault(idx.data(), idx.size() * 4);
            mTriSub_ = toDefault(triSub.data(), triSub.size() * 4); mSubs_ = toDefault(M.subs.data(), M.subs.size() * 4);
            mInst_ = toDefault(inst.data(), inst.size() * 4);
            ok = mUv_ && mIdx_ && mTriSub_ && mSubs_ && mInst_;
            mVerts_ = (UINT)(uv.size() / 2); mTris_ = (UINT)(idx.size() / 3); mSubN_ = (UINT)(M.subs.size() / 8); mSetN_ = (UINT)M.sets.size();
        }
        if (ok) ok = rebuildTlas(cl, temp);
        cl->Close();
        const bool idle = ok ? submitTimed(cl, "masked BLAS + TLAS") : waitIdle();
        rel(cl); rel(al);
        if (idle) for (auto* r : temp) r->Release();     // (not idle: leaked rather than freed in flight)
        if (!ok) { releaseMasked(); return false; }
        if (heap_) writeDescriptors();
        LOG_INFO("RT bridge: masked world: %u triangles, %u subs, %u vertices; TLAS %zu instances", mTris_, mSubN_, mVerts_, instances_.size());
        return true;
#endif
    }
    // (chapter streaming) a static set's two instances (InstanceID idBase, idBase + 1) take this mask - 0: out of every trace -
    // and the static TLAS is rebuilt (the per-frame character TLAS copies instances_ each frame)
    bool setStaticMask(unsigned idBase, unsigned mask) {
#if !defined(WFC_RT_SHADERS)
        (void)idBase; (void)mask; return false;
#else
        bool any = false;
        for (auto& in : instances_)
            if (in.InstanceID >= idBase && in.InstanceID < idBase + 2 && in.InstanceMask != (mask & 0xFF)) { in.InstanceMask = mask & 0xFF; any = true; }
        if (!any) return true;
        if (!dev5_ || !tlas_ || !waitIdle()) return false;
        ID3D12CommandAllocator* al = nullptr; ID3D12GraphicsCommandList4* cl = nullptr;
        if (FAILED(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&al)) ||
            FAILED(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, al, nullptr, __uuidof(ID3D12GraphicsCommandList4), (void**)&cl))) {
            rel(al); return false;
        }
        std::vector<ID3D12Resource*> temp;
        const bool ok = rebuildTlas(cl, temp);
        cl->Close();
        const bool idle = ok ? submitTimed(cl, "static set mask") : waitIdle();
        rel(cl); rel(al);
        if (idle) for (auto* r : temp) r->Release();
        if (ok && heap_) writeDescriptors();
        return ok;
#endif
    }
    // (chapter streaming) a static set removed: its two instances leave instances_, their BLAS are released, the TLAS rebuilt
    bool removeStaticSet(unsigned idBase) {
#if !defined(WFC_RT_SHADERS)
        (void)idBase; return false;
#else
        if (!dev5_ || !waitIdle()) return false;
        std::vector<D3D12_GPU_VIRTUAL_ADDRESS> gone;
        instances_.erase(std::remove_if(instances_.begin(), instances_.end(), [&](const D3D12_RAYTRACING_INSTANCE_DESC& d) {
            if (d.InstanceID < idBase || d.InstanceID >= idBase + 2) return false;
            gone.push_back(d.AccelerationStructure); return true; }), instances_.end());
        if (gone.empty()) return true;
        for (D3D12_GPU_VIRTUAL_ADDRESS va : gone)
            for (size_t b = 0; b < blas_.size(); ++b)
                if (blas_[b] && blas_[b]->GetGPUVirtualAddress() == va) { blas_[b]->Release(); blas_.erase(blas_.begin() + (long)b); break; }
        ID3D12CommandAllocator* al = nullptr; ID3D12GraphicsCommandList4* cl = nullptr;
        if (FAILED(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&al)) ||
            FAILED(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, al, nullptr, __uuidof(ID3D12GraphicsCommandList4), (void**)&cl))) {
            rel(al); return false;
        }
        std::vector<ID3D12Resource*> temp;
        const bool ok = rebuildTlas(cl, temp);
        cl->Close();
        const bool idle = ok ? submitTimed(cl, "static set removed") : waitIdle();
        rel(cl); rel(al);
        if (idle) for (auto* r : temp) r->Release();
        if (ok && heap_) writeDescriptors();
        return ok;
#endif
    }
    // the TLAS over instances_ (static sets + masked sets), built on cl; the previous TLAS released (callers waited idle)
    bool rebuildTlas(ID3D12GraphicsCommandList4* cl, std::vector<ID3D12Resource*>& temp) {
        D3D12_RESOURCE_BARRIER ub{}; ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        cl->ResourceBarrier(1, &ub);
        ID3D12Resource* ib = uploadBuffer(dev_, instances_.data(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instances_.size());
        if (!ib) return false;
        temp.push_back(ib);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd{};
        bd.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        bd.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        bd.Inputs.NumDescs = (UINT)instances_.size(); bd.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        bd.Inputs.InstanceDescs = ib->GetGPUVirtualAddress();
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pi{};
        dev5_->GetRaytracingAccelerationStructurePrebuildInfo(&bd.Inputs, &pi);
        ID3D12Resource* t = makeBuffer(dev_, pi.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                                       D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ID3D12Resource* sc = makeBuffer(dev_, pi.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (sc) temp.push_back(sc);
        if (!t || !sc) { rel(t); return false; }
        bd.DestAccelerationStructureData = t->GetGPUVirtualAddress(); bd.ScratchAccelerationStructureData = sc->GetGPUVirtualAddress();
        cl->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);
        rel(tlas_); tlas_ = t;
        return true;
    }
    bool addHitData(const RtHitData& D) {
#if !defined(WFC_RT_SHADERS)
        (void)D; return false;
#else
        if (!dev_ || !waitIdle()) return false;
        releaseHit();
        ID3D12CommandAllocator* al = nullptr; ID3D12GraphicsCommandList* cl = nullptr;
        if (FAILED(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&al)) ||
            FAILED(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, al, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&cl))) {
            rel(al); return false;
        }
        std::vector<ID3D12Resource*> temp;
        auto toDefault = [&](const void* d, size_t bytes) -> ID3D12Resource* {
            bytes = std::max<size_t>(bytes, 16);
            ID3D12Resource* up = uploadBuffer(dev_, d, bytes);
            ID3D12Resource* r = makeBuffer(dev_, bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
            if (up) temp.push_back(up);
            if (!up || !r) { rel(r); return nullptr; }
            cl->CopyBufferRegion(r, 0, up, 0, bytes);
            D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = r;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            cl->ResourceBarrier(1, &b);
            return r;
        };
        // per BLAS (index = set * 2 + side): uv1 base (its set's), triangle base, index base
        std::vector<uint32_t> inst, idx, triSub; std::vector<float> uv1, vtx; std::vector<uint32_t> setBase;
        bool haveVtx = !D.materialShader.empty();
        for (const auto& S : D.sets) haveVtx = haveVtx && S.vtx.size() == S.uv1.size() / 2 * 8;
        for (const auto& S : D.sets) {
            setBase.push_back((uint32_t)(uv1.size() / 2)); uv1.insert(uv1.end(), S.uv1.begin(), S.uv1.end());
            if (haveVtx) vtx.insert(vtx.end(), S.vtx.begin(), S.vtx.end());
        }
        for (const auto& B : D.blas) {
            inst.insert(inst.end(), {B.setIndex < (int)setBase.size() ? setBase[(size_t)B.setIndex] : 0u, (uint32_t)triSub.size(), (uint32_t)idx.size(), 0u});
            idx.insert(idx.end(), B.idx.begin(), B.idx.end());
            triSub.insert(triSub.end(), B.triSub.begin(), B.triSub.end());
        }
        hInst_ = toDefault(inst.data(), inst.size() * 4); hIdx_ = toDefault(idx.data(), idx.size() * 4);
        hUv1_ = toDefault(uv1.data(), uv1.size() * 4); hTriSub_ = toDefault(triSub.data(), triSub.size() * 4);
        hSubs_ = toDefault(D.subs.data(), D.subs.size() * 4);
        hBlasN_ = (UINT)D.blas.size(); hTris_ = (UINT)triSub.size(); hVerts_ = (UINT)(uv1.size() / 2); hSubN_ = (UINT)(D.subs.size() / 32);
        bool ok = hInst_ && hIdx_ && hUv1_ && hTriSub_ && hSubs_;
        UINT64 matBytes = 0, chunkBytes = 0;
        constexpr UINT64 kChunkBytes = 32ull << 20;
        if (ok && haveVtx && !D.matTex.empty()) {        // materials: per-vertex frame / UV0, the slot table, the textures
            hVtx_ = toDefault(vtx.data(), vtx.size() * 4); hMatTex_ = toDefault(D.matTex.data(), D.matTex.size() * 4);
            hMatTexN_ = (UINT)(D.matTex.size() / 12);
            matBytes += (UINT64)(vtx.size() + D.matTex.size()) * 4;
            static const DXGI_FORMAT kFmt[13] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_BC1_UNORM,
                DXGI_FORMAT_BC1_UNORM_SRGB, DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC2_UNORM_SRGB, DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM_SRGB,
                DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_BC4_UNORM,
                DXGI_FORMAT_BC7_UNORM};
            for (int cube = 0; cube < 2 && ok; ++cube) {
                const auto& list = cube ? D.cubes : D.textures;
                auto& out = cube ? hTexCube_ : hTex2D_;
                for (const RtHitTexture& X : list) {
                    if ((int)out.size() >= (cube ? kMaxTexCube : kMaxTex2D)) break;
                    const int faces = cube ? 6 : 1;
                    ID3D12Resource* t = nullptr;
                    if (X.w > 0 && X.h > 0 && X.levels > 0 && X.format >= 0 && X.format <= 12 && (int)X.data.size() == faces * X.levels) {
                        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
                        D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = (UINT64)X.w; rd.Height = (UINT)X.h;
                        rd.DepthOrArraySize = (UINT16)faces; rd.MipLevels = (UINT16)X.levels; rd.Format = kFmt[X.format]; rd.SampleDesc.Count = 1;
                        if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                                 __uuidof(ID3D12Resource), (void**)&t))) t = nullptr;
                        if (t) {
                            const UINT nsub = (UINT)(faces * X.levels);
                            std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(nsub); std::vector<UINT> rows(nsub); std::vector<UINT64> rowBytes(nsub);
                            UINT64 total = 0;
                            dev_->GetCopyableFootprints(&rd, 0, nsub, 0, fp.data(), rows.data(), rowBytes.data(), &total);
                            std::vector<unsigned char> staged((size_t)total);
                            bool fits = true;
                            for (UINT k = 0; k < nsub; ++k) {     // (subresource = face * levels + level, as data)
                                const auto& src = X.data[k];
                                if ((UINT64)src.size() < rowBytes[k] * rows[k]) { fits = false; break; }
                                for (UINT y = 0; y < rows[k]; ++y)
                                    std::memcpy(&staged[(size_t)(fp[k].Offset + (UINT64)y * fp[k].Footprint.RowPitch)], &src[(size_t)(y * rowBytes[k])], (size_t)rowBytes[k]);
                            }
                            ID3D12Resource* up = fits ? uploadBuffer(dev_, staged.data(), staged.size()) : nullptr;
                            if (!up) { rel(t); }
                            else {
                                temp.push_back(up);
                                for (UINT k = 0; k < nsub; ++k) {
                                    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = t; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = k;
                                    D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = up; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp[k];
                                    cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                                }
                                D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = t;
                                b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                                b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                                cl->ResourceBarrier(1, &b);
                                matBytes += total;
                                chunkBytes += total;
                            }
                        }
                    }
                    out.push_back(t);                    // (null keeps the index: the slot samples black)
                    if (chunkBytes >= kChunkBytes && ok) {   // (bounded submissions: this chunk's copies now, its uploads freed)
                        cl->Close();
                        if (!submitTimed(cl, "hit data: material texture copies")) { ok = false; break; }
                        for (auto* r : temp) r->Release();
                        temp.clear(); chunkBytes = 0;
                        al->Reset(); cl->Reset(al, nullptr);
                    }
                }
            }
            // the map's material reflection PSO (one uber-switch over every material): AMD's compiler took 440 s for
            // Molten on a cold driver cache (2026-10-13; the main thread inside amdxc64.dll at the first Realistic
            // frame). Compiled on a worker; until it is ready the reflection rays shade with the proxy PSO (the same
            // fallback as a failed build), then it is picked up between frames.
            startMaterialPso(D.materialShader);
        }
        UINT64 lmBytes = 0;
        for (size_t k = 0; k < D.lightmaps.size() && k < (size_t)kMaxLm && ok; ++k) {   // the lightmap pages
            const RtLightmap& L = D.lightmaps[k];
            if (L.w <= 0 || L.h <= 0) { hLm_.push_back({nullptr, false}); continue; }
            D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = (UINT64)L.w; rd.Height = (UINT)L.h;
            rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 1;
            ID3D12Resource* t = nullptr;
            if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     __uuidof(ID3D12Resource), (void**)&t))) { ok = false; break; }
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT64 total = 0;
            dev_->GetCopyableFootprints(&rd, 0, 1, 0, &fp, nullptr, nullptr, &total);
            std::vector<unsigned char> staged((size_t)total);
            for (int y = 0; y < L.h; ++y) std::memcpy(&staged[(size_t)y * fp.Footprint.RowPitch], &L.rgba[(size_t)y * L.w * 4], (size_t)L.w * 4);
            ID3D12Resource* up = uploadBuffer(dev_, staged.data(), staged.size());
            if (!up) { rel(t); ok = false; break; }
            temp.push_back(up);
            D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = t; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = up; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp;
            cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = t;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            cl->ResourceBarrier(1, &b);
            hLm_.push_back({t, L.srgb});
            lmBytes += (UINT64)L.w * L.h * 4;
        }
        cl->Close();
        const bool idle = ok ? submitTimed(cl, "hit data: buffers + lightmaps") : waitIdle();
        rel(cl); rel(al);
        if (idle) for (auto* r : temp) r->Release();
        if (!ok) { releaseHit(); return false; }
        if (heap_) writeDescriptors();
        hitBytes_ = (UINT64)(inst.size() + idx.size() + triSub.size() + uv1.size() + D.subs.size()) * 4 + lmBytes + matBytes;
        LOG_INFO("RT bridge: reflection hit data: %u BLAS, %u triangles, %u subs, %zu lightmap pages; %.1f MB (of it materials %.1f MB:"
                 " %zu + %zu cube textures, %u slots, vertex frames; material PSO %s)", hBlasN_, hTris_, hSubN_, hLm_.size(),
                 (double)hitBytes_ / 1048576.0, (double)matBytes / 1048576.0, hTex2D_.size(), hTexCube_.size(), hMatTexN_,
                 psoJob_ ? "compiling" : psoReflMat_ ? "yes" : "no");
        LOG_INFO("RT bridge: longest load-time D3D12 submission this map: %.0f ms ('%s')", maxSubmitMs_, maxSubmitWhat_);
        return true;
#endif
    }

    // ---- R4: the runtime ray-traced lightmap. Per GL lightmap page a shared RGBA16F GI page (GL samples it in place of
    // the bake), the texel list / gutter list / GiData (subs, lights, hit page map) as buffers; a job is recorded into
    // the next round trip (per frame: a slice of the texels) or run at once (load: whole passes, chunked) ----
    bool giSetup(const RtGiSetup& S, std::vector<GLuint>& glTex) {
#if !defined(WFC_RT_SHADERS)
        (void)S; (void)glTex; return false;
#else
        glTex.clear();
        if (!dev_ || !psoGi_ || !heap_ || !waitIdle()) return false;
        giRelease(); giReused_ = 0;
        gi_.resize(std::min(S.pageSize.size(), (size_t)kMaxLm));
        UINT64 bytes = 0;
        for (size_t k = 0; k < gi_.size(); ++k) {
            const int w = S.pageSize[k].first, h = S.pageSize[k].second;
            if (w <= 0 || h <= 0) continue;
            const bool f32 = (int)k == S.floatPage;
            const DXGI_FORMAT fmt = f32 ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
            // (a page of this size + format from an earlier map's pool first: freed GL-imported D3D12 memory stayed charged
            // to the process on AMD - +~300 MB per map change - so the shared pages are kept and reused, never freed until exit;
            // the first propagation pass overwrites every listed texel, the gutter pass the gutters)
            bool pooled = false;
            for (size_t q = 0; q < giPool_.size(); ++q)
                if (giPool_[q].first.w == w && giPool_[q].first.h == h && giPool_[q].second == fmt) {
                    gi_[k] = giPool_[q].first; giPool_.erase(giPool_.begin() + (std::ptrdiff_t)q); pooled = true; ++giReused_; break;
                }
            giFmt_.resize(gi_.size(), DXGI_FORMAT_UNKNOWN); giFmt_[k] = fmt;
            if (!pooled && !make(gi_[k], w, h, fmt, f32 ? 0x8814 /*GL_RGBA32F*/ : 0x881A /*GL_RGBA16F*/)) {
                LOG_WARN("RT GI: shared page %zu failed", k); giRelease(); return false;
            }
            glBindTexture(GL_TEXTURE_2D, gi_[k].tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F /*GL_CLAMP_TO_EDGE*/); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
            glBindTexture(GL_TEXTURE_2D, 0);
            giSig_.push_back(gi_[k].tex);
            bytes += (UINT64)w * h * (f32 ? 16 : 8);
        }
        ID3D12CommandAllocator* al = nullptr; ID3D12GraphicsCommandList* cl = nullptr;
        if (FAILED(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&al)) ||
            FAILED(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, al, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&cl))) {
            rel(al); giRelease(); return false;
        }
        std::vector<ID3D12Resource*> temp;
        auto toDefault = [&](const void* d, size_t n) -> ID3D12Resource* {
            n = std::max<size_t>(n, 16);
            ID3D12Resource* up = uploadBuffer(dev_, d, n);
            ID3D12Resource* r = makeBuffer(dev_, n, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
            if (up) temp.push_back(up);
            if (!up || !r) { rel(r); return nullptr; }
            cl->CopyBufferRegion(r, 0, up, 0, n);
            D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = r;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            cl->ResourceBarrier(1, &b);
            return r;
        };
        giTexels_ = toDefault(S.texels.data(), S.texels.size() * 4); giTexelN_ = (UINT)(S.texels.size() / 8);
        giDilate_ = toDefault(S.dilate.data(), S.dilate.size() * 4); giDilateN_ = (UINT)(S.dilate.size() / 4);
        giData_ = toDefault(S.data.data(), S.data.size() * 4); giDataN_ = (UINT)(S.data.size() / 4);
        cl->Close();
        const bool ok = giTexels_ && giDilate_ && giData_ && submitTimed(cl, "GI: texel / gutter / data buffers");
        rel(cl); rel(al);
        for (auto* r : temp) r->Release();
        if (!ok) { giRelease(); return false; }
        giHitMap_ = S.hitToGi;                           // (item 3: the reflection hits sample the GI pages)
        writeDescriptors();
        for (const Shared& s : gi_) glTex.push_back(s.tex);
        giBytes_ = bytes + (UINT64)(S.texels.size() + S.dilate.size() + S.data.size()) * 4;
        LOG_INFO("RT GI: %zu shared pages (%zu reused from the pool, %zu pooled spare), %u texels, %u gutter texels, %.1f MB on D3D12 (device VRAM now %.0f MB)", giSig_.size(), giReused_, giPool_.size(), giTexelN_, giDilateN_,
                 (double)giBytes_ / 1048576.0, vramMB());
        return true;
#endif
    }
    void giQueue(const RtGiJob& j) { giJob_ = j; giPending_ = true; }
    // S3: trace the probe grid (chunked submissions), read it back
    bool giProbes(const RtGiJob& j0, const float origin[3], float spacing, const int dims[3], int rays, const std::vector<uint32_t>& cells,
                  std::vector<float>& out) {
        out.clear();
        if (!giReady() || !tlas_ || !psoGiProbe_ || dims[0] <= 0 || dims[1] <= 0 || dims[2] <= 0 || cells.empty()) return false;
        const UINT n = (UINT)cells.size();
        rel(giProbeBuf_); rel(giProbeCells_);
        {   // the cell list (default heap, via an upload copy)
            ID3D12Resource* up = uploadBuffer(dev_, cells.data(), cells.size() * 4);
            giProbeCells_ = makeBuffer(dev_, cells.size() * 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
            if (!up || !giProbeCells_) { rel(up); rel(giProbeCells_); return false; }
            const bool copied = oneShot([&](ID3D12GraphicsCommandList* cl) {
                cl->CopyBufferRegion(giProbeCells_, 0, up, 0, cells.size() * 4);
                D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = giProbeCells_;
                b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                cl->ResourceBarrier(1, &b);
            }, "GI: probe cells");
            rel(up);
            if (!copied) { rel(giProbeCells_); return false; }
        }
        giProbeBuf_ = makeBuffer(dev_, (UINT64)n * 96, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ID3D12Resource* rb = makeBuffer(dev_, (UINT64)n * 96, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!giProbeBuf_ || !rb) { rel(giProbeBuf_); rel(rb); return false; }
        giProbeN_ = n;
        writeDescriptors();
        const UINT inc = dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_GPU_DESCRIPTOR_HANDLE base = heap_->GetGPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE lm = base; lm.ptr += 4ull * kTableN * inc;
        D3D12_GPU_DESCRIPTOR_HANDLE tG = base; tG.ptr += (4ull * kTableN + kMaxLm + kMaxTex2D + kMaxTexCube + kMaxLm + 2ull + 2ull * kTableN) * inc;
        const UINT chunk = 16384;
        bool ok = true;
        for (UINT first = 0; first < n && ok; first += chunk) {
            const UINT count = std::min(chunk, n - first);
            ok = oneShot([&](ID3D12GraphicsCommandList* cl) {
                float c[28] = {};
                c[0] = origin[0]; c[1] = origin[1]; c[2] = origin[2]; c[3] = spacing;
                c[4] = (float)dims[0]; c[5] = (float)dims[1]; c[6] = (float)dims[2];
                std::memcpy(c + 8, j0.sky, 12); c[11] = (float)rays;
                const uint32_t u[4] = {first, count, 0u, 0u}; std::memcpy(c + 12, u, 16);
                std::memcpy(c + 16, j0.mix, 16);
                const uint32_t o[4] = {j0.subOff, j0.lightOff, j0.pageMapOff, j0.mask}; std::memcpy(c + 20, o, 16);
                c[24] = j0.lightSize; c[25] = j0.bounceSat; c[26] = j0.emissiveGain;   // (X4: the probes see the same saturated bounce)
                cl->SetComputeRootSignature(rs_);
                cl->SetDescriptorHeaps(1, &heap_);
                cl->SetPipelineState(psoGiProbe_);
                cl->SetComputeRoot32BitConstants(0, 28, c, 0);
                cl->SetComputeRootShaderResourceView(1, tlas_->GetGPUVirtualAddress());
                cl->SetComputeRootDescriptorTable(2, tG);
                cl->SetComputeRootUnorderedAccessView(3, stats_->GetGPUVirtualAddress());
                cl->SetComputeRootDescriptorTable(4, lm);
                cl->Dispatch((count + 63) / 64, 1, 1);
                if (first + count >= n) {                // (the last chunk: copy the grid to the readback buffer)
                    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = giProbeBuf_;
                    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                    cl->ResourceBarrier(1, &b);
                    cl->CopyBufferRegion(rb, 0, giProbeBuf_, 0, (UINT64)n * 96);
                    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
                    cl->ResourceBarrier(1, &b);
                }
            }, "GI: probe grid");
        }
        if (ok) {
            void* p = nullptr; D3D12_RANGE r{0, (SIZE_T)n * 96}, none{0, 0};
            if (SUCCEEDED(rb->Map(0, &r, &p))) { out.resize((size_t)n * 24); std::memcpy(out.data(), p, (size_t)n * 96); rb->Unmap(0, &none); }
            else ok = false;
        }
        rel(rb);
        rel(giProbeBuf_); rel(giProbeCells_); giProbeN_ = 0;   // (the grid lives on the CPU; the GPU copies are not kept)
        writeDescriptors();
        return ok && !out.empty();
    }
    bool giReady() const { return psoGi_ && giTexels_ && !gi_.empty(); }
    // load time: whole passes over every texel, in chunks of one submission each (GL owns the pages around it)
    bool giRunNow(const RtGiJob& j0, int passes, UINT chunk) {
        if (!giReady() || !tlas_) return false;
        using clk = std::chrono::steady_clock;
        const clk::time_point t0 = clk::now();
        if (!giStatUp_) { const uint32_t z[16] = {}; giStatUp_ = uploadBuffer(dev_, z, sizeof z); }
        if (!giStatRb_) giStatRb_ = makeBuffer(dev_, 64, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        auto statCopy = [&](ID3D12GraphicsCommandList* cl, bool toRb) {   // (bytes 192 .. 255 of the stats buffer)
            D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = stats_;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            b.Transition.StateAfter = toRb ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_COPY_DEST;
            cl->ResourceBarrier(1, &b);
            if (toRb) cl->CopyBufferRegion(giStatRb_, 0, stats_, 192, 64); else cl->CopyBufferRegion(stats_, 192, giStatUp_, 0, 64);
            std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
            cl->ResourceBarrier(1, &b);
        };
        if (giStatUp_ && giStatRb_) oneShot([&](ID3D12GraphicsCommandList* cl) { statCopy(cl, false); }, "GI: stats reset");
        for (int pass = 0; pass < passes; ++pass)
            for (UINT first = 0; first < giTexelN_; first += chunk) {
                RtGiJob j = j0; j.first = first; j.count = std::min(chunk, giTexelN_ - first); j.frame = (unsigned)(pass * 7919u + first);
                j.dilate = first + chunk >= giTexelN_;
                if (!oneShot([&](ID3D12GraphicsCommandList* cl) { giRecord(cl, j); }, "GI: load-time pass")) return false;
            }
        LOG_INFO("RT GI: %d load-time passes over %u texels in %.0f ms", passes, giTexelN_,
                 std::chrono::duration<double, std::milli>(clk::now() - t0).count());
        if (giStatUp_ && giStatRb_ && oneShot([&](ID3D12GraphicsCommandList* cl) { statCopy(cl, true); }, "GI: stats read")) {
            void* p = nullptr; D3D12_RANGE r{0, 64}, none{0, 0};
            if (SUCCEEDED(giStatRb_->Map(0, &r, &p))) {
                uint32_t s[16]; std::memcpy(s, p, 64); giStatRb_->Unmap(0, &none);
                const double rays = std::max(1.0, (double)s[0]), st = std::max(1.0, (double)s[1]);
                LOG_INFO("RT GI bounce rays: %u, %.1f %% hit static geometry (of those: GI page %.1f %%, bake %.1f %%, none %.1f %%);"
                         " hit albedo mean %.3f, hit radiance mean %.4f (before the gain)", s[0], 100.0 * s[1] / rays, 100.0 * s[2] / st,
                         100.0 * s[3] / st, 100.0 * s[4] / st, ((double)s[7] * 4294967296.0 + s[5]) / 1000.0 / st,
                         ((double)s[8] * 4294967296.0 + s[6]) / 10000.0 / st);
            }
        }
        return true;
    }
    void releaseHit() {
        abandonMaterialPso();
        rel(hInst_); rel(hIdx_); rel(hUv1_); rel(hTriSub_); rel(hSubs_); rel(hVtx_); rel(hMatTex_); rel(psoReflMat_);
        for (auto* t : hTex2D_) rel(t);
        for (auto* t : hTexCube_) rel(t);
        hTex2D_.clear(); hTexCube_.clear(); hMatTexN_ = 0;
        for (auto& l : hLm_) rel(l.first);
        hLm_.clear(); hBlasN_ = hTris_ = hVerts_ = hSubN_ = 0; hitBytes_ = 0;
    }
    void releaseMasked() {
        for (auto* b : mBlas_) b->Release();
        mBlas_.clear();
        rel(mUv_); rel(mIdx_); rel(mTriSub_); rel(mSubs_); rel(mInst_);
        mVerts_ = mTris_ = mSubN_ = mSetN_ = 0;
        instances_.erase(std::remove_if(instances_.begin(), instances_.end(),
                                        [](const D3D12_RAYTRACING_INSTANCE_DESC& d) { return d.InstanceMask == 0x8; }), instances_.end());
    }
    void clearStaticWorld() {
        maxSubmitMs_ = 0.0; maxSubmitWhat_ = "";
        if ((tlas_ || !blas_.empty() || rtDynamicHasWork()) && !waitIdle()) {   // (still in flight: dropped, not freed)
            LOG_WARN("RT bridge: the GPU did not go idle at the map change: the RT scene is dropped without release (leaked: "
                     "%zu BLAS %.1f MB, TLAS %s, reflection hit data %.1f MB, R4 GI pages + buffers %.1f MB)", blas_.size(), (double)totalBlas_ / 1048576.0,
                     tlas_ ? "yes" : "no", (double)hitBytes_ / 1048576.0, (double)giBytes_ / 1048576.0);
            // (R4: dropped like the rest - still possibly in flight, so not released; the next giSetup starts clean)
            gi_.clear(); giSig_.clear(); giTexels_ = giDilate_ = giData_ = nullptr; giTexelN_ = giDilateN_ = giDataN_ = 0; giBytes_ = 0; giPending_ = false;
            tlas_ = nullptr; blas_.clear(); instances_.clear(); totalBlas_ = 0; for (bool& v : rbValid_) v = false;
            for (auto& d : dyn_) d = DynTlas{};
            rtDynamicAbandon();
            return;
        }
        const double v0 = vramMB();
        rtDynamicClear();                                // (M7 RT characters: Systems' D3D12RtDynamic)
        const double v1 = vramMB();
        giRelease();
        const double v2 = vramMB();
        releaseMasked();
        releaseHit();
        const double v3 = vramMB();
        for (auto& d : dyn_) { rel(d.tlas); rel(d.scratch); rel(d.inst); d = DynTlas{}; }
        rel(tlas_);
        for (auto* b : blas_) b->Release();
        blas_.clear(); instances_.clear(); totalBlas_ = 0;
        for (bool& v : rbValid_) v = false;
        if (v0 > 0) LOG_INFO("RT bridge VRAM (device, MB) at the map change: %.0f -> characters %.0f -> R4 GI %.0f -> masked + hit data %.0f -> BLAS / TLAS %.0f",
                             v0, v1, v2, v3, vramMB());
    }
    double vramMB() {                                    // (leak diagnostics) this process's local video memory, MB (0: unknown)
        if (!adapter_) return 0.0;
        DXGI_QUERY_VIDEO_MEMORY_INFO mi{};
        return SUCCEEDED(adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &mi)) ? (double)mi.CurrentUsage / 1048576.0 : 0.0;
    }

    bool roundTrip(const RtFrame* fr) {
        using clk = std::chrono::steady_clock;
        const clk::time_point t0 = clk::now();
        const UINT64 n = ++value_;
        GLuint64_ v = n; semParam_(sem_, kFenceValue, &v);
        std::vector<GLuint> tx = {depth_.tex, mask_.tex, ao_.tex, dnOut_.tex, wdepth_.tex, refl_.tex, soft_.tex, lvis_.tex, vol_.tex};
        std::vector<GLenum> ly = {kLayoutShaderRead, kLayoutGeneral, kLayoutGeneral, kLayoutGeneral, kLayoutShaderRead, kLayoutGeneral, kLayoutGeneral, kLayoutGeneral, kLayoutGeneral};
        if (atlas_.tex) { tx.push_back(atlas_.tex); ly.push_back(kLayoutShaderRead); }
        if (pcol_.tex) { tx.push_back(pcol_.tex); ly.push_back(kLayoutShaderRead); }   // (item 3: GL wrote it)
        for (GLuint g : giSig_) { tx.push_back(g); ly.push_back(kLayoutGeneral); }   // (R4: the GI pages)
        signalSem_(sem_, (GLuint)sigBufs_.size(), sigBufs_.empty() ? nullptr : sigBufs_.data(), (GLuint)tx.size(), tx.data(), ly.data());
        glFlush();
        const clk::time_point t1 = clk::now();
        const int a = (int)(frame_++ & 1);
        if (allocDone_[a] && fence_->GetCompletedValue() < allocDone_[a]) {
            fence_->SetEventOnCompletion(allocDone_[a], event_);
            if (WaitForSingleObject(event_, 1000) != WAIT_OBJECT_0 && fence_->GetCompletedValue() < allocDone_[a]) {
                static bool logged = false;
                if (!logged) { logged = true; LOG_WARN("RT bridge: the D3D12 job of two frames ago is still running after 1 s: this frame's job skipped"); }
                queue_->Wait(fence_, n);                     // (GL still needs its value: signalled after the pending work)
                const UINT64 skip = ++value_;
                queue_->Signal(fence_, skip);
                GLuint64_ wv = skip; semParam_(sem_, kFenceValue, &wv);
                std::vector<GLuint> wt = {mask_.tex, ao_.tex, dnOut_.tex, lvis_.tex};
                for (GLuint g : giSig_) wt.push_back(g);
                std::vector<GLenum> wl(wt.size(), kLayoutShaderRead);
                waitSem_(sem_, (GLuint)sigBufs_.size(), sigBufs_.empty() ? nullptr : sigBufs_.data(), (GLuint)wt.size(), wt.data(), wl.data());
                return false;
            }
        }
        if (rbValid_[a]) readStats(a);
        alloc_[a]->Reset(); list_->Reset(alloc_[a], nullptr);
        jobTlas_ = tlas_;
        if (fr && tlas_ && pso_) dynamicTlas(a);       // characters: their BLAS refits, then a per-slot TLAS with them
        giTimed_[a] = false;
        if (giPending_ && tlas_ && giReady() && tsHeap_) {   // (R4: this frame's slice of the texels, timed: queries 4 / 5 of the slot)
            list_->EndQuery(tsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)a * 6 + 4);
            giRecord(list_, giJob_);
            list_->EndQuery(tsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)a * 6 + 5);
            giTimed_[a] = true;
        }
        giPending_ = false;
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
        std::vector<GLuint> wt = {mask_.tex, ao_.tex, dnOut_.tex, refl_.tex, soft_.tex, lvis_.tex, vol_.tex};
        for (GLuint g : giSig_) wt.push_back(g);
        std::vector<GLenum> wl(wt.size(), kLayoutShaderRead);
        waitSem_(sem_, (GLuint)sigBufs_.size(), sigBufs_.empty() ? nullptr : sigBufs_.data(), (GLuint)wt.size(), wt.data(), wl.data());
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
    // the characters (Systems' D3D12RtDynamic, mask 0x4; death bodies 0x10): their BLAS records, then the static instances + the characters
    // into a per-ring-slot TLAS (grown by half again when short); without characters the static TLAS stays
    struct DynTlas { ID3D12Resource* tlas = nullptr; ID3D12Resource* scratch = nullptr; ID3D12Resource* inst = nullptr;
                     UINT64 tlasBytes = 0, scratchBytes = 0, instBytes = 0; };
    void dynamicTlas(int a) {
        if (!list4_ && FAILED(list_->QueryInterface(__uuidof(ID3D12GraphicsCommandList4), (void**)&list4_))) return;
        rtDynamicRecord(list4_);
        dynInst_ = instances_;
        rtDynamicAppendInstances(dynInst_, 0x4);
        if (dynInst_.size() == instances_.size()) return;
        DynTlas& d = dyn_[a];
        const UINT64 ib = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * dynInst_.size();
        if (ib > d.instBytes) { rel(d.inst); d.instBytes = ib * 2; d.inst = makeBuffer(dev_, d.instBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ); }
        void* p = nullptr;
        if (!d.inst || FAILED(d.inst->Map(0, nullptr, &p))) { d.instBytes = 0; return; }
        std::memcpy(p, dynInst_.data(), ib); d.inst->Unmap(0, nullptr);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd{};
        bd.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        bd.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        bd.Inputs.NumDescs = (UINT)dynInst_.size(); bd.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        bd.Inputs.InstanceDescs = d.inst->GetGPUVirtualAddress();
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pi{};
        dev5_->GetRaytracingAccelerationStructurePrebuildInfo(&bd.Inputs, &pi);
        if (pi.ResultDataMaxSizeInBytes > d.tlasBytes) {
            rel(d.tlas); d.tlasBytes = pi.ResultDataMaxSizeInBytes * 3 / 2;
            d.tlas = makeBuffer(dev_, d.tlasBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        }
        if (pi.ScratchDataSizeInBytes > d.scratchBytes) {
            rel(d.scratch); d.scratchBytes = pi.ScratchDataSizeInBytes * 3 / 2;
            d.scratch = makeBuffer(dev_, d.scratchBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        }
        if (!d.tlas || !d.scratch) { rel(d.tlas); rel(d.scratch); d.tlasBytes = d.scratchBytes = 0; return; }
        bd.DestAccelerationStructureData = d.tlas->GetGPUVirtualAddress(); bd.ScratchAccelerationStructureData = d.scratch->GetGPUVirtualAddress();
        list4_->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);
        D3D12_RESOURCE_BARRIER ub{}; ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        list4_->ResourceBarrier(1, &ub);
        jobTlas_ = d.tlas;
    }
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
        rg[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; rg[0].NumDescriptors = kSrvN; rg[0].BaseShaderRegister = 1;
        rg[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; rg[1].NumDescriptors = 4; rg[1].BaseShaderRegister = 0;
        rg[1].OffsetInDescriptorsFromTableStart = kSrvN;
        D3D12_DESCRIPTOR_RANGE lmRange[6]{};            // reflection hits: lightmap pages (space1), material 2D / cube textures (space2 / 3);
        // R4: the GI pages (UAV space1); item 2: the per-light visibility target (UAV u0 space2), the shadow lights (SRV t0 space4)
        const UINT lmN[6] = {(UINT)kMaxLm, (UINT)kMaxTex2D, (UINT)kMaxTexCube, (UINT)kMaxLm, 1u, 1u};
        const UINT lmSpace[6] = {1u, 2u, 3u, 1u, 2u, 4u};
        for (int k = 0; k < 6; ++k) {
            lmRange[k].RangeType = (k == 3 || k == 4) ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV; lmRange[k].NumDescriptors = lmN[k];
            lmRange[k].BaseShaderRegister = 0;
            lmRange[k].RegisterSpace = lmSpace[k]; lmRange[k].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        }
        D3D12_ROOT_PARAMETER rp[5]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[0].Constants.Num32BitValues = 52;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; rp[1].Descriptor.ShaderRegister = 0;
        rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[2].DescriptorTable.NumDescriptorRanges = 2; rp[2].DescriptorTable.pDescriptorRanges = rg;
        rp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; rp[3].Descriptor.ShaderRegister = 4;
        rp[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[4].DescriptorTable.NumDescriptorRanges = 6; rp[4].DescriptorTable.pDescriptorRanges = lmRange;
        D3D12_STATIC_SAMPLER_DESC ss[3]{};               // s0: the opacity atlas (point, clamp); s1: linear, clamp; s2: linear, wrap
        for (int k = 0; k < 3; ++k) {
            ss[k].Filter = k ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
            ss[k].AddressU = ss[k].AddressV = ss[k].AddressW = k == 2 ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            ss[k].MaxLOD = D3D12_FLOAT32_MAX; ss[k].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; ss[k].ShaderRegister = (UINT)k;
        }
        D3D12_ROOT_SIGNATURE_DESC rd{}; rd.NumParameters = 5; rd.pParameters = rp; rd.NumStaticSamplers = 3; rd.pStaticSamplers = ss;
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
        pd.CS.pShaderBytecode = g_rt_ao_filter; pd.CS.BytecodeLength = sizeof g_rt_ao_filter;
        if (FAILED(dev_->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&psoAoF_))) { LOG_WARN("RT bridge: AO filter PSO failed"); return false; }
        pd.CS.pShaderBytecode = g_rt_shadow_soft; pd.CS.BytecodeLength = sizeof g_rt_shadow_soft;
        if (FAILED(dev_->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&psoSoft_))) { LOG_WARN("RT bridge: soft shadow PSO failed"); return false; }
        pd.CS.pShaderBytecode = g_rt_reflect; pd.CS.BytecodeLength = sizeof g_rt_reflect;
        if (FAILED(dev_->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&psoRefl_))) { LOG_WARN("RT bridge: reflection PSO failed"); return false; }
        pd.CS.pShaderBytecode = g_rt_volumetric; pd.CS.BytecodeLength = sizeof g_rt_volumetric;   // (X3: a failure leaves no volumetrics)
        if (FAILED(dev_->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&psoVol_))) { psoVol_ = nullptr; LOG_WARN("RT bridge: volumetric PSO failed"); }
        pd.CS.pShaderBytecode = g_rt_gi; pd.CS.BytecodeLength = sizeof g_rt_gi;   // (R4: a failure leaves Realistic on the bake)
        if (FAILED(dev_->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&psoGi_))) { psoGi_ = nullptr; LOG_WARN("RT bridge: GI texel PSO failed (the bake stays)"); }
        pd.CS.pShaderBytecode = g_rt_gi_dilate; pd.CS.BytecodeLength = sizeof g_rt_gi_dilate;
        if (FAILED(dev_->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&psoGiDil_))) psoGiDil_ = nullptr;
        pd.CS.pShaderBytecode = g_rt_gi_probe; pd.CS.BytecodeLength = sizeof g_rt_gi_probe;
        if (FAILED(dev_->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&psoGiProbe_))) psoGiProbe_ = nullptr;
        D3D12_QUERY_HEAP_DESC qh{}; qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qh.Count = 12;
        if (FAILED(dev_->CreateQueryHeap(&qh, __uuidof(ID3D12QueryHeap), (void**)&tsHeap_))) return false;
        for (auto& r : tsReadback_) r = makeBuffer(dev_, 48, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        qh.Count = 4;                                    // (X3: its own pair per slot - the cost line of every package 7 item)
        if (FAILED(dev_->CreateQueryHeap(&qh, __uuidof(ID3D12QueryHeap), (void**)&volTsHeap_))) volTsHeap_ = nullptr;
        for (auto& r : volTsReadback_) r = volTsHeap_ ? makeBuffer(dev_, 16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST) : nullptr;
#if defined(WFC_RT_DENOISER)
        dnInit_ = dn_.init(dev_);
        dnViewInit_ = dnInit_;
        if (!dnInit_) LOG_WARN("RT bridge: no shadow denoiser (raw RT shadow)");
        else if (depth_.res) {                           // (the targets came first: size the denoiser now)
            RtDenoiseInputs in; in.depth = depth_.res; in.hitMask = hit_; in.normal = nrm_; in.velocity = vel_; in.output = dnOut_.res;
            dn_.resize(depth_.w, depth_.h, in);
        }
#endif
        D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 4 * kTableN + kMaxLm + kMaxTex2D + kMaxTexCube + kMaxLm + 2 + 3 * kTableN + kTableN;   // (+ X3: table V)   // (+ R4: GI page UAVs, tables E / F / G)
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(dev_->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&heap_))) return false;
        stats_ = makeBuffer(dev_, kStatN * 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        const uint32_t zeros[kStatN] = {};
        zeros_ = uploadBuffer(dev_, zeros, sizeof zeros);
        for (auto& r : readback_) r = makeBuffer(dev_, kStatN * 4, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        // (item 2) the shadow-light ring: made here, before the descriptors are written (made lazily at the first frame, its
        // SRV stayed null until the next map's descriptor rewrite - every light read radius 0 and was skipped)
        if (!lightBuf_) lightBuf_ = makeBuffer(dev_, 56 * 16, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);   // (+ X3 v3: 4 halo lamps + colours)   // (2 slots x (4 shadow + 4 transient + 4 prev VP + 4 transient colours + 4 shadow colours))
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
        auto tex = [&](ID3D12Resource* r, DXGI_FORMAT f) {
            D3D12_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Format = f; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sd.Texture2D.MipLevels = 1;
            dev_->CreateShaderResourceView(r, &sd, h); h.ptr += inc;
        };
        auto sbuf = [&](ID3D12Resource* r, UINT n, UINT stride) {   // structured buffer (null: an empty view)
            D3D12_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Format = DXGI_FORMAT_UNKNOWN; sd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sd.Buffer.NumElements = r ? n : 1; sd.Buffer.StructureByteStride = stride;
            dev_->CreateShaderResourceView(r, &sd, h); h.ptr += inc;
        };
        auto uav = [&](ID3D12Resource* r, DXGI_FORMAT f) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format = f; u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            dev_->CreateUnorderedAccessView(r, nullptr, &u, h); h.ptr += inc;
        };
        for (int table = 0; table < 4; ++table) {        // A (depth check): u0 = mask; B (shadow / AO): the RGBA target; C: reflection; D: soft shadows
            tex(depth_.res, DXGI_FORMAT_R32_FLOAT); tex(wdepth_.res, DXGI_FORMAT_R32_FLOAT);
            tex(atlas_.res, DXGI_FORMAT_R8_UNORM);
            sbuf(mUv_, mVerts_, 8); sbuf(mIdx_, mTris_ * 3, 4); sbuf(mTriSub_, mTris_, 4); sbuf(mSubs_, mSubN_ * 2, 16); sbuf(mInst_, mSetN_, 16);
            sbuf(hInst_, hBlasN_, 16); sbuf(hIdx_, hTris_ * 3, 4); sbuf(hUv1_, hVerts_, 8); sbuf(hTriSub_, hTris_, 4); sbuf(hSubs_, hSubN_ * 8, 16);
            sbuf(hMatTex_, hMatTexN_, 48); sbuf(hVtx_, hVerts_ * 2, 16);
            tex(pcol_.res, DXGI_FORMAT_R16G16B16A16_FLOAT);   // (item 3) t16: the previous frame's HDR scene
            if (table == 3) {                            // D: u0 the soft output, u1 the denoised visibility, u2 the normals
                uav(soft_.res, DXGI_FORMAT_R8_UNORM); uav(dnOut_.res, DXGI_FORMAT_R8_UNORM);
                uav(nrm_, DXGI_FORMAT_R16G16B16A16_FLOAT); uav(vel_, DXGI_FORMAT_R16G16_FLOAT);
                continue;
            }
            if (table == 0) uav(mask_.res, DXGI_FORMAT_R32_FLOAT);
            else uav(table == 1 ? ao_.res : refl_.res, DXGI_FORMAT_R16G16B16A16_FLOAT);
            uav(hit_, DXGI_FORMAT_R32_UINT); uav(nrm_, DXGI_FORMAT_R16G16B16A16_FLOAT); uav(vel_, DXGI_FORMAT_R16G16_FLOAT);
        }
        // (item 3, R4: a page with an RT lightmap twin is read from the GI page - reflections show the ray-traced lighting,
        // not the bake; the same decode scales apply, as in rt_gi's hitRadiance. WFC_RT_REFLGI=0 = the bake copies)
        static const bool reflGi = [] { const char* e = std::getenv("WFC_RT_REFLGI"); return !(e && e[0] == '0'); }();
        for (int k = 0; k < kMaxLm; ++k) {               // the lightmap pages (null past the uploaded ones)
            const int gp = reflGi && k < (int)giHitMap_.size() ? giHitMap_[(size_t)k] : -1;
            if (gp >= 0 && gp < (int)gi_.size() && gi_[(size_t)gp].res && (size_t)gp < giFmt_.size()) { tex(gi_[(size_t)gp].res, giFmt_[(size_t)gp]); continue; }
            tex(k < (int)hLm_.size() ? hLm_[(size_t)k].first : nullptr, k < (int)hLm_.size() && hLm_[(size_t)k].second ?
                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM);
        }
        for (int cube = 0; cube < 2; ++cube) {           // the material textures (full mip chains; null past the uploaded ones)
            const auto& v = cube ? hTexCube_ : hTex2D_;
            for (int k = 0; k < (cube ? kMaxTexCube : kMaxTex2D); ++k) {
                ID3D12Resource* r = k < (int)v.size() ? v[(size_t)k] : nullptr;
                D3D12_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                sd.Format = r ? r->GetDesc().Format : DXGI_FORMAT_R8G8B8A8_UNORM;
                if (cube) { sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; sd.TextureCube.MipLevels = (UINT)-1; }
                else { sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = (UINT)-1; }
                dev_->CreateShaderResourceView(r, &sd, h); h.ptr += inc;
            }
        }
        for (int k = 0; k < kMaxLm; ++k)                 // R4: the GI pages (UAV space1; null past / between them)
            uav(k < (int)gi_.size() ? gi_[(size_t)k].res : nullptr, k < (int)gi_.size() && gi_[(size_t)k].res ? gi_[(size_t)k].res->GetDesc().Format : DXGI_FORMAT_R16G16B16A16_FLOAT);
        uav(lvis_.res, DXGI_FORMAT_R8G8B8A8_UNORM);      // (item 2) u0 space2: the per-light visibility
        sbuf(lightBuf_, 56, 16);                          // (item 2) t0 space4: the shadow lights (2 ring slots x 20 float4)
        for (int table = 0; table < 3; ++table) {        // R4: E (the texel tracer) / F (the gutter) / G (S3 probes): t1 .. t13 as A .. D
            tex(depth_.res, DXGI_FORMAT_R32_FLOAT); tex(wdepth_.res, DXGI_FORMAT_R32_FLOAT);
            tex(atlas_.res, DXGI_FORMAT_R8_UNORM);
            sbuf(mUv_, mVerts_, 8); sbuf(mIdx_, mTris_ * 3, 4); sbuf(mTriSub_, mTris_, 4); sbuf(mSubs_, mSubN_ * 2, 16); sbuf(mInst_, mSetN_, 16);
            sbuf(hInst_, hBlasN_, 16); sbuf(hIdx_, hTris_ * 3, 4); sbuf(hUv1_, hVerts_, 8); sbuf(hTriSub_, hTris_, 4); sbuf(hSubs_, hSubN_ * 8, 16);
            if (table == 0) sbuf(giTexels_, giTexelN_, 32); else if (table == 1) sbuf(giDilate_, giDilateN_, 16); else sbuf(giProbeCells_, giProbeN_, 4);
            sbuf(giData_, giDataN_, 16);
            tex(pcol_.res, DXGI_FORMAT_R16G16B16A16_FLOAT);   // (t16, as A .. D)
            if (table == 2) {                            // G: u0 = the probe buffer (structured float4)
                D3D12_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format = DXGI_FORMAT_UNKNOWN; u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                u.Buffer.NumElements = giProbeBuf_ ? giProbeN_ * 6 : 1; u.Buffer.StructureByteStride = 16;
                dev_->CreateUnorderedAccessView(giProbeBuf_, nullptr, &u, h); h.ptr += inc;
            } else uav(refl_.res, DXGI_FORMAT_R16G16B16A16_FLOAT);
            uav(hit_, DXGI_FORMAT_R32_UINT);
            uav(nrm_, DXGI_FORMAT_R16G16B16A16_FLOAT); uav(vel_, DXGI_FORMAT_R16G16_FLOAT);
        }
        {   // X3 table V: t1 .. t16 as A .. D; u0 the integrated froxel grid (shared), u1 / u2 the injected grid's ping-pong
            tex(depth_.res, DXGI_FORMAT_R32_FLOAT); tex(wdepth_.res, DXGI_FORMAT_R32_FLOAT);
            tex(atlas_.res, DXGI_FORMAT_R8_UNORM);
            sbuf(mUv_, mVerts_, 8); sbuf(mIdx_, mTris_ * 3, 4); sbuf(mTriSub_, mTris_, 4); sbuf(mSubs_, mSubN_ * 2, 16); sbuf(mInst_, mSetN_, 16);
            sbuf(hInst_, hBlasN_, 16); sbuf(hIdx_, hTris_ * 3, 4); sbuf(hUv1_, hVerts_, 8); sbuf(hTriSub_, hTris_, 4); sbuf(hSubs_, hSubN_ * 8, 16);
            sbuf(hMatTex_, hMatTexN_, 48); sbuf(hVtx_, hVerts_ * 2, 16);
            tex(pcol_.res, DXGI_FORMAT_R16G16B16A16_FLOAT);
            uav(vol_.res, DXGI_FORMAT_R16G16B16A16_FLOAT);
            uav(volH_[0], DXGI_FORMAT_R16G16B16A16_FLOAT); uav(volH_[1], DXGI_FORMAT_R16G16B16A16_FLOAT); uav(vel_, DXGI_FORMAT_R16G16_FLOAT);
        }
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
        list_->SetComputeRootShaderResourceView(1, jobTlas_->GetGPUVirtualAddress());
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
        c[24] = fr.aoRadius; c[25] = fr.bias; c[26] = (float)(fr.instanceMask & 0xFF); c[27] = fr.sunConeTan;
        std::memcpy(c + 28, fr.ffxReproj, 64);
        c[44] = fr.znear; c[45] = fr.zfar; c[46] = fr.reflectShade ? 1.0f : 0.0f; c[47] = fr.aoRadiusDyn;
        float cE[52] = {};                               // (item 2) Extra2.z = the shadow light count, .w = its ring offset
        if (lightBuf_) {                                 // (Extra2.z = shadow count + 8 x transient count, .w = the slot's base)
            void* lp = nullptr; D3D12_RANGE none{0, 0};
            if (SUCCEEDED(lightBuf_->Map(0, &none, &lp))) {
                std::memcpy((char*)lp + (size_t)slot * 448, fr.shadowLights, 64);
                std::memcpy((char*)lp + (size_t)slot * 448 + 64, fr.transLights, 64);
                std::memcpy((char*)lp + (size_t)slot * 448 + 128, fr.prevVP, 64);
                std::memcpy((char*)lp + (size_t)slot * 448 + 192, fr.transColors, 64);
                std::memcpy((char*)lp + (size_t)slot * 448 + 256, fr.shadowColors, 64);
                std::memcpy((char*)lp + (size_t)slot * 448 + 320, fr.volLights, 64);
                std::memcpy((char*)lp + (size_t)slot * 448 + 384, fr.volColors, 64);
                lightBuf_->Unmap(0, nullptr);
            }
            cE[50] = (float)(std::min(fr.shadowLightN, 4) + 8 * std::min(fr.transLightN, 4)); cE[51] = (float)(slot * 28);
            cE[48] = fr.transClear;
        }
        list_->SetComputeRootSignature(rs_);
        list_->SetPipelineState(psoSao_);
        list_->SetDescriptorHeaps(1, &heap_);
        std::memcpy(cE, c, sizeof(float) * 48);
        list_->SetComputeRoot32BitConstants(0, 52, cE, 0);
        {   // (item 2) table 4: the light visibility UAV / shadow lights (and the rest of table 4)
            D3D12_GPU_DESCRIPTOR_HANDLE lm4 = heap_->GetGPUDescriptorHandleForHeapStart();
            lm4.ptr += 4ull * kTableN * dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            list_->SetComputeRootDescriptorTable(4, lm4);
        }
        list_->SetComputeRootShaderResourceView(1, jobTlas_->GetGPUVirtualAddress());
        D3D12_GPU_DESCRIPTOR_HANDLE t2 = heap_->GetGPUDescriptorHandleForHeapStart();
        t2.ptr += (UINT64)kTableN * dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        list_->SetComputeRootDescriptorTable(2, t2);
        list_->SetComputeRootUnorderedAccessView(3, stats_->GetGPUVirtualAddress());
        list_->EndQuery(tsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot * 6);
        list_->Dispatch((UINT)(depth_.w + 7) / 8, (UINT)(depth_.h + 3) / 4, 1);     // one 8 x 4 group per denoiser tile
        {   // the AO filter (same root signature and table): raw AO (.y) -> filtered AO (.w)
            D3D12_RESOURCE_BARRIER ub{}; ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; list_->ResourceBarrier(1, &ub);
            list_->SetPipelineState(psoAoF_);
            list_->Dispatch((UINT)(depth_.w + 7) / 8, (UINT)(depth_.h + 7) / 8, 1);
            list_->SetPipelineState(psoSao_);
        }
        list_->EndQuery(tsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot * 6 + 1);
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
        if (fr.softShadow && psoSoft_) {                 // (R2) contact-hardening penumbrae: denoised visibility -> soft (table D)
            D3D12_RESOURCE_BARRIER ub{}; ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; list_->ResourceBarrier(1, &ub);
            list_->SetComputeRootSignature(rs_);
            list_->SetPipelineState(psoSoft_);
            list_->SetDescriptorHeaps(1, &heap_);
            float c3[52] = {};
            std::memcpy(c3, c, sizeof(float) * 48);
            c3[49] = fr.pixelSpread; c3[50] = fr.penumbraTan; c3[51] = fr.softMaxRadius;
            list_->SetComputeRoot32BitConstants(0, 52, c3, 0);
            list_->SetComputeRootShaderResourceView(1, jobTlas_->GetGPUVirtualAddress());
            D3D12_GPU_DESCRIPTOR_HANDLE t4 = heap_->GetGPUDescriptorHandleForHeapStart();
            t4.ptr += 3ull * kTableN * dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            list_->SetComputeRootDescriptorTable(2, t4);
            list_->SetComputeRootUnorderedAccessView(3, stats_->GetGPUVirtualAddress());
            D3D12_GPU_DESCRIPTOR_HANDLE lm = heap_->GetGPUDescriptorHandleForHeapStart();
            lm.ptr += 4ull * kTableN * dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            list_->SetComputeRootDescriptorTable(4, lm);
            list_->Dispatch((UINT)(depth_.w + 7) / 8, (UINT)(depth_.h + 7) / 8, 1);
        }
        if (fr.volumetric && psoVol_ && vol_.res && volH_[0] && volH_[1]) {   // X3: the froxel volumetric light (table V)
            D3D12_RESOURCE_BARRIER ub{}; ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; list_->ResourceBarrier(1, &ub);
            list_->SetComputeRootSignature(rs_);
            list_->SetPipelineState(psoVol_);
            list_->SetDescriptorHeaps(1, &heap_);
            float cv[52] = {};
            std::memcpy(cv, c, sizeof(float) * 28);       // InvViewProj, ToSun, Size, Params
            cv[23] = 0.0f;
            std::memcpy(cv + 28, fr.volSun, 16); std::memcpy(cv + 32, fr.volFog, 16);
            cv[36] = fr.volMisc[0]; cv[37] = 64.0f; cv[38] = (float)volTx_; cv[39] = (float)volTy_;
            cv[40] = 0.0f; cv[41] = (float)volParity_; cv[42] = (volHistValid_ && fr.prevValid) ? 1.0f : 0.0f;
            cv[43] = (float)std::min(fr.transLightN, 4);
            cv[44] = fr.znear; cv[45] = fr.zfar; cv[46] = (float)(slot * 28); cv[47] = fr.volLampK;
            cv[48] = fr.volHistory; cv[49] = 16.0f; cv[50] = (float)std::min(fr.volLightN, 4); cv[51] = fr.volTransK;
            cv[23] = fr.volLampCore;                     // (Size.w: the lamp / transient core radius)
            cv[27] = fr.volCap;                          // (Params.w: the cap on the integrated in-scatter)
            list_->SetComputeRoot32BitConstants(0, 52, cv, 0);
            list_->SetComputeRootShaderResourceView(1, jobTlas_->GetGPUVirtualAddress());
            const UINT inc = dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            D3D12_GPU_DESCRIPTOR_HANDLE tV = heap_->GetGPUDescriptorHandleForHeapStart();
            tV.ptr += (4ull * kTableN + kMaxLm + kMaxTex2D + kMaxTexCube + kMaxLm + 2ull + 3ull * kTableN) * inc;
            list_->SetComputeRootDescriptorTable(2, tV);
            list_->SetComputeRootUnorderedAccessView(3, stats_->GetGPUVirtualAddress());
            D3D12_GPU_DESCRIPTOR_HANDLE lm = heap_->GetGPUDescriptorHandleForHeapStart();
            lm.ptr += 4ull * kTableN * inc;
            list_->SetComputeRootDescriptorTable(4, lm);
            const bool volTimed = volTsHeap_ && volTsReadback_[slot];
            if (volTimed) list_->EndQuery(volTsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot * 2);
            list_->Dispatch((UINT)(volTx_ + 7) / 8, (UINT)(volTy_ + 7) / 8, 64);   // inject (+ temporal)
            list_->ResourceBarrier(1, &ub);
            cv[40] = 1.0f;
            list_->SetComputeRoot32BitConstants(0, 52, cv, 0);
            list_->Dispatch((UINT)(volTx_ + 7) / 8, (UINT)(volTy_ + 7) / 8, 1);    // integrate
            if (volTimed) {
                list_->EndQuery(volTsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot * 2 + 1);
                list_->ResolveQueryData(volTsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot * 2, 2, volTsReadback_[slot], 0);
            }
            volTimed_[slot] = volTimed;
            volParity_ ^= 1; volHistValid_ = true;
        } else { volHistValid_ = false; volTimed_[slot] = false; }
        list_->EndQuery(tsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot * 6 + 2);
        tsShade_[slot] = fr.reflectShade;
        pollMaterialPso();
        if (fr.reflect && psoRefl_) {                    // stage 2 prototype: the reflection rays (table C)
            D3D12_RESOURCE_BARRIER ub{}; ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; list_->ResourceBarrier(1, &ub);
            list_->SetComputeRootSignature(rs_);
            list_->SetPipelineState(psoReflMat_ ? psoReflMat_ : psoRefl_);
            list_->SetDescriptorHeaps(1, &heap_);
            float c2[52] = {};
            std::memcpy(c2, c, sizeof(float) * 48);
            c2[48] = fr.time; c2[49] = fr.pixelSpread;
            const bool reflTlOff = !render::wfc::rtdef::reflTl();   // (v3 row 6 off)
            c2[50] = (fr.prevValid && pcol_.res ? 1.0f : 0.0f) + (reflTlOff ? 0.0f : 2.0f * (float)std::min(fr.transLightN, 4));   // (item 3: the previous frame; v3 row 6: the transient lights)
            c2[51] = (float)(slot * 28 + 8);           // (ring: the slot's prev VP columns; transient lights at -4, their colours at +4)
            list_->SetComputeRoot32BitConstants(0, 52, c2, 0);
            list_->SetComputeRootShaderResourceView(1, jobTlas_->GetGPUVirtualAddress());
            D3D12_GPU_DESCRIPTOR_HANDLE t3 = heap_->GetGPUDescriptorHandleForHeapStart();
            t3.ptr += 2ull * kTableN * dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            list_->SetComputeRootDescriptorTable(2, t3);
            list_->SetComputeRootUnorderedAccessView(3, stats_->GetGPUVirtualAddress());
            D3D12_GPU_DESCRIPTOR_HANDLE lm = heap_->GetGPUDescriptorHandleForHeapStart();
            lm.ptr += 4ull * kTableN * dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            list_->SetComputeRootDescriptorTable(4, lm);
            list_->Dispatch((UINT)(depth_.w + 7) / 8, (UINT)(depth_.h + 7) / 8, 1);
        }
        list_->EndQuery(tsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot * 6 + 3);
        list_->ResolveQueryData(tsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)slot * 6, giTimed_[slot] ? 6 : 4, tsReadback_[slot], 0);
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
        uint32_t rays = 0; uint64_t ts[6] = {};
        void* p = nullptr; D3D12_RANGE r{0, 4}, none{0, 0};
        if (SUCCEEDED(readback_[slot]->Map(0, &r, &p))) { rays = *(const uint32_t*)p; readback_[slot]->Unmap(0, &none); }
        D3D12_RANGE r2{0, 48};
        if (SUCCEEDED(tsReadback_[slot]->Map(0, &r2, &p))) { std::memcpy(ts, p, 48); tsReadback_[slot]->Unmap(0, &none); }
        if (volTimed_[slot] && tsFreq_) {                // (X3: inject + integrate)
            uint64_t vt[2] = {}; D3D12_RANGE r3{0, 16};
            if (SUCCEEDED(volTsReadback_[slot]->Map(0, &r3, &p))) { std::memcpy(vt, p, 16); volTsReadback_[slot]->Unmap(0, &none); }
            if (vt[1] > vt[0]) volMs_.push_back((double)(vt[1] - vt[0]) * 1000.0 / (double)tsFreq_);
            volTimed_[slot] = false;
        }
        if (giTimed_[slot] && tsFreq_ && ts[5] > ts[4]) { const double g = (double)(ts[5] - ts[4]) * 1000.0 / (double)tsFreq_; giSum_ += g; giMax_ = std::max(giMax_, g); ++giN_; }
        const double ms = tsFreq_ && ts[1] > ts[0] ? (double)(ts[1] - ts[0]) * 1000.0 / (double)tsFreq_ : 0.0;
        const double dnMs = tsFreq_ && ts[2] > ts[1] ? (double)(ts[2] - ts[1]) * 1000.0 / (double)tsFreq_ : 0.0;
        const double rfMs = tsFreq_ && ts[3] > ts[2] ? (double)(ts[3] - ts[2]) * 1000.0 / (double)tsFreq_ : 0.0;
        saoMs_.push_back(ms); saoRaySum_ += rays; dnSum_ += dnMs; dnMax_ = std::max(dnMax_, dnMs); rfSum_ += rfMs; rfMax_ = std::max(rfMax_, rfMs);
        rfAb_[tsShade_[slot] ? 1 : 0] += rfMs; rfAbN_[tsShade_[slot] ? 1 : 0] += 1;
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
        if (rfSum_ > 0) LOG_INFO("RT reflections (1 ray / pixel, closest hit): GPU %.3f ms avg, %.3f max (rays counted with shadow / AO above)",
                                 rfSum_ / (double)v.size(), rfMax_);
        if (rfAbN_[0] > 0 && rfAbN_[1] > 0)
            LOG_INFO("RT reflections A / B (alternating 30-frame blocks): hit shading on %.3f ms (%d frames), off %.3f ms (%d frames)",
                     rfAb_[1] / rfAbN_[1], rfAbN_[1], rfAb_[0] / rfAbN_[0], rfAbN_[0]);
        if (!volMs_.empty()) {
            std::vector<double> w = volMs_;
            std::sort(w.begin(), w.end());
            double vs = 0; for (double x : w) vs += x;
            LOG_INFO("RT X3 volumetric light (%zu of the 300 frames, %dx%d tiles x 64 slices, inject + integrate): GPU %.3f ms avg, "
                     "%.3f p50, %.3f p95, %.3f max", w.size(), volTx_, volTy_, vs / (double)w.size(), w[w.size() / 2],
                     w[w.size() * 95 / 100], w.back());
            volMs_.clear();
        }
        if (giN_ > 0) LOG_INFO("RT GI texel tracer (R4, %d of the 300 frames traced a slice): GPU %.3f ms avg per traced frame, %.3f max", giN_, giSum_ / giN_, giMax_);
        giSum_ = giMax_ = 0; giN_ = 0;
        rfAb_[0] = rfAb_[1] = 0; rfAbN_[0] = rfAbN_[1] = 0;
        saoMs_.clear(); saoRaySum_ = 0; dnSum_ = dnMax_ = 0; rfSum_ = rfMax_ = 0;
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
    // process exit (the renderer's destructor, GL context still current): the last frame's D3D12 job finishes first, then
    // the GL side lets go of everything imported from D3D12 (shared textures' memory objects, the fence's semaphore), then
    // the scene. Without it the process ended with D3D12 work in flight on resources whose GL imports went with the
    // context - a teardown the driver must sort out while other processes have work on the same GPU.
public:
    void shutdown() {
        if (!waitIdle()) LOG_WARN("RT bridge: shutdown - the D3D12 queue did not go idle within 5 s");
        clearStaticWorld();
        for (Shared* s : {&depth_, &mask_, &ao_, &dnOut_, &wdepth_, &atlas_, &refl_, &soft_, &lvis_, &pcol_, &vol_}) free(*s);
        for (auto& ps : giPool_) free(ps.first);         // (R4: the pooled GI pages)
        giPool_.clear();
        if (sem_ && deleteSem_) deleteSem_(1, &sem_);
        sem_ = 0;
        glFinish();
        LOG_INFO("RT bridge: shut down (queue idle, GL imports released)");
    }
private:
    void free(Shared& s) {
        if (s.tex) glDeleteTextures(1, &s.tex);
        if (s.mem && deleteMem_) deleteMem_(1, &s.mem);
        if (s.handle) CloseHandle(s.handle);
        rel(s.res);
        s = Shared{};
    }
    // load-time submissions (BLAS / TLAS builds, masked world, reflection hit data): executed and waited for one at a time,
    // each timed (execute -> idle, wall clock: an upper bound of its GPU time); the longest per map is logged, and one over
    // 250 ms warns (Windows resets the driver at ~2 s of one packet's GPU work - a shared or slow GPU nears it sooner)
    double maxSubmitMs_ = 0.0; const char* maxSubmitWhat_ = "";
    struct PsoJob { std::mutex m; ID3D12PipelineState* pso = nullptr; bool done = false, failed = false, abandoned = false; double ms = 0; };
    std::shared_ptr<PsoJob> psoJob_;
    void startMaterialPso(const std::vector<unsigned char>& bytes) {
        auto job = std::make_shared<PsoJob>();
        psoJob_ = job;
        ID3D12Device* dev = dev_; ID3D12RootSignature* rs = rs_;
        dev->AddRef(); rs->AddRef();
        std::thread([job, dev, rs, code = bytes]() {
            const auto t0 = std::chrono::steady_clock::now();
            D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature = rs;
            pd.CS.pShaderBytecode = code.data(); pd.CS.BytecodeLength = code.size();
            ID3D12PipelineState* p = nullptr;
            const bool ok = SUCCEEDED(dev->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&p)) && p;
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            {
                std::lock_guard<std::mutex> lk(job->m);
                job->done = true; job->failed = !ok; job->ms = ms;
                if (job->abandoned) { if (p) p->Release(); }    // (the map changed meanwhile)
                else job->pso = p;
            }
            rs->Release(); dev->Release();
        }).detach();
        LOG_INFO("RT bridge: the map's material reflection PSO compiles in the background (reflections use the proxy shading until it is ready)");
    }
    void pollMaterialPso() {                             // between frames: a finished background PSO goes live
        if (!psoJob_) return;
        std::lock_guard<std::mutex> lk(psoJob_->m);
        if (!psoJob_->done) return;
        if (psoJob_->failed) LOG_WARN("RT bridge: the map's material reflection PSO failed after %.0f ms (the proxy shades)", psoJob_->ms);
        else { psoReflMat_ = psoJob_->pso; psoJob_->pso = nullptr;
               LOG_INFO("RT bridge: the map's material reflection PSO is ready after %.0f ms (material-shaded reflections on)", psoJob_->ms); }
        psoJob_.reset();
    }
    void abandonMaterialPso() {
        if (!psoJob_) return;
        std::lock_guard<std::mutex> lk(psoJob_->m);
        if (psoJob_->done) { if (psoJob_->pso) psoJob_->pso->Release(); psoJob_->pso = nullptr; }
        else psoJob_->abandoned = true;
        psoJob_.reset();
    }
    bool submitTimed(ID3D12GraphicsCommandList* cl, const char* what) {
        const auto t0 = std::chrono::steady_clock::now();
        ID3D12CommandList* lists[] = {cl};
        queue_->ExecuteCommandLists(1, lists);
        const bool idle = waitIdle();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ms > maxSubmitMs_) { maxSubmitMs_ = ms; maxSubmitWhat_ = what; }
        if (ms > 250.0) LOG_WARN("RT bridge: load-time D3D12 submission '%s' took %.0f ms (execute -> idle)", what, ms);
        return idle;
    }
    bool waitIdle() {
        if (!queue_) return true;
        const UINT64 v = ++value_; queue_->Signal(fence_, v);
        if (fence_->GetCompletedValue() < v) { fence_->SetEventOnCompletion(v, event_); WaitForSingleObject(event_, 5000); }
        if (fence_->GetCompletedValue() >= v) return true;
        LOG_WARN("RT bridge: the D3D12 queue did not go idle within 5 s (resources kept, not freed)");
        return false;
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
    typedef void(APIENTRY* PFN_DeleteSemaphoresEXT_)(GLsizei, const GLuint*);
    PFN_DeleteSemaphoresEXT_ deleteSem_ = nullptr;
    int rtTier_ = 0;
    UINT64 value_ = 0, frame_ = 0, allocDone_[2] = {};
    Shared depth_, mask_, ao_, dnOut_, wdepth_, atlas_, refl_, soft_, lvis_, pcol_, vol_;
    ID3D12PipelineState* psoVol_ = nullptr;             // (X3) the volumetric light pass (table V, after E / F / G)
    ID3D12Resource* volH_[2] = {nullptr, nullptr};       // (X3) the injected froxel grid, history ping-pong
    int volTx_ = 0, volTy_ = 0, volParity_ = 0; bool volHistValid_ = false;
    ID3D12Resource* lightBuf_ = nullptr;                 // (item 2) the shadow lights, an upload ring of 2 x 4 float4
    std::vector<ID3D12Resource*> mBlas_;                   // masked world: BLAS per set, lookup buffers
    ID3D12Resource* mUv_ = nullptr; ID3D12Resource* mIdx_ = nullptr; ID3D12Resource* mTriSub_ = nullptr;
    ID3D12Resource* mSubs_ = nullptr; ID3D12Resource* mInst_ = nullptr;
    UINT mVerts_ = 0, mTris_ = 0, mSubN_ = 0, mSetN_ = 0;
    ID3D12Resource* hInst_ = nullptr; ID3D12Resource* hIdx_ = nullptr; ID3D12Resource* hUv1_ = nullptr;   // reflection hits
    ID3D12Resource* hTriSub_ = nullptr; ID3D12Resource* hSubs_ = nullptr;
    std::vector<std::pair<ID3D12Resource*, bool>> hLm_;  // lightmap pages (sRGB?)
    ID3D12Resource* hVtx_ = nullptr; ID3D12Resource* hMatTex_ = nullptr; UINT hMatTexN_ = 0;   // materials
    std::vector<ID3D12Resource*> hTex2D_, hTexCube_;
    UINT hBlasN_ = 0, hTris_ = 0, hVerts_ = 0, hSubN_ = 0; UINT64 hitBytes_ = 0;
    ID3D12Resource* hit_ = nullptr; ID3D12Resource* nrm_ = nullptr; ID3D12Resource* vel_ = nullptr;
    std::vector<GLuint> sigBufs_;
    ID3D12Resource* dbgRb_ = nullptr; int dbgSlot_ = -1;
    double dnSum_ = 0, dnMax_ = 0, rfSum_ = 0, rfMax_ = 0, rfAb_[2] = {0, 0}; int rfAbN_[2] = {0, 0};
    ID3D12QueryHeap* volTsHeap_ = nullptr; ID3D12Resource* volTsReadback_[2] = {}; bool volTimed_[2] = {}; std::vector<double> volMs_;
    bool tsShade_[4] = {true, true, true, true};
#if defined(WFC_RT_DENOISER)
    RtShadowDenoiser dn_; bool dnInit_ = false;
    bool dnViewInit_ = false;                            // (split screen) this view's denoiser initialised
#endif
    struct ViewSet {                                     // (split screen) the inactive view's set (selectView)
        Shared depth, mask, ao, dnOut, wdepth, refl, soft, lvis, pcol, vol;
        ID3D12Resource* volH[2] = {nullptr, nullptr}; int volTx = 0, volTy = 0, volParity = 0; bool volHistValid = false;
        ID3D12Resource* hit = nullptr; ID3D12Resource* nrm = nullptr; ID3D12Resource* vel = nullptr;
#if defined(WFC_RT_DENOISER)
        RtShadowDenoiser dn; bool dnViewInit = false;
#endif
    } other_;
    int view_ = 0;
    std::vector<ID3D12Resource*> blas_; std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances_; ID3D12Resource* tlas_ = nullptr;
    ID3D12Resource* jobTlas_ = nullptr;                   // this job's TLAS: tlas_, or the ring slot's with the characters
    ID3D12GraphicsCommandList4* list4_ = nullptr;
    DynTlas dyn_[2];
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> dynInst_;
    UINT64 totalBlas_ = 0;
    ID3D12RootSignature* rs_ = nullptr; ID3D12PipelineState* pso_ = nullptr; ID3D12DescriptorHeap* heap_ = nullptr;
    ID3D12PipelineState* psoSao_ = nullptr; ID3D12PipelineState* psoAoF_ = nullptr; ID3D12PipelineState* psoRefl_ = nullptr; ID3D12PipelineState* psoReflMat_ = nullptr; ID3D12PipelineState* psoSoft_ = nullptr; ID3D12QueryHeap* tsHeap_ = nullptr; ID3D12Resource* tsReadback_[2] = {};
    IDXGIAdapter3* adapter_ = nullptr; UINT64 tsFreq_ = 0; int rbMode_[2] = {};
    std::vector<double> saoMs_; unsigned long long saoRaySum_ = 0; int saoRays_ = 0; float saoRadius_ = 0.0f;
    ID3D12Resource* stats_ = nullptr; ID3D12Resource* zeros_ = nullptr; ID3D12Resource* readback_[2] = {};
    bool rbValid_[2] = {};
    unsigned long long statSum_[kStatN] = {}; int statFrames_ = 0;
    double sum_[3] = {}, max_[3] = {}; int n_ = 0;

    // ---- R4 helpers ----
    void giRecord(ID3D12GraphicsCommandList* cl, const RtGiJob& j) {
        float c[28] = {};
        std::memcpy(c, j.toSun, 16); std::memcpy(c + 4, j.sunCol, 16); std::memcpy(c + 8, j.sky, 16);
        c[24] = j.lightSize; c[25] = j.bounceSat; c[26] = j.emissiveGain;
        const uint32_t u[4] = {j.first, j.count, j.frame, j.lights};
        std::memcpy(c + 12, u, 16); std::memcpy(c + 16, j.mix, 16);
        const uint32_t o[4] = {j.subOff, j.lightOff, j.pageMapOff, j.mask};
        std::memcpy(c + 20, o, 16);
        const UINT inc = dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_GPU_DESCRIPTOR_HANDLE base = heap_->GetGPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE lm = base; lm.ptr += 4ull * kTableN * inc;
        D3D12_GPU_DESCRIPTOR_HANDLE tE = base; tE.ptr += (4ull * kTableN + kMaxLm + kMaxTex2D + kMaxTexCube + kMaxLm + 2ull) * inc;
        D3D12_GPU_DESCRIPTOR_HANDLE tF = tE; tF.ptr += (UINT64)kTableN * inc;
        cl->SetComputeRootSignature(rs_);
        cl->SetDescriptorHeaps(1, &heap_);
        cl->SetPipelineState(psoGi_);
        cl->SetComputeRoot32BitConstants(0, 28, c, 0);
        cl->SetComputeRootShaderResourceView(1, tlas_->GetGPUVirtualAddress());   // (the static scene: characters do not light the map)
        cl->SetComputeRootDescriptorTable(2, tE);
        cl->SetComputeRootUnorderedAccessView(3, stats_->GetGPUVirtualAddress());
        cl->SetComputeRootDescriptorTable(4, lm);
        if (j.count) cl->Dispatch((j.count + 63) / 64, 1, 1);
        if (j.dilate && psoGiDil_ && giDilateN_) {       // the gutter, twice (ring 2 reads ring 1)
            for (int r = 0; r < 2; ++r) {
                D3D12_RESOURCE_BARRIER ub{}; ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; cl->ResourceBarrier(1, &ub);
                cl->SetPipelineState(psoGiDil_);
                float c2[28]; std::memcpy(c2, c, sizeof c);
                const uint32_t ud[4] = {0u, giDilateN_, j.frame, j.lights}; std::memcpy(c2 + 12, ud, 16);
                cl->SetComputeRoot32BitConstants(0, 28, c2, 0);
                cl->SetComputeRootDescriptorTable(2, tF);
                cl->Dispatch((giDilateN_ + 63) / 64, 1, 1);
            }
        }
        D3D12_RESOURCE_BARRIER ub{}; ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; cl->ResourceBarrier(1, &ub);
    }
    // one GL -> D3D12 -> GL submission outside the frame's round trip (load time): GL signals with the GI pages, D3D12
    // waits, runs the recorded work, signals; GL waits (the pages back in the shader-read layout)
    template <class F> bool oneShot(F record, const char* what) {
        const UINT64 n = ++value_;
        GLuint64_ v = n; semParam_(sem_, kFenceValue, &v);
        std::vector<GLenum> ly(giSig_.size(), kLayoutGeneral);
        signalSem_(sem_, 0, nullptr, (GLuint)giSig_.size(), giSig_.data(), ly.data());
        glFlush();
        ID3D12CommandAllocator* al = nullptr; ID3D12GraphicsCommandList* cl = nullptr;
        if (FAILED(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&al)) ||
            FAILED(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, al, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&cl))) {
            rel(al); queue_->Wait(fence_, n); return false;
        }
        record(cl);
        cl->Close();
        queue_->Wait(fence_, n);
        const bool idle = submitTimed(cl, what);
        rel(cl); rel(al);
        const UINT64 done = ++value_;
        queue_->Signal(fence_, done);
        GLuint64_ w = done; semParam_(sem_, kFenceValue, &w);
        std::vector<GLenum> wl(giSig_.size(), kLayoutShaderRead);
        waitSem_(sem_, 0, nullptr, (GLuint)giSig_.size(), giSig_.data(), wl.data());
        return idle;
    }
    void giRelease() {
        for (size_t k = 0; k < gi_.size(); ++k) {         // (to the pool: see giSetup)
            if (!gi_[k].res) continue;
            giPool_.emplace_back(gi_[k], k < giFmt_.size() ? giFmt_[k] : DXGI_FORMAT_UNKNOWN);
            gi_[k] = Shared{};
        }
        gi_.clear(); giFmt_.clear(); giSig_.clear(); giHitMap_.clear();
        rel(giTexels_); rel(giDilate_); rel(giData_); giTexelN_ = giDilateN_ = giDataN_ = 0; giBytes_ = 0; giPending_ = false;
    }
    std::vector<Shared> gi_; std::vector<GLuint> giSig_;
    std::vector<int> giHitMap_;                          // (item 3) hit lightmap page -> GI page (-1 none)
    std::vector<DXGI_FORMAT> giFmt_; std::vector<std::pair<Shared, DXGI_FORMAT>> giPool_; size_t giReused_ = 0;
    ID3D12Resource* giTexels_ = nullptr; ID3D12Resource* giDilate_ = nullptr; ID3D12Resource* giData_ = nullptr;
    UINT giTexelN_ = 0, giDilateN_ = 0, giDataN_ = 0; UINT64 giBytes_ = 0;
    ID3D12PipelineState* psoGi_ = nullptr; ID3D12PipelineState* psoGiDil_ = nullptr;
    RtGiJob giJob_; bool giPending_ = false;
    ID3D12PipelineState* psoGiProbe_ = nullptr; ID3D12Resource* giProbeBuf_ = nullptr; ID3D12Resource* giProbeCells_ = nullptr; UINT giProbeN_ = 0;
    ID3D12Resource* giStatUp_ = nullptr; ID3D12Resource* giStatRb_ = nullptr;
    bool giTimed_[2] = {false, false}; double giSum_ = 0, giMax_ = 0; int giN_ = 0;
};

Bridge* g_bridge = nullptr;
bool g_failed = false;
// Ray Tracing availability without building the bridge (the Settings row asks at every launch, RT Off must stay idle):
// GL's external-object entry points, then a throwaway D3D12 device on GL's adapter with ray tracing tier >= 1.1, released.
// The bridge itself (device, queue, allocators, the shared fence imported as a GL semaphore) is built on the first enable.
bool probeAvailable() {
    static int known = -1;
    if (known >= 0) return known == 1;
    known = 0;
#if !defined(WFC_RT_SHADERS)
    // a build without the RT shaders (third_party/dxc absent at configure: tools/render/fetch_dxc.py) could build the scene but
    // never trace it - Faithful / Realistic would look Original: report RT unavailable, so the Settings row hides it
    LOG_INFO("RT availability: this build has no RT shaders (third_party/dxc missing at configure): Ray Tracing unavailable");
    return false;
#endif
    auto g = [](const char* n) { return (void*)wglGetProcAddress(n); };
    for (const char* n : {"glCreateMemoryObjectsEXT", "glImportMemoryWin32HandleEXT", "glTexStorageMem2DEXT", "glGenSemaphoresEXT",
                          "glImportSemaphoreWin32HandleEXT", "glSemaphoreParameterui64vEXT", "glWaitSemaphoreEXT", "glSignalSemaphoreEXT"})
        if (!g(n)) { LOG_INFO("RT availability: GL external-object entry point %s missing: Ray Tracing unavailable", n); return false; }
    d3d12SelectAgility();
    HMODULE d3d = LoadLibraryW(L"d3d12.dll");
    auto create = d3d ? (PFN_D3D12_CREATE_DEVICE)(void*)GetProcAddress(d3d, "D3D12CreateDevice") : nullptr;
#if defined(WFC_RT_GL_ADAPTER)
    IDXGIAdapter1* adapter = d3d12AdapterForGL();
#else
    IUnknown* adapter = nullptr;
#endif
    ID3D12Device* dev = nullptr;
    const bool made = create && SUCCEEDED(create(adapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&dev)) && dev;
    if (adapter) adapter->Release();
    int tier = 0;
    if (made) {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
        if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof o5))) tier = (int)o5.RaytracingTier;
        dev->Release();
    }
    known = made && tier >= (int)D3D12_RAYTRACING_TIER_1_1 ? 1 : 0;
    LOG_INFO("RT availability (probe, nothing kept): %s", !made ? "no D3D12 device" : known ? "ray tracing tier 1.1+" : "ray tracing tier below 1.1");
    return known == 1;
}

bool ensure() {
    if (g_failed) return false;
    if (!g_bridge) { g_bridge = new Bridge(); if (!g_bridge->init()) { delete g_bridge; g_bridge = nullptr; g_failed = true; return false; } }
    return true;
}
}  // namespace

bool rtBridgeAvailable() { return g_bridge ? true : !g_failed && probeAvailable(); }
void rtBridgeShutdown() { if (g_bridge) g_bridge->shutdown(); }
bool rtBridgePrepare(int w, int h, RtTargets* t) { return ensure() && g_bridge->prepare(w, h, t); }
bool rtBridgeDenoised() { return g_bridge && g_bridge->denoised(); }
void rtBridgeSelectView(int view) { if (g_bridge) g_bridge->selectView(view); }
bool rtBridgeAddStaticWorld(const RtStaticWorld& world, unsigned mask) { return ensure() && g_bridge->addStaticWorld(world, mask); }
void rtBridgeClearStaticWorld() { if (g_bridge) g_bridge->clearStaticWorld(); }
bool rtBridgeRoundTrip(const RtFrame* frame) { return g_bridge && g_bridge->roundTrip(frame); }
GLuint rtBridgeMaskAtlas(int w, int h) { return ensure() ? g_bridge->maskAtlas(w, h) : 0; }
bool rtBridgeAddHitData(const RtHitData& d) { return g_bridge && g_bridge->addHitData(d); }
bool rtBridgeAddMasked(const RtMaskedWorld& m) { return g_bridge && g_bridge->addMasked(m); }
bool rtBridgeSetStaticMask(unsigned idBase, unsigned mask) { return g_bridge && g_bridge->setStaticMask(idBase, mask); }
bool rtBridgeRemoveStaticSet(unsigned idBase) { return g_bridge && g_bridge->removeStaticSet(idBase); }
void rtBridgeSetSignalBuffers(const GLuint* bufs, int n) { if (g_bridge) g_bridge->setSignalBuffers(bufs, n); }
bool rtBridgeGiSetup(const RtGiSetup& s, std::vector<GLuint>& glTex) { return g_bridge && g_bridge->giSetup(s, glTex); }
double rtBridgeVramMB() { return g_bridge ? g_bridge->vramMB() : 0.0; }
void rtBridgeGiQueue(const RtGiJob& j) { if (g_bridge) g_bridge->giQueue(j); }
bool rtBridgeGiRunNow(const RtGiJob& j, int passes, unsigned chunk) { return g_bridge && g_bridge->giRunNow(j, passes, chunk); }
bool rtBridgeGiProbes(const RtGiJob& j, const float origin[3], float spacing, const int dims[3], int rays, const std::vector<uint32_t>& cells,
                      std::vector<float>& out) {
    return g_bridge && g_bridge->giProbes(j, origin, spacing, dims, rays, cells, out);
}

}  // namespace render
#else
#include "render/d3d12/D3D12RtBridge.h"
namespace render {
bool rtBridgeAvailable() { return false; }
void rtBridgeShutdown() {}
bool rtBridgePrepare(int, int, RtTargets*) { return false; }
bool rtBridgeDenoised() { return false; }
void rtBridgeSelectView(int) {}
bool rtBridgeAddStaticWorld(const RtStaticWorld&, unsigned) { return false; }
void rtBridgeClearStaticWorld() {}
bool rtBridgeRoundTrip(const RtFrame*) { return false; }
GLuint rtBridgeMaskAtlas(int, int) { return 0; }
bool rtBridgeAddHitData(const RtHitData&) { return false; }
bool rtBridgeAddMasked(const RtMaskedWorld&) { return false; }
bool rtBridgeSetStaticMask(unsigned, unsigned) { return false; }
bool rtBridgeRemoveStaticSet(unsigned) { return false; }
void rtBridgeSetSignalBuffers(const GLuint*, int) {}
bool rtBridgeGiSetup(const RtGiSetup&, std::vector<GLuint>& glTex) { glTex.clear(); return false; }
double rtBridgeVramMB() { return 0.0; }
void rtBridgeGiQueue(const RtGiJob&) {}
bool rtBridgeGiRunNow(const RtGiJob&, int, unsigned) { return false; }
bool rtBridgeGiProbes(const RtGiJob&, const float*, float, const int*, int, const std::vector<uint32_t>&, std::vector<float>& out) { out.clear(); return false; }
}  // namespace render
#endif
