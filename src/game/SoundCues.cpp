#include "game/SoundCues.h"
#include "assets/Json.h"
#include "core/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace game {
using namespace cuedata;
namespace {

using LARGE_INTEGER_T = long long;
LARGE_INTEGER_T nowTicks() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
double ticksToMs(LARGE_INTEGER_T t) { return (double)t / 1e6; }

const CueDef kCues[] = {
#include "game/SoundCues.inc"
};

// FSB4 sample-header loop regions of the looping vehicle waves (AssetTools vehicle_audio_loops.json).
// headerLoopFlag is informational only: loop enable is the wave event's bLooping (RE d50c2a9).
struct FsbLoop { const char* wav; int rate, channels; uint32_t totalSamples, loopStart, loopEnd; bool headerLoopFlag; };
#include "game/VehicleLoops.inc"

constexpr float UU = 0.01f;
constexpr float kInstanceTail = 10.0f;  // upper bound on a one-shot instance's life after its last event
constexpr float kSpeedParamMax = 120.0f; // [CONF] SoundParameters.Optimus_Prime_Speed.Max
constexpr float kSlipParamMax = 1.57f;   // [CONF] SoundParameters.Optimus_Prime_Tire_Squeal.Max
constexpr float kOcclCheck = 0.25f;      // [CONF] Xe-TransEngine.ini AudioDevice OcclusionCheckInterval
constexpr float kOcclDb = -6.0f;         // [CONF] Default__PhysicalMaterial AudioOcclusionVolume (61/63 materials)
constexpr float kOcclTime = 0.5f;        // [CONF] Default__PhysicalMaterial AudioOcclusionTransitionTime



float frand() { return (float)std::rand() / (float)RAND_MAX; }
float randRange(float a, float b) { return a + (b - a) * frand(); }   // ranges may be authored inverted
float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }   // root Volume: clamp UNKNOWN (A8), unclamped
// [CONF native dBToLinear 0x82CBED00] x = clamp(x, -96, 0); x > -96 ? 10^(x/20) : 0. Used for wave-event
// Volume, random volume variation, rear / SmartPan / occlusion attenuation. Never boosts above unity.
float dbToLinear(float x) {
    x = std::min(0.0f, std::max(-96.0f, x));
    return x > -96.0f ? (float)std::pow(10.0, (double)x * 0.05) : 0.0f;
}
// [CONF native SemitonesToRatio 0x82CBEE20] clamp(s, -36, 36); 2^(s/12).
float stToRate(float st) {
    st = std::min(36.0f, std::max(-36.0f, st));
    return (float)std::pow(2.0, (double)st / 12.0);
}

float evalCurve(const std::vector<CurvePt>& c, float x, float empty) {
    if (c.empty()) return empty;
    if (x <= c.front().x) return c.front().y;
    if (x >= c.back().x) return c.back().y;
    for (size_t i = 0; i + 1 < c.size(); ++i)
        if (x <= c[i + 1].x) {
            float u = (x - c[i].x) / (c[i + 1].x - c[i].x);
            return c[i].y + (c[i + 1].y - c[i].y) * u;
        }
    return c.back().y;
}

float lastEventTime(const CueDef& c) {
    float t = 0; for (const EventDef& e : c.events) t = e.time > t ? e.time : t; return t;
}

bool isWet(const std::string& category) { return category.rfind("SFX_DRY", 0) != 0 && category.rfind("DX_DRY", 0) != 0 &&
                                                 category.rfind("MUSIC", 0) != 0; }

// Same fold-down as tools/systems/gen_cues.py (HM_Engine.Default__SoundNodeWaveEvent: Center/BackL/
// BackR/LFE at -96 dB, i.e. front stereo unless the event overrides its pans).
float stereoGain(const assets::Json& p) {
    auto lin = [](float db) { return db > -96.0f ? dbToGain(db) : 0.0f; };
    float front = (lin(p["PanFrontLeft"].asFloat(0.0f)) + lin(p["PanFrontRight"].asFloat(0.0f))) * 0.5f;
    float back = (lin(p["PanBackLeft"].asFloat(-96.0f)) + lin(p["PanBackRight"].asFloat(-96.0f))) * 0.5f;
    float g = front + 0.70710678f * back;
    return g > 1.0f ? 1.0f : g;
}

