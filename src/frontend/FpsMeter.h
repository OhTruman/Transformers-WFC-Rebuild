// PC EXTENSION — the FPS counter's measurement (Settings > Graphics > FPS Counter, user playtest 2026-10-13).
// Rendering's guidance: the frontend's own frame interval (what the player feels: CPU + GPU + present), refreshed 4 times
// a second over a 0.5 s window (the last two 0.25 s windows), drawn in the UI layer.
#pragma once
#include <cmath>
#include <cstdio>
#include <string>

namespace frontend {

class FpsMeter {
public:
    static constexpr double kRefresh = 0.25;   // seconds per readout update
    // One presented frame at time t (seconds, monotonic). True when the readout changed.
    bool frame(double t) {
        if (start_ < 0.0) { start_ = t; return false; }
        ++frames_;
        const double span = t - start_;
        if (span < kRefresh) return false;
        const double rate = frames_ / span;
        fps_ = prevRate_ > 0.0 ? (rate + prevRate_) * 0.5 : rate;
        prevRate_ = rate;
        frames_ = 0;
        start_ = t;
        return true;
    }
    // A pause in drawing (counter off, a long load): start a new window instead of averaging the gap in.
    void reset() { start_ = -1.0; frames_ = 0; prevRate_ = 0.0; fps_ = 0.0; }
    double fps() const { return fps_; }
    double ms() const { return fps_ > 0.0 ? 1000.0 / fps_ : 0.0; }
    // "144 FPS  6.9 ms" ("-- FPS" before the first readout).
    std::string text() const {
        if (fps_ <= 0.0) return "-- FPS";
        char b[48];
        std::snprintf(b, sizeof b, "%d FPS  %.1f ms", (int)std::lround(fps_), ms());
        return b;
    }

private:
    double start_ = -1.0, prevRate_ = 0.0, fps_ = 0.0;
    int frames_ = 0;
};

} // namespace frontend
