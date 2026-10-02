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
    bool available = false;    // PickupStatusChange bPickupAvailable after the transition
    core::Vec3 pos{0, 0, 0};
    const char* pickupSound = nullptr;   // authored PickupSound of the inventory class (Taken only)
};

class PickupFactory : public Actor {
public:
    using Kind = PickupEvent::Kind;
    PickupFactory(const std::string& name, Kind kind, const core::Vec3& loc, float respawnTime, int index)
        : name_(name), kind_(kind), respawnTime_(respawnTime), index_(index) { pos_ = loc; }

    // Per-step factory logic (World ticks actors): respawn timer while sleeping; touch test while available.
    void tick(World& world, float dt) override;

    Kind kind() const { return kind_; }
    const std::string& name() const { return name_; }
    bool available() const { return available_; }
    float respawnRemaining() const { return respawnRemain_; }
    float respawnTime() const { return respawnTime_; }
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
    float respawnRemain_ = 0.0f;
    bool tryGive(Character& c);
};

} // namespace game
