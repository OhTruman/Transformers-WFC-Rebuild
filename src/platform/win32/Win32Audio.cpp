// Clean-room reconstruction — Win32 audio backend: a small polling waveOut mixer.
// Decodes PCM WAV, normalises to the mix format (48 kHz / stereo / 16-bit), and mixes
// overlapping one-shot voices into double-buffered waveOut blocks pumped from update().
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include "audio/Audio.h"
#include "core/Log.h"
#include "platform/FsbDecode.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
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
// Nearest-neighbour resample to kRate, duplicate / take the first two channels to kChannels.
void toOutput(const int16_t* src, size_t srcFrames, int ch, int rate, std::vector<int16_t>& out) {
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
}

std::string bankOf(const std::string& wav) {
    return wav.size() > 4 && (wav.compare(wav.size() - 4, 4, ".wav") == 0 || wav.compare(wav.size() - 4, 4, ".WAV") == 0)
               ? wav.substr(0, wav.size() - 4) + ".fsb" : std::string();
}

bool fileExists(const std::string& p) {
    const DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// The original bank beside the WAV decodes instead (platform::audioSource: fsb first, or auto when the WAV is not there).
bool bankFirst(const std::string& path) {
    if (!platform::fsbDecodeAvailable()) return false;
    const platform::AudioSource s = platform::audioSource();
    if (s == platform::AudioSource::Wav) return false;
    const std::string bank = bankOf(path);
    if (bank.empty() || !fileExists(bank)) return false;
    return s == platform::AudioSource::Fsb || !fileExists(path);
}

std::thread::id gMainThread;   // the thread that created the device (the game thread)

bool loadBank(const std::string& path, std::vector<int16_t>& out, int* srcRate) {
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<int16_t> pcm;
    int ch = 0, rate = 0;
    if (!platform::decodeFsb(bankOf(path), pcm, ch, rate)) { LOG_WARN("audio: bank decode failed: %s", bankOf(path).c_str()); return false; }
    if (srcRate) *srcRate = rate;
    toOutput(pcm.data(), pcm.size() / (size_t)ch, ch, rate, out);
    const long long us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    platform::FsbStats& st = platform::fsbStats();
    ++st.decodes; st.microseconds += us; st.pcmBytes += (long long)(pcm.size() * 2);
    if (std::this_thread::get_id() == gMainThread) {
        ++st.mainThreadDecodes; st.mainThreadMicroseconds += us;
        const long long n = st.mainThreadDecodes.load();
        if (n <= 50)
            LOG_WARN("audio: bank decoded on the main thread (%.2f ms, %.0f KB): %s", us / 1000.0, pcm.size() * 2 / 1024.0, path.c_str());
        else if (n % 100 == 0)
            LOG_WARN("audio: %lld banks decoded on the main thread so far (%.0f ms in total)", n, st.mainThreadMicroseconds.load() / 1000.0);
    }
    return true;
}

bool loadWav(const std::string& path, std::vector<int16_t>& out, int* srcRate = nullptr) {
    if (bankFirst(path)) return loadBank(path, out, srcRate);
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return bankOf(path).size() && platform::fsbDecodeAvailable() && fileExists(bankOf(path)) && loadBank(path, out, srcRate);
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
    if (srcRate) *srcRate = rate;
    if (fmt != 1 || bits != 16) { LOG_WARN("wav: unsupported fmt=%d bits=%d %s", fmt, bits, path.c_str()); return false; }

    toOutput(reinterpret_cast<const int16_t*>(data), dataLen / (size_t)(ch * 2), ch, rate, out);
    return true;
}

float mbToGain(float mb) { return mb <= -9999.0f ? 0.0f : std::pow(10.0f, mb / 2000.0f); }
float dbGain(float db) { return std::pow(10.0f, db / 20.0f); }
// WFC dBToLinear (0x82CBED00) [CONF]: clamp to [-96, 0] dB, -96 -> silence; never above unity.
float dbToLinear(float x) {
    x = std::min(0.0f, std::max(-96.0f, x));
    return x > -96.0f ? (float)std::pow(10.0, (double)x * 0.05) : 0.0f;
}

// MASTER_WET environment DSP: the zone preset's Echo followed by an FMOD-style I3DL2 SFX reverb
// (send). Structure [MED]: HF shelf on the send (RoomHF at HFReference), a tapped early-reflection
// line (ReflectionsDelay, Room+Reflections mB), and a stereo comb/allpass late tank (Room+Reverb mB,
// DecayTime -> comb feedback for a 60 dB decay, DecayHFRatio -> in-loop damping, Diffusion ->
// allpass gain), pre-delayed by ReflectionsDelay + ReverbDelay. Parameters cross-fade over the mixer
// preset's FadeInTime.
class EnvDsp {
public:
    void init() {
        static const int combs[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};   // Freeverb tunings @44.1k
        static const int aps[4] = {556, 441, 341, 225};
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < 8; ++i) comb_[c][i].assign((size_t)((combs[i] + c * 23) * kRate / 44100), 0.0f);
            for (int i = 0; i < 4; ++i) ap_[c][i].assign((size_t)((aps[i] + c * 23) * kRate / 44100), 0.0f);
        }
        pre_.assign((size_t)kRate, 0.0f);            // up to 0.5 s stereo pre-delay (interleaved)
        echo_.assign((size_t)(kRate * 2 * 2), 0.0f); // up to 2 s stereo echo (interleaved)
        cur_ = target_ = Environment{};
        derive();
    }
    void set(const Environment& e, float fade) {
        from_ = cur_; target_ = e; fadeLen_ = std::max(fade, 0.0f); fadePos_ = 0.0f;
        if (fadeLen_ <= 0.0f) { cur_ = target_; derive(); }
    }
    bool active() const { return gRefl_ > 0.0f || gLate_ > 0.0f || echoWet_ > 0.0f || tailLeft_ > 0; }

    // In-place on the interleaved MASTER_WET bus (`n` frames): echo, then add the reverb send.
    void process(float* bus, int n) {
        if (fadeLen_ > 0.0f && fadePos_ < fadeLen_) {        // cross-fade the preset (per block)
            fadePos_ += (float)n / kRate;
            float u = std::min(1.0f, fadePos_ / fadeLen_);
            auto L = [u](float a, float b) { return a + (b - a) * u; };
            Environment m = target_;
            m.room = L(from_.room, target_.room); m.roomHF = L(from_.roomHF, target_.roomHF);
            m.reflections = L(from_.reflections, target_.reflections); m.reverb = L(from_.reverb, target_.reverb);
            m.echoWet = L(from_.echoWet, target_.echoWet); m.echoDry = L(from_.echoDry, target_.echoDry);
            // mB gains fade in level, not log space, so "off" (-10000) does not snap
            gFadeRefl_ = L(mbToGain(from_.room + from_.reflections), mbToGain(target_.room + target_.reflections));
            gFadeLate_ = L(mbToGain(from_.room + from_.reverb), mbToGain(target_.room + target_.reverb));
            cur_ = m; derive(); gRefl_ = gFadeRefl_; gLate_ = gFadeLate_;
        }
        const size_t echoLen = echo_.size(), preLen = pre_.size();
        for (int f = 0; f < n; ++f) {
            float l = bus[f * 2], r = bus[f * 2 + 1];
            // Echo (category DSP): out = in*DryMix + delayed*WetMix, delay line fed back by DecayRatio.
            if (echoWet_ > 0.0f || echoDry_ != 1.0f) {
                size_t rd = (echoPos_ + echoLen - echoDelay_ * 2) % echoLen;
                float dl = echo_[rd], dr = echo_[rd + 1];
                echo_[echoPos_] = l + dl * echoDecay_; echo_[echoPos_ + 1] = r + dr * echoDecay_;
                l = l * echoDry_ + dl * echoWet_; r = r * echoDry_ + dr * echoWet_;
            }
            echoPos_ = (echoPos_ + 2) % echoLen;
            bus[f * 2] = l; bus[f * 2 + 1] = r;
            if (gRefl_ <= 0.0f && gLate_ <= 0.0f && tailLeft_ <= 0) continue;
            // Send: HF shelf (RoomHF at HFReference).
            float in = (l + r) * 0.5f;
            lp_ += lpK_ * (in - lp_);
            float send = lp_ + (in - lp_) * gHF_;
            pre_[prePos_] = send;
            float er = 0.0f;
            for (int t = 0; t < 4; ++t) er += pre_[(prePos_ + preLen - erTap_[t]) % preLen] * erGain_[t];
            float late = pre_[(prePos_ + preLen - lateTap_) % preLen] * (gLate_ > 0.0f ? 1.0f : 0.0f);
            prePos_ = (prePos_ + 1) % preLen;
            float out[2];
            for (int c = 0; c < 2; ++c) {
                float acc = 0.0f;
                for (int i = 0; i < 8; ++i) {
                    std::vector<float>& b = comb_[c][i];
                    size_t& ix = combPos_[c][i];
                    float y = b[ix];
                    combLp_[c][i] = y * (1.0f - damp_) + combLp_[c][i] * damp_;
                    b[ix] = late + combLp_[c][i] * fb_[i];
                    ix = (ix + 1) % b.size();
                    acc += y * combNorm_[i];
                }
                acc *= 0.35355339f;                     // 1/sqrt(8): unit-energy tank
                for (int i = 0; i < 4; ++i) {
                    std::vector<float>& b = ap_[c][i];
                    size_t& ix = apPos_[c][i];
                    float bo = b[ix];
                    float y = -acc * apG_ + bo;
                    b[ix] = acc + bo * apG_;
                    ix = (ix + 1) % b.size();
                    acc = y;
                }
                out[c] = acc * gLate_ + er * gRefl_;
            }
            bus[f * 2] += out[0]; bus[f * 2 + 1] += out[1];
        }
        tailLeft_ = (gRefl_ > 0.0f || gLate_ > 0.0f) ? (int)(cur_.decayTime * kRate) + kRate : std::max(0, tailLeft_ - n);
    }

