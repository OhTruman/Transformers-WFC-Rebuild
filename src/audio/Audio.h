// Clean-room reconstruction — audio abstraction.
// Gameplay depends ONLY on this interface, never on the Windows audio implementation.
#pragma once
#include <string>
#include "core/Math.h"

namespace audio {

using Sound = int;
constexpr Sound kInvalidSound = -1;

class IAudio {
public:
    virtual ~IAudio() = default;

    // Load a PCM WAV from disk; returns a handle (kInvalidSound on failure).
    virtual Sound load(const std::string& path) = 0;

    // 2D one-shot (UI/non-diegetic). volume 0..1.
    virtual void play(Sound s, float volume = 1.0f) = 0;

    // 3D positional one-shot: attenuated by distance and panned by listener orientation.
    // refDist = full-volume radius, maxDist = silence radius (world units = metres).
    virtual void playAt(Sound s, const core::Vec3& pos, float volume,
                        float refDist, float maxDist) = 0;

    // Listener (camera) pose, set once per frame before update().
    virtual void setListener(const core::Vec3& pos, const core::Vec3& forward,
                             const core::Vec3& right) = 0;

    // Pump the mixer; call once per frame.
    virtual void update() = 0;
};

// Platform factory. Returns a working backend, or a silent no-op one if no audio device.
IAudio* createAudio();

} // namespace audio
