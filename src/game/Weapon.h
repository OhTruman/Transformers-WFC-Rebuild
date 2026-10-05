// Clean-room reconstruction — a weapon instance (TnWeapon + its TnWeaponData). Defaults = the Ion Blaster's versus data
// (IonBlaster_WEPDATA, MultiplayerData) [CONF]; fromDef() fills any weapon from the generated WeaponDef table.
#pragma once
#include "game/WeaponDef.h"

namespace game {

struct Weapon {
    const char* name = "Ion Blaster";
    const WeaponDef* def = nullptr;         // null = the built-in Ion Blaster defaults
    WeaponFire fireType = WeaponFire::InstantHit;
    int  shots         = 1;        // NumShotsToFire (pellets per shot)
    bool autoFire      = true;     // bAutoFire
    float putDownTime  = 0.5f;     // PutDownTime
    float fineAimSpreadMult = 0.5f;   // FineAimSpreadModifier
    const char* damageType = "TransGame.TnDamageTypeIonBlaster";
    // Instant-hit weapons and projectile weapons with recovered PROJDATA are simulated; melee / grenade: PARTIAL.
    float projSpeed = 0.0f, projDamage = 0.0f, projRadiusM = 0.0f; bool projHoming = false;
    float homingForce = 0.0f, closingDistM = 0.0f, closingForce = 0.0f, closingTime = 0.0f, projMaxSpeed = 0.0f;
    float lockOnTime = 0.0f, holdLockOnTime = 0.0f; bool lockRobots = false;
    bool grenade() const { return fireType == WeaponFire::Grenade && def && def->tossStrength > 0.0f; }
    bool simulated() const { return fireType == WeaponFire::InstantHit || (fireType == WeaponFire::Projectile && projSpeed > 0.0f); }
    bool projectile() const { return fireType == WeaponFire::Projectile && projSpeed > 0.0f; }
    static Weapon fromDef(const WeaponDef& d) {
        Weapon w;
        w.def = &d; w.name = d.display; w.fireType = d.fire; w.shots = d.shots > 0 ? d.shots : 1; w.autoFire = d.autoFire;
        w.damage = d.damage; w.fireInterval = d.interval; w.magSize = d.clip; w.ammo = d.clip; w.reserveMax = d.maxAmmo;
        w.reserve = d.initialReserve; w.reloadTime = d.reloadTime; w.equipTime = d.equipTime; w.putDownTime = d.putDownTime;
        w.hitscan = d.fire == WeaponFire::InstantHit; w.rangeM = d.rangeM; w.falloffNearM = d.falloffNearM;
        w.falloffFarMul = d.falloffFarMul; w.spreadMin = d.spreadMin; w.spreadMax = d.spreadMax; w.spreadPerShot = d.spreadPerShot;
        w.spreadCooldown = d.spreadCooldown > 0.0f ? d.spreadCooldown : 2.0f; w.spread = d.spreadMin;
        w.fineAimSpreadMult = d.fineAimSpread; w.damageType = d.damageType;
        w.projSpeed = d.projSpeed; w.projDamage = d.projDamage; w.projRadiusM = d.projRadiusM; w.projHoming = d.projHoming;
        w.homingForce = d.homingForce; w.closingDistM = d.closingDistM; w.closingForce = d.closingForce; w.closingTime = d.closingTime;
        w.projMaxSpeed = d.projMaxSpeed; w.lockOnTime = d.lockOnTime; w.holdLockOnTime = d.holdLockOnTime; w.lockRobots = d.lockRobots;
        if (w.projectile() && d.projDamageType && *d.projDamageType) w.damageType = d.projDamageType;
        return w;
    }
    float damage        = 15.0f;    // [CONF] InstantHitDamage
    float fireInterval  = 0.065f;   // [CONF] FireIntervalModifier.IntervalRange (Min=Max)
    // Refire timer. [CONF, native RE] one-shot timer reset to zero after each shot; the weapon fires
    // when elapsed > FireInterval (strictly), the fractional overshoot is discarded, and at most one
    // shot fires per simulation tick. At 60 Hz this is a shot every 4th tick = ~900 RPM, which IS the
    // original runtime cadence (not the 923 RPM a remainder-carrying timer would give).
    float sinceShot     = 999.0f;
    int   magSize       = 50;       // [CONF] MaxAmmoClipCount
    int   ammo          = 50;
    int   reserveMax    = 250;      // [CONF] MaxAmmoCount
    int   reserve       = 150;      // [CONF] InitialReserveAmmoCount
    float reloadTime    = 1.5f;     // [CONF] WeaponReloadAnimTime
    float equipTime     = 0.2f;     // [CONF] EquipTime
    bool  hitscan       = true;     // [CONF] bInstantHit=true

