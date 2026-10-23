// Clean-room reconstruction — non-verbal dialog (Systems, 2026-10-23): HmAnimNotify_Dialog -> HmDialogEventManager -> the pawn's
// dialog [RE 2026-10-23 + CAMPAIGN_DIALOG_SCHEMA section 2, CONFIRMED from script]:
//   * the notify (MinWeight gate as the sound notifies) calls WorldInfo._DialogEventManager.Play(pawn, SoundEvent);
//   * Play: the event's DialogGroup (else the manager's DefaultDialogGroup); CanPlayDialog walks the group and its ParentDialogGroup
//     chain: a group with RetriggerTimer > 0 refuses; a group at its InstanceLimit (0 = no limit) refuses unless the new event's
//     DialogPriority is higher than its lowest-priority playing instance, which is then stopped (preemption);
//   * PlayInternal adds the instance to every group of the chain (by priority); a group that reaches its limit (or has limit 0) arms
//     RetriggerTimer = RandRange(ReTriggerDelayMin, Max); then the pawn plays the event's cue: TnPawn.FindSoundCue - the level's
//     character set (TnWorldInfo CharacterSoundEventSets, by the chassis's ROBODEF), then the robot set, then the vehicle set;
//     no cue = nothing plays;
//   * Tick counts the timers down; a group starts with RetriggerTimer = 0 when RandomInt(100) < InitialChanceToPlay, else a
//     retrigger delay (GetInitialDelay).
// One manager per loaded level (MP: the map's; campaign: the chapter's base level). Presentation only: its randomness and timers are
// audio-side (not the sim's). Data: CharacterAudio.inc "dialog" (tools/systems/gen_character_audio.py).
#pragma once
#include <map>
#include <random>
#include <string>
#include <vector>
#include "game/SoundCues.h"

namespace game {

struct CharacterAudioProfile;

class DialogEvents {
public:
    // A level was loaded (its audio): the manager's groups restart and the level's character sets apply ("" = none). Loads the
    // level sets' cues into `cues` (level-owned). `level` = the map / chapter name (MP_IAC_Streets, D5_OME).
    void setLevel(const std::string& level, SoundCues& cues);
    // An HmAnimNotify_Dialog of `who` fired (event = the HmSoundEvent's short name). Returns the instance (-1: refused / no cue).
    int play(const CharacterAudioProfile& who, int owner, const std::string& event, SoundCues& cues, const SoundCues::Emitter& at, float distM);
    void tick(float dt, SoundCues& cues);
    // The cue `who` plays for `event` here (FindSoundCue): "" = none.
    const std::string& findCue(const CharacterAudioProfile& who, const std::string& event) const;

private:
    struct Inst { int instance; float priority; int owner; };
    struct Group { float timer = 0.0f; bool started = false; std::vector<Inst> playing; };
    std::string level_;
    std::map<std::string, Group> groups_;
    std::mt19937 rng_{0x5eed1234u};
    Group& group(const std::string& name);
    float retriggerDelay(const std::string& name);
};

} // namespace game