std::vector<CurvePt> jsonCurve(const assets::Json& c) {
    std::vector<CurvePt> out;
    for (size_t i = 0; i < c.size(); ++i) out.push_back({c[i]["X"].asFloat(), c[i]["Y"].asFloat()});
    return out;
}

} // namespace

int SoundCues::findCue(const char* name) const {
    for (size_t i = 0; i < cues_.size(); ++i) if (cues_[i].name == name) return (int)i;
    return -1;
}

const CueDef* SoundCues::cueDef(const char* name) const {
    int c = findCue(name);
    return c >= 0 ? &cues_[(size_t)c] : nullptr;
}

void SoundCues::loadWaves(size_t c, const std::string& contentRoot) {
    if (waves_.size() <= c) waves_.resize(c + 1);
    waves_[c].clear();
    for (const EventDef& e : cues_[c].events) {
        std::vector<audio::Sound> w;
        for (const std::string& f : e.waves) {
            audio::Sound s = audio_ ? audio_->load(contentRoot + f) : audio::kInvalidSound;
            if (s != audio::kInvalidSound) w.push_back(s);
        }
        waves_[c].push_back(w);
    }
}

void SoundCues::load(audio::IAudio* a, const std::string& contentRoot) {
    audio_ = a;
    contentRoot_ = contentRoot;
    cues_.assign(std::begin(kCues), std::end(kCues));
    waves_.clear();
    if (!a) return;
    int ok = 0, total = 0;
    for (size_t c = 0; c < cues_.size(); ++c) {
        loadWaves(c, contentRoot);
        for (size_t e = 0; e < cues_[c].events.size(); ++e) { ok += (int)waves_[c][e].size(); total += (int)cues_[c].events[e].waves.size(); }
    }
    LOG_INFO("sound cues: %zu cues, %d/%d waves loaded", cues_.size(), ok, total);
    applyLoopPoints();
}

int SoundCues::applyLoopPoints() {
    int applied = 0;
    for (const FsbLoop& l : kFsbLoops) {
        audio::Sound s = audio_ ? audio_->load(contentRoot_ + l.wav) : audio::kInvalidSound;   // cached by path
        if (s != audio::kInvalidSound && audio_->setLoopPoints(s, l.loopStart, l.loopEnd)) ++applied;
    }
    LOG_INFO("sound cues: FSB loop regions %d/%d applied (whole sample; no header LOOP flag)", applied,
             (int)(sizeof(kFsbLoops) / sizeof(kFsbLoops[0])));
    return applied;
}

