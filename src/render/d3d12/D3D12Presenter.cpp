// Clean-room reconstruction — optional D3D12 presentation (PC ADAPTATION, A3a; OFF by default: the GL window swap
// stays the path). The GL renderer keeps rendering; at present time its finished back buffer is copied into a
// texture shared with D3D12 (GL_EXT_memory_object_win32 / GL_EXT_semaphore_win32 with D3D12 handles, verified by
// D3D12InteropProbe), and D3D12 copies it into a flip-model DXGI swapchain and presents. This is the base the
// upscalers (FSR 3.1 / DLSS via Streamline) and frame generation present through. WFC_D3D12PRESENT=1 enables it;
// WFC_D3D12_VSYNC=1 presents with sync interval 1 (else immediate, tearing allowed when supported).
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_5.h>
#include <d3d11.h>
#include <GL/gl.h>

#include "platform/PresentHook.h"
#include "render/d3d12/D3D12FrameGen.h"
#include "core/Log.h"
#if defined(WFC_FFX_SDK)
#include "ffx_api.h"
#include "ffx_api_loader.h"
#include "ffx_framegeneration.h"
#include "dx12/ffx_api_dx12.h"
#include "dx12/ffx_api_framegeneration_dx12.h"
#endif

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace glx { void gpuPrePresent(); void textureCacheInvalidate(); }   // GLExt.cpp (WFC_GPUSTAMPS stamp; the bind cache)

namespace render {
void d3d12SelectAgility();   // D3D12Agility.cpp
static bool g_fgAtStartup = false;                // setFrameGenerationAtStartup (the profile's Frame Generation row)
void setFrameGenerationAtStartup(bool on) { g_fgAtStartup = on; }
// the FSR 3 FG runtime beside the exe: the FFX loader + frame-generation DLLs and the Agility D3D12 runtime
bool frameGenerationRuntimeShipped() {
    static const bool shipped = [] {
#if !defined(WFC_FFX_SDK)
        return false;                                    // (built without the FFX SDK: FG can never start)
#endif
        char exe[MAX_PATH * 2] = "";
        GetModuleFileNameA(nullptr, exe, sizeof exe);
        std::string dir(exe);
        dir = dir.substr(0, dir.find_last_of("\\/") + 1);
        for (const char* f : {"amd_fidelityfx_loader_dx12.dll", "amd_fidelityfx_framegeneration_dx12.dll", "D3D12\\D3D12Core.dll"})
            if (GetFileAttributesA((dir + f).c_str()) == INVALID_FILE_ATTRIBUTES) return false;
        return true;
    }();
    return shipped;
}
namespace {
typedef uint64_t GLuint64_;
typedef void(APIENTRY* PFN_CreateMemoryObjectsEXT)(GLsizei, GLuint*);
typedef void(APIENTRY* PFN_DeleteMemoryObjectsEXT)(GLsizei, const GLuint*);
typedef void(APIENTRY* PFN_ImportMemoryWin32HandleEXT)(GLuint, GLuint64_, GLenum, void*);
typedef void(APIENTRY* PFN_MemoryObjectParameterivEXT)(GLuint, GLenum, const GLint*);
typedef void(APIENTRY* PFN_TexStorageMem2DEXT)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, GLuint64_);
typedef void(APIENTRY* PFN_GenSemaphoresEXT)(GLsizei, GLuint*);
typedef void(APIENTRY* PFN_DeleteSemaphoresEXT)(GLsizei, const GLuint*);
typedef void(APIENTRY* PFN_ImportSemaphoreWin32HandleEXT)(GLuint, GLenum, void*);
typedef void(APIENTRY* PFN_SemaphoreParameterui64vEXT)(GLuint, GLenum, const GLuint64_*);
typedef void(APIENTRY* PFN_WaitSemaphoreEXT)(GLuint, GLuint, const GLuint*, GLuint, const GLuint*, const GLenum*);
typedef void(APIENTRY* PFN_SignalSemaphoreEXT)(GLuint, GLuint, const GLuint*, GLuint, const GLuint*, const GLenum*);
typedef void(APIENTRY* PFN_GenFramebuffers)(GLsizei, GLuint*);
typedef void(APIENTRY* PFN_DeleteFramebuffers)(GLsizei, const GLuint*);
typedef void(APIENTRY* PFN_BindFramebuffer)(GLenum, GLuint);
typedef void(APIENTRY* PFN_FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void(APIENTRY* PFN_BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef void* GLsync_;
typedef GLsync_(APIENTRY* PFN_FenceSync)(GLenum, GLbitfield);
typedef void(APIENTRY* PFN_WaitSync)(GLsync_, GLbitfield, GLuint64_);
typedef void(APIENTRY* PFN_DeleteSync)(GLsync_);
typedef GLenum(APIENTRY* PFN_ClientWaitSync)(GLsync_, GLbitfield, GLuint64_);
typedef HGLRC(WINAPI* PFN_CreateContextAttribsARB)(HDC, HGLRC, const int*);

constexpr GLenum kHandleD3D12Resource = 0x958A, kHandleD3D12Fence = 0x9594, kDedicated = 0x9581, kFenceValue = 0x9595;
constexpr GLenum kLayoutTransferDst = 0x9593 /*GL_LAYOUT_TRANSFER_DST_EXT*/, kLayoutTransferSrc = 0x9592;
constexpr GLenum kLayoutShaderRead = 0x958F /*GL_LAYOUT_SHADER_READ_ONLY_EXT*/;
constexpr UINT kMaxBuffers = 3;               // swapchain buffers: buffers_ (WFC_D3D12_BUFFERS, default 2)
constexpr UINT kRing = 3;                        // shared textures: GL writes one while D3D12 reads another

template <class T> void rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

struct GlFns {
    PFN_CreateMemoryObjectsEXT createMem = nullptr; PFN_DeleteMemoryObjectsEXT deleteMem = nullptr;
    PFN_ImportMemoryWin32HandleEXT importMem = nullptr; PFN_MemoryObjectParameterivEXT memParam = nullptr;
    PFN_TexStorageMem2DEXT texStorageMem = nullptr; PFN_GenSemaphoresEXT genSem = nullptr; PFN_DeleteSemaphoresEXT deleteSem = nullptr;
    PFN_ImportSemaphoreWin32HandleEXT importSem = nullptr; PFN_SemaphoreParameterui64vEXT semParam = nullptr;
    PFN_WaitSemaphoreEXT waitSem = nullptr; PFN_SignalSemaphoreEXT signalSem = nullptr;
    PFN_GenFramebuffers genFb = nullptr; PFN_DeleteFramebuffers deleteFb = nullptr; PFN_BindFramebuffer bindFb = nullptr;
    PFN_FramebufferTexture2D fbTex = nullptr; PFN_BlitFramebuffer blit = nullptr;
    PFN_FenceSync fenceSync = nullptr; PFN_WaitSync waitSync = nullptr; PFN_DeleteSync deleteSync = nullptr;
    PFN_ClientWaitSync clientWaitSync = nullptr;
    bool load() {
        auto g = [](const char* n) { return (void*)wglGetProcAddress(n); };
        createMem = (PFN_CreateMemoryObjectsEXT)g("glCreateMemoryObjectsEXT"); deleteMem = (PFN_DeleteMemoryObjectsEXT)g("glDeleteMemoryObjectsEXT");
        importMem = (PFN_ImportMemoryWin32HandleEXT)g("glImportMemoryWin32HandleEXT"); memParam = (PFN_MemoryObjectParameterivEXT)g("glMemoryObjectParameterivEXT");
        texStorageMem = (PFN_TexStorageMem2DEXT)g("glTexStorageMem2DEXT"); genSem = (PFN_GenSemaphoresEXT)g("glGenSemaphoresEXT");
        deleteSem = (PFN_DeleteSemaphoresEXT)g("glDeleteSemaphoresEXT"); importSem = (PFN_ImportSemaphoreWin32HandleEXT)g("glImportSemaphoreWin32HandleEXT");
        semParam = (PFN_SemaphoreParameterui64vEXT)g("glSemaphoreParameterui64vEXT"); waitSem = (PFN_WaitSemaphoreEXT)g("glWaitSemaphoreEXT");
        signalSem = (PFN_SignalSemaphoreEXT)g("glSignalSemaphoreEXT"); genFb = (PFN_GenFramebuffers)g("glGenFramebuffers");
        deleteFb = (PFN_DeleteFramebuffers)g("glDeleteFramebuffers"); bindFb = (PFN_BindFramebuffer)g("glBindFramebuffer");
        fbTex = (PFN_FramebufferTexture2D)g("glFramebufferTexture2D"); blit = (PFN_BlitFramebuffer)g("glBlitFramebuffer");
        fenceSync = (PFN_FenceSync)g("glFenceSync"); waitSync = (PFN_WaitSync)g("glWaitSync"); deleteSync = (PFN_DeleteSync)g("glDeleteSync");
        clientWaitSync = (PFN_ClientWaitSync)g("glClientWaitSync");
        return createMem && importMem && texStorageMem && genSem && importSem && semParam && waitSem && signalSem && genFb && bindFb && fbTex && blit;
    }
};

// M5 item 1 diagnostics (WFC_D3D12_INTEROP2PROBE=1): the per-frame CPU cost of the WGL_NV_DX_interop2 route - a D3D11
// texture registered with GL, locked, the GL back buffer blitted into it, unlocked - next to the external-semaphore
// signal. Measurement only: nothing consumes the D3D11 texture.
struct Interop2Probe {
    typedef HANDLE(WINAPI* PFN_Open)(void*);
    typedef HANDLE(WINAPI* PFN_Register)(HANDLE, void*, GLuint, GLenum, GLenum);
    typedef BOOL(WINAPI* PFN_Lock)(HANDLE, GLint, HANDLE*);
    PFN_Open open = nullptr; PFN_Register reg = nullptr; PFN_Lock lock = nullptr, unlock = nullptr;
    ID3D11Device* dev = nullptr; ID3D11Texture2D* tex = nullptr;
    HANDLE hDev = nullptr, hObj = nullptr; GLuint glTex = 0, fbo = 0;
    int w = 0, h = 0; double sum = 0, mx = 0; int n = 0; bool ok = false;
    bool init(int W, int H, const GlFns& gl) {
        open = (PFN_Open)(void*)wglGetProcAddress("wglDXOpenDeviceNV");
        reg = (PFN_Register)(void*)wglGetProcAddress("wglDXRegisterObjectNV");
        lock = (PFN_Lock)(void*)wglGetProcAddress("wglDXLockObjectsNV");
        unlock = (PFN_Lock)(void*)wglGetProcAddress("wglDXUnlockObjectsNV");
        typedef HRESULT(WINAPI * PFN_CreateD3D11)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
                                                  ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
        HMODULE m = LoadLibraryW(L"d3d11.dll");
        auto create = m ? (PFN_CreateD3D11)(void*)GetProcAddress(m, "D3D11CreateDevice") : nullptr;
        if (!open || !reg || !lock || !unlock || !create) { LOG_WARN("interop2 probe: entry points missing"); return false; }
        if (FAILED(create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, nullptr))) return false;
        D3D11_TEXTURE2D_DESC d{}; d.Width = (UINT)W; d.Height = (UINT)H; d.MipLevels = 1; d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &tex))) return false;
        hDev = open(dev);
        if (!hDev) { LOG_WARN("interop2 probe: wglDXOpenDeviceNV failed"); return false; }
        glGenTextures(1, &glTex);
        hObj = reg(hDev, tex, glTex, GL_TEXTURE_2D, 0x0001 /*WGL_ACCESS_READ_WRITE_NV*/);
        if (!hObj) { LOG_WARN("interop2 probe: wglDXRegisterObjectNV failed"); return false; }
        gl.genFb(1, &fbo);
        w = W; h = H; ok = true;
        LOG_INFO("interop2 probe: D3D11 texture %dx%d registered with GL", W, H);
        return true;
    }
    void frame(const GlFns& gl) {
        if (!ok) return;
        const auto t0 = std::chrono::steady_clock::now();
        if (!lock(hDev, 1, &hObj)) { ok = false; LOG_WARN("interop2 probe: lock failed"); return; }
        GLint prevRead = 0, prevDraw = 0;
        glGetIntegerv(0x8CAA, &prevRead); glGetIntegerv(0x8CA6, &prevDraw);
        gl.bindFb(0x8CA8, 0); glReadBuffer(GL_BACK);
        gl.bindFb(0x8CA9, fbo);
        gl.fbTex(0x8CA9, 0x8CE0, GL_TEXTURE_2D, glTex, 0);
        gl.blit(0, 0, w, h, 0, h, w, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        gl.bindFb(0x8CA8, (GLuint)prevRead); gl.bindFb(0x8CA9, (GLuint)prevDraw);
        unlock(hDev, 1, &hObj);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        sum += ms; mx = std::max(mx, ms);
        if (++n == 300) { LOG_INFO("interop2 probe: lock + blit + unlock (avg / max ms over 300) %.3f / %.2f", sum / 300, mx); sum = mx = 0; n = 0; }
    }
};

