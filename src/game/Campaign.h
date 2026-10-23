#pragma once
// Campaign chapters (GameModeTag SOLO / CP / CCP: TnCampaignGameSolo / TnCoopGame <- TnCampaignGame) - step 2: the chapter World and the
// game flow. Behaviour: RE CAMPAIGN_RUNTIME_EVENTS_RE (chapter start, InProgress.BeginState + SetupAiSquad, game over -> InProgressOver ->
// UI 14 after 1 s), CAMPAIGN_SQUAD_AI_RE (the squad: the chapter trio minus the humans' picks, spawned at the active checkpoint, respawned
// at once in front of a player, never downed; a solo human is never downed either), CAMPAIGN_FRONTEND_FLOW_RE (the MapId table, the
// trios). Data: AssetTools out/campaign_render/<CH>/ (chapter.json, the sublevels' world / collision / physics, navigation.json,
// bot_nav.json once delivered) and manifests/next_map/<CH>_kismet.json. Owned and ticked by World (WorldCampaign.cpp).
#include "core/Math.h"
#include "game/Kismet.h"

#include <map>
#include <string>
#include <vector>

namespace game {

struct CampaignChapter {
    bool ok = false;
    std::string chapter;                 // "D5_OME"
    std::string dir;                     // <campaign root>/<chapter>/
    std::string persistentDir;           // "D5_OME_BASE" (the persistent level's unit; physics.json "world" holds its KillZ)
    std::string initialState;            // WorldInfo.InitialStreamingState
    std::vector<std::string> units;      // the sublevel dirs loaded at the start (initial state + the persistent level)
    int mapId = -1, faction = 0;         // TnDataProvider_MapInfo MapId (96..105); 0 Autobot, 1 Decepticon
    std::vector<std::string> trio;       // CampaignCharacters (CharacterName, the iconic order of TransLevels.ini)
    struct Point { std::string actor; core::Vec3 pos{0, 0, 0}; };
    Point playerStart;                   // TnCampaignPlayerStart (the chapter start)
    struct Checkpoint { std::string actor, tag; int displayOrder = 0; std::string state; core::Vec3 pos{0, 0, 0}; };
    std::vector<Checkpoint> checkpoints; // DisplayOrder order
    static std::string root();           // WFC_CAMPAIGN_ROOT or AssetTools out/campaign_render
    static std::string chapterOf(const std::string& name);   // "D5_OME_BASE_m" / "D5_OME_BASE" / "D5_OME" -> "D5_OME"; "" if none
    static bool load(const std::string& chapter, CampaignChapter& out);
    const Checkpoint* checkpoint(const std::string& nameOrPath) const;   // by actor name / path suffix / tag
};

// A SeqAct_TnPawnFactory of the level Kismet (AssetTools next_map/<CH>_pawn_factories.json) and its pooled AI participants.
struct CampaignFactory {
    std::string action, cls, aicd, chassis, spawnedVar, note;
    int op = -1, team = 0, count = 1, pending = 0, spawned = 0;
    bool ai = false, vehicle = false, alert = true, heldLogged = false;
    float delay = 0.5f, timer = 0.0f;
    std::vector<core::Vec3> points;
    std::vector<std::string> outputs;
    std::vector<int> pool;
};

struct CampaignState {
    bool active = false;
    int difficulty = 1;                  // ?WfcDifficulty 0 / 1 / 2 (GameDifficulty)
    std::string checkpoint;              // the active checkpoint's actor ("" = the chapter start)
    std::vector<int> squad;              // the AI squad's match players
    std::vector<std::string> squadChassis;
    bool gameOver = false;               // InProgress.CampaignGameOver -> InProgressOver
    float gameOverTime = -1.0f;          // match time of the game over
    bool gameOverSent = false;           // SendGameOverEvent (UI event 14) after _DelayBeforeShowingGameOverScreen 1 s
    int gameOverVictim = -1;
    bool missionComplete = false;        // TnSeqAct_MissionComplete -> StartCompleteMission
    size_t killsSeen = 0;                // Match::killHistory entries already looked at
    std::vector<std::string> checkpointsReached;   // NotifyReachedCheckpoint (SeqAct_SetActiveCheckpoint), in order
    bool kismetLoaded = false, kismetBegun = false;
    std::vector<CampaignFactory> factories;   // step 3: the pawn factories
    std::vector<int> formPending;             // spawned with StartingFormOverride TF_Vehicle (the form change the step after)
    int enemiesSpawned = 0;
    std::vector<std::string> trioChassis;     // CampaignCharacters' chassis, trio order
    struct Objective { core::Vec3 pos{0, 0, 0}; float radius = 5.0f; bool persistent = false; };
    std::map<int, Objective> objectives;      // step 4: Kismet AI objectives per match player (MoveToActor / SetMoveToObjective)
    std::map<std::string, core::Vec3> actorPos;   // actor name -> location (navigation.json, actors.json)
    std::string humanChassis;                 // the human's current pick (the squad is the trio minus it)
    KismetVM vm;
    // [Systems] the VM's audio ops (LevelAudioScript hosted): VM op -> its audio op id ("<level>|<path under Main_Sequence>"; "" = not
    // audio) and back. Built once the level audio and the VM are both loaded.
    std::vector<std::string> audioId;
    std::map<std::string, int> audioOp;
    int audioOps = 0;
    // [Systems] SIM-TIMED conversations (the chapter data's line durations; every peer, with or without audio, on the same tick):
    // op key ("<level>|<path under Main_Sequence>") per VM op, the lines per conversation key, the running ones by VM op.
    struct DialogLine { float duration = -1.0f, postDelay = 0.0f; std::string output; };
    struct DialogRun { int line = 0; float t = 0.0f; bool inPost = false; };
    std::vector<std::string> opKey;
    std::map<std::string, std::vector<DialogLine>> dialog;
    std::map<int, DialogRun> dialogRuns;
    // [Systems] the full-screen movie a Kismet op is playing (SeqAct_MoviePlayer / TnSeqAct_MissionComplete MovieName): the frontend's
    // movie player shows it (Game.PlayMovie path) and its end fires the op's Stopped. movieSerial changes per request.
    std::string movie;
    int movieOp = -1, movieSerial = 0;
    bool movieStop = false;
};

} // namespace game
