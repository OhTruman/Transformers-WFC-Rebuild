// Clean-room reconstruction — WFC SoundCue playback (HM_Engine SoundNodeRoot + SoundNodeWaveEvent).
//
// WFC cues are not UE3 node graphs but a flat timeline: a SoundNodeRoot (Volume dB, Pitch
// semitones, random variation ranges, DistanceMin/Max + RolloffFactor in UU, SmartPan
// distances, an optional SoundParameter, MaxConcurrentPlayCount on the cue) whose children are
// SoundNodeWaveEvents (start Time, Volume/Pitch offsets + variation, ChanceToPlayNone, bLooping,
// a random choice among their waves, VolumeCurve/PitchCurve over the root's SoundParameter, and
// an Envelope of volume/pitch curves over the cue's playback time). Values are [CONF], generated
// by tools/systems/gen_cues.py from the cooked cues into SoundCues.inc.
//   SoundParameters.SOUND_DISTANCE (HmSoundParameterDistanceToListener, Max 18000 UU)
//   SoundParameters.Optimus_Prime_Speed (HmSoundParameter, Max 120): vehicle speed in mph [MED:
//   the curves put nominal pitch at 33 = the 15 m/s cruise speed in mph].
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "audio/Audio.h"
#include "core/Math.h"

namespace game {

class SoundCues {
public:
    // Where an instance plays. kWorld: a fixed world position (one-shot world impacts). Any other
    // owner id: the instance is attached (UE3/Hm AudioComponent attached to an actor or socket) and
    // follows the owner every tick, including its one-shot and delayed wave events. Owner ids are
    // defined by the caller and resolved through setResolver().
    static constexpr int kWorld = -1;
    struct Emitter {
        core::Vec3 pos{0, 0, 0};    // initial / world position
        int owner = kWorld;
        core::Vec3 offset{0, 0, 0}; // owner-local offset, interpreted by the resolver
    };
    using Resolver = std::function<bool(int owner, const core::Vec3& offset, core::Vec3& outPos)>;
    void setResolver(Resolver r) { resolver_ = std::move(r); }

    // Load every wave the cue table references (ExtractedAssets/content/<pkg>/<wave>.wav).
    void load(audio::IAudio* a, const std::string& contentRoot);

    // Play a cue at `pos`; returns an instance id (-1 on failure). `distM` drives SOUND_DISTANCE
    // curves (for the local player's own sounds, kSmartPan_PreferPlayer, the distance from the
    // player); `speedMph` drives speed-keyed curves (vehicle cues).
    int play(const char* cue, const core::Vec3& pos, float distM, float speedMph = 0.0f);
    int play(const char* cue, const Emitter& em, float distM, float speedMph = 0.0f);
    // Live parameters for a playing (e.g. looping) instance; `pos` is ignored for attached instances.
    void update(int instance, const core::Vec3& pos, float speedMph);
    // Stop an instance, fading its voices out over `fade` seconds (0 = immediate).
    void stop(int instance, float fade);
    bool playing(int instance) const;
    void tick(float dt);

    int activeInstances(const char* cue) const;
    size_t liveInstances() const { return live_.size(); }
    size_t pendingEvents() const { return pending_.size(); }

private:
    struct VoiceRef { audio::Voice v; int event; float baseDb, baseSt; };
    struct Pending { int inst; float t; int event; };
    struct Instance {
        int cue; int id; float age; core::Vec3 pos; float distM, speedMph;
        int owner = kWorld; core::Vec3 offset{0, 0, 0}; bool posDirty = false;
        std::vector<VoiceRef> voices;
        float fade = -1.0f, fadeLeft = 0.0f;   // fade-out duration / remaining (fade < 0 = none)
        bool looping = false;
    };

    void launch(Instance& in, int event);
    void refresh(Instance& in);
    bool resolve(Instance& in);
    float paramFor(const Instance& in) const;
    Instance* find(int id);

    audio::IAudio* audio_ = nullptr;
    Resolver resolver_;
    std::vector<std::vector<std::vector<audio::Sound>>> waves_;   // [cue][event][wave]
    std::vector<Instance> live_;
    std::vector<Pending> pending_;
    int nextId_ = 0;
};

} // namespace game
