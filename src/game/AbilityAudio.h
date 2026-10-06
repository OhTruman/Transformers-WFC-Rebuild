// Clean-room reconstruction — ability and buff sounds (TnAbility.OnTriggerSound, TnBuff Apply / Unapply sounds,
// TnBuffDrainSource / TnBuffDrainTarget tick sounds). Gameplay owns abilities and buffs (state, timers, targets);
// this class only turns their transitions into the authored sounds [RE pass 5 §12, CONF script + class defaults]:
//   * OnTriggerSound: TnAbility.ServerTriggerAbility, after the trigger succeeded -> OwnerPawn.PlaySound (replicated to
//     everyone, the owner included; positional at the pawn). LocalTriggerAbility plays nothing.
//   * Buff Apply: if CanPlaySounds() -> OwnerPawn.CreateAudioComponent(ApplySound): attached to the buffed pawn, plays
//     (loops for _LP cues) until the buff ends; re-applying while it plays does not restart it.
//     Unapply: the loop stops, then PlaySound(UnapplySound) at the pawn (local). Owner death: the loop stops, no Unapply.
//     CanPlaySounds = !OnlyPlaySoundOnLocalPlayer || the buffed pawn is the local player's (TnBuff default True;
//     only TnBuffCloak sets False). TnBuffCloak picks Autobot / Decepticon cues by the BUFFED pawn's team.
//   * TnBuffDrainSource.HealSound: every buff tick on the drainer's machine while it has >= 1 target;
//     TnBuffDrainTarget.DamageSound: every tick, everywhere, at the victim [CONF; per-frame tick rate HIGH].
#pragma once
#include <map>
#include <string>
#include <vector>
#include "game/SoundCues.h"

namespace game {

class AbilityAudio {
public:
    // A successful trigger of ability `id` ("Barrier" or "TnAbilityBarrier"): its OnTriggerSound at the owner pawn.
    // Returns the instance (-1: the class authors none).
    int abilityTriggered(SoundCues& cues, const std::string& id, const SoundCues::Emitter& pawn, float listenerDist);

    // The buffed pawn: `key` identifies it across ticks (one live sound per pawn x buff), `local` = the local player's
    // pawn, `team` 0 = Autobot / 1 = Decepticon (no team: the character faction, Autobot 0).
    struct Owner { int key = 0; bool local = true; int team = 0; SoundCues::Emitter at; float listenerDist = 0.0f; };
    // The buff's state on that pawn this tick; transitions play the sounds. Returns true if a sound started.
    bool setBuff(SoundCues& cues, const std::string& buffClass, const Owner& o, bool active);
    // The pawn died: every buff loop on it stops, no Unapply sound (the buffs are gone - setBuff(false) after this is silent).
    void pawnDied(SoundCues& cues, int key);

    // TnBuffDrainSource.ClientTickBuff (the drainer's own machine, while it has targets) / TnBuffDrainTarget.ClientTickBuff
    // (at each victim, every machine): one play per buff tick.
    int drainSourceTick(SoundCues& cues, const SoundCues::Emitter& drainer, float listenerDist, bool drainerLocal, int targets);
    int drainTargetTick(SoundCues& cues, const core::Vec3& victimPos, float listenerDist);

    // A refused ability press while abilities are blocked (jammed / disabled): TnPlayerController.AbilitiesJammedSound,
    // local, at the pawn. Cooldown / resource refusals are silent [RE pass 5 s12 addendum].
    int abilitiesJammed(SoundCues& cues, const SoundCues::Emitter& pawn);
    // TnAcrobaticsManager hover state each tick (0 none, 1 JumpingToHover, 2 Hovering) [RE pass 5 s12 addendum]: entering
    // JumpingToHover starts _HoverLoopSound attached to the pawn; it carries into Hovering; leaving fades it over 0.5 s,
    // and leaving Hovering also plays _HoverCooldownSound (HOVER_JUMP_LAND). Everyone hears it (the state replicates).
    void hoverState(SoundCues& cues, int state, const SoundCues::Emitter& pawn, float listenerDist);

    // Kill confirm for the killer (TnPlayerController.TellClientToPlayKilledPawnSound -> ClientPlaySound: the killer only,
    // effectively 2D) [RE pass 5 s13]: headshot > victim in robot form > victim character SoldierJet (Jet*) > SoldierCar
    // (Car*) > any other vehicle (truck, tank) - by the victim's CHARACTER class, not its vehicle.
    int killedPawn(SoundCues& cues, bool headshot, bool victimRobotForm, const std::string& victimChassisId);
    // PressTransform refused (TnBuffTransformDisruptor, already transforming, transforming disabled, no room):
    // TransformFailedSound, local, at the pawn [RE pass 5 s13].
    int transformFailed(SoundCues& cues, const SoundCues::Emitter& pawn);

    // TnRollerMine (the local owner's, TnRollerMineAbility) [RE pass 5 s12 addendum + s13, CONF]: spawn -> _IdleLoopingSound
    // (loops with the actor); ArmTime 3.0 s -> ArmSound; fuse <= 1.5 s left (t 8.5 of the 10 s fuse) -> _BuildupSound once;
    // destroyed -> loop stops + _ExplosionSound at the mine. Removed otherwise (owner death: FadingOut, kill-Z): loop stops,
    // nothing else. Every tick with Gameplay's state; `exploded` on the destruction tick.
    void rollerMine(SoundCues& cues, bool alive, float t, const core::Vec3& pos, float listenerDist);
    void rollerMineExploded(SoundCues& cues, const core::Vec3& pos, float listenerDist);

