// Clean-room reconstruction — multiplayer character selection contract (Frontend / AssetTools -> Gameplay).
// Provenance: RE MILESTONE05_PLAYTEST_RE §7, OVERNIGHT_2026-10-04 §E, notes/data/mp_chassis_roster.json
// (TransCustomization.ini TnDataProvider_Chassis, 33 chassis) [CONF].
//
//   frontend selection (CustomTransformers: Customize.SelectCharacter(name, type 0 custom / 1 iconic))
//   -> CharacterSelection (stable ids, no file names) -> resolveChassis(selection, faction at spawn)
//   -> ChassisDef (AssetTools Characters/<ChassisId> export keyed by the RE chassis UniqueId) -> spawn.
//
// The faction is the player's team at spawn (TnGame.GetResolvedCharacterFaction = TeamNum; FFA forces 1 = Decepticon), so
// one custom character becomes an Autobot or a Decepticon body by team [CONF]. Spawning waits for a selection
// (TnPlayerSpawnHelperMultiplayer.CheckReadySpawn: PRI.HasSelectedCharacter) [CONF].
#pragma once
#include <string>
#include <vector>

namespace game {

enum class Specialty { Leader = 0, Scientist = 1, Scout = 2, Soldier = 3 };

struct CharacterColor { int r = 0, g = 0, b = 0, a = 255; int palette = 0; float x = 0, y = 0; };   // black = material default paint

// PRI._SelectedCharacter (TnPlayerCharacterData, string ids). Filled by Frontend from GameFlow::SelectedCharacter.
struct CharacterSelection {
    int type = 1;                    // 0 custom (specialty preset / customization slot), 1 iconic (a named chassis)
    Specialty specialty = Specialty::Leader;
    std::string chassisId = "Truck"; // iconic: the chassis UniqueId (e.g. "Truck" = Optimus Prime)
    std::string customSlot;          // custom: customization slot / CharacterName
    // CharacterData parallel arrays per faction (0 Autobot, 1 Decepticon): ChassisTypes / colours. Empty chassis =
    // the specialty preset's body (TR_MPPlayerCharacterData_p.<Class>_PCD_MP).
    std::string chassisByFaction[2];
    CharacterColor primary[2], secondary[2];
    // WeaponTypes / VehicleWeapons / MeleeWeapons / Abilities / Skills (provider UniqueIds). Empty = the chassis'
    // iconic preset lists.
    std::vector<std::string> weapons, vehicleWeapons, melee, abilities, skills;
};

inline const char* specialtyName(Specialty s) {
    static const char* n[4] = {"Leader", "Scientist", "Scout", "Soldier"};
    return n[(int)s];
}

// Default MP presets TR_MPPlayerCharacterData_p.*_PCD_MP: chassis per specialty and faction [CONF].
inline const char* defaultChassis(Specialty s, int faction) {
    static const char* t[4][2] = {{"Truck3", "Truck4"},   // Leader: Ironhide / Soundwave
                                  {"Jet4", "Jet"},        // Scientist: Air Raid / Starscream
                                  {"Car2", "Car4"},       // Scout: Sideswipe / Barricade
                                  {"Tank3", "Tank2"}};    // Soldier: Warpath / Brawl
    return t[(int)s][faction == 1 ? 1 : 0];
}

// ResolveReplicatedCharacterData: the body for this spawn.
inline std::string resolveChassis(const CharacterSelection& sel, int faction) {
    if (sel.type == 1) return sel.chassisId;                // iconic characters keep their own chassis
    int f = faction == 1 ? 1 : 0;                           // ResolveReplicatedCharacterData: Index of the faction
    if (!sel.chassisByFaction[f].empty()) return sel.chassisByFaction[f];
    return defaultChassis(sel.specialty, faction);          // custom: the specialty's body for the resolved faction
}

// The pawn itself is built from game::ChassisDef (ChassisDef.h): AssetTools Characters/<ChassisId>, no substitute body.

} // namespace game
