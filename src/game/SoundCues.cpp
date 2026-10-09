#include "game/SoundCues.h"
#include <cstdio>
#include <cctype>
#include <set>
#include "assets/Json.h"
#include "core/Log.h"
#include "platform/CpuPreference.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <filesystem>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <thread>
#include <atomic>
#include <memory>

namespace game {
using namespace cuedata;
namespace {

using LARGE_INTEGER_T = long long;
LARGE_INTEGER_T nowTicks() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
double ticksToMs(LARGE_INTEGER_T t) { return (double)t / 1e6; }

// Persistent decode lanes (one thread each, created once, never joined). Worker decodes used to start a new thread per
// selection warm-up / level bank (std::async): each exited thread left its allocator segments abandoned, and the PCM later
// freed there stayed committed - the process grew a few tens of MB per match. Two lanes keep the old concurrency: a level's
// bank loads while a selection warm-up decodes. Tasks run in submission order per lane.
class DecodeLane {
public:
    bool background = false;                         // run below normal priority (the bulk-decode pool)
    std::future<void> submit(std::function<void()> fn) {
        std::packaged_task<void()> task(std::move(fn));
        std::future<void> f = task.get_future();
        {
            std::lock_guard<std::mutex> lk(mx_);
            if (!started_) { started_ = true; std::thread([this] { run(); }).detach(); }
            q_.push_back(std::move(task));
        }
        cv_.notify_one();
        return f;
    }
private:
    void run() {
        if (background) platform::lowerCurrentThreadPriority();
        for (;;) {
            std::packaged_task<void()> task;
            {
                std::unique_lock<std::mutex> lk(mx_);
                cv_.wait(lk, [this] { return !q_.empty(); });
                task = std::move(q_.front());
                q_.pop_front();
            }
            task();
        }
    }
    std::mutex mx_;
    std::condition_variable cv_;
    std::deque<std::packaged_task<void()>> q_;
    bool started_ = false;
};
// Leaked on purpose: detached lanes may still wait on them during static destruction at exit.
DecodeLane& warmLane() { static DecodeLane* l = new DecodeLane; return *l; }
DecodeLane& levelLane() { static DecodeLane* l = new DecodeLane; return *l; }

// Bulk decodes (a cue set at load, a level bank, a selection warm-up) fan out over a small pool of persistent lanes: the
// original banks decode at ~8 ms per MB of PCM, so one thread would add seconds to a load. Blocks until all are loaded
// (load() caches by path). Never called on the main thread during play.
void parallelLoad(audio::IAudio* a, const std::vector<std::string>& paths) {
    static const int kLanes = [] { const unsigned h = std::thread::hardware_concurrency(); return (int)std::max(2u, std::min(8u, h / 2)); }();
    static DecodeLane* pool = [] { auto* p = new DecodeLane[(size_t)kLanes]; for (int l = 0; l < kLanes; ++l) p[l].background = true; return p; }();
    if (paths.size() < 4) { for (const std::string& p : paths) a->load(p); return; }
    auto next = std::make_shared<std::atomic<size_t>>(0);
    const auto list = std::make_shared<std::vector<std::string>>(paths);
    std::vector<std::future<void>> done;
    for (int l = 0; l < kLanes; ++l)
        done.push_back(pool[l].submit([a, next, list] {
            for (size_t i; (i = next->fetch_add(1)) < list->size();) a->load((*list)[i]);
        }));
    for (std::future<void>& f : done) f.wait();
}

const CueDef kCues[] = {
#include "game/SoundCues.inc"
};

// FSB4 sample-header loop regions of the looping vehicle waves (AssetTools vehicle_audio_loops.json).
// headerLoopFlag is informational only: loop enable is the wave event's bLooping (RE d50c2a9).
struct FsbLoop { const char* wav; int rate, channels; uint32_t totalSamples, loopStart, loopEnd; bool headerLoopFlag; };
#include "game/VehicleLoops.inc"


constexpr float UU = 0.01f;
constexpr long long kDeferDecodeBytes = 2ll * 1024 * 1024;   // streamed cues above this decode on the worker at play
constexpr long long kPrePickBytes = 16ll * 1024 * 1024;      // streamed cues above this decode only the waves that will play
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

unsigned long long SoundCues::nameHash(const char* a, const char* b) {
    unsigned long long h = 1469598103934665603ull;
    for (const char* p = a; *p; ++p) { h ^= (unsigned char)*p; h *= 1099511628211ull; }
    for (const char* p = b; *p; ++p) { h ^= (unsigned char)*p; h *= 1099511628211ull; }
    return h;
}

int SoundCues::findCue(const char* name) const {
    {
        auto it = nameIndex_.find(nameHash(name));
        if (it != nameIndex_.end()) for (int i : it->second) if (cues_[(size_t)i].name == name) return i;
    }
    // Full asset names of the cues the compiled table keeps under a short name (tools/systems/gen_cues.py short()):
    // a character / weapon profile names them by package.
    static const struct { const char* pkg; const char* prefix; } kAlias[] = {
        {"BL_WPN_GUN_ION_BLASTER.", ""}, {"BL_WPN_FOLEY.", "FOLEY."}, {"BL_VEH_OPTIMUS_PRIME.", ""}, {"BL_VEH_SOUNDWAVE.", ""}};
    for (const auto& a : kAlias) {
        const size_t n = std::strlen(a.pkg);
        if (std::strncmp(name, a.pkg, n) != 0) continue;
        // prefix + rest, compared without building a string
        auto it = nameIndex_.find(nameHash(a.prefix, name + n));
        if (it == nameIndex_.end()) continue;
        const size_t pl = std::strlen(a.prefix);
        for (int i : it->second) {
            const std::string& cn = cues_[(size_t)i].name;
            if (!cues_[(size_t)i].mapBank && cn.size() == pl + std::strlen(name + n) && cn.compare(0, pl, a.prefix) == 0 &&
                cn.compare(pl, std::string::npos, name + n) == 0)
                return i;
        }
    }
    return -1;
}

const CueDef* SoundCues::cueDef(const char* name) const {
    int c = findCue(name);
    return c >= 0 ? &cues_[(size_t)c] : nullptr;
}

void SoundCues::ensurePick(size_t c) {
    if (c >= cues_.size() || !cues_[c].streamed) return;
    if (pick_.size() <= c) pick_.resize(c + 1);
    if (pickOff_.size() <= c) pickOff_.resize(c + 1, 0);
    if (pickUsed_.size() <= c) pickUsed_.resize(c + 1, 0);
    if (!pick_[c].empty() || pickOff_[c] || waveBytes(c) <= kPrePickBytes) return;
    for (const EventDef& e : cues_[c].events)
        pick_[c].push_back(e.waves.size() > 1 ? std::rand() % (int)e.waves.size() : -1);   // launch's choice, made now
}

bool SoundCues::costlyLoad(size_t c) const {
    if (!audio_ || c >= cues_.size()) return false;
    for (size_t ei = 0; ei < cues_[c].events.size(); ++ei)
        for (size_t wi = 0; wi < cues_[c].events[ei].waves.size(); ++wi) {
            if (!wanted(c, ei, wi)) continue;
            const std::string& f = cues_[c].events[ei].waves[wi];
            const bool abs = f.size() > 1 && (f[1] == ':' || f[0] == '/');
            if (audio_->loadIsCostly(abs ? f : contentRoot_ + f)) return true;
        }
    return false;
}

bool SoundCues::wanted(size_t c, size_t e, size_t w) const {
    if (c >= pick_.size() || e >= pick_[c].size() || pick_[c][e] < 0) return true;
    return (int)w == pick_[c][e];
}

void SoundCues::loadWaves(size_t c, const std::string& contentRoot) {
    if (waves_.size() <= c) waves_.resize(c + 1);
    if (resident_.size() <= c) resident_.resize(c + 1, 0);
    ensurePick(c);
    waves_[c].clear();
    resident_[c] = 1;
    for (size_t ei = 0; ei < cues_[c].events.size(); ++ei) {
        const EventDef& e = cues_[c].events[ei];
        std::vector<audio::Sound> w;
        for (size_t wi = 0; wi < e.waves.size(); ++wi) {
            if (!wanted(c, ei, wi)) continue;
            const std::string& f = e.waves[wi];
            const bool abs = f.size() > 1 && (f[1] == ':' || f[0] == '/');       // a localized twin outside content/
            audio::Sound s = audio_ ? audio_->load(abs ? f : contentRoot + f) : audio::kInvalidSound;
            if (s != audio::kInvalidSound) w.push_back(s);
        }
        waves_[c].push_back(w);
    }
}

void SoundCues::load(audio::IAudio* a, const std::string& contentRoot) {
    audio_ = a;
    if (std::find(tables().begin(), tables().end(), this) == tables().end()) tables().push_back(this);
    contentRoot_ = contentRoot;
    cues_.assign(std::begin(kCues), std::end(kCues));
    nameIndex_.clear();
    for (size_t c = 0; c < cues_.size(); ++c) nameIndex_[nameHash(cues_[c].name.c_str())].push_back((int)c);
    waves_.clear();
    if (!a) return;
    int ok = 0, total = 0;
    resident_.assign(cues_.size(), 0);
    waves_.assign(cues_.size(), {});
    if (a->threadSafeLoad()) {   // decode the resident set in parallel first; the loop below then takes cached handles
        std::vector<std::string> paths;
        for (const CueDef& cd : cues_) {
            if (cd.streamed) continue;
            for (const EventDef& e : cd.events)
                for (const std::string& f : e.waves) {
                    const bool abs = f.size() > 1 && (f[1] == ':' || f[0] == '/');
                    paths.push_back(abs ? f : contentRoot + f);
                }
        }
        parallelLoad(a, paths);
    }
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
SoundCues::LocStats& SoundCues::locStats() { static LocStats s; return s; }

std::string SoundCues::localizedWave(const std::string& rel, const std::string& owner, const std::string& contentRoot, bool account) {
    const char* lang = std::getenv("WFC_LANGUAGE");
    std::string twin = lang && *lang ? lang : "INT";
    for (char& ch : twin) ch = (char)std::toupper((unsigned char)ch);
    if (twin == "INT") twin = "int";
    const char* locRoot = std::getenv("WFC_LOC_ROOT");
    const std::string alt = (locRoot && *locRoot ? std::string(locRoot) + "/" : contentRoot + "_LOC/") + twin + "/" + rel;
    if (std::FILE* fp = std::fopen((alt.size() > 1 && (alt[1] == ':' || alt[0] == '/') ? alt : contentRoot + alt).c_str(), "rb")) {
        std::fclose(fp);
        if (account) ++locStats().twin;
        return alt;
    }
    std::string o = owner, t = twin;
    for (char& ch : o) ch = (char)std::toupper((unsigned char)ch);
    for (char& ch : t) ch = (char)std::toupper((unsigned char)ch);
    if (o == t) { if (account) ++locStats().merged; return rel; }
    if (!account) return std::string();
    ++locStats().skipped;
    static std::set<std::string> warned;
    if (warned.insert(rel).second)
        LOG_WARN("sound cues: localized wave %s: no _LOC_%s twin extracted (the extracted copy is _LOC_%s) - not played",
                 rel.c_str(), twin.c_str(), owner.c_str());
    return std::string();
}

int SoundCues::addCues(const assets::Json& cues, const std::string& contentRoot) {
    int added = 0;
    std::vector<size_t> toLoad;                     // resident (non-streamed) cues: waves loaded after the parse
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
        nameIndex_[nameHash(cues_.back().name.c_str())].push_back((int)cues_.size() - 1);
        bool anyChar = false;
        for (const std::string& ch : dialogChars) anyChar = anyChar || !ch.empty();
        if (anyChar) dialogChars_[cues_.size() - 1] = dialogChars;
        if (d.streamed) {
            if (waves_.size() < cues_.size()) waves_.resize(cues_.size());
            if (resident_.size() < cues_.size()) resident_.resize(cues_.size(), 0);
            waves_.back().assign(d.events.size(), {});
        } else toLoad.push_back(cues_.size() - 1);
        ++added;
    }
    // Resident cues whose waves are plain WAV reads (or already decoded) load now. Those that would decode an original bank go
    // to one pooled worker decode for the bank: a level load (the frontend menus have no loading screen) never waits for it;
    // a play before it finishes starts when the waves are adopted, as the original streamed them.
    BankWarm bw;
    for (size_t c : toLoad) {
        if (audio_ && audio_->threadSafeLoad() && costlyLoad(c)) {
            bw.cues.push_back(c);
            for (const EventDef& e : cues_[c].events)
                for (const std::string& f : e.waves) {
                    const bool abs = f.size() > 1 && (f[1] == ':' || f[0] == '/');
                    bw.paths.push_back(abs ? f : contentRoot + f);
                }
        } else loadWaves(c, contentRoot);
    }
    if (!bw.cues.empty()) {
        audio::IAudio* a = audio_;
        const std::vector<std::string> paths = bw.paths;
        bw.done = levelLane().submit([a, paths] { parallelLoad(a, paths); });
        LOG_INFO("sound cues: %zu map cues (%zu waves) decoding on the worker; they start when ready", bw.cues.size(), bw.paths.size());
        bankWarm_.push_back(std::move(bw));
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
    pinned_[(size_t)c] = 1;
    if ((size_t)c < resident_.size() && resident_[(size_t)c]) return true;
    if (isWarming((size_t)c) || startWarm((size_t)c)) return true;   // already warming / now warming on a worker
    loadWaves((size_t)c, contentRoot_);
    return true;
}

long long SoundCues::waveBytes(size_t c) {
    if (waveBytes_.size() <= c) waveBytes_.resize(c + 1, -1);
    if (waveBytes_[c] >= 0) return waveBytes_[c];
    long long n = 0;
    for (const EventDef& e : cues_[c].events)
        for (const std::string& f : e.waves) {
            const bool abs = f.size() > 1 && (f[1] == ':' || f[0] == '/');
            std::error_code ec;
            const std::string p = abs ? f : contentRoot_ + f;
            const auto sz = std::filesystem::file_size(p, ec);
            if (!ec) n += (long long)sz;
            else if (p.size() > 4) {                                     // no WAV in the package: the original bank (~16:1)
                const auto bs = std::filesystem::file_size(p.substr(0, p.size() - 4) + ".fsb", ec);
                if (!ec) n += (long long)bs * 16;
            }
        }
    return waveBytes_[c] = n;
}

bool SoundCues::isWarming(size_t c) const {
    for (const Warm& w : warming_) if (w.cue == c) return true;
    for (const BankWarm& w : bankWarm_) for (size_t x : w.cues) if (x == c) return true;
    return false;
}

bool SoundCues::adoptBankWarm() {
    bool any = false;
    for (size_t i = 0; i < bankWarm_.size();) {
        BankWarm& w = bankWarm_[i];
        if (w.done.valid() && w.done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++i; continue; }
        if (w.done.valid()) w.done.wait();
        for (size_t c : w.cues) if (c < cues_.size()) loadWaves(c, contentRoot_);   // cache hits
        bankWarm_.erase(bankWarm_.begin() + (long)i);
        any = true;
    }
    return any;
}

bool SoundCues::startWarm(size_t c) {
    if (!audio_ || !audio_->threadSafeLoad()) return false;
    ensurePick(c);
    std::vector<std::string> paths;
    long long bytes = 0;
    for (size_t ei = 0; ei < cues_[c].events.size(); ++ei)
        for (size_t wi = 0; wi < cues_[c].events[ei].waves.size(); ++wi) {
            if (!wanted(c, ei, wi)) continue;
            const std::string& f = cues_[c].events[ei].waves[wi];
            const bool abs = f.size() > 1 && (f[1] == ':' || f[0] == '/');
            paths.push_back(abs ? f : contentRoot_ + f);
            std::error_code ec;
            const auto sz = std::filesystem::file_size(paths.back(), ec);
            if (!ec) bytes += (long long)sz;
            else {
                const auto bs = std::filesystem::file_size(paths.back().substr(0, paths.back().size() - 4) + ".fsb", ec);
                if (!ec) bytes += (long long)bs * 16;
            }
        }
    if (paths.empty()) return false;
    audio::IAudio* a = audio_;
    Warm w;
    w.cue = c; w.paths = paths; w.name = cues_[c].name; w.bytes = bytes; w.start = std::chrono::steady_clock::now();
    // One worker decode at a time (decodeMutex): a match's final-stretch + end music (up to ~250 MB of waves each) must not
    // decode concurrently - peak memory, and the device lock they share.
    w.done = warmLane().submit([a, paths] {   // the lane serialises warm-ups; each fans out over the pool
        static std::mutex decodeMutex;
        std::lock_guard<std::mutex> lk(decodeMutex);
        parallelLoad(a, paths);
    });
    LOG_INFO("sound cues: worker decode of %s started (%.1f MB of waves)", w.name.c_str(), w.bytes / 1048576.0);
    warming_.push_back(std::move(w));
    return true;
}

void SoundCues::timedWait(std::future<void>& f, const char* why, const std::string& what) {
    if (!f.valid()) return;
    if (f.wait_for(std::chrono::seconds(0)) == std::future_status::ready) { f.wait(); return; }
    const auto t0 = std::chrono::steady_clock::now();
    f.wait();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (ms > 5.0) LOG_WARN("sound cues: main thread waited %.1f ms on the worker decode of %s (%s)", ms, what.c_str(), why);
}

bool SoundCues::pathNeeded(const std::string& path) const {
    for (const Warm& w : warming_) for (const std::string& p : w.paths) if (p == path) return true;
    for (const LevelWarm& w : levelWarm_) for (const std::string& p : w.paths) if (p == path) return true;
    for (const BankWarm& w : bankWarm_) for (const std::string& p : w.paths) if (p == path) return true;
    return false;
}

void SoundCues::orphanWarm(Warm& w) {
    LOG_INFO("sound cues: worker decode of %s abandoned (%.0f ms in; released when it finishes, nothing waits)", w.name.c_str(),
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - w.start).count());
    orphans_.push_back({w.paths, w.name, std::move(w.done)});
}

bool SoundCues::ownsSample(audio::Sound h) const {
    for (size_t c = 0; c < waves_.size(); ++c)
        for (const auto& ev : waves_[c]) for (audio::Sound x : ev) if (x == h) return true;
    return false;
}

int SoundCues::processOrphans() {
    int released = 0;
    for (SoundCues* t : tables()) if (!t->orphans_.empty()) released += t->processOwnOrphans();
    return released;
}

int SoundCues::processOwnOrphans() {
    int released = 0;
    for (size_t i = 0; i < orphans_.size();) {
        Orphan& o = orphans_[i];
        if (o.done.valid() && o.done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++i; continue; }
        if (o.done.valid()) o.done.wait();
        if (audio_)
            for (const std::string& p : o.paths) {
                bool needed = false;
                for (const SoundCues* t : tables()) needed = needed || t->pathNeeded(p);   // a live decode / prefetch wants it
                if (needed) continue;
                const audio::Sound h = audio_->cached(p);                   // the orphan's handle; never decodes here (main thread)
                if (h == audio::kInvalidSound) continue;
                bool owned = false;
                for (const SoundCues* t : tables()) owned = owned || t->ownsSample(h);
                if (!owned) { audio_->release(h); ++released; }
            }
        orphans_.erase(orphans_.begin() + (long)i);
    }
    return released;
}

void SoundCues::startInstance(Instance& in) {
    const CueDef& cd = cues_[(size_t)in.cue];
    const int id = in.id;
    for (int e = 0; e < (int)cd.events.size(); ++e) {
        if (cd.events[(size_t)e].time <= 0.0f) { Instance* p = find(id); if (p) launch(*p, e); }
        else pending_.push_back({id, cd.events[(size_t)e].time, e});
    }
}

int SoundCues::warmCueWaves(const assets::Json& cues, const std::string& contentRoot, const std::string& tag) {
    if (!audio_ || !audio_->threadSafeLoad()) return 0;
    std::vector<std::string> paths;
    for (const auto& kv : cues.obj) {
        if (kv.second["streamed"].asBool(false) || findCue(kv.first.c_str()) >= 0) continue;   // decoded on play / loaded
        const assets::Json& kids = kv.second["tree"]["children"];
        for (size_t i = 0; i < kids.size(); ++i) {
            if (kids[i]["class"].asString() != "SoundNodeWaveEvent") continue;
            const assets::Json& waves = kids[i]["children"];
            for (size_t w = 0; w < waves.size(); ++w) {
                std::string f = waves[w]["wav"].asString();
                if (f.rfind("content/", 0) == 0) f = f.substr(8);
                const std::string loc = waves[w]["loc"].asString();
                if (!loc.empty()) f = localizedWave(f, loc, contentRoot, false);   // the twin the load will pick
                if (f.empty()) continue;
                const bool abs = f.size() > 1 && (f[1] == ':' || f[0] == '/');
                paths.push_back(abs ? f : contentRoot + f);
            }
        }
    }
    if (paths.empty()) return 0;
    audio::IAudio* a = audio_;
    const int n = (int)paths.size();
    levelWarm_.push_back({tag, paths, levelLane().submit([a, paths] { parallelLoad(a, paths); })});
    return n;
}

void SoundCues::unpin(const char* cue) {
    const int c = findCue(cue);
    if (c >= 0 && (size_t)c < pinned_.size()) pinned_[(size_t)c] = 0;
}

void SoundCues::waitWarm(const std::string& tag) {
    for (LevelWarm& w : levelWarm_) if (w.tag == tag) timedWait(w.done, "level load waits for its own prefetch", tag);
}

int SoundCues::releaseWarmExcept(const std::string& keep) {
    int released = 0;
    for (size_t i = 0; i < levelWarm_.size();) {
        LevelWarm& w = levelWarm_[i];
        const bool busy = w.done.valid() && w.done.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
        if (busy) {   // still decoding: never waited for. Another level's: released when done. The kept level's own: its files
                      // are owned by the level's cues or wanted by their bank warm, so the orphan pass releases nothing of them.
            orphans_.push_back({w.paths, w.tag, std::move(w.done)});
            levelWarm_.erase(levelWarm_.begin() + (long)i);
            continue;
        }
        timedWait(w.done, "releaseWarmExcept", w.tag);
        if (w.tag != keep && audio_)
            for (const std::string& p : w.paths) {
                const audio::Sound h = audio_->cached(p);                 // resident only: never decode to release
                if (h == audio::kInvalidSound) continue;
                bool owned = false;
                for (size_t c = 0; c < waves_.size() && !owned; ++c)
                    for (const auto& ev : waves_[c]) { for (audio::Sound x : ev) if (x == h) { owned = true; break; } if (owned) break; }
                if (!owned) { audio_->release(h); ++released; }
            }
        levelWarm_.erase(levelWarm_.begin() + (long)i);
    }
    return released;
}

void SoundCues::adoptWarm(bool wait, long onlyCue) {
    for (size_t i = 0; i < warming_.size();) {
        Warm& w = warming_[i];
        if (onlyCue >= 0 && w.cue != (size_t)onlyCue) { ++i; continue; }
        if (!wait && w.done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++i; continue; }
        auto t0 = std::chrono::steady_clock::now();
        timedWait(w.done, "adopt", w.name);
        auto t1 = std::chrono::steady_clock::now();
        const size_t c = w.cue;
        LOG_INFO("sound cues: worker decode of %s ready after %.0f ms", w.name.c_str(),
                 std::chrono::duration<double, std::milli>(t1 - w.start).count());
        warming_.erase(warming_.begin() + (long)i);
        if (c >= resident_.size() || !resident_[c]) loadWaves(c, contentRoot_);   // device cache hits: no decode here
        static const bool dbg = std::getenv("WFC_PREFETCHLOG") != nullptr;
        if (dbg) LOG_INFO("prefetch adopt %s: wait %.2f ms, adopt %.2f ms", cues_[c].name.c_str(),
                          std::chrono::duration<double, std::milli>(t1 - t0).count(),
                          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count());
    }
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
    if (c < pick_.size()) { pick_[c].clear(); pickUsed_[c] = 0; pickOff_[c] = 0; }   // the next warm-up picks again
}

int SoundCues::mapCueCount() const {
    int n = 0;
    for (const cuedata::CueDef& c : cues_) n += c.mapBank ? 1 : 0;
    return n;
}

int SoundCues::unloadMapCues() {
    // Finished worker decodes get an owner (adopted); unfinished ones are ORPHANED, not waited for - a level unload right
    // after the final-stretch / end music started must not block the main thread on a 250 MB decode (08o freeze report).
    adoptWarm(false);
    for (Warm& w : warming_) orphanWarm(w);
    warming_.clear();
    for (BankWarm& w : bankWarm_) orphans_.push_back({w.paths, "map bank", std::move(w.done)});
    bankWarm_.clear();
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
    for (auto it = nameIndex_.begin(); it != nameIndex_.end();) {          // drop the unloaded cues from the name index
        std::vector<int>& v = it->second;
        while (!v.empty() && v.back() >= (int)first) v.pop_back();      // ascending: the removed ones are at the end
        it = v.empty() ? nameIndex_.erase(it) : std::next(it);
    }
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
    if (!warming_.empty()) adoptWarm(false, c);        // a finished prefetch of this cue: take it now (no wait)
    if (!bankWarm_.empty()) adoptBankWarm();
    if ((size_t)c < pick_.size() && !pick_[(size_t)c].empty() && pickUsed_[(size_t)c] && (size_t)c < resident_.size() &&
        resident_[(size_t)c]) {                         // played before with this pick: the original picks again per play
        pick_[(size_t)c].clear(); pickOff_[(size_t)c] = 1; resident_[(size_t)c] = 0;   // -> decode the full set below
    }
    bool deferred = false;
    if (!cues_[(size_t)c].streamed && ((size_t)c >= resident_.size() || !resident_[(size_t)c]) && isWarming((size_t)c))
        deferred = true;                               // a map cue whose bank is still decoding: starts when adopted
    if (cues_[(size_t)c].streamed && ((size_t)c >= resident_.size() || !resident_[(size_t)c])) {
        // Not resident (no prefetch, or still decoding): on a thread-safe backend decode on the worker and start the
        // instance when the waves are adopted (a short start delay instead of a 150-700 ms main-thread stall - the match
        // final-stretch music); otherwise decode now.
        // An original bank to decode (no WAV, or WFC_AUDIO_SOURCE=fsb) never decodes here on the main thread: worker, start when
        // ready (a few ms) - the original streamed these from disk.
        if (isWarming((size_t)c) || ((waveBytes((size_t)c) > kDeferDecodeBytes || costlyLoad((size_t)c)) && startWarm((size_t)c)))
            deferred = true;
        else {
            LARGE_INTEGER_T s0 = nowTicks();
            loadWaves((size_t)c, contentRoot_);
            LOG_INFO("sound cues: streamed %s decoded in %.1f ms", name, ticksToMs(nowTicks() - s0));
        }
    }
    if ((size_t)c < pick_.size() && !pick_[(size_t)c].empty()) pickUsed_[(size_t)c] = 1;   // this play takes the pick
    if ((size_t)c < pinned_.size()) pinned_[(size_t)c] = 0;     // played: normal release rule from now on
    const CueDef& cd = cues_[(size_t)c];
    Instance in;
    in.cue = c; in.id = nextId_++; in.age = 0.0f; in.distM = distM; in.param = param;
    in.owner = em.owner; in.offset = em.offset; in.socket = em.socket; in.pos = em.pos;
    in.dialogChar = nextDialogChar_;
    nextDialogChar_.clear();
    resolve(in);
    // AActor::PlaySound -> USoundCue::IsAudible [UE3 engine, HIGH]: a positional one-shot beyond the cue's audible distance
    // (DistanceMax) from the listener is not started at all - it would only hold a channel at zero gain (at 16 v 16+ those
    // inaudible far-away bot shots / steps were most of the 96-channel pool). Loops start regardless (they may come into
    // range while playing), as do 2D / UI sounds.
    if (in.owner != kUI && cd.spatial != Spatial::TwoD && !cd.rootLoop && cd.distMaxUU > 0.0f) {
        bool loops = false;
        for (const EventDef& ev : cd.events) if (ev.loop) { loops = true; break; }
        const core::Vec3 d = in.pos - listener_;
        const float maxM = cd.distMaxUU * UU;
        if (!loops && core::dot(d, d) > maxM * maxM) { ++inaudibleSkipped_; return -1; }
    }
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
        const core::Vec3 d = in.pos - listener_;
        const float maxM = cd.distMaxUU * UU;
        if (cd.distMaxUU > 0.0f && core::dot(d, d) > maxM * maxM) in.occlStale = true;   // inaudible loop: no ray yet
        else in.occl = in.occlTarget = occlusion_(listener_, in.pos, in.owner) ? 1.0f : 0.0f;
        in.occlCheck = kOcclCheck * (float)(in.id % 8) / 8.0f;
    }
    for (const EventDef& e : cd.events) if (e.loop) in.looping = true;
    if (cd.rootLoop && cd.loopEnd > cd.loopStart) in.looping = true;   // the timeline wraps forever
    in.waiting = deferred;
    live_.push_back(in);
    const int id = in.id;
    if (!deferred) startInstance(live_.back());
    else {
        static const bool log = std::getenv("WFC_CUELOG") != nullptr || std::getenv("WFC_AUDIOTIME") != nullptr;
        if (log) LOG_INFO("sound cues: streamed %s decoding on the worker (instance %d starts when ready)", name, id);
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
    // Silent one-shot layer: AUTHORED silence only (the -96 dB / distance-layer curves / envelope). Runtime level - a
    // fade-in starting at 0, the instance volume, a sound-group slider at 0 - must not drop the voice: it would never
    // sound once the level rises (a deferred music start fades in from 0).
    const float authored = ref.baseGain * ed.stereoGain * evalCurve(ed.volCurve, x, 1.0f) * evalCurve(ed.envVol, in.age, 1.0f);
    if (authored <= 0.0f && !ed.loop) return;      // silent one-shot layer (distance layering)
    float gain = authored * gainOf(in) * dbToLinear(kOcclDb * in.occl);
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
    p.protect = in.owner >= 0 && in.owner < kParticipantOwnerBase;   // attached to the local pawn / its weapon
    ref.v = audio_->playVoice(s, p);
    if (ref.v != audio::kInvalidVoice) in.voices.push_back(ref);
    else if (in.owner >= 0 && in.owner < kParticipantOwnerBase) {     // the local player's own sound got no channel
        static int warned = 0;
        if (warned++ < 20) LOG_WARN("sound cues: no channel for the local player's %s ev%d (all 96 busy with more important sounds)",
                                    cd.name.c_str(), e);
    }
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
void SoundCues::refresh(Instance& in, int resolved) {
    const CueDef& cd = cues_[(size_t)in.cue];
    float x = paramFor(in);
    float fade = in.fade > 0.0f ? core::clampf(in.fadeLeft / in.fade, 0.0f, 1.0f) : 1.0f;
    const bool moved = (resolved < 0 ? resolve(in) : resolved != 0) || in.posDirty;
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
    if (fade > 0.0f && !in->waiting) { in->fade = fade; in->fadeLeft = fade; return; }   // a not-started instance: nothing to fade
    for (const VoiceRef& r : in->voices) audio_->stopVoice(r.v);
    for (size_t i = 0; i < live_.size(); ++i)
        if (live_[i].id == id) { retire(i); break; }
}

void SoundCues::tick(float dt) {
    if (!warming_.empty()) adoptWarm(false);
    if (!bankWarm_.empty()) adoptBankWarm();
    processOrphans();                                  // this table's and any other (unticked) table's orphaned decodes
    for (size_t i = 0; i < live_.size(); ++i) {
        Instance& in = live_[i];
        if (!in.waiting || (size_t)in.cue >= resident_.size() || !resident_[(size_t)in.cue]) continue;
        in.waiting = false; in.age = 0.0f;                       // its timeline starts now
        resolve(in);
        startInstance(in);
    }
    if (!audio_) return;
    // Mixer: timers / Duration expiry, then linear parameter ramps; MASTER_WET goes to the backend as-is
    // (the mixer ramp is the only fade).
    mixer_.tick(dt);
    if (mixer_.environmentChanged()) audio_->setEnvironment(mixer_.environment(), 0.0f);
    for (Instance& in : live_) {
        if (in.waiting) continue;                                 // not started: no timeline yet
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
        } else if (!in.waiting && !in.looping && in.age > lastEventTime(cues_[(size_t)in.cue])) {
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
            // Beyond the cue's audible distance the voices are virtual (gain 0): the line check is skipped (the 32 v 32 cost
            // was mostly these). Back in range it is checked at once and takes its value directly - what the continuous
            // checks would have reached - so nothing audible changes.
            const core::Vec3 d = in.pos - listener_;
            const float maxM = cd.distMaxUU * UU;
            if (cd.distMaxUU > 0.0f && core::dot(d, d) > maxM * maxM) {
                in.occlStale = true;
            } else {
                in.occlCheck -= dt;
                if (in.occlStale || in.occlCheck <= 0.0f) {
                    in.occlCheck = kOcclCheck;
                    in.occlTarget = occlusion_(listener_, in.pos, in.owner) ? 1.0f : 0.0f;
                    if (in.occlStale) in.occl = in.occlTarget;
                    in.occlStale = false;
                }
                in.occl += core::clampf(in.occlTarget - in.occl, -dt / kOcclTime, dt / kOcclTime);
            }
        }
        // Dormant (zero work while inaudible): a positional instance more than 10 % + 2 m beyond its audible distance - its
        // voices are already culled (virtual) by the device at that distance - skips the per-step gain / curve / envelope
        // evaluation and the device update; only its position is followed. Fades, envelopes and curves are functions of time
        // and state, so the refresh on its return (well before the audible boundary) yields exactly what the per-step
        // updates would have. Counts in dormantInstances().
        int resolvedHere = -1;
        if (in.owner != kUI && cd.spatial != Spatial::TwoD && cd.distMaxUU > 0.0f && !in.voices.empty()) {
            resolvedHere = resolve(in) ? 1 : 0;
            const core::Vec3 d = in.pos - listener_;
            const float far = cd.distMaxUU * UU * 1.1f + 2.0f;
            if (core::dot(d, d) > far * far) { in.dormant = true; ++i; continue; }
            if (in.dormant) { in.dormant = false; in.posDirty = true; }   // back: one full refresh (position, gains) now
        }
        refresh(in, resolvedHere);
        ++i;
    }
    releaseIdleStreams();
}

// Streamed cues (music): the decoded waves go once the last instance has ended (a prefetched, not yet played cue
// stays pinned).
int SoundCues::releaseIdleStreams() {
    adoptWarm(false);                                  // warming cues are pinned (never released here): no need to wait
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
