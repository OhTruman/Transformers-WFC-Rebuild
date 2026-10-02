#include "game/SoundMixer.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace game {
namespace {
using PresetDef = SoundMixer::PresetDef;
using CategoryPreset = SoundMixer::CategoryPreset;
#include "game/SoundMixer.inc"
constexpr int kSfxWetVehEngineCount = (int)(sizeof(kSfxWetVehEngine) / sizeof(kSfxWetVehEngine[0]));
constexpr int kMasterWetCount = (int)(sizeof(kMasterWet) / sizeof(kMasterWet[0]));
enum { kVolume = 0 };
bool logOn() { static const bool on = std::getenv("WFC_CUELOG") != nullptr || std::getenv("WFC_MIXERLOG") != nullptr; return on; }
} // namespace

void SoundMixer::Ramp::setTarget(float t, float T) {           // 0x827560A8
    target = t;
    if (T > 0.0f) { rate = (t - value) / T; remaining = T; active = true; }
    else { value = t; active = false; }
}

void SoundMixer::Ramp::update(float dt) {                       // 0x827560F8
    if (!active) return;
    remaining -= dt;
    value += rate * dt;
    bool passed = (rate > 0.0f && value >= target) || (rate < 0.0f && value <= target) || rate == 0.0f;
    if (remaining <= 0.0f || passed) { value = target; active = false; }
}

SoundMixer::SoundMixer()
    : SoundMixer(kPresetDefs, (int)(sizeof(kPresetDefs) / sizeof(kPresetDefs[0])), kSfxWetVehEngine,
                 kSfxWetVehEngineCount, kMasterWet, kMasterWetCount) {}

SoundMixer::SoundMixer(const PresetDef* presets, int n, const CategoryPreset* cat0, int n0, const CategoryPreset* cat1, int n1) {
    presets_.push_back({PresetDef{"Default", 0.0f, 0.5f, 0.5f, -1.0f}});   // built-in (mixer Init 0x82782A18)
    for (int i = 0; i < n; ++i) presets_.push_back({presets[i]});
    cats_[0].name = "SFX_WET_VEH_ENGINE"; cats_[0].table = cat0; cats_[0].count = n0;
    cats_[1].name = "MASTER_WET"; cats_[1].table = cat1; cats_[1].count = n1;
    flush();
}

int SoundMixer::find(const std::string& name) const {
    for (size_t i = 0; i < presets_.size(); ++i) if (name == presets_[i].def.name) return (int)i;
    return -1;
}

bool SoundMixer::isActive(int p) const {
    return std::find(active_.begin(), active_.end(), p) != active_.end();
}

const CategoryPreset* SoundMixer::defines(const Category& c, int p) const {
    for (int i = 0; i < c.count; ++i) if (std::strcmp(c.table[i].name, presets_[(size_t)p].def.name) == 0) return &c.table[i];
    return nullptr;
}

void SoundMixer::flush() {
    active_.clear();
    for (Preset& p : presets_) { p.refs = 0; p.elapsed = 0.0f; }
    for (Category& c : cats_) {
        c.current = 0;
        const CategoryPreset* d = defines(c, 0);
        for (int k = 0; k < 17; ++k) { c.ramps[k] = Ramp{}; c.ramps[k].value = c.ramps[k].target = d ? d->v[k] : 0.0f; }
    }
    enable("Default");
    currentReverb_.clear();
    envDirty_ = true;
}

void SoundMixer::activateReverb(const std::string& preset) {
    if (preset == currentReverb_) return;
    if (!enable(preset)) return;
    if (!currentReverb_.empty()) disable(currentReverb_, false);
    if (logOn()) LOG_INFO("MIXER reverb %s -> %s", currentReverb_.empty() ? "None" : currentReverb_.c_str(), preset.c_str());
    currentReverb_ = preset;
}

