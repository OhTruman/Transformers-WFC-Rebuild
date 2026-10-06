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
    if (key == 0 && hoverLoop_ >= 0) { cues.stop(hoverLoop_, 0.0f); hoverLoop_ = -1; }   // the local pawn's hover: no land
    if (key == 0) hoverState_ = 0;
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

void AbilityAudio::hoverState(SoundCues& cues, int state, const SoundCues::Emitter& pawn, float dist) {
    if (state == hoverState_) return;
    const int prev = hoverState_;
    hoverState_ = state;
    if (state == 1 && prev == 0) {                                     // JumpingToHover.BeginState
        const std::string& q = CharacterAudio::classSound("TnAcrobaticsManager", "_HoverLoopSound");
        if (!q.empty() && (hoverLoop_ < 0 || !cues.playing(hoverLoop_))) hoverLoop_ = cues.play(q.c_str(), pawn, dist);
        return;
    }
    if (state == 2) return;                                              // into Hovering: the loop carries on
    if (hoverLoop_ >= 0) { cues.stop(hoverLoop_, 0.5f); hoverLoop_ = -1; }   // EndState: FadeOut(0.5)
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

void AbilityAudio::stopAll(SoundCues& cues) {
    for (int* id : {&barrierLoop_, &sentryLoop_}) { if (*id >= 0) cues.stop(*id, 0.0f); *id = -1; }
    barrierAlive_ = barrierFading_ = sentryAlive_ = false; sentryTarget_ = -1;
    if (missileLoop_ >= 0) cues.stop(missileLoop_, 0.0f);
    missileLoop_ = -1; missileAlive_ = false;
    if (rollerLoop_ >= 0) cues.stop(rollerLoop_, 0.0f);
    rollerLoop_ = -1; rollerAlive_ = false;
    for (Live& l : live_) if (l.inst >= 0) cues.stop(l.inst, 0.0f);
    live_.clear();
    if (hoverLoop_ >= 0) cues.stop(hoverLoop_, 0.0f);
    hoverLoop_ = -1; hoverState_ = 0;
}

int AbilityAudio::liveLoops(const SoundCues& cues) const {
    int n = 0;
    for (const Live& l : live_) if (l.inst >= 0 && cues.playing(l.inst)) ++n;
    return n;
}

} // namespace game
