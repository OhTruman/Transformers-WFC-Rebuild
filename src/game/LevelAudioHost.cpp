#include "game/LevelAudioHost.h"
#include "core/Log.h"

#include <cstdlib>

namespace game {

bool LevelAudioHost::load(const std::string& level, const std::string& manifestPath) {
    if (!audio_) return false;
    if (!level_.empty()) unload();
    const std::string path = manifestPath.empty() ? root_ + "/Maps/" + level + "/audio.json" : manifestPath;
    // No wait for this level's prefetch: its worker decodes and the bank warm share files (a load waits for a file another
    // worker is decoding), and cues not decoded yet start when ready - a main-thread wait here was a menu hitch with banks.
    const bool ok = ambient_.load(path, root_ + "/../content/", cues_, audio_, level);
    cues_.releaseWarmExcept(level);                     // other prefetched levels that never loaded: no orphan samples
    for (const auto& pm : prefetchedMusic_) {          // ... and no music pinned for them forever
        if (pm.first != level) cues_.unpin(pm.second.c_str());
        else levelPinnedMusic_.push_back(pm.second);    // this level's: pinned until played or the level unloads
    }
    prefetchedMusic_.clear();
    level_ = ok ? level : std::string();
    match_.setAnnouncerEvents(ambient_.announcerEvents());
    return ok;
}

void LevelAudioHost::unload() {
    for (const std::string& m : levelPinnedMusic_) cues_.unpin(m.c_str());   // prefetched but never played: releasable
    levelPinnedMusic_.clear();
    music_.onOwnerDestroyed();                 // the level's WorldInfo music player goes with the level
    match_.setAnnouncerEvents({});             // ... and its announcer
    cues_.stopAll();                           // every instance (UI, Kismet, impacts...) - hard stop, queues dropped
    ambient_.unload(cues_);                    // bed, zones, pools, script, level cues + samples, presets; mixer Flush
    cues_.releaseIdleStreams();                // the level's music (streamed) is released now, not on the next tick
    if (audio_) audio_->setEnvironment(cues_.mixer().environment(), 0.0f);   // dry Default now, not on the next tick
    // The movie preset survives the Flush (UnflushableMixerPresets) [CONF config]: movie_ stays as it was.
    level_.clear();
}

// [HM_Engine.FmodAudioDevice] MovieMixerPreset=CINE_MUTE_FOR_BINK, enabled for the movie's lifetime [CONF config;
// HIGH: the native movie player's enable / disable points]; UnflushableMixerPresets keeps it through a level change.
void LevelAudioHost::setMoviePlaying(bool playing) {
    movieExplicit_ = playing;
    applyMovieMute();
}


bool LevelAudioHost::startMovieAudio(const std::string& path, int languageSlot) {
    stopMovieAudio();
    if (!audio_) return false;
    movieStream_ = true;
    applyMovieMute();
    if (languageSlot < 0) {
        const char* lang = std::getenv("WFC_LANGUAGE");                 // GLanguage (Language=int in Xe-TransEngine.ini)
        languageSlot = audio::movieLanguageSlot(lang ? lang : "INT");
        if (const char* s = std::getenv("WFC_MOVIE_LANGSLOT")) languageSlot = std::atoi(s);
    }
    std::unique_ptr<audio::MovieAudioPlayer> p(audio::createMovieAudioPlayer());
    if (!p->open(audio_, path, languageSlot)) {
        LOG_INFO("movie audio: %s has no audio tracks (silent by design)", path.c_str());
        movieStream_ = false;
        applyMovieMute();
        return false;
    }
    // MoviesToAlwaysPlaySound (the logos): fixed Bink volume 0xCCCC = 0.8; others GetMovieVolume = the SFX class
    // volume (setMovieSfxVolume; default FX 80 -> 0.8) [CONF native + script; HIGH device lookup].
    std::string name = path.substr(path.find_last_of("/\\") + 1);
    name = name.substr(0, name.find('.'));
    movieFixedVolume_ = SoundMixer::movieAlwaysPlaysSound(name);
    movieVolumeApplied_ = movieFixedVolume_ ? -1.0f : movieSfxVolume();
    p->setVolume(movieFixedVolume_ ? (float)0xCCCC / 65536.0f : movieVolumeApplied_);
    p->start();
    movieAudio_ = std::move(p);
    return true;
}


bool LevelAudioHost::prefetch(const std::string& level) {
    if (!audio_) return false;
    // The level's eager waves (frontend title: ~55 ms of decode at level start) and its music, both on workers.
    const int warmed = AmbientAudio::warmLevel(root_ + "/Maps/" + level + "/audio.json", root_ + "/../content/", cues_, level);
    MusicTrack t;
    const bool music = AmbientAudio::levelMusicTrack(level, t) && cues_.prefetch(t.cue.c_str());
    if (music) prefetchedMusic_.push_back({level, t.cue});
    return music || warmed > 0;
}

LevelAudioHost::State LevelAudioHost::state() const {
    State s;
    s.level = level_;
    s.reverb = cues_.mixer().currentReverb();
    s.mixer = cues_.mixer().activeList();
    s.music = music_.current().cue;
    s.musicState = (int)music_.state();
    s.musicInstance = music_.musicInstance();
    s.instances = (int)cues_.liveInstances();
    s.pending = (int)cues_.pendingEvents();
    s.levelCues = cues_.mapCueCount();
    s.levelPresets = cues_.mixer().mapPresetCount();
    s.voices = audio_ ? audio_->activeVoices() : -1;
    s.pcmMB = audio_ ? audio_->residentBytes() / 1048576.0 : 0.0;
    s.emitters = ambient_.activeEmitters();
    s.scriptSounds = ambient_.script().liveSounds();
    s.poolsPlaying = ambient_.script().poolsPlaying() + (ambient_.poolsRunning() ? 1 : 0);
    s.timelines = ambient_.script().timelinesPlaying();
    s.timelinePos = ambient_.script().timelinePosition();
    s.masterScale = cues_.mixer().masterScale();
    s.movie = movie_;
    s.movieAudio = movieAudio_ != nullptr;
    s.movieClock = movieAudioClock();
    s.streams = audio_ ? audio_->openStreams() : 0;
    return s;
}

} // namespace game
