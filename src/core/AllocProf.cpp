// The global C++ operator new / delete: mimalloc (third_party/mimalloc) when built with WFC_MIMALLOC (the Release allocator
// for the 32 v 32 allocation rate), else the CRT malloc. Aligned new / delete keep the library's own pair.
// DEV TOOL (WFC_ALLOCPROF): counts heap allocations and samples their call stacks, to find per-frame / per-step allocations.
// Off by default: operator new then costs one static flag check over malloc. With WFC_ALLOCPROF=<N> (sample every N-th
// main-thread allocation, default 16) it counts main-thread and other-thread allocations, and every ~10 s writes the rate and
// the top stacks sampled in that window (module offsets) to wfc_allocprof.txt in the working directory; symbolize them offline with
// llvm-addr2line -f -C -i -e <exe> 0x<imagebase + offset> (build with debug info). The sampler never allocates: a fixed
// open-addressing table, a re-entrancy guard, and plain C stdio for the report.
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
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
            mainTid = GetCurrentThreadId();                    // static initialisation runs on the main thread
        }
    }
};

Prof& prof() { static Prof p; return p; }
thread_local bool tInside = false;

double nowS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void report(Prof& p, double t) {
    FILE* f = std::fopen("wfc_allocprof.txt", "a");
    if (!f) return;
    const long long m = p.mainAllocs.load(), o = p.otherAllocs.load();
    const double dt = p.lastReport > 0.0 ? t - p.lastReport : 0.0;
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
        else if (t - p.lastReport >= 10.0) report(p, t);
    }
    tInside = false;
}

} // namespace

void* operator new(std::size_t n) {
    if (prof().on) record(n);
    if (void* q = WFC_RAW_ALLOC(n ? n : 1)) return q;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    if (prof().on) record(n);
    return WFC_RAW_ALLOC(n ? n : 1);
}
void* operator new[](std::size_t n, const std::nothrow_t& t) noexcept { return ::operator new(n, t); }
void operator delete(void* q) noexcept { WFC_RAW_FREE(q); }
void operator delete[](void* q) noexcept { WFC_RAW_FREE(q); }
void operator delete(void* q, std::size_t) noexcept { WFC_RAW_FREE(q); }
void operator delete[](void* q, std::size_t) noexcept { WFC_RAW_FREE(q); }
void operator delete(void* q, const std::nothrow_t&) noexcept { WFC_RAW_FREE(q); }
void operator delete[](void* q, const std::nothrow_t&) noexcept { WFC_RAW_FREE(q); }
