// Clean-room reconstruction — a level's world sound: placed emitters, Kismet audio zones / pools / reverb, Kismet
// audio ops (LevelAudioScript), the level's cue bank and presets, all from the level manifests (no level-specific
// code). The authored facts below are those of the first map, MP_IAC_Streets (MP_IAC_Streets_AUDIO_m, AssetTools
// ExtractedAssets/VerticalSlice/Maps/MP_IAC_Streets/audio.json, vs_audio.py / authored.db); the rules are generic.
//
// [CONF] data: 40 AmbientSound (point), 17 HmAmbientSoundVolumeEmitter (Radius 500 x scale box), 13
// HmAmbientSoundLineEmitter (LineLength 500 x scale), all auto-play looping cues of the map bank
// BL_LVL_MP_IAC_STREETS; 9 Kismet audio zones (SeqEvent_Touch on a TriggerVolume, Pawn only, camera
// ignored): entering one runs SeqAct_Reverb (a SoundMixerProperties preset: MASTER_WET Reverb / Echo,
// FadeInTime) and starts its SeqAct_PlayPlayerPositionalSound pools (looping random one-shots every
// DelayMin..DelayMax s, DistanceMin..Max from the player); "Scene 0 Ended" stops the pool.
// [CONF native, RE 7c4a2e0 A1] Zone.Enter (0x827892D8) on a Touch: when the local PlayerController's
// AmbientAudioZone differs, the previous zone stops (IsEntered false -> "Scene 0 Ended": its pool stops), the
// new zone becomes current and its "Scene 0 Begun" fires SeqAct_Reverb, which enables the new REVERB_* preset
// and explicitly disables the previous one (SoundMixer::activateReverb). Touching the current zone again does
// nothing. No Streets zone links UnTouched: leaving every volume keeps the last zone's reverb and pool, and an
// inner volume's reverb persists when walking back into a still-overlapping outer one (no new Touch). The
// Default (dry) state exists only before the first Touch and after a level change (mixer Flush).
// [CONF native A7] shaped emitters, every frame: line (LineEmitter 0x82760110) = closest point on the segment
// origin -/+ X * LineLength/2 (X includes DrawScale3D), clamped to the ends; volume (VolumeEmitter
// 0x82760278) = listener in actor-local space clamped to +/-Radius per axis (an oriented box, scaled by
// DrawScale3D; inside -> the listener itself). The AudioComponent sits at that point, so pan, attenuation,
// SmartPan distance and occlusion all use it.
// [CONF authored, AssetTools a23c675 mp_iac_streets_complete.json] 70 emitters (40 point, 13 line, 17
// volume), all looping auto-play cues; no Kismet op toggles them (streets_kismet.json).
// [CONF] Playback: each AudioComponent auto-plays once at level start (bAutoPlay) and is registered with the
// native per-cue instance limiting (RegisterInstanceLimiting: global per cue asset, kKillFarthest against the
// listener, the newcomer refused when it is the farthest). Line / volume emitters re-Play every tick when
// not playing (A7 tick); no line / volume cue has more emitters than its limit, so they all play. Four point
// cues do (EMIT_FLOURESCENT_LIGHTS 13 / 5, EMIT_ENERGON_LIQUID_CRATERS 12 / 5, EMIT_LAMP_POSTS 8 / 5,
// EMIT_FLOOD_LIGHTS 5 / 3): the instances nearest the level-start listener play.
// [HIGH] A point AmbientSound refused or killed at start never restarts (Engine AmbientSound has no script
// or tick that replays its AudioComponent; WFC's AmbientSound class is script-less).
// [UNKNOWN, native] whether a listener exists at the native level-start registration; the rebuild registers on
// the first tick with the spawn camera. FMOD's own virtual-voice handling (MaxChannels 96) is internal; the
// rebuild mixes every playing voice (culled beyond DistanceMax by the per-voice native rule).
// LEVEL MANIFESTS (generic, no level-specific code): a level's audio = the AssetTools map manifest
// (<assets>/Maps/<level>/audio.json: emitters, zones, reverb presets, pools, cue bank) and / or the Systems level
// manifest compiled in from tools/systems/gen_level_audio.py (LevelAudio.inc: the level's Kismet audio ops + cues +
// reverb presets for the UI levels; per-cue-asset concurrency limits for every level). Either may be absent; both
// load through load() and unload through unload().
#pragma once
#include <string>
#include <vector>
#include "audio/Audio.h"
#include "core/Math.h"
#include "game/LevelAudioScript.h"

