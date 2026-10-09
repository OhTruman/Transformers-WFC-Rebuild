// Clean-room reconstruction — GPU facts for graphics-settings auto-detection (PC ADAPTATION, user decision: the
// first launch picks the best look the PC can hold). Read once: vendor / renderer from the GL context, dedicated VRAM
// from DXGI (vendor-neutral; GL_NVX_gpu_memory_info / GL_ATI_meminfo as fallbacks), Vulkan 1.x presence and the
// ray-tracing device extensions from the Vulkan loader (run-time bound: nothing extra linked).
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#ifndef VKAPI_PTR
#define VKAPI_PTR __stdcall
#endif

#include "render/Renderer.h"
#include "core/Log.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace render {
namespace {

// ---- DXGI (dxgi.dll, run time) ----
struct DXGI_ADAPTER_DESC1_ {
    WCHAR Description[128]; UINT VendorId, DeviceId, SubSysId, Revision;
    SIZE_T DedicatedVideoMemory, DedicatedSystemMemory, SharedSystemMemory; LUID AdapterLuid; UINT Flags;
};
struct IDXGIAdapter1_;
struct IDXGIFactory1_;
// COM vtable layouts (IUnknown 3, IDXGIObject 4, IDXGIFactory 5, IDXGIFactory1: EnumAdapters1 = slot 12)
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory1)(REFIID, void**);
int dxgiVramMB(UINT& vendorOut) {
    HMODULE m = LoadLibraryW(L"dxgi.dll");
    if (!m) return 0;
    auto create = (PFN_CreateDXGIFactory1)(void*)GetProcAddress(m, "CreateDXGIFactory1");
    static const GUID kIID_IDXGIFactory1 = {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};
    void* f = nullptr;
    if (!create || FAILED(create(kIID_IDXGIFactory1, &f)) || !f) return 0;
    void** vt = *(void***)f;
    typedef HRESULT(WINAPI* PFN_Enum1)(void*, UINT, void**);
    typedef ULONG(WINAPI* PFN_Release)(void*);
    auto enum1 = (PFN_Enum1)vt[12];
    int best = 0;
    for (UINT i = 0;; ++i) {
        void* a = nullptr;
        if (FAILED(enum1(f, i, &a)) || !a) break;
        void** avt = *(void***)a;
        typedef HRESULT(WINAPI* PFN_GetDesc1)(void*, DXGI_ADAPTER_DESC1_*);
        DXGI_ADAPTER_DESC1_ d{};
        if (SUCCEEDED(((PFN_GetDesc1)avt[10])(a, &d)) && !(d.Flags & 2 /*software*/)) {
            const int mb = (int)(d.DedicatedVideoMemory / (1024 * 1024));
            if (mb > best) { best = mb; vendorOut = d.VendorId; }
        }
        ((PFN_Release)avt[2])(a);
    }
    ((PFN_Release)vt[2])(f);
    return best;
}

// ---- Vulkan loader (vulkan-1.dll, run time; the few structs needed, from the Vulkan 1.0 headers' layout) ----
typedef struct VkApplicationInfo_ { int sType; const void* pNext; const char* pApplicationName; uint32_t applicationVersion;
                                   const char* pEngineName; uint32_t engineVersion; uint32_t apiVersion; } VkApplicationInfo_;
typedef struct VkInstanceCreateInfo_ { int sType; const void* pNext; uint32_t flags; const VkApplicationInfo_* pApplicationInfo;
                                      uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames;
                                      uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames; } VkInstanceCreateInfo_;
typedef struct VkExtensionProperties_ { char extensionName[256]; uint32_t specVersion; } VkExtensionProperties_;
typedef void* VkInstance_; typedef void* VkPhysicalDevice_;
typedef int(VKAPI_PTR* PFN_vkEnumerateInstanceVersion_)(uint32_t*);
typedef int(VKAPI_PTR* PFN_vkCreateInstance_)(const VkInstanceCreateInfo_*, const void*, VkInstance_*);
typedef void(VKAPI_PTR* PFN_vkDestroyInstance_)(VkInstance_, const void*);
typedef int(VKAPI_PTR* PFN_vkEnumeratePhysicalDevices_)(VkInstance_, uint32_t*, VkPhysicalDevice_*);
typedef int(VKAPI_PTR* PFN_vkEnumerateDeviceExtensionProperties_)(VkPhysicalDevice_, const char*, uint32_t*, VkExtensionProperties_*);
typedef void*(VKAPI_PTR* PFN_vkGetInstanceProcAddr_)(VkInstance_, const char*);

