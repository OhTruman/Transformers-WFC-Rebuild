// Clean-room reconstruction — the level-scoped Systems audio: what a loaded level owns and how it goes away.
// World owns one and exposes it as the frontend / integration audio contract (World.h "SYSTEMS AUDIO CONTRACT");
// the validation suite drives the same class directly.
//   * load(level): the level's manifests (AmbientAudio: AssetTools <assets>/Maps/<level>/audio.json and / or the
//     compiled-in Systems level manifest) - emitters, zones, pools, reverb presets, Kismet audio ops, cue bank.
//   * the level's WorldInfo music player (HmMusicPlayer), destroyed with the level [HIGH: WorldInfo-owned,
//     bStopWhenOwnerDestroyed].
//   * GFx UI sounds (FrontendAudio), the MovieMixerPreset, streamed-music prefetch.
// unload(): the music player stops at once, every cue instance and queued event stops, the level's cues / samples /
// presets are released, the mixer flushes (Default only, reverb None; the unflushable movie preset stays) and the
// backend environment goes dry.
//   * Full-screen movie audio (Systems M07): the movie's own Bink sound (audio::MovieAudioPlayer) streamed beside the
//     game mix; the game mix itself is muted by the MovieMixerPreset while a movie is up. A movie is not level-owned:
//     unload() does not stop it (no shipped movie spans a travel; the loading Binks have no sound).
#pragma once
#include <memory>
#include <string>
#include "audio/Audio.h"
#include "audio/MovieAudio.h"
#include "core/Math.h"
#include "game/AmbientAudio.h"
#include "game/FrontendAudio.h"
#include "game/MatchAudio.h"
#include "game/MusicPlayer.h"
#include "game/SoundCues.h"

namespace game {

class LevelAudioHost {
public:
    // Engine.MovieSettings MovieMixerPreset (Xe-TransEngine.ini) [CONF].

    explicit LevelAudioHost(SoundCues& cues) : cues_(cues), music_(cues), frontend_(cues, music_), match_(cues, music_) {
        ambient_.setMusicPlayer(&music_);
    }
    ~LevelAudioHost() { stopMovieAudio(); }
    // `assetRoot` = the ExtractedAssets level root (<root>/Maps/<level>/audio.json, <root>/../content/ waves).
    void attach(audio::IAudio* a, const std::string& assetRoot) { audio_ = a; root_ = assetRoot; }

    // Unloads any loaded level first. `manifestPath` overrides <root>/Maps/<level>/audio.json (tools / tests).
    bool load(const std::string& level, const std::string& manifestPath = std::string());
    void unload();
    int event(const std::string& trigger, const core::Vec3& listener) {
        return audio_ ? ambient_.fireEvent(trigger, cues_, listener) : 0;
    }
    int playUiSound(const char* name) { return frontend_.playUiSound(name); }
    bool stopUiSound(const char* name, float fade) { return frontend_.stopUiSound(name, fade); }
    // A Bink movie is up (the intro chain, a loading underlay): MovieMixerPreset on the game mix.
    void setMoviePlaying(bool playing);
    // The movie's own sound: opens the movie file's audio tracks and starts them now (call when its video starts);
    // also marks the movie as up. `languageSlot` < 0: WFC_MOVIE_LANGSLOT, else 0. False: the movie has no audio
    // (the loading Binks) - nothing plays, by design.
    bool startMovieAudio(const std::string& moviePath, int languageSlot = -1);
    // Movie end or skip: the movie sound stops at once. The game-mix mute stays until setMoviePlaying(false) (a
    // chain of movies keeps the game mix down between them).
    void stopMovieAudio() {                       // inline: World's destructor needs it in every build
        if (!movieAudio_) return;
        movieAudio_->stop();
        movieAudio_.reset();
    }
    void setMovieAudioPaused(bool paused) { if (movieAudio_) movieAudio_->setPaused(paused); }
    bool movieAudioActive() const { return movieAudio_ != nullptr; }
    double movieAudioClock() const { return movieAudio_ ? movieAudio_->clock() : 0.0; }   // video can slave to it
    bool movieAudioFinished() const { return !movieAudio_ || movieAudio_->finished(); }
    bool prefetch(const std::string& level);
    void resetMatch() { ambient_.resetMatch(); }
    // Level audio (emitters, zones at `pawn`, pools, Kismet ops) + the music player. The caller ticks the cues.
    void tick(float dt, const core::Vec3& listener, const core::Vec3& pawn) {
        if (!audio_) return;
        ambient_.tick(dt, listener, pawn, cues_);
        match_.tick(dt);
        music_.tick(dt);
    }

    struct State {
        std::string level, reverb, mixer, music;
        int instances = 0, pending = 0, levelCues = 0, levelPresets = 0, voices = -1, emitters = 0;
        int scriptSounds = 0, poolsPlaying = 0, timelines = 0, musicState = 0, musicInstance = -1;
        float timelinePos = -1.0f, masterScale = 1.0f;
        double pcmMB = 0.0, movieClock = 0.0;
        bool movie = false, movieAudio = false;
        int streams = 0;
    };
    State state() const;

    const std::string& level() const { return level_; }
    AmbientAudio& ambient() { return ambient_; }
    const AmbientAudio& ambient() const { return ambient_; }
    MusicPlayer& music() { return music_; }
    FrontendAudio& frontend() { return frontend_; }
    MatchAudio& match() { return match_; }            // the level's announcer + match messages (MP maps)

private:
    SoundCues& cues_;
    MusicPlayer music_;
    FrontendAudio frontend_;
    MatchAudio match_;
    AmbientAudio ambient_;
    audio::IAudio* audio_ = nullptr;
    std::string root_, level_;
    bool movie_ = false;
    std::unique_ptr<audio::MovieAudioPlayer> movieAudio_;
};

} // namespace game
