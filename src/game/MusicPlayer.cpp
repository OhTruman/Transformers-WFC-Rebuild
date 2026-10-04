#include "game/MusicPlayer.h"
#include "core/Log.h"

#include <cstdlib>

namespace game {
namespace {
bool logOn() { static const bool on = std::getenv("WFC_MUSICLOG") != nullptr; return on; }
SoundCues::Emitter ui() { SoundCues::Emitter e; e.owner = SoundCues::kUI; return e; }
} // namespace

void MusicPlayer::playMusic(const MusicTrack& track, bool ignoreSpazTimer, float fadeOutOverride) {
    if (track.cue.empty()) return;                                    // PlayMusic: no SoundCue
    // QueueMusicTrack
    if (track.cue == current_.cue) {
        current_ = track; queued_ = track; fadeOutOverride_ = fadeOutOverride;
        state_ = State::Playing;                                      // already playing: no restart
        return;
    }
    if (track.priority < queued_.priority) return;
    if (track.priority == queued_.priority && track.cue == queued_.cue) return;
    queued_ = track;
    fadeOutOverride_ = fadeOutOverride;
    if (ignoreSpazTimer) spaz_ = 0.0f;
    state_ = State::Queued;
    if (logOn()) LOG_INFO("MUSIC queued %s (spaz %.2f s)", track.cue.c_str(), spaz_);
}

void MusicPlayer::stopMusic(float fadeOutOverride) {
    fadeOutOverride_ = fadeOutOverride;
    gotoStopped();
}

void MusicPlayer::gotoStopped() {
    stopMusicComponent();
    current_ = MusicTrack{};
    queued_ = MusicTrack{};
    state_ = State::Stopped;
}

void MusicPlayer::stopMusicComponent() {
    if (music_ < 0) return;
    const float t = fadeOutOverride_ < 0.0f ? current_.fadeOut : fadeOutOverride_;
    cues_.stop(music_, t);                                            // FadeOut(t, 0) + bAutoDestroy
    if (logOn()) LOG_INFO("MUSIC stop %s fade %.2f s", current_.cue.c_str(), t);
    music_ = -1;
}

void MusicPlayer::crossfade() {
    stopMusicComponent();
    music_ = cues_.play(queued_.cue.c_str(), ui(), 0.0f);
    if (music_ < 0) { gotoStopped(); return; }
    if (queued_.fadeIn > 0.0f) cues_.fadeIn(music_, queued_.fadeIn); // FadeIn(FadeInTime, 1.0)
    current_ = queued_;
    spaz_ = kSpazTime;
    boredom_ = 0.0f;
    state_ = State::Playing;
    if (logOn()) LOG_INFO("MUSIC play %s fade-in %.2f s", current_.cue.c_str(), current_.fadeIn);
}

void MusicPlayer::tick(float dt) {
    spaz_ -= dt;                                                      // TickInternal
    if (spaz_ <= 0.0f) spaz_ = 0.0f;
    if (state_ == State::Queued) {
        if (spaz_ <= 0.0f) crossfade();
    } else if (state_ == State::Playing && current_.boredom > 0.0f) {
        boredom_ += dt;
        if (boredom_ >= current_.boredom) crossfade();
    }
    if (music_ >= 0 && !cues_.playing(music_)) music_ = -1;           // the component finished on its own
    if (stinger_ >= 0 && !cues_.playing(stinger_)) stinger_ = -1;
}

bool MusicPlayer::playStinger(const char* cue) {
    const cuedata::CueDef* nd = cues_.cueDef(cue);
    if (!nd) return false;
    if (stinger_ >= 0 && cues_.playing(stinger_)) {                   // IsStingerHigherPriority
        const cuedata::CueDef* cd = cues_.instanceCue(stinger_);
        if (!cd || cd->priority >= nd->priority) return false;
        cues_.stop(stinger_, 0.1f);                                   // FadeOut(0.1, 0.0)
    }
    stinger_ = cues_.play(cue, ui(), 0.0f);
    return stinger_ >= 0;
}

void MusicPlayer::onOwnerDestroyed() {
    if (music_ >= 0) cues_.stop(music_, 0.0f);
    if (stinger_ >= 0) cues_.stop(stinger_, 0.0f);
    music_ = stinger_ = -1;
    current_ = MusicTrack{}; queued_ = MusicTrack{};
    fadeOutOverride_ = -1.0f; spaz_ = 0.0f; boredom_ = 0.0f;
    state_ = State::Stopped;
}

} // namespace game