bool SoundMixer::enable(const std::string& name) {
    int p = find(name);
    if (p < 0) { if (logOn()) LOG_INFO("MIXER enable %s: unknown preset", name.c_str()); return false; }
    presets_[(size_t)p].elapsed = 0.0f;
    if (!isActive(p)) {
        auto it = active_.begin();
        while (it != active_.end() && presets_[(size_t)*it].def.priority >= presets_[(size_t)p].def.priority) ++it;
        active_.insert(it, p);
        retarget();
    }
    presets_[(size_t)p].refs += 1;
    if (logOn()) LOG_INFO("MIXER enable %s refs=%d active=[%s]", name.c_str(), presets_[(size_t)p].refs, activeList().c_str());
    return true;
}

void SoundMixer::disable(const std::string& name, bool force) {
    int p = find(name);
    if (p < 0 || !isActive(p)) return;
    Preset& pr = presets_[(size_t)p];
    if (force) pr.refs = 1;
    if (pr.refs == 1) {
        active_.erase(std::find(active_.begin(), active_.end(), p));
        retarget();
    }
    pr.refs = std::max(0, pr.refs - 1);
    if (logOn()) LOG_INFO("MIXER disable %s refs=%d active=[%s]", name.c_str(), pr.refs, activeList().c_str());
}

void SoundMixer::retarget() {
    for (int ci = 0; ci < 2; ++ci) {
        Category& c = cats_[ci];
        int target = -1;
        const CategoryPreset* vals = nullptr;
        for (int p : active_) if ((vals = defines(c, p)) != nullptr) { target = p; break; }   // first match wins
        if (target < 0 || target == c.current) continue;                                     // keep / no restart
        const PresetDef& nd = presets_[(size_t)target].def;
        const PresetDef& cd = presets_[(size_t)c.current].def;
        float fade = nd.priority < cd.priority ? cd.fadeOut : nd.fadeIn;
        for (int k = 0; k < 17; ++k) c.ramps[k].setTarget(vals->v[k], fade);
        if (logOn()) LOG_INFO("MIXER %s: %s -> %s over %.2f s", c.name, cd.name, nd.name, fade);
        c.current = target;
        if (ci == 1) envDirty_ = true;
    }
}

void SoundMixer::tick(float dt) {
    // AdvanceTimers: last -> first; expiry is a non-forced Disable (RefCount n > 1 drops once per tick).
    for (size_t i = active_.size(); i-- > 0;) {
        if (i >= active_.size()) continue;
        Preset& p = presets_[(size_t)active_[i]];
        p.elapsed += dt;
        if (std::strcmp(p.def.name, "Default") != 0 && p.def.duration >= 0.0f && p.elapsed >= p.def.duration)
            disable(p.def.name, false);
    }
    for (int ci = 0; ci < 2; ++ci)
        for (Ramp& r : cats_[ci].ramps) {
            bool was = r.active;
            r.update(dt);
            if ((was || r.active) && ci == 1) envDirty_ = true;
        }
}

float SoundMixer::categoryVolume(const std::string& category) const {
    for (const Category& c : cats_)
        if (category == c.name) return std::min(1.0f, std::max(0.0f, c.ramps[kVolume].value));   // clamp [0,1]
    return 1.0f;
}

audio::Environment SoundMixer::environment() {
    envDirty_ = false;
    const Ramp* r = cats_[1].ramps;
    audio::Environment e;
    e.room = r[1].value; e.roomHF = r[2].value; e.decayTime = r[4].value; e.decayHFRatio = r[5].value;
    e.reflections = r[6].value; e.reflectionsDelay = r[7].value; e.reverb = r[8].value; e.reverbDelay = r[9].value;
    e.diffusion = r[10].value; e.density = r[11].value; e.hfReference = r[12].value;
    e.echoDelayMs = r[13].value; e.echoDecay = r[14].value; e.echoWet = r[15].value; e.echoDry = r[16].value;
    return e;
}

std::string SoundMixer::activeList() const {
    std::string s;
    for (int p : active_) {
        if (!s.empty()) s += ",";
        s += presets_[(size_t)p].def.name;
        s += "(" + std::to_string(presets_[(size_t)p].refs) + ")";
    }
    return s;
}

const char* SoundMixer::categoryTarget(int category) const {
    return presets_[(size_t)cats_[category].current].def.name;
}

} // namespace game
