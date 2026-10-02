// WFC fidelity measurement build: in-process sampling profiler (Windows x64).
//
// Linked ONLY into the wfc_rebuild_prof measurement target, next to the unmodified product
// sources. A static constructor (runs on the main thread before main) starts a sampler thread when
// WFC_PROF=<out.txt> is set. Every ~1 ms it suspends the main thread, captures the full call stack
// with RtlVirtualUnwind (SEH unwind tables), resumes it, and keeps (time, return addresses). At
// exit it writes one line per sample: "<t_seconds> <mod>:<rva> <mod>:<rva> ..." (leaf first) plus a
// module table. tools/fidelity/profile-report.ps1 symbolizes and attributes the samples.
// Nothing here changes product behaviour; overhead is the suspend/resume per sample.
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00   // GetCurrentThreadStackLimits (Windows 8+)
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace {

struct Sample {
    double t;
    uint8_t depth;
    DWORD64 pc[48];
};

class Profiler {
public:
    Profiler() {
        const char* out = std::getenv("WFC_PROF");
        if (!out || !*out) return;
        out_ = out;
        if (const char* hz = std::getenv("WFC_PROF_HZ")) periodMs_ = 1000 / std::max(1, std::atoi(hz));
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &main_,
                        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
        GetCurrentThreadStackLimits(&stackLo_, &stackHi_);   // main thread stack bounds (we run on it here)
        QueryPerformanceFrequency(&freq_);
        QueryPerformanceCounter(&t0_);
        samples_.reserve(kMaxSamples);   // never allocate while the main thread is suspended
        timeBeginPeriod(1);
        running_ = true;
        mainTid_ = GetCurrentThreadId();
        thread_ = CreateThread(nullptr, 0, &Profiler::threadMain, this, 0, nullptr);
        watchdog_ = CreateThread(nullptr, 0, &Profiler::watchdogMain, this, 0, nullptr);
        std::atexit(&Profiler::onExit);
        self() = this;
    }
    static Profiler*& self() { static Profiler* p = nullptr; return p; }

private:
    static DWORD WINAPI threadMain(LPVOID p) { static_cast<Profiler*>(p)->loop(); return 0; }

    // The main thread may own a lock the unwinder needs (loader lock during LoadLibrary, the
    // function-table lock): suspending it there and calling RtlLookupFunctionEntry deadlocks.
    // (1) skip samples while the main thread owns the PEB loader lock; (2) a watchdog resumes the
    // main thread if a sample takes > 20 ms, and that sample is discarded. state_: 1 = suspended by
    // the sampler; whoever moves it 1 -> 0 calls ResumeThread (exactly once).
    bool mainOwnsLoaderLock() const {
        auto* peb = reinterpret_cast<const char*>(__readgsqword(0x60));
        auto* ll = *reinterpret_cast<RTL_CRITICAL_SECTION* const*>(peb + 0x110);   // PEB.LoaderLock (x64)
        return ll && ll->OwningThread && (DWORD)(uintptr_t)ll->OwningThread == mainTid_ && ll->RecursionCount > 0;
    }
    void resumeOnce() { if (InterlockedCompareExchange(&state_, 0, 1) == 1) ResumeThread(main_); }

    static DWORD WINAPI watchdogMain(LPVOID p) {
        auto* self = static_cast<Profiler*>(p);
        while (self->running_) {
            Sleep(5);
            LONG64 at = InterlockedCompareExchange64(&self->suspendedAt_, 0, 0);
            if (self->state_ == 1 && at) {
                LARGE_INTEGER now; QueryPerformanceCounter(&now);
                if ((now.QuadPart - at) * 1000 > self->freq_.QuadPart * 20) { self->rescued_ = true; self->resumeOnce(); }
            }
        }
        return 0;
    }

    void loop() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        while (running_) {
            Sleep(periodMs_);
            if (samples_.size() >= kMaxSamples) break;   // buffer full: stop rather than reallocate
            if (mainOwnsLoaderLock()) { ++skipped_; continue; }
            rescued_ = false;
            if (SuspendThread(main_) == (DWORD)-1) break;
            LARGE_INTEGER sus; QueryPerformanceCounter(&sus);
            InterlockedExchange64(&suspendedAt_, sus.QuadPart);
            InterlockedExchange(&state_, 1);
            if (mainOwnsLoaderLock()) { resumeOnce(); ++skipped_; continue; }   // took it between check and suspend
            CONTEXT ctx;
            ctx.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(main_, &ctx)) {
                Sample s;
                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                s.t = double(now.QuadPart - t0_.QuadPart) / double(freq_.QuadPart);
                // SEH unwind (Win64 frames set rbp at an offset, so rbp chains are not walkable).
                // Every stack read is bounds-checked against the main thread's stack; the unwinder
                // only reads unwind tables and the (suspended) stack. No allocation happens here.
                s.depth = 0;
                for (int i = 0; i < 48 && ctx.Rip && !rescued_; ++i) {
                    if (ctx.Rsp < stackLo_ || ctx.Rsp + 64 > stackHi_) break;
                    s.pc[s.depth++] = ctx.Rip;
                    DWORD64 base = 0;
                    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
                    if (!fn) {   // leaf without unwind info: return address is at [rsp]
                        ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
                        ctx.Rsp += 8;
                        continue;
                    }
                    PVOID handler = nullptr;
                    DWORD64 frame = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, fn, &ctx, &handler, &frame, nullptr);
                }
                if (!rescued_) samples_.push_back(s); else ++skipped_;   // stack moved under us: drop
            }
            InterlockedExchange64(&suspendedAt_, 0);
            resumeOnce();
        }
    }

    static void onExit() {
        Profiler* p = self();
        if (!p || !p->running_) return;
        p->running_ = false;
        WaitForSingleObject(p->thread_, 2000);
        p->write();
    }

    void write() {
        std::FILE* f = std::fopen(out_.c_str(), "wb");
        if (!f) return;
        std::map<HMODULE, int> mods;
        std::vector<std::string> names;
        auto modOf = [&](DWORD64 a, DWORD64& rva) {
            HMODULE m = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(a), &m);
            rva = m ? a - reinterpret_cast<DWORD64>(m) : a;
            auto it = mods.find(m);
            if (it != mods.end()) return it->second;
            char path[MAX_PATH] = "?";
            if (m) GetModuleFileNameA(m, path, MAX_PATH);
            int id = (int)names.size();
            names.push_back(path);
            mods[m] = id;
            return id;
        };
        std::fprintf(f, "# samples %zu period_ms %lu skipped %ld\n", samples_.size(), (unsigned long)periodMs_, (long)skipped_);
        std::string body;
        char buf[64];
        for (const Sample& s : samples_) {
            std::snprintf(buf, sizeof buf, "S %.6f", s.t);
            body += buf;
            for (int i = 0; i < s.depth; ++i) {
                DWORD64 rva;
                int id = modOf(s.pc[i], rva);
                std::snprintf(buf, sizeof buf, " %d:%llx", id, (unsigned long long)rva);
                body += buf;
            }
            body += '\n';
        }
        for (size_t i = 0; i < names.size(); ++i) std::fprintf(f, "M %zu %s\n", i, names[i].c_str());
        std::fwrite(body.data(), 1, body.size(), f);
        std::fclose(f);
    }

    static constexpr size_t kMaxSamples = 1 << 16;
    std::string out_;
    HANDLE main_ = nullptr, thread_ = nullptr, watchdog_ = nullptr;
    DWORD mainTid_ = 0;
    volatile LONG state_ = 0;            // 1 while the sampler holds the main thread suspended
    volatile LONG64 suspendedAt_ = 0;    // QPC time of the current suspension (0 = none)
    volatile bool rescued_ = false;      // the watchdog resumed the main thread mid-sample
    volatile long skipped_ = 0;          // samples skipped (loader lock held / rescued)
    DWORD periodMs_ = 1;
    ULONG_PTR stackLo_ = 0, stackHi_ = 0;
    LARGE_INTEGER freq_{}, t0_{};
    volatile bool running_ = false;
    std::vector<Sample> samples_;
};

// Heap-allocated on purpose: a static object's destructor would run BEFORE the atexit handler
// registered in its constructor and free the sample buffer while the sampler still writes to it.
Profiler* gProfiler = new Profiler();   // created during static init on the main thread

} // namespace
#endif
