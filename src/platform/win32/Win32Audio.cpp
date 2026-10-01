// Clean-room reconstruction — Win32 audio backend: a small polling waveOut mixer.
// Decodes PCM WAV, normalises to the mix format (48 kHz / stereo / 16-bit), and mixes
// overlapping one-shot voices into double-buffered waveOut blocks pumped from update().
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include "audio/Audio.h"
#include "core/Log.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace audio {
namespace {

constexpr int kRate = 48000;
constexpr int kChannels = 2;
constexpr int kBlockFrames = 1024;                 // ~21 ms per block
constexpr int kBlockSamples = kBlockFrames * kChannels;
constexpr int kNumBlocks = 4;

uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// Decode a PCM WAV and resample/rechannel to kRate/kChannels int16 interleaved.
bool loadWav(const std::string& path, std::vector<int16_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamoff n = f.tellg();
    if (n < 44) return false;
    std::vector<uint8_t> buf((size_t)n);
    f.seekg(0); f.read((char*)buf.data(), n);
    const uint8_t* d = buf.data();
    if (std::memcmp(d, "RIFF", 4) || std::memcmp(d + 8, "WAVE", 4)) return false;

    int fmt = 0, ch = 0, rate = 0, bits = 0;
    const uint8_t* data = nullptr; size_t dataLen = 0;
    size_t off = 12;
    while (off + 8 <= (size_t)n) {
        uint32_t sz = rd32(d + off + 4);
        const uint8_t* c = d + off + 8;
        if (!std::memcmp(d + off, "fmt ", 4)) {
            fmt = rd16(c); ch = rd16(c + 2); rate = (int)rd32(c + 4); bits = rd16(c + 14);
        } else if (!std::memcmp(d + off, "data", 4)) {
            data = c; dataLen = sz;
        }
        off += 8 + sz + (sz & 1);
    }
    if (!data || ch < 1 || rate < 1) return false;
    if (fmt != 1 || bits != 16) { LOG_WARN("wav: unsupported fmt=%d bits=%d %s", fmt, bits, path.c_str()); return false; }

    const int16_t* src = reinterpret_cast<const int16_t*>(data);
    size_t srcFrames = dataLen / (size_t)(ch * 2);
    // Nearest-neighbour resample to kRate, duplicate/mix channels to kChannels.
    size_t outFrames = (size_t)((double)srcFrames * kRate / rate);
    out.resize(outFrames * kChannels);
    for (size_t i = 0; i < outFrames; ++i) {
        size_t sf = (size_t)((double)i * rate / kRate);
        if (sf >= srcFrames) sf = srcFrames - 1;
        int16_t l = src[sf * ch];
        int16_t r = ch > 1 ? src[sf * ch + 1] : l;
        out[i * kChannels] = l;
        out[i * kChannels + 1] = r;
    }
    return true;
}

struct Voice {
    const std::vector<int16_t>* data = nullptr;
    size_t pos = 0;
    float vol = 1.0f;
    bool positional = false;
    core::Vec3 wpos{0, 0, 0};
    float refDist = 5.0f, maxDist = 60.0f;
    float gL = 1.0f, gR = 1.0f;   // per-block resolved channel gains
    bool active = false;
};

class Win32Audio final : public IAudio {
public:
    bool init() {
        WAVEFORMATEX wf{};
        wf.wFormatTag = WAVE_FORMAT_PCM;
        wf.nChannels = kChannels;
        wf.nSamplesPerSec = kRate;
        wf.wBitsPerSample = 16;
        wf.nBlockAlign = kChannels * 2;
        wf.nAvgBytesPerSec = kRate * wf.nBlockAlign;
        if (waveOutOpen(&wo_, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            LOG_WARN("audio: no output device; running silent");
            return false;
        }
        for (int b = 0; b < kNumBlocks; ++b) {
            blocks_[b].assign(kBlockSamples, 0);
            WAVEHDR& h = hdr_[b];
            std::memset(&h, 0, sizeof(h));
            h.lpData = (LPSTR)blocks_[b].data();
            h.dwBufferLength = kBlockSamples * sizeof(int16_t);
            waveOutPrepareHeader(wo_, &h, sizeof(h));
            h.dwFlags |= WHDR_DONE;   // mark as free so update() fills+queues it
        }
        ok_ = true;
        return true;
    }

    Sound load(const std::string& path) override {
        std::vector<int16_t> pcm;
        if (!loadWav(path, pcm)) return kInvalidSound;
        sounds_.push_back(std::move(pcm));
        return (Sound)(sounds_.size() - 1);
    }

    Voice* freeVoice() {
        for (Voice& v : voices_) if (!v.active) return &v;
        voices_.push_back(Voice{});
        return &voices_.back();
    }

    void play(Sound s, float volume) override {
        if (!ok_ || s < 0 || (size_t)s >= sounds_.size()) return;
        Voice* v = freeVoice();
        *v = Voice{}; v->data = &sounds_[(size_t)s]; v->vol = volume; v->active = true;
    }

    void playAt(Sound s, const core::Vec3& pos, float volume, float refDist, float maxDist) override {
        if (!ok_ || s < 0 || (size_t)s >= sounds_.size()) return;
        Voice* v = freeVoice();
        *v = Voice{}; v->data = &sounds_[(size_t)s]; v->vol = volume; v->active = true;
        v->positional = true; v->wpos = pos; v->refDist = refDist; v->maxDist = maxDist;
    }

    void setListener(const core::Vec3& pos, const core::Vec3& fwd, const core::Vec3& right) override {
        lpos_ = pos; lfwd_ = fwd; lright_ = right;
    }

    void update() override {
        if (!ok_) return;
        for (int b = 0; b < kNumBlocks; ++b) {
            if (!(hdr_[b].dwFlags & WHDR_DONE)) continue;
            mixBlock(blocks_[b]);
            hdr_[b].dwFlags &= ~WHDR_DONE;
            hdr_[b].dwBufferLength = kBlockSamples * sizeof(int16_t);
            waveOutWrite(wo_, &hdr_[b], sizeof(WAVEHDR));
        }
    }

    ~Win32Audio() override {
        if (ok_) {
            waveOutReset(wo_);
            for (int b = 0; b < kNumBlocks; ++b) waveOutUnprepareHeader(wo_, &hdr_[b], sizeof(WAVEHDR));
            waveOutClose(wo_);
        }
    }

private:
    void resolveGains(Voice& v) {
        float g = v.vol * master_;
        if (!v.positional) { v.gL = v.gR = g; return; }
        core::Vec3 d = v.wpos - lpos_;
        float dist = core::length(d);
        float atten = dist <= v.refDist ? 1.0f
                     : (dist >= v.maxDist ? 0.0f : (v.maxDist - dist) / (v.maxDist - v.refDist));
        g *= atten;
        float pan = 0.0f;
        if (dist > 1e-3f) pan = core::clampf(core::dot(d * (1.0f / dist), lright_), -1.0f, 1.0f);
        float ang = (pan + 1.0f) * 0.25f * core::PI;   // equal-power pan: -1=>L, +1=>R
        v.gL = std::cos(ang) * g;
        v.gR = std::sin(ang) * g;
    }

    void mixBlock(std::vector<int16_t>& dst) {
        static std::vector<int32_t> acc;
        acc.assign(kBlockSamples, 0);
        for (Voice& v : voices_) {
            if (!v.active) continue;
            resolveGains(v);
            const std::vector<int16_t>& s = *v.data;
            for (int f = 0; f < kBlockFrames; ++f) {
                if (v.pos + 1 >= s.size()) { v.active = false; break; }
                int16_t sl = s[v.pos], sr = s[v.pos + 1];
                v.pos += 2;
                if (v.positional) {
                    int32_t mono = (sl + sr) / 2;
                    acc[f * 2]     += (int32_t)(mono * v.gL);
                    acc[f * 2 + 1] += (int32_t)(mono * v.gR);
                } else {
                    acc[f * 2]     += (int32_t)(sl * v.gL);
                    acc[f * 2 + 1] += (int32_t)(sr * v.gR);
                }
            }
        }
        for (int i = 0; i < kBlockSamples; ++i) {
            int32_t x = acc[i];
            if (x > 32767) x = 32767; else if (x < -32768) x = -32768;
            dst[i] = (int16_t)x;
        }
    }

    HWAVEOUT wo_ = nullptr;
    bool ok_ = false;
    float master_ = 0.5f;                 // [PROV] overall SFX level (was far too loud at 1.0)
    core::Vec3 lpos_{0, 0, 0}, lfwd_{0, 0, -1}, lright_{1, 0, 0};
    std::vector<std::vector<int16_t>> sounds_;
    std::vector<Voice> voices_;
    std::vector<int16_t> blocks_[kNumBlocks];
    WAVEHDR hdr_[kNumBlocks];
};

// Silent fallback when there is no audio device.
class NullAudio final : public IAudio {
public:
    Sound load(const std::string&) override { return kInvalidSound; }
    void play(Sound, float) override {}
    void playAt(Sound, const core::Vec3&, float, float, float) override {}
    void setListener(const core::Vec3&, const core::Vec3&, const core::Vec3&) override {}
    void update() override {}
};

} // namespace

IAudio* createAudio() {
    auto* a = new Win32Audio();
    if (a->init()) return a;
    delete a;
    return new NullAudio();
}

} // namespace audio
#endif
