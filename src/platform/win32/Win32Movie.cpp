// Media Foundation movie decoding (Source Reader): H.264 video to RGB32.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include "platform/Movie.h"
#include "core/Log.h"

#include <cstring>
#include <mutex>
#include <vector>

namespace platform {

namespace {

bool g_mfStarted = false;

std::wstring widen(const std::string& s) {
    std::wstring w(s.size() + 1, L'\0');
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], (int)w.size());
    w.resize(n > 0 ? (size_t)n - 1 : 0);
    return w;
}

template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

class MFMoviePlayer final : public IMoviePlayer {
public:
    ~MFMoviePlayer() override { release(pending_); release(reader_); }

    bool open(const std::string& path) override {
        // COM per thread (movie audio is also decoded on a worker thread); Media Foundation once per process.
        thread_local bool comReady = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        (void)comReady;
        static std::once_flag once;
        std::call_once(once, [] { g_mfStarted = SUCCEEDED(MFStartup(MF_VERSION)); });
        if (!g_mfStarted) return false;
        path_ = path;
        return openVideo();
    }

    void advance(double dt) override {
        if (!reader_ || done_) return;
        clock_ += dt;
        // Show the latest frame that is due; one decoded frame is held back until its time comes. Late frames are
        // skipped without conversion (at most 8 per call).
        IMFSample* due = nullptr;
        for (int guard = 0; guard < 8; ++guard) {
            if (!pending_) {
                DWORD idx = 0, flags = 0;
                LONGLONG ts = 0;
                HRESULT hr = reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &idx, &flags, &ts, &pending_);
                if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) { release(pending_); eos_ = true; break; }
                if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) readFormat();
                if (!pending_) continue;
                pendingTs_ = ts / 1e7;
            }
            if (pendingTs_ > clock_) break;
            release(due);
            due = pending_;
            pending_ = nullptr;
        }
        if (due) { copyFrame(due); due->Release(); }
        // Finished once the stream has ended and the last frame has had its display time.
        if (eos_ && !pending_ && (clock_ >= duration_ || duration_ <= 0)) done_ = true;
    }

    bool finished() const override { return done_; }
    double duration() const override { return duration_; }
    double position() const override { return clock_; }

    void restart() override {
        if (!reader_) return;
        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = 0;
        reader_->SetCurrentPosition(GUID_NULL, var);
        PropVariantClear(&var);
        release(pending_);
        clock_ = 0;
        done_ = eos_ = false;
    }

    bool frame(const uint8_t*& rgba, int& w, int& h, uint64_t& serial) const override {
        if (rgba_.empty()) return false;
        rgba = rgba_.data(); w = w_; h = h_; serial = serial_;
        return true;
    }

    bool decodeAudio(std::vector<std::vector<int16_t>>& tracks, int& rate) override {
        // A second reader over the audio streams only (the video reader keeps its position). Every track is
        // converted to 16-bit mono PCM by the Source Reader (FLAC decoder) and read to the end.
        IMFSourceReader* r = nullptr;
        if (FAILED(MFCreateSourceReaderFromURL(widen(path_).c_str(), nullptr, &r))) return false;
        std::vector<DWORD> streams;
        for (DWORD i = 0;; ++i) {
            IMFMediaType* t = nullptr;
            if (FAILED(r->GetNativeMediaType(i, 0, &t))) break;
            GUID major{};
            t->GetGUID(MF_MT_MAJOR_TYPE, &major);
            t->Release();
            r->SetStreamSelection(i, major == MFMediaType_Audio);
            if (major != MFMediaType_Audio) continue;
            IMFMediaType* pcm = nullptr;
            MFCreateMediaType(&pcm);
            pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1);
            if (SUCCEEDED(r->SetCurrentMediaType(i, nullptr, pcm))) streams.push_back(i);
            pcm->Release();
        }
        if (streams.empty()) { r->Release(); return false; }
        rate = 48000;
        IMFMediaType* cur = nullptr;
        if (SUCCEEDED(r->GetCurrentMediaType(streams[0], &cur))) {
            UINT32 sr = 0;
            if (SUCCEEDED(cur->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr)) && sr) rate = (int)sr;
            cur->Release();
        }
        tracks.assign(streams.size(), {});
        std::vector<bool> eos(streams.size(), false);
        size_t ended = 0;
        while (ended < streams.size()) {
            DWORD idx = 0, flags = 0;
            LONGLONG ts = 0;
            IMFSample* s = nullptr;
            if (FAILED(r->ReadSample((DWORD)MF_SOURCE_READER_ANY_STREAM, 0, &idx, &flags, &ts, &s))) break;
            size_t k = 0;
            while (k < streams.size() && streams[k] != idx) ++k;
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                if (k < eos.size() && !eos[k]) { eos[k] = true; ++ended; }
                release(s);
                continue;
            }
            if (!s || k >= tracks.size()) { release(s); continue; }
            IMFMediaBuffer* b = nullptr;
            if (SUCCEEDED(s->ConvertToContiguousBuffer(&b))) {
                BYTE* p = nullptr;
                DWORD len = 0;
                if (SUCCEEDED(b->Lock(&p, nullptr, &len))) {
                    const int16_t* q = reinterpret_cast<const int16_t*>(p);
                    tracks[k].insert(tracks[k].end(), q, q + len / 2);
                    b->Unlock();
                }
                b->Release();
            }
            s->Release();
        }
        r->Release();
        return true;
    }

