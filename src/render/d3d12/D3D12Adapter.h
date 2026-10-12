// Clean-room reconstruction — the D3D12 adapter that matches the GL context (PC ADAPTATION, Systems; M5 D prep).
#pragma once

struct IDXGIAdapter1;

namespace render {

// The DXGI adapter the current GL context renders on (GL_EXT_external_objects_win32: GL_DEVICE_LUID_EXT), AddRef'ed, for
// D3D12CreateDevice - the GL <-> D3D12 interop needs both APIs on one adapter, and on a hybrid laptop the default adapter
// need not be GL's. nullptr = D3D12's default adapter (no LUID from GL, or no adapter with it): the behaviour before.
// The GL context must be current. The first call logs the GL renderer, its LUID and the adapter chosen.
IDXGIAdapter1* d3d12AdapterForGL();

}  // namespace render
