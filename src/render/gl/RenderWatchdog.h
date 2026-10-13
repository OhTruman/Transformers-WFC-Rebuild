// Clean-room reconstruction — render-thread stall watchdog (diagnostics for hangs: human playtest "the game froze",
// RX 7900 XTX, no TDR / WER record).
#pragma once

namespace render::watchdog {

// The render thread marks where it is (a static string) and when a frame completes. A side thread checks every
// second; when nothing has progressed (no frame completed, no phase marked) for kStallSeconds it logs the last phase, the frame number and the stall
// time, then writes one minidump with every thread's stack (wfc_hang_<pid>_<n>.dmp next to wfc.log) - a freeze then
// leaves evidence of where the main thread (driver swap, a GL call, a game-side wait) and the worker threads were.
// Armed by the first completed frame (boot movies before it are not stalls); a movie streaming frames through
// IRenderer::updateTexture counts as progress. WFC_NOWATCHDOG=1 disables it. Safe to call before start(); no cost beyond two atomic stores per mark.
void start();
void phase(const char* where);     // static-lifetime string
void frameDone(int frame);
void stop();

// [Systems] GPU driver reset / unrecoverable hang (AMD TDR, 2026-10-13: after a reset the main thread blocked in the driver -
// glGetQueryObjectiv spinning in atio6axx, or SwapBuffers never returning - and the process hung forever). Ends the process
// cleanly from any thread: logs `why`, writes wfc_gpu_lost_<pid>.txt (working directory), flushes the log, puts back the
// desktop display mode (a fullscreen run), shows a message box for at most WFC_GPULOST_DIALOG_S seconds (default 20, 0 in
// scripted runs: WFC_FRONTEND_SCRIPT / WFC_PLAYERBOT; 0 = none) and terminates with exit code 3 (no atexit / DLL teardown: the
// driver may hold its locks; harness runners treat 3 as "GPU reset: rerun"). Never returns.
[[noreturn]] void gpuLost(const char* why);
// The watchdog's backstop: a stalled render thread (>= 5 s) is ended through gpuLost only on evidence of GPU loss - a tiny
// D3D11 probe device per hardware adapter reports removed (a TDR removes every device on the adapter) - never on time alone
// (legitimate stalls run minutes); not under a debugger. WFC_HANG_EXIT_S (default -1 = off) adds an opt-in time-only cap.
// WFC_SIMHANG=1 blocks the render thread for good once (frame 60): with no removal it is logged and dumped only;
// WFC_SIMHANG=removed also makes the probe report a removed device (the evidence path, which must exit).
// WFC_SIMRESET=N makes GLRenderer::beginFrame's reset poll report a reset at its N-th call (the direct path).

// Unhandled-exception report (any thread): wfc_crash_<pid>.txt in the working directory - exception code / address,
// the render phase and frame, and the faulting thread's stack (RVAs, named from wfc_rebuild.map beside the exe when
// present) - plus a minidump wfc_crash_<pid>.dmp with the exception context. No behaviour change otherwise: the
// process still terminates as before. Installed first thing in main(); WFC_NOCRASHHANDLER=1 disables it,
// WFC_CRASHTEST=1 faults right after installing (verification).
void installCrashHandler();

}  // namespace render::watchdog
