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
//       4 -> Run, 1 Scuff, 3 Land, 10 HardLand; no speed-based choice. The surface's PhysicalMaterial
//       HmPhysicalMaterialProperty.FootstepSounds override the defaults; none are recovered for Streets
//       (AssetTools request), so the HM_Engine.Default__HmFootstepComponent defaults apply [HIGH].
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

class RobotFoley {
public:
    // Advance one simulation step and append the cue names (SoundCues table names) to play, attached
    // to the pawn, to `out`. Call after the pawn's animation update.
    void tick(const Character& pc, float dt, std::vector<const char*>& out);

    // Diagnostics: last landing classification.
    const char* lastLandClip() const { return lastLand_; }
    float lastFallHeightUU() const { return lastFallUU_; }

private:
    void clipNotifies(const std::string& clip, float a, float b, bool includeStart, float weight,
                      std::vector<const char*>& out) const;
    void clipNotifiesTimed(const std::string& clip, float dur, bool loop, float t0, float t1, bool includeStart,
                           std::vector<const char*>& out) const;

    bool active_ = false;            // robot form, not transforming, last step
    bool grounded_ = true;
    float apexY_ = 0.0f;             // _FallBaseHeight
    float prevVy_ = 0.0f;
    std::string clip_;               // base clip last step
    float norm_ = 0.0f;              // locomotion: sync-group phase last step
    float time_ = 0.0f;              // other clips: clip time last step (s)
    bool loco_ = false;
    std::vector<std::pair<float, const char*>> delayed_;   // (seconds left, cue) for timed landing notifies
    const char* lastLand_ = "-";
    float lastFallUU_ = 0.0f;
};

} // namespace game
