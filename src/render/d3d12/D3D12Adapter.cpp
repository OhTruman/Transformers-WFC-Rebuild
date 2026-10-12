// Clean-room reconstruction — see D3D12Adapter.h.
#include "render/d3d12/D3D12Adapter.h"

#include <windows.h>
#include <dxgi1_4.h>
#include <GL/gl.h>

#include "core/Log.h"

#include <cstring>
#include <mutex>
#include <string>

namespace render {

namespace {

constexpr GLenum kGlDeviceLuid = 0x9599;   // GL_DEVICE_LUID_EXT (EXT_external_objects_win32), 8 bytes
typedef void(APIENTRY* PFN_GetUnsignedBytev)(GLenum, GLubyte*);

std::string narrow(const wchar_t* w) {
    char buf[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, (int)sizeof buf - 1, nullptr, nullptr);
    return buf;
}

IDXGIAdapter1* g_adapter = nullptr;   // the match, kept for the process (AddRef'ed per caller)
std::once_flag g_once;

void resolve() {
    const char* glName = (const char*)glGetString(GL_RENDERER);
    const std::string gl = glName ? glName : "?";
    auto getUb = (PFN_GetUnsignedBytev)(void*)wglGetProcAddress("glGetUnsignedBytevEXT");
    // Only with GL_EXT_memory_object_win32 (the extension that defines the LUID query; the compatibility context lists it in
    // GL_EXTENSIONS); afterwards any GL error the query raised is drained, so a later glGetError check is not misled.
    const char* ext = (const char*)glGetString(GL_EXTENSIONS);
    const bool hasLuidQuery = getUb && ext && std::strstr(ext, "GL_EXT_memory_object_win32") != nullptr;
    unsigned char luidBytes[8] = {};
    bool haveLuid = false;
    if (hasLuidQuery) {
        getUb(kGlDeviceLuid, luidBytes);
        while (glGetError() != GL_NO_ERROR) {}
        for (unsigned char b : luidBytes) haveLuid = haveLuid || b != 0;
    }
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    typedef HRESULT(WINAPI * PFN_CF1)(REFIID, void**);
    auto cf1 = dxgi ? (PFN_CF1)(void*)GetProcAddress(dxgi, "CreateDXGIFactory1") : nullptr;
    IDXGIFactory4* factory = nullptr;
    if (!cf1 || FAILED(cf1(__uuidof(IDXGIFactory4), (void**)&factory)) || !factory) {
        LOG_INFO("GPU: GL on '%s'; no DXGI factory: D3D12 on its default adapter", gl.c_str());
        return;
    }
    LUID luid{};
    std::memcpy(&luid, luidBytes, sizeof luid);
    if (haveLuid && SUCCEEDED(factory->EnumAdapterByLuid(luid, __uuidof(IDXGIAdapter1), (void**)&g_adapter)) && g_adapter) {
        DXGI_ADAPTER_DESC1 d{};
        g_adapter->GetDesc1(&d);
        LOG_INFO("GPU: GL on '%s' (LUID %08lx:%08lx) -> D3D12 on '%s' (matched by LUID)", gl.c_str(), (unsigned long)luid.HighPart,
                 (unsigned long)luid.LowPart, narrow(d.Description).c_str());
    } else {
        IDXGIAdapter1* def = nullptr;
        DXGI_ADAPTER_DESC1 d{};
        if (SUCCEEDED(factory->EnumAdapters1(0, &def)) && def) { def->GetDesc1(&d); def->Release(); }
        LOG_INFO("GPU: GL on '%s' (%s) -> D3D12 on its default adapter '%s'", gl.c_str(),
                 haveLuid ? "its LUID matches no DXGI adapter" : "no GL device LUID", narrow(d.Description).c_str());
        g_adapter = nullptr;
    }
    factory->Release();
}

}  // namespace

IDXGIAdapter1* d3d12AdapterForGL() {
    std::call_once(g_once, resolve);
    if (g_adapter) g_adapter->AddRef();
    return g_adapter;
}

}  // namespace render
