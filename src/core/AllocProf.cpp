// The global C++ operator new / delete: mimalloc (third_party/mimalloc) when built with WFC_MIMALLOC (the Release allocator
// for the 32 v 32 allocation rate), else the CRT malloc. Aligned new / delete keep the library's own pair.
// DEV TOOL (WFC_ALLOCPROF): counts heap allocations and samples their call stacks, to find per-frame / per-step allocations.
// Off by default: operator new then costs one static flag check over malloc. With WFC_ALLOCPROF=<N> (sample every N-th
// main-thread allocation, default 16) it counts main-thread and other-thread allocations, and every ~10 s writes the rate and
// the top stacks sampled in that window (module offsets) to wfc_allocprof.txt in the working directory; symbolize them offline with
// llvm-addr2line -f -C -i -e <exe> 0x<imagebase + offset> (build with debug info). The sampler never allocates: a fixed
// open-addressing table, a re-entrancy guard, and plain C stdio for the report.
// Live mode (where the memory IS, e.g. the ~5 GB of a 64-player match): WFC_ALLOCPROF_LIVE=<min KB> (default 64) tracks every
// operator new block >= that size on ANY thread with its call stack until it is deleted; each report then also lists the live
// bytes per call site and the process private bytes (the share not covered = C malloc, the GL driver, mappings, small blocks).
// Reports are written from the main thread's sampler, so set WFC_ALLOCPROF too (e.g. WFC_ALLOCPROF=64 WFC_ALLOCPROF_LIVE=64).
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "core/Log.h"
#if defined(WFC_MIMALLOC) && WFC_MIMALLOC
#include <mimalloc.h>
#define WFC_RAW_ALLOC(n) mi_malloc(n)
#define WFC_RAW_FREE(q) mi_free(q)
#else
#define WFC_RAW_ALLOC(n) std::malloc(n)
#define WFC_RAW_FREE(q) std::free(q)
#endif

