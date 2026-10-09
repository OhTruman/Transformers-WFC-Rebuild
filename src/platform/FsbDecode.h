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