class D3D12Presenter {
public:
    bool init() {
        if (!gl_.load()) { LOG_WARN("D3D12 present: GL external-object entry points missing: GL present kept"); return false; }
        hwnd_ = WindowFromDC(wglGetCurrentDC());
        if (!hwnd_) { LOG_WARN("D3D12 present: no window: GL present kept"); return false; }
        d3d12SelectAgility();                        // (D3D12Agility.cpp: the Agility runtime when shipped)
        HMODULE d3d = LoadLibraryW(L"d3d12.dll"), dxgi = LoadLibraryW(L"dxgi.dll");
        auto create = d3d ? (PFN_D3D12_CREATE_DEVICE)(void*)GetProcAddress(d3d, "D3D12CreateDevice") : nullptr;
        typedef HRESULT(WINAPI * PFN_CF2)(UINT, REFIID, void**);
        auto cf2 = dxgi ? (PFN_CF2)(void*)GetProcAddress(dxgi, "CreateDXGIFactory2") : nullptr;
        if (!create || !cf2 || FAILED(cf2(0, __uuidof(IDXGIFactory5), (void**)&factory_)) ||
            FAILED(create(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&dev_))) {
            LOG_WARN("D3D12 present: no D3D12 device: GL present kept"); return false;
        }
        BOOL tear = FALSE;
        if (SUCCEEDED(factory_->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tear, sizeof tear))) tearing_ = tear != FALSE;
        vsync_ = std::getenv("WFC_D3D12_VSYNC") != nullptr;
#if defined(WFC_FFX_SDK)
        if (std::getenv("WFC_FRAMEGEN") || g_fgAtStartup) {   // FSR 3 frame generation (M5 C): the FG swapchain + context
            ffxDll_ = LoadLibraryW(L"amd_fidelityfx_loader_dx12.dll");
            if (ffxDll_) { ffxLoadFunctions(&ffx_, ffxDll_); fg_ = ffx_.CreateContext && ffx_.Configure && ffx_.Dispatch && ffx_.DestroyContext; }
            if (!fg_) LOG_WARN("FRAMEGEN: amd_fidelityfx_loader_dx12.dll / entry points missing: plain D3D12 present");
        }
#endif
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(dev_->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&queue_))) return false;
        if (const char* e = std::getenv("WFC_D3D12_BUFFERS")) buffers_ = (UINT)std::min(3, std::max(2, std::atoi(e)));
        if (const char* e = std::getenv("WFC_D3D12_LATENCY")) latency_ = (UINT)std::min(3, std::max(1, std::atoi(e)));
        for (UINT i = 0; i < buffers_; ++i)
            if (FAILED(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&alloc_[i]))) return false;
        if (FAILED(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc_[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&list_))) return false;
        list_->Close();
        if (FAILED(dev_->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), (void**)&fence_))) return false;
        if (FAILED(dev_->CreateSharedHandle(fence_, nullptr, GENERIC_ALL, nullptr, &fenceHandle_))) return false;
        gl_.genSem(1, &sem_);
        gl_.importSem(sem_, kHandleD3D12Fence, fenceHandle_);
        RECT rc; GetClientRect(hwnd_, &rc);
        if (!createSwapchain(std::max<LONG>(1, rc.right - rc.left), std::max<LONG>(1, rc.bottom - rc.top))) return false;
        if (std::getenv("WFC_D3D12_WORKER")) startWorker();
        {   // M5 item 1 diagnostics: is the WGL_NV_DX_interop2 path (lock / unlock instead of an external semaphore) offered?
            typedef const char*(WINAPI * PFN_GetExtStr)(HDC);
            auto ges = (PFN_GetExtStr)(void*)wglGetProcAddress("wglGetExtensionsStringARB");
            const char* ext = ges ? ges(wglGetCurrentDC()) : nullptr;
            LOG_INFO("D3D12 present: WGL_NV_DX_interop2 %s, WGL_NV_DX_interop %s", ext && std::strstr(ext, "WGL_NV_DX_interop2") ? "available" : "absent",
                     ext && std::strstr(ext, "WGL_NV_DX_interop ") ? "available" : "absent");
        }
        LOG_INFO("PRESENT: D3D12 (flip model, %u buffers, %u shared, latency-waitable max %u, %s%s%s) over the GL renderer (%dx%d)", buffers_, kRing, latency_,
                 vsync_ ? "v-sync" : tearing_ ? "immediate + tearing" : "immediate", worker_ ? ", worker-thread signal + present" : "",
                 fg_ ? ", FSR 3 frame generation" : "", w_, h_);
        return true;
    }
    // WFC_D3D12_WORKER=1: the GL semaphore signal (~0.7 ms CPU in the AMD driver, M5 item 1) and the D3D12 copy / present
    // run on a worker thread with a second GL context sharing the main one's objects; the main thread blits into the ring
    // slot, fences, flushes and returns. Order: the worker's glWaitSync on the main context's fence (GPU side) before
    // the signal. At most kRing - 1 frames on the worker (a ring slot is never rewritten before its signal was issued).
    void startWorker() {
        HDC dc = wglGetCurrentDC(); HGLRC mainCtx = wglGetCurrentContext();
        auto cca = (PFN_CreateContextAttribsARB)(void*)wglGetProcAddress("wglCreateContextAttribsARB");
        const int attrs[] = {0};
        HGLRC ctx = cca ? cca(dc, mainCtx, attrs) : nullptr;
        if (!ctx) { ctx = wglCreateContext(dc); if (ctx && !wglShareLists(mainCtx, ctx)) { wglDeleteContext(ctx); ctx = nullptr; } }
        if (!ctx || !gl_.fenceSync || !gl_.waitSync) { LOG_WARN("D3D12 present: no shared GL context for the worker: in-thread present kept"); return; }
        workerCtx_ = ctx; workerDc_ = dc;
        std::atomic<int> ready{0};
        worker_ = new std::thread([this, &ready] {
            if (!wglMakeCurrent(workerDc_, workerCtx_)) { ready = -1; return; }
            gl_.genSem(1, &workerSem_);
            gl_.importSem(workerSem_, kHandleD3D12Fence, fenceHandle_);
            ready = 1;
            workerLoop();
        });
        while (ready == 0) std::this_thread::yield();
        if (ready < 0) { worker_->join(); delete worker_; worker_ = nullptr; LOG_WARN("D3D12 present: worker context not current: in-thread present kept"); }
    }
    void workerLoop() {
        for (;;) {
            Job j;
            {
                std::unique_lock<std::mutex> lk(mx_);
                cv_.wait(lk, [this] { return stop_ || !jobs_.empty(); });
                if (jobs_.empty()) break;                // stop_ with nothing queued: shutdown()
                j = jobs_.front();
                jobs_.pop_front();
            }
            // AMD's glSignalSemaphoreEXT busy-spins (SwitchToThread) until the GPU finished the preceding GL work
            // (Systems' CPU profile, 2026-10-12): wait for it here in the kernel instead, then the signal is immediate
            if (gl_.clientWaitSync) gl_.clientWaitSync(j.sync, 0, 5000000000ull);
            else gl_.waitSync(j.sync, 0, ~0ull /*GL_TIMEOUT_IGNORED*/);
            gl_.deleteSync(j.sync);
            ++value_;
            const GLuint64_ v = value_;
            gl_.semParam(workerSem_, kFenceValue, &v);
            GLuint texs[4] = {glTex_[j.slot], 0, 0, 0}; GLenum lays[4] = {kLayoutTransferSrc, kLayoutShaderRead, kLayoutShaderRead, kLayoutShaderRead};
            const GLuint nTex = fgTextures(j.slot, texs + 1) + 1;
            gl_.signalSem(workerSem_, 0, nullptr, nTex, texs, lays);
            glFlush();
            const auto t1 = std::chrono::steady_clock::now();
            d3dTail(j.slot, j.t0, t1, j.sp);
            {
                std::lock_guard<std::mutex> lk(mx_);
                --inFlight_;
            }
            cv_.notify_all();
        }
        wglMakeCurrent(nullptr, nullptr);
    }
    void stopWorker() {
        if (!worker_) return;
        drainWorker();
        { std::lock_guard<std::mutex> lk(mx_); stop_ = true; }
        cv_.notify_all();
        worker_->join(); delete worker_; worker_ = nullptr;
        if (workerCtx_) { wglDeleteContext(workerCtx_); workerCtx_ = nullptr; }
    }
    void drainWorker() {
        if (!worker_) return;
        std::unique_lock<std::mutex> lk(mx_);
        cv_.wait(lk, [this] { return inFlight_ == 0; });
    }

    // the platform hook: copy the GL back buffer to the shared texture, D3D12 copies + presents
    bool present() {
        using clk = std::chrono::steady_clock;
        const clk::time_point t0 = clk::now();
        RECT rc; GetClientRect(hwnd_, &rc);
        // minimised (0 x 0 client area): nothing to present; no resize to a degenerate size (FG soak). The GL work is
        // still submitted and the loop throttled (no unbounded queue while hidden), and the first frame back resets FG.
        if (IsIconic(hwnd_) || rc.right - rc.left <= 0 || rc.bottom - rc.top <= 0) {
            glFlush();
            Sleep(10);
            resumeReset_ = true;
            return true;
        }
        const int w = std::max<LONG>(1, rc.right - rc.left), h = std::max<LONG>(1, rc.bottom - rc.top);
        if (w != w_ || h != h_) {
            drainWorker(); waitIdle();
            if (!resize(w, h)) {                         // no flip-model swapchain at this size: GL presents from now on
                LOG_ERROR("D3D12 present: the swapchain could not be resized to %dx%d: presenting through GL from now on "
                          "(frame generation off)", w, h);
                failed_ = true;
                return false;
            }
        }
        const UINT slot = (UINT)(mainFrame_++ % kRing);
        if (resumeReset_) { fgParams_[slot].reset = true; resumeReset_ = false; }   // (restored: no history across the gap)
        // diagnostics: WFC_D3D12_PREFLUSH=1 submits the frame's GL work first (attribution: is the signal's cost the submission?)
        static const bool preFlush = std::getenv("WFC_D3D12_PREFLUSH") != nullptr;
        if (preFlush) glFlush();
        // variants (A/B): WFC_D3D12_NOGLWAIT=1 skips the GL-side semaphore wait (relies on the ring age + the CPU
        // latency wait); WFC_D3D12_NOFLUSH=1 skips glFlush after the signal
        static const bool noGlWait = std::getenv("WFC_D3D12_NOGLWAIT") != nullptr;
        static const bool noFlush = std::getenv("WFC_D3D12_NOFLUSH") != nullptr;
        // GL waits only until D3D12 finished reading THIS ring slot (kRing frames ago: normally long done, then no GL
        // wait at all: the fence's completed value is a CPU read, and a GPU-side semaphore wait costs ~0.4 ms CPU)
        if (readDone_[slot] && !noGlWait && fence_->GetCompletedValue() < readDone_[slot]) {
            const GLuint64_ v = readDone_[slot].load();
            gl_.semParam(sem_, kFenceValue, &v);
            const GLenum layout = kLayoutTransferDst;
            gl_.waitSem(sem_, 0, nullptr, 1, &glTex_[slot], &layout);
        }
        const clk::time_point tA = clk::now();
        GLint prevRead = 0, prevDraw = 0;
        glGetIntegerv(0x8CAA /*GL_READ_FRAMEBUFFER_BINDING*/, &prevRead);
        glGetIntegerv(0x8CA6 /*GL_DRAW_FRAMEBUFFER_BINDING*/, &prevDraw);
        gl_.bindFb(0x8CA8 /*GL_READ_FRAMEBUFFER*/, 0);
        glReadBuffer(GL_BACK);
        gl_.bindFb(0x8CA9 /*GL_DRAW_FRAMEBUFFER*/, glFbo_[slot]);
        gl_.blit(0, 0, w_, h_, 0, h_, w_, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);   // flipped: D3D's origin is top-left
        gl_.bindFb(0x8CA8, (GLuint)prevRead);
        gl_.bindFb(0x8CA9, (GLuint)prevDraw);
        static const bool probe2 = std::getenv("WFC_D3D12_INTEROP2PROBE") != nullptr;
        if (probe2) {
            if (!probe2_.ok && !probe2Tried_) { probe2Tried_ = true; probe2_.init(w_, h_, gl_); }
            probe2_.frame(gl_);
        }
        const clk::time_point tB = clk::now();
        if (worker_) {                                   // hand the signal + D3D12 present to the worker
            Job j;
            j.slot = slot; j.t0 = t0;
            j.sync = gl_.fenceSync(0x9117 /*GL_SYNC_GPU_COMMANDS_COMPLETE*/, 0);
            glFlush();
            const clk::time_point tC = clk::now();
            j.sp[0] = std::chrono::duration<double, std::milli>(tA - t0).count();
            j.sp[1] = std::chrono::duration<double, std::milli>(tB - tA).count();
            j.sp[2] = std::chrono::duration<double, std::milli>(tC - tB).count();   // (fence + flush on the main thread)
            j.sp[3] = 0.0;
            {
                std::unique_lock<std::mutex> lk(mx_);
                // at most kRing - 1 frames on the worker: the slot written now is never one still waiting for its signal
                cv_.wait(lk, [this] { return inFlight_ < (int)kRing - 1; });
                jobs_.push_back(j); ++inFlight_;
            }
            const double qw = std::chrono::duration<double, std::milli>(clk::now() - tC).count();
            qwSum_ += qw; qwMax_ = std::max(qwMax_, qw);
            if (++qwN_ == 300) {
                LOG_INFO("PRESENT D3D12 worker: main-thread queue wait (avg / max ms over 300) %.3f / %.2f", qwSum_ / 300, qwMax_);
                qwSum_ = qwMax_ = 0; qwN_ = 0;
            }
            cv_.notify_all();
            return true;
        }
        ++value_;
        {
            // variant (A/B): WFC_D3D12_SIGNALNOTEX=1 signals without the texture / layout transition (D3D12 imports keep
            // their own state; the layout argument is the Vulkan-interop contract)
            static const bool noTex = std::getenv("WFC_D3D12_SIGNALNOTEX") != nullptr;
            const GLuint64_ v = value_;
            gl_.semParam(sem_, kFenceValue, &v);
            GLuint texs[4] = {glTex_[slot], 0, 0, 0}; GLenum lays[4] = {kLayoutTransferSrc, kLayoutShaderRead, kLayoutShaderRead, kLayoutShaderRead};
            const GLuint nTex = fgTextures(slot, texs + 1) + 1;
            if (noTex) gl_.signalSem(sem_, 0, nullptr, 0, nullptr, nullptr);
            else gl_.signalSem(sem_, 0, nullptr, nTex, texs, lays);
        }
        glx::gpuPrePresent();                          // (WFC_GPUSTAMPS only) the GL side's present point
        const clk::time_point tC = clk::now();
        if (!noFlush) glFlush();
        const clk::time_point t1 = clk::now();
        const double sp[4] = {std::chrono::duration<double, std::milli>(tA - t0).count(), std::chrono::duration<double, std::milli>(tB - tA).count(),
                              std::chrono::duration<double, std::milli>(tC - tB).count(), std::chrono::duration<double, std::milli>(t1 - tC).count()};
        return d3dTail(slot, t0, t1, sp);
    }
    // D3D12: wait for GL, copy into the back buffer, present, signal (on the calling thread or the worker)
    bool d3dTail(UINT slot, std::chrono::steady_clock::time_point t0, std::chrono::steady_clock::time_point t1, const double sp[4]) {
        using clk = std::chrono::steady_clock;
        if (lost_) return true;                          // (device lost: the main thread falls back to GL)
        for (int k = 0; k < 4; ++k) { subSum_[k] += sp[k]; subMax_[k] = std::max(subMax_[k], sp[k]); }
        if (waitable_) WaitForSingleObjectEx(waitable_, 1000, TRUE);   // frame-latency pacing (no fence spin)
        const clk::time_point t2 = clk::now();
        const UINT bi = swap_->GetCurrentBackBufferIndex();
        throttle(bi);
        const clk::time_point t3 = clk::now();
        alloc_[bi]->Reset();
        list_->Reset(alloc_[bi], nullptr);
        D3D12_RESOURCE_BARRIER b[2]{};
        for (auto& x : b) { x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; }
        b[0].Transition.pResource = shared_[slot]; b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b[1].Transition.pResource = back_[bi]; b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        list_->ResourceBarrier(2, b);
        fgRecord(slot);                                  // FSR 3 FG: configure + prepare (depth / motion / camera) for this frame
        list_->CopyResource(back_[bi], shared_[slot]);
        // WFC_D3D12SHOT=<frame>,<file.bmp> (verification): the presented image read back from the D3D12 side
        static int shotFrame = -1; static std::string shotFile;
        static const bool shotParsed = [] { if (const char* e = std::getenv("WFC_D3D12SHOT")) { std::string t = e; size_t c = t.find(',');
            if (c != std::string::npos) { shotFrame = std::atoi(t.c_str()); shotFile = t.substr(c + 1); } } return true; }();
        (void)shotParsed;
        ID3D12Resource* readback = nullptr;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        if (++presented_ == shotFrame) {
            UINT64 total = 0;
            D3D12_RESOURCE_DESC bd = back_[bi]->GetDesc();
            dev_->GetCopyableFootprints(&bd, 0, 1, 0, &fp, nullptr, nullptr, &total);
            D3D12_HEAP_PROPERTIES rp{}; rp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC rb{}; rb.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rb.Width = total; rb.Height = 1;
            rb.DepthOrArraySize = 1; rb.MipLevels = 1; rb.SampleDesc.Count = 1; rb.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (SUCCEEDED(dev_->CreateCommittedResource(&rp, D3D12_HEAP_FLAG_NONE, &rb, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                        __uuidof(ID3D12Resource), (void**)&readback))) {
                D3D12_RESOURCE_BARRIER t{}; t.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; t.Transition.pResource = back_[bi];
                t.Transition.Subresource = 0; t.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; t.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                list_->ResourceBarrier(1, &t);
                D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = readback; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp;
                D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = back_[bi]; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
                list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                std::swap(t.Transition.StateBefore, t.Transition.StateAfter);
                list_->ResourceBarrier(1, &t);
            }
        }
        std::swap(b[0].Transition.StateBefore, b[0].Transition.StateAfter);
        std::swap(b[1].Transition.StateBefore, b[1].Transition.StateAfter);
        list_->ResourceBarrier(2, b);
        list_->Close();
        queue_->Wait(fence_, value_);
        ID3D12CommandList* lists[] = {list_};
        queue_->ExecuteCommandLists(1, lists);
        const clk::time_point t4 = clk::now();
        HRESULT hr = swap_->Present(vsync_ ? 1 : 0, !vsync_ && tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
        // (verification) WFC_D3D12_SIMLOST=<n>: the n-th present reports DXGI_ERROR_DEVICE_REMOVED (the GL fallback path)
        static const int simLost = [] { const char* e = std::getenv("WFC_D3D12_SIMLOST"); return e ? std::atoi(e) : -1; }();
        if (presented_ == simLost) hr = DXGI_ERROR_DEVICE_REMOVED;
        ++value_;
        queue_->Signal(fence_, value_);
        frameValue_[bi] = value_;
        readDone_[slot] = value_;
        {   // phase timers (CPU ms): GL copy + signal + flush | latency wait | fence throttle | D3D12 submit | Present
            const clk::time_point t5 = clk::now();
            const double ph[5] = {std::chrono::duration<double, std::milli>(t1 - t0).count(), std::chrono::duration<double, std::milli>(t2 - t1).count(),
                                  std::chrono::duration<double, std::milli>(t3 - t2).count(), std::chrono::duration<double, std::milli>(t4 - t3).count(),
                                  std::chrono::duration<double, std::milli>(t5 - t4).count()};
            for (int k = 0; k < 5; ++k) { phSum_[k] += ph[k]; phMax_[k] = std::max(phMax_[k], ph[k]); }
            if (++phN_ == 300) {
                LOG_INFO("PRESENT D3D12 phases (avg / max ms over 300): GL copy+signal %.3f / %.2f, latency wait %.3f / %.2f, "
                         "throttle %.3f / %.2f, submit %.3f / %.2f, Present %.3f / %.2f",
                         phSum_[0] / 300, phMax_[0], phSum_[1] / 300, phMax_[1], phSum_[2] / 300, phMax_[2], phSum_[3] / 300, phMax_[3],
                         phSum_[4] / 300, phMax_[4]);
                LOG_INFO("PRESENT D3D12 GL sub-phases (avg / max ms): resize + semaphore wait %.3f / %.2f, blit %.3f / %.2f, "
                         "semaphore signal %.3f / %.2f, glFlush %.3f / %.2f",
                         subSum_[0] / 300, subMax_[0], subSum_[1] / 300, subMax_[1], subSum_[2] / 300, subMax_[2], subSum_[3] / 300, subMax_[3]);
                for (int k = 0; k < 4; ++k) { subSum_[k] = 0; subMax_[k] = 0; }
                for (int k = 0; k < 5; ++k) { phSum_[k] = 0; phMax_[k] = 0; }
                phN_ = 0;
            }
        }
        if (FAILED(hr)) presentFailed(hr);
        {   // frame start -> Present returned (CPU-side latency proxy) and FG resets, per 300 frames
            const long long fs = frameStartNs_[slot];
            if (fs) { const double ms = (std::chrono::duration_cast<std::chrono::nanoseconds>(clk::now().time_since_epoch()).count() - fs) / 1e6;
                      latSum_ += ms; latMax_ = std::max(latMax_, ms); ++latN_; }
            if (latN_ >= 300) {
                LOG_INFO("PRESENT latency proxy (frame start -> Present returned, avg / max ms over %d): %.3f / %.2f%s; FG resets %d",
                         latN_, latSum_ / latN_, latMax_, fg_ ? " (FG)" : "", fgResets_);
                latSum_ = latMax_ = 0; latN_ = 0; fgResets_ = 0;
            }
        }
        fgCount(t0);
        if (readback) {                                // wait, then write a top-down 32-bit BMP of the presented image
            waitIdle();
            uint8_t* p = nullptr;
            D3D12_RANGE rr{0, (SIZE_T)(fp.Footprint.RowPitch * fp.Footprint.Height)};
            if (SUCCEEDED(readback->Map(0, &rr, (void**)&p)) && p) {
                const int W = (int)fp.Footprint.Width, H = (int)fp.Footprint.Height;
                FILE* f = std::fopen(shotFile_().c_str(), "wb");
                if (f) {
                    const uint32_t img = (uint32_t)(W * H * 4);
                    uint8_t hdr[54] = {'B', 'M'}; uint32_t fsz = 54 + img;
                    std::memcpy(hdr + 2, &fsz, 4); uint32_t off = 54; std::memcpy(hdr + 10, &off, 4);
                    uint32_t ih = 40; std::memcpy(hdr + 14, &ih, 4); std::memcpy(hdr + 18, &W, 4); int32_t nh = -H; std::memcpy(hdr + 22, &nh, 4);
                    uint16_t planes = 1, bpp = 32; std::memcpy(hdr + 26, &planes, 2); std::memcpy(hdr + 28, &bpp, 2);
                    std::memcpy(hdr + 34, &img, 4);
                    std::fwrite(hdr, 1, 54, f);
                    std::vector<uint8_t> row((size_t)W * 4);
                    for (int y = 0; y < H; ++y) {
                        const uint8_t* s0 = p + (size_t)y * fp.Footprint.RowPitch;
                        for (int x = 0; x < W; ++x) { row[x * 4] = s0[x * 4 + 2]; row[x * 4 + 1] = s0[x * 4 + 1]; row[x * 4 + 2] = s0[x * 4]; row[x * 4 + 3] = 255; }
                        std::fwrite(row.data(), 1, row.size(), f);
                    }
                    std::fclose(f);
                    LOG_INFO("D3D12 present: frame %d written to %s (%dx%d)", presented_, shotFile_().c_str(), W, H);
                }
                readback->Unmap(0, nullptr);
            }
            rel(readback);
        }
        return true;
    }

    // a failed Present: device removed / reset / hung -> lost_ (logged once; present() then hands the window back to GL);
    // any other code logged once per code (no per-frame spam)
    void presentFailed(HRESULT hr) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG) {
            if (!lost_.exchange(true))
                LOG_ERROR("D3D12 present: device lost (Present 0x%08lx, removed reason 0x%08lx): presenting through GL from now on "
                          "(frame generation off)", (unsigned long)hr, (unsigned long)dev_->GetDeviceRemovedReason());
            return;
        }
        std::lock_guard<std::mutex> lk(logMx_);
        if (std::find(failCodes_.begin(), failCodes_.end(), hr) != failCodes_.end()) return;
        failCodes_.push_back(hr);
        LOG_WARN("D3D12 present: Present failed (0x%08lx; logged once per code)", (unsigned long)hr);
    }
    bool failed() const { return failed_ || lost_; }
    // ordered teardown (exit, or the GL fallback): worker joined, GPU idle, FG presents drained, FG context, the FG
    // inputs and the shared ring freed, the swapchain released before the FG swapchain context, then the device objects.
    // The GL context must be current (it is: ~GLRenderer, or present() on the main thread).
    void shutdown() {
        if (down_) return;
        down_ = true;
        stopWorker();
        // drained unless the device is really gone (then fences complete at once; a healthy device - Present failed for
        // another reason, or WFC_D3D12_SIMLOST - must drain, or destroying the FG swapchain blocks on its present queue)
        const bool removed = dev_ && dev_->GetDeviceRemovedReason() != S_OK;
        if (!removed) waitIdle();
#if defined(WFC_FFX_SDK)
        if (fgSwapCtx_ && !removed) {
            ffxDispatchDescFrameGenerationSwapChainWaitForPresentsDX12 wp{};
            wp.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12;
            ffx_.Dispatch(&fgSwapCtx_, &wp.header);
        }
        if (fgCtx_) { ffx_.DestroyContext(&fgCtx_, nullptr); fgCtx_ = nullptr; }
#endif
        for (auto& slot : fgIn_) for (auto& x : slot) freeShared(x);
        for (UINT k = 0; k < kRing; ++k) {
            if (glFbo_[k]) { gl_.deleteFb(1, &glFbo_[k]); glFbo_[k] = 0; }
            if (glTex_[k]) { glDeleteTextures(1, &glTex_[k]); glTex_[k] = 0; }
            if (glMem_[k]) { gl_.deleteMem(1, &glMem_[k]); glMem_[k] = 0; }
            if (sharedHandle_[k]) { CloseHandle(sharedHandle_[k]); sharedHandle_[k] = nullptr; }
            rel(shared_[k]);
        }
        glx::textureCacheInvalidate();
        for (auto& b : back_) rel(b);
        rel(swap_);
#if defined(WFC_FFX_SDK)
        if (fgSwapCtx_) { ffx_.DestroyContext(&fgSwapCtx_, nullptr); fgSwapCtx_ = nullptr; }
#endif
        fg_ = false;
        rel(list_);
        for (auto& a : alloc_) rel(a);
        rel(queue_); rel(fence_);
        if (fenceHandle_) { CloseHandle(fenceHandle_); fenceHandle_ = nullptr; }
        rel(dev_); rel(factory_);
        LOG_INFO("D3D12 present: shut down%s", lost_ ? " (device lost)" : "");
    }

    // ---------------------------------------------------------------- FSR 3 frame generation (M5 C)
    bool fgOn() const { return fg_ && fgCtx_ && !lost_ && !failed_; }
    bool fgGenerating() const { return fgOn() && fgGenerating_; }
    // the slot the next present() uses: wait (GPU side) for D3D12's last reads of it, (re)create the inputs at this size
    bool fgInputs(int rw, int rh, FgInputs* out) {
        if (!fgOn() || rw <= 0 || rh <= 0) return false;
        const UINT slot = (UINT)(mainFrame_ % kRing);
        if (readDone_[slot] && fence_->GetCompletedValue() < readDone_[slot]) {
            const GLuint64_ v = readDone_[slot].load();
            gl_.semParam(sem_, kFenceValue, &v);
            GLuint t[3] = {fgIn_[slot][0].tex, fgIn_[slot][1].tex, fgIn_[slot][2].tex};
            GLenum l[3] = {kLayoutTransferDst, kLayoutTransferDst, kLayoutTransferDst};
            gl_.waitSem(sem_, 0, nullptr, t[0] ? 3 : 0, t, l);
        }
        if (fgIn_[slot][0].w != w_ || fgIn_[slot][0].h != h_ || fgIn_[slot][1].w != rw || fgIn_[slot][1].h != rh) {
            const int key[4] = {w_, h_, rw, rh};
            if (std::equal(key, key + 4, fgFailSize_)) return false;   // (failed at this size already: no retry each frame)
            // FG may still read these inputs (queued presents / interpolation): drained and idle first, as resize()
            if (fgIn_[slot][0].res) {
                drainWorker();
#if defined(WFC_FFX_SDK)
                if (fgSwapCtx_) {
                    ffxDispatchDescFrameGenerationSwapChainWaitForPresentsDX12 wp{};
                    wp.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12;
                    ffx_.Dispatch(&fgSwapCtx_, &wp.header);
                }
#endif
                waitIdle();
            }
            for (auto& x : fgIn_[slot]) freeShared(x);
            if (!makeShared(fgIn_[slot][0], w_, h_, DXGI_FORMAT_R8G8B8A8_UNORM, 0x8058 /*GL_RGBA8*/) ||
                !makeShared(fgIn_[slot][1], rw, rh, DXGI_FORMAT_R32_FLOAT, 0x822E /*GL_R32F*/) ||
                !makeShared(fgIn_[slot][2], rw, rh, DXGI_FORMAT_R16G16_FLOAT, 0x822F /*GL_RG16F*/)) {
                LOG_WARN("FRAMEGEN: input textures failed at %dx%d / render %dx%d (real frames only at this size)", w_, h_, rw, rh);
                for (auto& x : fgIn_[slot]) freeShared(x);
                std::copy(key, key + 4, fgFailSize_);
                return false;
            }
        }
        out->hudless = fgIn_[slot][0].tex; out->depth = fgIn_[slot][1].tex; out->velocity = fgIn_[slot][2].tex;
        fgHave_[slot] = true;
        return true;
    }
    void fgSetParams(const FgParams& p) { fgParams_[(UINT)(mainFrame_ % kRing)] = p; }
    void setFrameStart(long long ns) { frameStartNs_[(UINT)(mainFrame_ % kRing)] = ns; }
    GLuint fgTextures(UINT slot, GLuint* out) const {   // the slot's FG inputs written this frame (for the GL signal)
        if (!fgOn() || !fgHave_[slot]) return 0;
        for (int k = 0; k < 3; ++k) out[k] = fgIn_[slot][k].tex;
        return 3;
    }
    void fgRecord(UINT slot) {
#if defined(WFC_FFX_SDK)
        if (!fgOn()) return;
        const bool have = fgHave_[slot];
        fgHave_[slot] = false;
        fgGenerating_ = have;                            // (frameGenerationFactor: 2 only while frames are generated)
        const FgParams& p = fgParams_[slot];
        if (have && p.reset) ++fgResets_;
        const uint64_t id = ++fgFrameId_;
        ffxConfigureDescFrameGeneration c{};
        c.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        c.swapChain = swap_;
        c.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* ctx) -> ffxReturnCode_t {
            D3D12Presenter* self = (D3D12Presenter*)ctx;
            return self->ffx_.Dispatch(&self->fgCtx_, &params->header);
        };
        c.frameGenerationCallbackUserContext = this;
        c.frameGenerationEnabled = have;                 // menus / frames without inputs: real frames only
        c.allowAsyncWorkloads = false;
        c.HUDLessColor = have ? ffxApiGetResourceDX12(fgIn_[slot][0].res, FFX_API_RESOURCE_STATE_COMMON) : FfxApiResource{};
        c.flags = std::getenv("WFC_FRAMEGEN_DEBUG") ? (FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES | FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_RESET_INDICATORS) : 0;
        c.onlyPresentGenerated = std::getenv("WFC_FRAMEGEN_ONLYGEN") != nullptr;   // (verification: show generated frames only)
        c.generationRect = {0, 0, w_, h_};
        c.frameID = id;
        if (ffx_.Configure(&fgCtx_, &c.header) != FFX_API_RETURN_OK && !fgCfgFailLogged_) { LOG_WARN("FRAMEGEN: configure failed"); fgCfgFailLogged_ = true; }
        if (!have) return;
        D3D12_RESOURCE_BARRIER t[2]{};
        for (int k = 0; k < 2; ++k) {
            t[k].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; t[k].Transition.pResource = fgIn_[slot][1 + k].res;
            t[k].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            t[k].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            t[k].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        list_->ResourceBarrier(2, t);
        ffxDispatchDescFrameGenerationPrepareV2 d{};
        d.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
        d.frameID = id; d.flags = c.flags; d.commandList = list_;
        d.renderSize = {(uint32_t)p.renderW, (uint32_t)p.renderH};
        d.jitterOffset = {p.jitterX, p.jitterY};
        d.motionVectorScale = {(float)p.renderW, (float)p.renderH};
        d.frameTimeDelta = p.frameTimeMs; d.reset = p.reset;
        d.cameraNear = p.nearM; d.cameraFar = p.farM; d.cameraFovAngleVertical = p.fovY; d.viewSpaceToMetersFactor = 1.0f;
        d.depth = ffxApiGetResourceDX12(fgIn_[slot][1].res, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        d.motionVectors = ffxApiGetResourceDX12(fgIn_[slot][2].res, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        for (int k = 0; k < 3; ++k) { d.cameraPosition[k] = p.camPos[k]; d.cameraUp[k] = p.camUp[k]; d.cameraRight[k] = p.camRight[k]; d.cameraForward[k] = p.camFwd[k]; }
        if (ffx_.Dispatch(&fgCtx_, &d.header) != FFX_API_RETURN_OK && !fgPrepFailLogged_) { LOG_WARN("FRAMEGEN: prepare failed"); fgPrepFailLogged_ = true; }
        for (auto& x : t) std::swap(x.Transition.StateBefore, x.Transition.StateAfter);
        list_->ResourceBarrier(2, t);
#else
        (void)slot;
#endif
    }
    // presented frames (real + generated) from the swapchain's statistics vs real frames, per 300 real frames
    void fgCount(std::chrono::steady_clock::time_point t0) {
        if (!fg_) return;
        if (fgCountN_ == 0) {
            fgCountT0_ = t0; DXGI_FRAME_STATISTICS st{};
            fgCountP0_ = SUCCEEDED(swap_->GetFrameStatistics(&st)) ? st.PresentCount : 0;
            UINT pc = 0; fgLast0_ = SUCCEEDED(swap_->GetLastPresentCount(&pc)) ? pc : 0;
        }
        if (++fgCountN_ < 300) return;
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - fgCountT0_).count();
        DXGI_FRAME_STATISTICS st{}; UINT pc = 0;
        const bool okStats = SUCCEEDED(swap_->GetFrameStatistics(&st));
        const bool okLast = SUCCEEDED(swap_->GetLastPresentCount(&pc));
        LOG_INFO("FRAMEGEN: %d real frames in %.3f s (%.1f fps); swapchain presents %s %.1f / s (frame statistics), %s %.1f / s (last present count)",
                 fgCountN_, s, fgCountN_ / s, okStats ? "" : "(n/a)", okStats ? (st.PresentCount - fgCountP0_) / s : 0.0,
                 okLast ? "" : "(n/a)", okLast ? (pc - fgLast0_) / s : 0.0);
        fgCountN_ = 0;
    }
#if defined(WFC_FFX_SDK)
    bool createFgContext(int w, int h) {
        if (fgCtx_) { ffx_.DestroyContext(&fgCtx_, nullptr); fgCtx_ = nullptr; }
        ffxCreateBackendDX12Desc be{}; be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be.device = dev_;
        ffxCreateContextDescFrameGenerationHudless hl{}; hl.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_HUDLESS;
        hl.hudlessBackBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
        ffxCreateContextDescFrameGeneration c{}; c.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        c.flags = std::getenv("WFC_FRAMEGEN_DEBUG") ? FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING : 0;
        c.displaySize = {(uint32_t)w, (uint32_t)h}; c.maxRenderSize = {(uint32_t)w, (uint32_t)h};
        c.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
        c.header.pNext = &be.header; be.header.pNext = &hl.header;
        if (ffx_.CreateContext(&fgCtx_, &c.header, nullptr) != FFX_API_RETURN_OK) { fgCtx_ = nullptr; return false; }
        LOG_INFO("FRAMEGEN: context %dx%d (HUD-less UI extraction)", w, h);
        return true;
    }
#endif
    struct FgShared { ID3D12Resource* res = nullptr; HANDLE handle = nullptr; GLuint mem = 0, tex = 0; int w = 0, h = 0; };
    bool makeShared(FgShared& x, int w, int h, DXGI_FORMAT fmt, GLenum glFmt) {
        while (glGetError() != GL_NO_ERROR) {}           // (a GL error left by earlier rendering is not this import's)
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = (UINT64)w; rd.Height = (UINT)h;
        rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.Format = fmt; rd.SampleDesc.Count = 1;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                 __uuidof(ID3D12Resource), (void**)&x.res))) return false;
        if (FAILED(dev_->CreateSharedHandle(x.res, nullptr, GENERIC_ALL, nullptr, &x.handle))) return false;
        const D3D12_RESOURCE_ALLOCATION_INFO ai = dev_->GetResourceAllocationInfo(0, 1, &rd);
        gl_.createMem(1, &x.mem);
        GLint dedicated = GL_TRUE;
        if (gl_.memParam) gl_.memParam(x.mem, kDedicated, &dedicated);
        gl_.importMem(x.mem, ai.SizeInBytes, kHandleD3D12Resource, x.handle);
        glGenTextures(1, &x.tex);
        glBindTexture(GL_TEXTURE_2D, x.tex);
        gl_.texStorageMem(GL_TEXTURE_2D, 1, glFmt, w, h, x.mem, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glx::textureCacheInvalidate();                   // (raw binds: the renderer's bind cache no longer matches)
        x.w = w; x.h = h;
        return glGetError() == GL_NO_ERROR;
    }
    void freeShared(FgShared& x) {
        if (x.tex) { glDeleteTextures(1, &x.tex); glx::textureCacheInvalidate(); }
        if (x.mem) gl_.deleteMem(1, &x.mem);
        if (x.handle) CloseHandle(x.handle);
        rel(x.res);
        x = FgShared{};
    }
    bool fg_ = false, fgHave_[kRing] = {}, fgCfgFailLogged_ = false, fgPrepFailLogged_ = false;
    std::atomic<bool> fgGenerating_{false}, lost_{false};
    bool failed_ = false, down_ = false, stop_ = false, resumeReset_ = false;
    int fgFailSize_[4] = {};
    std::mutex logMx_; std::vector<HRESULT> failCodes_;
    FgShared fgIn_[kRing][3];                          // per ring slot: HUD-less, depth, motion vectors
    FgParams fgParams_[kRing];
    uint64_t fgFrameId_ = 0;
    long long frameStartNs_[kRing] = {};
    double latSum_ = 0, latMax_ = 0; int latN_ = 0, fgResets_ = 0;
    UINT swapFlags_ = 0;
    int fgCountN_ = 0; UINT fgCountP0_ = 0, fgLast0_ = 0; std::chrono::steady_clock::time_point fgCountT0_;
