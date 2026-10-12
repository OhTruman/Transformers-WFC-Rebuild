// Clean-room reconstruction — hybrid-GPU preference (PC ADAPTATION, Systems; M5 D prep). On laptops with an integrated
// and a discrete GPU, the NVIDIA (Optimus) and AMD (switchable graphics / PowerXpress) drivers read these exports from the
// exe and run it on the discrete GPU; without them the GL context can be created on the integrated GPU. The GL <-> D3D12
// interop then also needs D3D12 on the same adapter (render/d3d12/D3D12Adapter.cpp). No effect with a single GPU.
#include <windows.h>

extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
