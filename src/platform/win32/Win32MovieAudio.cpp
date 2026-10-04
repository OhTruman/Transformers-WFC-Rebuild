// Clean-room reconstruction — movie audio decode (Systems): Media Foundation Source Reader over the movie file's audio
// streams (one mono FLAC stream per Bink track), PCM float, folded to stereo (audio/MovieAudio.h) and pushed into an
// IAudio stream. Decoding runs on its own thread and keeps ~0.5 s queued, so the sound does not depend on the game
// thread (a blocked frame or a long load does not starve it; the backend mixes on its own thread too).
// Media Foundation is loaded at run time (mfplat / mfreadwrite) with locally defined GUIDs: no link dependency.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include "audio/Audio.h"
#include "audio/MovieAudio.h"
#include "core/Log.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace audio {
namespace {

// GUID values from the Windows SDK (mfapi.h / mfidl.h), defined locally.
const GUID kMT_MAJOR_TYPE = {0x48eba18e, 0xf8c9, 0x4687, {0xbf, 0x11, 0x0a, 0x74, 0xc9, 0xf9, 0x6a, 0x8f}};
const GUID kMT_SUBTYPE = {0xf7e34c9a, 0x42e8, 0x4714, {0xb7, 0x4b, 0xcb, 0x29, 0xd7, 0x2c, 0x35, 0xe5}};
const GUID kMediaType_Audio = {0x73647561, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID kAudioFormat_Float = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
const GUID kMT_AUDIO_NUM_CHANNELS = {0x37e48bf5, 0x645e, 0x4c5b, {0x89, 0xde, 0xad, 0xa9, 0xe2, 0x9b, 0x69, 0x6a}};
const GUID kMT_AUDIO_SAMPLES_PER_SECOND = {0x5faeeae7, 0x0290, 0x4c31, {0x9e, 0x8a, 0xc5, 0x34, 0xf6, 0x8d, 0x9d, 0xba}};
const GUID kPD_DURATION = {0x6c990d33, 0xbb8e, 0x477a, {0x85, 0x98, 0x0d, 0x5d, 0x96, 0xfc, 0xd8, 0x8a}};

using FnStartup = HRESULT(WINAPI*)(ULONG, DWORD);
using FnCreateMediaType = HRESULT(WINAPI*)(IMFMediaType**);
using FnCreateReader = HRESULT(WINAPI*)(LPCWSTR, IMFAttributes*, IMFSourceReader**);
struct MF {
    bool ok = false;
    FnCreateMediaType createMediaType = nullptr;
    FnCreateReader createReader = nullptr;
};
const MF& mf() {
    static MF m;
    static std::once_flag once;
    std::call_once(once, [] {
        HMODULE plat = LoadLibraryW(L"mfplat.dll"), rd = LoadLibraryW(L"mfreadwrite.dll");
        if (!plat || !rd) { LOG_WARN("movie audio: Media Foundation not available"); return; }
        auto startup = (FnStartup)(void*)GetProcAddress(plat, "MFStartup");
        m.createMediaType = (FnCreateMediaType)(void*)GetProcAddress(plat, "MFCreateMediaType");
        m.createReader = (FnCreateReader)(void*)GetProcAddress(rd, "MFCreateSourceReaderFromURL");
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m.ok = startup && m.createMediaType && m.createReader && SUCCEEDED(startup(MF_VERSION, 0 /*MFSTARTUP_FULL*/));
        if (!m.ok) LOG_WARN("movie audio: MFStartup failed");
    });
    return m;
}

std::wstring widen(const std::string& s) {
    std::wstring w(s.size() + 1, L'\0');
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], (int)w.size());
    w.resize(n > 0 ? (size_t)n - 1 : 0);
    return w;
}

class MFMovieAudio final : public MovieAudioPlayer {
public:
    ~MFMovieAudio() override { stop(); }

