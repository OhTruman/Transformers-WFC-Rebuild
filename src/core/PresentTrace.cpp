// DEV TOOL (WFC_PRESENTTRACE=<stall ms, default 25>): why does SwapBuffers stall mid-match? Every present is timed (a
// self-installing hook of the exe's SwapBuffers import, like WFC_FILELOG - no renderer change), and per frame it records:
// the time inside SwapBuffers, the frame interval, the GPU's free memory (GL_ATI_meminfo: VBO / texture / render-buffer pools,
// queried with the context current inside the hook), the process's page faults and working set, and DWM's composition
// timing (missed / dropped frames). The message pump / input polling calls (PeekMessageW, DispatchMessageW, GetAsyncKeyState:
// hooked the same way) are timed too, since a stall in win32k shows up in whichever kernel call the frame is in. When a present,
// the input calls or the whole frame interval take longer than the threshold, the last 120 frames are written to
// wfc_presenttrace.txt with the stall, so a slow GPU frame queued earlier, a VRAM eviction, paging or a missed composition
// shows up as the change in the frames before it. Also per frame: the system's available physical memory and commit (memory
// pressure trims the process's working set; the re-faults land in whatever the frame is doing). WFC_WSMIN_MB=<n> (A/B knob,
// independent of the trace) sets a soft minimum working set of n MB at startup, so the memory manager trims us last.
// Nothing is patched or measured with the variables unset.
#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "core/Log.h"

namespace {

using SwapFn = BOOL(WINAPI*)(HDC);
using PeekFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using DispatchFn = LRESULT(WINAPI*)(const MSG*);
using KeyFn = SHORT(WINAPI*)(int);
PeekFn oPeek = nullptr; DispatchFn oDispatch = nullptr; KeyFn oKey = nullptr;
double inputMs = 0.0, inputMaxMs = 0.0;   // this frame: time inside the hooked input / message calls, the slowest single one
int inputCalls = 0;
using GetIntFn = void(WINAPI*)(unsigned, int*);
using GetStrFn = const unsigned char*(WINAPI*)(unsigned);
GetStrFn glGetStringFn = nullptr;
int meminfoState = 0;   // 0 = not checked yet, 1 = GL_ATI_meminfo present, -1 = absent (never queried: no GL error raised)
using DwmTimingFn = HRESULT(WINAPI*)(HWND, DWM_TIMING_INFO*);
SwapFn oSwap = nullptr;
GetIntFn glGetIntegervFn = nullptr;
DwmTimingFn dwmTiming = nullptr;

constexpr unsigned kVboFreeAti = 0x87FB, kTexFreeAti = 0x87FC, kRbFreeAti = 0x87FD;   // GL_ATI_meminfo
constexpr int kRing = 120;
struct Rec {
    double t, interval, swapMs;      // ms since start; ms since the previous present returned; ms inside SwapBuffers
    int vboFree, texFree, rbFree;    // KB free in each pool (first value of GL_ATI_meminfo), -1 = unavailable
    unsigned long pageFaults;        // since the previous frame
    double wsMB;
    unsigned long long dwmMissed, dwmDropped;   // cumulative DWM counters (cFramesMissed, cFramesDropped)
    double inputMs, inputMaxMs;      // time inside PeekMessage / DispatchMessage / GetAsyncKeyState this frame, slowest call
    int inputCalls;
    unsigned availMB, commitMB;      // system: available physical memory, committed bytes (GlobalMemoryStatusEx)
    double privMB;                   // this process: private (committed) bytes - the game's own allocation / release pattern
};
Rec ring[kRing];
long head = 0;
double stallMs = 25.0, last = 0.0;
unsigned long lastFaults = 0;
long stalls = 0;
std::chrono::steady_clock::time_point t0;
bool meminfo = false;

double nowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }

