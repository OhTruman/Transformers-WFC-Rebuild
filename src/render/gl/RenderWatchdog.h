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

}  // namespace render::watchdog
