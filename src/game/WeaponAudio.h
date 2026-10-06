// Clean-room reconstruction — weapon sound lifecycle by weapon IDENTITY (the class actually fired, robot or vehicle
// weapon), never a fallback weapon's (Systems M08d). Gameplay owns firing / projectile / beam state and reports it;
// this owns the sound instances.
//   fire        TnWeapon.PlayFiringSound: WP_Fire / WP_LowAmmoFire for every fire type (instant hit AND projectile
//               launch) [CONF data WeaponSounds]; beam weapons play no per-shot sound (their loops below).
//   beam        TnWeaponBeam / TnWeaponRepair [CONF script]: while firing WP_Looping (GetWeaponEventForFireMode 9);
//               OnPlayFireEffects per hit: friendly target -> stop WP_FireSecondary, play WP_Fire (heal loop); enemy
//               -> stop WP_Fire, play WP_FireSecondary (damage loop); nothing -> stop both. StopFireEffects: stop
//               WP_Fire / WP_FireSecondary / WP_Looping, play WP_LoopingTail. Loops use the event's authored
//               LoopingFadeInTime / LoopingFadeOutTime. [HIGH: the EWeaponEvent indices 1 / 2 / 9 / 12 = WP_Fire /
//               WP_FireSecondary / WP_Looping / WP_LoopingTail, consistent with the Repair Ray's authored events]
//   projectile  HmProjectile [CONF script]: FlightSound attached from spawn; Explode: flight sound FadeOut(0.25),
//               ExplosionSound at the projectile; a projectile removed without exploding stops its flight sound.
// Every instance this owns is stopped by stopAll() (death, class change, match restart, map unload).
#pragma once
#include <map>
#include <string>
#include "core/Math.h"
#include "game/SoundCues.h"

namespace game {

class WeaponAudio {
public:
    enum class BeamTarget { None, Friendly, Enemy };

    // One shot of `weaponClass` (instant hit or projectile launch) at `emitter`. Returns the instance or -1.
    int fire(SoundCues& cues, const std::string& weaponClass, bool lowAmmo, const SoundCues::Emitter& emitter, float ownDist);

    // A beam weapon's state, every tick while it is held (firing = the trigger is down and the beam is up).
    void beam(SoundCues& cues, const std::string& weaponClass, bool firing, BeamTarget target, const SoundCues::Emitter& emitter);
    bool beamActive() const { return beamLoop_ >= 0; }

    // Projectiles: `key` is the caller's projectile identity.
    void projectileSpawned(SoundCues& cues, int key, const std::string& weaponClass, const core::Vec3& pos, float listenerDist);
    void projectileMoved(SoundCues& cues, int key, const core::Vec3& pos);
    // Returns the explosion instance (-1: none authored).
    int projectileExploded(SoundCues& cues, int key, const std::string& weaponClass, const core::Vec3& pos, float listenerDist);
    void projectileRemoved(SoundCues& cues, int key);
    int flightLoops() const { return (int)flight_.size(); }

    void stopAll(SoundCues& cues);

private:
    struct Flight { int instance; std::string weaponClass; };
    std::map<int, Flight> flight_;
    int beamLoop_ = -1, beamHeal_ = -1, beamDamage_ = -1;
    std::string beamClass_;
    void stopBeamLoop(SoundCues& cues, int& id, const char* ev);
};

} // namespace game
