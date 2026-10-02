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

// One playing wave. Distances in metres. Cue voices follow FmodAudioDevice's per-source spatialization
// [CONF native 0x82759B08]: inverse rolloff Min / ((max(d,Min) - Min) * Rolloff + Min), culled when
// max(d,Min) > Max, rear attenuation, and the SmartPan 2D <-> 3D mix (see Win32Audio).
struct VoiceParams {
    float volume = 1.0f;        // linear
    float pitch = 1.0f;         // playback-rate multiplier
    bool positional = false;
    core::Vec3 pos{0, 0, 0};
    float minDist = 4.0f, maxDist = 64.0f, rolloff = 1.0f;
    float pan2D = 2.0f, pan3D = 4.0f;  // SmartPan: centred inside pan2D, fully panned beyond pan3D
    bool loop = false;                 // wave loops until stopVoice
    float rearAttenDb = 0.0f;          // SoundNodeRoot.RearAttenuation: extra dB for sources behind the listener
    bool wet = true;                   // routed through the MASTER_WET bus (environment reverb / echo)
    int spatial = 0;                   // SoundNodeRoot.Spatialization: 0 k3D, 1 k2D, 2 kSmartPan, 3 kSmartPan_PreferPlayer
    float panAtten3DDb = 0.0f;         // SmartPanAttenuation3D
};

// MASTER_WET environment (SoundMixerProperties DSP preset of a Kismet SeqAct_Reverb zone): FMOD Ex SFX
// reverb in I3DL2 units (mB, s) plus the category Echo. Defaults = the Default preset (reverb off).
struct Environment {
    float room = -10000.0f, roomHF = -10000.0f;       // mB: reverb send level, its high-frequency cut
    float decayTime = 1.0f, decayHFRatio = 1.0f;      // s, ratio
    float reflections = -10000.0f, reflectionsDelay = 0.0f;   // mB, s
    float reverb = -10000.0f, reverbDelay = 0.0f;     // mB (late reverb level), s (after reflections)
    float diffusion = 100.0f, density = 100.0f;       // %
    float hfReference = 5000.0f;                      // Hz
    float echoDelayMs = 500.0f, echoDecay = 0.5f, echoWet = 0.0f, echoDry = 1.0f;
};

// Read-back of one voice as last mixed (diagnostics): pan -1..1 after SmartPan, distance gain, channel gains.
struct VoiceInfo { float dist = 0.0f, pan = 0.0f, atten = 1.0f, gainL = 0.0f, gainR = 0.0f; };

// Read-back of the mixer (diagnostics).
struct MixStats { float peakDb = -96.0f; float gainReductionDb = 0.0f; int voices = 0; int wetVoices = 0;
                  float mixMsPerBlock = 0.0f;      // CPU cost of one 1024-frame block (~21 ms of audio)
                  float lastUpdateMs = 0.0f, lastMixMs = 0.0f; int lastUpdateBlocks = 0; int maxUpdateBlocks = 0; };

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
    // True while the voice is still sounding (backends that cannot tell report false).
    virtual bool isPlaying(Voice) const { return false; }
    // A sample's own loop region (FSB sample-header loop start / end, in source-file sample frames, end
    // inclusive). Without one a looping voice loops the whole sample.
    // Backends that cannot loop a region return false.
    virtual bool setLoopPoints(Sound, uint32_t /*startFrame*/, uint32_t /*endFrame*/) { return false; }
    virtual bool reportsVoices() const { return false; }   // isPlaying() is meaningful
    // MASTER_WET environment, cross-faded over `fadeSeconds` (mixer preset FadeInTime).
    virtual void setEnvironment(const Environment&, float /*fadeSeconds*/) {}
    // Master category DSP: compressor (dB threshold, ms attack/release, dB make-up).
    virtual void setMasterCompressor(float /*thresholdDb*/, float /*attackMs*/, float /*releaseMs*/, float /*makeupDb*/) {}
    virtual bool mixStats(MixStats&) const { return false; }
    virtual bool voiceInfo(Voice, VoiceInfo&) const { return false; }
    // Local player pawn origin (Actor.Location) for kSmartPan_PreferPlayer voices; valid = a local pawn exists.
    virtual void setSmartPanPlayer(const core::Vec3& /*pos*/, bool /*valid*/) {}

    // Listener (camera) pose, set once per frame before update().
    virtual void setListener(const core::Vec3& pos, const core::Vec3& forward,
                             const core::Vec3& right) = 0;

    // Pump the mixer; call once per frame.
    virtual void update() = 0;
};

// Platform factory. Returns a working backend, or a silent no-op one if no audio device.
IAudio* createAudio();

} // namespace audio
