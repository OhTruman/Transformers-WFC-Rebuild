// Clean-room reconstruction — the Streets world sound bed (MP_IAC_Streets_AUDIO_m), from the recovered
// ExtractedAssets/VerticalSlice/Maps/MP_IAC_Streets/audio.json (AssetTools vs_audio.py, authored.db).
//
// [CONF] data: 40 AmbientSound (point), 17 HmAmbientSoundVolumeEmitter (Radius 500 x scale box), 13
// HmAmbientSoundLineEmitter (LineLength 500 x scale), all auto-play looping cues of the map bank
// BL_LVL_MP_IAC_STREETS; 9 Kismet audio zones (SeqEvent_Touch on a TriggerVolume, Pawn only, camera
// ignored): entering one runs SeqAct_Reverb (a SoundMixerProperties preset: MASTER_WET Reverb / Echo,
// FadeInTime) and starts its SeqAct_PlayPlayerPositionalSound pools (looping random one-shots every
// DelayMin..DelayMax s, DistanceMin..Max from the player); "Scene 0 Ended" stops the pool.
// [INFERRED, AssetTools] entering a zone ends the previous zone's scene (one reverb + pool at a time);
// leaving a zone without entering another keeps it (no on_untouched ops); a player in a gap at spawn
// has the Default (dry) environment. Volume-emitter half extents = Radius x actor scale; line =
// LineLength x scale X along the actor X axis.
// [PROVISIONAL] how native code places a shaped emitter's sound (HmAmbientSoundLineEmitter.GetLinePoints /
// HmAmbientSoundVolumeEmitter.GetExtents are native): here the nearest point of the box / line to the
// listener (inside a box: at the listener, i.e. non-directional room tone). ReVa request.
// [PROVISIONAL] voice budget: the most audible kMaxActive emitters play; the rest are virtual. The original
// device has MaxChannels=96 (Xe-TransEngine.ini [HM_Engine.FmodAudioDevice]); its virtual-voice policy is native.
#pragma once
#include <string>
#include <vector>
#include "audio/Audio.h"
#include "core/Math.h"

namespace game {

class SoundCues;

class AmbientAudio {
public:
    // Loads audio.json (cues are added to `cues`). Returns false if the file is missing.
    bool load(const std::string& audioJsonPath, const std::string& contentRoot, SoundCues& cues, audio::IAudio* a);
    // `listener` = camera (attenuation / emitter placement); `pawn` = the touching actor for zones.
    void tick(float dt, const core::Vec3& listener, const core::Vec3& pawn, SoundCues& cues);

    bool loaded() const { return loaded_; }
    int activeEmitters() const { return active_; }
    int emitterCount() const { return (int)emitters_.size(); }
    const char* zoneName() const { return zone_ >= 0 ? zones_[(size_t)zone_].name.c_str() : "-"; }
    int oneShotsPlayed() const { return oneShots_; }

private:
    struct Emitter {
        enum Kind { Point, Volume, Line } kind = Point;
        std::string cue;
        core::Vec3 origin{0, 0, 0};
        core::Vec3 axis[3];          // actor axes scaled by the actor scale (glTF, metres per unit)
        float half = 0.0f;           // box half extent (Radius, m) / half line length (m), in axis units
        float volDb = 0.0f, minM = 4.0f, maxM = 64.0f, rolloff = 1.0f;
        int instance = -1;
        float level = 0.0f;          // current fade level (0..1)
        bool want = false;
        bool loops = true;           // auto-play cue with a looping event; a one-shot cue plays once
        bool done = false;
    };
    struct Pool { std::string cue; float delayMin, delayMax, distMinM, distMaxM; bool looping; };
    struct Zone {
        std::string name;
        std::vector<core::Vec3> tris;   // triangle soup of the trigger volume (glTF)
        core::Vec3 bmin, bmax;
        audio::Environment env;
        float fadeIn = 0.25f;
        std::vector<Pool> pools;
    };

    core::Vec3 placeFor(const Emitter& e, const core::Vec3& listener) const;
    bool inside(const Zone& z, const core::Vec3& p) const;
    void enterZone(int z);

    bool loaded_ = false;
    audio::IAudio* audio_ = nullptr;
    std::vector<Emitter> emitters_;
    std::vector<Zone> zones_;
    int zone_ = -1;
    bool zoneChecked_ = false;
    std::vector<float> poolTimers_;
    int active_ = 0;
    int oneShots_ = 0;
    float zoneTimer_ = 0.0f;
};

} // namespace game
