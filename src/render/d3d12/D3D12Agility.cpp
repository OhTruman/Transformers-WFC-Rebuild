// Clean-room reconstruction — D3D12 Agility SDK selection (PC ADAPTATION, M5 B). FSR 3.1 needs shader model 6.6; the
// in-box Windows 10 runtime stops at 6.5. When D3D12\D3D12Core.dll (tools/render/fetch_d3d12_agility.py) sits beside the
// exe, d3d12SelectAgility() asks the D3D12 loader for it (ID3D12SDKConfiguration::SetSDKVersion) before the process'
// first device; otherwise nothing changes and the in-box runtime is used. (No static D3D12SDKVersion / D3D12SDKPath
// exports: with those, a missing folder makes every D3D12CreateDevice fail with D3D12_ERROR_INVALID_REDIST.)
// The selection itself runs at start-up whenever the runtime is shipped (a static initialiser, see selectEarly); only the
// opt-in D3D12 present / FSR 3 paths create devices.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include "core/Log.h"

#include <string>

namespace render {

// CLSID_D3D12SDKConfiguration (d3d12.h; defined here so no GUID library is needed)
static const GUID kClsidD3D12SDKConfiguration = {0x7cda6aca, 0xa03e, 0x49c8, {0x94, 0x58, 0x03, 0x34, 0xd2, 0x0e, 0x07, 0xce}};

namespace {
// 0 = not tried, 1 = no runtime beside the exe, 2 = selected, 3 = refused (hr)
int g_state = 0;
long g_hr = 0;

// Silent (runs from a static initialiser, before the log exists): the selection has to happen before anything in the
// process loads the in-box D3D12 runtime - the AMD OpenGL driver does for fullscreen presentation, after which
// SetSDKVersion fails with D3D12_ERROR_INVALID_REDIST and FSR 3 would fall back to FSR 1
void selectEarly() {
#if defined(WFC_D3D12_AGILITY)
    if (g_state) return;
    char exe[MAX_PATH * 2] = "";
    GetModuleFileNameA(nullptr, exe, sizeof exe);
    std::string dir(exe);
    dir = dir.substr(0, dir.find_last_of("\\/") + 1);
    if (GetFileAttributesA((dir + "D3D12\\D3D12Core.dll").c_str()) == INVALID_FILE_ATTRIBUTES) { g_state = 1; return; }
    HMODULE d3d = LoadLibraryW(L"d3d12.dll");
    typedef HRESULT(WINAPI * PFN_GetInterface)(REFCLSID, REFIID, void**);
    auto gi = d3d ? (PFN_GetInterface)(void*)GetProcAddress(d3d, "D3D12GetInterface") : nullptr;
    ID3D12SDKConfiguration* cfg = nullptr;
    HRESULT hr = gi ? gi(kClsidD3D12SDKConfiguration, __uuidof(ID3D12SDKConfiguration), (void**)&cfg) : E_NOINTERFACE;
    if (SUCCEEDED(hr) && cfg) { hr = cfg->SetSDKVersion(WFC_D3D12_AGILITY, ".\\D3D12\\"); cfg->Release(); }
    g_hr = (long)hr;
    g_state = SUCCEEDED(hr) ? 2 : 3;
#endif
}
#if defined(WFC_D3D12_AGILITY)
struct EarlySelect { EarlySelect() { selectEarly(); } } g_earlySelect;   // only acts when D3D12/D3D12Core.dll is shipped
#endif
}  // namespace

void d3d12SelectAgility() {
    selectEarly();
    static bool logged = false;
    if (logged) return;
    logged = true;
#if defined(WFC_D3D12_AGILITY)
    if (g_state == 1) LOG_INFO("D3D12: Agility runtime not beside the exe (D3D12/D3D12Core.dll): in-box runtime");
    else if (g_state == 2) LOG_INFO("D3D12: Agility runtime selected at start-up (SDK version %u, D3D12/ beside the exe)", (unsigned)WFC_D3D12_AGILITY);
    else LOG_WARN("D3D12: Agility runtime not selected (0x%08lx): in-box runtime", (unsigned long)g_hr);
#endif
}

}  // namespace render
#else
namespace render { void d3d12SelectAgility() {} }
#endif