private:
    void derive() {
        gRefl_ = mbToGain(cur_.room + cur_.reflections);
        gLate_ = mbToGain(cur_.room + cur_.reverb);
        gHF_ = cur_.roomHF <= -9999.0f ? 0.0f : std::pow(10.0f, cur_.roomHF / 2000.0f);
        lpK_ = 1.0f - std::exp(-2.0f * 3.14159265f * std::max(100.0f, cur_.hfReference) / kRate);
        int er0 = (int)(std::max(0.001f, cur_.reflectionsDelay) * kRate);
        static const float erT[4] = {1.0f, 1.31f, 1.73f, 2.19f}, erG[4] = {0.62f, 0.5f, 0.42f, 0.36f};
        for (int t = 0; t < 4; ++t) { erTap_[t] = std::min((int)pre_.size() - 1, (int)(er0 * erT[t])); erGain_[t] = erG[t]; }
        lateTap_ = std::min((int)pre_.size() - 1, (int)((cur_.reflectionsDelay + cur_.reverbDelay) * kRate) + 1);
        static const int combs[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        float T = std::max(0.1f, cur_.decayTime), Thf = std::max(0.05f, T * std::max(0.1f, cur_.decayHFRatio));
        for (int i = 0; i < 8; ++i) {
            float d = (float)combs[i] / 44100.0f;
            fb_[i] = std::pow(10.0f, -3.0f * d / T);                   // RT60
            combNorm_[i] = std::sqrt(std::max(0.0f, 1.0f - fb_[i] * fb_[i]));
        }
        float gHi = std::pow(10.0f, -3.0f * 0.03f / Thf), gLo = std::pow(10.0f, -3.0f * 0.03f / T);
        damp_ = std::min(0.7f, std::max(0.0f, 1.0f - gHi / gLo) * 4.0f);
        apG_ = 0.3f + 0.4f * std::min(1.0f, std::max(0.0f, cur_.diffusion / 100.0f));
        echoDelay_ = (size_t)std::min((float)(echo_.size() / 2 - 1), std::max(1.0f, cur_.echoDelayMs * 0.001f * kRate));
        echoDecay_ = std::min(0.95f, std::max(0.0f, cur_.echoDecay));
        echoWet_ = cur_.echoWet; echoDry_ = cur_.echoDry;
    }

    Environment cur_, target_, from_;
    float fadeLen_ = 0.0f, fadePos_ = 0.0f, gFadeRefl_ = 0.0f, gFadeLate_ = 0.0f;
    float gRefl_ = 0.0f, gLate_ = 0.0f, gHF_ = 0.0f, lpK_ = 0.5f, lp_ = 0.0f, damp_ = 0.2f, apG_ = 0.5f;
    float fb_[8] = {}, combNorm_[8] = {};
    int erTap_[4] = {}, lateTap_ = 1, tailLeft_ = 0;
    float erGain_[4] = {};
    std::vector<float> comb_[2][8], ap_[2][4], pre_, echo_;
    size_t combPos_[2][8] = {}, apPos_[2][4] = {}, prePos_ = 0, echoPos_ = 0, echoDelay_ = 1;
    float combLp_[2][8] = {};
    float echoDecay_ = 0.5f, echoWet_ = 0.0f, echoDry_ = 1.0f;
};

