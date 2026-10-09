#pragma once
// Returns the allocator's freed-but-committed memory to the OS (mimalloc: mi_collect(true) - purges freed pages and frees
// empty segments; CRT build: _heapmin). Call at load boundaries only (after a level unload, after the loading screen closes),
// never per frame: it walks the heap. Logs the private bytes released and its own time. WFC_NOHEAPTRIM=1 turns it off
// (A/B). Implemented in AllocProf.cpp.
namespace core {
void trimHeap(const char* why);
}
