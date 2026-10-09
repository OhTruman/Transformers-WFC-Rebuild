// libvgmstream r2117 decode of the original FMOD banks (see platform/FsbDecode.h).
#include "platform/FsbDecode.h"
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <mutex>

#if defined(WFC_VGMSTREAM) && WFC_VGMSTREAM
extern "C" {
#include "libvgmstream.h"
#include "libvgmstream_streamfile.h"
}
#endif

namespace platform {

FsbStats& fsbStats() { static FsbStats s; return s; }

AudioSource audioSource() {
    static const AudioSource s = [] {
        const char* e = std::getenv("WFC_AUDIO_SOURCE");
        if (e && std::strcmp(e, "wav") == 0) return AudioSource::Wav;
        if (e && std::strcmp(e, "fsb") == 0) return AudioSource::Fsb;
        return AudioSource::Auto;
    }();
    return s;
}

#if defined(WFC_VGMSTREAM) && WFC_VGMSTREAM
bool fsbDecodeAvailable() { return true; }

FsbStream::~FsbStream() {
    if (lib_) libvgmstream_free((libvgmstream_t*)lib_);
    if (sf_) libstreamfile_close((libstreamfile_t*)sf_);
}

bool FsbStream::open(const std::string& path) {
    libstreamfile_t* sf = libstreamfile_open_from_stdio(path.c_str());
    if (!sf) return false;
    sf_ = sf;
    libvgmstream_config_t cfg{};
    cfg.ignore_loop = true;                          // one pass of the stream: the game loops it itself
    cfg.force_sfmt = LIBVGMSTREAM_SFMT_PCM16;
    // Opening a stream opens its FFmpeg decoder, whose static tables (WMA Pro / XMA VLCs) FFmpeg 5 builds on the first open and
    // not thread-safely ("Assertion ret >= 0 failed at libavcodec/vlc.c" with parallel FIRST opens). Opens are serialised until
    // one has succeeded; after that they run in parallel (an open is ~2.6 ms, as much as decoding a small bank: serialising
    // every open capped a match load at ~1.2 k opens / 3 s).
    static std::mutex openMx;
    static std::atomic<bool> warmed{false};
    libvgmstream_t* lib;
    if (warmed.load(std::memory_order_acquire)) lib = libvgmstream_create(sf, 0, &cfg);
    else {
        std::lock_guard<std::mutex> lk(openMx);
        lib = libvgmstream_create(sf, 0, &cfg);
        if (lib) warmed.store(true, std::memory_order_release);
    }
    if (!lib) return false;
    lib_ = lib;
    channels_ = lib->format->channels;
    rate_ = lib->format->sample_rate;
    frames_ = lib->format->play_samples > 0 ? lib->format->play_samples : 0;
    return channels_ > 0 && rate_ > 0;
}

int FsbStream::next(const int16_t*& pcm) {
    auto* lib = (libvgmstream_t*)lib_;
    if (!lib || done_) return 0;
    if (lib->decoder->done) { done_ = true; return 0; }
    if (libvgmstream_render(lib) < 0) { done_ = true; return -1; }
    pcm = (const int16_t*)lib->decoder->buf;
    return lib->decoder->buf_bytes / (2 * channels_);
}

bool decodeFsb(const std::string& path, std::vector<int16_t>& pcm, int& channels, int& rate) {
    FsbStream st;
    if (!st.open(path)) return false;
    channels = st.channels(); rate = st.rate();
    pcm.clear();
    if (st.frames() > 0) pcm.reserve((size_t)st.frames() * (size_t)channels);
    const int16_t* b = nullptr;
    for (int n; (n = st.next(b)) != 0;) {
        if (n < 0) return false;
        pcm.insert(pcm.end(), b, b + (size_t)n * (size_t)channels);
    }
    return !pcm.empty();
}
#else
bool fsbDecodeAvailable() { return false; }
bool decodeFsb(const std::string&, std::vector<int16_t>&, int&, int&) { return false; }
FsbStream::~FsbStream() {}
bool FsbStream::open(const std::string&) { return false; }
int FsbStream::next(const int16_t*&) { return 0; }
#endif

}   // namespace platform