#if defined(WFC_FFX_SDK)
    HMODULE ffxDll_ = nullptr;
    ffxFunctions ffx_{};
    ffxContext fgSwapCtx_ = nullptr, fgCtx_ = nullptr;
#else
    void* fgCtx_ = nullptr;
#endif

private:
    bool createSwapchain(int w, int h) {
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = (UINT)w; sd.Height = (UINT)h; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = buffers_; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapFlags_ = (tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0) | (fg_ ? 0u : (UINT)DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
        sd.Flags = swapFlags_;
        IDXGISwapChain1* sc1 = nullptr;
#if defined(WFC_FFX_SDK)
        if (fg_) {
            IDXGISwapChain4* sc4 = nullptr;
            ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 d{};
            d.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
            d.swapchain = &sc4; d.hwnd = hwnd_; d.desc = &sd; d.fullscreenDesc = nullptr; d.dxgiFactory = factory_; d.gameQueue = queue_;
            if (ffx_.CreateContext(&fgSwapCtx_, &d.header, nullptr) == FFX_API_RETURN_OK && sc4) sc1 = sc4;
            else { LOG_WARN("FRAMEGEN: FG swapchain creation failed: plain swapchain"); fg_ = false; fgSwapCtx_ = nullptr;
                   swapFlags_ |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT; sd.Flags = swapFlags_; }
        }
#endif
        if (!sc1 && (FAILED(factory_->CreateSwapChainForHwnd(queue_, hwnd_, &sd, nullptr, nullptr, &sc1)) || !sc1)) {
            LOG_WARN("D3D12 present: CreateSwapChainForHwnd failed (the GL window may not accept a flip-model swapchain)");
            return false;
        }
        factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
        sc1->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&swap_);
        rel(sc1);
        if (swap_ && !fg_) { swap_->SetMaximumFrameLatency(latency_); waitable_ = swap_->GetFrameLatencyWaitableObject(); }
        return resize(w, h, false);
    }
    bool resizeFail(const char* what, int w, int h) {   // logged once per kind: a failing resize falls back to GL each frame
        static std::string seen;
        if (seen.find(what) == std::string::npos) { seen += what; LOG_WARN("D3D12 present: resize to %dx%d failed at %s", w, h, what); }
        return false;
    }
    bool resize(int w, int h, bool resizeBuffers = true) {
        while (glGetError() != GL_NO_ERROR) {}           // (a GL error left by earlier rendering is not this resize's)
        for (auto& b : back_) rel(b);
#if defined(WFC_FFX_SDK)
        if (resizeBuffers && fg_) {                      // FG swapchain: its presents drained and the FG context gone first
            if (fgSwapCtx_) {
                ffxDispatchDescFrameGenerationSwapChainWaitForPresentsDX12 wp{};
                wp.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12;
                ffx_.Dispatch(&fgSwapCtx_, &wp.header);
            }
            if (fgCtx_) { ffx_.DestroyContext(&fgCtx_, nullptr); fgCtx_ = nullptr; }
        }
#endif
        if (resizeBuffers && FAILED(swap_->ResizeBuffers(buffers_, (UINT)w, (UINT)h, DXGI_FORMAT_R8G8B8A8_UNORM, swapFlags_)))
            return resizeFail("ResizeBuffers", w, h);
        for (UINT i = 0; i < buffers_; ++i) swap_->GetBuffer(i, __uuidof(ID3D12Resource), (void**)&back_[i]);
        // the shared texture ring + its GL imports
        for (UINT k = 0; k < kRing; ++k) {
            if (glFbo_[k]) { gl_.deleteFb(1, &glFbo_[k]); glFbo_[k] = 0; }
            if (glTex_[k]) { glDeleteTextures(1, &glTex_[k]); glTex_[k] = 0; }
            if (glMem_[k]) { gl_.deleteMem(1, &glMem_[k]); glMem_[k] = 0; }
            if (sharedHandle_[k]) { CloseHandle(sharedHandle_[k]); sharedHandle_[k] = nullptr; }
            rel(shared_[k]);
            readDone_[k] = 0;
        }
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = (UINT64)w; rd.Height = (UINT)h; rd.DepthOrArraySize = 1;
        rd.MipLevels = 1; rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 1;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        const D3D12_RESOURCE_ALLOCATION_INFO ai = dev_->GetResourceAllocationInfo(0, 1, &rd);
        for (UINT k = 0; k < kRing; ++k) {
            if (FAILED(dev_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                     __uuidof(ID3D12Resource), (void**)&shared_[k]))) return resizeFail("CreateCommittedResource", w, h);
            if (FAILED(dev_->CreateSharedHandle(shared_[k], nullptr, GENERIC_ALL, nullptr, &sharedHandle_[k]))) return resizeFail("CreateSharedHandle", w, h);
            gl_.createMem(1, &glMem_[k]);
            GLint dedicated = GL_TRUE;
            if (gl_.memParam) gl_.memParam(glMem_[k], kDedicated, &dedicated);
            gl_.importMem(glMem_[k], ai.SizeInBytes, kHandleD3D12Resource, sharedHandle_[k]);
            glGenTextures(1, &glTex_[k]);
            glBindTexture(GL_TEXTURE_2D, glTex_[k]);
            gl_.texStorageMem(GL_TEXTURE_2D, 1, 0x8058 /*GL_RGBA8*/, w, h, glMem_[k], 0);
            glBindTexture(GL_TEXTURE_2D, 0);
            gl_.genFb(1, &glFbo_[k]);
            gl_.bindFb(0x8D40 /*GL_FRAMEBUFFER*/, glFbo_[k]);
            gl_.fbTex(0x8D40, 0x8CE0 /*GL_COLOR_ATTACHMENT0*/, GL_TEXTURE_2D, glTex_[k], 0);
        }
        gl_.bindFb(0x8D40, 0);
        glx::textureCacheInvalidate();                   // (raw binds / deletes above)
        w_ = w; h_ = h;
        for (auto& v : frameValue_) v = 0;