namespace game {

class SoundCues;
class MusicPlayer;

class AmbientAudio {
public:
    // Loads a level's audio: the manifest file `audioJsonPath` (may be missing) merged with the compiled-in Systems
    // manifest of `level` (default: the file's "map"). Any previously loaded level is unloaded first. Returns false
    // if neither exists.
    bool load(const std::string& audioJsonPath, const std::string& contentRoot, SoundCues& cues, audio::IAudio* a,
              const std::string& level = std::string());
    // Map unload: stops and forgets everything the map owns (bed, zones, pools, map cues + samples, presets).
    void unload(SoundCues& cues);
    // Round reset without a level change (Kismet Reset of the zone / pool ops); the bed keeps playing.
    void resetMatch();
    // `listener` = camera (attenuation / emitter placement); `pawn` = the touching actor for zones.
    void tick(float dt, const core::Vec3& listener, const core::Vec3& pawn, SoundCues& cues);
    // The level's WorldInfo music player (SeqAct_PlayMusic / StopMusic ops); owned by the caller.
    void setMusicPlayer(MusicPlayer* m) { music_ = m; }
    // A frontend-owned Kismet trigger of the loaded level ("FsCommand:enterFrontEnd", "MovieStopped:FMV_intro").
    int fireEvent(const std::string& trigger, SoundCues& cues, const core::Vec3& listener);
    const LevelAudioScript& script() const { return script_; }
    const std::string& levelName() const { return level_; }
    // The compiled-in Systems level manifests.
    static bool hasLevelManifest(const std::string& level);
    static int levelManifestCount();
    static const char* levelManifestName(int i);
    // The first SeqAct_PlayMusic track a level's manifest authors (false: none).
    static bool levelMusicTrack(const std::string& level, MusicTrack& out);

    bool loaded() const { return loaded_; }
    int activeEmitters() const { return active_; }
    int refusedAtStart() const { return refusedAtStart_; }   // point emitters refused by instance limiting
    int emitterCount() const { return (int)emitters_.size(); }
    // Diagnostics: emitter kind (0 point, 1 volume, 2 line), cue, current cue instance (-1 = not playing).
    int emitterKind(int i) const { return (int)emitters_[(size_t)i].kind; }
    const std::string& emitterCue(int i) const { return emitters_[(size_t)i].cue; }
    int emitterInstance(int i) const { return emitters_[(size_t)i].instance; }
    int zoneCount() const { return (int)zones_.size(); }
    int currentZone() const { return zone_; }
    bool poolsRunning() const { return zone_ >= 0 && sceneActive_[(size_t)zone_] && !poolTimers_.empty(); }
    int poolCount() const { int n = 0; for (const Zone& z : zones_) n += (int)z.pools.size(); return n; }
    const char* zoneName() const { return zone_ >= 0 ? zones_[(size_t)zone_].name.c_str() : "-"; }
    int oneShotsPlayed() const { return oneShots_; }

private:
    struct Emitter {
        enum Kind { Point, Volume, Line } kind = Point;
        std::string cue;
        core::Vec3 origin{0, 0, 0};
        core::Vec3 axis[3];          // actor axes scaled by the actor scale (glTF, metres per unit)
        float half = 0.0f;           // box half extent (Radius, m) / half line length (m), in axis units
        int instance = -1;           // its AudioComponent (cue instance), -1 = not playing
    };
    struct Pool { std::string cue; float delayMin, delayMax, distMinM, distMaxM; bool looping; };
    struct Zone {
        std::string name;
        std::vector<core::Vec3> tris;   // triangle soup of the trigger volume (glTF)
        core::Vec3 bmin, bmax;
        float priority = 0.0f;          // REVERB_* mixer preset Priority (higher wins) [CONF]
        std::string preset;
        std::vector<Pool> pools;
    };

    core::Vec3 placeFor(const Emitter& e, const core::Vec3& listener) const;
    bool inside(const Zone& z, const core::Vec3& p) const;
    void enterZone(int z, SoundCues& cues);
    std::vector<char> touching_;         // per zone: the pawn overlapped it at the last check (Touch edge)
    std::vector<char> sceneActive_;      // per zone: IsEntered with Scene 0 begun (its pools run)

    bool loaded_ = false;
    std::string level_;
    LevelAudioScript script_;
    MusicPlayer* music_ = nullptr;
    audio::IAudio* audio_ = nullptr;
    std::vector<Emitter> emitters_;
    std::vector<Zone> zones_;
    int zone_ = -1;                      // PlayerController.AmbientAudioZone (-1 = None)
    std::vector<float> poolTimers_;
    int active_ = 0;
    bool started_ = false;
    int refusedAtStart_ = 0;
    int oneShots_ = 0;
};

} // namespace game
