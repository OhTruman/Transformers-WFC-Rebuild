// Clean-room reconstruction — WFC SoundCue playback (HM_Engine SoundNodeRoot + SoundNodeWaveEvent).
//
// WFC cues are not UE3 node graphs but a flat timeline: a SoundNodeRoot (Volume dB, Pitch
// semitones, random variation ranges, DistanceMin/Max + RolloffFactor in UU, SmartPan
// distances, MaxConcurrentPlayCount on the cue) whose children are SoundNodeWaveEvents (start
// Time, Volume/Pitch offsets + variation, a random choice among their waves, and an optional
// VolumeCurve over SOUND_DISTANCE in UU = distance layering). Values are [CONF], extracted from
// the cooked cues (BL_WPN_GUN_ION_BLASTER / BL_WPN_FOLEY in MP_IAC_Streets_BASE_m); the class
// defaults come from HM_Engine.Default__SoundNodeRoot (Volume -6, Distance 400..6400, Rolloff 1).
#pragma once
#include <string>
#include <vector>
#include "audio/Audio.h"
#include "core/Math.h"

namespace game {

class SoundCues {
public:
    // Load every wave the cue table references (ExtractedAssets/content/<pkg>/<wave>.wav).
    void load(audio::IAudio* a, const std::string& contentRoot);

    // Play a cue at `pos`. `paramDistM` drives the per-wave distance curves (SOUND_DISTANCE):
    // for the local player's own weapon (kSmartPan_PreferPlayer) it is the source's distance
    // from the player, otherwise from the listener.
    bool play(const char* cue, const core::Vec3& pos, float paramDistM);
    void tick(float dt);

    int activeInstances(const char* cue) const;

private:
    struct Pending { int inst; float t; int event; };
    struct Instance { int cue; int id; float age; core::Vec3 pos; float paramDist; std::vector<audio::Voice> voices; };

    void launch(Instance& in, int event);

    audio::IAudio* audio_ = nullptr;
    std::vector<std::vector<std::vector<audio::Sound>>> waves_;   // [cue][event][wave]
    std::vector<Instance> live_;
    std::vector<Pending> pending_;
    int nextId_ = 0;
};

} // namespace game
