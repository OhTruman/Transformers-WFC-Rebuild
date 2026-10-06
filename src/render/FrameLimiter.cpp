// Clean-room reconstruction — presented-frame rate limiter.
#include "render/FrameLimiter.h"

#include <algorithm>
#include <chrono>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

namespace render {
namespace {
double nowS() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

FrameLimiter::FrameLimiter() {
#ifdef _WIN32
    // Windows 10 1803+: sub-millisecond waits without raising the global timer resolution
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
#endif
}

FrameLimiter::~FrameLimiter() {
#ifdef _WIN32
    if (timer_) CloseHandle((HANDLE)timer_);
#endif
}

void FrameLimiter::setLimit(float hz) {
    hz_ = hz > 0.0f ? hz : 0.0f;
    next_ = 0.0;
}

double FrameLimiter::wait() {
    if (hz_ <= 0.0f) return 0.0;
    const double period = 1.0 / hz_;
    const double t0 = nowS();
    if (next_ <= 0.0 || t0 - next_ > period) next_ = t0;   // first frame, or late by more than a frame: no burst
    const double target = next_;
    double remain = target - t0;
    if (remain > 0.0015) {                                   // sleep to ~1 ms before the slot
        const double sleepS = remain - 0.001;
#ifdef _WIN32
        if (timer_) {
            LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(sleepS * 1.0e7);   // relative, 100 ns units
            if (SetWaitableTimer((HANDLE)timer_, &due, 0, nullptr, nullptr, FALSE))
                WaitForSingleObject((HANDLE)timer_, INFINITE);
        } else {
            std::this_thread::sleep_for(std::chrono::duration<double>(sleepS));
        }
#else
        std::this_thread::sleep_for(std::chrono::duration<double>(sleepS));
#endif
    }
    while (nowS() < target) std::this_thread::yield();      // the last ~1 ms
    next_ = target + period;
    return (nowS() - t0) * 1000.0;
}

}  // namespace render