namespace {

struct Prof {
    static constexpr int kDepth = 12;
    static constexpr int kSlots = 8192;
    struct Slot { unsigned hash; long long count; void* frames[kDepth]; int depth; };
    bool on = false;
    int every = 16;
    double reportEveryS = 10.0;                                // WFC_ALLOCPROF_EVERY_S
    DWORD mainTid = 0;
    std::atomic<long long> mainAllocs{0}, otherAllocs{0}, mainBytes{0};
    long long sampleTick = 0;                                  // main thread only
    Slot slots[kSlots];
    double lastReport = 0.0;
    long long lastMain = 0, lastOther = 0;
    Prof() {
        std::memset(slots, 0, sizeof slots);
        if (const char* e = std::getenv("WFC_ALLOCPROF")) {
            on = true;
            const int n = std::atoi(e);
            if (n > 0) every = n;
            if (const char* r = std::getenv("WFC_ALLOCPROF_EVERY_S")) { const double v = std::atof(r); if (v > 0.1) reportEveryS = v; }
            mainTid = GetCurrentThreadId();                    // static initialisation runs on the main thread
        }
    }
};

Prof& prof() { static Prof p; return p; }
thread_local bool tInside = false;

// ---- live mode
struct Live {
    static constexpr int kDepth = 12, kSites = 4096, kPtrs = 1 << 20;
    struct Site { unsigned hash; int depth; void* frames[kDepth]; long long bytes, blocks; };
    struct Ptr { void* p; unsigned size; int site; };                  // p == nullptr: empty, (void*)1: deleted
    bool on = false;
    size_t minBytes = 64 * 1024;
    std::atomic_flag lock = ATOMIC_FLAG_INIT;
    Site sites[kSites];
    Ptr* ptrs = nullptr;                                                // VirtualAlloc'd (never through operator new)
    long long tracked = 0, trackedBytes = 0, dropped = 0;
    Live() {
        std::memset(sites, 0, sizeof sites);
        if (const char* e = std::getenv("WFC_ALLOCPROF_LIVE")) {
            const int kb = std::atoi(e);
            if (kb > 0) minBytes = (size_t)kb * 1024;
            ptrs = (Ptr*)VirtualAlloc(nullptr, sizeof(Ptr) * kPtrs, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            on = ptrs != nullptr;
        }
    }
    void acquire() { while (lock.test_and_set(std::memory_order_acquire)) {} }
    void release() { lock.clear(std::memory_order_release); }
    static unsigned ptrHash(void* p) { return (unsigned)(((uintptr_t)p >> 4) * 2654435761u); }
};
Live& live() { static Live l; return l; }
std::atomic<long long> gNewLive{0};                                     // live mode + mimalloc: all operator new bytes

void liveAdd(void* q, std::size_t n) {
    Live& L = live();
    void* fr[Live::kDepth];
    USHORT d = CaptureStackBackTrace(2, Live::kDepth, fr, nullptr);
    if (d == 0) { fr[0] = nullptr; d = 1; }
    unsigned h = 2166136261u;
    for (USHORT i = 0; i < d; ++i) { h ^= (unsigned)(uintptr_t)fr[i]; h *= 16777619u; }
    L.acquire();
    int site = -1;
    for (int probe = 0; probe < 128; ++probe) {
        const int idx = (int)((h + (unsigned)probe) % Live::kSites);
        Live::Site& s = L.sites[idx];
        if (s.depth == 0) { s.hash = h; s.depth = d; std::memcpy(s.frames, fr, sizeof(void*) * d); site = idx; break; }
        if (s.hash == h && s.depth == d) { site = idx; break; }
    }
    bool stored = false;
    if (site >= 0) {
        unsigned i = Live::ptrHash(q) & (Live::kPtrs - 1);
        for (int probe = 0; probe < 4096; ++probe, i = (i + 1) & (Live::kPtrs - 1)) {
            Live::Ptr& e = L.ptrs[i];
            if (e.p == nullptr || e.p == (void*)1) {
                e.p = q; e.size = (unsigned)std::min<std::size_t>(n, 0xFFFFFFFFu); e.site = site;
                L.sites[site].bytes += (long long)e.size; ++L.sites[site].blocks; ++L.tracked; L.trackedBytes += (long long)e.size;
                stored = true;
                break;
            }
        }
    }
    if (!stored) ++L.dropped;
    L.release();
}

void liveRemove(void* q) {
    Live& L = live();
    L.acquire();
    unsigned i = Live::ptrHash(q) & (Live::kPtrs - 1);
    for (int probe = 0; probe < 4096; ++probe, i = (i + 1) & (Live::kPtrs - 1)) {
        Live::Ptr& e = L.ptrs[i];
        if (e.p == nullptr) break;
        if (e.p == q) {
            L.sites[e.site].bytes -= e.size; --L.sites[e.site].blocks; --L.tracked; L.trackedBytes -= e.size;
            e.p = (void*)1;
            break;
        }
    }
    L.release();
}

void liveReport(FILE* f) {
    Live& L = live();
    if (!L.on) return;
    using GetPmi = BOOL(WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    const auto getPmi = (GetPmi)(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32GetProcessMemoryInfo");
    PROCESS_MEMORY_COUNTERS_EX pmc{}; pmc.cb = sizeof pmc;
    const double privMB = getPmi && getPmi(GetCurrentProcess(), (PPROCESS_MEMORY_COUNTERS)&pmc, sizeof pmc) ? pmc.PrivateUsage / 1048576.0 : -1.0;
    {   // where the private bytes are: operator new (all sizes), the Windows heaps (CRT malloc, driver heaps), other committed
        double heapsMB = 0.0;
        HANDLE heaps[64];
        const DWORD nh = GetProcessHeaps(64, heaps);
        using HeapSummaryFn = BOOL(WINAPI*)(HANDLE, DWORD, void*);
        const auto heapSummary = (HeapSummaryFn)(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "HeapSummary");
        struct { DWORD cb; SIZE_T allocated, committed, reserved, maxReserve; } hs;
        for (DWORD i = 0; i < nh && i < 64; ++i) {
            hs.cb = sizeof hs;
            if (heapSummary && heapSummary(heaps[i], 0, &hs)) heapsMB += hs.committed / 1048576.0;
        }
        double privMB2 = 0.0, mappedMB = 0.0, imageMB = 0.0;
        MEMORY_BASIC_INFORMATION mbi;
        for (char* a = nullptr; VirtualQuery(a, &mbi, sizeof mbi) == sizeof mbi; a = (char*)mbi.BaseAddress + mbi.RegionSize) {
            if (mbi.State == MEM_COMMIT) {
                if (mbi.Type == MEM_PRIVATE) privMB2 += mbi.RegionSize / 1048576.0;
                else if (mbi.Type == MEM_MAPPED) mappedMB += mbi.RegionSize / 1048576.0;
                else if (mbi.Type == MEM_IMAGE) imageMB += mbi.RegionSize / 1048576.0;
            }
            if ((uintptr_t)mbi.BaseAddress + mbi.RegionSize < (uintptr_t)mbi.BaseAddress) break;
        }
        std::fprintf(f, "MEM: operator new live (all sizes) %.1f MB; Windows heaps committed %.1f MB (%lu heaps); committed private %.1f MB, mapped %.1f MB, image %.1f MB\n",
                     gNewLive.load() / 1048576.0, heapsMB, (unsigned long)nh, privMB2, mappedMB, imageMB);
        // the largest committed private allocations (by AllocationBase): size patterns tell driver pools / staging / our own
        struct Rg { char* base; double mb; double wsMB; };
        Rg top[16]; int nTop = 0;
        char* curBase = nullptr; double curMB = 0.0;
        auto flush = [&] {
            if (!curBase || curMB < 16.0) return;
            int k = nTop < 16 ? nTop++ : 15;
            if (k == 15 && nTop == 16 && top[15].mb >= curMB) return;
            top[k] = {curBase, curMB, 0.0};
            for (int i = k; i > 0 && top[i].mb > top[i - 1].mb; --i) { Rg t = top[i]; top[i] = top[i - 1]; top[i - 1] = t; }
        };
        for (char* a2 = nullptr; VirtualQuery(a2, &mbi, sizeof mbi) == sizeof mbi; a2 = (char*)mbi.BaseAddress + mbi.RegionSize) {
            if ((char*)mbi.AllocationBase != curBase) { flush(); curBase = (char*)mbi.AllocationBase; curMB = 0.0; }
            if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE) curMB += mbi.RegionSize / 1048576.0;
            if ((uintptr_t)mbi.BaseAddress + mbi.RegionSize < (uintptr_t)mbi.BaseAddress) break;
        }
        flush();
        // resident share per region (QueryWorkingSetEx would be exact; here: touched pages of the first 64 MB sample)
        std::fprintf(f, "MEM top committed private allocations:");
        for (int i = 0; i < nTop; ++i) std::fprintf(f, " %p:%.0fMB", (void*)top[i].base, top[i].mb);
        std::fprintf(f, "\n");
    }
    L.acquire();
    std::fprintf(f, "LIVE: tracked blocks >= %zu KB: %lld blocks, %.1f MB live (dropped %lld); process private %.1f MB\n",
                 L.minBytes / 1024, L.tracked, L.trackedBytes / 1048576.0, L.dropped, privMB);
    int top[40]; int nTop = 0;
    for (int k = 0; k < 40; ++k) {
        int best = -1;
        for (int i = 0; i < Live::kSites; ++i) {
            if (L.sites[i].bytes <= 0) continue;
            bool used = false;
            for (int j = 0; j < nTop; ++j) if (top[j] == i) { used = true; break; }
            if (!used && (best < 0 || L.sites[i].bytes > L.sites[best].bytes)) best = i;
        }
        if (best < 0) break;
        top[nTop++] = best;
    }
    const HMODULE exe = GetModuleHandleA(nullptr);
    for (int k = 0; k < nTop; ++k) {
        const Live::Site& s = L.sites[top[k]];
        std::fprintf(f, "LIVE %.1f MB in %lld blocks:", s.bytes / 1048576.0, s.blocks);
        for (int d = 0; d < s.depth; ++d) {
            HMODULE mod = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)s.frames[d], &mod);
            if (mod == exe) std::fprintf(f, " +0x%llx", (unsigned long long)((char*)s.frames[d] - (char*)exe));
            else std::fprintf(f, " ext");
        }
        std::fprintf(f, "\n");
    }
    L.release();
}

double nowS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void report(Prof& p, double t) {
    FILE* f = std::fopen("wfc_allocprof.txt", "a");
    if (!f) return;
    const long long m = p.mainAllocs.load(), o = p.otherAllocs.load();
    const double dt = p.lastReport > 0.0 ? t - p.lastReport : 0.0;
    std::fprintf(f, "== context: %s\n", core::logLastLine());
    std::fprintf(f, "== t %.1f s: main-thread allocations %lld (%.0f /s), other threads %lld (%.0f /s), main bytes %lld\n", t,
                 m, dt > 0 ? (m - p.lastMain) / dt : 0.0, o, dt > 0 ? (o - p.lastOther) / dt : 0.0, p.mainBytes.load());
    p.lastMain = m; p.lastOther = o; p.lastReport = t;
    // Top 40 sampled stacks (no allocation: selection by repeated max scans over a small index array on the stack).
    int top[40]; int nTop = 0;
    for (int k = 0; k < 40; ++k) {
        int best = -1;
        for (int i = 0; i < Prof::kSlots; ++i) {
            if (p.slots[i].count <= 0) continue;
            bool used = false;
            for (int j = 0; j < nTop; ++j) if (top[j] == i) { used = true; break; }
            if (!used && (best < 0 || p.slots[i].count > p.slots[best].count)) best = i;
        }
        if (best < 0) break;
        top[nTop++] = best;
    }
    const HMODULE exe = GetModuleHandleA(nullptr);
    for (int k = 0; k < nTop; ++k) {
        const Prof::Slot& s = p.slots[top[k]];
        std::fprintf(f, "%lld samples (x%d):", s.count, p.every);
        for (int d = 0; d < s.depth; ++d) {
            HMODULE mod = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)s.frames[d], &mod);
            if (mod == exe) std::fprintf(f, " +0x%llx", (unsigned long long)((char*)s.frames[d] - (char*)exe));
            else std::fprintf(f, " ext");
        }
        std::fprintf(f, "\n");
    }
    liveReport(f);
    std::fclose(f);
    std::memset(p.slots, 0, sizeof p.slots);                   // each report covers its own ~10 s window (steady state)
}

