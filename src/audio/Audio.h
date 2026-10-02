// Clean-room reconstruction — audio abstraction.
// Gameplay depends ONLY on this interface, never on the Windows audio implementation.
#pragma once
#include <string>
#include "core/Math.h"

namespace audio {

using Sound = int;
constexpr Sound kInvalidSound = -1;
using Voice = int;
constexpr Voice kInvalidVoice = -1;

// One playing wave. Distances in metres. With rolloff > 0 the gain follows FMOD Ex's 3D inverse
// model (WFC's Hm sound system sits on FMOD): 1 inside minDist, minDist / (minDist + rolloff *
// (d - minDist)) beyond it, held constant past maxDist.
struct VoiceParams {
    float volume = 1.0f;        // linear
    float pitch = 1.0f;         // playback-rate multiplier
    bool positional = false;
    core::Vec3 pos{0, 0, 0};
    float minDist = 4.0f, maxDist = 64.0f, rolloff = 1.0f;
    float pan2D = 2.0f, pan3D = 4.0f;  // SmartPan: centred inside pan2D, fully panned beyond pan3D
    bool loop = false;                 // wave loops until stopVoice
};

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

    // Full-control voice (SoundCue wave events). Returns a handle usable with stopVoice.
    virtual Voice playVoice(Sound s, const VoiceParams& p) = 0;
    virtual void stopVoice(Voice v) = 0;
    // Live update of a playing voice (looping engine/boost layers follow speed and position).
    virtual void updateVoice(Voice v, float volume, float pitch, const core::Vec3& pos) = 0;

    // Listener (camera) pose, set once per frame before update().
    virtual void setListener(const core::Vec3& pos, const core::Vec3& forward,
                             const core::Vec3& right) = 0;

    // Pump the mixer; call once per frame.
    virtual void update() = 0;
};

// Platform factory. Returns a working backend, or a silent no-op one if no audio device.
IAudio* createAudio();

} // namespace audio
