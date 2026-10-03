// Clean-room reconstruction — the pickup SOUND of WFC pickup factories (Systems).
// Data: AssetTools streets_pickup_factories.json factory classes (gen_pickups.py -> PickupPresentation.inc)
// [CONF authored]: per factory class, its InventoryType and that inventory's PickupSound. Class data, not
// placement, so any map's factories of these classes resolve the same way.
// Behaviour [CONF script]: Engine.PickupFactory GiveTo -> SpawnCopyFor -> Inventory.AnnouncePickup:
// Other.PlaySound(PickupSound), a sound attached to the RECEIVING pawn; a respawn plays nothing (RespawnEffect
// empty, RE d50c2a9 P2). Flag / bomb objective inventories author no PickupSound.
// OWNERSHIP:
//   * Gameplay owns the factory state machine (agents/gameplay PickupFactory, World::pickupEvents()).
//   * Rendering owns the pickup effects AND their runtime state (WfcMapFx setMapEffectState).
//   * Systems only plays the PickupSound on each Gameplay Taken event (onTaken, keyed by factory class).
#pragma once
#include "game/SoundCues.h"

namespace game {

class PickupPresentation {
public:
    struct PickupClassSound { const char* factoryClass; const char* inventoryClass; const char* pickupSound; };
    static int classCount();
    static const PickupClassSound& classDef(int i);
    // PickupSound (SoundCues table name) of a factory class, null if none authored / unknown class.
    static const char* pickupSoundFor(const char* factoryClass);

    // Gameplay PickupEvent Taken: Inventory.AnnouncePickup (PickupSound on the receiving pawn). Returns the
    // sound's cue instance (-1 = no PickupSound / unknown class).
    static int onTaken(const char* factoryClass, SoundCues& cues, const SoundCues::Emitter& receiver, float listenerDist) {
        const char* cue = pickupSoundFor(factoryClass);
        return cue ? cues.play(cue, receiver, listenerDist) : -1;
    }
};

} // namespace game