void record(std::size_t n) {
    Prof& p = prof();
    if (GetCurrentThreadId() != p.mainTid) { p.otherAllocs.fetch_add(1, std::memory_order_relaxed); return; }
    p.mainAllocs.fetch_add(1, std::memory_order_relaxed);
    p.mainBytes.fetch_add((long long)n, std::memory_order_relaxed);
    if (tInside) return;
    tInside = true;
    if (++p.sampleTick % p.every == 0) {
        void* fr[Prof::kDepth];
        const USHORT d = CaptureStackBackTrace(2, Prof::kDepth, fr, nullptr);   // skip record + operator new
        unsigned h = 2166136261u;
        for (USHORT i = 0; i < d; ++i) { h ^= (unsigned)(uintptr_t)fr[i]; h *= 16777619u; }
        for (int probe = 0; probe < 64; ++probe) {
            Prof::Slot& s = p.slots[(h + (unsigned)probe) % Prof::kSlots];
            if (s.count == 0) { s.hash = h; s.depth = d; std::memcpy(s.frames, fr, sizeof(void*) * d); s.count = 1; break; }
            if (s.hash == h && s.depth == d) { ++s.count; break; }
        }
        const double t = nowS();
        if (p.lastReport == 0.0) p.lastReport = t;
        else if (t - p.lastReport >= p.reportEveryS) report(p, t);
    }
    tInside = false;
}

} // namespace

