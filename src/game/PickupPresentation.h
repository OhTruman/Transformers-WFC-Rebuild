// Clean-room reconstruction — the presentation side (effect activation + pickup sound) of the 27 placed
// MP_IAC_Streets pickup factories. Data: AssetTools 7a69756 streets_pickup_factories.json (gen_pickups.py ->
// PickupPresentation.inc) [CONF authored]. Behaviour: decompiled script (RE-Workspace, read-only) [CONF]:
//   * Spawn [CONF, RE d50c2a9 P2]: PreBeginPlay -> InitializePickup -> SetPickupMesh -> SetPickupVisible (the
//     ammo crate creates its PickupFactoryMesh), so the factory starts available with CustomPickupEffect active
//     and the highlight beam ACTIVE where ShouldDisplayHighlightFx (ammo crates); the template's bAutoActivate
//     false is overridden by that call. (Health / overshield create no mesh; their CustomPickupEffect is active
//     by bAutoActivate - the same state.) PickupEffect is attached (rendered) only for classes that list it in
//     Components (ammo crate, objectives), never for health / overshield.
//   * TnPickupFactory.SetPickupHidden: CustomPickupEffect.SetHidden(true) + DeactivateSystem(); if
//     ShouldDisplayHighlightFx: PickupEffect.DeactivateSystem().
//   * TnPickupFactory.SetPickupVisible: CustomPickupEffect.SetHidden(false) + ActivateSystem(); if
//     ShouldDisplayHighlightFx: PickupEffect.ActivateSystem().
//   * Engine.PickupFactory: GiveTo -> SpawnCopyFor (Inventory.AnnouncePickup: Other.PlaySound(PickupSound),
//     a sound attached to the recipient pawn) -> PickedUpBy -> SetRespawn -> Sleeping (BeginState:
//     SetPickupHidden; EndState after RespawnTime: SetPickupVisible).
// OWNERSHIP: the factory actors, touch validation, Sleeping/respawn timing and game-rule gating are Gameplay's
// state machine (agents/gameplay PickupFactory, World::pickupEvents()); this class keeps no timers. Each
// Gameplay PickupEvent maps to onTaken (Taken: AnnouncePickup + SetPickupHidden) or onRespawned (Respawned:
// SetPickupVisible), keyed by the factory actor name. Drawing the decoded particle systems is not done here: the
// compiled-module flag semantics (pstream flagA / flagB, modules outside the record list) are UNKNOWN, so the
// emitters cannot be reproduced without guessing; effectState() exposes what the original has active.
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
    struct EffectState {
        bool customActive = false, customHidden = false;   // CustomPickupEffect
        bool highlightActive = false;                       // PickupEffect (meaningful only when attached)
    };

    PickupPresentation();
    static int count();
    static const FactoryDef& def(int i);

    void reset();                        // map start: available (PreBeginPlay SetPickupVisible)
    static int find(const char* actor);  // factory index by actor name (e.g. "TnAmmoCratePickupFactory_10561")
    // Gameplay PickupEvent adapters. onTaken: Inventory.AnnouncePickup (PickupSound on the receiving pawn) then
    // Sleeping.BeginState SetPickupHidden; returns the sound's cue instance (-1 = none / unknown factory).
    int onTaken(const char* actor, SoundCues& cues, const SoundCues::Emitter& receiver, float listenerDist) {
        int i = find(actor);
        if (i < 0) return -1;
        int id = announcePickup(i, cues, receiver, listenerDist);
        setPickupHidden(i);
        return id;
    }
    // onRespawned: Sleeping.EndState SetPickupVisible (no respawn sound: RespawnEffect empty).
    bool onRespawned(const char* actor) { int i = find(actor); if (i >= 0) setPickupVisible(i); return i >= 0; }
    void setPickupHidden(int i);         // TnPickupFactory.SetPickupHidden
    void setPickupVisible(int i);        // TnPickupFactory.SetPickupVisible
    // Inventory.AnnouncePickup: PlaySound(PickupSound) on the recipient. Returns the cue instance (-1 = none).
    int announcePickup(int i, SoundCues& cues, const SoundCues::Emitter& recipient, float listenerDist) {
        const char* cue = def(i).pickupSound;
        return cue ? cues.play(cue, recipient, listenerDist) : -1;
    }
    const EffectState& effectState(int i) const { return state_[(size_t)i]; }

private:
    std::vector<EffectState> state_;
};

} // namespace game
