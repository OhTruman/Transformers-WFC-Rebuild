#include "game/SoundCues.h"
#include "assets/Json.h"
#include "core/Log.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace game {
using namespace cuedata;
namespace {

const CueDef kCues[] = {
#include "game/SoundCues.inc"
};

constexpr float UU = 0.01f;
constexpr float kInstanceTail = 10.0f;  // upper bound on a one-shot instance's life after its last event
constexpr float kSpeedParamMax = 120.0f; // [CONF] SoundParameters.Optimus_Prime_Speed.Max
constexpr float kSlipParamMax = 1.57f;   // [CONF] SoundParameters.Optimus_Prime_Tire_Squeal.Max

float frand() { return (float)std::rand() / (float)RAND_MAX; }
float randRange(float a, float b) { return a + (b - a) * frand(); }   // ranges may be authored inverted
float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }
float stToRate(float st) { return std::pow(2.0f, st / 12.0f); }

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
    cues_.assign(std::begin(kCues), std::end(kCues));
    waves_.clear();
    if (!a) return;
    int ok = 0, total = 0;
    for (size_t c = 0; c < cues_.size(); ++c) {
        loadWaves(c, contentRoot);
        for (size_t e = 0; e < cues_[c].events.size(); ++e) { ok += (int)waves_[c][e].size(); total += (int)cues_[c].events[e].waves.size(); }
    }
    LOG_INFO("sound cues: %zu cues, %d/%d waves loaded", cues_.size(), ok, total);
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
        d.maxConcurrent = kv.second["MaxConcurrentPlayCount"].asInt(0);
        d.volDb = rp["Volume"].asFloat(-6.0f);                     // HM_Engine.Default__SoundNodeRoot
        d.volVarMin = rp["VolumeVariationMin"].asFloat(0.0f); d.volVarMax = rp["VolumeVariationMax"].asFloat(0.0f);
        d.pitchSt = rp["Pitch"].asFloat(0.0f);
        d.pitchVarMin = rp["PitchVariationMin"].asFloat(0.0f); d.pitchVarMax = rp["PitchVariationMax"].asFloat(0.0f);
        d.distMinUU = rp["DistanceMin"].asFloat(400.0f); d.distMaxUU = rp["DistanceMax"].asFloat(6400.0f);
        d.rolloff = rp["RolloffFactor"].asFloat(1.0f);
        d.pan2DUU = rp["SmartPanDistance2D"].asFloat(400.0f); d.pan3DUU = rp["SmartPanDistance3D"].asFloat(800.0f);
        d.rearAttenDb = rp["RearAttenuation"].asFloat(0.0f);
        d.category = rp["Category"].asString();
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
    int c = findCue(name);
    if (c < 0) { LOG_WARN("sound cue %s not in table", name); return -1; }
    const CueDef& cd = cues_[(size_t)c];
    // MaxConcurrentPlayCount: steal the oldest instance of this cue.
    if (cd.maxConcurrent > 0) {
        int count = 0, oldest = -1;
        for (int i = 0; i < (int)live_.size(); ++i) {
            if (live_[(size_t)i].cue != c) continue;
            ++count;
            if (oldest < 0 || live_[(size_t)i].age > live_[(size_t)oldest].age) oldest = i;
        }
        if (count >= cd.maxConcurrent && oldest >= 0) stop(live_[(size_t)oldest].id, 0.0f);
    }
    Instance in;
    in.cue = c; in.id = nextId_++; in.age = 0.0f; in.distM = distM; in.param = param;
    in.owner = em.owner; in.offset = em.offset; in.socket = em.socket; in.pos = em.pos;
    resolve(in);
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
    ref.baseDb = cd.volDb + randRange(cd.volVarMin, cd.volVarMax) + ed.volDb + randRange(ed.volVarMin, ed.volVarMax);
    ref.baseSt = cd.pitchSt + randRange(cd.pitchVarMin, cd.pitchVarMax) + ed.pitchSt + randRange(ed.pitchVarMin, ed.pitchVarMax);
    float x = paramFor(in);
    float gain = dbToGain(ref.baseDb) * ed.stereoGain * in.volume * evalCurve(ed.volCurve, x, 1.0f) *
                 evalCurve(ed.envVol, in.age, 1.0f);
    if (gain <= 0.0f && !ed.loop) return;          // silent one-shot layer (distance layering)
    audio::VoiceParams p;
    p.volume = gain;
    p.pitch = stToRate(ref.baseSt + evalCurve(ed.pitchCurve, x, 0.0f) + evalCurve(ed.envPitch, in.age, 0.0f));
    p.positional = in.owner != kUI;
    p.pos = in.pos;
    p.minDist = cd.distMinUU * UU;
    p.maxDist = cd.distMaxUU * UU;
    p.rolloff = cd.rolloff;
    p.pan2D = cd.pan2DUU * UU; p.pan3D = cd.pan3DUU * UU;
    p.rearAttenDb = cd.rearAttenDb;
    p.wet = isWet(cd.category);
    p.loop = ed.loop;
    ref.v = audio_->playVoice(s, p);
    if (ref.v != audio::kInvalidVoice) in.voices.push_back(ref);
    static const bool log = std::getenv("WFC_CUELOG") != nullptr;
    if (log)
        LOG_INFO("CUE %s ev%d t=%.3f wave=%d gain=%.3f (%.1f dB, param %.2f) pitch=%.3f loop=%d voice=%d owner=%d pos=%.2f,%.2f,%.2f",
                 cd.name.c_str(), e, in.age, s, p.volume, ref.baseDb, x, p.pitch, (int)ed.loop, ref.v, in.owner,
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
    in.posDirty = false;
    for (const VoiceRef& r : in.voices) {
        const EventDef& ed = cd.events[(size_t)r.event];
        if (!moved && ed.volCurve.empty() && ed.pitchCurve.empty() && ed.envVol.empty() && ed.envPitch.empty() &&
            in.fade <= 0.0f && in.volume == 1.0f)
            continue;                                // static world one-shot: nothing to update
        float gain = dbToGain(r.baseDb) * ed.stereoGain * in.volume * evalCurve(ed.volCurve, x, 1.0f) *
                     evalCurve(ed.envVol, in.age, 1.0f) * fade;
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
        if (live_[i].id == id) { live_[i] = live_.back(); live_.pop_back(); break; }
}

void SoundCues::tick(float dt) {
    if (!audio_) return;
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
                live_[i] = live_.back(); live_.pop_back();
                continue;
            }
        }
        if (done) {
            for (const VoiceRef& r : in.voices) audio_->stopVoice(r.v);
            live_[i] = live_.back(); live_.pop_back();
            continue;
        }
        refresh(in);
        ++i;
    }
}

} // namespace game
