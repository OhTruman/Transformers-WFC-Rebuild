#include "game/LevelAudioHost.h"
#include "core/Log.h"

namespace game {

bool LevelAudioHost::load(const std::string& level, const std::string& manifestPath) {
    if (!audio_) return false;
    if (!level_.empty()) unload();
    const std::string path = manifestPath.empty() ? root_ + "/Maps/" + level + "/audio.json" : manifestPath;
    const bool ok = ambient_.load(path, root_ + "/../content/", cues_, audio_, level);
    level_ = ok ? level : std::string();
    return ok;
}

void LevelAudioHost::unload() {
    music_.onOwnerDestroyed();                 // the level's WorldInfo music player goes with the level
    cues_.stopAll();                           // every instance (UI, Kismet, impacts...) - hard stop, queues dropped
    ambient_.unload(cues_);                    // bed, zones, pools, script, level cues + samples, presets; mixer Flush
    cues_.releaseIdleStreams();                // the level's music (streamed) is released now, not on the next tick
    if (audio_) audio_->setEnvironment(cues_.mixer().environment(), 0.0f);   // dry Default now, not on the next tick
    movie_ = false;                            // the Flush dropped the movie preset too [CONF Flush]
    level_.clear();
}

// Enabled for the movie's lifetime [HIGH: native movie player]. A level change (mixer Flush) during a movie drops it
// like any preset [CONF Flush]; whether the native player re-enables it afterwards is UNKNOWN.
void LevelAudioHost::setMoviePlaying(bool playing) {
    if (playing == movie_) return;
    movie_ = playing;
    if (playing) cues_.mixer().enable(kMovieMixerPreset);
    else cues_.mixer().disable(kMovieMixerPreset, false);
}

bool LevelAudioHost::prefetch(const std::string& level) {
    MusicTrack t;
    return audio_ && AmbientAudio::levelMusicTrack(level, t) && cues_.prefetch(t.cue.c_str());
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
    return s;
}

} // namespace game
