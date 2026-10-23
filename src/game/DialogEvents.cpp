// Clean-room reconstruction — see DialogEvents.h.
#include "game/DialogEvents.h"
#include "game/CharacterAudio.h"
#include "core/Log.h"

#include <algorithm>
#include <cstdlib>

namespace game {

namespace {
bool logOn() { static const bool on = std::getenv("WFC_DIALOGLOG") != nullptr; return on; }
const std::string kNone;
}

void DialogEvents::setLevel(const std::string& level, SoundCues& cues) {
    level_ = level;
    groups_.clear();
    const int n = CharacterAudio::loadDialogCues(cues, level);
    if (logOn()) LOG_INFO("DIALOG level %s: %d level-set cues loaded", level.c_str(), n);
}

const std::string& DialogEvents::findCue(const CharacterAudioProfile& who, const std::string& event) const {
    const assets::Json& d = CharacterAudio::dialogData();
    const std::string& set = d["level_sets"][level_][who.robodef].asString();   // 1. the level's character set
    if (!set.empty()) {
        const assets::Json& c = d["sets"][set][event];
        if (c.isString() && !c.asString().empty()) return c.asString();
    }
    auto v = who.voice.find(event);                                             // 2. the robot form's set
    if (v != who.voice.end() && !v->second.empty()) return v->second;
    auto h = who.vehicle.find(event);                                           // 3. the vehicle form's set
    if (h != who.vehicle.end() && !h->second.empty()) return h->second;
    return kNone;
}

DialogEvents::Group& DialogEvents::group(const std::string& name) {
    Group& g = groups_[name];
    if (!g.started) {   // GetInitialDelay: 0 when RandomInt(100) < InitialChanceToPlay, else a retrigger delay
        g.started = true;
        const int chance = CharacterAudio::dialogData()["groups"][name]["chance"].asInt(100);
        g.timer = (int)(rng_() % 100u) < chance ? 0.0f : retriggerDelay(name);
    }
    return g;
}

float DialogEvents::retriggerDelay(const std::string& name) {
    const assets::Json& j = CharacterAudio::dialogData()["groups"][name];
    const float a = j["retrigger_min"].asFloat(0.0f), b = j["retrigger_max"].asFloat(0.0f);
    if (b <= a) return a;
    return a + (b - a) * (float)(rng_() % 10000u) / 10000.0f;
}

int DialogEvents::play(const CharacterAudioProfile& who, int owner, const std::string& event, SoundCues& cues,
                       const SoundCues::Emitter& at, float distM) {
    const assets::Json& d = CharacterAudio::dialogData();
    const assets::Json& ev = d["events"][event];
    std::string first = ev["group"].asString();
    if (first.empty()) first = "SoundEvents_Dialog.DefaultDialogGroup";   // the manager's DefaultDialogGroup (every level's)
    const float priority = ev["priority"].asFloat(0.0f);
    std::vector<std::string> chain;
    for (std::string g = first; !g.empty() && chain.size() < 8; g = d["groups"][g]["parent"].asString()) chain.push_back(g);
    // CanPlayDialog: every group of the chain; preemption victims are only stopped once the whole chain allows the event.
    std::vector<std::pair<std::string, size_t>> evict;
    for (const std::string& name : chain) {
        Group& g = group(name);
        for (size_t i = 0; i < g.playing.size();)   // drop the finished instances first
            if (!cues.playing(g.playing[i].instance)) g.playing.erase(g.playing.begin() + (long)i); else ++i;
        if (g.timer > 0.0f) { if (logOn()) LOG_INFO("DIALOG %s refused: %s retrigger %.2f s", event.c_str(), name.c_str(), g.timer); return -1; }
        const int limit = d["groups"][name]["limit"].asInt(1);
        if (limit != 0 && (int)g.playing.size() >= limit) {
            size_t low = 0;
            for (size_t i = 1; i < g.playing.size(); ++i) if (g.playing[i].priority < g.playing[low].priority) low = i;
            if (!(priority > g.playing[low].priority)) { if (logOn()) LOG_INFO("DIALOG %s refused: %s full", event.c_str(), name.c_str()); return -1; }
            evict.push_back({name, low});
        }
    }
    const std::string& cue = findCue(who, event);
    if (cue.empty()) { if (logOn()) LOG_INFO("DIALOG %s: no cue for %s", event.c_str(), who.key.c_str()); return -1; }
    for (const auto& e : evict) {   // StopDialog on the preempted instance (it leaves every group it was in)
        const int victim = groups_[e.first].playing[e.second].instance;
        cues.stop(victim, 0.0f);
        for (auto& kv : groups_)
            kv.second.playing.erase(std::remove_if(kv.second.playing.begin(), kv.second.playing.end(),
                                                   [victim](const Inst& x) { return x.instance == victim; }), kv.second.playing.end());
    }
    const int inst = cues.play(cue.c_str(), at, distM);
    if (inst < 0) return -1;
    for (const std::string& name : chain) {   // PlayInternal
        Group& g = groups_[name];
        g.playing.push_back({inst, priority, owner});
        const int limit = d["groups"][name]["limit"].asInt(1);
        if (limit == 0 || (int)g.playing.size() >= limit) g.timer = retriggerDelay(name);
    }
    if (logOn()) LOG_INFO("DIALOG %s by %s (owner %d): %s, priority %.0f, groups %zu", event.c_str(), who.key.c_str(), owner, cue.c_str(), priority, chain.size());
    return inst;
}

void DialogEvents::tick(float dt, SoundCues& cues) {
    for (auto& kv : groups_) {
        if (kv.second.timer > 0.0f) kv.second.timer = std::max(0.0f, kv.second.timer - dt);
        std::vector<Inst>& p = kv.second.playing;
        p.erase(std::remove_if(p.begin(), p.end(), [&cues](const Inst& x) { return !cues.playing(x.instance); }), p.end());
    }
}

} // namespace game
