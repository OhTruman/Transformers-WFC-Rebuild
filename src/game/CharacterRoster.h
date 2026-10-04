// Clean-room reconstruction — multiplayer character selection contract (Frontend / AssetTools -> Gameplay).
// Provenance: RE MILESTONE05_PLAYTEST_RE §7, OVERNIGHT_2026-10-04 §E, notes/data/mp_chassis_roster.json
// (TransCustomization.ini TnDataProvider_Chassis, 33 chassis) [CONF].
//
//   frontend selection (CustomTransformers: Customize.SelectCharacter(name, type 0 custom / 1 iconic))
//   -> CharacterSelection (stable ids, no file names) -> resolveChassis(selection, faction at spawn)
//   -> PawnDefinition (AssetTools-exported ROBODEF / VEHDEF data keyed by chassis UniqueId) -> spawn.
//
// The faction is the player's team at spawn (TnGame.GetResolvedCharacterFaction = TeamNum; FFA forces 1 = Decepticon), so
// one custom character becomes an Autobot or a Decepticon body by team [CONF]. Spawning waits for a selection
// (TnPlayerSpawnHelperMultiplayer.CheckReadySpawn: PRI.HasSelectedCharacter) [CONF].
#pragma once
#include <string>

namespace game {

enum class Specialty { Leader = 0, Scientist = 1, Scout = 2, Soldier = 3 };

struct CharacterSelection {
    int type = 1;                    // 0 custom (specialty preset / customization slot), 1 iconic (a named chassis)
    Specialty specialty = Specialty::Leader;
    std::string chassisId = "Truck"; // iconic: the chassis UniqueId (e.g. "Truck" = Optimus Prime)
    std::string customSlot;          // custom: customization slot / character name (profile data; outside Gameplay)
};

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
    return defaultChassis(sel.specialty, faction);          // custom: the specialty's body for the resolved faction
}

// What Gameplay needs to build a pawn for a chassis (filled from the AssetTools ROBODEF / VEHDEF export). Only the
// Optimus ("Truck") resources are loaded today: any other resolved chassis falls back to them [RECONSTRUCTION FALLBACK].
struct PawnDefinition {
    std::string chassisId = "Truck";
    float collisionRadius = 2.0f, collisionHalfHeight = 2.0f;   // ROBODEF CollisionRadius / Height (Optimus 200 / 200 UU)
    float groundSpeed = 14.0f, accelRate = 120.0f, airSpeed = 12.0f;   // shared player values (1400 / 12000 / 1200 UU)
    std::string momentumBlueprint = "TR_Acrobatics_p.TruckTransformerMomentum";
    std::string vehicleClass = "TnTruckFormBlueprint";
};

} // namespace game
