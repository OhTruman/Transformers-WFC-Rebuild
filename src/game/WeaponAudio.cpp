#include "game/WeaponAudio.h"
#include "game/CharacterAudio.h"

namespace game {

int WeaponAudio::fire(SoundCues& cues, const std::string& cls, bool lowAmmo, const SoundCues::Emitter& em, float ownDist,
                      int fireMode) {
    if (CharacterAudio::weaponIsBeam(cls)) return -1;              // the beam's loops carry its sound
    static const char* const kFire[3] = {"WP_Fire", "WP_FireSecondary", "WP_FireTertiary"};
    static const char* const kLow[3] = {"WP_LowAmmoFire", "WP_LowAmmoFireSecondary", "WP_LowAmmoFireTertiary"};
    const int m = fireMode < 0 || fireMode > 2 ? 0 : fireMode;
    const std::string* q = &CharacterAudio::weaponCue(cls, lowAmmo ? kLow[m] : kFire[m]);
    if (q->empty() && lowAmmo) q = &CharacterAudio::weaponCue(cls, kFire[m]);
    if (q->empty()) return -1;
    return cues.play(q->c_str(), em, ownDist);
}

void WeaponAudio::stopBeamLoop(SoundCues& cues, int& id, const char* ev) {
    if (id < 0) return;
    float fi, fo;
    CharacterAudio::weaponEventFades(beamClass_, ev, fi, fo);
    cues.stop(id, fo);
    id = -1;
}

void WeaponAudio::beam(SoundCues& cues, const std::string& cls, bool firing, BeamTarget target, const SoundCues::Emitter& em) {
    auto start = [&](int& id, const char* ev) {
        if (id >= 0 && cues.playing(id)) return;
        const std::string& q = CharacterAudio::weaponCue(cls, ev);
        if (q.empty()) return;
        float fi, fo;
        CharacterAudio::weaponEventFades(cls, ev, fi, fo);
        id = cues.play(q.c_str(), em, 0.0f);
        if (id >= 0 && fi > 0.0f) cues.fadeIn(id, fi);
    };
    if (firing && beamClass_ != cls && beamLoop_ >= 0) beam(cues, beamClass_, false, BeamTarget::None, em);   // weapon swapped
    if (!firing) {
        if (beamLoop_ < 0 && beamHeal_ < 0 && beamDamage_ < 0) return;
        stopBeamLoop(cues, beamHeal_, "WP_Fire");                   // StopFireEffects
        stopBeamLoop(cues, beamDamage_, "WP_FireSecondary");
        stopBeamLoop(cues, beamLoop_, "WP_Looping");
        const std::string& tail = CharacterAudio::weaponCue(beamClass_, "WP_LoopingTail");
        if (!tail.empty()) cues.play(tail.c_str(), em, 0.0f);
        return;
    }
    beamClass_ = cls;
    start(beamLoop_, "WP_Looping");
    if (target == BeamTarget::Friendly) { stopBeamLoop(cues, beamDamage_, "WP_FireSecondary"); start(beamHeal_, "WP_Fire"); }
    else if (target == BeamTarget::Enemy) { stopBeamLoop(cues, beamHeal_, "WP_Fire"); start(beamDamage_, "WP_FireSecondary"); }
    else { stopBeamLoop(cues, beamHeal_, "WP_Fire"); stopBeamLoop(cues, beamDamage_, "WP_FireSecondary"); }
}

void WeaponAudio::projectileSpawned(SoundCues& cues, int key, const std::string& cls, const core::Vec3& pos, float dist) {
    projectileRemoved(cues, key);
    const WeaponProjectile* p = CharacterAudio::weaponProjectile(cls);
    if (!p || p->flightSound.empty()) return;
    SoundCues::Emitter e;
    e.pos = pos;                                                    // kWorld: moved with the projectile
    const int id = cues.play(p->flightSound.c_str(), e, dist);
    if (id >= 0) flight_[key] = {id, cls};
}

void WeaponAudio::projectileMoved(SoundCues& cues, int key, const core::Vec3& pos) {
    auto it = flight_.find(key);
    if (it != flight_.end()) cues.update(it->second.instance, pos, 0.0f);
}

int WeaponAudio::projectileExploded(SoundCues& cues, int key, const std::string& cls, const core::Vec3& pos, float dist) {
    auto it = flight_.find(key);
    if (it != flight_.end()) { cues.stop(it->second.instance, 0.25f); flight_.erase(it); }   // FadeOut(0.25, 0)
    const WeaponProjectile* p = CharacterAudio::weaponProjectile(cls);
    if (!p || p->explosionSound.empty()) return -1;
    return cues.play(p->explosionSound.c_str(), pos, dist);           // PlaySound(ExplosionSound) on the projectile
}

void WeaponAudio::projectileHitWall(SoundCues& cues, const std::string& cls, const core::Vec3& pos, float dist, bool fuseStarted) {
    const WeaponProjectile* p = CharacterAudio::weaponProjectile(cls);
    if (!p) return;
    if (fuseStarted && !p->fuseSound.empty()) cues.play(p->fuseSound.c_str(), pos, dist);
    if (!p->bounceSound.empty()) cues.play(p->bounceSound.c_str(), pos, dist);
}

void WeaponAudio::projectileRemoved(SoundCues& cues, int key) {
    auto it = flight_.find(key);
    if (it == flight_.end()) return;
    cues.stop(it->second.instance, 0.25f);
    flight_.erase(it);
}

namespace { const char* const kChargeLoops[3] = {"WP_Looping", "WP_LoopingSecondary", "WP_LoopingTertiary"}; }

void WeaponAudio::stopChargeLoop(SoundCues& cues, int slot) {
    if (charge_[slot] < 0) return;
    float fi, fo;
    CharacterAudio::weaponEventFades(chargeClass_, kChargeLoops[slot], fi, fo);
    cues.stop(charge_[slot], fo);
    charge_[slot] = -1;
}

void WeaponAudio::chargeState(SoundCues& cues, const std::string& cls, int state, const SoundCues::Emitter& em) {
    if (state == chargeState_ && cls == chargeClass_) return;
    if (state == 0 && chargeState_ == 0) { chargeClass_ = cls; return; }    // idle -> idle (another weapon held): nothing
    if (!chargeClass_.empty() && cls != chargeClass_ && chargeState_ != 0) chargeState(cues, chargeClass_, 0, em);   // swapped
    chargeClass_ = cls;
    chargeState_ = state;
    auto start = [&](int slot) {
        const std::string& q = CharacterAudio::weaponCue(cls, kChargeLoops[slot]);
        if (q.empty() || (charge_[slot] >= 0 && cues.playing(charge_[slot]))) return;
        float fi, fo;
        CharacterAudio::weaponEventFades(cls, kChargeLoops[slot], fi, fo);
        charge_[slot] = cues.play(q.c_str(), em, 0.0f);
        if (charge_[slot] >= 0 && fi > 0.0f) cues.fadeIn(charge_[slot], fi);
    };
    switch (state) {
    case 1: start(0); break;                                          // Charging.BeginState: PlayWeaponEvent(9)
    case 2: break;                                                    // level 1: muzzle flash only
    case 3: stopChargeLoop(cues, 0); start(1); break;                 // AllWeaponMeshesStopSoundEvent(9) + 10
    case 4: stopChargeLoop(cues, 1); start(2); break;                 // stop 10, play 11
    default: {                                                        // Charging.EndState
        for (int s = 0; s < 3; ++s) stopChargeLoop(cues, s);
        const std::string& tail = CharacterAudio::weaponCue(cls, "WP_LoopingTail");
        if (!tail.empty()) cues.play(tail.c_str(), em, 0.0f);
    }
    }
}

int WeaponAudio::chargeFizzle(SoundCues& cues, const std::string& cls, const SoundCues::Emitter& em) {
    const std::string& q = CharacterAudio::weaponCue(cls, "WP_NoAmmoFire");
    return q.empty() ? -1 : cues.play(q.c_str(), em, 0.0f);
}

int WeaponAudio::chargeLoops(const SoundCues& cues) const {
    int n = 0;
    for (int id : charge_) if (id >= 0 && cues.playing(id)) ++n;
    return n;
}

void WeaponAudio::stopAll(SoundCues& cues) {
    for (int& id : charge_) { if (id >= 0) cues.stop(id, 0.0f); id = -1; }
    chargeState_ = 0; chargeClass_.clear();
    for (auto& kv : flight_) cues.stop(kv.second.instance, 0.0f);
    flight_.clear();
    for (int* id : {&beamLoop_, &beamHeal_, &beamDamage_}) { if (*id >= 0) cues.stop(*id, 0.0f); *id = -1; }
    beamClass_.clear();
}

} // namespace game
