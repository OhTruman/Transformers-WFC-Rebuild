#include "game/LevelAudioScript.h"
#include "game/SoundCues.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace game {
namespace {
constexpr float UU = 0.01f;
float frand() { return (float)std::rand() / (float)RAND_MAX; }
bool logOn() { static const bool on = std::getenv("WFC_LEVELAUDIOLOG") != nullptr; return on; }
} // namespace

int LevelAudioScript::load(const assets::Json& k) {
    unload();
    std::unordered_map<std::string, int> actorIndex, opIndex;
    for (const auto& kv : k["actors"].obj) {
        actorIndex[kv.first] = (int)actors_.size();
        actors_.push_back({kv.second[0].asFloat(), kv.second[1].asFloat(), kv.second[2].asFloat()});
    }
    const assets::Json& ops = k["ops"];
    for (size_t i = 0; i < ops.size(); ++i) {
        const assets::Json& j = ops[i];
        const std::string t = j["type"].asString();
        Op op;
        op.id = j["id"].asString();
        op.cue = j["cue"].asString();
        if (t == "play_sound") {
            op.type = Type::PlaySound;
            op.fadeIn = j["fade_in"].asFloat(0.0f);
            op.fadeOut = j["fade_out"].asFloat(0.0f);
            for (size_t a = 0; a < j["targets"].size(); ++a) {
                auto it = actorIndex.find(j["targets"][a].asString());
                if (it != actorIndex.end()) op.targets.push_back(it->second);
            }
            if (j["volume"].asFloat(1.0f) != 1.0f || j["pitch"].asFloat(1.0f) != 1.0f || j["suppress_spatialization"].asBool(false))
                LOG_WARN("level audio: %s volume / pitch multiplier / spatialization override not applied", op.id.c_str());
        } else if (t == "positional_pool") {
            op.type = Type::Pool;
            op.delayMin = j["delay_min"].asFloat(3.0f); op.delayMax = j["delay_max"].asFloat(5.0f);
            op.distMinM = j["distance_min"].asFloat(2000.0f) * UU; op.distMaxM = j["distance_max"].asFloat(2000.0f) * UU;
            op.looping = j["looping"].asBool(true);
            auto it = actorIndex.find(j["source"].asString());
            op.source = it != actorIndex.end() ? it->second : -1;
        } else if (t == "reverb") {
            op.type = Type::Reverb;
            op.preset = j["preset"].asString();
        } else if (t == "play_music") {
            op.type = Type::PlayMusic;
            op.track.cue = op.cue;
            op.track.fadeIn = j["fade_in"].asFloat(1.0f); op.track.fadeOut = j["fade_out"].asFloat(1.0f);
            op.track.boredom = j["boredom"].asFloat(0.0f); op.track.priority = j["priority"].asInt(0);
            op.ignoreSpaz = j["ignore_spaz_timer"].asBool(false);
            op.fadeOutOverride = j["fade_out_override"].asFloat(-1.0f);
        } else if (t == "stop_music") {
            op.type = Type::StopMusic;
            op.fadeOutOverride = j["fade_out_override"].asFloat(-1.0f);
        } else if (t == "timeline") {
            op.type = Type::Timeline;
            op.length = j["length"].asFloat(0.0f);
            op.rate = j["play_rate"].asFloat(1.0f);
            op.tlLooping = j["looping"].asBool(false);
            for (size_t e = 0; e < j["events"].size(); ++e)
                op.events.push_back({j["events"][e]["time"].asFloat(), "Timeline:" + op.id + ":" + j["events"][e]["name"].asString()});
            std::stable_sort(op.events.begin(), op.events.end(), [](const Event& a, const Event& b) { return a.time < b.time; });
        } else {
            LOG_WARN("level audio: op %s of unknown type %s skipped", op.id.c_str(), t.c_str());
            continue;
        }
        opIndex[op.id] = (int)ops_.size();
        ops_.push_back(op);
    }
    const assets::Json& links = k["links"];
    for (size_t i = 0; i < links.size(); ++i) {
        auto it = opIndex.find(links[i]["to"].asString());
        if (it == opIndex.end()) continue;
        const std::string from = links[i]["from"].asString();
        byTrigger_[from].push_back({it->second, links[i]["input"].asInt(0)});
        ++links_;
        if (musicStart_.empty() && ops_[(size_t)it->second].type == Type::PlayMusic && from != "GameplayStarted" &&
            from.rfind("Timeline:", 0) != 0)
            musicStart_ = from;                                      // the first external trigger that starts the music
    }
    return (int)ops_.size();
}

void LevelAudioScript::unload() {
    ops_.clear();
    actors_.clear();
    byTrigger_.clear();
    sounds_.clear();
    links_ = oneShots_ = fired_ = depth_ = 0;
    musicStart_.clear();
}

void LevelAudioScript::resetMatch() {
    for (Op& op : ops_) if (op.type == Type::Pool) op.playing = false;
}

core::Vec3 LevelAudioScript::actorPos(int a, const core::Vec3& listener) const {
    return a >= 0 && a < (int)actors_.size() ? actors_[(size_t)a] : listener;
}

int LevelAudioScript::fire(const std::string& trigger, SoundCues& cues, MusicPlayer* music, const core::Vec3& listener) {
    auto it = byTrigger_.find(trigger);
    if (it == byTrigger_.end()) return 0;
    if (++depth_ > 8) { --depth_; LOG_WARN("level audio: trigger depth exceeded at %s", trigger.c_str()); return 0; }
    ++fired_;
    if (logOn()) LOG_INFO("LEVELAUDIO fire %s (%zu inputs)", trigger.c_str(), it->second.size());
    const std::vector<std::pair<int, int>> targets = it->second;   // copy: an op may fire further triggers
    for (const auto& t : targets) input(ops_[(size_t)t.first], t.second, cues, music, listener);
    --depth_;
    return (int)targets.size();
}