    // TnGuidedMissile (ability / killstreak): its projectile mesh's FlightSound from launch, following the missile; on
    // detonation the flight FadeOut(0.25) and ExplosionSound at the missile (HmProjectile, as the weapon projectiles).
    void guidedMissile(SoundCues& cues, bool alive, const core::Vec3& pos, float listenerDist);
    void guidedMissileExploded(SoundCues& cues, const core::Vec3& pos, float listenerDist);

    // TnBarrierSpawnable [RE pass 5 s12 addendum 3, CONF]: ActiveLoopSound from spawn, at the barrier, until the actor goes;
    // DestroySound when its health reaches 0 (damage or the DegenRate lifetime) = Gameplay's fade start; a silent removal
    // otherwise (re-cast ForceFadeout, owner death). Every tick: alive, fading (health 0, fading out), position.
    void barrier(SoundCues& cues, bool alive, bool fading, const core::Vec3& pos, float listenerDist);
    // TnSentryPawnAbility [RE s12 addenda 3 / 4, CONF]: IdleSound loop from deploy, at the sentry; ActivateSound on every
    // EnemyAcquired (the target changing to an enemy); each shot its gun's WP_Fire (TnWeaponDefaultSentryAbility), a world
    // hit its DefaultImpactSound; destroyed (damage or the 30 s lifetime): the loop fades 0.25 s + Sentry_DSYS's
    // SENTRY_EXPL one-shot; the owner's death too (TnSentryPawn.Kill -> full-health damage trigger -> Destroyed) [CONF RE
    // pass 5 s12 addendum 5]. Every tick: alive, target (-1 none), position.
    void sentry(SoundCues& cues, bool alive, int target, const core::Vec3& pos, float listenerDist);
    void sentryShot(SoundCues& cues, const core::Vec3& muzzle, float muzzleDist, bool worldHit, const core::Vec3& hit, float hitDist);

    // TnPlayerPawn.Tick [RE pass 5 s12 addendum 11, CONF]: the overshield health reaching 0 while the overshield is up
    // (depleted or expired) -> OvershieldOffSound at the pawn. Every tick with the local pawn's overshield health.
    void overshield(SoundCues& cues, float overshieldHealth, const SoundCues::Emitter& pawn);
    // InRobotForm.HitWall during a dodge -> TnPawn.HitWallSound at the pawn (no speed / angle condition).
    int dodgeHitWall(SoundCues& cues, const SoundCues::Emitter& pawn);

    // A pawn died (RE pass 5 s12 addendum 12, CONF): in vehicle form TnVehicleForm.OnPlayDeath plays the chassis'
    // _Blueprint.DeathSound at the wreck on every machine (the killer hears it too); in robot form only a melee death
    // (TnDeathTypeMelee, from a TnDamageTypeMelee kill [selection HIGH]) plays the disintegrate sound. Returns the instance.
    int pawnDeath(SoundCues& cues, const std::string& chassisId, bool vehicleForm, const std::string& damageType,
                  const core::Vec3& pos, float listenerDist);

    // A non-weapon hit on a pawn (melee / whirlwind / shoulder slam / ram): the damage type's TnHitEffectPlayer entry
    // plays its HitSound - an event of the VICTIM's SoundEventSet - if bCausesBlood, at most every RetriggerTime per
    // victim per entry [CONF script + data, as the weapon hit effects]. `victimKey` identifies the victim.
    int pawnHitEffect(SoundCues& cues, const std::string& damageType, const std::string& victimChassis, int victimKey,
                      const core::Vec3& pos, float listenerDist, float clock);

    // TnProjectileKamikazeMine [RE pass 5 s12 addendum 16, CONF], per mine `key`, every tick: FlightSound (idle loop) from
    // the throw; the first target found -> TargetFoundSound + the idle loop fades out 0.25 s while SecondaryFlightSound
    // (tracking loop) fades in 0.25 s. Exploded (target reached, wall, touched, shot): the loop fades 0.25 s +
    // ExplosionSound. Removed otherwise (LifeSpan / owner death: the fade-out fizzle): the loop stops, nothing else.
    void kamikazeMine(SoundCues& cues, int key, const core::Vec3& pos, bool targetFound, float listenerDist);
    void kamikazeMineExploded(SoundCues& cues, int key, const core::Vec3& pos, float listenerDist);
    void kamikazeMineRemoved(SoundCues& cues, int key);
    int kamikazeMines() const { return (int)mines_.size(); }

    void stopAll(SoundCues& cues);                   // level unload / match restart: loops stop, nothing else plays
    int liveLoops(const SoundCues& cues) const;      // buff sounds still playing (diagnostics)
    static std::string abilityClass(const std::string& id);   // "Barrier" -> "TnAbilityBarrier"

private:
    struct Live { int key; std::string buff; bool active = false; int inst = -1; };
    Live& slot(int key, const std::string& buff);
    std::vector<Live> live_;
    int hoverState_ = 0, hoverLoop_ = -1;
    bool rollerAlive_ = false; float rollerT_ = 0.0f; int rollerLoop_ = -1;
    bool missileAlive_ = false; int missileLoop_ = -1;
    bool barrierAlive_ = false, barrierFading_ = false; int barrierLoop_ = -1;
    bool sentryAlive_ = false; int sentryTarget_ = -1, sentryLoop_ = -1;
    float overshield_ = 0.0f;
    std::map<std::pair<int, int>, float> lastHit_;     // (victim, hit-effect entry) -> clock of the last HitSound
    struct Mine { int loop = -1; bool tracking = false; };
    std::map<int, Mine> mines_;
};

} // namespace game
