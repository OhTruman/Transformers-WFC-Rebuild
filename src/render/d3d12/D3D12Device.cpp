// Clean-room reconstruction — see D3D12Device.h.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include "render/d3d12/D3D12Device.h"
#include "render/d3d12/D3D12Adapter.h"
#include "core/Log.h"

#include <mutex>

namespace render {
void d3d12SelectAgility();   // D3D12Agility.cpp

namespace {
std::mutex g_mx;
ID3D12Device* g_dev = nullptr;      // the shared device (one reference held here)
int g_created = 0;
bool g_failedLogged = false;
}  // namespace

bool d3d12DeviceRemoved(ID3D12Device* dev) { return dev && dev->GetDeviceRemovedReason() != S_OK; }

ID3D12Device* d3d12SharedDevice() {
    std::lock_guard<std::mutex> lk(g_mx);
    if (g_dev && d3d12DeviceRemoved(g_dev)) {          // removed: dropped (its holders release their own references)
        LOG_WARN("D3D12: the shared device was removed (reason 0x%08lx): a new one for the next caller",
                 (unsigned long)g_dev->GetDeviceRemovedReason());
        g_dev->Release();
        g_dev = nullptr;
    }
    if (!g_dev) {
        d3d12SelectAgility();
        HMODULE d3d = LoadLibraryW(L"d3d12.dll");
        auto create = d3d ? (PFN_D3D12_CREATE_DEVICE)(void*)GetProcAddress(d3d, "D3D12CreateDevice") : nullptr;
        IDXGIAdapter1* adapter = d3d12AdapterForGL();   // GL's adapter (hybrid laptops); nullptr = the default
        const HRESULT hr = create ? create(adapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&g_dev) : E_FAIL;
        if (adapter) adapter->Release();
        if (FAILED(hr) || !g_dev) {
            g_dev = nullptr;
            if (!g_failedLogged) { g_failedLogged = true; LOG_WARN("D3D12: no device (D3D12CreateDevice 0x%08lx)", (unsigned long)hr); }
            return nullptr;
        }
        ++g_created;
        LOG_INFO("D3D12: shared device %s (presenter, FSR 3.1 and RT share it)", g_created == 1 ? "created" : "re-created after a removal");
    }
    g_dev->AddRef();
    return g_dev;
}

}  // namespace render
#else
#include "render/d3d12/D3D12Device.h"
namespace render {
ID3D12Device* d3d12SharedDevice() { return nullptr; }
bool d3d12DeviceRemoved(ID3D12Device*) { return false; }
}  // namespace render
#endif
