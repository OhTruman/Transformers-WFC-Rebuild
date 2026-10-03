// Clean-room reconstruction — WFC pickup factories (TnPickupFactory family) for the authored Streets
// population. Provenance: AssetTools 7a69756 manifests/streets_pickup_factories.json (CONFIRMED AUTHORED
// DATA unless marked) and the slice's gameplay.json placement.
//
//   class                       inventory              RespawnTime  payload
//   TnAmmoCratePickupFactory    TnAmmoCratePickup      30 s         refill ValidWeaponTypes Primary/Secondary/Vehicle
//   TnHealthPickupFactory       TnHealthPickup         60 s         AddedHealth 50
//   TnOverShieldPickupFactory   TnOverShieldPickup     120 s        overshield (amount/duration native: PARTIAL)
//   TnGameObjectivePickupFactoryFlag/Bomb: RequiredGameRuleClass SingleFlagCTF / ScoreBombingRun — inactive
//   in the slice's game mode (not instantiated; multiplayer modes are out of scope).
//
// States (Engine.PickupFactory): 'Pickup' (available, touchable) <-> 'Sleeping' (taken, RespawnTime to go).
// Touch: COLLIDE_TouchAll, CylinderComponent radius 200 / height 100 UU at the factory location.
// Each transition raises exactly one PickupEvent (SeqEvent_PickupStatusChange semantics) that the
// presentation lanes consume: Systems (PickupSound / PickupObtainedDialog cues) and Rendering (FX:
// health/overshield CustomPickupEffect visible while available; ammo crate mesh + inactive Pickup_FX).
// Gameplay does not draw or play anything for pickups.
// Native (not recovered): TakePickUp / GiveTo / ValidTouch / IsReadyToPickup bodies — see [PROV] below.
#pragma once
#include "game/Actor.h"
#include "core/Math.h"

#include <string>

namespace game {

class World;
class Character;

struct PickupEvent {
    enum class Kind { AmmoCrate, Health, OverShield };
    enum class Type { Taken, Respawned };
    int factory = -1;          // index into World::pickupFactories()
    Kind kind = Kind::Health;
    Type type = Type::Taken;
    bool available = false;    // PickupStatusChange bPickupAvailable after the transition (= !bPickupHidden)
    core::Vec3 pos{0, 0, 0};
    // Taken only: AnnouncePickup -> P.PlaySound(PickupSound) plays on the RECEIVING pawn, at receiverPos.
    // Respawned carries no sound (RespawnEffect is empty, RespawnEffectTime 0) [CONF RE d50e2a9].
    const char* pickupSound = nullptr;
    core::Vec3 receiverPos{0, 0, 0};
    // Visual state after the transition (SetPickupVisible / SetPickupHidden): mesh, CustomPickupEffect and,
    // only where ShouldDisplayHighlightFx, the PickupEffect highlight beam.
    bool meshVisible = false, customEffectActive = false, beamActive = false;
};

class PickupFactory : public Actor {
public:
    using Kind = PickupEvent::Kind;
    PickupFactory(const std::string& name, Kind kind, const core::Vec3& loc, float respawnTime, int index)
        : name_(name), kind_(kind), respawnTime_(respawnTime), index_(index) { pos_ = loc; }

    // Per-step factory logic (World ticks actors): respawn timer while sleeping; touch test while available.
    void tick(World& world, float dt) override;
    // PickupFactory.Reset (TnTeamGame.StartMatch): a sleeping factory returns to 'Pickup' at once [HIGH: stock UE3].
    void resetToPickup(World& world);

    Kind kind() const { return kind_; }
    const std::string& name() const { return name_; }
    bool available() const { return available_; }
    float respawnRemaining() const { return respawnRemain_; }
    float respawnTime() const { return respawnTime_; }
    // Visual state for Rendering/Systems (SetPickupVisible / SetPickupHidden) [CONF RE d50e2a9]:
    bool meshVisible() const { return available_; }
    // CustomPickupEffect (HealthPickup_FX / OvershieldPickup_FX): shown and active only while available.
    bool customEffectActive() const { return available_ && kind_ != Kind::AmmoCrate; }
    // PickupEffect (Pickup_FX) highlight beam: ActivateSystem in SetPickupVisible only when
    // ShouldDisplayHighlightFx, which is true only for the ammo-crate (weapon) factory.
    bool beamActive() const { return available_ && shouldDisplayHighlightFx(kind_); }
    static bool shouldDisplayHighlightFx(Kind k) { return k == Kind::AmmoCrate; }
    // Authored inventory data.
    static const char* className(Kind k);
    static const char* pickupSound(Kind k);
    static constexpr float kTouchRadius = 2.0f;     // CylinderComponent CollisionRadius 200 UU
    static constexpr float kTouchHalfHeight = 1.0f; // CollisionHeight 100 UU
    static constexpr float kAddedHealth = 50.0f;    // TnHealthPickup.AddedHealth

private:
    std::string name_;
    Kind kind_;
    float respawnTime_;
    int index_;
    bool available_ = true;
    float traceRecheck_ = 0.0f;   // ValidTouch FastTrace failure: retry after 0.5 s while still touching
    float respawnRemain_ = 0.0f;
    bool overlapping_ = false;      // the player pawn's cylinder overlapped last step (Touch = overlap begin)
    bool overlaps(const Character& c) const;
    bool tryGive(Character& c);
    void take(World& world, Character& c);
};

} // namespace game
