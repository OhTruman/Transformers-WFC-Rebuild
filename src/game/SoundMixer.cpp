#include "game/SoundMixer.h"
#include "core/Log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace game {
namespace {
using PresetDef = SoundMixer::PresetDef;
using CategoryPreset = SoundMixer::CategoryPreset;
using CategoryRow = SoundMixer::CategoryRow;
using CategoryParent = SoundMixer::CategoryParent;
using GroupCategory = SoundMixer::GroupCategory;
using GroupDefault = SoundMixer::GroupDefault;
#include "game/SoundMixer.inc"
enum { kVolume = 0 };
bool sameName(const std::string& a, const char* b) {         // FName comparison: case-insensitive
    const size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) if (std::toupper((unsigned char)a[i]) != std::toupper((unsigned char)b[i])) return false;
    return true;
}
// The distinct group names, in mapping order, and their current volumes (device-global).
struct Groups {
    std::vector<std::string> names;
    std::vector<float> vol;
    std::unordered_map<std::string, std::vector<int>> scaleOf;  // category -> groups over it and its ancestors (cache)
    Groups() {
        for (const GroupCategory& g : kGroupCategories) {
            bool known = false;
            for (const std::string& n : names) known = known || n == g.group;
            if (!known) names.push_back(g.group);
        }
        reset();
    }
    int index(const std::string& group) const {
        for (size_t i = 0; i < names.size(); ++i) if (sameName(group, names[i].c_str())) return (int)i;
        return -1;
    }
    void reset() {
        vol.assign(names.size(), 1.0f);
        for (const GroupDefault& d : kProfileGroupDefaults) {
            const int i = index(d.group);
            if (i >= 0) vol[(size_t)i] = std::min(1.0f, std::max(0.0f, (float)d.slider / 100.0f));
        }
    }
    const std::vector<int>& groupsOver(const std::string& category) {
        auto it = scaleOf.find(category);
        if (it != scaleOf.end()) return it->second;
        std::vector<int> out;
        std::string c = category;
        for (int guard = 0; !c.empty() && guard < 64; ++guard) {
            for (const GroupCategory& g : kGroupCategories)
                if (c == g.category) {
                    const int i = index(g.group);
                    if (i >= 0 && std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
                }
            std::string parent;
            for (const CategoryParent& p : kCategoryParents) if (c == p.category) { parent = p.parent; break; }
            c = parent;
        }
        return scaleOf.emplace(category, out).first->second;
    }
};
Groups& groups() { static Groups g; return g; }
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

SoundMixer::SoundMixer() {
    presets_.push_back(Preset{"Default", 0.0f, 0.5f, 0.5f, -1.0f});   // built-in (mixer Init 0x82782A18)
    for (const PresetDef& p : kPresetDefs) presets_.push_back(Preset{p.name, p.priority, p.fadeIn, p.fadeOut, p.duration});
    for (const CategoryRow& r : kCategoryRows) addRow(r.category, r.name, r.v);
    flush();
}

SoundMixer::SoundMixer(const PresetDef* presets, int n, const CategoryPreset* cat0, int n0, const CategoryPreset* cat1, int n1) {
    presets_.push_back(Preset{"Default", 0.0f, 0.5f, 0.5f, -1.0f});
    for (int i = 0; i < n; ++i)
        presets_.push_back(Preset{presets[i].name, presets[i].priority, presets[i].fadeIn, presets[i].fadeOut, presets[i].duration});
    for (int i = 0; i < n0; ++i) addRow("SFX_WET_VEH_ENGINE", cat0[i].name, cat0[i].v);
    for (int i = 0; i < n1; ++i) addRow("MASTER_WET", cat1[i].name, cat1[i].v);
    flush();
}

int SoundMixer::category(const std::string& name) const {
    auto it = catIndex_.find(name);
    return it == catIndex_.end() ? -1 : it->second;
}

void SoundMixer::addRow(const std::string& cat, const char* name, const float* v) {
    int c = category(cat);
    if (c < 0) { cats_.emplace_back(); cats_.back().name = cat; c = (int)cats_.size() - 1; catIndex_[cat] = c; }
    Entry e; e.name = name;
    std::copy(v, v + kParams, e.v);
    cats_[(size_t)c].table.push_back(e);
    masterWet_ = category("MASTER_WET");
    master_ = category("Master");
    if (c == master_ && e.name == "Default") masterDefault_ = v[kVolume];
}

bool SoundMixer::addMapPreset(const std::string& name, float priority, float fadeIn, float fadeOut, float duration,
                              const float masterWet[kParams]) {
    if (find(name) >= 0) { LOG_WARN("mixer: preset %s already exists (map preset ignored)", name.c_str()); return false; }
    Preset p{name, priority, fadeIn, fadeOut, duration};
    p.map = true;
    presets_.push_back(p);
    if (masterWet_ < 0) {
        cats_.emplace_back(); cats_.back().name = "MASTER_WET"; masterWet_ = (int)cats_.size() - 1; catIndex_["MASTER_WET"] = masterWet_;
    }
    Entry e; e.name = name; e.map = true;
    std::copy(masterWet, masterWet + kParams, e.v);
    cats_[(size_t)masterWet_].table.push_back(e);
    return true;
}

bool SoundMixer::addMapPresetRows(const std::string& name, float priority, float fadeIn, float fadeOut, float duration,
                                  const std::vector<std::pair<std::string, std::vector<float>>>& rows) {
    if (find(name) >= 0) { LOG_WARN("mixer: preset %s already exists (map preset ignored)", name.c_str()); return false; }
    Preset p{name, priority, fadeIn, fadeOut, duration};
    p.map = true;
    presets_.push_back(p);
    for (const auto& r : rows) {
        int c = category(r.first);
        if (c < 0 || r.second.size() < (size_t)kParams) continue;          // categories the mixer does not model
        Entry e; e.name = name; e.map = true;
        std::copy(r.second.begin(), r.second.begin() + kParams, e.v);
        cats_[(size_t)c].table.push_back(e);
    }
    return true;
}

int SoundMixer::removeMapPresets() {
    flush();                                          // only Default (index 0, built-in) stays active
    int n = 0;
    for (size_t i = presets_.size(); i-- > 1;)
        if (presets_[i].map) { presets_.erase(presets_.begin() + (long)i); ++n; }
    for (Category& c : cats_)
        c.table.erase(std::remove_if(c.table.begin(), c.table.end(), [](const Entry& e) { return e.map; }), c.table.end());
    return n;
}

int SoundMixer::mapPresetCount() const {
    int n = 0;
    for (const Preset& p : presets_) n += p.map ? 1 : 0;
    return n;
}

int SoundMixer::find(const std::string& name) const {
    for (size_t i = 0; i < presets_.size(); ++i) if (name == presets_[i].name) return (int)i;
    return -1;
}

bool SoundMixer::isActive(int p) const {
    return std::find(active_.begin(), active_.end(), p) != active_.end();
}

const SoundMixer::Entry* SoundMixer::defines(const Category& c, int p) const {
    for (const Entry& e : c.table) if (e.name == presets_[(size_t)p].name) return &e;
    return nullptr;
}

bool SoundMixer::unflushable(const std::string& name) {
    for (const char* n : kUnflushablePresets) if (name == n) return true;
    return false;
}

const char* SoundMixer::movieMixerPreset() { return kMovieMixerPresetName; }

bool SoundMixer::movieAlwaysPlaysSound(const std::string& n) {
    for (const char* m : kMoviesToAlwaysPlaySound) if (n == m) return true;
    return false;
}

void SoundMixer::flush() {
    std::vector<std::pair<int, int>> keep;                // active unflushable presets (index, refs)
    for (int p : active_) if (unflushable(presets_[(size_t)p].name)) keep.push_back({p, presets_[(size_t)p].refs});
    active_.clear();
    for (Preset& p : presets_) { p.refs = 0; p.elapsed = 0.0f; }
    for (Category& c : cats_) {
        c.current = 0;
        const Entry* d = defines(c, 0);
        for (int k = 0; k < kParams; ++k) { c.ramps[k] = Ramp{}; c.ramps[k].value = c.ramps[k].target = d ? d->v[k] : 0.0f; }
    }
    enable("Default");
    for (const auto& kp : keep) {                         // back in, at their priority, values in place at once
        auto it = active_.begin();
        while (it != active_.end() && presets_[(size_t)*it].priority >= presets_[(size_t)kp.first].priority) ++it;
        active_.insert(it, kp.first);
        presets_[(size_t)kp.first].refs = kp.second;
    }
    if (!keep.empty()) {
        retarget();
        for (Category& c : cats_) for (Ramp& r : c.ramps) { r.value = r.target; r.active = false; }
    }
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
        while (it != active_.end() && presets_[(size_t)*it].priority >= presets_[(size_t)p].priority) ++it;
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
    for (int ci = 0; ci < (int)cats_.size(); ++ci) {
        Category& c = cats_[(size_t)ci];
        int target = -1;
        const Entry* vals = nullptr;
        for (int p : active_) if ((vals = defines(c, p)) != nullptr) { target = p; break; }   // first match wins
        if (target < 0 || target == c.current) continue;                                     // keep / no restart
        const Preset& nd = presets_[(size_t)target];
        const Preset& cd = presets_[(size_t)c.current];
        float fade = nd.priority < cd.priority ? cd.fadeOut : nd.fadeIn;
        for (int k = 0; k < kParams; ++k) c.ramps[k].setTarget(vals->v[k], fade);
        if (logOn()) LOG_INFO("MIXER %s: %s -> %s over %.2f s", c.name.c_str(), cd.name.c_str(), nd.name.c_str(), fade);
        c.current = target;
        if (ci == masterWet_) envDirty_ = true;
    }
}

void SoundMixer::tick(float dt) {
    // AdvanceTimers: last -> first; expiry is a non-forced Disable (RefCount n > 1 drops once per tick).
    for (size_t i = active_.size(); i-- > 0;) {
        if (i >= active_.size()) continue;
        Preset& p = presets_[(size_t)active_[i]];
        p.elapsed += dt;
        if (p.name != "Default" && p.duration >= 0.0f && p.elapsed >= p.duration) {
            const std::string name = p.name;
            disable(name, false);
        }
    }
    for (int ci = 0; ci < (int)cats_.size(); ++ci)
        for (Ramp& r : cats_[(size_t)ci].ramps) {
            bool was = r.active;
            r.update(dt);
            if ((was || r.active) && ci == masterWet_) envDirty_ = true;
        }
}

float SoundMixer::categoryVolume(const std::string& name) const {
    const int c = category(name);
    return c < 0 ? 1.0f : std::min(1.0f, std::max(0.0f, cats_[(size_t)c].ramps[kVolume].value));   // clamp [0,1]
}

bool SoundMixer::masterCompressor(float& thr, float& att, float& rel, float& mk) {
    thr = kMasterCompressor[1]; att = kMasterCompressor[2]; rel = kMasterCompressor[3]; mk = kMasterCompressor[4];
    return ((int)kMasterCompressor[0] & 32) != 0;      // DSPEffectConfig compressor bit [MED]
}

bool SoundMixer::setGroupVolume(const std::string& group, float linear) {
    Groups& g = groups();
    const int i = g.index(group);
    if (i < 0) { LOG_WARN("mixer: SetAudioGroupVolume: unknown sound group %s", group.c_str()); return false; }
    g.vol[(size_t)i] = std::min(1.0f, std::max(0.0f, linear));
    if (logOn()) LOG_INFO("mixer: group %s volume %.2f", g.names[(size_t)i].c_str(), g.vol[(size_t)i]);
    return true;
}

float SoundMixer::groupVolume(const std::string& group) {
    const Groups& g = groups();
    const int i = g.index(group);
    return i < 0 ? 1.0f : g.vol[(size_t)i];
}

void SoundMixer::resetGroupVolumes() { groups().reset(); }

int SoundMixer::profileDefaultSlider(const std::string& group) {
    for (const GroupDefault& d : kProfileGroupDefaults) if (sameName(group, d.group)) return d.slider;
    return -1;
}

float SoundMixer::groupScale(const std::string& category) {
    Groups& g = groups();
    float s = 1.0f;
    for (int i : g.groupsOver(category)) s *= g.vol[(size_t)i];
    return s;
}

float SoundMixer::masterScale() const {
    if (master_ < 0 || masterDefault_ <= 0.0f) return 1.0f;
    return std::min(1.0f, std::max(0.0f, cats_[(size_t)master_].ramps[kVolume].value)) / masterDefault_;
}

audio::Environment SoundMixer::environment() {
    envDirty_ = false;
    audio::Environment e;
    if (masterWet_ < 0) return e;
    const Ramp* r = cats_[(size_t)masterWet_].ramps;
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
        s += presets_[(size_t)p].name;
        s += "(" + std::to_string(presets_[(size_t)p].refs) + ")";
    }
    return s;
}

const char* SoundMixer::categoryTarget(const char* name) const {
    const int c = category(name);
    return c < 0 ? "" : presets_[(size_t)cats_[(size_t)c].current].name.c_str();
}

} // namespace game
