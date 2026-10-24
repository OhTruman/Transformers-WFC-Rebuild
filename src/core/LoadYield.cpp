#include "core/LoadYield.h"

#include "core/Log.h"

#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

namespace core {

namespace {
LoadYieldFn g_fn;
std::chrono::steady_clock::time_point g_last = std::chrono::steady_clock::now();
LoadYieldStats g_stats;
bool g_inside = false;
constexpr double kIntervalMs = 1000.0 / 60.0;
// loading progress (beginLoadProgress)
bool g_progOn = false;
std::string g_progKey;
long g_steps = 0, g_expected = 0;
float g_frac = 0.0f;
const char* g_stage = "";
const char* kProfileFile = "wfc_loadprofile.ini";   // (next to wfc_profile.ini: key=steps per line)
std::map<std::string, long> readProfile() {
    std::map<std::string, long> m;
    std::ifstream f(kProfileFile);
    std::string line;
    while (std::getline(f, line)) {
        const size_t eq = line.rfind('=');
        if (eq != std::string::npos && eq > 0) m[line.substr(0, eq)] = std::atol(line.c_str() + eq + 1);
    }
    return m;
}
}

void setLoadYield(LoadYieldFn fn) {
    g_fn = std::move(fn);
    g_last = std::chrono::steady_clock::now();
}

void loadYield(const char* where) {
    if (g_progOn && !g_inside) {                       // (every call is a load step, presented or not)
        ++g_steps;
        g_stage = where ? where : "";
        const float f = g_expected > 0 ? std::min(0.99f, (float)g_steps / (float)g_expected)
                                       : 0.9f * (1.0f - std::exp(-(float)g_steps / 600.0f));
        if (f > g_frac) g_frac = f;
    }
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

void beginLoadProgress(const std::string& key) {
    g_progOn = true; g_progKey = key; g_steps = 0; g_frac = 0.0f; g_stage = "";
    const auto m = readProfile();
    const auto it = m.find(key);
    g_expected = it != m.end() ? it->second : 0;
    LOG_INFO("load progress: %s, %ld steps expected%s", key.c_str(), g_expected, g_expected ? "" : " (first load: no history)");
}

void endLoadProgress() {
    if (!g_progOn) return;
    g_progOn = false; g_frac = 1.0f;
    auto m = readProfile();
    m[g_progKey] = g_steps;
    std::ofstream f(kProfileFile, std::ios::trunc);
    for (const auto& kv : m) f << kv.first << '=' << kv.second << '\n';
    LOG_INFO("load progress: %s done in %ld steps (expected %ld)", g_progKey.c_str(), g_steps, g_expected);
}

LoadProgress loadProgress() {
    LoadProgress p;
    p.active = g_progOn; p.fraction = g_frac; p.stage = g_stage; p.steps = g_steps; p.expected = g_expected;
    return p;
}

} // namespace core
