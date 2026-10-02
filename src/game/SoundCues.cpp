#include "game/SoundCues.h"
#include "core/Log.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace game {
namespace {

struct CurvePt { float x, y; };
enum class Param { None, Distance, Speed };
struct EventDef {
    float time, volDb, volVarMin, volVarMax, pitchSt, pitchVarMin, pitchVarMax;
    int chanceNone;                      // ChanceToPlayNone (percent)
    bool loop;                           // bLooping
    std::vector<const char*> waves;      // random pick
    std::vector<CurvePt> volCurve;       // over the root SoundParameter (linear gain)
    std::vector<CurvePt> pitchCurve;     // over the root SoundParameter (semitones)
    std::vector<CurvePt> envVol;         // Envelope over playback time (s, linear gain)
    std::vector<CurvePt> envPitch;       // Envelope over playback time (s, semitones)
};
struct CueDef {
    const char* name;
    int maxConcurrent;                   // SoundCue.MaxConcurrentPlayCount (0 = unlimited)
    float volDb, volVarMin, volVarMax, pitchSt, pitchVarMin, pitchVarMax;
    float distMinUU, distMaxUU, rolloff;
    float pan2DUU, pan3DUU;              // SoundNodeRoot SmartPanDistance2D / 3D (default 400 / 800)
    Param param;                         // SoundNodeRoot.SoundParameter
    std::vector<EventDef> events;
};

const CueDef kCues[] = {
#include "game/SoundCues.inc"
};
constexpr int kCueCount = (int)(sizeof(kCues) / sizeof(kCues[0]));

constexpr float UU = 0.01f;
constexpr float kInstanceTail = 10.0f;  // upper bound on a one-shot instance's life after its last event
constexpr float kSpeedParamMax = 120.0f; // [CONF] SoundParameters.Optimus_Prime_Speed.Max

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

int findCue(const char* name) {
    for (int i = 0; i < kCueCount; ++i) if (std::strcmp(kCues[i].name, name) == 0) return i;
    return -1;
}

float lastEventTime(const CueDef& c) {
    float t = 0; for (const EventDef& e : c.events) t = e.time > t ? e.time : t; return t;
}

} // namespace

