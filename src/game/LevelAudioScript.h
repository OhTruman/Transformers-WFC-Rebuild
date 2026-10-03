// Clean-room reconstruction — a level's Kismet AUDIO ops, driven by the level manifest's "kismet" section
// (tools/systems/gen_level_audio.py from the cooked level packages in AssetTools authored.db, read-only). Generic:
// UI_FrontEnd_m, the lobbies and any later level load through the same path (AmbientAudio / World::loadMapAudio).
//
// Ops and their recovered semantics:
//   play_sound       SeqAct_PlaySound. Play (input 0): for every Target (no Target: the local PlayerController)
//                    PC.Kismet_ClientPlaySound [CONF script, Engine.PlayerController]: SourceActor.CreateAudioComponent
//                    (cue, bPlay false, bStopWhenOwnerDestroyed true), VolumeMultiplier / PitchMultiplier,
//                    bAutoDestroy, FadeIn(FadeInTime, 1.0). Every Play makes a NEW component (no de-duplication).
//                    Stop (input 1): PC.Kismet_ClientStopSound [CONF script]: the FIRST component on the source actor
//                    playing that cue and not already fading -> FadeOut(FadeOutTime, 0.0) (0 = stop at once).
//                    [HIGH] no Target -> the player (stock SeqAct_PlaySound.Activated is native; the authored no-target
//                    cues are all 2D, so the source position does not change what is heard).
//   positional_pool  SeqAct_PlayPlayerPositionalSound [CONF script, HM_Engine]: Play -> DelayRemaining =
//                    RandRange(DelayMin, DelayMax), IsPlaying; Stop -> IsPlaying false; Update: the delay counts down,
//                    then a world one-shot at CalculatePosition (random yaw 0..359 deg, RandRange(DistanceMin, Max)
//                    around the "Source Actor", else Listeners[0]) and a new delay; Looping false -> done after one.
//                    Reset (Kismet reset) -> IsPlaying false.
//   reverb           SeqAct_Reverb -> SoundMixer::activateReverb [CONF native, RE A1].
//   play_music       SeqAct_PlayMusic -> the level's MusicPlayer (HmMusicPlayer) [CONF script].
//   stop_music       SeqAct_StopMusic -> MusicPlayer::stopMusic [CONF script].
//   timeline         SeqAct_Interp whose event track drives audio (UI_FrontEnd_m "Camera Orbiter", 393.55 s,
//                    looping). Play starts it at its current position (bRewindOnPlay false; already playing -> no
//                    change). Event keys fire when the position passes them, forward range [old, new); a loop wrap
//                    fires the rest up to the end, then [0, new) again, so a key at 0 refires every loop [HIGH:
//                    stock UE3 InterpTrackEvent / SeqAct_Interp; authored times and PlayRate CONF]. The rebuild runs
//                    the timeline's clock itself (client-side matinee started by the same trigger; the frontend owns
//                    the camera it moves).
// Triggers ("links" from): GameplayStarted (fired by AmbientAudio on the level's first tick), FsCommand:<cmd> and
// MovieStopped:<movie> (GFx fscommands / Bink movie ends - the frontend owner forwards them, fire()), and
// Timeline:<op>:<event> (internal). A sub-sequence's Start / Stop inputs are already flattened by the generator.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "assets/Json.h"
#include "core/Math.h"
#include "game/MusicPlayer.h"

namespace game {

class SoundCues;

class LevelAudioScript {
public:
    // Loads a manifest "kismet" object; returns the op count. Replaces any previous script.
    int load(const assets::Json& kismet);
    void unload();                        // forget (the caller stops / unloads the cue instances)
    void resetMatch();                    // Kismet Reset: positional pools IsPlaying false (sounds keep playing)
    // An authored trigger (see above). Returns the number of op inputs it reached (0 = unknown trigger).
    int fire(const std::string& trigger, SoundCues& cues, MusicPlayer* music, const core::Vec3& listener);
    void tick(float dt, const core::Vec3& listener, SoundCues& cues, MusicPlayer* music);

    // Diagnostics.
    int opCount() const { return (int)ops_.size(); }
    int linkCount() const { return links_; }
    bool hasTrigger(const std::string& t) const { return byTrigger_.count(t) != 0; }
    // The first frontend-owned trigger (not GameplayStarted / a timeline event) that reaches a play_music op, in
    // authored link order (UI_FrontEnd_m: FsCommand:enterFrontEnd); "" if the music starts at level start or none.
    const std::string& musicStartTrigger() const { return musicStart_; }
    int liveSounds() const { return (int)sounds_.size(); }
    int poolsPlaying() const;
    int timelinesPlaying() const;
    float timelinePosition() const;       // the first timeline's position (s), -1 if none
    int oneShots() const { return oneShots_; }
    int fired() const { return fired_; }

private:
    enum class Type { PlaySound, Pool, Reverb, PlayMusic, StopMusic, Timeline };
    struct Event { float time; std::string trigger; };
    struct Op {
        Type type = Type::PlaySound;
        std::string id, cue, preset;
        float fadeIn = 0.0f, fadeOut = 0.0f;
        std::vector<int> targets;         // actor indices; empty = the player
        // positional_pool
        float delayMin = 0, delayMax = 0, distMinM = 0, distMaxM = 0;
        bool looping = true;
        int source = -1;                  // actor index, -1 = the listener
        bool playing = false;
        float remaining = 0.0f;
        // music
        MusicTrack track;
        bool ignoreSpaz = false;
        float fadeOutOverride = -1.0f;
        // timeline
        float length = 0.0f, rate = 1.0f, pos = 0.0f;
        bool tlLooping = false;
        std::vector<Event> events;
    };
    struct Sound { int actor; std::string cue; int instance; bool fading; };   // -1 actor = the player

    void input(Op& op, int idx, SoundCues& cues, MusicPlayer* music, const core::Vec3& listener);
    void fireRange(const Op& tl, float from, float to, bool inclusiveEnd, SoundCues& cues, MusicPlayer* music,
                   const core::Vec3& listener);
    core::Vec3 actorPos(int a, const core::Vec3& listener) const;

    std::vector<Op> ops_;
    std::vector<core::Vec3> actors_;
    std::unordered_map<std::string, std::vector<std::pair<int, int>>> byTrigger_;   // trigger -> (op, input)
    std::vector<Sound> sounds_;
    int links_ = 0, oneShots_ = 0, fired_ = 0, depth_ = 0;
    std::string musicStart_;
};

} // namespace game
