#include "game/AbilityAudio.h"
#include "game/CharacterAudio.h"
#include "core/Log.h"
#include <cstdlib>

namespace game {

std::string AbilityAudio::abilityClass(const std::string& id) {
    return id.rfind("TnAbility", 0) == 0 ? id : "TnAbility" + id;
}

int AbilityAudio::abilityTriggered(SoundCues& cues, const std::string& id, const SoundCues::Emitter& pawn, float dist) {
    const std::string& q = CharacterAudio::abilityTriggerSound(abilityClass(id));
    static const bool log = std::getenv("WFC_CUELOG") != nullptr;
    if (log) LOG_INFO("ability audio: %s triggered -> %s", id.c_str(), q.empty() ? "(no OnTriggerSound)" : q.c_str());
    return q.empty() ? -1 : cues.play(q.c_str(), pawn, dist);
}

AbilityAudio::Live& AbilityAudio::slot(int key, const std::string& buff) {
    for (Live& l : live_) if (l.key == key && l.buff == buff) return l;
    live_.push_back(Live{key, buff});
    return live_.back();
}

bool AbilityAudio::setBuff(SoundCues& cues, const std::string& buffClass, const Owner& o, bool active) {
    Live& l = slot(o.key, buffClass);
    if (l.active == active) return false;
    l.active = active;
    const BuffSounds* b = CharacterAudio::buffSounds(buffClass);
    if (!b || (b->onlyLocal && !o.local)) return false;                 // CanPlaySounds
    const bool decepticon = o.team == 1;
    if (active) {
        const std::string& q = !b->apply.empty() ? b->apply : (decepticon ? b->decepticonApply : b->autobotApply);
        if (q.empty() || (l.inst >= 0 && cues.playing(l.inst))) return false;   // re-apply while playing: no restart
        l.inst = cues.play(q.c_str(), o.at, o.listenerDist);
        return l.inst >= 0;
    }
    if (l.inst >= 0) { cues.stop(l.inst, 0.0f); l.inst = -1; }
    const std::string& q = !b->unapply.empty() ? b->unapply : (decepticon ? b->decepticonUnapply : b->autobotUnapply);
    if (!q.empty()) cues.play(q.c_str(), o.at, o.listenerDist);
    return false;
}

void AbilityAudio::pawnDied(SoundCues& cues, int key) {
    auto h = hover_.find(key);                                        // the pawn's hover: stops, no land
    if (h != hover_.end()) { if (h->second.loop >= 0) cues.stop(h->second.loop, 0.0f); hover_.erase(h); }
    for (Live& l : live_)
        if (l.key == key) {
            if (l.inst >= 0) cues.stop(l.inst, 0.0f);
            l.inst = -1; l.active = false;
        }
}

int AbilityAudio::drainSourceTick(SoundCues& cues, const SoundCues::Emitter& drainer, float dist, bool drainerLocal, int targets) {
    const BuffSounds* b = CharacterAudio::buffSounds("TnBuffDrainSource");
    if (!b || b->heal.empty() || !drainerLocal || targets <= 0) return -1;
    return cues.play(b->heal.c_str(), drainer, dist);
}

int AbilityAudio::drainTargetTick(SoundCues& cues, const core::Vec3& pos, float dist) {
    const BuffSounds* b = CharacterAudio::buffSounds("TnBuffDrainTarget");
    if (!b || b->damage.empty()) return -1;
    return cues.play(b->damage.c_str(), pos, dist);
}

int AbilityAudio::abilitiesJammed(SoundCues& cues, const SoundCues::Emitter& pawn) {
    const std::string& q = CharacterAudio::classSound("TnPlayerController", "AbilitiesJammedSound");
    return q.empty() ? -1 : cues.play(q.c_str(), pawn, 0.0f);
}

void AbilityAudio::hoverState(SoundCues& cues, int key, int state, const SoundCues::Emitter& pawn, float dist) {
    Hover& hv = hover_[key];
    if (state == hv.state) return;
    const int prev = hv.state;
    hv.state = state;
    if (state == 1 && prev == 0) {                                     // JumpingToHover.BeginState
        const std::string& q = CharacterAudio::classSound("TnAcrobaticsManager", "_HoverLoopSound");
        if (!q.empty() && (hv.loop < 0 || !cues.playing(hv.loop))) hv.loop = cues.play(q.c_str(), pawn, dist);
        return;
    }
    if (state == 2) return;                                              // into Hovering: the loop carries on
    if (hv.loop >= 0) { cues.stop(hv.loop, 0.5f); hv.loop = -1; }   // EndState: FadeOut(0.5)
    if (prev == 2) {                                                     // Hovering ended: the cooldown (land) one-shot
        const std::string& q = CharacterAudio::classSound("TnAcrobaticsManager", "_HoverCooldownSound");
        if (!q.empty()) cues.play(q.c_str(), pawn, dist);
    }
}

int AbilityAudio::killedPawn(SoundCues& cues, bool headshot, bool robot, const std::string& chassis) {
    const char* field = headshot ? "KilledWithHeadshotSound" : robot ? "KilledRobotSound"
                      : chassis.rfind("Jet", 0) == 0 ? "KilledJetSound" : chassis.rfind("Car", 0) == 0 ? "KilledCarSound"
                      : "KilledVehicleSound";
    const std::string& q = CharacterAudio::classSound("TnPlayerController", field);
    return q.empty() ? -1 : cues.play(q.c_str(), SoundCues::Emitter{{0, 0, 0}, SoundCues::kUI, {0, 0, 0}, ""}, 0.0f);
}

int AbilityAudio::transformFailed(SoundCues& cues, const SoundCues::Emitter& pawn) {
    const std::string& q = CharacterAudio::classSound("TnPlayerController", "TransformFailedSound");
    return q.empty() ? -1 : cues.play(q.c_str(), pawn, 0.0f);
}

void AbilityAudio::rollerMine(SoundCues& cues, bool alive, float t, const core::Vec3& pos, float dist) {
    auto snd = [](const char* f) -> const std::string& { return CharacterAudio::classSound("TnRollerMineAbility", f); };
    if (alive && !rollerAlive_) {                                        // spawned
        rollerT_ = 0.0f;
        const std::string& q = snd("_IdleLoopingSound");
        if (!q.empty()) rollerLoop_ = cues.play(q.c_str(), pos, dist);
    }
    if (!alive && rollerAlive_ && rollerLoop_ >= 0) { cues.stop(rollerLoop_, 0.0f); rollerLoop_ = -1; }   // removed silently
    if (alive) {
        if (rollerLoop_ >= 0) cues.update(rollerLoop_, pos, 0.0f);       // the loop rolls with the mine
        if (rollerT_ < 3.0f && t >= 3.0f && !snd("ArmSound").empty()) cues.play(snd("ArmSound").c_str(), pos, dist);
        if (rollerT_ < 8.5f && t >= 8.5f && !snd("_BuildupSound").empty()) cues.play(snd("_BuildupSound").c_str(), pos, dist);
        rollerT_ = t;
    }
    rollerAlive_ = alive;
}

void AbilityAudio::rollerMineExploded(SoundCues& cues, const core::Vec3& pos, float dist) {
    if (rollerLoop_ >= 0) { cues.stop(rollerLoop_, 0.0f); rollerLoop_ = -1; }
    rollerAlive_ = false;
    const std::string& q = CharacterAudio::classSound("TnRollerMineAbility", "_ExplosionSound");
    if (!q.empty()) cues.play(q.c_str(), pos, dist);
}

void AbilityAudio::guidedMissile(SoundCues& cues, bool alive, const core::Vec3& pos, float dist) {
    if (alive && !missileAlive_) {
        const std::string& q = CharacterAudio::classSound("TnGuidedMissile", "FlightSound");
        if (!q.empty()) missileLoop_ = cues.play(q.c_str(), pos, dist);
    }
    if (alive && missileLoop_ >= 0) cues.update(missileLoop_, pos, 0.0f);
    if (!alive && missileLoop_ >= 0) { cues.stop(missileLoop_, 0.0f); missileLoop_ = -1; }   // gone without an explosion
    missileAlive_ = alive;
}

void AbilityAudio::guidedMissileExploded(SoundCues& cues, const core::Vec3& pos, float dist) {
    if (missileLoop_ >= 0) { cues.stop(missileLoop_, 0.25f); missileLoop_ = -1; }          // Explode: FadeOut(0.25)
    missileAlive_ = false;
    const std::string& q = CharacterAudio::classSound("TnGuidedMissile", "ExplosionSound");
    if (!q.empty()) cues.play(q.c_str(), pos, dist);
}

void AbilityAudio::barrier(SoundCues& cues, bool alive, bool fading, const core::Vec3& pos, float dist) {
    auto snd = [](const char* f) -> const std::string& { return CharacterAudio::classSound("TnBarrierSpawnable", f); };
    if (alive && !barrierAlive_) {                                       // Initialize
        barrierFading_ = false;
        if (!snd("ActiveLoopSound").empty()) barrierLoop_ = cues.play(snd("ActiveLoopSound").c_str(), pos, dist);
    }
    if (alive && fading && !barrierFading_ && !snd("DestroySound").empty()) cues.play(snd("DestroySound").c_str(), pos, dist);
    if (!alive && barrierLoop_ >= 0) { cues.stop(barrierLoop_, 0.0f); barrierLoop_ = -1; }   // the actor is gone
    barrierAlive_ = alive;
    barrierFading_ = alive && fading;
}

void AbilityAudio::sentry(SoundCues& cues, bool alive, int target, const core::Vec3& pos, float dist) {
    auto snd = [](const char* f) -> const std::string& { return CharacterAudio::classSound("TnSentryPawnAbility", f); };
    if (alive && !sentryAlive_) {                                        // deployed
        sentryTarget_ = -1;
        if (!snd("IdleSound").empty()) sentryLoop_ = cues.play(snd("IdleSound").c_str(), pos, dist);
    }
    if (alive && target >= 0 && target != sentryTarget_ && !snd("ActivateSound").empty())   // EnemyAcquired
        cues.play(snd("ActivateSound").c_str(), pos, dist);
    if (!alive && sentryAlive_) {                                        // Destroyed
        if (sentryLoop_ >= 0) { cues.stop(sentryLoop_, 0.25f); sentryLoop_ = -1; }
        if (!snd("DestroyedSound").empty()) cues.play(snd("DestroyedSound").c_str(), pos, dist);
    }
    sentryAlive_ = alive;
    sentryTarget_ = alive ? target : -1;
}

void AbilityAudio::sentryShot(SoundCues& cues, const core::Vec3& muzzle, float muzzleDist, bool worldHit, const core::Vec3& hit,
                              float hitDist) {
    const std::string& fire = CharacterAudio::classSound("TnWeaponDefaultSentryAbility", "WP_Fire");
    if (!fire.empty()) cues.play(fire.c_str(), muzzle, muzzleDist);
    const std::string& impact = CharacterAudio::classSound("TnWeaponDefaultSentryAbility", "DefaultImpactSound");
    if (worldHit && !impact.empty()) cues.play(impact.c_str(), hit, hitDist);
}

void AbilityAudio::overshield(SoundCues& cues, float os, const SoundCues::Emitter& pawn) {
    if (overshield_ > 0.0f && os <= 0.0f) {
        const std::string& q = CharacterAudio::classSound("TnPlayerPawn", "OvershieldOffSound");
        if (!q.empty()) cues.play(q.c_str(), pawn, 0.0f);
    }
    overshield_ = os;
}

int AbilityAudio::dodgeHitWall(SoundCues& cues, const SoundCues::Emitter& pawn) {
    const std::string& q = CharacterAudio::classSound("TnPawn", "HitWallSound");
    return q.empty() ? -1 : cues.play(q.c_str(), pawn, 0.0f);
}

int AbilityAudio::pawnDeath(SoundCues& cues, const std::string& chassis, bool vehicleForm, const std::string& damageType,
                            const core::Vec3& pos, float dist) {
    std::string q;
    if (vehicleForm) {
        const CharacterAudioProfile* p = CharacterAudio::find(chassis);
        if (p) q = p->vehicleDeath;
    } else if (CharacterAudio::isMeleeDamageType(damageType)) {
        q = CharacterAudio::classSound("TnDeathTypeMelee", "DeathSound");
    }
    if (q.empty()) return -1;
    if (!cues.hasCue(q.c_str()) && CharacterAudio::find(chassis)) CharacterAudio::loadCues(cues, *CharacterAudio::find(chassis));
    return cues.play(q.c_str(), pos, dist);
}

int AbilityAudio::pawnHitEffect(SoundCues& cues, const std::string& dt, const std::string& chassis, int victimKey,
                                const core::Vec3& pos, float dist, float clock) {
    const WeaponHitEffect* he = CharacterAudio::damageHitEffect(dt);
    if (!he || !he->causesBlood || he->hitEvent.empty()) return -1;
    const auto key = std::make_pair(victimKey, he->index);
    auto it = lastHit_.find(key);
    if (it != lastHit_.end() && clock < it->second + he->retrigger) return -1;      // RetriggerTime
    lastHit_[key] = clock;
    const CharacterAudioProfile* vp = CharacterAudio::find(chassis);
    const CharacterAudioProfile& p = vp ? *vp : CharacterAudio::defaultProfile();
    const std::string& q = p.voiceCue(he->hitEvent);
    if (q.empty()) return -1;
    if (!cues.hasCue(q.c_str())) CharacterAudio::loadCues(cues, p);                  // the victim body's set, level-owned
    return cues.play(q.c_str(), pos, dist);
}

void AbilityAudio::kamikazeMine(SoundCues& cues, int key, const core::Vec3& pos, bool targetFound, float dist) {
    auto snd = [](const char* f) -> const std::string& { return CharacterAudio::classSound("TnProjectileKamikazeMine", f); };
    auto it = mines_.find(key);
    if (it == mines_.end()) {                                            // thrown / spawned: the idle loop
        Mine m;
        if (!snd("FlightSound").empty()) m.loop = cues.play(snd("FlightSound").c_str(), pos, dist);
        it = mines_.emplace(key, m).first;
    }
    Mine& m = it->second;
    if (targetFound && !m.tracking) {                                    // FoundTarget
        m.tracking = true;
        if (!snd("TargetFoundSound").empty()) cues.play(snd("TargetFoundSound").c_str(), pos, dist);
        if (m.loop >= 0) cues.stop(m.loop, 0.25f);
        m.loop = snd("SecondaryFlightSound").empty() ? -1 : cues.play(snd("SecondaryFlightSound").c_str(), pos, dist);
        if (m.loop >= 0) cues.fadeIn(m.loop, 0.25f);
    }
    if (m.loop >= 0) cues.update(m.loop, pos, 0.0f);
}

void AbilityAudio::kamikazeMineExploded(SoundCues& cues, int key, const core::Vec3& pos, float dist) {
    auto it = mines_.find(key);
    if (it != mines_.end()) { if (it->second.loop >= 0) cues.stop(it->second.loop, 0.25f); mines_.erase(it); }
    const std::string& q = CharacterAudio::classSound("TnProjectileKamikazeMine", "ExplosionSound");
    if (!q.empty()) cues.play(q.c_str(), pos, dist);
}

void AbilityAudio::kamikazeMineRemoved(SoundCues& cues, int key) {
    auto it = mines_.find(key);
    if (it == mines_.end()) return;
    if (it->second.loop >= 0) cues.stop(it->second.loop, 0.0f);         // destroyed after the fade-out: cut
    mines_.erase(it);
}

void AbilityAudio::stopAll(SoundCues& cues) {
    for (auto& kv : mines_) if (kv.second.loop >= 0) cues.stop(kv.second.loop, 0.0f);
    mines_.clear();
    lastHit_.clear();
    overshield_ = 0.0f;
    for (int* id : {&barrierLoop_, &sentryLoop_}) { if (*id >= 0) cues.stop(*id, 0.0f); *id = -1; }
    barrierAlive_ = barrierFading_ = sentryAlive_ = false; sentryTarget_ = -1;
    if (missileLoop_ >= 0) cues.stop(missileLoop_, 0.0f);
    missileLoop_ = -1; missileAlive_ = false;
    if (rollerLoop_ >= 0) cues.stop(rollerLoop_, 0.0f);
    rollerLoop_ = -1; rollerAlive_ = false;
    for (Live& l : live_) if (l.inst >= 0) cues.stop(l.inst, 0.0f);
    live_.clear();
    for (auto& h : hover_) if (h.second.loop >= 0) cues.stop(h.second.loop, 0.0f);
    hover_.clear();
}

int AbilityAudio::liveLoops(const SoundCues& cues) const {
    int n = 0;
    for (const Live& l : live_) if (l.inst >= 0 && cues.playing(l.inst)) ++n;
    return n;
}

} // namespace game
