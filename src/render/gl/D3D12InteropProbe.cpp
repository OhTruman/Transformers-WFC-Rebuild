// Clean-room reconstruction — capability probe for the optional upscaler / frame-generation / ray-tracing path
// (PC ADAPTATION, A3a): can the GL renderer share images and synchronisation with Direct3D 12 on this machine?
// D3D12 clears a shared texture to a known colour and signals a shared fence; GL imports the texture memory
// (GL_EXT_memory_object_win32, D3D12 resource handle) and the fence (GL_EXT_semaphore_win32, D3D12 fence handle),
// waits on it and reads the pixels back. Run with WFC_D3D12PROBE=1 (logged as "D3D12 INTEROP PROBE: ...").
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <GL/gl.h>

#include "core/Log.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace render {
typedef uint64_t GLuint64;
namespace {

typedef void(APIENTRY* PFN_CreateMemoryObjectsEXT)(GLsizei, GLuint*);
typedef void(APIENTRY* PFN_DeleteMemoryObjectsEXT)(GLsizei, const GLuint*);
typedef void(APIENTRY* PFN_ImportMemoryWin32HandleEXT)(GLuint, GLuint64, GLenum, void*);
typedef void(APIENTRY* PFN_MemoryObjectParameterivEXT)(GLuint, GLenum, const GLint*);
typedef void(APIENTRY* PFN_TexStorageMem2DEXT)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, GLuint64);
typedef void(APIENTRY* PFN_GenSemaphoresEXT)(GLsizei, GLuint*);
typedef void(APIENTRY* PFN_DeleteSemaphoresEXT)(GLsizei, const GLuint*);
typedef void(APIENTRY* PFN_ImportSemaphoreWin32HandleEXT)(GLuint, GLenum, void*);
typedef void(APIENTRY* PFN_SemaphoreParameterui64vEXT)(GLuint, GLenum, const GLuint64*);
typedef void(APIENTRY* PFN_WaitSemaphoreEXT)(GLuint, GLuint, const GLuint*, GLuint, const GLuint*, const GLenum*);
typedef void(APIENTRY* PFN_GetTexImage)(GLenum, GLint, GLenum, GLenum, void*);

constexpr GLenum kHandleD3D12Resource = 0x958A;   // GL_HANDLE_TYPE_D3D12_RESOURCE_EXT
constexpr GLenum kHandleD3D12Fence = 0x9594;      // GL_HANDLE_TYPE_D3D12_FENCE_EXT
constexpr GLenum kDedicatedMemory = 0x9581;       // GL_DEDICATED_MEMORY_OBJECT_EXT
constexpr GLenum kD3D12FenceValue = 0x9595;       // GL_D3D12_FENCE_VALUE_EXT
constexpr GLenum kLayoutColorAttachment = 0x958E; // GL_LAYOUT_COLOR_ATTACHMENT_EXT

template <class T> void rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

}  // namespace

