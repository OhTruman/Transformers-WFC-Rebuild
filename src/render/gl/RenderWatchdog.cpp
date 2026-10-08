// Clean-room reconstruction — render-thread stall watchdog.
#include "render/gl/RenderWatchdog.h"
#include "core/Log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstring>
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
// armed by the first completed renderer frame: before it, boot movies (Activision / Hasbro / High Moon logos, ~30 s)
// present outside the renderer's frames and are not a stall (09a false dump at every boot)
std::atomic<bool> gArmed{false};
std::thread gThread;

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

#ifdef _WIN32
// MiniDumpWriteDump, loaded at run time (no link dependency on dbghelp); resolved once, ahead of any crash
typedef BOOL(WINAPI* MiniDumpFn)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
std::atomic<MiniDumpFn> gDumpFn{nullptr};
MiniDumpFn dumpFn() {
    if (MiniDumpFn fn = gDumpFn.load()) return fn;
    HMODULE dbg = LoadLibraryA("dbghelp.dll");          // kept loaded for the process lifetime
    MiniDumpFn fn = dbg ? (MiniDumpFn)(void*)GetProcAddress(dbg, "MiniDumpWriteDump") : nullptr;
    gDumpFn = fn;
    return fn;
}

#pragma pack(push, 4)
struct DumpExceptionInfo {             // MINIDUMP_EXCEPTION_INFORMATION (dbghelp.h, 4-byte packed)
    DWORD threadId;
    EXCEPTION_POINTERS* pointers;
    BOOL clientPointers;
};
#pragma pack(pop)

bool writeDump(const char* path, EXCEPTION_POINTERS* ep = nullptr) {
    MiniDumpFn fn = dumpFn();
    if (!fn) return false;
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DumpExceptionInfo ei{GetCurrentThreadId(), ep, FALSE};
    // MiniDumpWithThreadInfo (0x1000) | MiniDumpWithUnloadedModules (0x20) | MiniDumpWithHandleData (0x4)
    const bool ok = fn(GetCurrentProcess(), GetCurrentProcessId(), f, 0x1000 | 0x20 | 0x4, ep ? &ei : nullptr, nullptr, nullptr) != FALSE;
    CloseHandle(f);
    return ok;
}

// ---- crash report (unhandled exception) ------------------------------------------------------------------------
// Runs on the faulting thread with the process in an unknown state: no heap, no CRT stdio, no logger locks - Win32
// file writes from stack buffers, the map read into VirtualAlloc memory.
void put(HANDLE f, const char* fmt, ...) {
    char b[1024];
    va_list a;
    va_start(a, fmt);
    const int n = std::vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    DWORD w = 0;
    if (n > 0) WriteFile(f, b, (DWORD)std::min(n, (int)sizeof b - 1), &w, nullptr);
}

bool hex8(const char* p, uint32_t& v) {
    v = 0;
    for (int i = 0; i < 8; ++i) {
        const char c = p[i];
        const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0) return false;
        v = v * 16 + (uint32_t)d;
    }
    return true;
}

// Nearest symbol at or below `rva` in the lld map beside the exe (symbol lines: "RVA 00000000 0 <name>").
bool mapSymbol(const char* map, size_t len, uint32_t rva, const char*& name, size_t& nameLen, uint32_t& symRva) {
    name = nullptr;
    symRva = 0;
    for (size_t i = 0; i + 22 < len;) {
        size_t e = i;
        while (e < len && map[e] != '\n') ++e;
        uint32_t a, sz;
        if (e - i > 22 && hex8(map + i, a) && map[i + 8] == ' ' && hex8(map + i + 9, sz) && sz == 0 && a <= rva && a >= symRva) {
            size_t k = i + 17;
            while (k < e && map[k] == ' ') ++k;
            if (k + 1 < e && map[k] == '0' && map[k + 1] == ' ') {
                ++k;
                while (k < e && map[k] == ' ') ++k;
                size_t ne = e;
                while (ne > k && (map[ne - 1] == '\r' || map[ne - 1] == ' ')) --ne;
                if (ne > k) { name = map + k; nameLen = ne - k; symRva = a; }
            }
        }
        i = e + 1;
    }
    return name != nullptr;
}

std::atomic<bool> gCrashed{false};

LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) {
    if (gCrashed.exchange(true)) return EXCEPTION_CONTINUE_SEARCH;   // one report (a fault inside the report)
    const DWORD pid = GetCurrentProcessId();
    char path[96];
    std::snprintf(path, sizeof path, "wfc_crash_%lu.txt", (unsigned long)pid);
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        const uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
        // the map beside the exe (wfc_rebuild.map), for names; frames are written as RVAs either way
        char mpath[MAX_PATH];
        const char* map = nullptr;
        size_t mapLen = 0;
        const DWORD ml = GetModuleFileNameA(nullptr, mpath, MAX_PATH);
        if (ml > 4 && ml < MAX_PATH) {
            std::memcpy(mpath + ml - 4, ".map", 5);
            HANDLE mf = CreateFileA(mpath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (mf != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER sz{};
                if (GetFileSizeEx(mf, &sz) && sz.QuadPart > 0 && sz.QuadPart < (1ll << 28)) {
                    char* buf = (char*)VirtualAlloc(nullptr, (SIZE_T)sz.QuadPart, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                    DWORD rd = 0;
                    if (buf && ReadFile(mf, buf, (DWORD)sz.QuadPart, &rd, nullptr)) { map = buf; mapLen = rd; }
                    else if (buf) VirtualFree(buf, 0, MEM_RELEASE);
                }
                CloseHandle(mf);
            }
        }
        const EXCEPTION_RECORD* er = ep->ExceptionRecord;
        put(f, "wfc_rebuild crash: exception 0x%08lx at %p", (unsigned long)er->ExceptionCode, er->ExceptionAddress);
        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
            put(f, " (%s 0x%llx)", er->ExceptionInformation[0] == 0 ? "read" : er->ExceptionInformation[0] == 1 ? "write" : "execute",
                (unsigned long long)er->ExceptionInformation[1]);
        put(f, "\nthread %lu; render phase '%s', frame %d; map %s\n", (unsigned long)GetCurrentThreadId(), gPhase.load(),
            gFrame.load(), map ? "loaded" : "not found (frames as RVAs)");
        CONTEXT ctx = *ep->ContextRecord;
        for (int depth = 0; depth < 64 && ctx.Rip; ++depth) {
            HMODULE mod = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)(uintptr_t)ctx.Rip, &mod);
            if ((uintptr_t)mod == base) {
                const uint32_t rva = (uint32_t)(ctx.Rip - base);
                const char* nm = nullptr; size_t nl = 0; uint32_t sr = 0;
                if (map && mapSymbol(map, mapLen, rva, nm, nl, sr))
                    put(f, "  #%02d %08x %.*s +0x%x\n", depth, rva, (int)std::min<size_t>(nl, 900), nm, rva - sr);
                else put(f, "  #%02d %08x\n", depth, rva);
            } else {
                char mname[MAX_PATH] = "?";
                if (mod) GetModuleFileNameA(mod, mname, MAX_PATH);
                const char* leaf = std::strrchr(mname, '\\');
                put(f, "  #%02d %s+0x%llx\n", depth, leaf ? leaf + 1 : mname, (unsigned long long)(ctx.Rip - (uintptr_t)mod));
            }
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
            if (!fe) {                                           // leaf: return address on top of the stack
                if (IsBadReadPtr((void*)ctx.Rsp, 8)) break;
                ctx.Rip = *(DWORD64*)ctx.Rsp;
                ctx.Rsp += 8;
                continue;
            }
            void* hd = nullptr; DWORD64 est = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fe, &ctx, &hd, &est, nullptr);
        }
        if (map) VirtualFree((void*)map, 0, MEM_RELEASE);
        CloseHandle(f);
    }
    char dpath[96];
    std::snprintf(dpath, sizeof dpath, "wfc_crash_%lu.dmp", (unsigned long)pid);
    const bool dumped = writeDump(dpath, ep);
    char msg[256];
    const int n = std::snprintf(msg, sizeof msg, "wfc_rebuild crashed: report %s, minidump %s%s\n", path, dpath, dumped ? "" : " (failed)");
    DWORD w = 0;
    if (n > 0) WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, (DWORD)n, &w, nullptr);
    // [integration 09c] the buffered log's last lines (Systems): last, after the report and dump are on disk; never blocks
    // (try-locks, returns at once if the faulting thread holds the writer).
    core::logTryFlush();
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void loop() {
    int dumps = 0;
    bool reported = false;
    while (gRun.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const long long last = gLastProgressMs.load();
        if (last == 0 || !gArmed.load()) continue;
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
    gArmed.store(true, std::memory_order_relaxed);
    gFrame.store(frame, std::memory_order_relaxed);
    gLastProgressMs.store(nowMs(), std::memory_order_relaxed);
}

void installCrashHandler() {
#ifdef _WIN32
    if (std::getenv("WFC_NOCRASHHANDLER")) return;
    dumpFn();                                       // dbghelp resolved now, not inside a crash
    ULONG guarantee = 64 * 1024;                    // room for the report after a stack overflow (this thread)
    SetThreadStackGuarantee(&guarantee);
    SetUnhandledExceptionFilter(crashFilter);
    if (std::getenv("WFC_CRASHTEST")) *(volatile int*)nullptr = 0;   // verifies the report end to end
#endif
}

void stop() {
    if (!gRun.exchange(false)) return;
    if (gThread.joinable()) gThread.join();
}

}  // namespace render::watchdog
