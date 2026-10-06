// Clean-room reconstruction — match / announcer audio (Systems M07): the sound side of the multiplayer messages, ported
// from the decompiled script (RE-Workspace work/script/decomp, read-only) with data from the level manifests
// (tools/systems/gen_level_audio.py: authored.db class defaults + the map's TnWorldInfo announcer set).
// Gameplay owns the rules and decides WHEN a message is broadcast; it reports the message here; Systems plays it.
//
//   TnAnnouncer [CONF script]: an HmDialogComponent on the WorldInfo, DialogCharacter = Team0DialogCharacter
//     (DialogCharacters.OPRIME) or, after SetTeam(1), Team1DialogCharacter (DialogCharacters.MGTRON) [CONF defaults].
//     PlayEvent(event) -> SoundEventSet.FindSoundCue(event) -> Play: PlayInternal plays at once when the component is
//     idle; while it plays, the same cue as the current one clears the queue, another cue is queued if it is of
//     higher-or-equal root Priority than the queued one (IsHigherPriority: refuses only a strictly lower one);
//     Tick plays the queued cue once the component is idle. The map's set (TnWorldInfo.AnnouncerSoundEventSet =
//     SoundEvents_Dialog.Announcer.CHR_ANNOUNCER_DIALOG on Streets and Gorge [CONF]): 140 events -> BL_DX_SWITCHBOARD
//     cues whose wave events carry DialogCharacter OPRIME / MGTRON (only the component's character plays [HIGH]).
//   TnGameTypeMessage.ClientReceive [CONF script] (TnGameRules: HandleStartGame -> 0, HandleGameNearlyComplete -> 1,
//     HandleEndGame(Winner) -> 2; TeamGameMessageClass / FFAGameMessageClass per rules):
//       0: HUD text GameTypeMessage (Gameplay / HUD), announcer GameTypeDialog then GameDescriptionDialog, music
//          GameTypeMusic (FadeIn 0, FadeOut 0, Priority 0);
//       1: music GameNearlyCompleteMusic (FadeIn 0, FadeOut 0, Priority 0);
//       2: music GetEndGameMusic (Priority 1, fades 0): no winner -> TieMusic; team game: winner team 0 ->
//          AutobotsWinMusic else DecepticonsWinMusic; DM (TnGameTypeMessageDM): the local player is the winner ->
//          AutobotsWinMusic else DecepticonsWinMusic.
//   TnGameProgressAnnouncementMessage.ClientReceive [CONF script]: announcer PlayEvent(Sounds[switch]); switches 0..7 =
//     30 s / 1 min / 2 min left, 25 / 50 points remaining, 1 / 3 / 5 kills remaining (TnGameRules_ReportGameProgress*:
//     time 30->0, 60->1 (NotifyGame), 120->2; points 25->3 (NotifyGame), 50->4; kills 1->5, 3->6, 5->7 (NotifyGame)).
//   TnVersusGameOverMessage [PARTIAL: TransContent script not decompiled]: AutobotWinSound / DecepticonWinSound
//     (MP_GameAutobotWin / MP_GameDecepticonWin announcer events) - played through the announcer by winning team.
// Not authored, so not played: kill feed (TnDeathMessage), kill-streak text (TnKillStreakMessage /
// TnKillStreakEndedMessage carry no sound), score text. HUD GFx sounds (HUD_POSITIVE_HIT_INDICATOR,
// HUD_OBJECTIVE_ADDED, ...) are Sound.PlaySound calls of the HUD movie -> World::playUiSound.
#pragma once
#include <map>
#include <string>
#include "assets/Json.h"

namespace game {

class SoundCues;
class MusicPlayer;

class MatchAudio {
public:
    MatchAudio(SoundCues& cues, MusicPlayer& music) : cues_(cues), music_(music) {}   // inline: World builds without this .cpp in tools
    // The loaded level's announcer set (event -> cue) from its manifest; empty = no announcer (UI levels).
    void setAnnouncerEvents(const std::map<std::string, std::string>& events) { events_ = events; reset(); }
    void reset();                                   // level unload: nothing current / queued, team 0

    void setLocalTeam(int team);                    // TnAnnouncer.SetTeam (0 Autobots, 1 Decepticons)
    // TnAnnouncer.PlayEvent: an announcer event (e.g. "SoundEvents_Dialog.Announcer.MP_FlagCapturedDialog").
    // Returns false if the level's set has no cue for it.
    bool announcerEvent(const std::string& event);
    // TnKillstreakActivated* (the streak's AnnouncementMessageType) [CONF RE pass 5 s12 addendum 11]: per receiving client,
    // the activator hears SelfAnnouncementSound, its team (OnSameTeam) FriendlyAnnouncementSound, others EnemyAnnouncementSound;
    // a role with none falls back to FactionAnnouncementSound[activator team] when authored, else silent. Through the
    // announcer queue (PlayEvent). `id` = the TnDataProvider_Killstreak UniqueId ("OrbitalReconStreak").
    enum class StreakRole { Self, Friendly, Enemy };
    bool killstreakActivated(const std::string& id, StreakRole role, int activatorTeam);
    // TnGameTypeMessage(<class>).ClientReceive(switch): class e.g. "TnGameTypeMessageTDM"; winnerTeam -1 = none (tie);
    // localPlayerWon only for the DM message (FFA end music).
    bool gameTypeMessage(const std::string& messageClass, int sw, int winnerTeam = -1, bool localPlayerWon = false);
    bool progressAnnouncement(int sw);               // TnGameProgressAnnouncementMessage switch 0..7
    // TnVersusGameOverMessage.ClientReceive [CONF RE MP sweep S5]: a winner (normal or forfeit) -> MP_GameAutobotWin /
    // MP_GameDecepticonWin; a tie -> none. FFA (TnFreeForAllGameOverMessage) has no win line.
    bool versusGameOver(int winnerTeam);
    void tick(float dt);

