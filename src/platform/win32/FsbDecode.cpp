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

bool decodeFsb(const std::string& path, std::vector<int16_t>& pcm, int& channels, int& rate) {
    libstreamfile_t* sf = libstreamfile_open_from_stdio(path.c_str());
    if (!sf) return false;
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
    bool ok = false;
    if (lib) {
        channels = lib->format->channels;
        rate = lib->format->sample_rate;
        pcm.clear();
        if (lib->format->play_samples > 0) pcm.reserve((size_t)lib->format->play_samples * (size_t)channels);
        ok = true;
        while (!lib->decoder->done) {
            if (libvgmstream_render(lib) < 0) { ok = false; break; }
            const int16_t* b = (const int16_t*)lib->decoder->buf;
            pcm.insert(pcm.end(), b, b + lib->decoder->buf_bytes / 2);
        }
        libvgmstream_free(lib);
    }
    libstreamfile_close(sf);
    return ok && channels > 0 && rate > 0 && !pcm.empty();
}
#else
bool fsbDecodeAvailable() { return false; }
bool decodeFsb(const std::string&, std::vector<int16_t>&, int&, int&) { return false; }
#endif

}   // namespace platform
