// WFC fidelity measurement build: recording audio backend ("spy").
//
// Linked into the wfc_rebuild_audiospy target INSTEAD of platform/win32/Win32Audio.cpp; every
// other product source is unmodified. It implements audio::createAudio() with an IAudio that plays
// nothing and logs every call, stamped with SIMULATION time (lockstep frames / WFC_FIXEDHZ), to
// WFC_AUDIOSPY=<file>:
//   L <t> <sound> <seconds> <path>                                  load (duration from the WAV header)
//   P <t> <sound> <vol>                                             2D one-shot
//   A <t> <sound> <x> <y> <z> <vol> <ref> <max>                     3D one-shot at a FIXED position
//   V <t> <voice> <sound> <positional> <loop> <x> <y> <z> <min> <max> <vol> <pitch> <rolloff> <pan2D> <pan3D>
//   U <t> <voice> <x> <y> <z> <vol> <pitch>                         voice update (position follows owner)
//   S <t> <voice>                                                   voice stop (explicit)
//   E <t> <voice>                                                   voice ended by itself (modelled end of wave)
//   H <t> <x> <y> <z> <fx> <fy> <fz> <rx> <ry> <rz>                 listener pose, once per frame
//   F <t> <n>                                                       update() call n (one per game frame)
// isPlaying() models a real mixer: a non-looping voice sounds for (wave seconds / pitch), a looping
// voice until stopVoice. (Product code may keep a cue instance alive only while its voices play.)
// tools/fidelity/audio-attach.ps1 turns this into attachment findings.
#include "audio/Audio.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
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
        const char* hz = std::getenv("WFC_FIXEDHZ");
        hz_ = hz ? std::atof(hz) : 60.0;
        if (hz_ <= 1) hz_ = 60.0;
    }
    ~SpyAudio() override { if (f_) std::fclose(f_); }

    Sound load(const std::string& path) override {
        Sound s = (Sound)paths_.size();
        paths_.push_back(path);
        double sec = wavSeconds(path);
        secs_.push_back(sec);
        log("L %.4f %d %.3f %s\n", now(), s, sec, path.c_str());
        return s;
    }
    void play(Sound s, float volume) override { log("P %.4f %d %.3f\n", now(), s, volume); }
    void playAt(Sound s, const core::Vec3& p, float volume, float refDist, float maxDist) override {
        log("A %.4f %d %.3f %.3f %.3f %.3f %.2f %.2f\n", now(), s, p.x, p.y, p.z, volume, refDist, maxDist);
    }
    Voice playVoice(Sound s, const VoiceParams& vp) override {
        Voice v = nextVoice_++;
        double len = (s >= 0 && (size_t)s < secs_.size() && secs_[(size_t)s] > 0) ? secs_[(size_t)s] : 0.5;
        live_[v] = Live{now(), vp.loop, len / (vp.pitch > 0.05f ? vp.pitch : 0.05f)};
        log("V %.4f %d %d %d %d %.3f %.3f %.3f %.2f %.2f %.3f %.3f %.3f %.2f %.2f\n", now(), v, s, (int)vp.positional,
            (int)vp.loop, vp.pos.x, vp.pos.y, vp.pos.z, vp.minDist, vp.maxDist, vp.volume, vp.pitch, vp.rolloff, vp.pan2D,
            vp.pan3D);
        return v;
    }
    void stopVoice(Voice v) override {
        if (live_.erase(v)) log("S %.4f %d\n", now(), v);
    }
    void updateVoice(Voice v, float volume, float pitch, const core::Vec3& p) override {
        log("U %.4f %d %.3f %.3f %.3f %.3f %.3f\n", now(), v, p.x, p.y, p.z, volume, pitch);
    }
    // Not marked override: builds whose IAudio predates isPlaying() simply get an extra method.
    bool isPlaying(Voice v) const { return live_.count(v) != 0; }
    void setListener(const core::Vec3& p, const core::Vec3& f, const core::Vec3& r) override {
        log("H %.4f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n", now(), p.x, p.y, p.z, f.x, f.y, f.z, r.x, r.y, r.z);
    }
    void update() override {
        ++frames_;
        double t = now();
        for (auto it = live_.begin(); it != live_.end();) {
            if (!it->second.loop && t - it->second.start >= it->second.len) {
                log("E %.4f %d\n", t, it->first);
                it = live_.erase(it);
            } else {
                ++it;
            }
        }
        log("F %.4f %d\n", t, frames_);
    }

private:
    struct Live { double start; bool loop; double len; };
    // Simulation time: wfc_rebuild_observe runs in lockstep (one fixed step per frame), so events of
    // frame k carry t = (k-1)/hz and its F marker t = k/hz.
    double now() const { return frames_ / hz_; }
    template <class... A> void log(const char* fmt, A... a) { if (f_) std::fprintf(f_, fmt, a...); }

    std::FILE* f_ = nullptr;
    double hz_ = 60.0;
    std::vector<std::string> paths_;
    std::vector<double> secs_;
    std::map<Voice, Live> live_;
    Voice nextVoice_ = 4096;
    int frames_ = 0;
};

} // namespace

IAudio* createAudio() { return new SpyAudio(); }

} // namespace audio