#if defined(WFC_FFX_SDK)
        if (fg_ && !createFgContext(w, h)) { LOG_WARN("FRAMEGEN: context creation failed: real frames only"); }
#endif
        const GLenum ge = glGetError();
        if (ge != GL_NO_ERROR) { char b[48]; std::snprintf(b, sizeof b, "GL import (error 0x%04x)", (unsigned)ge); return resizeFail(b, w, h); }
        return true;
    }
    void throttle(UINT bi) {                           // at most buffers_ frames in flight on the D3D12 side
        if (frameValue_[bi] && fence_->GetCompletedValue() < frameValue_[bi]) {
            HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            fence_->SetEventOnCompletion(frameValue_[bi], e);
            WaitForSingleObject(e, 1000);
            CloseHandle(e);
        }
    }
    void waitIdle() {
        if (!queue_) return;
        ++value_;
        queue_->Signal(fence_, value_);
        if (fence_->GetCompletedValue() < value_) {
            HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            fence_->SetEventOnCompletion(value_, e);
            WaitForSingleObject(e, 2000);
            CloseHandle(e);
        }
    }

    GlFns gl_;
    HWND hwnd_ = nullptr;
    IDXGIFactory5* factory_ = nullptr;
    ID3D12Device* dev_ = nullptr;
    ID3D12CommandQueue* queue_ = nullptr;
    ID3D12CommandAllocator* alloc_[kMaxBuffers] = {};
    ID3D12GraphicsCommandList* list_ = nullptr;
    IDXGISwapChain3* swap_ = nullptr;
    ID3D12Resource* back_[kMaxBuffers] = {};
    ID3D12Resource* shared_[kRing] = {};
    HANDLE sharedHandle_[kRing] = {}, fenceHandle_ = nullptr, waitable_ = nullptr;
    std::atomic<UINT64> readDone_[kRing] = {};
    UINT64 mainFrame_ = 0;
    struct Job { UINT slot = 0; GLsync_ sync = nullptr; std::chrono::steady_clock::time_point t0; double sp[4] = {}; };
    std::thread* worker_ = nullptr;
    HGLRC workerCtx_ = nullptr; HDC workerDc_ = nullptr; GLuint workerSem_ = 0;
    std::mutex mx_; std::condition_variable cv_; std::deque<Job> jobs_; int inFlight_ = 0;
    double qwSum_ = 0, qwMax_ = 0; int qwN_ = 0;
    Interop2Probe probe2_; bool probe2Tried_ = false;
    ID3D12Fence* fence_ = nullptr;
    UINT64 value_ = 0, frameValue_[kMaxBuffers] = {};
    UINT buffers_ = 2, latency_ = 2;
    GLuint glMem_[kRing] = {}, glTex_[kRing] = {}, glFbo_[kRing] = {}, sem_ = 0;
    int w_ = 0, h_ = 0, presented_ = 0;
    double phSum_[5] = {}, phMax_[5] = {}, subSum_[4] = {}, subMax_[4] = {};
    int phN_ = 0;
    bool tearing_ = false, vsync_ = false;
    static std::string shotFile_() { const char* e = std::getenv("WFC_D3D12SHOT"); std::string t = e ? e : ""; size_t c = t.find(','); return c == std::string::npos ? t : t.substr(c + 1); }
};