// A map bank's cue graphs as AssetTools vs_audio.py writes them: {name: {tree: {class, params,
// children}}}; root = SoundNodeRoot, children SoundNodeWaveEvent -> SoundNodeWaveEx {wav}.
int SoundCues::addCues(const assets::Json& cues, const std::string& contentRoot) {
    int added = 0;
    for (const auto& kv : cues.obj) {
        const assets::Json& root = kv.second["tree"];
        if (root["class"].asString() != "SoundNodeRoot" || findCue(kv.first.c_str()) >= 0) continue;
        const assets::Json& rp = root["params"];
        CueDef d;
        d.name = kv.first;
        d.maxConcurrent = kv.second["MaxConcurrentPlayCount"].asInt(5);   // Engine.Default__SoundCue
        d.limit = Limit::KillFarthest;                                      // (no map-bank cue authors one)
        d.panAtten3DDb = rp["SmartPanAttenuation3D"].asFloat(0.0f);
        d.volDb = rp["Volume"].asFloat(-6.0f);                     // HM_Engine.Default__SoundNodeRoot
        d.volVarMin = rp["VolumeVariationMin"].asFloat(0.0f); d.volVarMax = rp["VolumeVariationMax"].asFloat(0.0f);
        d.pitchSt = rp["Pitch"].asFloat(0.0f);
        d.pitchVarMin = rp["PitchVariationMin"].asFloat(0.0f); d.pitchVarMax = rp["PitchVariationMax"].asFloat(0.0f);
        d.distMinUU = rp["DistanceMin"].asFloat(400.0f); d.distMaxUU = rp["DistanceMax"].asFloat(6400.0f);
        d.rolloff = rp["RolloffFactor"].asFloat(1.0f);
        d.pan2DUU = rp["SmartPanDistance2D"].asFloat(400.0f); d.pan3DUU = rp["SmartPanDistance3D"].asFloat(800.0f);
        d.rearAttenDb = rp["RearAttenuation"].asFloat(0.0f);
        d.category = rp["Category"].asString();
        d.mixerPreset.clear();                                   // map bank cues author none
        d.occlusion = rp["EnableOcclusionVolume"].asBool(true);
        d.priority = rp["Priority"].asFloat(0.0f);                  // unset in object and class default -> 0
        const std::string sp = rp["SpatializationType"].asString();
        d.spatial = sp == "k2D" ? Spatial::TwoD : sp == "kSmartPan" ? Spatial::SmartPan
                  : sp == "kSmartPan_PreferPlayer" ? Spatial::SmartPanPreferPlayer : Spatial::ThreeD;
        d.param = Param::None;
        const assets::Json& kids = root["children"];
        for (size_t i = 0; i < kids.size(); ++i) {
            const assets::Json& ev = kids[i];
            if (ev["class"].asString() != "SoundNodeWaveEvent") continue;
            const assets::Json& p = ev["params"];
            EventDef e{};
            e.time = p["Time"].asFloat(0.0f);
            e.volDb = p["Volume"].asFloat(0.0f);
            e.volVarMin = p["VolumeVariationMin"].asFloat(0.0f); e.volVarMax = p["VolumeVariationMax"].asFloat(0.0f);
            e.pitchSt = p["Pitch"].asFloat(0.0f);
            e.pitchVarMin = p["PitchVariationMin"].asFloat(0.0f); e.pitchVarMax = p["PitchVariationMax"].asFloat(0.0f);
            e.chanceNone = p["ChanceToPlayNone"].asInt(0);
            e.loop = p["bLooping"].asBool(false);
            e.overridePriority = p["OverridePriority"].asBool(false);
            e.priority = p["Priority"].asFloat(0.0f);
            e.stereoGain = stereoGain(p);
            e.volCurve = jsonCurve(p["VolumeCurve"]); e.pitchCurve = jsonCurve(p["PitchCurve"]);
            const assets::Json& waves = ev["children"];
            for (size_t w = 0; w < waves.size(); ++w) {
                std::string f = waves[w]["wav"].asString();
                if (f.rfind("content/", 0) == 0) f = f.substr(8);
                if (!f.empty()) e.waves.push_back(f);
            }
            d.events.push_back(e);
        }
        cues_.push_back(d);
        loadWaves(cues_.size() - 1, contentRoot);
        ++added;
    }
    return added;
}

SoundCues::Instance* SoundCues::find(int id) {
    for (Instance& x : live_) if (x.id == id) return &x;
    return nullptr;
}

int SoundCues::activeInstances(const char* cue) const {
    int c = findCue(cue), n = 0;
    for (const Instance& in : live_) if (in.cue == c) ++n;
    return n;
}

bool SoundCues::playing(int id) const {
    for (const Instance& x : live_) if (x.id == id) return true;
    return false;
}

bool SoundCues::instancePos(int id, core::Vec3& out) const {
    for (const Instance& x : live_) if (x.id == id) { out = x.pos; return true; }
    return false;
}