// Master category compressor (SoundConfig Master Default preset: Threshold -6 dB, Attack 10 ms,
// Release 50 ms, GainMakeup 0) [MED: DSPEffectConfig bit 32 read as the compressor; hard knee,
// limiting ratio]. Peak detector on the stereo master.
class Compressor {
public:
    void set(float thrDb, float attMs, float relMs, float makeupDb) {
        on_ = true; thr_ = dbGain(thrDb); makeup_ = dbGain(makeupDb);
        att_ = std::exp(-1.0f / (std::max(0.1f, attMs) * 0.001f * kRate));
        rel_ = std::exp(-1.0f / (std::max(0.1f, relMs) * 0.001f * kRate));
    }
    void process(float* buf, int n, float& peak, float& minGain) {
        for (int f = 0; f < n; ++f) {
            float a = std::max(std::fabs(buf[f * 2]), std::fabs(buf[f * 2 + 1]));
            peak = std::max(peak, a);
            if (!on_) continue;
            env_ = a > env_ ? att_ * env_ + (1.0f - att_) * a : rel_ * env_ + (1.0f - rel_) * a;
            float g = env_ > thr_ ? thr_ / env_ : 1.0f;
            minGain = std::min(minGain, g);
            buf[f * 2] *= g * makeup_; buf[f * 2 + 1] *= g * makeup_;
        }
    }
private:
    bool on_ = false;
    float thr_ = 1.0f, makeup_ = 1.0f, att_ = 0.0f, rel_ = 0.0f, env_ = 0.0f;
};

// A loaded sample: PCM at kRate plus its loop region in output frames (whole sample unless set).
struct Sample {
    std::vector<int16_t> pcm;
    int srcRate = 0;
    double loopStart = 0.0, loopEnd = -1.0;   // output frames; loopEnd exclusive, < 0: the sample end
    bool releasing = false;                   // released while a block was mixing: freed at its end, already gone for callers
    int gen = 0;                              // slot generation: a Sound handle = index | gen << kGenShift
};

struct Voice {
    const std::vector<int16_t>* data = nullptr;
    const Sample* sample = nullptr;
    double pos = 0.0;             // frame position (fractional when pitched)
    double rate = 1.0;            // playback rate (pitch)
    float vol = 1.0f;
    bool positional = false;
    bool inverse = false;         // FMOD inverse rolloff (cue voices) vs legacy linear
    core::Vec3 wpos{0, 0, 0};
    float refDist = 5.0f, maxDist = 60.0f, rolloff = 1.0f;
    float pan2D = 0.0f, pan3D = 0.0f;
    float rearAttenDb = 0.0f;
    bool wet = false;             // MASTER_WET bus (environment) vs dry
    int spatial = 0;              // 0 k3D, 1 k2D, 2 kSmartPan, 3 kSmartPan_PreferPlayer
    float panAtten3DDb = 0.0f;    // SmartPanAttenuation3D
    int priority = 128;           // FMOD channel priority (0 most important)
    bool protect = false;         // the local player's own sound (PC ADAPTATION)
    float gL = 1.0f, gR = 1.0f;   // per-block resolved channel gains
    float dDist = 0.0f, dPan = 0.0f, dAtten = 1.0f;   // diagnostics of the last resolve
    bool active = false;
    bool culled = false;          // last resolve: positional and beyond maxDist -> virtual (no channel, not mixed)
    bool loop = false;
    int gen = 0;
};

class Win32Audio final : public IAudio {
public:
    bool init() {
        gMainThread = std::this_thread::get_id();
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
        env_.init();
        ok_ = true;
        // Mixing and waveOut submission run on their own thread: waveOutWrite can block inside the driver
        // (measured 17 ms at device start and ~170 ms when the queue drained during a load), which must not
        // stall the game thread. Game-thread calls only take mx_ briefly.
        run_ = true;
        thread_ = std::thread([this] {
            while (run_) { pump(); Sleep(2); }
        });
        return true;
    }

    // A wave referenced by several cues / events is decoded once.
    bool threadSafeLoad() const override { return true; }
    bool loadIsCostly(const std::string& path) const override {
        if (!bankFirst(path) && fileExists(path)) return false;     // a plain WAV read
        std::lock_guard<std::mutex> lk(mx_);
        return loaded_.find(path) == loaded_.end();
    }   // decode outside the lock; deque storage
    Sound cached(const std::string& path) const override {
        std::lock_guard<std::mutex> lk(mx_);
        auto it = loaded_.find(path);
        return it == loaded_.end() ? kInvalidSound : it->second;
    }
    // Sound handles carry the slot's generation, so a released slot can be reused by the next load (a level reload used to append
    // a new slot per wave every match) while a stale handle to the old sample simply stops resolving.
    static constexpr int kGenShift = 20, kIndexMask = (1 << kGenShift) - 1, kGenMask = 0x7FF;
    Sound handleOf(size_t idx) const { return (Sound)((int)idx | (sounds_[idx].gen << kGenShift)); }
    Sample* sampleOf(Sound s) {
        if (s < 0) return nullptr;
        const size_t idx = (size_t)(s & kIndexMask);
        if (idx >= sounds_.size() || sounds_[idx].gen != ((s >> kGenShift) & kGenMask)) return nullptr;
        return &sounds_[idx];
    }
    Sound load(const std::string& path) override {
        {
            std::lock_guard<std::mutex> lk(mx_);
            auto it = loaded_.find(path);
            if (it != loaded_.end()) return it->second;
        }
        Sample smp;                                    // decode outside the lock
        if (!loadWav(path, smp.pcm, &smp.srcRate)) return kInvalidSound;
        std::lock_guard<std::mutex> lk(mx_);
        auto again = loaded_.find(path);               // another thread (prefetch warming) decoded it meanwhile: one copy
        if (again != loaded_.end()) return again->second;
        size_t idx;
        if (!freeSlots_.empty()) {                     // reuse a released slot (its PCM is already freed), next generation
            idx = freeSlots_.back(); freeSlots_.pop_back();
            const int g = (sounds_[idx].gen + 1) & kGenMask;
            sounds_[idx] = std::move(smp);
            sounds_[idx].gen = g;
        } else {
            if (sounds_.size() > (size_t)kIndexMask) return kInvalidSound;
            sounds_.push_back(std::move(smp));         // deque: existing voices' data pointers stay valid
            idx = sounds_.size() - 1;
        }
        const Sound h = handleOf(idx);
        loaded_[path] = h;
        return h;
    }

