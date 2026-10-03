// Clean-room reconstruction — the pickup SOUND of the 27 placed MP_IAC_Streets pickup factories (Systems).
// Data: AssetTools 7a69756 streets_pickup_factories.json (gen_pickups.py -> PickupPresentation.inc)
// [CONF authored]. Behaviour [CONF script]: Engine.PickupFactory GiveTo -> SpawnCopyFor ->
// Inventory.AnnouncePickup: Other.PlaySound(PickupSound), a sound attached to the RECEIVING pawn; a respawn
// plays nothing (RespawnEffect empty, RE d50c2a9 P2).
// OWNERSHIP:
//   * Gameplay owns the factory state machine (agents/gameplay PickupFactory, World::pickupEvents()).
//   * Rendering owns the pickup effects AND their runtime state (agents/rendering 411c970 WfcMapFx,
//     setMapEffectState: CustomPickupEffect / PickupEffect highlight per SetPickupVisible / SetPickupHidden).
//   * Systems only plays the PickupSound on each Gameplay Taken event (onTaken, keyed by actor name). The
//     FactoryDef effect fields are kept as authored data, not driven here.
#pragma once
#include "core/Math.h"
#include "game/SoundCues.h"

namespace game {

class PickupPresentation {
public:
    enum class Kind { AmmoCrate, Health, OverShield, ObjectiveFlag, ObjectiveBomb };
    struct FactoryDef {
        const char* actor;
        Kind kind;
        core::Vec3 ueLocation;           // UU
        core::Vec3 position;             // glTF metres
        float respawnTime;               // authored per instance (Gameplay's Sleeping duration)
        const char* pickupSound;         // inventory PickupSound cue (SoundCues table name), may be null
        const char* customEffect;        // CustomPickupEffect template, null = none
        const char* pickupEffect;        // PickupEffect (highlight) template
        bool pickupEffectAttached;       // listed in the factory class Components (rendered when active)
        bool highlightFx;                // ShouldDisplayHighlightFx
        const char* requiredGameRule;    // objective factories: only under this game rules class
    };
    static int count();
    static const FactoryDef& def(int i);

    static int find(const char* actor);  // factory index by actor name (e.g. "TnAmmoCratePickupFactory_10561")
    // Gameplay PickupEvent Taken: Inventory.AnnouncePickup (PickupSound on the receiving pawn). Returns the
    // sound's cue instance (-1 = none / unknown factory).
    int onTaken(const char* actor, SoundCues& cues, const SoundCues::Emitter& receiver, float listenerDist) {
        int i = find(actor);
        return i < 0 ? -1 : announcePickup(i, cues, receiver, listenerDist);
    }
    // Inventory.AnnouncePickup: PlaySound(PickupSound) on the recipient. Returns the cue instance (-1 = none).
    int announcePickup(int i, SoundCues& cues, const SoundCues::Emitter& recipient, float listenerDist) {
        const char* cue = def(i).pickupSound;
        return cue ? cues.play(cue, recipient, listenerDist) : -1;
    }
};

} // namespace game