// Curve parameter: SOUND_DISTANCE in UU; the speed parameter in mph; the tire-squeal slip angle in
// rad. Cues without a root parameter but with curves (VEH_OPTIMUS_BOOST_START) are fed the speed [MED].
float SoundCues::paramFor(const Instance& in) const {
    const CueDef& cd = cues_[(size_t)in.cue];
    if (cd.param == Param::Distance) return in.distM / UU;
    if (cd.param == Param::TireSqueal) return core::clampf(in.param, 0.0f, kSlipParamMax);
    return core::clampf(in.param, 0.0f, kSpeedParamMax);
}

int SoundCues::play(const char* name, const core::Vec3& pos, float distM, float param) {
    return play(name, Emitter{pos, kWorld, {0, 0, 0}, {}}, distM, param);
}

int SoundCues::play(const char* name, const Emitter& em, float distM, float param) {
    if (!audio_) return -1;
    static const bool timeLog = std::getenv("WFC_AUDIOTIME") != nullptr;
    LARGE_INTEGER_T t0 = timeLog ? nowTicks() : 0;
    struct Done { bool on; LARGE_INTEGER_T t0; const char* n; ~Done() { if (on) { double ms = ticksToMs(nowTicks() - t0); if (ms > 0.05) LOG_INFO("AUDIOTIME play %s %.3f ms", n, ms); } } } done{timeLog, t0, name};
    int c = findCue(name);
    if (c < 0) { LOG_WARN("sound cue %s not in table", name); return -1; }
    const CueDef& cd = cues_[(size_t)c];
    Instance in;
    in.cue = c; in.id = nextId_++; in.age = 0.0f; in.distM = distM; in.param = param;
    in.owner = em.owner; in.offset = em.offset; in.socket = em.socket; in.pos = em.pos;
    resolve(in);
    // USoundCue::RegisterInstanceLimiting [CONF native 0x82E767B8]: 0 = unlimited; at the limit,
    // kKillOldest stops the oldest registered instance, kKillNewest refuses the new sound, kKillFarthest
    // walks newest -> oldest keeping the farthest (ties -> the older) of the instances at least as far
    // from the listener as the new sound, stops it, or refuses the new sound if none is. A stop is
    // immediate (no fade).
    if (cd.maxConcurrent > 0) {
        std::vector<size_t> playing;                     // registration order = instance id
        for (size_t i = 0; i < live_.size(); ++i) if (live_[i].cue == c) playing.push_back(i);
        if ((int)playing.size() >= cd.maxConcurrent) {
            std::sort(playing.begin(), playing.end(), [&](size_t a, size_t b) { return live_[a].id < live_[b].id; });
            int victim = -1;
            if (cd.limit == Limit::KillNewest) return -1;
            if (cd.limit == Limit::KillOldest) victim = live_[playing.front()].id;
            else {
                auto d2 = [&](const core::Vec3& p) { core::Vec3 v = p - listener_; return core::dot(v, v); };
                float best = d2(in.pos);
                for (size_t k = playing.size(); k-- > 0;) {
                    float d = d2(live_[playing[k]].pos);
                    if (d - best >= 0.0f || std::fabs(d - best) < 1e-6f) { best = d; victim = live_[playing[k]].id; }
                }
                if (victim < 0) return -1;               // the new sound is the farthest: refused
            }
            stop(victim, 0.0f);
        }
    }
    if (!cd.mixerPreset.empty()) mixer_.enable(cd.mixerPreset);      // SoundNodeRoot.PlayMixerPreset (AC play)
    // A new instance starts with the current occlusion (no fade-in from clear).
    if (cd.occlusion && occlusion_ && in.owner != kUI && cd.spatial != Spatial::TwoD) {
        in.occl = in.occlTarget = occlusion_(listener_, in.pos, in.owner) ? 1.0f : 0.0f;
        in.occlCheck = kOcclCheck * (float)(in.id % 8) / 8.0f;
    }
    for (const EventDef& e : cd.events) if (e.loop) in.looping = true;
    live_.push_back(in);
    int id = in.id;
    for (int e = 0; e < (int)cd.events.size(); ++e) {
        if (cd.events[(size_t)e].time <= 0.0f) launch(live_.back(), e);
        else pending_.push_back({id, cd.events[(size_t)e].time, e});
    }
    return id;
}