void vulkanFacts(std::string& version, bool& rt, bool& interop) {
    HMODULE m = LoadLibraryW(L"vulkan-1.dll");
    if (!m) return;
    auto gipa = (PFN_vkGetInstanceProcAddr_)(void*)GetProcAddress(m, "vkGetInstanceProcAddr");
    if (!gipa) return;
    auto ver = (PFN_vkEnumerateInstanceVersion_)gipa(nullptr, "vkEnumerateInstanceVersion");
    uint32_t v = (1u << 22);
    if (ver) ver(&v);
    version = std::to_string(v >> 22) + "." + std::to_string((v >> 12) & 0x3ff) + "." + std::to_string(v & 0xfff);
    auto create = (PFN_vkCreateInstance_)gipa(nullptr, "vkCreateInstance");
    if (!create) return;
    VkApplicationInfo_ app{0 /*VK_STRUCTURE_TYPE_APPLICATION_INFO*/, nullptr, "wfc_rebuild", 1, "wfc", 1, (1u << 22) | (2u << 12)};
    VkInstanceCreateInfo_ ci{1 /*VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO*/, nullptr, 0, &app, 0, nullptr, 0, nullptr};
    VkInstance_ inst = nullptr;
    if (create(&ci, nullptr, &inst) != 0 || !inst) return;
    auto enumDev = (PFN_vkEnumeratePhysicalDevices_)gipa(inst, "vkEnumeratePhysicalDevices");
    auto enumExt = (PFN_vkEnumerateDeviceExtensionProperties_)gipa(inst, "vkEnumerateDeviceExtensionProperties");
    auto destroy = (PFN_vkDestroyInstance_)gipa(inst, "vkDestroyInstance");
    uint32_t n = 0;
    if (enumDev && enumExt && enumDev(inst, &n, nullptr) == 0 && n) {
        std::vector<VkPhysicalDevice_> devs(n);
        enumDev(inst, &n, devs.data());
        for (VkPhysicalDevice_ d : devs) {
            uint32_t e = 0;
            if (enumExt(d, nullptr, &e, nullptr) != 0 || !e) continue;
            std::vector<VkExtensionProperties_> ext(e);
            enumExt(d, nullptr, &e, ext.data());
            bool rtp = false, as = false, mem = false, sem = false;
            for (const auto& x : ext) {
                rtp |= !std::strcmp(x.extensionName, "VK_KHR_ray_tracing_pipeline");
                as |= !std::strcmp(x.extensionName, "VK_KHR_acceleration_structure");
                mem |= !std::strcmp(x.extensionName, "VK_KHR_external_memory_win32");
                sem |= !std::strcmp(x.extensionName, "VK_KHR_external_semaphore_win32");
            }
            rt |= rtp && as;
            interop |= mem && sem;
        }
    }
    if (destroy) destroy(inst, nullptr);
}

}  // namespace

// Called with the renderer's GL context current.
GpuFacts readGpuFacts() {
    GpuFacts f;
    if (const char* s = (const char*)glGetString(GL_VENDOR)) f.vendor = s;
    if (const char* s = (const char*)glGetString(GL_RENDERER)) f.renderer = s;
    UINT vid = 0;
    f.vramMB = dxgiVramMB(vid);
    if (!f.vramMB) {                                   // fallbacks: the GL memory-info extensions
        GLint kb[4] = {0, 0, 0, 0};
        glGetIntegerv(0x9047 /*GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX*/, kb);
        if (glGetError() == GL_NO_ERROR && kb[0] > 0) f.vramMB = kb[0] / 1024;
        else {
            glGetIntegerv(0x87FC /*GL_TEXTURE_FREE_MEMORY_ATI*/, kb);
            if (glGetError() == GL_NO_ERROR && kb[0] > 0) f.vramMB = kb[0] / 1024;   // free, not total (approximate)
        }
    }
    f.vendorId = vid;
    vulkanFacts(f.vulkanVersion, f.rayTracing, f.vulkanInterop);
    LOG_INFO("GPU facts: %s / %s, %d MB VRAM, Vulkan %s, ray tracing %s, GL-Vulkan interop %s", f.vendor.c_str(),
             f.renderer.c_str(), f.vramMB, f.vulkanVersion.empty() ? "none" : f.vulkanVersion.c_str(),
             f.rayTracing ? "yes" : "no", f.vulkanInterop ? "yes" : "no");
    return f;
}

}  // namespace render
#endif
