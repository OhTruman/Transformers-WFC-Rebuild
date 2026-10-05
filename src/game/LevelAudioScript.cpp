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
            op.playerSource = j["source"].asString() == "<player>";
        } else if (t == "reverb") {
            op.type = Type::Reverb;
            op.preset = j["preset"].asString();
        } else if (t == "mixer") {
            op.type = Type::Mixer;
            op.preset = j["preset"].asString();
        } else if (t == "flyby") {
            op.type = Type::Flyby;
            op.delayMin = j["delay_min"].asFloat(3.0f); op.delayMax = j["delay_max"].asFloat(5.0f);
            op.angleMax = j["angle_max"].asFloat(359.0f);
            op.startMin = j["start_distance_min"].asFloat(2000.0f); op.startMax = j["start_distance_max"].asFloat(2000.0f);
            op.speedMin = j["speed_min"].asFloat(1000.0f); op.speedMax = j["speed_max"].asFloat(2000.0f);
            op.headMin = j["head_offset_min"].asFloat(-100.0f); op.headMax = j["head_offset_max"].asFloat(100.0f);
            op.looping = j["looping"].asBool(true);
            op.playerSource = true;                                  // Target: the player (all authored flybys)
        } else if (t == "zone") {
            op.type = Type::Zone;
            op.label = j["label"].asString().empty() ? op.id : j["label"].asString();
        } else if (t == "delay") {
            op.type = Type::Delay;
            op.duration = j["duration"].asFloat(1.0f);
        } else if (t == "gate") {
            op.type = Type::Gate;
            op.open = j["open"].asBool(true);
        } else if (t == "touch") {
            op.type = Type::Touch;
            op.maxTrigger = j["max_trigger"].asInt(0);
            op.retrigger = j["retrigger_delay"].asFloat(0.0f);
            op.bmin = {1e30f, 1e30f, 1e30f}; op.bmax = {-1e30f, -1e30f, -1e30f};
            const assets::Json& polys = j["polygons"];
            for (size_t p = 0; p < polys.size(); ++p)
                for (size_t v = 1; v + 1 < polys[p].size(); ++v) {           // fan-triangulate each planar face
                    const assets::Json* pts[3] = {&polys[p][0], &polys[p][v], &polys[p][v + 1]};
                    for (const assets::Json* q : pts) {
                        core::Vec3 x{(*q)[0].asFloat(), (*q)[1].asFloat(), (*q)[2].asFloat()};
                        op.tris.push_back(x);
                        op.bmin = {std::min(op.bmin.x, x.x), std::min(op.bmin.y, x.y), std::min(op.bmin.z, x.z)};
                        op.bmax = {std::max(op.bmax.x, x.x), std::max(op.bmax.y, x.y), std::max(op.bmax.z, x.z)};
                    }
                }
            ++touches_;
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
    flybys_.clear();
    currentZone_.clear();
    currentZoneLabel_.clear();
    havePawn_ = false;
    touches_ = 0;
    links_ = oneShots_ = fired_ = depth_ = 0;
    musicStart_.clear();
}

// GameInfo.ResetLevel -> Kismet Reset [HIGH]: pools / flybys stop (IsPlaying false / Stop), zones forget their scene
// (IsEntered false, SceneIndexCurrent -1, no output), delays rearm; touch state is the pawn's (a respawn re-touches).
void LevelAudioScript::resetMatch() {
    for (Op& op : ops_) {
        if (op.type == Type::Pool || op.type == Type::Flyby || op.type == Type::Delay) op.playing = false;
        if (op.type == Type::Zone) { op.entered = false; op.sceneCurrent = -1; op.sceneNext = 0; }
        if (op.type == Type::Touch) { op.inside = false; op.triggered = 0; }
    }
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
    case Type::Mixer:
        if (idx == 0) cues.mixer().enable(op.preset);
        else if (idx == 1) cues.mixer().disable(op.preset, false);
        break;
    case Type::Flyby:
        if (idx == 0) { op.remaining = op.delayMin + frand() * (op.delayMax - op.delayMin); op.playing = true; }
        else op.playing = false;
        break;
    case Type::Zone:
        if (idx == 0) {                                              // Enter
            if (currentZone_ != op.id) {
                for (Op& z : ops_) if (z.type == Type::Zone && z.id == currentZone_) z.entered = false;
                currentZone_ = op.id;
                currentZoneLabel_ = op.label;
            }
            op.entered = true;
        } else op.sceneNext = idx - 1;                               // Scene k
        break;
    case Type::Delay:
        if (idx == 0) { op.remaining = op.duration; op.playing = true; op.paused = false; }
        else if (idx == 1) { if (op.playing) { op.playing = false; out(op, "Aborted", cues, music, listener); } }
        else if (idx == 2) op.paused = !op.paused;
        break;
    case Type::Gate:
        if (idx == 0) { if (op.open) out(op, "Out", cues, music, listener); }
        else if (idx == 1) op.open = true;
        else if (idx == 2) op.open = false;
        else if (idx == 3) op.open = !op.open;
        break;
    case Type::Touch:
        break;
    }
}

