// Clean-room reconstruction — WFC FmodAudioDevice mixer (SoundMixerProperties presets), ported from the
// native RE report RE-Workspace/notes/MILESTONE03_AUDIO_NATIVE_FIDELITY.md (ReverseEngineering 7c4a2e0,
// follow-up A2-A4) [CONF]:
//   * Runtime preset entry {Priority, FadeIn, FadeOut, Duration, RefCount, Elapsed}; Init registers and
//     enables a built-in "Default" preset (Priority 0, FadeIn/FadeOut 0.5, Duration -1).
//   * Enable (0x82772778): Elapsed = 0; if not active, insert before the first active preset with LOWER
//     priority (equal priority -> after existing equals) and retarget categories; RefCount += 1.
//   * Disable(force) (0x827728E0): if active: force -> RefCount = 1; RefCount 1 -> remove + retarget;
//     RefCount = max(0, RefCount - 1).
//   * Tick (0x82756180 -> AdvanceTimers 0x827665F0): for active presets last -> first: Elapsed += dt; a
//     non-Default preset with Duration >= 0 and Elapsed >= Duration gets a non-forced Disable.
//   * Category SelectTarget (0x827661C8): the first active preset (priority order) that defines a DSP preset
//     for that category wins outright; none -> keep the current target. A changed target retargets every
//     parameter with one linear ramp in its authored unit (Volume linear amplitude, reverb levels mB, times s,
//     frequencies Hz) over FadeIn of the new preset when its priority >= the current one's, else the outgoing
//     preset's FadeOut; a ramp restarts from its current value with the full new time (0x827560A8/F8).
//   * Flush (0x8276AB60, level change): only Default stays active; the current-reverb slot becomes None.
// Applied: every category's Volume (all 47 SoundMixerProperties categories, SoundMixer.inc) and MASTER_WET reverb /
// echo. A cue's gain = its own category's volume x masterScale() (Master volume relative to Master's Default 0.708:
// the rebuild's output level stands in for Master's Default, so only Master CHANGES - CINE_MUTE_FOR_BINK - are
// applied). [CONF values; HIGH: Master is the root of every category (sound_group_category_mappings "Master" ->
// MASTER_DRY / MASTER_WET); the parent chain between the other categories is native and not applied.]
// Preset sources: the global SoundMixerProperties presets the cue table plays (VEHICLE_JUMP, VEHICLE_BOOST_END) and
// the MovieMixerPreset (CINE_MUTE_FOR_BINK) are built in; a level's REVERB_* presets come from its manifest
// reverb_presets (mixer_preset + dsp_by_category.MASTER_WET) through addMapPreset() at level load and are removed by
// removeMapPresets() at unload, so no map name is compiled in.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "audio/Audio.h"

namespace game {

class SoundMixer {
public:
    struct PresetDef { const char* name; float priority, fadeIn, fadeOut, duration; };
    struct CategoryPreset { const char* name; float v[17]; };   // Volume, Reverb (12), Echo (4)
    struct CategoryRow { const char* category; const char* name; float v[17]; };
    static constexpr int kParams = 17;

    SoundMixer();                                         // every category + the built-in global presets (SoundMixer.inc)
    // Validation: the same mixer over caller-owned tables (`presets` excludes the built-in Default; the two
    // category tables stand in for SFX_WET_VEH_ENGINE and MASTER_WET and should define "Default").
    SoundMixer(const PresetDef* presets, int n, const CategoryPreset* cat0, int n0, const CategoryPreset* cat1, int n1);

    // Map-owned presets (a map's reverb presets). Adding a name that already exists is refused (false).
    // `masterWet` = the preset's MASTER_WET DSP values in CategoryPreset order (Volume, Reverb, Echo).
    bool addMapPreset(const std::string& name, float priority, float fadeIn, float fadeOut, float duration,
                      const float masterWet[kParams]);
    // Map unload: Flush, then forget every map-owned preset. Returns the number removed.
    int removeMapPresets();
    int mapPresetCount() const;

    bool enable(const std::string& name);                 // EnableMixerPreset (false = unknown preset)
    void disable(const std::string& name, bool force);    // DisableMixerPreset
    void flush();                                         // level change: only Default remains, reverb slot None
    // SeqAct_Reverb.Activated (0x82764858) [CONF]: if `preset` differs from the global current-reverb slot
    // (0x8374FCCC): Enable(preset); on success explicitly Disable(previous, force=0) and store `preset`.
    // The same preset again is a no-op (no Enable, no ref-count change, no timer reset).
    void activateReverb(const std::string& preset);
    const std::string& currentReverb() const { return currentReverb_; }
    void tick(float dt);

    // Category outputs.
    float categoryVolume(const std::string& category) const;   // linear amplitude, clamped [0,1] (unknown: 1)
    float masterScale() const;                                 // Master volume / Master Default volume
    // Master's Default DSP compressor (global SoundMixerProperties data; false if its stage is not configured).
    static bool masterCompressor(float& thresholdDb, float& attackMs, float& releaseMs, float& makeupDb);
    bool environmentChanged() const { return envDirty_; }
    audio::Environment environment();                          // MASTER_WET reverb / echo (clears dirty)

    bool hasPreset(const std::string& name) const { return find(name) >= 0; }
    // Diagnostics.
    std::string activeList() const;
    const char* categoryTarget(const char* category) const;   // current target preset of a category ("" unknown)
    int presetCount() const { return (int)presets_.size(); }
    int categoryCount() const { return (int)cats_.size(); }

private:
    struct Preset {
        std::string name; float priority, fadeIn, fadeOut, duration;
        bool map = false;                          // owned by the loaded map (removed at unload)
        int refs = 0; float elapsed = 0.0f;
    };
    struct Entry { std::string name; float v[kParams]; bool map = false; };
    struct Ramp {
        float value = 0, target = 0, rate = 0, remaining = 0; bool active = false;
        void setTarget(float t, float T);
        void update(float dt);
    };
    struct Category {
        std::string name;
        std::vector<Entry> table;
        int current = 0;                           // preset index of the current target
        Ramp ramps[kParams];
    };

    int find(const std::string& name) const;
    bool isActive(int p) const;
    const Entry* defines(const Category& c, int p) const;
    void retarget();

    std::vector<Preset> presets_;
    std::vector<int> active_;                     // preset indices, highest priority first
    std::vector<Category> cats_;
    std::unordered_map<std::string, int> catIndex_;
    int masterWet_ = -1, master_ = -1;            // category indices (-1 = absent)
    float masterDefault_ = 1.0f;
    int category(const std::string& name) const;
    void addRow(const std::string& cat, const char* name, const float* v);
    bool envDirty_ = true;
    std::string currentReverb_;                   // global current-reverb name ("" = None)
};

} // namespace game