void dump(const Rec& stall) {
    FILE* f = std::fopen("wfc_presenttrace.txt", "a");
    if (!f) return;
    std::fprintf(f, "== stall at t %.3f s (stall #%ld): interval %.2f ms, swap %.2f ms, input %.2f ms (slowest call %.2f); last log: %s\n",
                 stall.t / 1000.0, stalls, stall.interval, stall.swapMs, stall.inputMs, stall.inputMaxMs, core::logLastLine());
    std::fprintf(f, "   t_s  interval_ms  swap_ms  input_ms  inMax_ms  inCalls  vboFreeKB  texFreeKB  rbFreeKB  pageFaults  wsMB  dwmMissed  dwmDropped  availMB  commitMB  privMB\n");
    for (long i = head - kRing; i < head; ++i) {
        if (i < 0) continue;
        const Rec& r = ring[i % kRing];
        std::fprintf(f, "%8.3f %10.2f %8.2f %9.2f %9.2f %8d %10d %10d %9d %11lu %7.1f %10llu %11llu\n", r.t / 1000.0, r.interval, r.swapMs,
                     r.inputMs, r.inputMaxMs, r.inputCalls, r.vboFree, r.texFree, r.rbFree, r.pageFaults, r.wsMB, r.dwmMissed, r.dwmDropped);
        std::fseek(f, -1, SEEK_CUR);   // (replace the newline: the system memory columns follow)
        std::fprintf(f, " %8u %9u %7.1f\n", r.availMB, r.commitMB, r.privMB);
    }
    std::fclose(f);
}

BOOL WINAPI hSwap(HDC dc) {
    Rec r{};
    const double before = nowMs();
    r.interval = last > 0.0 ? before - last : 0.0;
    const BOOL ok = oSwap(dc);
    const double after = nowMs();
    r.t = after; r.swapMs = after - before; last = after;
    r.vboFree = r.texFree = r.rbFree = -1;
    if (meminfo && meminfoState == 0 && glGetStringFn) {   // once, with the context current: is the extension there?
        const char* ext = (const char*)glGetStringFn(0x1F03);   // GL_EXTENSIONS (compatibility profile)
        meminfoState = ext && std::strstr(ext, "GL_ATI_meminfo") ? 1 : -1;
    }
    if (meminfo && meminfoState == 1 && glGetIntegervFn) {   // the context is current on this thread inside SwapBuffers' caller
        int v[4] = {-1, 0, 0, 0};
        glGetIntegervFn(kVboFreeAti, v); r.vboFree = v[0];
        v[0] = -1; glGetIntegervFn(kTexFreeAti, v); r.texFree = v[0];
        v[0] = -1; glGetIntegervFn(kRbFreeAti, v); r.rbFree = v[0];
    }
    PROCESS_MEMORY_COUNTERS_EX pmc{}; pmc.cb = sizeof pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof pmc)) {
        r.privMB = pmc.PrivateUsage / 1048576.0;
        r.pageFaults = lastFaults ? pmc.PageFaultCount - lastFaults : 0;
        lastFaults = pmc.PageFaultCount;
        r.wsMB = pmc.WorkingSetSize / 1048576.0;
    }
    if (dwmTiming) {
        DWM_TIMING_INFO ti{}; ti.cbSize = sizeof ti;
        if (SUCCEEDED(dwmTiming(nullptr, &ti))) { r.dwmMissed = ti.cFramesMissed; r.dwmDropped = ti.cFramesDropped; }
    }
    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) { r.availMB = (unsigned)(ms.ullAvailPhys >> 20); r.commitMB = (unsigned)((ms.ullTotalPageFile - ms.ullAvailPageFile) >> 20); }
    r.inputMs = inputMs; r.inputMaxMs = inputMaxMs; r.inputCalls = inputCalls;
    inputMs = inputMaxMs = 0.0; inputCalls = 0;
    ring[head % kRing] = r;
    ++head;
    if (r.swapMs >= stallMs || r.inputMs >= stallMs || r.interval >= stallMs) { ++stalls; dump(r); }
    return ok;
}

