#include "core/LoadYield.h"

#include "core/Log.h"

#include <chrono>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <map>
#include <vector>
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
// WFC_LOADSTAGES: per-label wall time since the previous step
const bool g_stagesOn = std::getenv("WFC_LOADSTAGES") != nullptr;
const auto g_t0 = std::chrono::steady_clock::now();
auto g_stageAt = g_t0;
struct StageTime { double ms = 0.0; long steps = 0; };
std::map<std::string, StageTime> g_stageTimes;
std::map<std::string, StageTime> g_partTimes;   // addLoadTime: measured inside the steps
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
    if (g_stagesOn && !g_inside) {
        const auto t = std::chrono::steady_clock::now();
        StageTime& st = g_stageTimes[where ? where : "?"];
        st.ms += std::chrono::duration<double, std::milli>(t - g_stageAt).count(); ++st.steps;
        g_stageAt = t;
    }
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

void addLoadTime(const char* what, double ms) {
    if (!g_stagesOn) return;
    StageTime& st = g_partTimes[what ? what : "?"];
    st.ms += ms; ++st.steps;
}

void logLoadStages(const char* when) {
    if (!g_stagesOn) return;
    const auto t = std::chrono::steady_clock::now();
    const double total = std::chrono::duration<double, std::milli>(t - g_t0).count();
    const double tail = std::chrono::duration<double, std::milli>(t - g_stageAt).count();
    std::vector<std::pair<std::string, StageTime>> v(g_stageTimes.begin(), g_stageTimes.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
    double inSteps = 0.0; long steps = 0;
    for (const auto& kv : v) { inSteps += kv.second.ms; steps += kv.second.steps; }
    LOG_INFO("LOADSTAGES %s: %.0f ms since start, %ld steps in %zu labels (%.0f ms), %.0f ms after the last step", when, total, steps,
             v.size(), inSteps, tail);
    for (size_t i = 0; i < v.size() && i < 15; ++i)
        LOG_INFO("LOADSTAGES   %8.0f ms %5.1f%%  %6ld steps  %s", v[i].second.ms, total > 0 ? 100.0 * v[i].second.ms / total : 0.0,
                 v[i].second.steps, v[i].first.c_str());
    for (const auto& kv : g_partTimes)   // measured parts (inside the steps above, not added to them)
        LOG_INFO("LOADSTAGES part %8.0f ms %5.1f%%  %6ld calls  %s", kv.second.ms, total > 0 ? 100.0 * kv.second.ms / total : 0.0, kv.second.steps,
                 kv.first.c_str());
}

LoadProgress loadProgress() {
    LoadProgress p;
    p.active = g_progOn; p.fraction = g_frac; p.stage = g_stage; p.steps = g_steps; p.expected = g_expected;
    return p;
}

} // namespace core
