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
// MASTER_DRY / MASTER_WET); the parent chain's preset volumes are not applied (no global preset targets a parent).]
// The profile sound-group volumes DO reach down the authored category tree: gain x groupScale(category) (see below).
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
    struct CategoryParent { const char* category; const char* parent; };
    struct GroupCategory { const char* group; const char* category; };
    struct GroupDefault { const char* group; int slider; };
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
    // A map-owned preset over several categories (`rows`: category name -> that category's DSP values in CategoryPreset
    // order; only Volume is applied outside MASTER_WET): SeqAct_Mixer presets such as EXT DUCK.
    bool addMapPresetRows(const std::string& name, float priority, float fadeIn, float fadeOut, float duration,
                          const std::vector<std::pair<std::string, std::vector<float>>>& rows);
    int mapPresetCount() const;

    bool enable(const std::string& name);                 // EnableMixerPreset (false = unknown preset)
    void disable(const std::string& name, bool force);    // DisableMixerPreset
    // Level change: only Default remains, reverb slot None - except the UnflushableMixerPresets
    // (Xe-TransEngine.ini [HM_Engine.SoundMixerProperties]: CINE_MUTE_FOR_BINK), which stay active with their ref
    // counts [CONF config; the native Flush honouring the list is HIGH].
    void flush();
    static bool unflushable(const std::string& name);
    static const char* movieMixerPreset();               // [HM_Engine.FmodAudioDevice] MovieMixerPreset [CONF config]
    static bool movieAlwaysPlaysSound(const std::string& movieName);   // [Engine.MovieSettings] MoviesToAlwaysPlaySound
    // SeqAct_Reverb.Activated (0x82764858) [CONF]: if `preset` differs from the global current-reverb slot
    // (0x8374FCCC): Enable(preset); on success explicitly Disable(previous, force=0) and store `preset`.
    // The same preset again is a no-op (no Enable, no ref-count change, no timer reset).
    void activateReverb(const std::string& preset);
    const std::string& currentReverb() const { return currentReverb_; }
    void tick(float dt);

    // Category outputs.
    float categoryVolume(const std::string& category) const;   // linear amplitude, clamped [0,1] (unknown: 1)
    float masterScale() const;                                 // Master volume / Master Default volume

    // Sound groups (the profile volume sliders). UAudioDevice::SetAudioGroupVolume(group, v): [HM_Engine.
    // SoundMixerProperties] SoundGroupCategoryMappings name the categories a group scales - SFX -> SFX_DRY, SFX_WET;
    // DIALOG -> DX_DRY, DX_WET; MUSIC -> MUSIC_DRY; MASTER -> MASTER_DRY, MASTER_WET [CONF config]. Native
    // SetGroupVolume (0x827666B0) finds each listed category node and REPLACES its fader (initialised to the config
    // Volume) with the value, immediately (SetTarget(v, 0)); FName match, unknown group = no-op [CONF RE pass 5 §10].
    // Each category's channel group is attached to its parent's [CONF], so the fader scales every descendant
    // (multiplicative FMOD ChannelGroup volume [HIGH]). All listed config Volumes are 1.0 (gen_mixer.py asserts it),
    // so replacing equals the multiplier applied here. Whether mixer presets ramp the same fader is not traced: the
    // preset category volumes stay a separate factor [HIGH].
    // HmPlayerController.UpdateLocalCacheOfProfileSettings applies SetAudioGroupVolume('Dialog' | 'SFX' | 'MUSIC',
    // slider / 100 clamped [0,1]) [CONF script]; the TnProfileSettings defaults are 80 / 80 / 80, which is also the
    // value before any profile is applied here. Device-global (the frontend and game cue tables share it), immediate.
    static bool setGroupVolume(const std::string& group, float linear);   // false: unknown group (no-op)
    static float groupVolume(const std::string& group);                   // unknown: 1
    static void resetGroupVolumes();                                      // the profile defaults
    static int profileDefaultSlider(const std::string& group);            // MUSIC / SFX / DIALOG (-1 unknown)
    // The product of the group volumes over `category` and its ancestors (1 when no group covers it).
    static float groupScale(const std::string& category);
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