    // ---- countdown ticks (Gameplay reports every countdown value) [CONF script + class defaults] ----
    // TnGameReplicationInfo.OnCountdownChange: while counting down, 0 <= CurrentCountdown <= LowCountdownTickThreshold
    // (10) -> PlaySound(LowCountdownTickSound = BL_HUD_INTERFACE.CTF_ROUND_TIMER_01): the pre-match 10..0 ticks.
    bool countdownChanged(int currentCountdown, bool countingDown);
    // A Gameplay objective broadcast (BroadcastLocalizedMessage class + switch) -> the class's ClientReceive audio:
    //   "TnFlagMessage(taken|dropped|captured|returned)" -> flagMessage(1 / 2 / 3 / 0);
    //   "TnBombMessage(taken|dropped|planted|defused|detonated)" (value = team where it matters) -> bombMessage(1/2/5/4/3);
    //   "TnDominationMessage" (value = point * 10 + type) -> dominationMessage. Other classes: false (no audio here; the
    //   hill lines come from the zone's DefendingTeamChanged -> kothDefenderChanged).
    bool objectiveBroadcast(const std::string& tag, int value);
    // TnGameReplicationInfoMultiplayer.OnObjectiveCountdownChange: CurrentObjectiveCountdown <= 5 and != -1 ->
    // LowObjectiveCountdownTickSound (BL_HUD_INTERFACE.EXTINCTION_ROUND_TIMER_01).
    bool objectiveCountdownChanged(int currentObjectiveCountdown);

    // ---- Gameplay match events (agents/gameplay game::Match::events()) -> the original broadcasts ----
    // MatchStarted: TnGameRules.HandleStartGame -> the mode's game-type message, switch 0. `modeTag` = GameModeTag
    // (TDM / DM / CTF / KOTH / DOM / EXT: TnOnlineGameSettings<tag>.Rules -> Team / FFA GameMessageClass);
    // `localTeam` sets the announcer voice (TnAnnouncer.SetTeam).
    bool onMatchStarted(const std::string& modeTag, int localTeam);
    bool onGameNearlyComplete();                     // GameNearlyComplete -> switch 1
    bool onProgressAnnouncement(int sw) { return progressAnnouncement(sw); }   // Time / KillsLeft announcement value
    // MatchEnded: HandleEndGame(Winner) -> switch 2; team games also TnVersusGameOverMessage [PARTIAL].
    bool onMatchEnded(int winnerTeam, bool localPlayerWon);
    static std::string messageClassForMode(const std::string& modeTag);

    // ---- objective / round messages (Gameplay broadcasts; Systems plays) [CONF script + class defaults] ----
    // TnFlagMessage.GetColoredString: 0 returned, 1 picked up, 2 dropped, 3 scored -> the HUD stinger (PC.PlaySound,
    // 2D: CTF_FLAG_RETURN / PICKUP / DROP / CAPTURE) and the announcer line (MP_Flag*Dialog).
    bool flagMessage(int sw);
    // TnBombMessage: 1 picked up (by `team`: MP_AutobotPickupBomb / MP_DecepticonPickupBomb), 2 dropped, 3 detonated
    // (by `team`), 4 defused, 5 planted.
    bool bombMessage(int sw, int team);
    // TnDominationMessage: switch = point * 10 + type; type 0 Autobots take point, 1 Decepticons take, 2 Autobots
    // capturing, 3 Decepticons capturing; points A..E.
    bool dominationMessage(int sw);
    // TnCTFMessage: the local player's team attacks -> AttackerDialog, else DefenderDialog.
    bool ctfMessage(bool localTeamAttacks);
    // TnRoundBasedGameMessage: 0 Autobots win the round, 1 Decepticons, 2 time up (+ TimeUpMusic), 3 switching sides
    // (+ SwitchingSidesMusic); music FadeIn / FadeOut 0, Priority 0.
    bool roundMessage(int sw);
    // TnKingOfTheHillZoneBase.PlayAnnouncerDialog (not while the match is over): Active.BeginState -> ZoneChangeSound;
    // DefendingTeamChanged (unless IgnoringTeamChangeAnnouncement): 0 / 1 captured by that team, 254 contested,
    // 255 neutral.
    // TnKingOfTheHillZoneBase.MatchStarting -> StartIgnoringAnnouncer(AnnouncerMatchStartHysteresisTime = 3 s): the hill
    // lines are suppressed that long from the match start [CONF script + class default].
    void kothMatchStarting();
    bool kothZoneActivated(bool matchOver = false);
    bool kothDefenderChanged(int defenderTeamIndex, bool ignoringTeamChange = false, bool matchOver = false);

    // Diagnostics.
    const std::string& currentCue() const { return current_; }
    const std::string& queuedCue() const { return queued_; }
    bool speaking() const;
    int linesPlayed() const { return lines_; }
    const std::string& dialogCharacter() const { return dialogChar_; }
    static bool hasMessageClass(const std::string& messageClass);

private:
    float clock_ = 0.0f, kothIgnoreUntil_ = -1.0f;
    bool playUiCue(const std::string& cue);
    bool play(const std::string& cue);               // TnAnnouncer.Play / PlayInternal
    bool higherPriority(const std::string& cue) const;
    SoundCues& cues_;
    MusicPlayer& music_;
    std::map<std::string, std::string> events_;
    std::string current_, queued_, dialogChar_;
    int instance_ = -1, lines_ = 0;
    std::string modeClass_;
};

} // namespace game
