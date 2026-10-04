#include "core/LoadYield.h"

#include "core/Log.h"

#include <chrono>

namespace core {

namespace {
LoadYieldFn g_fn;
std::chrono::steady_clock::time_point g_last = std::chrono::steady_clock::now();
LoadYieldStats g_stats;
bool g_inside = false;
constexpr double kIntervalMs = 1000.0 / 60.0;
}

void setLoadYield(LoadYieldFn fn) {
    g_fn = std::move(fn);
    g_last = std::chrono::steady_clock::now();
}

void loadYield(const char* where) {
    if (!g_fn || g_inside) return;
    auto now = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(now - g_last).count();
    if (ms < kIntervalMs) return;
    if (ms > g_stats.maxGapMs) { g_stats.maxGapMs = ms; g_stats.maxGapAt = where; }
    if (ms > 250.0) LOG_INFO("load: %.0f ms without a presented frame, ending at '%s'", ms, where);
    ++g_stats.frames;
    g_inside = true;
    g_fn(ms / 1000.0);
    g_inside = false;
    g_last = std::chrono::steady_clock::now();
}

void resetLoadYieldStats() {
    g_stats = LoadYieldStats{};
    g_last = std::chrono::steady_clock::now();
}

LoadYieldStats loadYieldStats() { return g_stats; }

} // namespace core
