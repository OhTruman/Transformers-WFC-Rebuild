#include "game/PickupFactory.h"
#include "game/Character.h"
#include "game/World.h"

#include <cmath>

namespace game {

const char* PickupFactory::className(Kind k) {
    switch (k) {
        case Kind::AmmoCrate: return "TnAmmoCratePickupFactory";
        case Kind::Health: return "TnHealthPickupFactory";
        case Kind::OverShield: return "TnOverShieldPickupFactory";
    }
    return "";
}

// Inventory CDO PickupSound [CONF authored].
const char* PickupFactory::pickupSound(Kind k) {
    switch (k) {
        case Kind::AmmoCrate: return "BL_HUD_INTERFACE.HEALTH_PU_AMMO";
        case Kind::Health: return "BL_HUD_INTERFACE.HEALTH_PU_ENERGON";
        case Kind::OverShield: return "BL_HUD_INTERFACE.OVERSHIELD_POWER_UP";
    }
    return nullptr;
}

// GiveTo / ValidTouch are native and were not recovered: the acceptance rules below are [PROV]
// (standard UE3 pickup semantics: a pickup that would do nothing is not consumed).
bool PickupFactory::tryGive(Character& c) {
    if (c.health().isDead()) return false;
    switch (kind_) {
        case Kind::Health:
            if (c.health().current >= c.health().max) return false;     // [PROV] nothing to heal
            c.health().heal(kAddedHealth);                               // AddedHealth 50 [CONF]
            return true;
        case Kind::AmmoCrate: {
            // ValidWeaponTypes WT_Primary/WT_Secondary/WT_Vehicle [CONF]; amount native -> [PROV] refill the
            // reserve to MaxAmmoCount. The slice's only weapon is the Ion Blaster (primary).
            Weapon& w = c.weapon();
            if (w.reserve >= w.reserveMax) return false;                 // [PROV]
            w.reserve = w.reserveMax;
            return true;
        }
        case Kind::OverShield:
            c.grantOverShield();                                         // amount/duration native [PARTIAL]
            return true;
    }
    return false;
}

bool PickupFactory::overlaps(const Character& c) const {
    core::Vec3 a = c.actorLocation();
    float pr = c.moveForm() == Form::Robot ? core::config::kPawnRadius : 3.34f;   // vehicle: CalculateCylinderBounds
    float ph = c.moveForm() == Form::Robot ? core::config::kPawnHalfHeight : 1.22f;
    float dx = a.x - pos_.x, dz = a.z - pos_.z;
    return std::sqrt(dx * dx + dz * dz) <= pr + kTouchRadius && std::fabs(a.y - pos_.y) <= ph + kTouchHalfHeight;
}

// GiveTo -> PickedUpBy -> SetRespawn -> StartSleeping: the same frame enters 'Sleeping' (SetPickupHidden).
void PickupFactory::take(World& world, Character& c) {
    available_ = false;
    respawnRemain_ = respawnTime_;
    PickupEvent e; e.factory = index_; e.kind = kind_; e.type = PickupEvent::Type::Taken;
    e.available = false; e.pos = pos_; e.pickupSound = pickupSound(kind_); e.receiverPos = c.actorLocation();
    world.raisePickupEvent(e);
}

// Engine.PickupFactory / TnPickupFactory / TnHealthPickupFactory state machine [CONF RE d50e2a9]:
//  'Pickup'  : visible, ammo crate rotating (PickupRotationRate Yaw 10000), Touch -> ValidTouch -> GiveTo.
//  'Sleeping': hidden; the touch collision stays, touches are ignored; Sleep(RespawnTime) exactly, then
//              back to 'Pickup' (SetPickupVisible). TnHealthPickupFactory.SetPickupVisible then CheckTouching:
//              a pawn already standing on it is re-tested at once. The actor is never destroyed.
// Touch is an overlap BEGIN, so other factories do not re-grant to a pawn that was already standing on them.
void PickupFactory::tick(World& world, float dt) {
    Character& c = world.player().pawn();
    bool overlapNow = overlaps(c);
    bool began = overlapNow && !overlapping_;
    overlapping_ = overlapNow;
    if (!available_) {
        respawnRemain_ -= dt;
        if (respawnRemain_ > 0.0f) return;                         // touches ignored while sleeping
        respawnRemain_ = 0.0f;
        available_ = true;
        PickupEvent e; e.factory = index_; e.kind = kind_; e.type = PickupEvent::Type::Respawned;
        e.available = true; e.pos = pos_;
        e.meshVisible = true; e.customEffectActive = customEffectActive(); e.beamActive = beamActive();
        world.raisePickupEvent(e);
        if (kind_ == Kind::Health && overlapNow && tryGive(c)) take(world, c);   // CheckTouching
        return;
    }
    if (kind_ == Kind::AmmoCrate) yaw_ += 10000.0f / 65536.0f * 6.2831853f * dt;   // PHYS_Rotating in 'Pickup'
    if (began && tryGive(c)) take(world, c);
}

} // namespace game
