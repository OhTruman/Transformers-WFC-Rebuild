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

    void stopAll(SoundCues& cues);                   // level unload / match restart: loops stop, nothing else plays
    int liveLoops(const SoundCues& cues) const;      // buff sounds still playing (diagnostics)
    static std::string abilityClass(const std::string& id);   // "Barrier" -> "TnAbilityBarrier"

private:
    struct Live { int key; std::string buff; bool active = false; int inst = -1; };
    Live& slot(int key, const std::string& buff);
    std::vector<Live> live_;
    int hoverState_ = 0, hoverLoop_ = -1;
};

} // namespace game
