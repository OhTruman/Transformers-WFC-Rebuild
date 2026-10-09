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
#include <GL/gl.h>

#include "platform/PresentHook.h"
#include "core/Log.h"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>

namespace render {
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

constexpr GLenum kHandleD3D12Resource = 0x958A, kHandleD3D12Fence = 0x9594, kDedicated = 0x9581, kFenceValue = 0x9595;
constexpr GLenum kLayoutTransferDst = 0x9593 /*GL_LAYOUT_TRANSFER_DST_EXT*/, kLayoutTransferSrc = 0x9592;
constexpr UINT kBuffers = 2;
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
        return createMem && importMem && texStorageMem && genSem && importSem && semParam && waitSem && signalSem && genFb && bindFb && fbTex && blit;
    }
};

class D3D12Presenter {
public:
    bool init() {
        if (!gl_.load()) { LOG_WARN("D3D12 present: GL external-object entry points missing: GL present kept"); return false; }
        hwnd_ = WindowFromDC(wglGetCurrentDC());
        if (!hwnd_) { LOG_WARN("D3D12 present: no window: GL present kept"); return false; }
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
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(dev_->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&queue_))) return false;
        for (UINT i = 0; i < kBuffers; ++i)
            if (FAILED(dev_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&alloc_[i]))) return false;
        if (FAILED(dev_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc_[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&list_))) return false;
        list_->Close();
        if (FAILED(dev_->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), (void**)&fence_))) return false;
        if (FAILED(dev_->CreateSharedHandle(fence_, nullptr, GENERIC_ALL, nullptr, &fenceHandle_))) return false;
        gl_.genSem(1, &sem_);
        gl_.importSem(sem_, kHandleD3D12Fence, fenceHandle_);
        RECT rc; GetClientRect(hwnd_, &rc);
        if (!createSwapchain(std::max<LONG>(1, rc.right - rc.left), std::max<LONG>(1, rc.bottom - rc.top))) return false;
        LOG_INFO("PRESENT: D3D12 (flip model, %u buffers, %u shared, latency-waitable, %s) over the GL renderer (%dx%d)", kBuffers, kRing,
                 vsync_ ? "v-sync" : tearing_ ? "immediate + tearing" : "immediate", w_, h_);
        return true;
    }

    // the platform hook: copy the GL back buffer to the shared texture, D3D12 copies + presents
    bool present() {
        using clk = std::chrono::steady_clock;
        const clk::time_point t0 = clk::now();
        RECT rc; GetClientRect(hwnd_, &rc);
        const int w = std::max<LONG>(1, rc.right - rc.left), h = std::max<LONG>(1, rc.bottom - rc.top);
        if (w != w_ || h != h_) { waitIdle(); if (!resize(w, h)) return false; }
        const UINT slot = (UINT)(presented_ % kRing);
        // variants (A/B): WFC_D3D12_NOGLWAIT=1 skips the GL-side semaphore wait (relies on the ring age + the CPU
        // latency wait); WFC_D3D12_NOFLUSH=1 skips glFlush after the signal
        static const bool noGlWait = std::getenv("WFC_D3D12_NOGLWAIT") != nullptr;
        static const bool noFlush = std::getenv("WFC_D3D12_NOFLUSH") != nullptr;
        // GL waits only until D3D12 finished reading THIS ring slot (kRing frames ago: normally long done)
        if (readDone_[slot] && !noGlWait) {
            const GLuint64_ v = readDone_[slot];
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
        const clk::time_point tB = clk::now();
        ++value_;
        {
            const GLuint64_ v = value_;
            gl_.semParam(sem_, kFenceValue, &v);
            const GLenum layout = kLayoutTransferSrc;
            gl_.signalSem(sem_, 0, nullptr, 1, &glTex_[slot], &layout);
        }
        const clk::time_point tC = clk::now();
        if (!noFlush) glFlush();
        const clk::time_point t1 = clk::now();
        {
            subSum_[0] += std::chrono::duration<double, std::milli>(tA - t0).count();
            subSum_[1] += std::chrono::duration<double, std::milli>(tB - tA).count();
            subSum_[2] += std::chrono::duration<double, std::milli>(tC - tB).count();
            subSum_[3] += std::chrono::duration<double, std::milli>(t1 - tC).count();
        }
        // D3D12: wait for GL, copy into the back buffer, present, signal
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
        const HRESULT hr = swap_->Present(vsync_ ? 1 : 0, !vsync_ && tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0);
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
                LOG_INFO("PRESENT D3D12 GL sub-phases (avg ms): resize + semaphore wait %.3f, blit %.3f, semaphore signal %.3f, glFlush %.3f",
                         subSum_[0] / 300, subSum_[1] / 300, subSum_[2] / 300, subSum_[3] / 300);
                for (int k = 0; k < 4; ++k) subSum_[k] = 0;
                for (int k = 0; k < 5; ++k) { phSum_[k] = 0; phMax_[k] = 0; }
                phN_ = 0;
            }
        }
        if (FAILED(hr)) { LOG_WARN("D3D12 present: Present failed (0x%08lx)", (unsigned long)hr); }
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

