// Clean-room reconstruction — presented-frame rate limiter (PC adaptation; the original runs at the console's
// vsync). Paces presentation only: the simulation keeps its own fixed step, physics and animation are unchanged.
#pragma once

namespace render {

class FrameLimiter {
public:
    FrameLimiter();
    ~FrameLimiter();
    void setLimit(float hz);          // 0 / negative = unlimited
    float limit() const { return hz_; }
    // Waits until the next frame slot. Deadline-scheduled: the next slot is one period after the previous one, or
    // "now" when the frame ran late (no catch-up burst after a hitch). A high-resolution waitable timer sleeps until
    // ~1 ms before the slot, then a short yield-spin lands on it. Returns the time waited (ms).
    double wait();

private:
    float hz_ = 0.0f;
    double next_ = 0.0;               // seconds on the steady clock
    void* timer_ = nullptr;           // Windows high-resolution waitable timer (null elsewhere / unavailable)
};

}  // namespace render
