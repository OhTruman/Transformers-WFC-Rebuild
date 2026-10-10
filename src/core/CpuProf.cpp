// DEV TOOL (WFC_CPUPROF=<interval ms, default 1>): a sampling profiler of the main (game / render-submit) thread, for
// function-level splits of the frame. A sampler thread suspends the main thread every interval, records its instruction
// pointer plus an unwound stack (RtlVirtualUnwind over every module's .pdata, up to 32 frames), resumes it, and every
// WFC_CPUPROF_EVERY_S seconds (default 10) writes the top stacks and the top functions (self, by leaf address) to
// wfc_cpuprof.txt as exe-relative offsets with the last log line as context. Symbolise offline against wfc_rebuild.map
// (tools/systems/cpuprof_sym.py). Hitches: the last ~2 s of samples are also kept with their times; when a "SLOWFRAME ...
// interval N ms" log line (WFC_SLOWFRAME, Rendering) reports N >= WFC_CPUPROF_HITCH_MS (default 12), the samples taken during
// that frame are appended to wfc_cpuprof_hitch.txt (one block per hitch) - one-off spikes that a 10 s window averages away.
// Self-installing (a static initializer on the main thread); with the variable unset
// nothing runs. Suspending the main thread while it is inside the GL driver can disturb the driver's submission: A/B the frame
// time with and without the profiler, and prefer a coarse interval (WFC_CPUPROF=5) for in-match work.
#ifdef _WIN32
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include "core/Log.h"

namespace {

constexpr int kDepth = 32;                      // deep enough to unwind through GL driver frames to the game's callers
constexpr int kSlots = 1 << 14;                 // open-addressing table of distinct stacks (no allocation in the sampler)
struct Slot { unsigned long long pc[kDepth]; int depth; long count; };

struct Recent { double t; int depth; unsigned long long pc[kDepth]; };
constexpr int kRecent = 4096;                   // ~4 s at 1 ms, ~20 s at 5 ms
struct CpuProf {
    HANDLE mainThread = nullptr;
    Recent* recent = nullptr;
    std::atomic<long> recentHead{0};            // next write index (the sampler is the only writer)
    double hitchMs = 12.0;
    LARGE_INTEGER freq{}, start{};
    int intervalMs = 1;
    double everyS = 10.0;
    Slot* slots = nullptr;
    long samples = 0, dropped = 0;
    unsigned long long base = 0, end = 0;       // the exe image (offsets are relative to it)
    std::atomic<bool> quit{false};
    std::thread t;
};

CpuProf& prof() { static CpuProf* p = new CpuProf; return *p; }   // leaked: the sampler may outlive static destruction

int walk(CONTEXT ctx, unsigned long long* out) {
    int n = 0;
    for (; n < kDepth && ctx.Rip; ++n) {
        out[n] = ctx.Rip;
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
        if (!fn) {                               // a leaf without unwind data: return address at [rsp]
            if (!ctx.Rsp) break;
            ctx.Rip = *(DWORD64*)ctx.Rsp;
            ctx.Rsp += 8;
            continue;
        }
        void* handlerData = nullptr; DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData, &establisher, nullptr);
    }
    return n;
}

void record(CpuProf& p, const unsigned long long* pc, int depth) {
    unsigned long long h = 1469598103934665603ull;
    for (int i = 0; i < depth; ++i) h = (h ^ pc[i]) * 1099511628211ull;
    for (int probe = 0; probe < 64; ++probe) {
        Slot& s = p.slots[(h + (unsigned)probe) & (kSlots - 1)];
        if (s.count == 0) { std::memcpy(s.pc, pc, sizeof(unsigned long long) * (size_t)depth); s.depth = depth; s.count = 1; return; }
        if (s.depth == depth && std::memcmp(s.pc, pc, sizeof(unsigned long long) * (size_t)depth) == 0) { ++s.count; return; }
    }
    ++p.dropped;
}

void report(CpuProf& p, double t) {
    FILE* f = std::fopen("wfc_cpuprof.txt", "a");
    if (!f) return;
    std::fprintf(f, "== context: %s\n", core::logLastLine());
    std::fprintf(f, "== t %.1f s: %ld main-thread samples (every %d ms), %ld dropped; offsets relative to the exe image\n", t, p.samples,
                 p.intervalMs, p.dropped);
    // top stacks
    int top[60]; int nTop = 0;
    for (int k = 0; k < 60; ++k) {
        int best = -1;
        for (int i = 0; i < kSlots; ++i) {
            if (p.slots[i].count <= 0) continue;
            bool used = false;
            for (int j = 0; j < nTop; ++j) if (top[j] == i) { used = true; break; }
            if (!used && (best < 0 || p.slots[i].count > p.slots[best].count)) best = i;
        }
        if (best < 0) break;
        top[nTop++] = best;
    }
    for (int k = 0; k < nTop; ++k) {
        const Slot& s = p.slots[top[k]];
        std::fprintf(f, "%ld samples:", s.count);
        for (int i = 0; i < s.depth; ++i) {
            if (s.pc[i] >= p.base && s.pc[i] < p.end) std::fprintf(f, " +0x%llx", s.pc[i] - p.base);
            else std::fprintf(f, " ext");
        }
        std::fprintf(f, "\n");
    }
    std::fclose(f);
    std::memset(p.slots, 0, sizeof(Slot) * kSlots);   // windowed: each report covers its own interval
    p.samples = 0; p.dropped = 0;
}