    // Fixed voice pool so handles stay valid; handle = index | generation << 12.
    // [CONF] Xe-TransEngine.ini [HM_Engine.FmodAudioDevice] MaxChannels=96. When every channel is busy a new
    // sound takes the channel of the least important playing voice by FMOD channel priority (larger number =
    // less important; cue voices 255 - Priority [CONF RE d50c2a9]); a newcomer less important than every
    // playing voice does not play. [HIGH, FMOD Ex internal] equal priorities: the quietest voice is taken.
    // Virtual voices [HIGH, FMOD Ex FMOD_INIT_VOL0_BECOMES_VIRTUAL]: a positional voice beyond its max distance (culled, zero
    // gain) holds no channel - it is not mixed, its timeline still advances, and it takes a channel again when it comes
    // back in range. PC ADAPTATION: the logical pool grows to kMaxLogical so 32 v 32's far-away loops (map emitters,
    // vehicle engines, projectile flights) do not crowd out audible sounds; the 96 real channels and their stealing rule are
    // unchanged.
    static constexpr int kMaxVoices = 96;
    static constexpr int kMaxLogical = 1024;                  // < 4096 (handle index bits)
    std::vector<int> realScratch_;                            // mixBlock: the audible voices this block
    struct MixJob {                                           // mixBlock: a heard voice's snapshot for the unlocked mix
        int idx = 0, gen = 0;
        const int16_t* pcm = nullptr; size_t frames = 0;
        double lstart = 0.0, lend = 0.0, pos = 0.0, rate = 1.0;
        float gL = 0.0f, gR = 0.0f;
        bool loop = false, positional = false, wet = false, ended = false;
    };
    std::vector<MixJob> jobs_;
    bool mixing_ = false;                                     // a block is being mixed outside mx_ (PCM releases wait)
    std::vector<Sound> releaseLater_;                         // slot indices (not handles)
    std::vector<size_t> freeSlots_;                           // released slots, reused by load()
    std::vector<std::vector<int16_t>> deadPcm_;               // released PCM waiting to be freed outside mx_
    bool envPending_ = false, compPending_ = false;
    Environment envNext_; float envFade_ = 0.0f; float compNext_[4] = {0, 0, 0, 0};
    // PC ADAPTATION (32 v 32): the local player's own sounds are never the victim, and a new one takes the least important
    // other channel even when every other channel outranks it - with dozens of bots firing, the human still hears their own
    // weapon / steps / foley. At <= 16 participants the 96 channels practically never fill, so this does not apply.
    Voice* freeVoice(int& index, int priority, bool startsVirtual = false, bool protect = false) {
        int real = 0, freeSlot = -1;
        for (int i = 0; i < (int)voices_.size(); ++i) {
            const Voice& v = voices_[(size_t)i];
            if (!v.active) { if (freeSlot < 0) freeSlot = i; }
            else if (!v.culled) ++real;
        }
        if (real < kMaxVoices || startsVirtual) {             // a virtual newcomer needs a slot, never a channel
            if (freeSlot >= 0) { index = freeSlot; return &voices_[(size_t)freeSlot]; }
            if ((int)voices_.size() < kMaxLogical) { voices_.push_back(Voice{}); index = (int)voices_.size() - 1; return &voices_.back(); }
        }
        if (startsVirtual) { ++stats_.droppedVoices; return nullptr; }   // kMaxLogical slots in use
        int best = -1;
        for (int i = 0; i < (int)voices_.size(); ++i) {
            const Voice& v = voices_[(size_t)i];
            if (!v.active || v.culled || v.protect) continue;          // free / virtual (no channel) / the player's own
            if (v.priority < priority && !protect) continue;           // more important than the newcomer
            if (best < 0 || v.priority > voices_[(size_t)best].priority ||
                (v.priority == voices_[(size_t)best].priority && v.gL + v.gR < voices_[(size_t)best].gL + voices_[(size_t)best].gR))
                best = i;
        }
        if (best < 0) { ++stats_.droppedVoices; return nullptr; }
        ++stats_.stolenVoices;
        voices_[(size_t)best].active = false;
        index = best;
        return &voices_[(size_t)best];
    }
    Voice* start(Sound s, int& index, int priority = 128, bool startsVirtual = false, bool protect = false) {
        Sample* smp = sampleOf(s);
        if (!ok_ || !smp || smp->pcm.size() < 4 || smp->releasing) return nullptr;   // released / stale / empty
        Voice* v = freeVoice(index, priority, startsVirtual, protect);
        if (!v) return nullptr;
        int gen = (v->gen + 1) & 0x7FFFF;
        *v = Voice{}; v->gen = gen; v->data = &smp->pcm; v->sample = smp; v->active = true;
        return v;
    }

    void play(Sound s, float volume) override {
        std::lock_guard<std::mutex> lk(mx_);
        int i; Voice* v = start(s, i);
        if (v) v->vol = volume;
    }

    void playAt(Sound s, const core::Vec3& pos, float volume, float refDist, float maxDist) override {
        std::lock_guard<std::mutex> lk(mx_);
        int i; Voice* v = start(s, i);
        if (!v) return;
        v->vol = volume; v->positional = true; v->wpos = pos; v->refDist = refDist; v->maxDist = maxDist;
    }

    audio::Voice playVoice(Sound s, const VoiceParams& p) override {
        std::lock_guard<std::mutex> lk(mx_);
        const bool outOfRange = p.positional && std::max(core::length(p.pos - lpos_), p.minDist) > p.maxDist;   // out of range now
        int i; Voice* v = start(s, i, p.priority, outOfRange, p.protect);
        if (!v) return kInvalidVoice;
        v->culled = outOfRange;
        v->vol = p.volume; v->rate = p.pitch > 0.05f ? p.pitch : 0.05f;
        v->positional = p.positional; v->inverse = true; v->wpos = p.pos;
        v->refDist = p.minDist; v->maxDist = p.maxDist; v->rolloff = p.rolloff;
        v->pan2D = p.pan2D; v->pan3D = p.pan3D;
        v->rearAttenDb = p.rearAttenDb; v->wet = p.wet; v->spatial = p.spatial; v->panAtten3DDb = p.panAtten3DDb;
        v->loop = p.loop;
        v->priority = p.priority;
        v->protect = p.protect;
        return i | (v->gen << 12);
    }

