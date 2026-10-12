// Clean-room reconstruction — optional FSR 3.1 temporal upscaling (PC ADAPTATION, M5 B; OFF by default: nothing here
// runs unless the renderer asks for it). The GL renderer keeps rendering at the render size; its scene colour (HDR),
// depth, motion vectors and reactive mask are copied into textures shared with D3D12 (GL_EXT_memory_object_win32 /
// GL_EXT_semaphore_win32 over D3D12 handles, as D3D12Presenter), AMD's FFX API upscaler (amd_fidelityfx_loader_dx12.dll,
// SDK v2.3.0 fetched by tools/render/fetch_ffx_sdk.py: FSR 3.1.5) runs on a D3D12 queue, and its display-size output
// is read back by GL (the post pass tonemaps it at the display size). Synchronisation: one shared fence; GL signals
// N (inputs written), D3D12 waits N, dispatches, signals N + 1, GL waits N + 1 on the GPU.
// Orientation: the shared textures keep GL's row order (bottom row first), so FSR sees the frame upside down; it is
// consistent across all inputs, and the motion-vector scale / jitter are given in that space (see fsr3Dispatch).
#if defined(_WIN32) && defined(WFC_FFX_SDK)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <GL/gl.h>

#include "render/d3d12/D3D12Upscaler.h"
#include "core/Log.h"

#include "ffx_api.h"
#include "ffx_api_loader.h"
#include "ffx_upscale.h"
#include "dx12/ffx_api_dx12.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

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
constexpr GLenum kLayoutShaderRead = 0x958F /*GL_LAYOUT_SHADER_READ_ONLY_EXT*/, kLayoutGeneral = 0x958D /*GL_LAYOUT_GENERAL_EXT*/;

template <class T> void rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

struct GlFns {
    PFN_CreateMemoryObjectsEXT createMem = nullptr; PFN_DeleteMemoryObjectsEXT deleteMem = nullptr;
    PFN_ImportMemoryWin32HandleEXT importMem = nullptr; PFN_MemoryObjectParameterivEXT memParam = nullptr;
    PFN_TexStorageMem2DEXT texStorageMem = nullptr; PFN_GenSemaphoresEXT genSem = nullptr;
    PFN_ImportSemaphoreWin32HandleEXT importSem = nullptr; PFN_SemaphoreParameterui64vEXT semParam = nullptr;
    PFN_WaitSemaphoreEXT waitSem = nullptr; PFN_SignalSemaphoreEXT signalSem = nullptr;
    bool load() {
        auto g = [](const char* n) { return (void*)wglGetProcAddress(n); };
        createMem = (PFN_CreateMemoryObjectsEXT)g("glCreateMemoryObjectsEXT"); deleteMem = (PFN_DeleteMemoryObjectsEXT)g("glDeleteMemoryObjectsEXT");
        importMem = (PFN_ImportMemoryWin32HandleEXT)g("glImportMemoryWin32HandleEXT"); memParam = (PFN_MemoryObjectParameterivEXT)g("glMemoryObjectParameterivEXT");
        texStorageMem = (PFN_TexStorageMem2DEXT)g("glTexStorageMem2DEXT"); genSem = (PFN_GenSemaphoresEXT)g("glGenSemaphoresEXT");
        importSem = (PFN_ImportSemaphoreWin32HandleEXT)g("glImportSemaphoreWin32HandleEXT");
        semParam = (PFN_SemaphoreParameterui64vEXT)g("glSemaphoreParameterui64vEXT"); waitSem = (PFN_WaitSemaphoreEXT)g("glWaitSemaphoreEXT");
        signalSem = (PFN_SignalSemaphoreEXT)g("glSignalSemaphoreEXT");
        return createMem && deleteMem && importMem && memParam && texStorageMem && genSem && importSem && semParam && waitSem && signalSem;
    }
};

// one D3D12 texture shared with GL
struct Shared {
    ID3D12Resource* res = nullptr; HANDLE handle = nullptr; GLuint mem = 0, tex = 0;
    int w = 0, h = 0;
};

