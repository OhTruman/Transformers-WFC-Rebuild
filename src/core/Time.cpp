#include "core/Time.h"

// Platform timing lives here but behind a neutral interface; gameplay never sees Win32.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace core {
double nowSeconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    static LARGE_INTEGER start = [] { LARGE_INTEGER s; QueryPerformanceCounter(&s); return s; }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return double(now.QuadPart - start.QuadPart) / double(freq.QuadPart);
}
} // namespace core

#else
#include <chrono>
namespace core {
double nowSeconds() {
    using clock = std::chrono::steady_clock;
    static auto start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}
} // namespace core
#endif
