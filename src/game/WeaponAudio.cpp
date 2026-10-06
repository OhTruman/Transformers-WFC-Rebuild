#include "game/WeaponAudio.h"
#include "game/CharacterAudio.h"

namespace game {

int WeaponAudio::fire(SoundCues& cues, const std::string& cls, bool lowAmmo, const SoundCues::Emitter& em, float ownDist) {
    if (CharacterAudio::weaponIsBeam(cls)) return -1;              // the beam's loops carry its sound
    const std::string* q = &CharacterAudio::weaponCue(cls, lowAmmo ? "WP_LowAmmoFire" : "WP_Fire");
    if (q->empty() && lowAmmo) q = &CharacterAudio::weaponCue(cls, "WP_Fire");
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

void WeaponAudio::stopAll(SoundCues& cues) {
    for (auto& kv : flight_) cues.stop(kv.second.instance, 0.0f);
    flight_.clear();
    for (int* id : {&beamLoop_, &beamHeal_, &beamDamage_}) { if (*id >= 0) cues.stop(*id, 0.0f); *id = -1; }
    beamClass_.clear();
}

} // namespace game
