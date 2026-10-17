// Clean-room reconstruction — M7 RT, the skinned characters' dynamic geometry, D3D12 side (PC ADAPTATION; OFF by default;
// Systems; render/d3d12/D3D12RtDynamic.h). The shared positions buffer (a committed DEFAULT buffer in COMMON with a shared
// handle: GL writes it, the BLAS builds read it after implicit promotion), per model an index buffer (DEFAULT, copied from
// an upload buffer by the first record), per instance slot a BLAS (ALLOW_UPDATE) over its region of the shared buffer:
// built the first time, refit (PERFORM_UPDATE in place) when GL rewrote its positions. Scratch: one arena per record,
// sub-allocated (256-byte aligned), grown on demand. Released resources wait three records (the bridge's two-allocator
// ring has finished the record two back by then).
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

#include "render/d3d12/D3D12RtDynamic.h"
#include "core/Log.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace render {
namespace {
template <class T> void rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

ID3D12Resource* makeBuf(ID3D12Device* dev, UINT64 bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE, D3D12_HEAP_FLAGS heapFlags = D3D12_HEAP_FLAG_NONE) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = heap;
    D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = std::max<UINT64>(bytes, 256);
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = flags;
    ID3D12Resource* r = nullptr;
    if (FAILED(dev->CreateCommittedResource(&hp, heapFlags, &rd, state, nullptr, __uuidof(ID3D12Resource), (void**)&r))) return nullptr;
    return r;
}
constexpr UINT64 kAlign = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;   // 256
UINT64 alignUp(UINT64 v) { return (v + kAlign - 1) & ~(kAlign - 1); }
constexpr int kRing = 3;

struct Model { ID3D12Resource* ib = nullptr; ID3D12Resource* upload = nullptr; uint32_t indices = 0, vertices = 0; bool ready = false; bool live = false; };
struct Slot { int model = -1; uint32_t first = 0, count = 0; ID3D12Resource* blas = nullptr; UINT64 blasBytes = 0, scratch = 0;
              bool built = false, live = false, pending = false, rebuild = false; };