void ffxMessage(uint32_t type, const wchar_t* msg) {
    char buf[512]; int n = WideCharToMultiByte(CP_UTF8, 0, msg, -1, buf, sizeof buf, nullptr, nullptr);
    if (n <= 0) return;
    if (type == FFX_API_MESSAGE_TYPE_ERROR) LOG_WARN("FSR3: %s", buf); else LOG_INFO("FSR3: %s", buf);
}

class Upscaler {
public:
    bool init() {
        if (!gl_.load()) { LOG_WARN("FSR3: GL external-object entry points missing"); return false; }
        ffxDll_ = LoadLibraryW(L"amd_fidelityfx_loader_dx12.dll");
        if (!ffxDll_) { LOG_WARN("FSR3: amd_fidelityfx_loader_dx12.dll not found next to the exe (tools/render/fetch_ffx_sdk.py)"); return false; }
        ffxLoadFunctions(&ffx_, ffxDll_);
        if (!ffx_.CreateContext || !ffx_.Dispatch || !ffx_.DestroyContext) { LOG_WARN("FSR3: FFX API entry points missing"); return false; }
        {   // diagnostics: a D3D12 runtime already in the process (e.g. loaded by the GL driver) pins the in-box one
            HMODULE core = GetModuleHandleW(L"D3D12Core.dll"); wchar_t path[MAX_PATH] = L"";
            if (core) GetModuleFileNameW(core, path, MAX_PATH);
            char p8[MAX_PATH * 3] = ""; WideCharToMultiByte(CP_UTF8, 0, path, -1, p8, sizeof p8, nullptr, nullptr);
            LOG_INFO("FSR3: before device creation: D3D12Core %s, d3d12.dll %s", core ? p8 : "not loaded",
                     GetModuleHandleW(L"d3d12.dll") ? "loaded" : "not loaded");
        }
        d3d12SelectAgility();                        // (D3D12Agility.cpp: SM 6.6+ runtime when shipped)
        HMODULE d3d = LoadLibraryW(L"d3d12.dll");
        auto create = d3d ? (PFN_D3D12_CREATE_DEVICE)(void*)GetProcAddress(d3d, "D3D12CreateDevice") : nullptr;
        const HRESULT hr = create ? create(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&dev_) : E_FAIL;
        if (FAILED(hr)) { LOG_WARN("FSR3: no D3D12 device (D3D12CreateDevice 0x%08lx)", (unsigned long)hr); return false; }
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(dev_->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&queue_))) return false;
        for (auto& a : alloc_)
            if (FAILED(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&a))) return false;
        if (FAILED(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc_[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&list_))) return false;
        list_->Close();
        if (FAILED(dev_->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), (void**)&fence_))) return false;
        if (FAILED(dev_->CreateSharedHandle(fence_, nullptr, GENERIC_ALL, nullptr, &fenceHandle_))) return false;
        gl_.genSem(1, &sem_);
        gl_.importSem(sem_, kHandleD3D12Fence, fenceHandle_);
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        {   // the shader model this runtime offers (FSR 3.1's shaders need a newer one than the in-box Windows 10 runtime)
            D3D12_FEATURE_DATA_SHADER_MODEL sm{}; sm.HighestShaderModel = (D3D_SHADER_MODEL)0x67;
            while (FAILED(dev_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof sm)) && sm.HighestShaderModel > 0x60)
                sm.HighestShaderModel = (D3D_SHADER_MODEL)(sm.HighestShaderModel - 1);
            D3D12_FEATURE_DATA_D3D12_OPTIONS1 o1{}; dev_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &o1, sizeof o1);
            D3D12_FEATURE_DATA_D3D12_OPTIONS4 o4{}; dev_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &o4, sizeof o4);
            LOG_INFO("FSR3: D3D12 highest shader model %d.%d, wave ops %d, 16-bit ops %d", (int)sm.HighestShaderModel >> 4,
                     (int)sm.HighestShaderModel & 15, (int)o1.WaveOps, (int)o4.Native16BitShaderOpsSupported);
            // FSR 3.1's pipelines need shader model 6.6: below it the upscaler's PSOs are never created and its first
            // dispatch faults (in-box Windows 10 runtime = 6.5; the Agility runtime in D3D12\ beside the exe gives 6.7)
            if (sm.HighestShaderModel < (D3D_SHADER_MODEL)0x66) {
                LOG_WARN("FSR3: shader model 6.6 unavailable (D3D12/D3D12Core.dll beside the exe, from tools/render/fetch_d3d12_agility.py missing?)");
                return false;
            }
        }
        LOG_INFO("FSR3: D3D12 device + FFX API loader ready (FSR upscaler from amd_fidelityfx_upscaler_dx12.dll)");
        return true;
    }

    bool prepare(int inW, int inH, int outW, int outH, Fsr3Targets* t) {
        if (inW <= 0 || inH <= 0 || outW <= 0 || outH <= 0) return false;
        if (inW != inW_ || inH != inH_ || outW != outW_ || outH != outH_) {
            waitIdle();
            destroyContext();
            for (Shared* s : {&color_, &depth_, &vel_, &reactive_, &out_}) freeShared(*s);
            if (!makeShared(color_, inW, inH, DXGI_FORMAT_R16G16B16A16_FLOAT, 0x881A /*GL_RGBA16F*/, false) ||
                !makeShared(depth_, inW, inH, DXGI_FORMAT_R32_FLOAT, 0x822E /*GL_R32F*/, false) ||
                !makeShared(vel_, inW, inH, DXGI_FORMAT_R16G16_FLOAT, 0x822F /*GL_RG16F*/, false) ||
                !makeShared(reactive_, inW, inH, DXGI_FORMAT_R8_UNORM, 0x8229 /*GL_R8*/, false) ||
                !makeShared(out_, outW, outH, DXGI_FORMAT_R16G16B16A16_FLOAT, 0x881A, true)) {
                LOG_WARN("FSR3: shared targets %dx%d -> %dx%d failed", inW, inH, outW, outH); inW_ = 0; return false;
            }
            ffxCreateBackendDX12Desc be{}; be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be.device = dev_;
            ffxCreateContextDescUpscale cd{}; cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE; cd.header.pNext = &be.header;
            cd.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
            if (std::getenv("WFC_FSR3_DEBUG")) cd.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
            cd.maxRenderSize = {(uint32_t)inW, (uint32_t)inH}; cd.maxUpscaleSize = {(uint32_t)outW, (uint32_t)outH};
            cd.fpMessage = &ffxMessage;
            if (ffx_.CreateContext(&ctx_, &cd.header, nullptr) != FFX_API_RETURN_OK) {
                LOG_WARN("FSR3: ffxCreateContext failed (%dx%d -> %dx%d)", inW, inH, outW, outH); ctx_ = nullptr; inW_ = 0; return false;
            }
            inW_ = inW; inH_ = inH; outW_ = outW; outH_ = outH;
            LOG_INFO("FSR3: context %dx%d -> %dx%d (HDR input, auto exposure)", inW, inH, outW, outH);
        }
        t->color = color_.tex; t->depth = depth_.tex; t->velocity = vel_.tex; t->reactive = reactive_.tex; t->output = out_.tex;
        return true;
    }

    bool dispatch(const Fsr3Params& p) {
        if (!ctx_) return false;
        using clk = std::chrono::steady_clock;
        const clk::time_point t0 = clk::now();
        // GL: the inputs are written -> N
        const UINT64 n = ++value_;
        GLuint texs[5] = {color_.tex, depth_.tex, vel_.tex, reactive_.tex, out_.tex};
        GLenum lay[5] = {kLayoutShaderRead, kLayoutShaderRead, kLayoutShaderRead, kLayoutShaderRead, kLayoutGeneral};
        GLuint64_ v = n;
        gl_.semParam(sem_, kFenceValue, &v);
        gl_.signalSem(sem_, 0, nullptr, 5, texs, lay);
        glFlush();
        const clk::time_point t1 = clk::now();
        // D3D12: wait N, upscale, signal N + 1 (the allocator of two frames ago must be finished)
        const int a = (int)(frame_++ & 1);
        if (allocDone_[a] && fence_->GetCompletedValue() < allocDone_[a]) {
            fence_->SetEventOnCompletion(allocDone_[a], event_); WaitForSingleObject(event_, 1000);
        }
        const clk::time_point tW = clk::now();
        alloc_[a]->Reset();
        list_->Reset(alloc_[a], nullptr);
        auto barrier = [&](ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
            D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = r;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; b.Transition.StateBefore = from; b.Transition.StateAfter = to;
            return b;
        };
        const D3D12_RESOURCE_STATES srv = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_BARRIER pre[5] = {barrier(color_.res, D3D12_RESOURCE_STATE_COMMON, srv), barrier(depth_.res, D3D12_RESOURCE_STATE_COMMON, srv),
                                         barrier(vel_.res, D3D12_RESOURCE_STATE_COMMON, srv), barrier(reactive_.res, D3D12_RESOURCE_STATE_COMMON, srv),
                                         barrier(out_.res, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
        list_->ResourceBarrier(5, pre);
        ffxDispatchDescUpscale d{};
        d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        d.commandList = list_;
        d.color = ffxApiGetResourceDX12(color_.res, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        d.depth = ffxApiGetResourceDX12(depth_.res, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        d.motionVectors = ffxApiGetResourceDX12(vel_.res, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        d.reactive = ffxApiGetResourceDX12(reactive_.res, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        d.output = ffxApiGetResourceDX12(out_.res, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, FFX_API_RESOURCE_USAGE_UAV);
        d.jitterOffset = {p.jitterX, p.jitterY};
        d.motionVectorScale = {p.mvScaleX, p.mvScaleY};
        d.renderSize = {(uint32_t)inW_, (uint32_t)inH_};
        d.upscaleSize = {(uint32_t)outW_, (uint32_t)outH_};
        d.enableSharpening = p.sharpness > 0.0f; d.sharpness = p.sharpness;
        d.frameTimeDelta = p.frameTimeMs; d.preExposure = 1.0f; d.reset = p.reset;
        d.cameraNear = p.nearM; d.cameraFar = p.farM; d.cameraFovAngleVertical = p.fovY; d.viewSpaceToMetersFactor = 1.0f;
        const clk::time_point tR = clk::now();
        const ffxReturnCode_t rc = ffx_.Dispatch(&ctx_, &d.header);
        const clk::time_point tD = clk::now();
        D3D12_RESOURCE_BARRIER post[5] = {barrier(color_.res, srv, D3D12_RESOURCE_STATE_COMMON), barrier(depth_.res, srv, D3D12_RESOURCE_STATE_COMMON),
                                          barrier(vel_.res, srv, D3D12_RESOURCE_STATE_COMMON), barrier(reactive_.res, srv, D3D12_RESOURCE_STATE_COMMON),
                                          barrier(out_.res, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON)};
        list_->ResourceBarrier(5, post);
        list_->Close();
        queue_->Wait(fence_, n);
        ID3D12CommandList* lists[] = {list_};
        queue_->ExecuteCommandLists(1, lists);
        const UINT64 done = ++value_;
        queue_->Signal(fence_, done);
        allocDone_[a] = done;
        const clk::time_point t2 = clk::now();
        // GL: wait N + 1 before reading the output (a GPU-side wait)
        GLuint64_ w = done;
        gl_.semParam(sem_, kFenceValue, &w);
        GLuint outTex = out_.tex; GLenum outLay = kLayoutShaderRead;
        gl_.waitSem(sem_, 0, nullptr, 1, &outTex, &outLay);
        {   // main-thread cost per phase (avg / max over 300 dispatches; the copies before this: Pipeline::fsr3Run)
            const clk::time_point t3 = clk::now();
            auto ms = [](clk::time_point a_, clk::time_point b_) { return std::chrono::duration<double, std::milli>(b_ - a_).count(); };
            const double ph[6] = {ms(t0, t1), ms(t1, tW), ms(tW, tR), ms(tR, tD), ms(tD, t2), ms(t2, t3)};
            for (int k = 0; k < 6; ++k) { phSum_[k] += ph[k]; phMax_[k] = std::max(phMax_[k], ph[k]); }
            if (++phN_ == 300) {
                LOG_INFO("FSR3 phases (avg / max ms over 300): GL signal + flush %.3f / %.2f, allocator wait %.3f / %.2f, "
                         "barriers %.3f / %.2f, ffxDispatch %.3f / %.2f, close + execute + signal %.3f / %.2f, GL wait %.3f / %.2f",
                         phSum_[0] / 300, phMax_[0], phSum_[1] / 300, phMax_[1], phSum_[2] / 300, phMax_[2], phSum_[3] / 300, phMax_[3],
                         phSum_[4] / 300, phMax_[4], phSum_[5] / 300, phMax_[5]);
                for (int k = 0; k < 6; ++k) phSum_[k] = phMax_[k] = 0;
                phN_ = 0;
            }
        }
        if (rc != FFX_API_RETURN_OK) {
            if (!dispatchFailLogged_) { LOG_WARN("FSR3: ffxDispatch returned %u", (unsigned)rc); dispatchFailLogged_ = true; }
            return false;
        }
        return true;
    }

private:
    bool makeShared(Shared& s, int w, int h, DXGI_FORMAT fmt, GLenum glFmt, bool uav) {
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = (UINT64)w; rd.Height = (UINT)h;
        rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.Format = fmt; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                   (uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);
        if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                 __uuidof(ID3D12Resource), (void**)&s.res))) return false;
        if (FAILED(dev_->CreateSharedHandle(s.res, nullptr, GENERIC_ALL, nullptr, &s.handle))) return false;
        const D3D12_RESOURCE_ALLOCATION_INFO ai = dev_->GetResourceAllocationInfo(0, 1, &rd);
        gl_.createMem(1, &s.mem);
        const GLint ded = 1; gl_.memParam(s.mem, kDedicated, &ded);
        gl_.importMem(s.mem, ai.SizeInBytes, kHandleD3D12Resource, s.handle);
        glGenTextures(1, &s.tex);
        glBindTexture(GL_TEXTURE_2D, s.tex);
        gl_.texStorageMem(GL_TEXTURE_2D, 1, glFmt, w, h, s.mem, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F /*GL_CLAMP_TO_EDGE*/);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
        glBindTexture(GL_TEXTURE_2D, 0);
        s.w = w; s.h = h;
        return glGetError() == GL_NO_ERROR;
    }
    void freeShared(Shared& s) {
        if (s.tex) glDeleteTextures(1, &s.tex);
        if (s.mem) gl_.deleteMem(1, &s.mem);
        if (s.handle) CloseHandle(s.handle);
        rel(s.res);
        s = Shared{};
    }
    void destroyContext() { if (ctx_) { ffx_.DestroyContext(&ctx_, nullptr); ctx_ = nullptr; } }
    void waitIdle() {
        if (!queue_ || !fence_) return;
        const UINT64 v = ++value_;
        queue_->Signal(fence_, v);
        if (fence_->GetCompletedValue() < v) { fence_->SetEventOnCompletion(v, event_); WaitForSingleObject(event_, 2000); }
    }

    GlFns gl_;
    HMODULE ffxDll_ = nullptr;
    ffxFunctions ffx_{};
    ffxContext ctx_ = nullptr;
    ID3D12Device* dev_ = nullptr; ID3D12CommandQueue* queue_ = nullptr;
    ID3D12CommandAllocator* alloc_[2] = {}; ID3D12GraphicsCommandList* list_ = nullptr;
    ID3D12Fence* fence_ = nullptr; HANDLE fenceHandle_ = nullptr, event_ = nullptr;
    GLuint sem_ = 0;
    UINT64 value_ = 0, frame_ = 0, allocDone_[2] = {};
    Shared color_, depth_, vel_, reactive_, out_;
    int inW_ = 0, inH_ = 0, outW_ = 0, outH_ = 0;
    bool dispatchFailLogged_ = false;
    double phSum_[6] = {}, phMax_[6] = {}; int phN_ = 0;
};

Upscaler* g_up = nullptr;
bool g_failed = false;
}  // namespace

bool fsr3Prepare(int inW, int inH, int outW, int outH, Fsr3Targets* t) {
    if (g_failed) return false;
    if (!g_up) {
        g_up = new Upscaler();
        if (!g_up->init()) { delete g_up; g_up = nullptr; g_failed = true; return false; }
    }
    return g_up->prepare(inW, inH, outW, outH, t);
}

bool fsr3Dispatch(const Fsr3Params& p) { return g_up && g_up->dispatch(p); }

}  // namespace render
#else
#include "render/d3d12/D3D12Upscaler.h"
namespace render {
bool fsr3Prepare(int, int, int, int, Fsr3Targets*) { return false; }
bool fsr3Dispatch(const Fsr3Params&) { return false; }
}  // namespace render
#endif
