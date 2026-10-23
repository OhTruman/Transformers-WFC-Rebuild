#pragma once
// Campaign chapters (GameModeTag SOLO / CP / CCP: TnCampaignGameSolo / TnCoopGame <- TnCampaignGame) - step 2: the chapter World and the
// game flow. Behaviour: RE CAMPAIGN_RUNTIME_EVENTS_RE (chapter start, InProgress.BeginState + SetupAiSquad, game over -> InProgressOver ->
// UI 14 after 1 s), CAMPAIGN_SQUAD_AI_RE (the squad: the chapter trio minus the humans' picks, spawned at the active checkpoint, respawned
// at once in front of a player, never downed; a solo human is never downed either), CAMPAIGN_FRONTEND_FLOW_RE (the MapId table, the
// trios). Data: AssetTools out/campaign_render/<CH>/ (chapter.json, the sublevels' world / collision / physics, navigation.json,
// bot_nav.json once delivered) and manifests/next_map/<CH>_kismet.json. Owned and ticked by World (WorldCampaign.cpp).
#include "core/Math.h"
#include "game/Kismet.h"
#include "render/Mesh.h"
#include "assets/SkinnedModel.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace game {

struct CampaignChapter {
    bool ok = false;
    std::string chapter;                 // "D5_OME"
    std::string dir;                     // <campaign root>/<chapter>/
    std::string persistentDir;           // "D5_OME_BASE" (the persistent level's unit; physics.json "world" holds its KillZ)
    std::string initialState;            // WorldInfo.InitialStreamingState
    std::vector<std::string> allUnits;   // every sublevel dir of the chapter (streaming: all loaded at the start, shown per state)
    std::map<std::string, std::vector<std::string>> unitStates;   // unit dir -> the LevelStreamingStates it is visible in
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
// A matinee (InterpData) of the chapter: the tracks the runtime plays (movement, event keys, director cuts, fade, FOVAngle).
struct CampaignMatinee {
    struct Key { float t = 0.0f; float v[3] = {0, 0, 0}, ai[3] = {0, 0, 0}, lo[3] = {0, 0, 0}; int mode = 0; };   // 0 linear 1 constant 2 curve
    struct Track { int kind = 0; bool relative = false; std::vector<Key> pos, euler, curve; std::vector<std::pair<float, std::string>> events;
                   std::vector<float> animOffset, animRate; std::vector<uint8_t> animLoop; };   // kind 5 AnimControl: events = (t, clip)
    // kind 0 move, 1 event keys, 2 director cuts (camera group names), 3 fade, 4 FOVAngle
    struct Group { std::string name; bool director = false; std::map<std::string, std::vector<std::string>> actors; std::vector<Track> tracks; };
    float length = 0.0f;
    std::vector<Group> groups;
    std::string interpData;   // the InterpData path (the pawn-group key)
};
// A matinee group bound to characters at runtime (CHARACTER:<ROBODEF>): their chassis and the anim file of each clip.
struct CampaignPawnGroup { std::vector<std::string> chassis; std::map<std::string, std::string> clipFile; };
// An InterpActor of the chapter moved by a matinee: its moving collision set and its pose (glTF metres).
struct CampaignMover {
    std::string name;
    core::Vec3 pivot{0, 0, 0}, offset{0, 0, 0}, lastOffset{0, 0, 0}, lo{0, 0, 0}, hi{0, 0, 0};
    std::vector<core::Vec3> tris;   // pivot-relative
    int pawnSet = -1;
    int floorSet = -1;              // its footprint as a walkable floor (a mover whose collision is the rim only: A1's lift)
    bool navMoving = false;         // its nav cells moved since the last relink
    std::vector<int> navCells;      // its mover-attached nav cells
    core::Vec3 navOffset{0, 0, 0};  // the offset those cells were last moved to
};

// A campaign destructible (an HmDestructible blueprint instance): its pieces' current states, health and timers, its hit box.
struct CampaignDestructible {
    struct State { int handle = 0; std::string name, mesh; float health = 0.0f, autoTime = 0.0f; std::vector<std::pair<int, std::string>> links;
                   bool blocks = false; int colSet = -1;     // a static-mesh state blocks; its collision set (built once)
                   float damageDeps = 0.0f; };              // HmDamageDependentsDestructionEffectFactory DamageAmount (0 = none)
    struct Piece { std::string name; std::vector<State> states; int cur = 0; float health = 0.0f, timer = 0.0f; State* state(); };
    std::string name;
    core::Vec3 pos{0, 0, 0}, lo{0, 0, 0}, hi{0, 0, 0};
    float yawDeg = 0.0f;
    core::Vec3 posUE{0, 0, 0}, rotDeg{0, 0, 0};   // the UE placement (location UU, pitch / yaw / roll degrees)
    bool block = true;                            // the actor blocks (collision_flags.bBlockActors)
    std::vector<Piece> pieces;
    int lastHitter = -1;
    std::vector<int> events;   // its Kismet notification events (op indices)
    bool fixedBounds = false;  // the hit box is set by its owner (a sentry), not from the state meshes
    bool gone = false;         // removed (its factory's Kill All)
    bool off = false;          // SeqAct_TnToggleDestructible Turn Off: not damageable
};
struct CampaignMeshAsset { bool ok = false; render::MeshData mesh; render::MeshHandle handle = -1; };

// A matinee-animated skeletal actor (AnimControl): its mesh / anim files, placement, the clip and time the matinee sets.
struct CampaignSkel {
    std::string mesh, clip;
    std::vector<std::string> animFiles;
    core::Vec3 pos{0, 0, 0};
    float yawDeg = 0.0f, time = 0.0f;
    bool loop = false, active = false, tried = false, ok = false;
    assets::SkinnedModel model;
    render::MeshData skinned;
    std::vector<core::Mat4> scratch;
};

struct CampaignFactory {
    std::string action, cls, aicd, chassis, spawnedVar, note;
    int op = -1, team = 0, count = 1, pending = 0, spawned = 0;
    bool ai = false, vehicle = false, alert = true, heldLogged = false;
    float delay = 0.5f, timer = 0.0f;
    std::vector<core::Vec3> points;
    std::vector<std::string> outputs;
    std::vector<int> spawnedDes;   // a destructible factory's spawned actors (Kill All)
    std::vector<int> live;         // its spawned pawns still alive (Single / All Pawns Killed)
    std::vector<int> liveSentries; // a sentry factory's living sentries (CampaignState::sentries indices)
    std::vector<int> pool;
};

// A sentry TURRETDEF (AssetTools spawnables.json) and a spawned sentry (a stationary AI turret on its placed body).
struct CampaignSentryDef { std::string turretdef, weapon; float range = 80.0f, sight = 100.0f, lagDeg = 90.0f, aimDeg = 3.0f, deathLife = 1.0f, hp = 125.0f; };
struct CampaignSentry {
    int def = -1, factory = -1, des = -1, forced = -1;
    std::string body;                 // the placed turret body actor (hidden after its death)
    core::Vec3 pos{0, 0, 0};
    float yaw = 0.0f, cool = 0.0f, deadT = 0.0f;
    bool dead = false, dormant = false;
};

// A launched roller mine (its hit box / draw: a destructible).
struct CampaignMine { int factory = -1, des = -1; core::Vec3 pos{0, 0, 0}, vel{0, 0, 0}; float fuse = 10.0f, damage = 200.0f, radius = 12.8f, speed = 20.0f; bool done = false; };

// A player interact (TnInteract*) and its TnSeqEvent_Interact events.
struct CampaignInteract {
    std::string name, cls;
    core::Vec3 pos{0, 0, 0};
    float touch = 10.0f, useRange = 10.0f, hold = 2.0f, t = 0.0f;   // TouchDistance, the use range, the Begin Use sequence length
    bool singleUse = false, enabled = true;
    int user = -1, toucher = -1;
    std::vector<int> events;
};
// Omega Supreme (D5): the boss state machine and his arms (WorldCampaignOmega.cpp).
struct CampaignOmega {
    enum Stage { Intro, Healthy, Wounded, Healing, Corrupted, Critical, Dying, Dead };
    int p = -1, stage = Intro, pendingStage = 0;
    core::Vec3 anchor{0, 0, 0}; float yaw = 0.0f, timer = 0.0f;
    float sentryHp[4] = {0, 0, 0, 0}; int sentryKills = 0, lastSentryHitter = -1; float heartHp = 0.0f;
    int battery = -1, healPhase = 0, heals = 0; float healT = 0.0f; bool healCorrupt = false; std::vector<int> corrupt;
    int target = -1, robin = 0; float targetT = 0.0f;
    int grabVictim = -1, grabPhase = 0; float grabTimer = 0.0f, grabT = 0.0f;
    int frenzyLeft = 0; float frenzyNext = 0.0f, frenzyT = 0.0f, barrageT = 0.0f, aoeT = 0.0f, rocketT = 0.0f;
    uint32_t rng = 0x0E6A5u;
    float rand01() { rng = rng * 1664525u + 1013904223u; return (float)(rng >> 8) * (1.0f / 16777216.0f); }
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
    std::vector<CampaignMatinee> matinees;    // step 5: Matinee
    struct Interp { int matinee = -1, op = -1; std::string action; float rate = 1.0f, dir = 1.0f, pos = 0.0f; bool looping = false, rewindOnPlay = false, playing = false; bool viewing = false; float lastFade = 0.0f; std::vector<int> cinePawns; };
    std::vector<Interp> interps;              // one per SeqAct_Interp op
    bool viewOn = false;                      // the CinematicView sent active
    int cineOp = -1, cineViewOp = -1;         // the op driving the local camera / the CinematicView
    std::string cineCamGroup;
    struct FormOp { int op = -1; bool vehicle = false; std::vector<int> targets; };
    std::vector<FormOp> formOps;              // step 7: TnSeqAct_ForceIntoForm in progress
    struct Touch { int op = -1; bool playerOnly = true, cylinder = false; std::vector<std::vector<core::Vec3>> polys; core::Vec3 lo{0, 0, 0}, hi{0, 0, 0}, center{0, 0, 0};
                   float radius = 0.0f, halfHeight = 0.0f; std::set<int> inside; };
    std::vector<Touch> touches;               // step 8: SeqEvent_Touch
    std::vector<CampaignDestructible> destructibles;   // step 9
    std::vector<CampaignInteract> interacts;   // step 14
    std::string interactPrompt;                // the local player's interact in reach (its class; empty = none)
    std::vector<uint8_t> interactHeldPrev;
    CampaignOmega omega;
    size_t factoryKillsSeen = 0;               // the kill history read by the factory death outputs
    struct SentryFactory { int def = -1, count = 1; std::vector<core::Vec3> points; std::vector<std::string> bodies; };
    std::vector<CampaignSentryDef> sentryDefs;
    std::map<std::string, SentryFactory> sentryFactories;   // factory action path -> its TURRETDEF + spawn points
    std::vector<CampaignSentry> sentries;
    struct MineFactory { float damage = 200.0f, radius = 12.8f, health = 30.0f, fuse = 10.0f, speed = 20.0f; std::vector<core::Vec3> points; };
    std::map<std::string, MineFactory> mineFactories;   // factory action path -> its authored mine values + spawn points
    std::vector<CampaignMine> mines;
    std::map<std::string, bool> actorHidden;   // world actor -> hidden (authored bHidden, then ToggleHidden / Destroy)
    std::set<std::string> actorDestroyed;      // SeqAct_Destroy'd world actors (they stay hidden)
    std::map<std::string, float> actorYaw;     // actors.json rotation_deg yaw (UE degrees): teleport destinations
    std::vector<std::pair<int, std::vector<int>>> transformOps;   // TnSeqAct_TransformWorldProp waiting for Finished: op, actors
    bool transformMoved = false;               // a matinee moved a transforming actor (nav blocks re-applied at rest)
    std::map<int, std::string> shootAt;        // SeqAct_TnAiShootAtActor: AI player -> its forced fire target
    std::map<int, std::string> aiUse;          // SeqAct_TnAiUseInteract: AI player -> the interact it goes to use
    std::map<std::string, std::vector<core::Vec3>> auxSpawns;   // checkpoint actor -> its AuxilliarySpawnPoints
    bool auxLoaded = false;
    bool omegaHold = false;                    // CAMPAIGNTEST: Omega stays passive
    bool cinematicMode = false;               // SeqAct_ToggleCinematicMode: the local player's input + HUD off
    std::map<std::string, std::vector<int>> bodyPools;   // step 6: AI participants per body (shared by the factories)
    std::string streamingState;
    std::map<std::string, bool> unitVisible;
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