void LevelAudioScript::out(const Op& op, const char* output, SoundCues& cues, MusicPlayer* music, const core::Vec3& listener) {
    fire("Out:" + op.id + ":" + output, cues, music, listener);
}

namespace {
bool rayTri(const core::Vec3& o, const core::Vec3& d, const core::Vec3& a, const core::Vec3& b, const core::Vec3& c) {
    core::Vec3 e1 = b - a, e2 = c - a, p = core::cross(d, e2);
    float det = core::dot(e1, p);
    if (std::fabs(det) < 1e-9f) return false;
    float inv = 1.0f / det;
    core::Vec3 tv = o - a;
    float u = core::dot(tv, p) * inv;
    if (u < 0 || u > 1) return false;
    core::Vec3 q = core::cross(tv, e1);
    float v = core::dot(d, q) * inv;
    if (v < 0 || u + v > 1) return false;
    return core::dot(e2, q) * inv > 0.0f;
}
} // namespace

bool LevelAudioScript::insideVolume(const Op& op, const core::Vec3& p) const {
    if (p.x < op.bmin.x || p.y < op.bmin.y || p.z < op.bmin.z || p.x > op.bmax.x || p.y > op.bmax.y || p.z > op.bmax.z)
        return false;
    const core::Vec3 d = core::normalize(core::Vec3{1.0f, 0.0137f, 0.0291f});   // ray parity (non-convex brushes)
    int hits = 0;
    for (size_t i = 0; i + 2 < op.tris.size(); i += 3)
        if (rayTri(p, d, op.tris[i], op.tris[i + 1], op.tris[i + 2])) ++hits;
    return (hits & 1) != 0;
}

void LevelAudioScript::fireRange(const Op& tl, float from, float to, bool inclusiveEnd, SoundCues& cues, MusicPlayer* music,
                                 const core::Vec3& listener) {
    for (const Event& e : tl.events)
        if (e.time >= from && (e.time < to || (inclusiveEnd && e.time <= to))) fire(e.trigger, cues, music, listener);
}

