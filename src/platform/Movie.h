// Clean-room reconstruction — full-screen movie playback (the shipped Bink movies, extracted by AssetTools as
// H.264 + FLAC .mkv). Video frames on demand; the movie's sound is decoded and played by Systems
// (audio::MovieAudioPlayer, Systems M07). Platform-neutral interface; the Win32 implementation uses Media Foundation.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace platform {

class IMoviePlayer {
public:
    virtual ~IMoviePlayer() = default;
    virtual bool open(const std::string& path) = 0;
    // Advances the playback clock; decodes video frames as they become due.
    virtual void advance(double dt) = 0;
    virtual bool finished() const = 0;
    virtual double duration() const = 0;
    virtual double position() const = 0;
    virtual void restart() = 0;   // loop
    // Current video frame (RGBA8, top row first); serial changes when a new frame was decoded.
    virtual bool frame(const uint8_t*& rgba, int& w, int& h, uint64_t& serial) const = 0;
};

IMoviePlayer* createMoviePlayer();

} // namespace platform
