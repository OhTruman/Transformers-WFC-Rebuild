// Clean-room reconstruction — the level-scoped Systems audio: what a loaded level owns and how it goes away.
// World owns one and exposes it as the frontend / integration audio contract (World.h "SYSTEMS AUDIO CONTRACT");
// the validation suite drives the same class directly.
//   * load(level): the level's manifests (AmbientAudio: AssetTools <assets>/Maps/<level>/audio.json and / or the
//     compiled-in Systems level manifest) - emitters, zones, pools, reverb presets, Kismet audio ops, cue bank.
//   * the level's WorldInfo music player (HmMusicPlayer), destroyed with the level [HIGH: WorldInfo-owned,
//     bStopWhenOwnerDestroyed].
//   * GFx UI sounds (FrontendAudio), the MovieMixerPreset, streamed-music prefetch.
// unload(): the music player stops at once, every cue instance and queued event stops, the level's cues / samples /
// presets are released, the mixer flushes (Default only, reverb None) and the backend environment goes dry.
#pragma once
#include <string>
#include "audio/Audio.h"
#include "core/Math.h"
#include "game/AmbientAudio.h"
#include "game/FrontendAudio.h"
#include "game/MusicPlayer.h"
#include "game/SoundCues.h"

namespace game {

class LevelAudioHost {
public:
    // Engine.MovieSettings MovieMixerPreset (Xe-TransEngine.ini) [CONF].
    static constexpr const char* kMovieMixerPreset = "CINE_MUTE_FOR_BINK";

    explicit LevelAudioHost(SoundCues& cues) : cues_(cues), music_(cues), frontend_(cues, music_) {
        ambient_.setMusicPlayer(&music_);
    }
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
    void setMoviePlaying(bool playing);
    bool prefetch(const std::string& level);
    void resetMatch() { ambient_.resetMatch(); }
    // Level audio (emitters, zones at `pawn`, pools, Kismet ops) + the music player. The caller ticks the cues.
    void tick(float dt, const core::Vec3& listener, const core::Vec3& pawn) {
        if (!audio_) return;
        ambient_.tick(dt, listener, pawn, cues_);
        music_.tick(dt);
    }

    struct State {
        std::string level, reverb, mixer, music;
        int instances = 0, pending = 0, levelCues = 0, levelPresets = 0, voices = -1, emitters = 0;
        int scriptSounds = 0, poolsPlaying = 0, timelines = 0, musicState = 0, musicInstance = -1;
        float timelinePos = -1.0f, masterScale = 1.0f;
        double pcmMB = 0.0;
        bool movie = false;
    };
    State state() const;

    const std::string& level() const { return level_; }
    AmbientAudio& ambient() { return ambient_; }
    const AmbientAudio& ambient() const { return ambient_; }
    MusicPlayer& music() { return music_; }
    FrontendAudio& frontend() { return frontend_; }

private:
    SoundCues& cues_;
    MusicPlayer music_;
    FrontendAudio frontend_;
    AmbientAudio ambient_;
    audio::IAudio* audio_ = nullptr;
    std::string root_, level_;
    bool movie_ = false;
};

} // namespace game