void LevelAudioScript::tick(float dt, const core::Vec3& listener, SoundCues& cues, MusicPlayer* music,
                            const core::Vec3* pawn, bool pawnAlive) {
    havePawn_ = pawn != nullptr && pawnAlive;
    if (pawn) pawn_ = *pawn;
    // Touch volumes (the local pawn, a point 1 m above its origin [PROVISIONAL, as AmbientAudio]). Volumes in authored
    // order; several Touches in one frame fire in that order (the last zone entered wins) [HIGH].
    if (touches_ > 0) {
        const core::Vec3 p = pawn_ + core::Vec3{0, 1.0f, 0};
        for (Op& op : ops_) {
            if (op.type != Type::Touch) continue;
            op.sinceTrigger += dt;
            const bool in = havePawn_ && insideVolume(op, p);
            if (in && !op.inside) {
                const bool allowed = (op.maxTrigger <= 0 || op.triggered < op.maxTrigger) && op.sinceTrigger >= op.retrigger;
                op.inside = true;
                if (allowed) { ++op.triggered; op.sinceTrigger = 0.0f; out(op, "Touched", cues, music, listener); }
            } else if (!in && op.inside) {
                op.inside = false;
                out(op, "UnTouched", cues, music, listener);
            }
        }
    }
    // Zones: the per-tick scene update.
    for (Op& op : ops_) {
        if (op.type != Type::Zone) continue;
        if (op.entered) {
            if (op.sceneCurrent != op.sceneNext) {
                if (op.sceneCurrent >= 0) out(op, ("Scene " + std::to_string(op.sceneCurrent) + " Ended").c_str(), cues, music, listener);
                op.sceneCurrent = op.sceneNext;
                if (logOn()) LOG_INFO("LEVELAUDIO zone %s scene %d begun", op.id.c_str(), op.sceneCurrent);
                out(op, ("Scene " + std::to_string(op.sceneCurrent) + " Begun").c_str(), cues, music, listener);
            }
        } else if (op.sceneCurrent >= 0) {
            const int cur = op.sceneCurrent;
            op.sceneCurrent = -1;
            out(op, ("Scene " + std::to_string(cur) + " Ended").c_str(), cues, music, listener);
        }
    }
    // Delays.
    for (Op& op : ops_) {
        if (op.type != Type::Delay || !op.playing || op.paused) continue;
        op.remaining -= dt;
        if (op.remaining <= 0.0f) { op.playing = false; out(op, "Finished", cues, music, listener); }
    }
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
        const core::Vec3 ref = op.playerSource ? (havePawn_ ? pawn_ : listener) : actorPos(op.source, listener);
        const core::Vec3 at = ref + core::Vec3{std::cos(ang) * d, 0.0f, std::sin(ang) * d};
        cues.play(op.cue.c_str(), at, core::length(at - listener));
        ++oneShots_;
        if (!op.looping) op.playing = false;
    }
    // Flybys [PROVISIONAL motion]: a one-shot that starts away from the player and passes through the head point.
    for (Op& op : ops_) {
        if (op.type != Type::Flyby || !op.playing) continue;
        op.remaining -= dt;
        if (op.remaining > 0.0f) continue;
        op.remaining = op.delayMin + frand() * (op.delayMax - op.delayMin);
        const core::Vec3 head = (havePawn_ ? pawn_ : listener) + core::Vec3{0, 1.5f + (op.headMin + frand() * (op.headMax - op.headMin)) * UU, 0};
        const float ang = frand() * (op.angleMax / 360.0f) * 6.2831853f;
        const float dist = (op.startMin + frand() * (op.startMax - op.startMin)) * UU;
        const float speed = (op.speedMin + frand() * (op.speedMax - op.speedMin)) * UU;
        const core::Vec3 dir{std::cos(ang), 0.0f, std::sin(ang)};
        const core::Vec3 start = head + dir * dist;
        const int inst = cues.play(op.cue.c_str(), start, core::length(start - listener));
        ++oneShots_;
        if (inst >= 0) flybys_.push_back({inst, start, dir * -speed, speed > 0.0f ? 2.0f * dist / speed : 0.0f});
        if (!op.looping) op.playing = false;
    }
    for (size_t i = 0; i < flybys_.size();) {
        Flyby& f = flybys_[i];
        f.life -= dt;
        if (!cues.playing(f.instance)) { flybys_[i] = flybys_.back(); flybys_.pop_back(); continue; }
        if (f.life > 0.0f) { f.pos = f.pos + f.vel * dt; cues.update(f.instance, f.pos, 0.0f); }
        ++i;
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
    for (const Op& op : ops_) n += (op.type == Type::Pool || op.type == Type::Flyby) && op.playing ? 1 : 0;
    return n;
}

int LevelAudioScript::zoneCount() const {
    int n = 0;
    for (const Op& op : ops_) n += op.type == Type::Zone ? 1 : 0;
    return n;
}

int LevelAudioScript::poolCount() const {
    int n = 0;
    for (const Op& op : ops_) n += op.type == Type::Pool || op.type == Type::Flyby ? 1 : 0;
    return n;
}

int LevelAudioScript::flybysPlaying() const {
    int n = 0;
    for (const Op& op : ops_) n += op.type == Type::Flyby && op.playing ? 1 : 0;
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
