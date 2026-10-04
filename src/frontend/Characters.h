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

struct CharacterPreset {
    std::string name;                         // CharacterName ("Scout")
    std::string specialty;                    // Scout / Scientist / Soldier / Leader
    std::string chassis[2];                   // per faction: Autobot, Decepticon (roster chassis ids, e.g. Car2 / Car4)
    std::string iconicNames[2];               // display names of those chassis (Sideswipe / Barricade)
    std::vector<std::string> weapons, vehicleWeapons, melee, abilities;
};

struct ChassisInfo {
    std::string id, displayName, specialty, availability, robotGltf, vehicleGltf;
    int faction = 3;                          // FactionRestriction 0 Autobot, 1 Decepticon, 3 neutral
    bool lockedChassis = false, lockedCharacter = false;
};

class CharacterRoster {
public:
    bool load(const std::string& rosterPackagePath);
    bool loaded() const { return !presets_.empty(); }
    const std::vector<CharacterPreset>& customCharacters() const { return presets_; }   // fresh profile
    const CharacterPreset* find(const std::string& name) const;
    const ChassisInfo* chassis(const std::string& id) const;
    const std::map<std::string, ChassisInfo>& allChassis() const { return chassis_; }

private:
    std::vector<CharacterPreset> presets_;
    std::map<std::string, ChassisInfo> chassis_;
};

} // namespace frontend
