#include "game/FrontendAudioRuntime.h"
#include "core/Config.h"
#include "core/Log.h"

#include <cstdlib>

namespace game {

FrontendAudioRuntime::FrontendAudioRuntime(audio::IAudio* a, const std::string& assetRoot) : audio_(a) {
    std::string root = assetRoot;
    if (root.empty()) root = std::getenv("WFC_ASSETS") ? std::getenv("WFC_ASSETS") : core::config::kAssetRootDefault;
    cues_.reset(new SoundCues());
    host_.reset(new LevelAudioHost(*cues_));
    if (!a) return;
    cues_->load(a, root + "/../content/");
    host_->attach(a, root);
    float thr, att, rel, mk;
    if (SoundMixer::masterCompressor(thr, att, rel, mk)) a->setMasterCompressor(thr, att, rel, mk);
}

FrontendAudioRuntime::~FrontendAudioRuntime() {
    if (host_) { host_->stopMovieAudio(); host_->unload(); }
    if (cues_) cues_->stopAll();
}

void FrontendAudioRuntime::uiLevelStarted(const std::string& level) {
    if (!audio_) return;
    if (host_->level() != level) {
        if (!host_->load(level)) { LOG_WARN("frontend audio: no audio manifest for %s", level.c_str()); return; }
        started_ = false;
    }
    if (started_) return;
    started_ = true;
    const std::string& t = host_->ambient().script().musicStartTrigger();
    if (!t.empty()) host_->event(t, listener_);
}

void FrontendAudioRuntime::tick(float dt) {
    if (!audio_) return;
    cues_->setListener(listener_);
    host_->tick(dt, listener_, listener_);
    cues_->tick(dt);
}

} // namespace game
