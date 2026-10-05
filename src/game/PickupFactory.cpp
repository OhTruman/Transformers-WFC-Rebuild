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

// PickupQuery -> InvManager.HandlePickupQuery -> ItemClass.PickupAllowed [CONF RE MILESTONE05_GAMEPLAY_UNKNOWNS §6]:
// health only below HealthMax (SHT_AddAllSegments: full heal, AddedHealth ignored), overshield only while
// NormalizedOverShieldHealth < 1 (SHT_AddOverShield: HealthMax + 550), ammo crate only when a primary / secondary /
// vehicle weapon is not AmmoMaxed (FillReserveAmmo). No MP game rule overrides PickupQuery.
bool PickupFactory::tryGive(Character& c) {
    if (c.health().isDead()) return false;
    switch (kind_) {
        case Kind::Health:
            if (c.health().current >= c.health().max) return false;
            c.health().heal(Health::HealType::AddAllSegments, kAddedHealth);
            return true;
        case Kind::AmmoCrate: {
            // ValidWeaponTypes WT_Primary/WT_Secondary/WT_Vehicle [CONF]; amount native -> [PROV] refill the
            // reserve to MaxAmmoCount. The slice's only weapon is the Ion Blaster (primary).
            Weapon& w = c.weapon();
            if (w.reserve >= w.reserveMax) return false;                 // AmmoMaxed
            w.reserve = w.reserveMax;
            return true;
        }
        case Kind::OverShield:
            if (c.health().normalizedOverShield() >= 1.0f) return false;
            c.health().heal(Health::HealType::AddOverShield, 1.0f);
            c.grantOverShield();
            return true;
    }
    return false;
}

bool PickupFactory::overlaps(const Character& c) const {
    core::Vec3 a = c.actorLocation();
    float pr = c.cylinderRadius(c.moveForm());     // vehicle: CalculateCylinderBounds
    float ph = c.cylinderHalfHeight(c.moveForm());
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
    // Pickup.ValidTouch: a touch through a wall (FastTrace pawn -> factory fails) is rejected and re-checked in 0.5 s.
    if (began) traceRecheck_ = 0.0f;
    if (!overlapNow) return;
    if (traceRecheck_ > 0.0f) { traceRecheck_ -= dt; if (traceRecheck_ > 0.0f) return; }
    else if (!began) return;
    const CollisionWorld* lw = world.weaponCollision();
    float th;
    if (lw && lw->segmentHit(c.actorLocation(), pos_ + core::Vec3{0, 1.0f, 0}, th)) { traceRecheck_ = 0.5f; return; }
    traceRecheck_ = 0.0f;
    if (tryGive(c)) take(world, c);
}

void PickupFactory::resetToPickup(World& world) {
    if (available_) return;
    available_ = true;
    respawnRemain_ = 0.0f;
    PickupEvent e; e.factory = index_; e.kind = kind_; e.type = PickupEvent::Type::Respawned;
    e.available = true; e.pos = pos_;
    e.meshVisible = true; e.customEffectActive = customEffectActive(); e.beamActive = beamActive();
    world.raisePickupEvent(e);
}

} // namespace game