void SoundCues::launch(Instance& in, int e) {
    const CueDef& cd = cues_[(size_t)in.cue];
    const EventDef& ed = cd.events[(size_t)e];
    const std::vector<audio::Sound>& w = waves_[(size_t)in.cue][(size_t)e];
    if (w.empty()) return;
    if (ed.chanceNone > 0 && frand() * 100.0f < (float)ed.chanceNone) return;
    audio::Sound s = w[(size_t)(std::rand() % (int)w.size())];
    VoiceRef ref;
    ref.event = e;
    ref.baseGain = dbToGain(cd.volDb) * dbToLinear(randRange(cd.volVarMin, cd.volVarMax)) *
                   dbToLinear(ed.volDb) * dbToLinear(randRange(ed.volVarMin, ed.volVarMax));
    ref.baseSt = cd.pitchSt + randRange(cd.pitchVarMin, cd.pitchVarMax) + ed.pitchSt + randRange(ed.pitchVarMin, ed.pitchVarMax);
    float x = paramFor(in);
    float gain = ref.baseGain * ed.stereoGain * gainOf(in) * dbToLinear(kOcclDb * in.occl) *
                 evalCurve(ed.volCurve, x, 1.0f) * evalCurve(ed.envVol, in.age, 1.0f);
    if (gain <= 0.0f && !ed.loop) return;          // silent one-shot layer (distance layering)
    audio::VoiceParams p;
    p.volume = gain;
    p.pitch = stToRate(ref.baseSt + evalCurve(ed.pitchCurve, x, 0.0f) + evalCurve(ed.envPitch, in.age, 0.0f));
    p.positional = in.owner != kUI && cd.spatial != Spatial::TwoD;
    p.spatial = (int)cd.spatial;
    p.panAtten3DDb = cd.panAtten3DDb;
    p.pos = in.pos;
    p.minDist = cd.distMinUU * UU;
    p.maxDist = cd.distMaxUU * UU;
    p.rolloff = cd.rolloff;
    p.pan2D = cd.pan2DUU * UU; p.pan3D = cd.pan3DUU * UU;
    p.rearAttenDb = cd.rearAttenDb;
    p.wet = isWet(cd.category);
    p.loop = ed.loop;
    // [CONF native, RE d50c2a9 P1] wave.Priority = OverridePriority ? node.Priority : root.Priority; the FMOD
    // channel priority is int(255 - clamp(Priority, -1, 255)) (0 = most important).
    p.priority = (int)(255.0f - std::min(255.0f, std::max(-1.0f, ed.overridePriority ? ed.priority : cd.priority)));
    ref.v = audio_->playVoice(s, p);
    if (ref.v != audio::kInvalidVoice) in.voices.push_back(ref);
    static const bool log = std::getenv("WFC_CUELOG") != nullptr;
    if (log)
        LOG_INFO("CUE %s ev%d t=%.3f wave=%d gain=%.3f (%.1f dB, param %.2f) pitch=%.3f loop=%d voice=%d owner=%d occl=%.2f pos=%.2f,%.2f,%.2f",
                 cd.name.c_str(), e, in.age, s, p.volume, 20.0f * std::log10(std::max(1e-6f, ref.baseGain)), x, p.pitch, (int)ed.loop, ref.v, in.owner, in.occl,
                 in.pos.x, in.pos.y, in.pos.z);
}

// Attached instances (an AudioComponent on its owner, e.g. HmAnimNotify_Sound / the vehicle audio
// component) take their owner's current position; world instances keep where they were played.
bool SoundCues::resolve(Instance& in) {
    if (in.owner < 0 || !resolver_) return false;
    core::Vec3 p;
    if (!resolver_(in.owner, in.socket, in.offset, p)) return false;   // owner unavailable: hold the last position
    in.pos = p;
    return true;
}

