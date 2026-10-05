#include "game/SoundCues.h"
#include <cstdio>
#include <cctype>
#include <set>
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
    // Centre (dialogue: the announcer cues pan to PanCenter 0 with the fronts at -96) folds to both sides at -3 dB
    // [HIGH: standard 5.1 -> stereo fold-down; no compiled or map-bed cue sets PanCenter].
    float centre = lin(p["PanCenter"].asFloat(-96.0f));
    float g = front + 0.70710678f * back + 0.70710678f * centre;
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
    // Full asset names of the cues the compiled table keeps under a short name (tools/systems/gen_cues.py short()):
    // a character / weapon profile names them by package.
    static const struct { const char* pkg; const char* prefix; } kAlias[] = {
        {"BL_WPN_GUN_ION_BLASTER.", ""}, {"BL_WPN_FOLEY.", "FOLEY."}, {"BL_VEH_OPTIMUS_PRIME.", ""}, {"BL_VEH_SOUNDWAVE.", ""}};
    for (const auto& a : kAlias) {
        const size_t n = std::strlen(a.pkg);
        if (std::strncmp(name, a.pkg, n) != 0) continue;
        const std::string s = std::string(a.prefix) + (name + n);
        for (size_t i = 0; i < cues_.size(); ++i) if (cues_[i].name == s && !cues_[i].mapBank) return (int)i;
    }
    return -1;
}

const CueDef* SoundCues::cueDef(const char* name) const {
    int c = findCue(name);
    return c >= 0 ? &cues_[(size_t)c] : nullptr;
}