private:
    bool openVideo() {
        IMFAttributes* attr = nullptr;
        MFCreateAttributes(&attr, 1);
        attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        HRESULT hr = MFCreateSourceReaderFromURL(widen(path_).c_str(), attr, &reader_);
        attr->Release();
        if (FAILED(hr)) { LOG_WARN("movie: cannot open %s (0x%08lx)", path_.c_str(), (unsigned long)hr); return false; }
        reader_->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        reader_->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        IMFMediaType* out = nullptr;
        MFCreateMediaType(&out);
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        hr = reader_->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, out);
        out->Release();
        if (FAILED(hr)) { LOG_WARN("movie: no RGB32 output for %s", path_.c_str()); release(reader_); return false; }
        readFormat();
        PROPVARIANT var;
        PropVariantInit(&var);
        if (SUCCEEDED(reader_->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var)))
            duration_ = var.uhVal.QuadPart / 1e7;
        PropVariantClear(&var);
        return true;
    }

    void readFormat() {
        IMFMediaType* t = nullptr;
        if (FAILED(reader_->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &t))) return;
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &w, &h);
        UINT32 stride = 0;
        if (FAILED(t->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) stride = w * 4;
        stride_ = (int32_t)stride;
        w_ = (int)w; h_ = (int)h;
        t->Release();
    }

    void copyFrame(IMFSample* s) {
        IMFMediaBuffer* b = nullptr;
        if (FAILED(s->ConvertToContiguousBuffer(&b))) return;
        BYTE* p = nullptr; DWORD len = 0;
        if (SUCCEEDED(b->Lock(&p, nullptr, &len)) && w_ > 0 && h_ > 0) {
            rgba_.resize((size_t)w_ * h_ * 4);
            int absStride = stride_ < 0 ? -stride_ : stride_;
            if (absStride == 0) absStride = w_ * 4;
            for (int y = 0; y < h_; ++y) {
                const BYTE* row = stride_ < 0 ? p + (size_t)(h_ - 1 - y) * absStride : p + (size_t)y * absStride;
                if ((size_t)((y + 1) * absStride) > len) break;
                uint8_t* dst = &rgba_[(size_t)y * w_ * 4];
                for (int x = 0; x < w_; ++x) {   // BGRX -> RGBA
                    dst[x * 4 + 0] = row[x * 4 + 2];
                    dst[x * 4 + 1] = row[x * 4 + 1];
                    dst[x * 4 + 2] = row[x * 4 + 0];
                    dst[x * 4 + 3] = 255;
                }
            }
            ++serial_;
            b->Unlock();
        }
        b->Release();
    }

    std::string path_;
    IMFSourceReader* reader_ = nullptr;
    IMFSample* pending_ = nullptr;   // decoded, not yet due
    double clock_ = 0, pendingTs_ = 0, duration_ = 0;
    bool done_ = false, eos_ = false;
    int w_ = 0, h_ = 0;
    int32_t stride_ = 0;
    std::vector<uint8_t> rgba_;
    uint64_t serial_ = 0;
};

} // namespace

IMoviePlayer* createMoviePlayer() { return new MFMoviePlayer(); }

} // namespace platform
