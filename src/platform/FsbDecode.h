#pragma once
// Original FMOD sound banks (content/**/X.fsb, XMA2) decoded in-process by libvgmstream r2117 (third_party/vgmstream; the
// release that produced the WAVs): one pass of the stream as interleaved PCM16 - the game loops it itself (loop points /
// SoundNodeWaveEvent.bLooping). Bit-identical to X.wav's PCM over that length (tools/systems/fsb_gate.cpp). Thread-safe.
// Built only with WFC_VGMSTREAM; otherwise fsbDecodeAvailable() is false and decodeFsb fails (WAVs are used).
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace platform {

bool fsbDecodeAvailable();
bool decodeFsb(const std::string& path, std::vector<int16_t>& pcm, int& channels, int& rate);

// The same decode, block by block (decodeFsb is built on it, so both are covered by the gate): a long bank can be played
// while the rest of it is still decoding (progressive load). frames() is the one-pass length known at open.
class FsbStream {
public:
    FsbStream() = default;
    FsbStream(const FsbStream&) = delete;
    FsbStream& operator=(const FsbStream&) = delete;
    ~FsbStream();
    bool open(const std::string& path);
    int channels() const { return channels_; }
    int rate() const { return rate_; }
    long long frames() const { return frames_; }
    // The next decoded block (interleaved PCM16, valid until the next call): its frame count, 0 when done, < 0 on error.
    int next(const int16_t*& pcm);
private:
    void* lib_ = nullptr;   // libvgmstream_t
    void* sf_ = nullptr;    // libstreamfile_t
    int channels_ = 0, rate_ = 0;
    long long frames_ = 0;
    bool done_ = false;
};

// Where audio comes from: WFC_AUDIO_SOURCE=wav (WAVs only), fsb (banks first, WAV fallback), auto (default: the WAV when it
// exists, else the bank - a package without WAVs plays the banks).
enum class AudioSource { Auto, Wav, Fsb };
AudioSource audioSource();

struct FsbStats {
    std::atomic<long long> decodes{0}, mainThreadDecodes{0};
    std::atomic<long long> pcmBytes{0};
    std::atomic<long long> microseconds{0}, mainThreadMicroseconds{0};
};
FsbStats& fsbStats();

}   // namespace platform
