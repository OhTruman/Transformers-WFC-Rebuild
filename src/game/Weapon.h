// Clean-room reconstruction — Ion Blaster (SMG). All values below are [CONF] recovered from
// WEP_IonBlaster package metadata (ExtractedAssets/.../weapon.json) unless marked otherwise.
#pragma once

namespace game {

struct Weapon {
    const char* name = "Ion Blaster";
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
    bool canFire() const { return sinceShot > fireInterval && ammo > 0 && !reloading(); }
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
