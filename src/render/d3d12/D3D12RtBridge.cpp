// Clean-room reconstruction — M7 stage 0 groundwork: the mid-frame GL -> D3D12 -> GL round trip (PC ADAPTATION; OFF by
// default; render/d3d12/D3D12RtBridge.h). Its own D3D12 device / queue (Agility selected at start-up when shipped), two
// textures shared with GL (depth in, mask out: R32F, render size) and one shared fence: GL signals N after writing the
// depth, D3D12 waits N, runs the stand-in job (copy depth -> mask; the RayQuery passes replace it), signals N + 1, GL
// waits N + 1 before the shading that will sample the mask. Main-thread phases are logged per 300 round trips.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <GL/gl.h>

#include "render/d3d12/D3D12RtBridge.h"
#include "core/Log.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>

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
template <class T> void rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

struct Shared { ID3D12Resource* res = nullptr; HANDLE handle = nullptr; GLuint mem = 0, tex = 0; int w = 0, h = 0; };

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
        if (!create || FAILED(create(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&dev_))) { LOG_WARN("RT bridge: no D3D12 device"); return false; }
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
        dev_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof o5);
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(dev_->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&queue_))) return false;
        for (auto& a : alloc_)
            if (FAILED(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&a))) return false;
        if (FAILED(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc_[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&list_))) return false;
        list_->Close();
        if (FAILED(dev_->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), (void**)&fence_))) return false;
        if (FAILED(dev_->CreateSharedHandle(fence_, nullptr, GENERIC_ALL, nullptr, &fenceHandle_))) return false;
        genSem_(1, &sem_);
        importSem_(sem_, kHandleD3D12Fence, fenceHandle_);
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        LOG_INFO("RT bridge: D3D12 device ready (raytracing tier %d.%d; stand-in job: copy)", (int)o5.RaytracingTier / 10, (int)o5.RaytracingTier % 10);
        return true;
    }
    bool prepare(int w, int h, RtTargets* t) {
        if (w != depth_.w || h != depth_.h) {
            waitIdle();
            free(depth_); free(mask_);
            if (!make(depth_, w, h) || !make(mask_, w, h)) { LOG_WARN("RT bridge: shared targets failed"); depth_.w = 0; return false; }
        }
        t->depth = depth_.tex; t->mask = mask_.tex;
        return true;
    }
    bool roundTrip() {
        using clk = std::chrono::steady_clock;
        const clk::time_point t0 = clk::now();
        const UINT64 n = ++value_;
        GLuint64_ v = n; semParam_(sem_, kFenceValue, &v);
        GLuint tx[2] = {depth_.tex, mask_.tex}; GLenum ly[2] = {kLayoutShaderRead, kLayoutGeneral};
        signalSem_(sem_, 0, nullptr, 2, tx, ly);
        glFlush();
        const clk::time_point t1 = clk::now();
        const int a = (int)(frame_++ & 1);
        if (allocDone_[a] && fence_->GetCompletedValue() < allocDone_[a]) { fence_->SetEventOnCompletion(allocDone_[a], event_); WaitForSingleObject(event_, 1000); }
        alloc_[a]->Reset(); list_->Reset(alloc_[a], nullptr);
        D3D12_RESOURCE_BARRIER b[2]{};
        for (auto& x : b) { x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; }
        b[0].Transition.pResource = depth_.res; b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b[1].Transition.pResource = mask_.res; b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        list_->ResourceBarrier(2, b);
        list_->CopyResource(mask_.res, depth_.res);          // the stand-in job (RT shadows / AO will write the mask)
        for (auto& x : b) std::swap(x.Transition.StateBefore, x.Transition.StateAfter);
        list_->ResourceBarrier(2, b);
        list_->Close();
        queue_->Wait(fence_, n);
        ID3D12CommandList* lists[] = {list_};
        queue_->ExecuteCommandLists(1, lists);
        const UINT64 done = ++value_;
        queue_->Signal(fence_, done);
        allocDone_[a] = done;
        const clk::time_point t2 = clk::now();
        GLuint64_ w = done; semParam_(sem_, kFenceValue, &w);
        GLuint mt = mask_.tex; GLenum ml = kLayoutShaderRead;
        waitSem_(sem_, 0, nullptr, 1, &mt, &ml);
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
    bool make(Shared& s, int w, int h) {
        while (glGetError() != GL_NO_ERROR) {}
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = (UINT64)w; rd.Height = (UINT)h;
        rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.Format = DXGI_FORMAT_R32_FLOAT; rd.SampleDesc.Count = 1;
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
        texStorageMem_(GL_TEXTURE_2D, 1, 0x822E /*GL_R32F*/, w, h, s.mem, 0);
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
        if (fence_->GetCompletedValue() < v) { fence_->SetEventOnCompletion(v, event_); WaitForSingleObject(event_, 2000); }
    }
    PFN_CreateMemoryObjectsEXT createMem_ = nullptr; PFN_DeleteMemoryObjectsEXT deleteMem_ = nullptr;
    PFN_ImportMemoryWin32HandleEXT importMem_ = nullptr; PFN_MemoryObjectParameterivEXT memParam_ = nullptr;
    PFN_TexStorageMem2DEXT texStorageMem_ = nullptr; PFN_GenSemaphoresEXT genSem_ = nullptr;
    PFN_ImportSemaphoreWin32HandleEXT importSem_ = nullptr; PFN_SemaphoreParameterui64vEXT semParam_ = nullptr;
    PFN_WaitSemaphoreEXT waitSem_ = nullptr; PFN_SignalSemaphoreEXT signalSem_ = nullptr;
    ID3D12Device* dev_ = nullptr; ID3D12CommandQueue* queue_ = nullptr;
    ID3D12CommandAllocator* alloc_[2] = {}; ID3D12GraphicsCommandList* list_ = nullptr;
    ID3D12Fence* fence_ = nullptr; HANDLE fenceHandle_ = nullptr, event_ = nullptr;
    GLuint sem_ = 0;
    UINT64 value_ = 0, frame_ = 0, allocDone_[2] = {};
    Shared depth_, mask_;
    double sum_[3] = {}, max_[3] = {}; int n_ = 0;
};

Bridge* g_bridge = nullptr;
bool g_failed = false;
}  // namespace

bool rtBridgePrepare(int w, int h, RtTargets* t) {
    if (g_failed) return false;
    if (!g_bridge) { g_bridge = new Bridge(); if (!g_bridge->init()) { delete g_bridge; g_bridge = nullptr; g_failed = true; return false; } }
    return g_bridge->prepare(w, h, t);
}
bool rtBridgeRoundTrip() { return g_bridge && g_bridge->roundTrip(); }

}  // namespace render
#else
#include "render/d3d12/D3D12RtBridge.h"
namespace render {
bool rtBridgePrepare(int, int, RtTargets*) { return false; }
bool rtBridgeRoundTrip() { return false; }
}  // namespace render
#endif
