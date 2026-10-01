// Clean-room reconstruction — high-resolution timing (platform-independent interface).
#pragma once
#include <cstdint>

namespace core {

// Returns seconds since first call, using a monotonic high-resolution clock.
double nowSeconds();

// Fixed-timestep accumulator for deterministic gameplay simulation.
class FixedStepClock {
public:
    explicit FixedStepClock(double hz = 60.0) : step_(1.0 / hz) {}

    // Advance real time; returns the number of fixed steps to run this frame.
    int tick(double realDeltaSeconds) {
        accumulator_ += realDeltaSeconds;
        // Clamp to avoid spiral-of-death after a long stall.
        if (accumulator_ > step_ * kMaxSteps) accumulator_ = step_ * kMaxSteps;
        int steps = 0;
        while (accumulator_ >= step_) {
            accumulator_ -= step_;
            ++steps;
        }
        return steps;
    }

    float stepSeconds() const { return static_cast<float>(step_); }
    float alpha() const { return static_cast<float>(accumulator_ / step_); } // render interpolation

private:
    static constexpr int kMaxSteps = 8;
    double step_;
    double accumulator_ = 0.0;
};

} // namespace core