void SoundCues::load(audio::IAudio* a, const std::string& contentRoot) {
    audio_ = a;
    waves_.assign(kCueCount, {});
    if (!a) return;
    int ok = 0, fail = 0;
    for (int c = 0; c < kCueCount; ++c) {
        for (const EventDef& e : kCues[c].events) {
            std::vector<audio::Sound> w;
            for (const char* f : e.waves) {
                audio::Sound s = a->load(contentRoot + f);
                if (s == audio::kInvalidSound) ++fail; else ++ok;
                w.push_back(s);
            }
            waves_[(size_t)c].push_back(w);
        }
    }
    LOG_INFO("sound cues: %d cues, %d waves loaded, %d failed", kCueCount, ok, fail);
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

// Curve parameter: SOUND_DISTANCE in UU, the speed parameter in mph. Cues without a root
// parameter but with curves (VEH_OPTIMUS_BOOST_START) are fed the speed [MED].
float SoundCues::paramFor(const Instance& in) const {
    const CueDef& cd = kCues[in.cue];
    if (cd.param == Param::Distance) return in.distM / UU;
    return core::clampf(in.speedMph, 0.0f, kSpeedParamMax);
}

int SoundCues::play(const char* name, const core::Vec3& pos, float distM, float speedMph) {
    return play(name, Emitter{pos, kWorld, {0, 0, 0}}, distM, speedMph);
}

int SoundCues::play(const char* name, const Emitter& em, float distM, float speedMph) {
    if (!audio_) return -1;
    int c = findCue(name);
    if (c < 0) { LOG_WARN("sound cue %s not in table", name); return -1; }
    const CueDef& cd = kCues[c];
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
    in.cue = c; in.id = nextId_++; in.age = 0.0f; in.distM = distM; in.speedMph = speedMph;
    in.owner = em.owner; in.offset = em.offset; in.pos = em.pos;
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
    const CueDef& cd = kCues[in.cue];
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
    float gain = dbToGain(ref.baseDb) * evalCurve(ed.volCurve, x, 1.0f) * evalCurve(ed.envVol, in.age, 1.0f);
    if (gain <= 0.0f && !ed.loop) return;          // silent one-shot layer (distance layering)
    audio::VoiceParams p;
    p.volume = gain;
    p.pitch = stToRate(ref.baseSt + evalCurve(ed.pitchCurve, x, 0.0f) + evalCurve(ed.envPitch, in.age, 0.0f));
    p.positional = true;
    p.pos = in.pos;
    p.minDist = cd.distMinUU * UU;
    p.maxDist = cd.distMaxUU * UU;
    p.rolloff = cd.rolloff;
    p.pan2D = cd.pan2DUU * UU; p.pan3D = cd.pan3DUU * UU;
    p.loop = ed.loop;
    ref.v = audio_->playVoice(s, p);
    if (ref.v != audio::kInvalidVoice) in.voices.push_back(ref);
    static const bool log = std::getenv("WFC_CUELOG") != nullptr;
    if (log)
        LOG_INFO("CUE %s ev%d t=%.3f wave=%d gain=%.3f (%.1f dB, param %.0f) pitch=%.3f loop=%d voice=%d owner=%d pos=%.2f,%.2f,%.2f",
                 cd.name, e, in.age, s, p.volume, ref.baseDb, x, p.pitch, (int)ed.loop, ref.v, in.owner,
                 in.pos.x, in.pos.y, in.pos.z);
}

// Attached instances (an AudioComponent on its owner, e.g. HmAnimNotify_Sound / the vehicle audio
// component) take their owner's current position; world instances keep where they were played.
bool SoundCues::resolve(Instance& in) {
    if (in.owner == kWorld || !resolver_) return false;
    core::Vec3 p;
    if (!resolver_(in.owner, in.offset, p)) return false;   // owner unavailable: hold the last position
    in.pos = p;
    return true;
}

// Re-evaluate parameter curves, envelopes, fade and (attached) position for every voice.
void SoundCues::refresh(Instance& in) {
    const CueDef& cd = kCues[in.cue];
    float x = paramFor(in);
    float fade = in.fade > 0.0f ? core::clampf(in.fadeLeft / in.fade, 0.0f, 1.0f) : 1.0f;
    const bool moved = resolve(in) || in.posDirty;
    in.posDirty = false;
    for (const VoiceRef& r : in.voices) {
        const EventDef& ed = cd.events[(size_t)r.event];
        if (!moved && ed.volCurve.empty() && ed.pitchCurve.empty() && ed.envVol.empty() && ed.envPitch.empty() &&
            in.fade <= 0.0f)
            continue;                                // static world one-shot: nothing to update
        float gain = dbToGain(r.baseDb) * evalCurve(ed.volCurve, x, 1.0f) * evalCurve(ed.envVol, in.age, 1.0f) * fade;
        float pitch = stToRate(r.baseSt + evalCurve(ed.pitchCurve, x, 0.0f) + evalCurve(ed.envPitch, in.age, 0.0f));
        audio_->updateVoice(r.v, gain, pitch, in.pos);
    }
}

void SoundCues::update(int id, const core::Vec3& pos, float speedMph) {
    Instance* in = find(id);
    if (!in) return;
    if (in->owner == kWorld) { in->pos = pos; in->posDirty = true; }
    in->speedMph = speedMph;
}

void SoundCues::stop(int id, float fade) {
    Instance* in = find(id);
    if (!in) return;
    static const bool log = std::getenv("WFC_CUELOG") != nullptr;
    if (log) LOG_INFO("CUE %s stop fade=%.2f age=%.2f", kCues[in->cue].name, fade, in->age);
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
        } else if (!in.looping && in.age > lastEventTime(kCues[in.cue])) {
            // One-shot: retire once every voice has finished (attached voices keep following their
            // owner until then); kInstanceTail bounds it for backends that cannot report voices.
            bool sounding = false;
            for (const VoiceRef& r : in.voices) if (audio_->isPlaying(r.v)) { sounding = true; break; }
            if (!sounding || in.age > lastEventTime(kCues[in.cue]) + kInstanceTail) {
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
