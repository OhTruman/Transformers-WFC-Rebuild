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
#include <utility>
#include <vector>
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
    // The preset (CINE_MUTE_FOR_BINK) is held while EITHER the caller says a movie is up OR a movie sound is playing
    // (startMovieAudio .. stopMovieAudio): the FmodAudioDevice applies it for as long as a Bink plays [CONF config:
    // MovieMixerPreset], whichever path started the movie. The intro chain's explicit flag keeps the game mix down
    // between its movies; a movie started without it (Extras -> Movies, a GFx script movie) releases the preset when
    // its sound stops, so the frontend music - ducked, still playing - comes back where it was (it is never restarted).
    void setMoviePlaying(bool playing);
    // The movie's own sound: opens the movie file's audio tracks and starts them now (call when its video starts);
    // also marks the movie as up. `languageSlot` < 0: the language's slot (movieLanguageSlot(GLanguage); WFC_LANGUAGE,
    // default INT; WFC_MOVIE_LANGSLOT overrides). False: the movie has no audio
    // (the loading Binks) - nothing plays, by design.
    bool startMovieAudio(const std::string& moviePath, int languageSlot = -1);
    // Movie end, skip or back: the movie sound stops at once; the movie preset is released unless the caller still
    // says a movie is up (setMoviePlaying(true), e.g. between the intro chain's movies).
    void stopMovieAudio() {                       // inline: World's destructor needs it in every build
        movieStream_ = false;
        applyMovieMute();
        if (!movieAudio_) return;
        movieAudio_->stop();
        movieAudio_.reset();
    }
    void setMovieAudioPaused(bool paused) { if (movieAudio_) movieAudio_->setPaused(paused); }
    bool movieAudioActive() const { return movieAudio_ != nullptr; }
    // GetMovieVolume (0x82CDDC08) [CONF native]: [MoviePlayer] VolumeScalar (absent -> 1.0) x the audio device's 'SFX'
    // class volume (the FX Volume option; Frontend owns it), clamped [0,1]; FullVolumeMovies (empty) skip the SFX
    // factor; MoviesToAlwaysPlaySound (the logos) override with 0xCCCC. The class volume is the FX slider / 100
    // (HmPlayerController.UpdateLocalCacheOfProfileSettings -> SetAudioGroupVolume('SFX', GetFxVolume()),
    // GetNormalizedPropertyValue = FClamp(slider / 100, 0, 1) [CONF script]; that the device's 'SFX' lookup returns it
    // unchanged is HIGH). Default: the profile default 80 -> 0.8. Applies to the next movie and the running one.
    // The class volume is the device's 'SFX' sound-group volume (SoundMixer::setGroupVolume); setting it here sets the
    // group (the game's SFX categories follow too). A running movie follows a change made anywhere (tick).
    void setMovieSfxVolume(float v) { SoundMixer::setGroupVolume("SFX", v); applyMovieVolume(); }
    float movieSfxVolume() const { return SoundMixer::groupVolume("SFX"); }
    void setMovieFxSlider(int slider) { setMovieSfxVolume((float)slider / 100.0f); }   // the options FX Volume, 0..100
    // The profile volume sliders (Music / FX / Dialogue Volume, 0..100), as HmPlayerController.
    // UpdateLocalCacheOfProfileSettings applies them: SetAudioGroupVolume('Dialog' | 'SFX' | 'MUSIC', FClamp(v / 100,
    // 0, 1)) [CONF script]. Device-global and immediate (playing sounds and a running movie follow).
    static void applyProfileVolumes(int musicSlider, int fxSlider, int dialogSlider) {
        SoundMixer::setGroupVolume("Dialog", (float)dialogSlider / 100.0f);
        SoundMixer::setGroupVolume("SFX", (float)fxSlider / 100.0f);
        SoundMixer::setGroupVolume("MUSIC", (float)musicSlider / 100.0f);
    }
    static bool setAudioGroupVolume(const std::string& group, float linear) { return SoundMixer::setGroupVolume(group, linear); }
    double movieAudioClock() const { return movieAudio_ ? movieAudio_->clock() : 0.0; }   // video can slave to it
    bool movieAudioFinished() const { return !movieAudio_ || movieAudio_->finished(); }
    bool prefetch(const std::string& level);
    void resetMatch() { ambient_.resetMatch(); }
    // Level audio (emitters, zones at `pawn`, pools, Kismet ops) + the music player. The caller ticks the cues.
    void tick(float dt, const core::Vec3& listener, const core::Vec3& pawn, bool pawnAlive = true) {
        if (!audio_) return;
        ambient_.tick(dt, listener, pawn, cues_, pawnAlive);
        match_.tick(dt);
        music_.tick(dt);
        if (movieAudio_ && !movieFixedVolume_ && movieVolumeApplied_ != movieSfxVolume()) applyMovieVolume();
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
    bool movie_ = false;          // the movie preset is enabled
    bool movieExplicit_ = false;  // setMoviePlaying
    bool movieStream_ = false;    // a movie sound is up
    void applyMovieMute() {       // inline: see stopMovieAudio
        const bool want = movieExplicit_ || movieStream_;
        if (want == movie_) return;
        movie_ = want;
        if (want) cues_.mixer().enable(SoundMixer::movieMixerPreset());
        else cues_.mixer().disable(SoundMixer::movieMixerPreset(), false);
    }
    std::unique_ptr<audio::MovieAudioPlayer> movieAudio_;
    float movieVolumeApplied_ = -1.0f;
    std::vector<std::pair<std::string, std::string>> prefetchedMusic_;   // (level, music cue) pinned by prefetch()
    std::vector<std::string> levelPinnedMusic_;        // the loaded level's prefetched music (unpinned at unload)
    void applyMovieVolume() {
        if (!movieAudio_ || movieFixedVolume_) return;
        movieVolumeApplied_ = movieSfxVolume();
        movieAudio_->setVolume(movieVolumeApplied_);
    }
    bool movieFixedVolume_ = false;
};

} // namespace game