    bool open(IAudio* a, const std::string& path, int languageSlot) override {
        stop();
        const MF& m = mf();
        if (!a || !m.ok) return false;
        IMFSourceReader* rd = nullptr;
        if (FAILED(m.createReader(widen(path).c_str(), nullptr, &rd)) || !rd) {
            LOG_WARN("movie audio: cannot open %s", path.c_str());
            return false;
        }
        // Every audio stream, in file order (= Bink track order), decoded to 32-bit float.
        rd->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        streams_.clear();
        int rate = 0;
        for (DWORD i = 0;; ++i) {
            IMFMediaType* nt = nullptr;
            if (FAILED(rd->GetNativeMediaType(i, 0, &nt))) break;
            GUID major{};
            nt->GetGUID(kMT_MAJOR_TYPE, &major);
            nt->Release();
            if (major != kMediaType_Audio) continue;
            IMFMediaType* out = nullptr;
            m.createMediaType(&out);
            out->SetGUID(kMT_MAJOR_TYPE, kMediaType_Audio);
            out->SetGUID(kMT_SUBTYPE, kAudioFormat_Float);
            HRESULT hr = rd->SetCurrentMediaType(i, nullptr, out);
            out->Release();
            if (FAILED(hr)) { LOG_WARN("movie audio: stream %lu has no float decoder (%s)", (unsigned long)i, path.c_str()); continue; }
            IMFMediaType* cur = nullptr;
            UINT32 ch = 1, sr = 48000;
            if (SUCCEEDED(rd->GetCurrentMediaType(i, &cur))) {
                cur->GetUINT32(kMT_AUDIO_NUM_CHANNELS, &ch);
                cur->GetUINT32(kMT_AUDIO_SAMPLES_PER_SECOND, &sr);
                cur->Release();
            }
            if (rate == 0) rate = (int)sr;
            rd->SetStreamSelection(i, TRUE);
            streams_.push_back({i, (int)ch, {}, false});
        }
        if (streams_.empty()) { rd->Release(); return false; }
        PROPVARIANT var;
        PropVariantInit(&var);
        if (SUCCEEDED(rd->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, kPD_DURATION, &var)))
            duration_ = var.uhVal.QuadPart / 1e7;
        PropVariantClear(&var);
        reader_ = rd;
        audio_ = a;
        rate_ = rate > 0 ? rate : 48000;
        layout_ = movieTrackLayout((int)streams_.size(), languageSlot);
        stream_ = a->openStream(rate_);
        a->setStreamPaused(stream_, true);            // silent until start()
        run_ = true;
        eos_ = false;
        thread_ = std::thread([this] { decodeLoop(); });
        LOG_INFO("movie audio: %s: %zu tracks @ %d Hz, %.1f s (centre track %d)", path.c_str(), streams_.size(), rate_,
                 duration_, layout_.c);
        return true;
    }

    void start() override { if (audio_ && stream_ > 0) audio_->setStreamPaused(stream_, false); }
    void setPaused(bool p) override { if (audio_ && stream_ > 0) audio_->setStreamPaused(stream_, p); }

    void stop() override {
        run_ = false;
        if (thread_.joinable()) thread_.join();
        if (audio_ && stream_ > 0) audio_->closeStream(stream_);
        stream_ = -1;
        if (reader_) { reader_->Release(); reader_ = nullptr; }
        streams_.clear();
        audio_ = nullptr;
    }

    int tracks() const override { return (int)streams_.size(); }
    double clock() const override { return audio_ && stream_ > 0 ? (double)audio_->streamPlayed(stream_) / rate_ : 0.0; }
    double duration() const override { return duration_; }
    bool finished() const override { return eos_ && (!audio_ || stream_ <= 0 || audio_->streamQueued(stream_) < 2); }

private:
    struct Track { DWORD index; int channels; std::vector<float> pcm; bool ended; };

    // Decode ahead, fold every frame all tracks have, push into the stream; stay ~0.5 s ahead of the mixer.
    void decodeLoop() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::vector<float> lr;
        std::vector<const float*> planes(streams_.size());
        const size_t ahead = (size_t)rate_ / 2;
        while (run_) {
            if (audio_->streamQueued(stream_) >= ahead) { Sleep(5); continue; }
            bool all = true;
            for (Track& t : streams_) all = all && t.ended;
            if (!all) {
                DWORD idx = 0, flags = 0;
                LONGLONG ts = 0;
                IMFSample* smp = nullptr;
                HRESULT hr = reader_->ReadSample((DWORD)MF_SOURCE_READER_ANY_STREAM, 0, &idx, &flags, &ts, &smp);
                Track* tr = nullptr;
                for (Track& t : streams_) if (t.index == idx) tr = &t;
                if (FAILED(hr)) { for (Track& t : streams_) t.ended = true; }
                else if (tr) {
                    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) tr->ended = true;
                    if (smp) append(*tr, smp);
                }
                if (smp) smp->Release();
            }
            // Fold what every track has (an ended track pads with silence).
            size_t n = SIZE_MAX;
            bool any = false;
            for (Track& t : streams_) {
                if (!t.ended || !t.pcm.empty()) n = std::min(n, t.ended ? (size_t)SIZE_MAX : t.pcm.size());
                any = any || !t.pcm.empty();
            }
            if (n == SIZE_MAX) {                                   // every track ended: drain the rest
                n = 0;
                for (Track& t : streams_) n = std::max(n, t.pcm.size());
            }
            if (n > 0) {
                for (Track& t : streams_) if (t.pcm.size() < n) t.pcm.resize(n, 0.0f);   // ended tracks: silence
                for (size_t i = 0; i < streams_.size(); ++i) planes[i] = streams_[i].pcm.data();
                lr.resize(n * 2);
                movieDownmix(layout_, planes.data(), n, lr.data());
                size_t done = 0;
                while (run_ && done < n) {
                    const size_t k = audio_->pushStream(stream_, lr.data() + done * 2, n - done);
                    done += k;
                    if (done < n) Sleep(5);
                }
                for (Track& t : streams_) t.pcm.erase(t.pcm.begin(), t.pcm.begin() + (long)std::min(n, t.pcm.size()));
            } else if (!any) {
                bool ended = true;
                for (Track& t : streams_) ended = ended && t.ended;
                if (ended) { eos_ = true; Sleep(10); }
            }
        }
        CoUninitialize();
    }

    static void append(Track& t, IMFSample* s) {
        IMFMediaBuffer* b = nullptr;
        if (FAILED(s->ConvertToContiguousBuffer(&b)) || !b) return;
        BYTE* p = nullptr;
        DWORD len = 0;
        if (SUCCEEDED(b->Lock(&p, nullptr, &len))) {
            const float* f = (const float*)p;
            const size_t n = len / sizeof(float) / (size_t)std::max(1, t.channels);
            for (size_t i = 0; i < n; ++i) t.pcm.push_back(f[i * (size_t)t.channels]);   // first channel (mono)
            b->Unlock();
        }
        b->Release();
    }

    IMFSourceReader* reader_ = nullptr;
    IAudio* audio_ = nullptr;
    std::vector<Track> streams_;
    MovieTrackLayout layout_;
    int rate_ = 48000, stream_ = -1;
    double duration_ = 0.0;
    std::atomic<bool> run_{false}, eos_{false};
    std::thread thread_;
};

} // namespace

MovieAudioPlayer* createMovieAudioPlayer() { return new MFMovieAudio(); }

} // namespace audio
#endif