void SoundCues::loadWaves(size_t c, const std::string& contentRoot) {
    if (waves_.size() <= c) waves_.resize(c + 1);
    if (resident_.size() <= c) resident_.resize(c + 1, 0);
    waves_[c].clear();
    resident_[c] = 1;
    for (const EventDef& e : cues_[c].events) {
        std::vector<audio::Sound> w;
        for (const std::string& f : e.waves) {
            const bool abs = f.size() > 1 && (f[1] == ':' || f[0] == '/');       // a localized twin outside content/
            audio::Sound s = audio_ ? audio_->load(abs ? f : contentRoot + f) : audio::kInvalidSound;
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
    resident_.assign(cues_.size(), 0);
    waves_.assign(cues_.size(), {});
    for (size_t c = 0; c < cues_.size(); ++c) {
        if (cues_[c].streamed) { waves_[c].assign(cues_[c].events.size(), {}); continue; }   // decoded on first play
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
// Localized waves (dialogue / announcer) [CONF RE TARGETED_PASS4 C]: the SoundNodeWaves live only in the map's
// <Map>_LOC_<lang> twin packages (_LOC_int, _LOC_FRA; same object paths) and the engine loads the ONE twin of GLanguage
// - the same language as the movies' Bink track (movieLanguageSlot), resolved by package instead of by track.
// The extracted content/<group>/<name>.wav is whichever twin the manifest row came from (`loc`: 3445 FRA vs 2836 int
// rows - the TDM match-start lines are French). Resolution: content/_LOC/<twin>/<group>/<name>.wav (WFC_LOC_ROOT
// overrides the _LOC root), else the merged file if `loc` is the selected twin, else NOT played (logged once): another
// language is never substituted. Twin of GLanguage: INT -> "int", else the language code (FRA ...).
std::string SoundCues::localizedWave(const std::string& rel, const std::string& owner, const std::string& contentRoot) {
    const char* lang = std::getenv("WFC_LANGUAGE");
    std::string twin = lang && *lang ? lang : "INT";
    for (char& ch : twin) ch = (char)std::toupper((unsigned char)ch);
    if (twin == "INT") twin = "int";
    const char* locRoot = std::getenv("WFC_LOC_ROOT");
    const std::string alt = (locRoot && *locRoot ? std::string(locRoot) + "/" : contentRoot + "_LOC/") + twin + "/" + rel;
    if (std::FILE* fp = std::fopen((alt.size() > 1 && (alt[1] == ':' || alt[0] == '/') ? alt : contentRoot + alt).c_str(), "rb")) {
        std::fclose(fp);
        return alt;
    }
    std::string o = owner, t = twin;
    for (char& ch : o) ch = (char)std::toupper((unsigned char)ch);
    for (char& ch : t) ch = (char)std::toupper((unsigned char)ch);
    if (o == t) return rel;
    static std::set<std::string> warned;
    if (warned.insert(rel).second)
        LOG_WARN("sound cues: localized wave %s: no _LOC_%s twin extracted (the extracted copy is _LOC_%s) - not played",
                 rel.c_str(), twin.c_str(), owner.c_str());
    return std::string();
}

int SoundCues::addCues(const assets::Json& cues, const std::string& contentRoot) {
    int added = 0;
    for (const auto& kv : cues.obj) {
        const assets::Json& root = kv.second["tree"];
        if (root["class"].asString() != "SoundNodeRoot" || findCue(kv.first.c_str()) >= 0) continue;
        const assets::Json& rp = root["params"];
        CueDef d;
        d.name = kv.first;
        // The level manifest's per-cue-asset fields (merged by AmbientAudio), else Engine.Default__SoundCue.
        d.maxConcurrent = kv.second["MaxConcurrentPlayCount"].asInt(5);
        const std::string il = kv.second["InstanceLimiting"].asString();
        d.limit = il == "kKillOldest" ? Limit::KillOldest : il == "kKillNewest" ? Limit::KillNewest : Limit::KillFarthest;
        d.mapBank = true;
        d.rootLoop = rp["bLooping"].asBool(false);
        d.loopStart = rp["LoopStart"].asFloat(0.0f);
        d.loopEnd = rp["LoopEnd"].asFloat(0.0f);
        d.streamed = kv.second["streamed"].asBool(false);   // decoded on first play, released after (dialogue, music)
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
        std::vector<std::string> dialogChars;                      // per wave event
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
            dialogChars.push_back(p["DialogCharacter"].asString());   // HmSoundNodeWaveEvent.DialogCharacter
            const assets::Json& waves = ev["children"];
            for (size_t w = 0; w < waves.size(); ++w) {
                std::string f = waves[w]["wav"].asString();
                if (f.rfind("content/", 0) == 0) f = f.substr(8);
                const std::string loc = waves[w]["loc"].asString();
                if (!loc.empty()) f = localizedWave(f, loc, contentRoot);
                if (!f.empty()) e.waves.push_back(f);
            }
            d.events.push_back(e);
        }
        cues_.push_back(d);
        bool anyChar = false;
        for (const std::string& ch : dialogChars) anyChar = anyChar || !ch.empty();
        if (anyChar) dialogChars_[cues_.size() - 1] = dialogChars;
        if (d.streamed) {
            if (waves_.size() < cues_.size()) waves_.resize(cues_.size());
            if (resident_.size() < cues_.size()) resident_.resize(cues_.size(), 0);
            waves_.back().assign(d.events.size(), {});
        } else loadWaves(cues_.size() - 1, contentRoot);
        ++added;
    }
    return added;
}

void SoundCues::stopAll() {
    for (size_t i = live_.size(); i-- > 0;) {
        if (audio_) for (const VoiceRef& r : live_[i].voices) audio_->stopVoice(r.v);
        retire(i);
    }
    pending_.clear();
}

int SoundCues::stopNonMapInstances() {
    int n = 0;
    for (size_t i = live_.size(); i-- > 0;) {
        if (cues_[(size_t)live_[i].cue].mapBank) continue;
        const int id = live_[i].id;
        for (size_t p = 0; p < pending_.size();) {
            if (pending_[p].inst == id) { pending_[p] = pending_.back(); pending_.pop_back(); } else ++p;
        }
        if (audio_) for (const VoiceRef& r : live_[i].voices) audio_->stopVoice(r.v);
        retire(i);
        ++n;
    }
    return n;
}

std::string SoundCues::pendingSummary() const {
    std::vector<std::pair<int, int>> n;                       // (count, cue)
    for (const Pending& p : pending_) {
        int c = -1;
        for (const Instance& x : live_) if (x.id == p.inst) { c = x.cue; break; }
        bool found = false;
        for (auto& e : n) if (e.second == c) { ++e.first; found = true; break; }
        if (!found) n.push_back({1, c});
    }
    std::sort(n.begin(), n.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::string s;
    for (const auto& e : n) s += (e.second >= 0 ? cues_[(size_t)e.second].name : std::string("<orphan>")) + " x" + std::to_string(e.first) + " ";
    return s;
}

bool SoundCues::prefetch(const char* cue) {
    int c = findCue(cue);
    if (c < 0) return false;
    if (pinned_.size() <= (size_t)c) pinned_.resize((size_t)c + 1, 0);
    if (!cues_[(size_t)c].streamed) return true;                   // always resident
    if ((size_t)c >= resident_.size() || !resident_[(size_t)c]) loadWaves((size_t)c, contentRoot_);
    pinned_[(size_t)c] = 1;
    return true;
}

bool SoundCues::wavesResident(const char* cue) const {
    int c = findCue(cue);
    return c >= 0 && (size_t)c < resident_.size() && resident_[(size_t)c];
}

void SoundCues::releaseWaves(size_t c) {
    if (c >= waves_.size()) return;
    for (auto& ev : waves_[c]) {
        for (audio::Sound w : ev) if (audio_) audio_->release(w);
        ev.clear();
    }
    if (c < resident_.size()) resident_[c] = 0;
}

int SoundCues::mapCueCount() const {
    int n = 0;
    for (const cuedata::CueDef& c : cues_) n += c.mapBank ? 1 : 0;
    return n;
}

int SoundCues::unloadMapCues() {
    for (size_t i = live_.size(); i-- > 0;) {
        if (!cues_[(size_t)live_[i].cue].mapBank) continue;
        const int id = live_[i].id;
        for (size_t p = 0; p < pending_.size();) {
            if (pending_[p].inst == id) { pending_[p] = pending_.back(); pending_.pop_back(); } else ++p;
        }
        if (audio_) for (const VoiceRef& r : live_[i].voices) audio_->stopVoice(r.v);
        retire(i);
    }
    // Map-bank cues are appended after the built-in table (addCues), so they form the tail.
    size_t first = cues_.size();
    while (first > 0 && cues_[first - 1].mapBank) --first;
    for (size_t c = 0; c < first; ++c)
        if (cues_[c].mapBank) LOG_WARN("sound cues: map cue %s is not at the table tail", cues_[c].name.c_str());
    const int removed = (int)(cues_.size() - first);
    if (audio_ && first < waves_.size()) {
        std::vector<audio::Sound> keep;
        for (size_t c = 0; c < first && c < waves_.size(); ++c)
            for (const auto& ev : waves_[c]) keep.insert(keep.end(), ev.begin(), ev.end());
        std::vector<audio::Sound> released;
        for (size_t c = first; c < waves_.size(); ++c)
            for (const auto& ev : waves_[c])
                for (audio::Sound w : ev)
                    if (std::find(keep.begin(), keep.end(), w) == keep.end() &&
                        std::find(released.begin(), released.end(), w) == released.end()) {
                        audio_->release(w);
                        released.push_back(w);
                    }
    }
    cues_.resize(first);
    for (auto it = dialogChars_.begin(); it != dialogChars_.end();) it = it->first >= first ? dialogChars_.erase(it) : std::next(it);
    if (waves_.size() > first) waves_.resize(first);
    if (resident_.size() > first) resident_.resize(first);
    if (pinned_.size() > first) pinned_.resize(first);
    return removed;
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

const cuedata::CueDef* SoundCues::instanceCue(int id) const {
    for (const Instance& x : live_) if (x.id == id) return &cues_[(size_t)x.cue];
    return nullptr;
}

int SoundCues::oldestInstance(const char* cue) const {
    int c = findCue(cue), best = -1;
    for (const Instance& x : live_) if (x.cue == c && (best < 0 || x.id < best)) best = x.id;
    return best;
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

int SoundCues::playDialog(const char* name, const Emitter& em, const std::string& dialogCharacter) {
    nextDialogChar_ = dialogCharacter;
    const int id = play(name, em, 0.0f);
    nextDialogChar_.clear();
    return id;
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
    if (cues_[(size_t)c].streamed && ((size_t)c >= resident_.size() || !resident_[(size_t)c])) {
        LARGE_INTEGER_T s0 = nowTicks();
        loadWaves((size_t)c, contentRoot_);
        LOG_INFO("sound cues: streamed %s decoded in %.1f ms", name, ticksToMs(nowTicks() - s0));
    }
    if ((size_t)c < pinned_.size()) pinned_[(size_t)c] = 0;     // played: normal release rule from now on
    const CueDef& cd = cues_[(size_t)c];
    Instance in;
    in.cue = c; in.id = nextId_++; in.age = 0.0f; in.distM = distM; in.param = param;
    in.owner = em.owner; in.offset = em.offset; in.socket = em.socket; in.pos = em.pos;
    in.dialogChar = nextDialogChar_;
    nextDialogChar_.clear();
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
    if (cd.rootLoop && cd.loopEnd > cd.loopStart) in.looping = true;   // the timeline wraps forever
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
    // HmDialogComponent [HIGH]: a dialogue cue holds one wave event per DialogCharacter; the component plays the events
    // of its own character (events with no character always play). Non-dialogue plays (no character) play every event.
    if (!in.dialogChar.empty()) {
        auto dc = dialogChars_.find((size_t)in.cue);
        if (dc != dialogChars_.end() && (size_t)e < dc->second.size() && !dc->second[(size_t)e].empty() &&
            dc->second[(size_t)e] != in.dialogChar)
            return;
    }
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
    for (Instance& in : live_) {
        in.age += dt;
        const CueDef& cd = cues_[(size_t)in.cue];
        if (cd.rootLoop && cd.loopEnd > cd.loopStart && in.age >= cd.loopEnd && in.fade < 0.0f) {
            // Root playback-time wrap (RE A6): t = fmod(t - LoopStart, LoopEnd - LoopStart) + LoopStart; the wave
            // events from LoopStart on run again on the cue's timeline. Finished voices are dropped from the list.
            in.age = cd.loopStart + std::fmod(in.age - cd.loopEnd, cd.loopEnd - cd.loopStart);
            if (audio_->reportsVoices())
                in.voices.erase(std::remove_if(in.voices.begin(), in.voices.end(),
                                               [&](const VoiceRef& r) { return !audio_->isPlaying(r.v); }), in.voices.end());
            for (int e = 0; e < (int)cd.events.size(); ++e)
                if (cd.events[(size_t)e].time >= cd.loopStart) pending_.push_back({in.id, cd.events[(size_t)e].time, e});
        }
    }
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
            // One-shot: retire once every voice has finished (the AudioComponent lives until its sound ends;
            // attached voices keep following their owner until then). kInstanceTail bounds it only for backends
            // that cannot report voices. (It used to bound every instance: a long one-shot - the 380 s frontend
            // music, long dialogue - was dropped 10 s in while its voice kept sounding, unmanaged: no mute, fade,
            // stop or "already playing" check reached it.)
            const bool reports = audio_->reportsVoices();
            bool sounding = !reports;                   // unknown: keep following until kInstanceTail
            for (const VoiceRef& r : in.voices) if (audio_->isPlaying(r.v)) { sounding = true; break; }
            if (!sounding || (!reports && in.age > lastEventTime(cues_[(size_t)in.cue]) + kInstanceTail)) {
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
    releaseIdleStreams();
}

// Streamed cues (music): the decoded waves go once the last instance has ended (a prefetched, not yet played cue
// stays pinned).
int SoundCues::releaseIdleStreams() {
    int n = 0;
    for (size_t c = 0; c < cues_.size(); ++c) {
        if (!cues_[c].streamed || c >= resident_.size() || !resident_[c]) continue;
        if (c < pinned_.size() && pinned_[c]) continue;                // prefetched, waiting for its first play
        bool used = false;
        for (const Instance& in : live_) if ((size_t)in.cue == c) { used = true; break; }
        if (!used) { releaseWaves(c); ++n; }
    }
    return n;
}

} // namespace game
