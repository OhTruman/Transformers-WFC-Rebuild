// Clean-room reconstruction — the presentation side (effect activation + pickup sound) of the 27 placed
// MP_IAC_Streets pickup factories. Data: AssetTools 7a69756 streets_pickup_factories.json (gen_pickups.py ->
// PickupPresentation.inc) [CONF authored]. Behaviour: decompiled script (RE-Workspace, read-only) [CONF]:
//   * Spawn: components activate per bAutoActivate - CustomPickupEffect (HealthPickup_FX / OvershieldPickup_FX,
//     ParticleSystemComponent default true) is active; PickupEffect (Pickup_FX highlight beam, TnPickupFactory
//     archetype bAutoActivate=false) is NOT. PickupEffect is attached (rendered) only for classes that list it
//     in Components (ammo crate, objectives), never for health / overshield.
//   * TnPickupFactory.SetPickupHidden: CustomPickupEffect.SetHidden(true) + DeactivateSystem(); if
//     ShouldDisplayHighlightFx: PickupEffect.DeactivateSystem().
//   * TnPickupFactory.SetPickupVisible: CustomPickupEffect.SetHidden(false) + ActivateSystem(); if
//     ShouldDisplayHighlightFx: PickupEffect.ActivateSystem(). So the ammo-crate beam first lights up when the
//     crate respawns, not at map start.
//   * Engine.PickupFactory: GiveTo -> SpawnCopyFor (Inventory.AnnouncePickup: Other.PlaySound(PickupSound),
//     a sound attached to the recipient pawn) -> PickedUpBy -> SetRespawn -> Sleeping (BeginState:
//     SetPickupHidden; EndState after RespawnTime: SetPickupVisible).
// OWNERSHIP: the factory actors, touch validation, Sleeping/respawn timing and game-rule gating are Gameplay's
// state machine; this class keeps no timers. Gameplay calls announcePickup / setPickupHidden /
// setPickupVisible at the script points above. Drawing the decoded particle systems is not done here: the
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

    void reset();                        // map start: bAutoActivate states
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