D3D12Presenter* g_presenter = nullptr;
}  // namespace
// exit (~GLRenderer, GL context current, before the window goes) or the GL fallback: ordered teardown, GL presents
void shutdownD3D12Present() {
    if (!g_presenter) return;
    platform::setPresentOverride(nullptr);
    g_presenter->shutdown();
    delete g_presenter;
    g_presenter = nullptr;
}
namespace {
bool presentHook() {
    if (!g_presenter) return false;
    if (!g_presenter->failed() && g_presenter->present() && !g_presenter->failed()) return true;
    shutdownD3D12Present();                              // device lost / no swapchain: this frame and the rest through GL
    return false;
}
}  // namespace
bool framegenActive() { return g_presenter && g_presenter->fgOn(); }
bool framegenGenerating() { return g_presenter && g_presenter->fgGenerating(); }
bool framegenInputs(int renderW, int renderH, FgInputs* out) { return g_presenter && g_presenter->fgInputs(renderW, renderH, out); }
void framegenParams(const FgParams& p) { if (g_presenter) g_presenter->fgSetParams(p); }
void presentFrameStart(long long ns) { if (g_presenter) g_presenter->setFrameStart(ns); }

// Called once with the GL context current (first frame): enables D3D12 presentation when WFC_D3D12PRESENT is set.
void initD3D12PresentIfRequested() {
    static bool tried = false;
    if (tried) return;
    tried = true;
    if (!std::getenv("WFC_D3D12PRESENT") && !std::getenv("WFC_FRAMEGEN") && !g_fgAtStartup) return;   // (FG presents through D3D12)
    auto* p = new D3D12Presenter();
    if (!p->init()) { delete p; LOG_WARN("PRESENT: GL (D3D12 present unavailable)"); return; }
    g_presenter = p;
    platform::setPresentOverride(&presentHook);
}

}  // namespace render
#endif