    void updateVoice(audio::Voice h, float volume, float pitch, const core::Vec3& pos) override {
        if (h < 0) return;
        std::lock_guard<std::mutex> lk(mx_);
        int i = h & 0xFFF, gen = h >> 12;
        if ((size_t)i >= voices_.size() || voices_[(size_t)i].gen != gen || !voices_[(size_t)i].active) return;
        Voice& v = voices_[(size_t)i];
        v.vol = volume; v.rate = pitch > 0.05f ? pitch : 0.05f; v.wpos = pos;
    }

    void setEnvironment(const Environment& e, float fade) override {
        std::lock_guard<std::mutex> lk(mx_);
        envNext_ = e; envFade_ = fade; envPending_ = true;      // applied by the mixer thread (env_ runs outside the lock)
    }
    void setSmartPanPlayer(const core::Vec3& pos, bool valid) override {
        std::lock_guard<std::mutex> lk(mx_);
        ppos_ = pos; pvalid_ = valid;
    }
    void setMasterCompressor(float t, float a, float r, float m) override {
        std::lock_guard<std::mutex> lk(mx_);
        compNext_[0] = t; compNext_[1] = a; compNext_[2] = r; compNext_[3] = m; compPending_ = true;
    }
    bool mixStats(MixStats& s) const override { std::lock_guard<std::mutex> lk(mx_); s = stats_; return ok_; }

    bool reportsVoices() const override { return ok_; }
    bool voiceInfo(audio::Voice h, VoiceInfo& o) const override {
        if (h < 0) return false;
        std::lock_guard<std::mutex> lk(mx_);
        int i = h & 0xFFF, gen = h >> 12;
        if ((size_t)i >= voices_.size() || voices_[(size_t)i].gen != gen || !voices_[(size_t)i].active) return false;
        const Voice& v = voices_[(size_t)i];
        o.dist = v.dDist; o.pan = v.dPan; o.atten = v.dAtten; o.gainL = v.gL; o.gainR = v.gR;
        return true;
    }
    void release(Sound s) override {
        std::vector<int16_t> dead;                               // freed after the lock is released (declared first): freeing
        std::lock_guard<std::mutex> lk(mx_);                     // tens of MB of music under mx_ stalled the step ~11 ms
        Sample* sp = sampleOf(s);
        if (!sp || sp->releasing) return;                        // stale handle / already released
        Sample& smp = *sp;
        const size_t idx = (size_t)(s & kIndexMask);
        for (Voice& v : voices_) if (v.active && v.sample == &smp) v.active = false;
        for (auto it = loaded_.begin(); it != loaded_.end(); ++it) if (it->second == s) { loaded_.erase(it); break; }
        if (mixing_) { smp.releasing = true; releaseLater_.push_back((Sound)idx); return; }   // the block being mixed may read its PCM
        dead.swap(smp.pcm);                                      // the slot goes to the free list (handles carry the generation)
        smp.loopStart = 0.0; smp.loopEnd = -1.0;
        freeSlots_.push_back(idx);
    }
    void stopAllVoices() override {
        std::lock_guard<std::mutex> lk(mx_);
        for (Voice& v : voices_) v.active = false;
    }
    int activeVoices() const override {
        std::lock_guard<std::mutex> lk(mx_);
        int n = 0;
        for (const Voice& v : voices_) n += v.active ? 1 : 0;
        return n;
    }
    size_t residentBytes() const override {
        std::lock_guard<std::mutex> lk(mx_);
        size_t b = 0;
        for (const Sample& smp : sounds_) if (!smp.releasing) b += smp.pcm.size() * sizeof(int16_t);
        return b;
    }
    bool setLoopPoints(Sound s, uint32_t start, uint32_t end) override {
        std::lock_guard<std::mutex> lk(mx_);
        Sample* sp = sampleOf(s);
        if (!sp || end <= start) return false;
        Sample& smp = *sp;
        double k = (double)kRate / (double)std::max(1, smp.srcRate);    // source frames -> output frames
        smp.loopStart = (double)start * k;
        smp.loopEnd = std::min((double)(smp.pcm.size() / 2), (double)(end + 1) * k);   // end inclusive
        return true;
    }
    bool isPlaying(audio::Voice h) const override {
        if (h < 0) return false;
        std::lock_guard<std::mutex> lk(mx_);
        int i = h & 0xFFF, gen = h >> 12;
        return (size_t)i < voices_.size() && voices_[(size_t)i].gen == gen && voices_[(size_t)i].active;
    }

    void stopVoice(audio::Voice h) override {
        if (h < 0) return;
        std::lock_guard<std::mutex> lk(mx_);
        int i = h & 0xFFF, gen = h >> 12;
        if ((size_t)i < voices_.size() && voices_[(size_t)i].gen == gen) voices_[(size_t)i].active = false;
    }

    void setListener(const core::Vec3& pos, const core::Vec3& fwd, const core::Vec3& right) override {
        std::lock_guard<std::mutex> lk(mx_);
        lpos_ = pos; lfwd_ = fwd; lright_ = right;
    }

    // The audio thread does the work; the game thread's per-frame pump has nothing to do.
    void update() override {}

    // ---- PCM streams (movie audio): mixed after the Master compressor, at the rebuild's output scaling ----
    int openStream(int sampleRate) override {
        if (sampleRate <= 0) return -1;
        std::lock_guard<std::mutex> lk(mx_);
        const int id = ++streamSerial_;
        Stream& s = streams_[id];
        s.step = (double)sampleRate / kRate;
        s.cap = (size_t)sampleRate * 2;               // ~2 s queued at most
        return id;
    }
    size_t pushStream(int id, const float* lr, size_t frames) override {
        std::lock_guard<std::mutex> lk(mx_);
        auto it = streams_.find(id);
        if (it == streams_.end() || !lr) return 0;
        Stream& s = it->second;
        if (s.read > 0 && s.read * 2 >= s.buf.size()) {   // compact the consumed head
            s.buf.erase(s.buf.begin(), s.buf.begin() + (long)(s.read * 2));
            s.read = 0;
        }
        const size_t queued = s.buf.size() / 2 - s.read;
        const size_t n = queued >= s.cap ? 0 : std::min(frames, s.cap - queued);
        s.buf.insert(s.buf.end(), lr, lr + n * 2);
        return n;
    }
    size_t streamQueued(int id) const override {
        std::lock_guard<std::mutex> lk(mx_);
        auto it = streams_.find(id);
        return it == streams_.end() ? 0 : it->second.buf.size() / 2 - it->second.read;
    }
    uint64_t streamPlayed(int id) const override {
        std::lock_guard<std::mutex> lk(mx_);
        auto it = streams_.find(id);
        return it == streams_.end() ? 0 : it->second.played;
    }
    void setStreamPaused(int id, bool paused) override {
        std::lock_guard<std::mutex> lk(mx_);
        auto it = streams_.find(id);
        if (it != streams_.end()) it->second.paused = paused;
    }
    void closeStream(int id) override {
        std::lock_guard<std::mutex> lk(mx_);
        streams_.erase(id);
    }
    int openStreams() const override {
        std::lock_guard<std::mutex> lk(mx_);
        return (int)streams_.size();
    }

