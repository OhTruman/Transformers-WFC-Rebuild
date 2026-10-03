// WFC fidelity measurement build: exact call counter (wfc_rebuild_count).
//
// The counted subsystems (see tools/fidelity/CMakeLists.txt) are compiled with
// -finstrument-functions-after-inlining; every function entry/exit lands here. This file is NOT
// instrumented. Main thread only. Per function: calls and inclusive time (outermost activation
// only, so recursion is not double counted). Instrumentation inflates absolute times: use the
// COUNTS (exact, deterministic under the lockstep clock) and relative inclusive shares.
// Counting starts at the first presented frame (map/collision loading under -O0 instrumentation
// would otherwise take hours: every Vec3 operator is a call).
//
//   WFC_COUNT=<out.txt>            output file (required to record anything)
//   WFC_COUNT_WATCH=<rva,rva,...>  hex RVAs written PER FRAME (resolved by perf-counters.ps1 via llvm-nm)
// Output:
//   M <imagebase>                         module base (RVAs are relative to it)
//   F <frame> <frame_us>                  presented frame boundary (from LockstepClock's hook)
//   W <frame> <rva> <calls> <incl_us>     watched function, per frame (only when called)
//   T <rva> <calls> <incl_us>             whole-run totals for every function seen
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define NOINSTR __attribute__((no_instrument_function))

namespace {

struct Slot {
    uintptr_t fn = 0;
    uint64_t calls = 0, incl = 0;      // whole run (ticks)
    uint64_t fCalls = 0, fIncl = 0;    // this frame
    int depth = 0;
    uint64_t start = 0;
    bool watched = false;
};
constexpr size_t kSlots = 1u << 16;   // open addressing; the counted code has far fewer functions

struct Counter {
    Slot* slots = nullptr;
    std::vector<Slot*> stack;
    DWORD mainThread = 0;
    std::FILE* out = nullptr;
    uintptr_t base = 0;
    double usPerTick = 0;              // TSC -> us, calibrated per frame against QPC
    uint64_t lastFrameTick = 0, lastQpc = 0, firstTick = 0, firstQpc = 0;
    double qpcFreq = 0;
    bool on = false, ready = false;
    std::vector<uintptr_t> watch;
};
Counter* C = nullptr;

// rdtsc: ~7 ns vs ~2 x 20 ns for QueryPerformanceCounter per instrumented call (invariant TSC).
NOINSTR inline uint64_t ticks() { return __rdtsc(); }
NOINSTR inline uint64_t qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (uint64_t)t.QuadPart; }

NOINSTR Slot* slotFor(uintptr_t fn) {
    size_t h = (size_t)((fn >> 4) * 0x9E3779B97F4A7C15ull) & (kSlots - 1);
    for (;;) {
        Slot& s = C->slots[h];
        if (s.fn == fn) return &s;
        if (s.fn == 0) {
            s.fn = fn;
            for (uintptr_t w : C->watch) if (w == fn) s.watched = true;
            return &s;
        }
        h = (h + 1) & (kSlots - 1);
    }
}

NOINSTR void writeTotals() {
    if (!C || !C->out) return;
    double k = C->usPerTick;
    uint64_t t = ticks();
    if (C->firstTick && t > C->firstTick) k = ((double)(qpc() - C->firstQpc) / C->qpcFreq * 1e6) / (double)(t - C->firstTick);
    for (size_t i = 0; i < kSlots; ++i) {
        const Slot& s = C->slots[i];
        if (s.fn && s.calls) std::fprintf(C->out, "T %llx %llu %.1f\n", (unsigned long long)(s.fn - C->base),
                                          (unsigned long long)s.calls, s.incl * k);
    }
    std::fclose(C->out);
    C->out = nullptr;
}

NOINSTR void init() {
    C = new Counter();
    C->mainThread = GetCurrentThreadId();
    const char* p = std::getenv("WFC_COUNT");
    if (!p || !*p) return;
    C->out = std::fopen(p, "wb");
    if (!C->out) return;
    // VirtualAlloc returns zeroed pages == default-constructed Slots.
    C->slots = (Slot*)VirtualAlloc(nullptr, sizeof(Slot) * kSlots, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!C->slots) return;
    C->stack.reserve(4096);
    C->base = (uintptr_t)GetModuleHandleW(nullptr);
    LARGE_INTEGER f; QueryPerformanceFrequency(&f);
    C->qpcFreq = (double)f.QuadPart;
    if (const char* w = std::getenv("WFC_COUNT_WATCH")) {
        const char* q = w;
        while (*q) {
            char* end = nullptr;
            unsigned long long v = std::strtoull(q, &end, 16);
            if (end == q) break;
            C->watch.push_back(C->base + (uintptr_t)v);
            q = (*end == ',') ? end + 1 : end;
        }
    }
    std::fprintf(C->out, "M %llx\n", (unsigned long long)C->base);
    C->ready = true;
    std::atexit(writeTotals);
}

} // namespace

extern "C" {

NOINSTR void __cyg_profile_func_enter(void* fn, void*) {
    // init() allocates; if anything on that path is instrumented (an instrumented operator new or
    // static constructor in the counted TUs) the hook would re-enter before C is set -> unbounded
    // recursion (stack overflow 0xC00000FD on the merged M03 tree). Ignore calls made during init.
    static bool initializing = false;
    if (!C) {
        if (initializing) return;
        initializing = true;
        init();
        initializing = false;
    }
    if (!C->on || GetCurrentThreadId() != C->mainThread) return;
    Slot* s = slotFor((uintptr_t)fn);
    if (s->depth++ == 0) s->start = ticks();
    ++s->calls; ++s->fCalls;
    C->stack.push_back(s);
}

NOINSTR void __cyg_profile_func_exit(void*, void*) {
    if (!C || !C->on || GetCurrentThreadId() != C->mainThread || C->stack.empty()) return;
    Slot* s = C->stack.back();
    C->stack.pop_back();
    if (--s->depth == 0) {
        uint64_t d = ticks() - s->start;
        s->incl += d; s->fIncl += d;
    }
}

// Called by LockstepClock at the start of each frame with the frame just presented.
NOINSTR void fidFrameBoundary(long frame) {
    if (!C || !C->ready || !C->out) return;
    uint64_t t = ticks(), q = qpc();
    if (!C->on) {   // first presented frame: start counting from here
        C->lastFrameTick = C->firstTick = t;
        C->lastQpc = C->firstQpc = q;
        C->on = true;
        return;
    }
    double us = (double)(q - C->lastQpc) / C->qpcFreq * 1e6;
    if (t > C->lastFrameTick) C->usPerTick = us / (double)(t - C->lastFrameTick);
    std::fprintf(C->out, "F %ld %.1f\n", frame, us);
    C->lastFrameTick = t;
    C->lastQpc = q;
    for (size_t i = 0; i < kSlots; ++i) {
        Slot& s = C->slots[i];
        if (!s.fn || !s.fCalls) continue;
        if (s.watched)
            std::fprintf(C->out, "W %ld %llx %llu %.1f\n", frame, (unsigned long long)(s.fn - C->base),
                         (unsigned long long)s.fCalls, s.fIncl * C->usPerTick);
        s.fCalls = 0; s.fIncl = 0;
    }
}

} // extern "C"