    // Range + damage falloff. [CONF] WeaponRange 30000 UU = 300 m; 1.0x <=50 m -> 0.5x @300 m.
    float rangeM        = 300.0f;
    float falloffNearM  = 50.0f;    // RangeDamageModifiers[0].Range 5000 UU
    float falloffFarMul = 0.5f;     // RangeDamageModifiers[1].Modifier @30000 UU

    // Per-shot spread (radians-ish bloom fraction). [CONF] PerShotSpreadModifier.
    float spreadMin     = 0.08f;    // Modifier.Min
    float spreadMax     = 0.18f;    // Modifier.Max
    float spreadPerShot = 0.005f;   // ModifierChangePerShot
    float spreadCooldown = 2.0f;    // seconds of no-fire before spread decays
    float spread        = 0.08f;    // current bloom
    float sinceFire     = 999.0f;

    float reloadTimer   = 0.0f;

    // Event serials (WP_Fire / WP_Reload): incremented per event so presentation layers
    // (owner + weapon-mesh animation, recoil, FX, cues) can edge-detect without coupling.
    unsigned shotSerial   = 0;
    unsigned reloadSerial = 0;
    bool lowAmmo() const { return ammo <= lowAmmoThreshold; }
    int  lowAmmoThreshold = 5;      // [CONF] IonBlaster_WEPMESH.LowAmmoThreshold (WP_LowAmmoFire)

    void tick(float dt) {
        if (sinceShot < 999.0f) sinceShot += dt;
        sinceFire += dt;
        // TnWeapon.CooldownSpread every tick [CONF native M03 runtime, RE d50e2a9]: linear recovery across
        // the whole Min..Max range in Cooldown (2 s); IncrementSpread +0.005 per shot (onFired).
        spread -= (spreadMax - spreadMin) * dt / spreadCooldown;
        if (spread < spreadMin) spread = spreadMin;
        if (reloadTimer > 0) {
            reloadTimer -= dt;
            if (reloadTimer <= 0) finishReload();
        }
    }
    bool reloading() const { return reloadTimer > 0.0f; }
    bool canFire() const { return simulated() && sinceShot > fireInterval && ammo > 0 && !reloading(); }
    void onFired() {
        sinceShot = 0.0f;                       // reset to zero: overshoot discarded
        if (ammo > 0) --ammo;
        ++shotSerial;
        sinceFire = 0.0f;
        spread = spread + spreadPerShot < spreadMax ? spread + spreadPerShot : spreadMax;
    }
    // Damage after range falloff (linear 1.0 at <=near to falloffFarMul at range).
    float damageAt(float distM) const {
        if (distM <= falloffNearM) return damage;
        if (distM >= rangeM) return damage * falloffFarMul;
        float t = (distM - falloffNearM) / (rangeM - falloffNearM);
        return damage * (1.0f - t * (1.0f - falloffFarMul));
    }

    bool canReload() const { return !reloading() && ammo < magSize && reserve > 0; }
    void beginReload() { if (canReload()) { reloadTimer = reloadTime; ++reloadSerial; } }
    void finishReload() {
        int need = magSize - ammo;
        int take = need < reserve ? need : reserve;
        ammo += take;
        reserve -= take;
    }
};

} // namespace game
