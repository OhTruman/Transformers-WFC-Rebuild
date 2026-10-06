// Clean-room reconstruction — render-thread stall watchdog.
#include "render/gl/RenderWatchdog.h"
#include "core/Log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace render::watchdog {
namespace {

constexpr double kStallSeconds = 5.0;
std::atomic<const char*> gPhase{"(not started)"};
std::atomic<int> gFrame{0};
std::atomic<long long> gLastProgressMs{0};
std::atomic<bool> gRun{false};
std::thread gThread;

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

#ifdef _WIN32
// MiniDumpWriteDump, loaded at run time (no link dependency on dbghelp)
bool writeDump(const char* path) {
    HMODULE dbg = LoadLibraryA("dbghelp.dll");
    if (!dbg) return false;
    typedef BOOL(WINAPI * PFN)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
    PFN fn = (PFN)(void*)GetProcAddress(dbg, "MiniDumpWriteDump");
    bool ok = false;
    if (fn) {
        HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            // MiniDumpWithThreadInfo (0x1000) | MiniDumpWithUnloadedModules (0x20) | MiniDumpWithHandleData (0x4)
            ok = fn(GetCurrentProcess(), GetCurrentProcessId(), f, 0x1000 | 0x20 | 0x4, nullptr, nullptr, nullptr) != FALSE;
            CloseHandle(f);
        }
    }
    FreeLibrary(dbg);
    return ok;
}
#endif

void loop() {
    int dumps = 0;
    bool reported = false;
    while (gRun.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const long long last = gLastProgressMs.load();
        if (last == 0) continue;
        const double stalled = (nowMs() - last) / 1000.0;
        if (stalled < kStallSeconds) { reported = false; continue; }
        if (reported) continue;
        reported = true;
        LOG_ERROR("watchdog: no frame completed for %.1f s - render thread last at '%s', frame %d", stalled,
                  gPhase.load(), gFrame.load());
#ifdef _WIN32
        if (dumps < 2) {
            char path[96];
            std::snprintf(path, sizeof path, "wfc_hang_%lu_%d.dmp", (unsigned long)GetCurrentProcessId(), dumps++);
            LOG_ERROR("watchdog: minidump %s: %s", path, writeDump(path) ? "written (all thread stacks)" : "failed");
        }
#endif
    }
}

}  // namespace

void start() {
    if (gRun.load() || std::getenv("WFC_NOWATCHDOG")) return;
    gRun = true;
    gLastProgressMs = nowMs();
    gThread = std::thread(loop);
}

void phase(const char* where) {     // any mark is progress: loads mark their steps without completing frames
    gPhase.store(where, std::memory_order_relaxed);
    gLastProgressMs.store(nowMs(), std::memory_order_relaxed);
}

void frameDone(int frame) {
    gFrame.store(frame, std::memory_order_relaxed);
    gLastProgressMs.store(nowMs(), std::memory_order_relaxed);
}

void stop() {
    if (!gRun.exchange(false)) return;
    if (gThread.joinable()) gThread.join();
}

}  // namespace render::watchdog
