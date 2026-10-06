// Clean-room reconstruction — the Systems side of the frontend audio seam, standalone (no World): the frontend
// (agents/frontend FrontendRuntime, IFrontendAudio) runs boot / title / lobbies / loading without a match World and
// recreates the World per match, so the UI levels' audio needs its own owner. The method set matches
// frontend::IFrontendAudio one to one, so the integration adapter is a plain forward:
//     struct SystemsFrontendAudio : frontend::IFrontendAudio {
//         game::FrontendAudioRuntime rt;   // rt(audioDevice)
//         int playUiSound(const std::string& n) override { return rt.playUiSound(n); }   ... etc.
//     };
// Seam semantics (Systems decides what is authored; the frontend owns screen state and only reports it):
//   uiLevelStarted(level)  the level's audio start point: loads the level's manifests if that level is not loaded
//                          (SeqEvent_GameplayStarted ops run on the next tick - the lobbies' music, bed, pools), then
//                          fires the frontend-owned trigger that starts the level's music, if the level authors one
//                          (UI_FrontEnd_m [FRONTEND START]: FsCommand:enterFrontEnd - music, reveal, reverb, Camera
//                          Orbiter timeline). Calling it again for the loaded level does nothing new (the ops are
//                          idempotent: the music does not restart, the timeline keeps its position) except that a
//                          play_sound op would make a new component, so the trigger fires only once per load.
//   levelChange()          a travel: the level's audio goes (music player, Kismet sounds, beds, pools, reverb,
//                          level cues / samples), see LevelAudioHost::unload.
//   playUiSound / stopUiSound  GFx Sound.PlaySound / StopSound (FrontendAudio).
//   levelEvent(trigger)    any other frontend-owned Kismet trigger ("MovieStopped:FMV_intro", "FsCommand:<cmd>").
//   setMoviePlaying(b)     a Bink movie is up (MovieMixerPreset CINE_MUTE_FOR_BINK on the game mix).
//   startMovieAudio(path)  the movie's own Bink sound, started with its video (Systems M07); stopMovieAudio() at its
//                          end or on skip. movieAudioClock() is the sound's clock (the video may slave to it).
//   tick(dt)               audio-only frame (level audio, music player, mixer, cue instances, voices).
#pragma once
#include <memory>
#include <string>
#include "audio/Audio.h"
#include "core/Math.h"
#include "game/LevelAudioHost.h"
#include "game/SoundCues.h"

namespace game {

class FrontendAudioRuntime {
public:
    // Loads the global cue table on `a` (the UI sounds, music, built-in cues). `assetRoot` "" = WFC_ASSETS / the
    // configured ExtractedAssets/VerticalSlice root.
    explicit FrontendAudioRuntime(audio::IAudio* a, const std::string& assetRoot = std::string());
    ~FrontendAudioRuntime();

    int playUiSound(const std::string& name) { return host_->playUiSound(name.c_str()); }
    bool stopUiSound(const std::string& name, float fade) { return host_->stopUiSound(name.c_str(), fade); }
    void uiLevelStarted(const std::string& level);
    void levelChange() { host_->unload(); started_ = false; }
    int levelEvent(const std::string& trigger) { return host_->event(trigger, listener_); }
    void setMoviePlaying(bool playing) { host_->setMoviePlaying(playing); }
    bool startMovieAudio(const std::string& moviePath, int languageSlot = -1) { return host_->startMovieAudio(moviePath, languageSlot); }
    void setMovieSfxVolume(float v) { host_->setMovieSfxVolume(v); }   // the FX Volume option's class volume
    void setMovieFxSlider(int slider) { host_->setMovieFxSlider(slider); } // FX Volume option 0..100 (/ 100)
    // The options / profile Music, FX and Dialogue Volume (0..100): call at profile load and on every slider change.
    // Applies to all audio (frontend, game, movies) at once - see LevelAudioHost::applyProfileVolumes.
    void applyProfileVolumes(int musicSlider, int fxSlider, int dialogSlider) {
        LevelAudioHost::applyProfileVolumes(musicSlider, fxSlider, dialogSlider);
    }
    void stopMovieAudio() { host_->stopMovieAudio(); }
    void setMovieAudioPaused(bool paused) { host_->setMovieAudioPaused(paused); }
    double movieAudioClock() const { return host_->movieAudioClock(); }
    bool movieAudioFinished() const { return host_->movieAudioFinished(); }
    bool prefetchLevel(const std::string& level) { return host_->prefetch(level); }
    void setListener(const core::Vec3& p) { listener_ = p; }
    void tick(float dt);

    LevelAudioHost::State state() const { return host_->state(); }
    SoundCues& cues() { return *cues_; }
    LevelAudioHost& host() { return *host_; }

private:
    audio::IAudio* audio_;
    std::unique_ptr<SoundCues> cues_;
    std::unique_ptr<LevelAudioHost> host_;
    core::Vec3 listener_{0, 0, 0};
    bool started_ = false;            // the loaded level's start trigger has fired
};

} // namespace game
