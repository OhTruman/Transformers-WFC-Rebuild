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

// Unhandled-exception report (any thread): wfc_crash_<pid>.txt in the working directory - exception code / address,
// the render phase and frame, and the faulting thread's stack (RVAs, named from wfc_rebuild.map beside the exe when
// present) - plus a minidump wfc_crash_<pid>.dmp with the exception context. No behaviour change otherwise: the
// process still terminates as before. Installed first thing in main(); WFC_NOCRASHHANDLER=1 disables it,
// WFC_CRASHTEST=1 faults right after installing (verification).
void installCrashHandler();

}  // namespace render::watchdog