void LevelAudioScript::input(Op& op, int idx, SoundCues& cues, MusicPlayer* music, const core::Vec3& listener) {
    switch (op.type) {
    case Type::PlaySound: {
        std::vector<int> src = op.targets;
        if (src.empty()) src.push_back(-1);
        for (int a : src) {
            if (idx == 0) {                                          // Kismet_ClientPlaySound
                const core::Vec3 at = actorPos(a, listener);
                const int inst = cues.play(op.cue.c_str(), at, core::length(at - listener));
                if (inst < 0) continue;
                if (op.fadeIn > 0.0f) cues.fadeIn(inst, op.fadeIn);
                sounds_.push_back({a, op.cue, inst, false});
                if (logOn()) LOG_INFO("LEVELAUDIO %s play %s on %d (fade-in %.2f)", op.id.c_str(), op.cue.c_str(), a, op.fadeIn);
            } else {                                                 // Kismet_ClientStopSound
                for (Sound& s : sounds_)
                    if (s.actor == a && s.cue == op.cue && !s.fading) {
                        cues.stop(s.instance, op.fadeOut);
                        s.fading = true;
                        if (logOn()) LOG_INFO("LEVELAUDIO %s stop %s on %d (fade-out %.2f)", op.id.c_str(), op.cue.c_str(), a, op.fadeOut);
                        break;
                    }
            }
        }
        break;
    }
    case Type::Pool:
        if (idx == 0) { op.remaining = op.delayMin + frand() * (op.delayMax - op.delayMin); op.playing = true; }
        else op.playing = false;
        break;
    case Type::Reverb:
        if (idx == 0) cues.mixer().activateReverb(op.preset);
        break;
    case Type::PlayMusic:
        if (idx == 0 && music) music->playMusic(op.track, op.ignoreSpaz, op.fadeOutOverride);
        break;
    case Type::StopMusic:
        if (idx == 0 && music) music->stopMusic(op.fadeOutOverride);
        break;
    case Type::Timeline:
        if (idx == 0 && op.rate != 0.0f) op.playing = true;          // Play: from the current position
        break;
    }
}

void LevelAudioScript::fireRange(const Op& tl, float from, float to, bool inclusiveEnd, SoundCues& cues, MusicPlayer* music,
                                 const core::Vec3& listener) {
    for (const Event& e : tl.events)
        if (e.time >= from && (e.time < to || (inclusiveEnd && e.time <= to))) fire(e.trigger, cues, music, listener);
}

void LevelAudioScript::tick(float dt, const core::Vec3& listener, SoundCues& cues, MusicPlayer* music) {
    // Timelines (event keys -> triggers).
    for (size_t i = 0; i < ops_.size(); ++i) {
        if (ops_[i].type != Type::Timeline || !ops_[i].playing) continue;
        const float old = ops_[i].pos, len = ops_[i].length;
        float now = old + dt * ops_[i].rate;
        if (now < len) { fireRange(ops_[i], old, now, false, cues, music, listener); ops_[i].pos = now; continue; }
        fireRange(ops_[i], old, len, true, cues, music, listener);   // up to the end (inclusive)
        if (!ops_[i].tlLooping || len <= 0.0f) { ops_[i].pos = len; ops_[i].playing = false; continue; }
        now = std::fmod(now - len, len);                              // wrap (a jump: no events), then [0, now)
        fireRange(ops_[i], 0.0f, now, false, cues, music, listener);
        ops_[i].pos = now;
    }
    // Positional pools.
    for (Op& op : ops_) {
        if (op.type != Type::Pool || !op.playing) continue;
        op.remaining -= dt;
        if (op.remaining > 0.0f) continue;
        op.remaining = op.delayMin + frand() * (op.delayMax - op.delayMin);
        const float ang = frand() * (359.0f / 360.0f) * 6.2831853f;
        const float d = op.distMinM + frand() * (op.distMaxM - op.distMinM);
        const core::Vec3 at = actorPos(op.source, listener) + core::Vec3{std::cos(ang) * d, 0.0f, std::sin(ang) * d};
        cues.play(op.cue.c_str(), at, core::length(at - listener));
        ++oneShots_;
        if (!op.looping) op.playing = false;
    }
    // Live components: drop finished ones; player-owned ones follow the player (the listener).
    for (size_t i = 0; i < sounds_.size();) {
        if (!cues.playing(sounds_[i].instance)) { sounds_.erase(sounds_.begin() + (long)i); continue; }   // keep creation order
        if (sounds_[i].actor < 0) cues.update(sounds_[i].instance, listener, 0.0f);
        ++i;
    }
}

int LevelAudioScript::poolsPlaying() const {
    int n = 0;
    for (const Op& op : ops_) n += op.type == Type::Pool && op.playing ? 1 : 0;
    return n;
}

int LevelAudioScript::timelinesPlaying() const {
    int n = 0;
    for (const Op& op : ops_) n += op.type == Type::Timeline && op.playing ? 1 : 0;
    return n;
}

float LevelAudioScript::timelinePosition() const {
    for (const Op& op : ops_) if (op.type == Type::Timeline) return op.pos;
    return -1.0f;
}

} // namespace game