void run() {
    CpuProf& p = prof();
    LARGE_INTEGER last = p.start;
    const LARGE_INTEGER freq = p.freq, start = p.start;
    timeBeginPeriod(1);
    while (!p.quit.load()) {
        Sleep((DWORD)p.intervalMs);
        if (SuspendThread(p.mainThread) == (DWORD)-1) break;   // the main thread is gone
        CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        unsigned long long pc[kDepth]; int depth = 0;
        if (GetThreadContext(p.mainThread, &ctx)) depth = walk(ctx, pc);
        ResumeThread(p.mainThread);
        if (depth > 0) {
            record(p, pc, depth); ++p.samples;
            LARGE_INTEGER now; QueryPerformanceCounter(&now);
            const long h = p.recentHead.load(std::memory_order_relaxed);
            Recent& r = p.recent[h % kRecent];
            r.t = (double)(now.QuadPart - p.start.QuadPart) * 1000.0 / (double)p.freq.QuadPart;
            r.depth = depth;
            std::memcpy(r.pc, pc, sizeof(unsigned long long) * (size_t)depth);
            p.recentHead.store(h + 1, std::memory_order_release);
        }
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        if ((double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart >= p.everyS) {
            last = now;
            report(p, (double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart);
        }
    }
    timeEndPeriod(1);
}

struct Installer {
    Installer() {
        const char* e = std::getenv("WFC_CPUPROF");
        if (!e || !*e) return;
        CpuProf& p = prof();
        p.intervalMs = std::atoi(e) > 0 ? std::atoi(e) : 1;
        if (const char* s = std::getenv("WFC_CPUPROF_EVERY_S")) { const double v = std::atof(s); if (v >= 1.0) p.everyS = v; }
        if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &p.mainThread,
                             THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0)) return;
        auto* dos = (IMAGE_DOS_HEADER*)GetModuleHandleW(nullptr);
        auto* nt = (IMAGE_NT_HEADERS*)((unsigned char*)dos + dos->e_lfanew);
        p.base = (unsigned long long)dos; p.end = p.base + nt->OptionalHeader.SizeOfImage;
        p.slots = (Slot*)VirtualAlloc(nullptr, sizeof(Slot) * kSlots, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        p.recent = (Recent*)VirtualAlloc(nullptr, sizeof(Recent) * kRecent, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!p.slots || !p.recent) return;
        if (const char* h = std::getenv("WFC_CPUPROF_HITCH_MS")) { const double v = std::atof(h); if (v > 1.0) p.hitchMs = v; }
        QueryPerformanceFrequency(&p.freq); QueryPerformanceCounter(&p.start);
        if (FILE* f = std::fopen("wfc_cpuprof_hitch.txt", "w")) std::fclose(f);
        if (FILE* f = std::fopen("wfc_cpuprof.txt", "w")) std::fclose(f);
        p.t = std::thread(run);
        p.t.detach();
    }
} gInstaller;

}   // namespace

namespace {
// The log line hook (core::setLogLineHook): a "SLOWFRAME f<n> interval <ms> ms" line at or above the hitch threshold dumps the
// samples of that interval (the frame just finished) to wfc_cpuprof_hitch.txt.
void onLogLine(const char* line) {
    CpuProf& p = prof();
    if (!p.recent || !line) return;
    const char* s = std::strstr(line, "SLOWFRAME f");
    if (!s) return;
    const char* iv = std::strstr(s, "interval ");
    if (!iv) return;
    const double ms = std::atof(iv + 9);
    if (ms < p.hitchMs) return;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    const double tNow = (double)(now.QuadPart - p.start.QuadPart) * 1000.0 / (double)p.freq.QuadPart;
    FILE* f = std::fopen("wfc_cpuprof_hitch.txt", "a");
    if (!f) return;
    std::fprintf(f, "== hitch %.2f ms at t %.3f s: %.150s\n", ms, tNow / 1000.0, s);
    const long head = p.recentHead.load(std::memory_order_acquire);
    int n = 0;
    for (long i = head - 1; i >= 0 && i >= head - kRecent; --i) {
        const Recent& r = p.recent[i % kRecent];
        if (r.t < tNow - ms - 5.0) break;           // the frame's interval (+ 5 ms slack for the log's delay)
        std::fprintf(f, "%.3f ms ago:", tNow - r.t);
        for (int d = 0; d < r.depth; ++d) {
            if (r.pc[d] >= p.base && r.pc[d] < p.end) std::fprintf(f, " +0x%llx", r.pc[d] - p.base);
            else std::fprintf(f, " ext");
        }
        std::fprintf(f, "\n");
        ++n;
    }
    std::fprintf(f, "== %d samples\n", n);
    std::fclose(f);
}
struct HookInstaller { HookInstaller() { if (prof().recent) core::setLogLineHook(onLogLine); } } gHookInstaller;   // after gInstaller
}   // namespace
#endif