void addInput(double ms) { inputMs += ms; if (ms > inputMaxMs) inputMaxMs = ms; ++inputCalls; }
BOOL WINAPI hPeek(LPMSG m, HWND h, UINT a, UINT b, UINT c) { const double t = nowMs(); BOOL r = oPeek(m, h, a, b, c); addInput(nowMs() - t); return r; }
LRESULT WINAPI hDispatch(const MSG* m) { const double t = nowMs(); LRESULT r = oDispatch(m); addInput(nowMs() - t); return r; }
SHORT WINAPI hKey(int vk) { const double t = nowMs(); SHORT r = oKey(vk); addInput(nowMs() - t); return r; }

template <class F> int patch(const char* name, F hook, F& original) {   // the exe's import slots (as WFC_FILELOG)
    auto* base = (unsigned char*)GetModuleHandleW(nullptr);
    auto* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    int n = 0;
    for (auto* imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (!imp->OriginalFirstThunk) continue;
        auto* names = (IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
        auto* slots = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            if (std::strcmp((const char*)((IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name, name) != 0) continue;
            DWORD old = 0;
            if (!VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) continue;
            if (!original) original = (F)(void*)slots->u1.Function;
            slots->u1.Function = (ULONG_PTR)(void*)hook;
            VirtualProtect(&slots->u1.Function, sizeof(void*), old, &old);
            ++n;
        }
    }
    return n;
}

struct WsMin {   // WFC_WSMIN_MB: soft minimum working set (no privilege needed for a soft limit)
    WsMin() {
        const char* e = std::getenv("WFC_WSMIN_MB");
        if (!e || std::atoi(e) <= 0) return;
        const SIZE_T mn = (SIZE_T)std::atoi(e) << 20;
        const BOOL ok = SetProcessWorkingSetSizeEx(GetCurrentProcess(), mn, mn + ((SIZE_T)1 << 30), QUOTA_LIMITS_HARDWS_MIN_DISABLE | QUOTA_LIMITS_HARDWS_MAX_DISABLE);
        LOG_INFO("WFC_WSMIN_MB: soft minimum working set %d MB %s (error %lu)", std::atoi(e), ok ? "set" : "NOT set", ok ? 0ul : GetLastError());
    }
} gWsMin;

struct Installer {
    Installer() {
        const char* e = std::getenv("WFC_PRESENTTRACE");
        if (!e || !*e) return;
        if (std::atof(e) > 1.0) stallMs = std::atof(e);
        t0 = std::chrono::steady_clock::now();
        if (patch("SwapBuffers", (SwapFn)hSwap, oSwap) == 0) return;
        patch("PeekMessageW", (PeekFn)hPeek, oPeek);
        patch("DispatchMessageW", (DispatchFn)hDispatch, oDispatch);
        patch("GetAsyncKeyState", (KeyFn)hKey, oKey);
        if (HMODULE gl = LoadLibraryA("opengl32.dll")) {
            glGetIntegervFn = (GetIntFn)(void*)GetProcAddress(gl, "glGetIntegerv");
            glGetStringFn = (GetStrFn)(void*)GetProcAddress(gl, "glGetString");
        }
        if (HMODULE dwm = LoadLibraryA("dwmapi.dll")) dwmTiming = (DwmTimingFn)(void*)GetProcAddress(dwm, "DwmGetCompositionTimingInfo");
        meminfo = true;   // the queries return -1 (untouched) when the driver lacks GL_ATI_meminfo
        if (FILE* f = std::fopen("wfc_presenttrace.txt", "w")) {
            std::fprintf(f, "# WFC_PRESENTTRACE: SwapBuffers hooked, stalls >= %.1f ms dump the previous %d frames\n", stallMs, kRing);
            std::fclose(f);
        }
    }
} gInstaller;

}   // namespace
#endif
