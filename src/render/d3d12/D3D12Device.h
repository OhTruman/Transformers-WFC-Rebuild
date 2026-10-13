// Clean-room reconstruction — the one D3D12 device the optional D3D12 paths share (PC ADAPTATION; Rendering): the
// presenter (D3D12 present / FSR 3 frame generation), FSR 3.1 and the RT bridge used to create a device each. Created on
// first use, after the Agility selection, on GL's adapter (d3d12AdapterForGL: hybrid laptops); every caller gets its own
// reference (AddRef'ed) and releases it. A removed device is never handed out again: the next caller gets a new one (the
// presenter's device-loss recovery, FSR 3.1 rebuilding on it). The GL context must be current for the first call.
#pragma once

struct ID3D12Device;

namespace render {

ID3D12Device* d3d12SharedDevice();          // AddRef'ed; nullptr = no D3D12 device (logged once)
bool d3d12DeviceRemoved(ID3D12Device* dev); // GetDeviceRemovedReason() != S_OK (nullptr: false)

}  // namespace render