// Re-evaluate parameter curves, envelopes, fade and (attached) position for every voice.
void SoundCues::refresh(Instance& in) {
    const CueDef& cd = cues_[(size_t)in.cue];
    float x = paramFor(in);
    float fade = in.fade > 0.0f ? core::clampf(in.fadeLeft / in.fade, 0.0f, 1.0f) : 1.0f;
    const bool moved = resolve(in) || in.posDirty;
    const float g = gainOf(in);
    const bool mixerMoving = g != in.lastGain;            // category volume ramp / instance level changed
    in.lastGain = g;
    in.posDirty = false;
    const bool occlChanging = in.occl != in.occlTarget;
    for (const VoiceRef& r : in.voices) {
        const EventDef& ed = cd.events[(size_t)r.event];
        if (!moved && !occlChanging && in.occl == 0.0f && ed.volCurve.empty() && ed.pitchCurve.empty() &&
            ed.envVol.empty() && ed.envPitch.empty() && in.fade <= 0.0f && g == 1.0f && !mixerMoving)
            continue;                                // static world one-shot: nothing to update
        float gain = r.baseGain * ed.stereoGain * gainOf(in) * dbToLinear(kOcclDb * in.occl) *
                     evalCurve(ed.volCurve, x, 1.0f) * evalCurve(ed.envVol, in.age, 1.0f) * fade;
        float pitch = stToRate(r.baseSt + evalCurve(ed.pitchCurve, x, 0.0f) + evalCurve(ed.envPitch, in.age, 0.0f));
        audio_->updateVoice(r.v, gain, pitch, in.pos);
    }
}

void SoundCues::update(int id, const core::Vec3& pos, float param) {
    Instance* in = find(id);
    if (!in) return;
    if (in->owner == kWorld) { in->pos = pos; in->posDirty = true; }
    in->param = param;
}

void SoundCues::logSpatial(const char* tag, const core::Vec3& ownerPos, bool worldToo) const {
    for (const Instance& in : live_) {
        if (in.owner == kWorld && !worldToo) continue;
        audio::VoiceInfo vi;
        bool have = false;
        for (const VoiceRef& r : in.voices) if (audio_ && audio_->voiceInfo(r.v, vi)) { have = true; break; }
        LOG_INFO("SPATIAL %s cue=%s owner=%d socket=%s src=%.2f,%.2f,%.2f ownerPos=%.2f,%.2f,%.2f src-owner=%.2fm "
                 "listener=%.2f,%.2f,%.2f dist=%.2fm pan=%+.2f atten=%.3f occl=%.2f gL=%.3f gR=%.3f%s",
                 tag, cues_[(size_t)in.cue].name.c_str(), in.owner, in.socket.empty() ? "-" : in.socket.c_str(),
                 in.pos.x, in.pos.y, in.pos.z, ownerPos.x, ownerPos.y, ownerPos.z, core::length(in.pos - ownerPos),
                 listener_.x, listener_.y, listener_.z, have ? vi.dist : core::length(in.pos - listener_),
                 have ? vi.pan : 0.0f, have ? vi.atten : 1.0f, in.occl, have ? vi.gainL : 0.0f, have ? vi.gainR : 0.0f,
                 have ? "" : " (no live voice)");
    }
}

int SoundCues::occludedInstances() const {
    int n = 0;
    for (const Instance& in : live_) if (in.occlTarget > 0.0f) ++n;
    return n;
}

void SoundCues::retire(size_t i) {
    const CueDef& cd = cues_[(size_t)live_[i].cue];
    if (!cd.mixerPreset.empty()) mixer_.disable(cd.mixerPreset, false);   // AC stopped: non-forced Disable
    live_[i] = live_.back();
    live_.pop_back();
}

