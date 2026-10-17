// [Systems] Voice capture framing - see VoiceFrames.h.
#include "net/VoiceFrames.h"

#include <algorithm>
#include <cmath>

namespace net {

void VoiceFramer::configure(int inputRate, int channels) {
    std::lock_guard<std::mutex> lk(m_);
    rate_ = std::max(8000, inputRate);
    channels_ = std::max(1, channels);
    step_ = (double)rate_ / (double)kVoiceRate;
    acc_ = 0.0; need_ = step_; sum_ = 0.0; curN_ = 0; frameSq_ = 0.0;
    out_.clear();
}

void VoiceFramer::push(const float* in, size_t frames) {
    for (size_t f = 0; f < frames; ++f) {
        float mono = 0.0f;
        for (int c = 0; c < channels_; ++c) mono += in[f * (size_t)channels_ + (size_t)c];
        mono /= (float)channels_;
        // Box filter: each output sample is the mean of the `step_` input samples it covers (an input sample can be split between two
        // outputs, or - below 16 kHz input - feed several).
        double remaining = 1.0;
        while (remaining > 1e-9) {
            const double take = std::min(remaining, need_ - acc_);
            sum_ += mono * take; acc_ += take; remaining -= take;
            if (acc_ >= need_ - 1e-9) {
                const float c = std::max(-1.0f, std::min(1.0f, (float)(sum_ / step_)));
                cur_[(size_t)curN_++] = (int16_t)std::lround(c * 32767.0f);
                frameSq_ += (double)c * c;
                if (curN_ == kVoiceFrameSamples) {
                    level_.store((float)std::sqrt(frameSq_ / kVoiceFrameSamples));
                    std::lock_guard<std::mutex> lk(m_);
                    out_.push_back(cur_);
                    while (out_.size() > 50) out_.pop_front();   // 1 s backlog at most (a stalled consumer drops the oldest)
                    curN_ = 0; frameSq_ = 0.0;
                }
                sum_ = 0.0; acc_ = 0.0;
            }
        }
    }
}

bool VoiceFramer::pop(VoiceFrame& out) {
    std::lock_guard<std::mutex> lk(m_);
    if (out_.empty()) return false;
    out = out_.front();
    out_.pop_front();
    return true;
}

size_t VoiceFramer::queued() const {
    std::lock_guard<std::mutex> lk(m_);
    return out_.size();
}

#ifndef _WIN32
namespace {
class NullVoiceCapture final : public IVoiceCapture {
public:
    bool start() override { return false; }
    void stop() override {}
    bool running() const override { return false; }
    bool pop(VoiceFrame&) override { return false; }
    float level() const override { return 0.0f; }
};
}  // namespace
std::unique_ptr<IVoiceCapture> createVoiceCapture() { return std::make_unique<NullVoiceCapture>(); }
#endif

}  // namespace net
