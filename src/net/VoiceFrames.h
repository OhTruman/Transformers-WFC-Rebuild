#pragma once
// [Systems] Voice capture framing (ONLINE_MP_ARCHITECTURE.md section 7): the capture device's samples (float, any rate / channel count)
// -> mono 16 kHz, 20 ms frames of 320 int16 samples - the codec's input. Box-filtered resampling (each output sample = the mean of the
// input samples it covers; enough for voice at 16 kHz), a per-frame RMS level (0..1, the Settings > Audio mic-test meter and the
// voice-activity gate). Platform neutral; the device side is IVoiceCapture (Win32VoiceCapture.cpp: WASAPI shared-mode capture).
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

namespace net {

constexpr int kVoiceRate = 16000;
constexpr int kVoiceFrameSamples = 320;   // 20 ms at 16 kHz
using VoiceFrame = std::array<int16_t, kVoiceFrameSamples>;

class VoiceFramer {
public:
    void configure(int inputRate, int channels);
    void push(const float* interleaved, size_t frames);   // device frames (channels samples each)
    bool pop(VoiceFrame& out);                            // the next complete 20 ms frame
    float level() const { return level_.load(); }         // RMS of the last completed frame, 0..1 (pre-gate)
    size_t queued() const;

private:
    int rate_ = 48000, channels_ = 1;
    double step_ = 3.0;          // input samples per output sample
    double acc_ = 0.0, need_ = 3.0;
    double sum_ = 0.0;
    VoiceFrame cur_{};
    int curN_ = 0;
    double frameSq_ = 0.0;
    std::atomic<float> level_{0.0f};
    mutable std::mutex m_;
    std::deque<VoiceFrame> out_;
};

// The device side (WASAPI on Windows; null elsewhere). start() opens the default capture endpoint; frames are pulled with pop().
class IVoiceCapture {
public:
    virtual ~IVoiceCapture() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
    virtual bool pop(VoiceFrame& out) = 0;
    virtual float level() const = 0;
};
std::unique_ptr<IVoiceCapture> createVoiceCapture();

}  // namespace net