void* operator new(std::size_t n) {
    if (prof().on) record(n);
    if (void* q = WFC_RAW_ALLOC(n ? n : 1)) {
#if defined(WFC_MIMALLOC) && WFC_MIMALLOC
        if (live().on) gNewLive.fetch_add((long long)mi_usable_size(q), std::memory_order_relaxed);
#endif
        if (n >= live().minBytes && live().on && !tInside) { tInside = true; liveAdd(q, n); tInside = false; }
        return q;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    if (prof().on) record(n);
    void* q = WFC_RAW_ALLOC(n ? n : 1);
#if defined(WFC_MIMALLOC) && WFC_MIMALLOC
    if (q && live().on) gNewLive.fetch_add((long long)mi_usable_size(q), std::memory_order_relaxed);
#endif
    if (q && n >= live().minBytes && live().on && !tInside) { tInside = true; liveAdd(q, n); tInside = false; }
    return q;
}
void* operator new[](std::size_t n, const std::nothrow_t& t) noexcept { return ::operator new(n, t); }
static inline void wfcFree(void* q) noexcept {
    if (q && live().on) {
#if defined(WFC_MIMALLOC) && WFC_MIMALLOC
        gNewLive.fetch_sub((long long)mi_usable_size(q), std::memory_order_relaxed);
#endif
        liveRemove(q);
    }
    WFC_RAW_FREE(q);
}
void operator delete(void* q) noexcept { wfcFree(q); }
void operator delete[](void* q) noexcept { wfcFree(q); }
void operator delete(void* q, std::size_t) noexcept { wfcFree(q); }
void operator delete[](void* q, std::size_t) noexcept { wfcFree(q); }
void operator delete(void* q, const std::nothrow_t&) noexcept { wfcFree(q); }
void operator delete[](void* q, const std::nothrow_t&) noexcept { wfcFree(q); }