class Dynamic {
public:
    void attach(ID3D12Device5* dev, uint64_t tsFreq) {
        if (dev_ == dev) return;
        dev_ = dev; tsFreq_ = tsFreq;
        static const long capEnv = [] { const char* e = std::getenv("WFC_RT_CHARVERTS"); return e ? std::atol(e) : 0L; }();
        capacity_ = capEnv > 0 ? (uint32_t)capEnv : (1u << 21);           // 2 M vertices, 24 MB
        free_.assign(1, {0u, capacity_});
    }
    bool available() const { return dev_ != nullptr; }
    bool buffer(RtDynamicBuffer* out) {
        if (!dev_) return false;
        if (!pos_) {
            pos_ = makeBuf(dev_, (UINT64)capacity_ * 12, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON,
                           D3D12_RESOURCE_FLAG_NONE, D3D12_HEAP_FLAG_SHARED);
            if (!pos_ || FAILED(dev_->CreateSharedHandle(pos_, nullptr, GENERIC_ALL, nullptr, &posHandle_))) {
                LOG_WARN("RT characters: shared positions buffer failed"); rel(pos_); dev_ = nullptr; return false;
            }
            const D3D12_RESOURCE_DESC rd = pos_->GetDesc();
            posBytes_ = dev_->GetResourceAllocationInfo(0, 1, &rd).SizeInBytes;
            LOG_INFO("RT characters: shared positions buffer %u vertices (%.1f MB)", capacity_, (double)posBytes_ / 1048576.0);
        }
        out->handle = posHandle_; out->bytes = posBytes_; out->capacity = capacity_;
        return true;
    }
    uint32_t epoch() const { return epoch_; }
    int model(const uint32_t* idx, uint32_t n, uint32_t verts) {
        if (!dev_ || !n || n % 3) return -1;
        Model m; m.indices = n; m.vertices = verts; m.live = true;
        m.upload = makeBuf(dev_, (UINT64)n * 4, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        m.ib = makeBuf(dev_, (UINT64)n * 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
        void* p = nullptr;
        if (!m.upload || !m.ib || FAILED(m.upload->Map(0, nullptr, &p))) { rel(m.upload); rel(m.ib); return -1; }
        std::memcpy(p, idx, (size_t)n * 4); m.upload->Unmap(0, nullptr);
        for (size_t k = 0; k < models_.size(); ++k) if (!models_[k].live) { models_[k] = m; return (int)k; }
        models_.push_back(m);
        return (int)models_.size() - 1;
    }
    void releaseModel(int k) {
        if (k < 0 || k >= (int)models_.size() || !models_[(size_t)k].live) return;
        later(models_[(size_t)k].ib); later(models_[(size_t)k].upload);
        models_[(size_t)k] = Model{};
    }
    void check(int slot, uint32_t n, uint64_t hash) {
        if (chkRecord_ || slot < 0 || slot >= (int)slots_.size() || !slots_[(size_t)slot].live || !n) return;
        chkSlot_ = slot; chkN_ = std::min(n, slots_[(size_t)slot].count); chkHash_ = hash; chkArmed_ = true;
    }
    int slot(int model) {
        if (!dev_ || model < 0 || model >= (int)models_.size() || !models_[(size_t)model].live) return -1;
        const Model& m = models_[(size_t)model];
        Slot s; s.model = model; s.count = m.vertices;
        if (!allocRange(s.count, s.first)) return -1;
        D3D12_RAYTRACING_GEOMETRY_DESC gd = geometry(s, m);
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in = inputs(&gd, false);
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pi{};
        dev_->GetRaytracingAccelerationStructurePrebuildInfo(&in, &pi);
        s.blasBytes = alignUp(pi.ResultDataMaxSizeInBytes);
        s.scratch = alignUp(std::max(pi.ScratchDataSizeInBytes, pi.UpdateScratchDataSizeInBytes));
        s.blas = makeBuf(dev_, s.blasBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (!s.blas) { freeRange(s.first, s.count); return -1; }
        s.live = true;
        blasTotal_ += s.blasBytes;
        for (size_t k = 0; k < slots_.size(); ++k) if (!slots_[k].live) { slots_[k] = s; return (int)k; }
        slots_.push_back(s);
        return (int)slots_.size() - 1;
    }
    uint32_t firstVertex(int s) const { return s >= 0 && s < (int)slots_.size() ? slots_[(size_t)s].first : 0; }
    void releaseSlot(int si) {
        if (si < 0 || si >= (int)slots_.size() || !slots_[(size_t)si].live) return;
        Slot& s = slots_[(size_t)si];
        later(s.blas); blasTotal_ -= s.blasBytes;
        freeRange(s.first, s.count);
        s = Slot{};
    }
    void frame(const RtDynamicInstance* inst, int n) {
        frame_.assign(inst, inst + std::max(n, 0));
        for (const RtDynamicInstance& i : frame_)
            if (i.update && i.slot >= 0 && i.slot < (int)slots_.size()) {
                slots_[(size_t)i.slot].pending = true;
                if (i.rebuild) slots_[(size_t)i.slot].rebuild = true;
            }
    }
    void record(ID3D12GraphicsCommandList4* cl) {
        if (!dev_ || !cl) return;
        ++records_;
        for (size_t k = 0; k < deferred_.size();)            // (the record three back has finished on the GPU)
            if (deferred_[k].second + kRing <= records_) { deferred_[k].first->Release(); deferred_[k] = deferred_.back(); deferred_.pop_back(); }
            else ++k;
        readTimestamps();
        readCheck();
        if (chkArmed_ && pos_) {                           // (WFC_RT_CHARCHECK) the positions as D3D12 sees them
            chkArmed_ = false;
            const UINT64 bytes = (UINT64)chkN_ * 12;
            if (bytes > chkBytes_) { later(chkReadback_); chkBytes_ = bytes; chkReadback_ = makeBuf(dev_, bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST); }
            if (chkReadback_) { cl->CopyBufferRegion(chkReadback_, 0, pos_, (UINT64)slots_[(size_t)chkSlot_].first * 12, bytes); chkRecord_ = records_; }
            else chkBytes_ = 0;
        }
        // index uploads of new models
        for (Model& m : models_) {
            if (!m.live || m.ready || !m.upload) continue;
            cl->CopyBufferRegion(m.ib, 0, m.upload, 0, (UINT64)m.indices * 4);
            D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = m.ib;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            cl->ResourceBarrier(1, &b);
            m.ready = true; later(m.upload);
        }
        // this frame's builds / refits: the scratch arena first
        UINT64 need = 0;
        for (const RtDynamicInstance& i : frame_) if (pendingSlot(i)) need += slots_[(size_t)i.slot].scratch;
        if (need > scratchBytes_) {
            later(scratch_);
            scratchBytes_ = alignUp(need + need / 2);
            scratch_ = makeBuf(dev_, scratchBytes_, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            if (!scratch_) { scratchBytes_ = 0; return; }
        }
        if (!need) return;
        const int r = (int)(records_ % kRing);
        timestamps();
        if (tsHeap_) cl->EndQuery(tsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(2 * r));
        UINT64 off = 0;
        for (RtDynamicInstance& i : frame_) {
            if (!pendingSlot(i)) continue;
            Slot& s = slots_[(size_t)i.slot];
            const Model& m = models_[(size_t)s.model];
            D3D12_RAYTRACING_GEOMETRY_DESC gd = geometry(s, m);
            const bool refit = s.built && !s.rebuild;
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd{};
            bd.Inputs = inputs(&gd, refit);
            bd.DestAccelerationStructureData = s.blas->GetGPUVirtualAddress();
            if (refit) bd.SourceAccelerationStructureData = bd.DestAccelerationStructureData;
            bd.ScratchAccelerationStructureData = scratch_->GetGPUVirtualAddress() + off;
            off += s.scratch;
            cl->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);
            ++(refit ? stats_.refits : s.built ? stats_.rebuilds : stats_.builds);
            s.built = true; s.pending = false; s.rebuild = false;
        }
        D3D12_RESOURCE_BARRIER ub{}; ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;   // the BLAS writes before the TLAS build reads them
        cl->ResourceBarrier(1, &ub);
        if (tsHeap_) {
            cl->EndQuery(tsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(2 * r + 1));
            cl->ResolveQueryData(tsHeap_, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(2 * r), 2, tsReadback_, (UINT64)r * 16);
            tsRecord_[r] = records_;
        }
    }
    void append(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& out, unsigned mask) {
        ++stats_.records;
        for (const RtDynamicInstance& i : frame_) {
            if (i.slot < 0 || i.slot >= (int)slots_.size()) continue;
            const Slot& s = slots_[(size_t)i.slot];
            if (!s.live || !s.built) continue;
            D3D12_RAYTRACING_INSTANCE_DESC d{};
            std::memcpy(d.Transform, i.transform, sizeof d.Transform);
            d.InstanceID = (UINT)i.slot;
            // (bodies: their own bit; every pass that traces 0x4 traces 0x14. WFC_RT_BODYBIT=0: bodies stay on the
            // characters' bit - the rays of before, for the zero-difference eye sheet)
            static const bool bodyBit = [] { const char* e = std::getenv("WFC_RT_BODYBIT"); return !(e && e[0] == '0'); }();
            d.InstanceMask = (i.body && bodyBit ? 0x10u : mask) & 0xFF;
            // closed meshes, but a mirrored model matrix or a refit's degenerate triangles must not let shadow rays through
            d.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
            d.AccelerationStructure = s.blas->GetGPUVirtualAddress();
            out.push_back(d);
            ++stats_.instances;
        }
    }
    void clear() {                                        // (the bridge's queue is idle)
        for (auto& d : deferred_) d.first->Release();
        deferred_.clear();
        chkRecord_ = 0; chkArmed_ = false;
        for (Slot& s : slots_) rel(s.blas);
        for (Model& m : models_) { rel(m.ib); rel(m.upload); }
        slots_.clear(); models_.clear(); frame_.clear();
        free_.assign(1, {0u, capacity_});
        blasTotal_ = 0;
        ++epoch_;
    }
    bool hasWork() const {
        for (const Slot& s : slots_) if (s.live) return true;
        for (const Model& m : models_) if (m.live) return true;
        return !deferred_.empty() || scratch_ != nullptr;
    }
    void abandon() {                                      // (the queue may still use them: dropped, not released)
        deferred_.clear(); slots_.clear(); models_.clear(); frame_.clear();
        scratch_ = nullptr; scratchBytes_ = 0;
        chkRecord_ = 0; chkArmed_ = false;
        free_.assign(1, {0u, capacity_});
        blasTotal_ = 0;
        ++epoch_;
        LOG_WARN("RT characters: map unload while the D3D12 queue was busy: dynamic BLAS dropped without release");
    }
    RtDynamicStats takeStats() {
        RtDynamicStats s = stats_;
        s.slots = 0; s.verticesUsed = 0;
        for (const Slot& x : slots_) if (x.live) { ++s.slots; s.verticesUsed += x.count; }
        s.capacity = capacity_; s.blasMB = (double)blasTotal_ / 1048576.0;
        stats_ = RtDynamicStats{};
        return s;
    }

private:
    bool pendingSlot(const RtDynamicInstance& i) const {
        if (i.slot < 0 || i.slot >= (int)slots_.size()) return false;
        const Slot& s = slots_[(size_t)i.slot];
        return s.live && s.pending && models_[(size_t)s.model].ready && pos_;
    }
    D3D12_RAYTRACING_GEOMETRY_DESC geometry(const Slot& s, const Model& m) const {
        D3D12_RAYTRACING_GEOMETRY_DESC gd{};
        gd.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES; gd.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        gd.Triangles.VertexBuffer.StartAddress = pos_ ? pos_->GetGPUVirtualAddress() + (UINT64)s.first * 12 : 0;
        gd.Triangles.VertexBuffer.StrideInBytes = 12;
        gd.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT; gd.Triangles.VertexCount = m.vertices;
        gd.Triangles.IndexBuffer = m.ib ? m.ib->GetGPUVirtualAddress() : 0; gd.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        gd.Triangles.IndexCount = m.indices;
        return gd;
    }
    static D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs(const D3D12_RAYTRACING_GEOMETRY_DESC* gd, bool update) {
        static const bool fastBuild = std::getenv("WFC_RT_CHARFASTBUILD") != nullptr;   // (A/B: trace vs build / refit speed)
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
        in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
                   (fastBuild ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD
                              : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE);
        if (update) in.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        in.NumDescs = 1; in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY; in.pGeometryDescs = gd;
        return in;
    }
    bool allocRange(uint32_t n, uint32_t& first) {        // first fit over the sorted free ranges
        for (size_t k = 0; k < free_.size(); ++k) {
            if (free_[k].second < n) continue;
            first = free_[k].first;
            free_[k].first += n; free_[k].second -= n;
            if (!free_[k].second) free_.erase(free_.begin() + (long)k);
            return true;
        }
        return false;
    }
    void freeRange(uint32_t first, uint32_t n) {
        auto it = std::lower_bound(free_.begin(), free_.end(), std::make_pair(first, 0u));
        it = free_.insert(it, {first, n});
        size_t k = (size_t)(it - free_.begin());
        if (k + 1 < free_.size() && free_[k].first + free_[k].second == free_[k + 1].first) {
            free_[k].second += free_[k + 1].second; free_.erase(free_.begin() + (long)k + 1);
        }
        if (k > 0 && free_[k - 1].first + free_[k - 1].second == free_[k].first) {
            free_[k - 1].second += free_[k].second; free_.erase(free_.begin() + (long)k);
        }
    }
    void readCheck() {
        if (!chkRecord_ || chkRecord_ + 2 > records_) return;
        chkRecord_ = 0;
        void* p = nullptr;
        D3D12_RANGE rg{0, (SIZE_T)chkN_ * 12};
        if (FAILED(chkReadback_->Map(0, &rg, &p))) return;
        uint64_t h = 1469598103934665603ull;
        const unsigned char* b = (const unsigned char*)p;
        for (size_t k = 0; k < (size_t)chkN_ * 12; ++k) { h ^= b[k]; h *= 1099511628211ull; }
        D3D12_RANGE none{0, 0}; chkReadback_->Unmap(0, &none);
        LOG_INFO("RT characters check: slot %d, %u vertices as D3D12 reads them %s GL's (hash %016llx / %016llx)", chkSlot_, chkN_,
                 h == chkHash_ ? "EQUAL" : "DIFFER from", (unsigned long long)h, (unsigned long long)chkHash_);
    }
    void later(ID3D12Resource*& r) { if (r) deferred_.push_back({r, records_}); r = nullptr; }
    void timestamps() {
        if (tsHeap_ || tsFailed_ || !tsFreq_) return;
        D3D12_QUERY_HEAP_DESC qh{}; qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qh.Count = 2 * kRing;
        tsReadback_ = makeBuf(dev_, 16 * kRing, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!tsReadback_ || FAILED(dev_->CreateQueryHeap(&qh, __uuidof(ID3D12QueryHeap), (void**)&tsHeap_))) { tsFailed_ = true; rel(tsReadback_); }
    }
    void readTimestamps() {                               // the record two back (finished: the bridge waited for its allocator)
        if (!tsHeap_) return;
        for (int r = 0; r < kRing; ++r) {
            if (!tsRecord_[r] || tsRecord_[r] + 2 > records_) continue;
            D3D12_RANGE rg{(SIZE_T)r * 16, (SIZE_T)r * 16 + 16};
            void* p = nullptr;
            if (SUCCEEDED(tsReadback_->Map(0, &rg, &p))) {
                uint64_t t[2]; std::memcpy(t, (const char*)p + r * 16, 16);
                D3D12_RANGE none{0, 0}; tsReadback_->Unmap(0, &none);
                if (t[1] > t[0]) { stats_.gpuMs += (double)(t[1] - t[0]) * 1000.0 / (double)tsFreq_; ++stats_.gpuSamples; }
            }
            tsRecord_[r] = 0;
        }
    }

    ID3D12Device5* dev_ = nullptr;
    uint64_t tsFreq_ = 0;
    uint32_t capacity_ = 0, epoch_ = 1;
    ID3D12Resource* pos_ = nullptr; HANDLE posHandle_ = nullptr; UINT64 posBytes_ = 0;
    std::vector<std::pair<uint32_t, uint32_t>> free_;     // (first, count), sorted by first
    std::vector<Model> models_;
    std::vector<Slot> slots_;
    std::vector<RtDynamicInstance> frame_;
    ID3D12Resource* scratch_ = nullptr; UINT64 scratchBytes_ = 0;
    std::vector<std::pair<ID3D12Resource*, uint64_t>> deferred_;
    uint64_t records_ = 0;
    UINT64 blasTotal_ = 0;
    ID3D12QueryHeap* tsHeap_ = nullptr; ID3D12Resource* tsReadback_ = nullptr; bool tsFailed_ = false;
    uint64_t tsRecord_[kRing] = {0, 0, 0};
    RtDynamicStats stats_;
    ID3D12Resource* chkReadback_ = nullptr; UINT64 chkBytes_ = 0; uint64_t chkRecord_ = 0, chkHash_ = 0;
    int chkSlot_ = -1; uint32_t chkN_ = 0; bool chkArmed_ = false;
};
Dynamic g_dyn;
}  // namespace

bool rtDynamicAvailable() { return g_dyn.available(); }
bool rtDynamicBuffer(RtDynamicBuffer* out) { return g_dyn.buffer(out); }
uint32_t rtDynamicEpoch() { return g_dyn.epoch(); }
int rtDynamicModel(const uint32_t* indices, uint32_t indexCount, uint32_t vertexCount) { return g_dyn.model(indices, indexCount, vertexCount); }
void rtDynamicReleaseModel(int model) { g_dyn.releaseModel(model); }
void rtDynamicCheck(int slot, uint32_t vertices, uint64_t glHash) { g_dyn.check(slot, vertices, glHash); }
int rtDynamicSlot(int model) { return g_dyn.slot(model); }
uint32_t rtDynamicFirstVertex(int slot) { return g_dyn.firstVertex(slot); }
void rtDynamicReleaseSlot(int slot) { g_dyn.releaseSlot(slot); }
void rtDynamicFrame(const RtDynamicInstance* inst, int n) { g_dyn.frame(inst, n); }
RtDynamicStats rtDynamicTakeStats() { return g_dyn.takeStats(); }
void rtDynamicAttach(ID3D12Device5* dev, uint64_t timestampFrequency) { g_dyn.attach(dev, timestampFrequency); }
void rtDynamicRecord(ID3D12GraphicsCommandList4* cl) { g_dyn.record(cl); }
void rtDynamicAppendInstances(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& out, unsigned mask) { g_dyn.append(out, mask); }
void rtDynamicClear() { g_dyn.clear(); }
bool rtDynamicHasWork() { return g_dyn.hasWork(); }
void rtDynamicAbandon() { g_dyn.abandon(); }

}  // namespace render
#else
#include "render/d3d12/D3D12RtDynamic.h"
namespace render {
bool rtDynamicAvailable() { return false; }
bool rtDynamicBuffer(RtDynamicBuffer*) { return false; }
uint32_t rtDynamicEpoch() { return 0; }
int rtDynamicModel(const uint32_t*, uint32_t, uint32_t) { return -1; }
int rtDynamicSlot(int) { return -1; }
void rtDynamicReleaseModel(int) {}
void rtDynamicCheck(int, uint32_t, uint64_t) {}
uint32_t rtDynamicFirstVertex(int) { return 0; }
void rtDynamicReleaseSlot(int) {}
void rtDynamicFrame(const RtDynamicInstance*, int) {}
RtDynamicStats rtDynamicTakeStats() { return {}; }
void rtDynamicAttach(ID3D12Device5*, uint64_t) {}
void rtDynamicRecord(ID3D12GraphicsCommandList4*) {}
void rtDynamicAppendInstances(std::vector<D3D12_RAYTRACING_INSTANCE_DESC>&, unsigned) {}
void rtDynamicClear() {}
bool rtDynamicHasWork() { return false; }
void rtDynamicAbandon() {}
}  // namespace render
#endif
