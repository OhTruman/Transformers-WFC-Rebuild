// Clean-room reconstruction — frontend frame-hitch profiler (diagnostics, off by default).
// WFC_FRAMEPROF=<ms>: any frontend frame longer than <ms> logs "FLOW frame.hitch" with the time spent per category in
// that frame (movie parse, AS advance, image upload, shape tessellation, scene load / draw, audio, preview loads, ...).
// Instrumented code adds time with core::prof::Scope; the frontend loop closes each frame with frameEnd().
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace core::prof {

inline double threshold() {
    static const double t = [] { const char* e = std::getenv("WFC_FRAMEPROF"); return e ? std::atof(e) / 1000.0 : 0.0; }();
    return t;
}
inline bool enabled() { return threshold() > 0.0; }
inline std::map<std::string, double>& frame() { static std::map<std::string, double> m; return m; }
inline double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
inline void add(const char* category, double seconds) { if (enabled()) frame()[category] += seconds; }

struct Scope {
    const char* category;
    double t0;
    explicit Scope(const char* c) : category(c), t0(enabled() ? now() : 0.0) {}
    ~Scope() { if (enabled()) add(category, now() - t0); }
};

// Ends a frame: returns the summary line when the frame was a hitch ("" otherwise) and clears the categories.
inline std::string frameEnd(double frameSeconds) {
    std::string out;
    if (enabled() && frameSeconds > threshold()) {
        std::vector<std::pair<double, std::string>> v;
        double accounted = 0;
        for (const auto& [k, s] : frame()) { v.push_back({s, k}); if (k[0] != '+') accounted += s; }   // "+x": a wrapper of other scopes
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        char b[96];
        std::snprintf(b, sizeof b, "ms=%.1f", frameSeconds * 1000.0);
        out = b;
        for (const auto& [s, k] : v) {
            if (s < 0.0005) continue;
            std::snprintf(b, sizeof b, " %s=%.1f", k.c_str(), s * 1000.0);
            out += b;
        }
        std::snprintf(b, sizeof b, " other=%.1f", (frameSeconds - accounted) * 1000.0);
        out += b;
    }
    frame().clear();
    return out;
}

} // namespace core::prof
