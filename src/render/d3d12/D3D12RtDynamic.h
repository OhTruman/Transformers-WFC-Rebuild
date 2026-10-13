// Clean-room reconstruction — M7 RT, the skinned characters' dynamic geometry (PC ADAPTATION; OFF by default; Systems).
// GL skins the selected characters with a compute pass into ONE buffer shared with D3D12 (float3 per vertex, a region per
// instance slot; render/gl/WfcRtSkin.cpp); the bridge's job (D3D12RtBridge.cpp) records this frame's per-slot BLAS
// builds / refits (rtDynamicRecord) before its TLAS build and appends the characters as instances (rtDynamicAppendInstances).
// BLAS in model space (the model matrix is the instance transform), so a character that moves without a new pose costs
// no refit. All calls on the main thread (the round trip's thread).
#pragma once
#include <cstdint>
#include <vector>

struct ID3D12Device5;
struct ID3D12GraphicsCommandList4;
struct D3D12_RAYTRACING_INSTANCE_DESC;

namespace render {

// ---- the GL side (Pipeline::rtSkinFeed) ----
struct RtDynamicBuffer {
    void* handle = nullptr;       // NT handle of the shared D3D12 buffer (GL imports it: GL_EXT_memory_object_win32)
    uint64_t bytes = 0;           // the allocation size the import takes
    uint32_t capacity = 0;        // float3 vertices
};
bool rtDynamicAvailable();                                // attached to a DXR 1.1 device
// the shared positions buffer (created on first use; stays until shutdown); false: unavailable
bool rtDynamicBuffer(RtDynamicBuffer* out);
// clears (map unload) discard every model and slot: the GL side re-registers when this changes
uint32_t rtDynamicEpoch();
// a skinned model's triangle list (copied; uploaded by the next record); -1: unavailable
int rtDynamicModel(const uint32_t* indices, uint32_t indexCount, uint32_t vertexCount);
void rtDynamicReleaseModel(int model);                   // (its slots released first)
// an instance slot of a model: its BLAS + a region of the shared buffer; -1: the buffer is full
int rtDynamicSlot(int model);
uint32_t rtDynamicFirstVertex(int slot);
void rtDynamicReleaseSlot(int slot);
struct RtDynamicInstance {
    int slot = -1;
    float transform[12];          // model -> world, row-major 3x4 (D3D12_RAYTRACING_INSTANCE_DESC::Transform)
    bool update = false;          // the slot's positions were rewritten this frame: build (first time) or refit its BLAS
    bool rebuild = false;         // (with update) a full build instead of a refit: a refit's quality decays over many poses
};
// this frame's characters in the TLAS (replaces the last call's list); the next record consumes the updates
void rtDynamicFrame(const RtDynamicInstance* inst, int n);
// statistics of the records since the last call: GPU ms of the builds / refits (timestamps, read two records late)
struct RtDynamicStats { int records = 0; long instances = 0, refits = 0, builds = 0, rebuilds = 0; double gpuMs = 0; int gpuSamples = 0;
                        uint32_t slots = 0, verticesUsed = 0, capacity = 0; double blasMB = 0; };
RtDynamicStats rtDynamicTakeStats();
// (WFC_RT_CHARCHECK) the next record copies the slot's first `vertices` positions as D3D12 reads them and, two records
// later, logs whether their FNV-1a 64 hash equals `glHash` (the same bytes as GL read them back after its compute pass)
void rtDynamicCheck(int slot, uint32_t vertices, uint64_t glHash);

// ---- the D3D12 side (the bridge) ----
void rtDynamicAttach(ID3D12Device5* dev, uint64_t timestampFrequency);   // after the bridge's device exists
void rtDynamicRecord(ID3D12GraphicsCommandList4* cl);                     // in the job, before the TLAS build
void rtDynamicAppendInstances(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& out, unsigned mask);   // mask 0x4
void rtDynamicClear();                                    // map unload, after the bridge's queue is idle
bool rtDynamicHasWork();                                  // slots, models or deferred releases exist (the clear must wait idle)
void rtDynamicAbandon();                                  // map unload while the queue is NOT idle: forget (leak), never free
bool rtDynamicHasWork();                                  // slots, models or deferred releases exist (the clear must wait idle)
void rtDynamicAbandon();                                  // map unload while the queue is NOT idle: forget (leak), never free

}  // namespace render
