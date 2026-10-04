// Clean-room reconstruction — HmMusicPlayer (HM_Engine), the WorldInfo-owned music player that SeqAct_PlayMusic /
// SeqAct_StopMusic drive. Ported from the decompiled script (RE-Workspace work/script/decomp
// HM_Engine.HmMusicPlayer / SeqAct_PlayMusic / SeqAct_StopMusic, read-only) [CONF] and the class defaults in
// AssetTools authored.db (HM_Engine.Default__HmMusicPlayer: SpazTime 5.0; MusicTrack defaults FadeIn 1, FadeOut 1,
// BoredomTime 0, Priority 0) [CONF]:
//   PlayMusic(track): no cue -> nothing; QueueMusicTrack:
//     same cue as the current track -> current = queued = track, FadeOutTimeOverride, state Playing (no restart);
//     track.Priority < queued.Priority -> ignored; same priority AND same cue as queued -> ignored;
//     else queued = track, FadeOutTimeOverride, IgnoreSpazTimer -> SpazTimer = 0, state Queued.
//   Queued: SpazTimer counts down (clamped at 0); at 0 -> Crossfading.
//   Crossfading.BeginState: StopMusicComponent; new component (queued cue, bPlay false) FadeIn(queued.FadeInTime);
//     current = queued; SpazTimer = SpazTime; BoredomTimer = 0; -> Playing.
//   Playing: SpazTimer counts down; with BoredomTime > 0 the boredom timer re-crossfades to the queued track.
//   StopMusicComponent: FadeOut(override >= 0 ? override : current.FadeOutTime) + auto-destroy.
//   StopMusic(fade): FadeOutTimeOverride = fade; state Stopped (BeginState: StopMusicComponent; tracks reset).
//   PlayStinger(cue): refused unless no stinger plays or its root Priority is lower than the new cue's; the old
//     stinger FadeOut(0.1); new stinger component (bPlay true).
// Components are 2D (SoundCues kUI owner); SoloTrack (index -1 by default) is not used.
// [HIGH] Level change: the player's owner (WorldInfo) is destroyed with the level, and its components
// (bStopWhenOwnerDestroyed) stop at once: onOwnerDestroyed().
// Replication (ReplicatePlay / ServerPlayMusic) is not modelled: single local player.
#pragma once
#include <string>
#include "game/SoundCues.h"

namespace game {

struct MusicTrack {
    std::string cue;              // SoundCue ("" = none)
    float fadeIn = 1.0f, fadeOut = 1.0f, boredom = 0.0f;
    int priority = 0;
};

class MusicPlayer {
public:
    enum class State { Stopped, Queued, Playing };
    static constexpr float kSpazTime = 5.0f;   // HM_Engine.Default__HmMusicPlayer.SpazTime [CONF]

    explicit MusicPlayer(SoundCues& cues) : cues_(cues) {}

    // SeqAct_PlayMusic: (UseCurrentTrackFadeOutTimeOverride ? CurrentTrackFadeOutTimeOverride : -1).
    void playMusic(const MusicTrack& track, bool ignoreSpazTimer = false, float fadeOutOverride = -1.0f);
    // SeqAct_StopMusic: (UseFadeTimeOverride ? FadeTimeOverride : -1).
    void stopMusic(float fadeOutOverride = -1.0f);
    bool playStinger(const char* cue);
    void tick(float dt);
    void onOwnerDestroyed();      // level change: music + stinger stop at once, state Stopped

    // Diagnostics.
    State state() const { return state_; }
    const MusicTrack& current() const { return current_; }
    const MusicTrack& queued() const { return queued_; }
    int musicInstance() const { return music_; }
    int stingerInstance() const { return stinger_; }
    float spazTimer() const { return spaz_; }

private:
    void gotoStopped();
    void crossfade();             // Crossfading.BeginState (-> Playing)
    void stopMusicComponent();

    SoundCues& cues_;
    State state_ = State::Stopped;
    MusicTrack current_, queued_;
    float fadeOutOverride_ = -1.0f;
    float spaz_ = 0.0f, boredom_ = 0.0f;
    int music_ = -1, stinger_ = -1;
};

} // namespace game
