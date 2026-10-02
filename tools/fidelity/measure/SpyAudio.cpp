// WFC fidelity measurement build: recording audio backend ("spy").
//
// Linked into the wfc_rebuild_audiospy target INSTEAD of platform/win32/Win32Audio.cpp; every
// other product source is unmodified. It implements audio::createAudio() with an IAudio that plays
// nothing and logs every call with a timestamp to WFC_AUDIOSPY=<file>:
//   L <t> <sound> <seconds> <path>                       load (duration from the WAV header)
//   P <t> <sound> <vol>                                  2D one-shot
//   A <t> <sound> <x> <y> <z> <ref> <max>                3D one-shot at a FIXED position
//   V <t> <voice> <sound> <positional> <loop> <x> <y> <z> <min> <max>   voice start
//   U <t> <voice> <x> <y> <z> <vol> <pitch>              voice update (position follows owner)
//   S <t> <voice>                                        voice stop
//   H <t> <x> <y> <z>                                    listener (camera) position, once per frame
// tools/fidelity/audio-attach-report.ps1 turns this into attachment findings.
#include "audio/Audio.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace audio {
namespace {

double wavSeconds(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return -1;
    char hdr[12];
    if (!f.read(hdr, 12)) return -1;
    unsigned byteRate = 0;
    for (;;) {
        char id[4];
        unsigned size = 0;
        if (!f.read(id, 4) || !f.read(reinterpret_cast<char*>(&size), 4)) return -1;
        if (std::string(id, 4) == "fmt ") {
            std::vector<char> fmt(size);
            f.read(fmt.data(), size);
            if (size >= 12) byteRate = *reinterpret_cast<unsigned*>(fmt.data() + 8);
        } else if (std::string(id, 4) == "data") {
            return byteRate ? double(size) / byteRate : -1;
        } else {
            f.seekg(size + (size & 1), std::ios::cur);
        }
    }
}

class SpyAudio : public IAudio {
public:
    SpyAudio() {
        const char* p = std::getenv("WFC_AUDIOSPY");
        f_ = std::fopen(p && *p ? p : "audiospy.txt", "wb");
        t0_ = std::chrono::steady_clock::now();
    }
    ~SpyAudio() override { if (f_) std::fclose(f_); }

    Sound load(const std::string& path) override {
        Sound s = (Sound)paths_.size();
        paths_.push_back(path);
        log("L %.4f %d %.3f %s\n", now(), s, wavSeconds(path), path.c_str());
        return s;
    }
    void play(Sound s, float volume) override { log("P %.4f %d %.3f\n", now(), s, volume); }
    void playAt(Sound s, const core::Vec3& p, float volume, float refDist, float maxDist) override {
        log("A %.4f %d %.3f %.3f %.3f %.2f %.2f\n", now(), s, p.x, p.y, p.z, refDist, maxDist);
        (void)volume;
    }
    Voice playVoice(Sound s, const VoiceParams& vp) override {
        Voice v = nextVoice_++;
        log("V %.4f %d %d %d %d %.3f %.3f %.3f %.2f %.2f\n", now(), v, s, (int)vp.positional, (int)vp.loop,
            vp.pos.x, vp.pos.y, vp.pos.z, vp.minDist, vp.maxDist);
        return v;
    }
    void stopVoice(Voice v) override { log("S %.4f %d\n", now(), v); }
    void updateVoice(Voice v, float volume, float pitch, const core::Vec3& p) override {
        log("U %.4f %d %.3f %.3f %.3f %.3f %.3f\n", now(), v, p.x, p.y, p.z, volume, pitch);
    }
    void setListener(const core::Vec3& p, const core::Vec3&, const core::Vec3&) override {
        log("H %.4f %.3f %.3f %.3f\n", now(), p.x, p.y, p.z);
    }
    void update() override {}

private:
    double now() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count(); }
    template <class... A> void log(const char* fmt, A... a) { if (f_) std::fprintf(f_, fmt, a...); }

    std::FILE* f_ = nullptr;
    std::chrono::steady_clock::time_point t0_;
    std::vector<std::string> paths_;
    Voice nextVoice_ = 4096;
};

} // namespace

IAudio* createAudio() { return new SpyAudio(); }

} // namespace audio
