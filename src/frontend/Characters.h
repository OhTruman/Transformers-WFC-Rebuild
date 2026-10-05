// Clean-room reconstruction — the local player's multiplayer characters (TnCharacterCustomizationData role) and the
// character-selection bindings (TnCharacterScriptBinding, "Customize.*").
//
// Data: AssetTools manifests/mp_content/roster_package.json (stable chassis ids, display names, class, faction, locks,
// loadouts; keyed by RE chassis ids), never per-character code. A fresh profile holds one custom character per
// specialty, filled from the class presets TR_MPPlayerCharacterData_p.<Class>_PCD_MP in SpecialtyClasses order
// (Scout, Scientist, Soldier, Leader) - Default__TnCharacterCustomizationData.Default<Class> + RefreshCharacterSlots
// (1 slot per specialty below level 5) [CONFIRMED script / authored]. Character slots unlock at levels 5 and 10.
// The selection is handed to Gameplay as (character name, type 0 custom / 1 iconic) plus the per-faction chassis the
// preset lists; the team decides the body at spawn (TnGame.GetResolvedCharacterFaction = TeamNum) [CONFIRMED].
// Unlock rules beyond the two script-proven ones are native / UNKNOWN and are not guessed.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace frontend {

// A colour as TnPlayerCharacterData stores it: the colour itself (black = "take it from the palette swatch") plus the
// palette id (0-4 Autobot, 5-9 Decepticon) and the swatch coordinates in that palette.
struct CharacterColor {
    int r = 0, g = 0, b = 0, a = 255;
    int palette = 0;
    float x = 0, y = 0;
    bool isBlack() const { return r == 0 && g == 0 && b == 0 && a == 255; }
};

struct CharacterPreset {
    std::string name;                         // CharacterName ("Scout")
    std::string friendlyName;                 // FriendlyName (the player's name for the class; empty = CharacterName)
    std::string specialty;                    // Scout / Scientist / Soldier / Leader
    std::string chassis[2];                   // per faction: Autobot, Decepticon (roster chassis ids, e.g. Car2 / Car4)
    std::string iconicNames[2];               // display names of those chassis (Sideswipe / Barricade)
    std::vector<std::string> weapons, vehicleWeapons, melee, abilities, skills;
    CharacterColor primary[2], secondary[2];  // per faction
};

struct ChassisInfo {
    std::string id, displayName, specialty, availability, robotGltf, vehicleGltf;
    std::vector<std::string> robotAnimSets;   // robot.anim_sets (roster package): the preview idle's AnimSets
    int faction = 3;                          // FactionRestriction 0 Autobot, 1 Decepticon, 3 neutral
    bool lockedChassis = false, lockedCharacter = false;
};

class CharacterRoster {
public:
    bool load(const std::string& rosterPackagePath);
    // The presets' authored character data (colours, skills, melee): data/frontend/character_presets.json.
    void loadAuthored(const std::string& presetsPath);
    bool loaded() const { return !characters_.empty(); }
    // The player's custom characters: the class presets on a fresh profile, edited by Create a Character
    // (Customize.CommitCharacter) and kept in the customization file (WriteCustomizationFile).
    const std::vector<CharacterPreset>& customCharacters() const { return characters_; }
    const CharacterPreset* find(const std::string& name) const;
    CharacterPreset* findMutable(const std::string& name);
    // TnCharacterCustomizationData.ResetCharacterFromName (TnLocalPlayer.ResetCharacter): the class preset, the
    // FriendlyName kept, and random colour palettes / coordinates (colours black = "sample the palette").
    void reset(const std::string& name);
    // Fresh profile (FillCharacterSlots -> ResetCharacterFromName per slot): every character gets random palettes.
    void randomizeAllColors();
    // Customization file (PC original: TnLocalPlayer.WriteCustomizationFile; the rebuild's file is wfc_characters.ini).
    bool save(const std::string& path) const;
    void loadSaved(const std::string& path);
    const ChassisInfo* chassis(const std::string& id) const;
    int numberOfColorPalettes = 5;            // TnCharacterCustomizationData.NumberOfColorPalettes (authored)
    const std::map<std::string, ChassisInfo>& allChassis() const { return chassis_; }

private:
    std::vector<CharacterPreset> presets_;     // class defaults
    std::vector<CharacterPreset> characters_;  // the player's characters
    std::map<std::string, ChassisInfo> chassis_;
};

} // namespace frontend
