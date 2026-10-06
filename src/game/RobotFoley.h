// Clean-room reconstruction — Optimus robot movement sound: footsteps, scuffs, jump, landing, idle and
// pivot foley, driven the way the original drives them (AnimNotifies on the clips actually playing,
// resolved through the character's SoundEventSet).
//
// [CONF] sources (MP_IAC_Streets_BASE_m, TR_HeavyMedium_ANM_p.Shared_ROBO_ANIM / Optimus_ROBO_ANIM):
//   * AnimNotify_Footstep (Default MinWeight 0.25) on Nav_StrafeWalk_* (kFootstep), Nav_StrafeJog_*
//     (kFootstepRun), Nav_IdlePivot90_* (kScuff + kFootstep), Nav_Land (kLand), Nav_Land_02/_03 (kHardLand).
//   * HmAnimNotify_SoundEvent / HmAnimNotify_Sound (Default MinWeight 0.25, optional SocketName,
//     bStopWhenActorDestroyed): Nav_TakeOff_01 FS_DEFAULT_JUMP, Nav_Land_03 FS_DEFAULT_LAND_HIGH_FALL +
//     FOLEY_FS_GROAN_SERVO_01 @0.432, pivots FOLEY_FS_GROAN_SERVO_01, Optimus NAV_Idle BL_FOLY_IDLES.OPTIMUS_IDLE @0.
//   * HM_Engine.Default__HmFootstepComponent: footstep type -> FS_DEFAULT_{WALK,JOG,RUN,SCUFF,LAND,HARD_LAND}.
//   * TR_Optimus_ROBODEF_p.Optimus_ROBODEF.SoundEventSet = SoundEvents.CHR_OPTIMUS: FS_DEFAULT_* ->
//     BL_FS_LRG_BOT.FS_{WALK,RUN,SCUFF,JUMP,JUMP_CHARGED,LAND,LAND_HARD,LAND_HIGH_FALL}_* (JOG unmapped).
//   * TR_Acrobatics_p.SharedAcrobatics.LandingAnims (MinHeight / MinSpeed UU): {1200,1200} Nav_Land_03,
//     {1000,1200} Nav_Land, {4500,0} Nav_Land_03, {500,0} Nav_Land_02, {250,0} Nav_Land.
// [MED] AnimNodeSynch default bFireSlaveNotifies=false: only the Strafers master clip fires notifies.
// [CONF HmFootstepComponent script] FootstepType 0 (no Type: walk clips) -> FootstepWalk / DefaultWalkSound,
//       4 -> Run, 1 Scuff, 3 Land, 10 HardLand; no speed-based choice.
// [CONF authored, AssetTools 7a69756 streets_surface_audio.json] Streets has no surface-specific footstep
//       or landing audio: every Streets PhysicalMaterial (Metal - also the engine DefaultPhysMaterialName and
//       the two BlockingVolume overrides - Rubber, Water, ForceField, and surfaces with no property) resolves
//       every slot to the same FS_DEFAULT_* events (TransGame.Default__TnPawn.FootstepComp0 defaults, no
//       PhysicalMaterialOverride, no SeqAct_SetFootstepMaterialOverride). These cues ARE the authored sound,
//       not a fallback, so no surface trace is needed. Which surface the native trace samples is moot.
//       [CONF authored, game-wide, Systems M06 query of authored.db] No cooked package authors surface audio
//       either: no PhysicalMaterialPropertyBase subclass instance, no SeqAct_SetFootstepMaterialOverride instance,
//       and every HmFootstepComponent (pawn classes, level-placed ones) keeps the FS_DEFAULT_* class defaults - so
//       the same events hold on any later map; only the pawn's SoundEventSet picks the sound.
// [CONF TnAcrobaticsManager script] LandingAnims: first match in array order on FallDistance (height
//       where the last fall began - ledge, or the jump apex - minus landing height) and ForwardSpeed
//       |Velocity . facing|; below 250 UU (or landing higher) no landing anim, so no landing sound.
// The sound always comes from the clip the ORIGINAL would play; Gameplay currently plays Nav_Land for
// every landing (handoff: Nav_Land_02 / _03 per the same table).
#pragma once
#include <string>
#include <vector>

namespace assets { struct SkinnedModel; }

namespace game {

class Character;
struct CharacterAudioProfile;

class RobotFoley {
public:
    // Advance one simulation step and append the cue names (SoundCues table names) to play, attached
    // to the pawn, to `out`. Call after the pawn's animation update.
    void tick(const Character& pc, float dt, std::vector<const char*>& out);
    // TnAbilityDodge: the dodge plays Nav_Boost_F/B/L/R (stance variants), whose only notify is SoundEvents_Footsteps.
    // FS_DEFAULT_JUMP_CHARGED @0 resolved through the character's sound-event set (TnPawn.FindSoundCue) [CONF RE pass 5
    // s12 addendum + data: every variant authors the same notify]. Gameplay does not play that clip, so the dodge's start
    // fires the clip's notifies here.
    void dodgeStarted(std::vector<const char*>& out) { clipOneShot("Nav_Boost_F", out); }
    // The one-shot action layer (TnPawn PlayCustomAnim: melee swings, Skill_* ability animations, whirlwind, grenade
    // throw): its authored AnimNotify_Sound / SoundEvent notifies fire as the clip plays [CONF data: the notifies are
    // on those AnimSequences]. Every tick with Gameplay's action clip ("" none) and its time; a replay restarts it.
    void actionLayer(const std::string& clip, float t, std::vector<const char*>& out);
    // An ability animation Gameplay does not play (Skill_Shockwave / _Warcry / _SpawnSentry ...): its notifies on trigger.
    void clipNotifiesOnce(const std::string& clip, std::vector<const char*>& out) { clipOneShot(clip.c_str(), out); }

    // The character whose clips / sound set drive the foley (default: CharacterAudio::defaultProfile()).
    void setProfile(const CharacterAudioProfile* p) { profile_ = p; }
    // Diagnostics: last landing classification.
    const char* lastLandClip() const { return lastLand_; }
    float lastFallHeightUU() const { return lastFallUU_; }

private:
    void clipNotifies(const std::string& clip, float a, float b, bool includeStart, float weight,
                      std::vector<const char*>& out) const;
    const CharacterAudioProfile& profile() const;
    void clipOneShot(const char* clip, std::vector<const char*>& out);
    void clipNotifiesTimed(const std::string& clip, float dur, bool loop, float t0, float t1, bool includeStart,
                           std::vector<const char*>& out) const;

    const CharacterAudioProfile* profile_ = nullptr;
    bool active_ = false;            // robot form, not transforming, last step
    bool grounded_ = true;
    float apexY_ = 0.0f;             // _FallBaseHeight
    float prevVy_ = 0.0f;
    std::string clip_;               // base clip last step
    std::string actionClip_; float actionT_ = 0.0f;   // action layer last step
    float norm_ = 0.0f;              // locomotion: sync-group phase last step
    float time_ = 0.0f;              // other clips: clip time last step (s)
    bool loco_ = false;
    std::vector<std::pair<float, const char*>> delayed_;   // (seconds left, cue) for timed landing notifies
    const char* lastLand_ = "-";
    float lastFallUU_ = 0.0f;
};

} // namespace game