bool probeD3D12Interop(std::string& detail) {
    auto gl = [](const char* n) { return (void*)wglGetProcAddress(n); };
    auto createMem = (PFN_CreateMemoryObjectsEXT)gl("glCreateMemoryObjectsEXT");
    auto deleteMem = (PFN_DeleteMemoryObjectsEXT)gl("glDeleteMemoryObjectsEXT");
    auto importMem = (PFN_ImportMemoryWin32HandleEXT)gl("glImportMemoryWin32HandleEXT");
    auto memParam = (PFN_MemoryObjectParameterivEXT)gl("glMemoryObjectParameterivEXT");
    auto texStorageMem = (PFN_TexStorageMem2DEXT)gl("glTexStorageMem2DEXT");
    auto genSem = (PFN_GenSemaphoresEXT)gl("glGenSemaphoresEXT");
    auto deleteSem = (PFN_DeleteSemaphoresEXT)gl("glDeleteSemaphoresEXT");
    auto importSem = (PFN_ImportSemaphoreWin32HandleEXT)gl("glImportSemaphoreWin32HandleEXT");
    auto semParam = (PFN_SemaphoreParameterui64vEXT)gl("glSemaphoreParameterui64vEXT");
    auto waitSem = (PFN_WaitSemaphoreEXT)gl("glWaitSemaphoreEXT");
    if (!createMem || !importMem || !texStorageMem || !genSem || !importSem || !waitSem || !semParam) {
        detail = "GL external-object entry points missing";
        return false;
    }
    HMODULE d3d = LoadLibraryW(L"d3d12.dll");
    auto create = d3d ? (PFN_D3D12_CREATE_DEVICE)(void*)GetProcAddress(d3d, "D3D12CreateDevice") : nullptr;
    ID3D12Device* dev = nullptr;
    if (!create || FAILED(create(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&dev)) || !dev) {
        detail = "no D3D12 device (feature level 12_0)";
        return false;
    }
    bool ok = false;
    ID3D12CommandQueue* q = nullptr; ID3D12CommandAllocator* alloc = nullptr; ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Resource* tex = nullptr; ID3D12DescriptorHeap* rtvHeap = nullptr; ID3D12Fence* fence = nullptr;
    HANDLE texHandle = nullptr, fenceHandle = nullptr;
    GLuint mem = 0, glTex = 0, sem = 0;
    const int W = 64, H = 64;
    do {
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&q))) { detail = "CreateCommandQueue"; break; }
        if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&alloc))) { detail = "CreateCommandAllocator"; break; }
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = W; rd.Height = H; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        D3D12_CLEAR_VALUE cv{}; cv.Format = rd.Format; cv.Color[0] = 0.25f; cv.Color[1] = 0.5f; cv.Color[2] = 0.75f; cv.Color[3] = 1.0f;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                __uuidof(ID3D12Resource), (void**)&tex))) { detail = "CreateCommittedResource (shared)"; break; }
        D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors = 1;
        if (FAILED(dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&rtvHeap))) { detail = "CreateDescriptorHeap"; break; }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        dev->CreateRenderTargetView(tex, nullptr, rtv);
        if (FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&list))) { detail = "CreateCommandList"; break; }
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = tex;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1, &b);
        list->ClearRenderTargetView(rtv, cv.Color, 0, nullptr);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        list->ResourceBarrier(1, &b);
        list->Close();
        if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), (void**)&fence))) { detail = "CreateFence (shared)"; break; }
        if (FAILED(dev->CreateSharedHandle(tex, nullptr, GENERIC_ALL, nullptr, &texHandle)) ||
            FAILED(dev->CreateSharedHandle(fence, nullptr, GENERIC_ALL, nullptr, &fenceHandle))) { detail = "CreateSharedHandle"; break; }
        // GL side: the memory as a texture, the fence as a semaphore
        while (glGetError() != GL_NO_ERROR) {}
        createMem(1, &mem);
        GLint dedicated = GL_TRUE;
        if (memParam) memParam(mem, kDedicatedMemory, &dedicated);
        const D3D12_RESOURCE_ALLOCATION_INFO ai = dev->GetResourceAllocationInfo(0, 1, &rd);
        importMem(mem, ai.SizeInBytes, kHandleD3D12Resource, texHandle);
        glGenTextures(1, &glTex);
        glBindTexture(GL_TEXTURE_2D, glTex);
        texStorageMem(GL_TEXTURE_2D, 1, 0x8058 /*GL_RGBA8*/, W, H, mem, 0);
        GLenum e1 = glGetError();
        genSem(1, &sem);
        importSem(sem, kHandleD3D12Fence, fenceHandle);
        GLenum e2 = glGetError();
        if (e1 != GL_NO_ERROR || e2 != GL_NO_ERROR) { detail = "GL import failed (memory err 0x" + std::to_string(e1) + ", fence err 0x" + std::to_string(e2) + ")"; break; }
        // D3D12: clear, then signal 1; GL: wait for 1, then read back
        ID3D12CommandList* lists[] = {list};
        q->ExecuteCommandLists(1, lists);
        q->Signal(fence, 1);
        const GLuint64 one = 1;
        semParam(sem, kD3D12FenceValue, &one);
        const GLenum layout = kLayoutColorAttachment;
        waitSem(sem, 0, nullptr, 1, &glTex, &layout);
        std::vector<uint8_t> px((size_t)W * H * 4, 0);
        auto getTex = (PFN_GetTexImage)(void*)GetProcAddress(GetModuleHandleW(L"opengl32.dll"), "glGetTexImage");
        if (!getTex) { detail = "glGetTexImage missing"; break; }
        getTex(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        const uint8_t* c = &px[((size_t)(H / 2) * W + W / 2) * 4];
        const bool match = std::abs((int)c[0] - 64) <= 1 && std::abs((int)c[1] - 128) <= 1 && std::abs((int)c[2] - 191) <= 1;
        detail = std::string("read back (") + std::to_string(c[0]) + "," + std::to_string(c[1]) + "," + std::to_string(c[2]) +
                 ") vs D3D12 clear (64,128,191): " + (match ? "MATCH" : "MISMATCH");
        ok = match;
    } while (false);
    if (glTex) glDeleteTextures(1, &glTex);
    if (mem && deleteMem) deleteMem(1, &mem);
    if (sem && deleteSem) deleteSem(1, &sem);
    if (texHandle) CloseHandle(texHandle);
    if (fenceHandle) CloseHandle(fenceHandle);
    rel(list); rel(fence); rel(rtvHeap); rel(tex); rel(alloc); rel(q); rel(dev);
    return ok;
}

}  // namespace render
#endif