    ~Win32Audio() override {
        const platform::FsbStats& st = platform::fsbStats();
        if (st.decodes > 0)
            LOG_INFO("audio: %lld banks decoded (%.0f MB PCM, %.0f ms), %lld of them on the main thread (%.0f ms)", st.decodes.load(),
                     st.pcmBytes.load() / 1048576.0, st.microseconds.load() / 1000.0, st.mainThreadDecodes.load(),
                     st.mainThreadMicroseconds.load() / 1000.0);
        run_ = false;
        if (thread_.joinable()) thread_.join();
        if (ok_) {
            waveOutReset(wo_);
            for (int b = 0; b < kNumBlocks; ++b) waveOutUnprepareHeader(wo_, &hdr_[b], sizeof(WAVEHDR));
            waveOutClose(wo_);
        }
    }

private:
    // Audio thread: mix every free block (under mx_) and queue it (outside mx_; waveOutWrite may block).
    void pump() {
        LARGE_INTEGER t0, t1, fq;
        QueryPerformanceCounter(&t0);
        int mixed = 0;
        long long mixTicks = 0;
        for (int b = 0; b < kNumBlocks; ++b) {
            if (!(hdr_[b].dwFlags & WHDR_DONE)) continue;
            ++mixed;
            {
                LARGE_INTEGER m0, m1; QueryPerformanceCounter(&m0);
                mixBlock(blocks_[b]);                             // locks only to snapshot / write back (see mixBlock)
                QueryPerformanceCounter(&m1); mixTicks += m1.QuadPart - m0.QuadPart;
            }
            hdr_[b].dwFlags &= ~WHDR_DONE;
            hdr_[b].dwBufferLength = kBlockSamples * sizeof(int16_t);
            waveOutWrite(wo_, &hdr_[b], sizeof(WAVEHDR));
        }
        if (!mixed) return;
        QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&fq);
        std::lock_guard<std::mutex> lk(mx_);
        stats_.lastUpdateMs = (float)(1000.0 * (double)(t1.QuadPart - t0.QuadPart) / (double)fq.QuadPart);
        stats_.lastUpdateBlocks = mixed;
        stats_.lastMixMs = (float)(1000.0 * (double)mixTicks / (double)fq.QuadPart);
        stats_.maxUpdateBlocks = std::max(stats_.maxUpdateBlocks, mixed);
    }

    // FmodAudioDevice::UpdatePreferPlayerLocation [CONF native 0x8275F658]: once per update, the reference
    // ramps linearly between the local pawn origin (camera within MaxPlayerSmartPanRadius 1400 UU) and the
    // listener (camera farther), over SmartPanPreferPlayerTransitionTime 0.5 s (Xe-TransEngine.ini).
    void updatePreferPlayer(float dt) {
        prefLoc_ = lpos_;
        if (!pvalid_) return;
        constexpr float kRadius = 14.0f, kTransition = 0.5f;
        float target = core::length(lpos_ - ppos_) >= kRadius ? 1.0f : 0.0f;
        if (target != rampTarget_) {                          // Ramp.SetTarget(t, T)
            rampTarget_ = target;
            rampRate_ = (target - rampValue_) / kTransition; rampRemaining_ = kTransition; rampActive_ = true;
        }
        if (rampActive_) {                                    // Ramp.Update(dt)
            rampRemaining_ -= dt;
            rampValue_ += rampRate_ * dt;
            bool over = (rampRate_ > 0.0f && rampValue_ >= rampTarget_) || (rampRate_ < 0.0f && rampValue_ <= rampTarget_);
            if (rampRemaining_ <= 0.0f || over) { rampValue_ = rampTarget_; rampActive_ = false; }
        }
        prefLoc_ = ppos_ + (lpos_ - ppos_) * rampValue_;
    }

    // FmodAudioDevice::ComputeSourceSpatialization [CONF native 0x82759B08]. The source stays at its own
    // position (the AudioComponent / socket); volume rolloff, cull and rear attenuation use listener ->
    // source; only the SmartPan 2D <-> 3D mix of kSmartPan_PreferPlayer measures from the PreferPlayer
    // reference. Stereo direction: equal-power pan of the source direction against the listener's right
    // axis, scaled by the 3D amount (0 = centred / 2D, 1 = fully 3D).
    void resolveGains(Voice& v) {
        float g = v.vol * master_;
        v.culled = false;
        if (!v.positional) { v.gL = v.gR = g; v.dPan = 0.0f; v.dAtten = 1.0f; return; }
        core::Vec3 d = v.wpos - lpos_;
        float dist = core::length(d);
        float atten;
        if (v.inverse) {
            float dc = std::max(dist, v.refDist);
            if (dc > v.maxDist) { v.gL = v.gR = 0.0f; v.dDist = dist; v.dAtten = 0.0f; v.dPan = 0.0f; v.culled = true; return; }   // culled
            atten = v.refDist / ((dc - v.refDist) * v.rolloff + v.refDist);
        } else {
            atten = dist <= v.refDist ? 1.0f
                  : (dist >= v.maxDist ? 0.0f : (v.maxDist - dist) / (v.maxDist - v.refDist));
        }
        core::Vec3 dir = dist > 1e-4f ? d * (1.0f / dist) : lfwd_;
        float f = core::dot(lfwd_, dir);
        if (f < 0.0f) atten *= 1.0f - (1.0f - dbToLinear(v.rearAttenDb)) * (-f);     // behind the listener
        float amount = 1.0f;                                  // k3D: fully 3D
        if (v.spatial == 2 || v.spatial == 3) {               // kSmartPan / kSmartPan_PreferPlayer
            float ds = v.spatial == 3 ? core::length(prefLoc_ - v.wpos) : dist;
            float d2 = v.pan2D, d3 = v.pan3D;
            if (d2 == d3) amount = ds < d2 ? 0.0f : 1.0f;
            else {
                float lo = std::min(d2, d3), hi = std::max(d2, d3);
                float t = core::clampf((core::clampf(ds, lo, hi) - lo) / (hi - lo), 0.0f, 1.0f);
                amount = d2 < d3 ? t : 1.0f - t;
            }
            atten *= 1.0f - (1.0f - dbToLinear(v.panAtten3DDb)) * amount;      // SmartPanGain
        }
        g *= atten;
        float pan = core::clampf(core::dot(dir, lright_), -1.0f, 1.0f) * amount;
        v.dDist = dist; v.dPan = pan; v.dAtten = atten;
        float ang = (pan + 1.0f) * 0.25f * core::PI;          // equal-power pan: -1 => L, +1 => R
        v.gL = std::cos(ang) * g;
        v.gR = std::sin(ang) * g;
    }

    // Voices -> MASTER_DRY / MASTER_WET buses (float, full scale 1.0); MASTER_WET runs the zone
    // environment; Master applies the level and the compressor, then clips to 16 bit.
    // Three phases so game threads never wait for the per-sample work (the step's playVoice / updateVoice used to block for
    // a whole block, 0.2-0.6 ms at 32 v 32): (A) under mx_: environment / compressor requests, gains, the 96-voice cap,
    // virtual voices, a snapshot of every heard voice; (B) unlocked: the per-sample mix, environment, compressor (env_ / comp_
    // are only touched by this thread); (C) under mx_: positions / ends written back (a voice stopped or restarted meanwhile -
    // gen changed - is left alone), movie streams, stats, deferred sample releases. Same output as one locked pass.
    void mixBlock(std::vector<int16_t>& dst) {
        LARGE_INTEGER t0, t1, fq;
        QueryPerformanceCounter(&t0);
        std::unique_lock<std::mutex> lk(mx_);
        if (envPending_) { env_.set(envNext_, envFade_); envPending_ = false; }
        if (compPending_) { comp_.set(compNext_[0], compNext_[1], compNext_[2], compNext_[3]); compPending_ = false; }
        updatePreferPlayer((float)kBlockFrames / kRate);
        dry_.assign(kBlockSamples, 0.0f);
        wet_.assign(kBlockSamples, 0.0f);
        int nv = 0, nw = 0;
        constexpr float k = 1.0f / 32768.0f;
        int nvirt = 0;
        // FMOD virtual voices: at most kMaxVoices are heard. Normally freeVoice keeps it so, but a virtual voice coming back into
        // range takes no channel; when more than kMaxVoices are audible, the least important (the player's own last, then
        // priority, then the quietest) are virtual for this block.
        realScratch_.clear();
        for (int i = 0; i < (int)voices_.size(); ++i) {
            Voice& v = voices_[(size_t)i];
            if (!v.active) continue;
            resolveGains(v);
            if (!v.culled) realScratch_.push_back(i);
        }
        if ((int)realScratch_.size() > kMaxVoices) {
            auto more = [&](int a, int b) {                       // a more important than b
                const Voice& x = voices_[(size_t)a]; const Voice& y = voices_[(size_t)b];
                if (x.protect != y.protect) return x.protect;
                if (x.priority != y.priority) return x.priority < y.priority;
                return x.gL + x.gR > y.gL + y.gR;
            };
            std::nth_element(realScratch_.begin(), realScratch_.begin() + kMaxVoices, realScratch_.end(), more);
            for (size_t k = kMaxVoices; k < realScratch_.size(); ++k) voices_[(size_t)realScratch_[k]].culled = true;
            stats_.overflowVirtualized += (int)realScratch_.size() - kMaxVoices;
        }
        jobs_.clear();
        for (int vi = 0; vi < (int)voices_.size(); ++vi) {
            Voice& v = voices_[(size_t)vi];
            if (!v.active) continue;
            if (v.culled) {                                   // virtual: advance the timeline only
                ++nvirt;
                const double frames = (double)(v.data->size() / 2);
                const double lstart = v.sample->loopStart;
                const double lend = v.sample->loopEnd > 0.0 ? v.sample->loopEnd : frames;
                v.pos += v.rate * kBlockFrames;
                if (v.loop && lend - lstart >= 2.0) { if (v.pos >= lend) v.pos = lstart + std::fmod(v.pos - lend, lend - lstart); }
                else if (v.pos + 1.0 >= frames) v.active = false;
                continue;
            }
            ++nv; if (v.wet) ++nw;
            MixJob jb;
            jb.idx = vi; jb.gen = v.gen; jb.pcm = v.data->data(); jb.frames = v.data->size() / 2;
            jb.lstart = v.sample->loopStart; jb.lend = v.sample->loopEnd > 0.0 ? v.sample->loopEnd : (double)jb.frames;
            jb.loop = v.loop; jb.pos = v.pos; jb.rate = v.rate; jb.gL = v.gL; jb.gR = v.gR; jb.positional = v.positional; jb.wet = v.wet;
            jobs_.push_back(jb);
        }
        mixing_ = true;
        lk.unlock();
        // (B) no lock: only this thread touches jobs_, dry_ / wet_, env_ and comp_; the PCM stays alive (releases wait for C).
        for (MixJob& jb : jobs_) {
            float* bus = jb.wet ? wet_.data() : dry_.data();
            const int16_t* s = jb.pcm;
            const bool loops = jb.loop && jb.lend - jb.lstart >= 2.0;
            for (int f = 0; f < kBlockFrames; ++f) {
                // Loop region [loopStart, loopEnd): the FSB sample-header region (whole sample for every
                // slice wave); the last frame interpolates into loopStart, so a period is exactly the region.
                // Loops only when the voice was started with loop (wave event bLooping), indefinitely.
                if (loops && jb.pos >= jb.lend) jb.pos = jb.lstart + std::fmod(jb.pos - jb.lend, jb.lend - jb.lstart);
                size_t fi = (size_t)jb.pos;
                size_t fn = fi + 1;
                if (loops) { if ((double)fn >= jb.lend) fn = (size_t)jb.lstart; }
                else if (fn >= jb.frames) { jb.ended = true; break; }
                float u = (float)(jb.pos - (double)fi);      // linear interpolation for pitch
                float sl = (s[fi * 2] + (s[fn * 2] - s[fi * 2]) * u) * k;
                float sr = (s[fi * 2 + 1] + (s[fn * 2 + 1] - s[fi * 2 + 1]) * u) * k;
                jb.pos += jb.rate;
                if (jb.positional) {
                    float mono = (sl + sr) * 0.5f;
                    bus[f * 2]     += mono * jb.gL;
                    bus[f * 2 + 1] += mono * jb.gR;
                } else {
                    bus[f * 2]     += sl * jb.gL;
                    bus[f * 2 + 1] += sr * jb.gR;
                }
            }
        }
        env_.process(wet_.data(), kBlockFrames);
        for (int i = 0; i < kBlockSamples; ++i) dry_[i] += wet_[i];
        float peak = 0.0f, minGain = 1.0f;
        comp_.process(dry_.data(), kBlockFrames, peak, minGain);
        lk.lock();                                            // (C)
        mixing_ = false;
        for (const MixJob& jb : jobs_) {
            Voice& v = voices_[(size_t)jb.idx];
            if (!v.active || v.gen != jb.gen) continue;       // stopped / restarted while mixing: its new state wins
            v.pos = jb.pos;
            if (jb.ended) v.active = false;
        }
        for (Sound r : releaseLater_)
            if (r >= 0 && (size_t)r < sounds_.size() && sounds_[(size_t)r].releasing) {
                Sample& smp = sounds_[(size_t)r];
                deadPcm_.emplace_back(); deadPcm_.back().swap(smp.pcm);   // freed below, after the unlock
                smp.loopStart = 0.0; smp.loopEnd = -1.0; smp.releasing = false;
                freeSlots_.push_back((size_t)r);                           // releaseLater_ holds slot indices
            }
        releaseLater_.clear();
        stats_.peakDb = peak > 1e-6f ? 20.0f * std::log10(peak) : -96.0f;
        stats_.gainReductionDb = std::min(stats_.gainReductionDb * 0.9f, 20.0f * std::log10(minGain));
        stats_.voices = nv; stats_.wetVoices = nw; stats_.peakVoices = std::max(stats_.peakVoices, nv);
        stats_.virtualVoices = nvirt;
        // Movie streams: their own output, after the game mix's Master chain [HIGH: the Bink player outputs
        // beside FMOD; MovieMixerPreset mutes the game mix, not the movie]. Level: full scale maps to the
        // rebuild's Master Default calibration (master_ stands for Master 0.708) [PROV].
        const float sg = master_ / 0.7079458f;
        float speak = 0.0f;
        for (auto& kv : streams_) {
            Stream& s = kv.second;
            if (s.paused) continue;
            const size_t avail = s.buf.size() / 2;
            for (int f = 0; f < kBlockFrames; ++f) {
                const size_t i0 = s.read;
                if (i0 + 1 >= avail) { if (s.started) stats_.streamUnderrunFrames += kBlockFrames - f; break; }   // underrun
                s.started = true;
                speak = std::max(speak, std::max(std::fabs(s.buf[i0 * 2]), std::fabs(s.buf[i0 * 2 + 1])) * sg);
                const float u = (float)s.frac;
                dry_[f * 2]     += (s.buf[i0 * 2] + (s.buf[i0 * 2 + 2] - s.buf[i0 * 2]) * u) * sg;
                dry_[f * 2 + 1] += (s.buf[i0 * 2 + 1] + (s.buf[i0 * 2 + 3] - s.buf[i0 * 2 + 1]) * u) * sg;
                s.frac += s.step;
                const size_t adv = (size_t)s.frac;
                s.frac -= (double)adv;
                s.read += adv;
                s.played += adv;
            }
        }
        stats_.streamPeakDb = speak > 1e-6f ? 20.0f * std::log10(speak) : -96.0f;
        stats_.streams = (int)streams_.size();
        for (int i = 0; i < kBlockSamples; ++i) {
            float x = dry_[i] * 32768.0f;
            dst[i] = (int16_t)(x > 32767.0f ? 32767.0f : (x < -32768.0f ? -32768.0f : x));
        }
        QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&fq);
        float ms = (float)(1000.0 * (double)(t1.QuadPart - t0.QuadPart) / (double)fq.QuadPart);
        stats_.mixMsPerBlock += (ms - stats_.mixMsPerBlock) * 0.1f;
        if (!deadPcm_.empty()) { std::vector<std::vector<int16_t>> dead; dead.swap(deadPcm_); lk.unlock(); }   // freed unlocked
    }

    HWAVEOUT wo_ = nullptr;
    bool ok_ = false;
    float master_ = 0.5f;                 // [PROV] overall SFX level (was far too loud at 1.0)
    EnvDsp env_;
    core::Vec3 ppos_{0, 0, 0};
    bool pvalid_ = false;
    core::Vec3 prefLoc_{0, 0, 0};      // FmodAudioDevice PreferPlayerLocation (+0x1A0)
    // Linear ramp (device+0x188, SetTarget 0x827560A8 / Update 0x827560F8): 0 = pawn origin, 1 = listener.
    float rampValue_ = 0.0f, rampTarget_ = 0.0f, rampRate_ = 0.0f, rampRemaining_ = 0.0f;
    bool rampActive_ = false;
    Compressor comp_;
    MixStats stats_;
    std::vector<float> dry_, wet_;
    std::map<std::string, Sound> loaded_;
    core::Vec3 lpos_{0, 0, 0}, lfwd_{0, 0, -1}, lright_{1, 0, 0};
    std::deque<Sample> sounds_;
    mutable std::mutex mx_;
    std::thread thread_;
    std::atomic<bool> run_{false};
    std::vector<Voice> voices_;
    std::vector<int16_t> blocks_[kNumBlocks];
    WAVEHDR hdr_[kNumBlocks];
    struct Stream {
        std::vector<float> buf;        // interleaved stereo, [read*2 ..) not yet played
        size_t read = 0, cap = 0;
        double step = 1.0, frac = 0.0;
        uint64_t played = 0;
        bool paused = false, started = false;   // started: first frame played (later empty blocks = underrun)
    };
    std::map<int, Stream> streams_;
    int streamSerial_ = 0;
};

// Silent fallback when there is no audio device.
class NullAudio final : public IAudio {
public:
    Sound load(const std::string&) override { return kInvalidSound; }
    void play(Sound, float) override {}
    void playAt(Sound, const core::Vec3&, float, float, float) override {}
    audio::Voice playVoice(Sound, const VoiceParams&) override { return kInvalidVoice; }
    void stopVoice(audio::Voice) override {}
    void updateVoice(audio::Voice, float, float, const core::Vec3&) override {}
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