private:
    bool createSwapchain(int w, int h) {
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = (UINT)w; sd.Height = (UINT)h; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = kBuffers; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Flags = (tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0) | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        IDXGISwapChain1* sc1 = nullptr;
        if (FAILED(factory_->CreateSwapChainForHwnd(queue_, hwnd_, &sd, nullptr, nullptr, &sc1)) || !sc1) {
            LOG_WARN("D3D12 present: CreateSwapChainForHwnd failed (the GL window may not accept a flip-model swapchain)");
            return false;
        }
        factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
        sc1->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&swap_);
        rel(sc1);
        if (swap_) { swap_->SetMaximumFrameLatency(2); waitable_ = swap_->GetFrameLatencyWaitableObject(); }
        return resize(w, h, false);
    }
    bool resize(int w, int h, bool resizeBuffers = true) {
        for (auto& b : back_) rel(b);
        if (resizeBuffers && FAILED(swap_->ResizeBuffers(kBuffers, (UINT)w, (UINT)h, DXGI_FORMAT_R8G8B8A8_UNORM,
                                                         (tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0) | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))) return false;
        for (UINT i = 0; i < kBuffers; ++i) swap_->GetBuffer(i, __uuidof(ID3D12Resource), (void**)&back_[i]);
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
                                                     __uuidof(ID3D12Resource), (void**)&shared_[k]))) return false;
            if (FAILED(dev_->CreateSharedHandle(shared_[k], nullptr, GENERIC_ALL, nullptr, &sharedHandle_[k]))) return false;
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
        w_ = w; h_ = h;
        for (auto& v : frameValue_) v = 0;
        return glGetError() == GL_NO_ERROR;
    }
    void throttle(UINT bi) {                           // at most kBuffers frames in flight on the D3D12 side
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
    ID3D12CommandAllocator* alloc_[kBuffers] = {};
    ID3D12GraphicsCommandList* list_ = nullptr;
    IDXGISwapChain3* swap_ = nullptr;
    ID3D12Resource* back_[kBuffers] = {};
    ID3D12Resource* shared_[kRing] = {};
    HANDLE sharedHandle_[kRing] = {}, fenceHandle_ = nullptr, waitable_ = nullptr;
    UINT64 readDone_[kRing] = {};
    ID3D12Fence* fence_ = nullptr;
    UINT64 value_ = 0, frameValue_[kBuffers] = {};
    GLuint glMem_[kRing] = {}, glTex_[kRing] = {}, glFbo_[kRing] = {}, sem_ = 0;
    int w_ = 0, h_ = 0, presented_ = 0;
    double phSum_[5] = {}, phMax_[5] = {}, subSum_[4] = {};
    int phN_ = 0;
    bool tearing_ = false, vsync_ = false;
    static std::string shotFile_() { const char* e = std::getenv("WFC_D3D12SHOT"); std::string t = e ? e : ""; size_t c = t.find(','); return c == std::string::npos ? t : t.substr(c + 1); }
};

D3D12Presenter* g_presenter = nullptr;
bool presentHook() { return g_presenter && g_presenter->present(); }

}  // namespace

// Called once with the GL context current (first frame): enables D3D12 presentation when WFC_D3D12PRESENT is set.
void initD3D12PresentIfRequested() {
    static bool tried = false;
    if (tried) return;
    tried = true;
    if (!std::getenv("WFC_D3D12PRESENT")) return;
    auto* p = new D3D12Presenter();
    if (!p->init()) { delete p; LOG_WARN("PRESENT: GL (D3D12 present unavailable)"); return; }
    g_presenter = p;
    platform::setPresentOverride(&presentHook);
}

}  // namespace render
#endif