float SoundCues::level(const Instance& in) {
    float f = in.fadeInLen > 0.0f ? core::clampf(in.age / in.fadeInLen, 0.0f, 1.0f) : 1.0f;
    return in.volume * f;
}

void SoundCues::fadeIn(int id, float seconds) {
    Instance* in = find(id);
    if (!in) return;
    in->fadeInLen = seconds + in->age;     // ramp from the instance start
    refresh(*in);
}

void SoundCues::setVolume(int id, float linear) {
    if (Instance* in = find(id)) in->volume = linear;
}

void SoundCues::stop(int id, float fade) {
    Instance* in = find(id);
    if (!in) return;
    static const bool log = std::getenv("WFC_CUELOG") != nullptr;
    if (log) LOG_INFO("CUE %s stop fade=%.2f age=%.2f", cues_[(size_t)in->cue].name.c_str(), fade, in->age);
    // No further events from a stopped instance.
    for (size_t i = 0; i < pending_.size();) {
        if (pending_[i].inst == id) { pending_[i] = pending_.back(); pending_.pop_back(); } else ++i;
    }
    if (fade > 0.0f) { in->fade = fade; in->fadeLeft = fade; return; }
    for (const VoiceRef& r : in->voices) audio_->stopVoice(r.v);
    for (size_t i = 0; i < live_.size(); ++i)
        if (live_[i].id == id) { retire(i); break; }
}

void SoundCues::tick(float dt) {
    if (!audio_) return;
    // Mixer: timers / Duration expiry, then linear parameter ramps; MASTER_WET goes to the backend as-is
    // (the mixer ramp is the only fade).
    mixer_.tick(dt);
    if (mixer_.environmentChanged()) audio_->setEnvironment(mixer_.environment(), 0.0f);
    for (Instance& in : live_) in.age += dt;
    for (size_t i = 0; i < pending_.size();) {
        Pending& p = pending_[i];
        Instance* in = find(p.inst);
        if (!in) { pending_[i] = pending_.back(); pending_.pop_back(); continue; }
        if (in->age >= p.t) {
            resolve(*in);                            // a delayed wave event starts at the owner's position now
            launch(*in, p.event);
            pending_[i] = pending_.back(); pending_.pop_back();
            continue;
        }
        ++i;
    }
    for (size_t i = 0; i < live_.size();) {
        Instance& in = live_[i];
        bool done = false;
        if (in.fade > 0.0f) {
            in.fadeLeft -= dt;
            if (in.fadeLeft <= 0.0f) done = true;
        } else if (!in.looping && in.age > lastEventTime(cues_[(size_t)in.cue])) {
            // One-shot: retire once every voice has finished (attached voices keep following their
            // owner until then); kInstanceTail bounds it for backends that cannot report voices.
            bool sounding = !audio_->reportsVoices();   // unknown: keep following until kInstanceTail
            for (const VoiceRef& r : in.voices) if (audio_->isPlaying(r.v)) { sounding = true; break; }
            if (!sounding || in.age > lastEventTime(cues_[(size_t)in.cue]) + kInstanceTail) {
                retire(i);
                continue;
            }
        }
        if (done) {
            for (const VoiceRef& r : in.voices) audio_->stopVoice(r.v);
            retire(i);
            continue;
        }
        // Occlusion: line check from the listener every OcclusionCheckInterval (staggered by instance
        // id), then a linear fade over AudioOcclusionTransitionTime.
        const CueDef& cd = cues_[(size_t)in.cue];
        if (cd.occlusion && occlusion_ && in.owner != kUI && cd.spatial != Spatial::TwoD) {
            in.occlCheck -= dt;
            if (in.occlCheck <= 0.0f) {
                in.occlCheck = kOcclCheck;
                in.occlTarget = occlusion_(listener_, in.pos, in.owner) ? 1.0f : 0.0f;
            }
            in.occl += core::clampf(in.occlTarget - in.occl, -dt / kOcclTime, dt / kOcclTime);
        }
        refresh(in);
        ++i;
    }
}

} // namespace game
