// Campaign step 2 (TnCampaignGameSolo, the D5_OME boot): the chapter World (the units' collision merged; the render units through
// Rendering's loadChapterUnits), GameModeTag SOLO / CP / CCP, the chapter start / checkpoint starts, the AI squad (Move-To-Buddy, instant
// respawn near a human), the game over -> InProgressOver -> UI event 14, the level Kismet ticking, WFC_CAMPAIGNTEST; plus RE's Kismet VM
// review (K1-K6). Sources copied from argv[2]: Campaign.h, WorldCampaign.cpp, Kismet.h / .cpp, Application_CampaignTest.cpp.
const fs = require('fs');
const path = require('path');
const G = process.argv[2] || path.resolve(__dirname, '..', '..', '..');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
for (const [src, dst] of [['Campaign.h', 'src/game/Campaign.h'], ['WorldCampaign.cpp', 'src/game/WorldCampaign.cpp'], ['Kismet.h', 'src/game/Kismet.h'],
                          ['Kismet.cpp', 'src/game/Kismet.cpp'], ['Application_CampaignTest.cpp', 'src/core/Application_CampaignTest.cpp'], ['Application_KismetTest.cpp', 'src/core/Application_KismetTest.cpp']])
    fs.copyFileSync(path.join(G, 'docs/handoff/campaign', src), dst);

ed('src/game/Match.h', [
    ['    bool survival = false;        // Escalation (GameModeTag ESC): the director spawns the horde; TnRespawnHelperRoundEnd (no timer)\n',
     '    bool survival = false;        // Escalation (GameModeTag ESC): the director spawns the horde; TnRespawnHelperRoundEnd (no timer)\n' +
     '    bool campaign = false;        // campaign chapters (GameModeTag SOLO / CP / CCP, TnCampaignGame): no respawn timer (World respawns the AI\n' +
     '                                  // squad; a human death is the game over)\n'],
    ['    enum class State { None, PendingMatch, InProgress, MatchOver, Returned };\n',
     '    enum class State { None, PendingMatch, InProgress, MatchOver, Returned, InProgressOver };   // InProgressOver: TnCampaignGame after CampaignGameOver\n'],
    ['        RemoteIdle,           // online host rule: Remote participant `player` idle for World::kRemoteIdleKickSeconds; the session kicks that peer\n',
     '        RemoteIdle,           // online host rule: Remote participant `player` idle for World::kRemoteIdleKickSeconds; the session kicks that peer\n' +
     '        // Campaign (RE CAMPAIGN_RUNTIME_EVENTS_RE section 7): SendGameOverEvent (value 14 = the UI event), NotifyReachedCheckpoint (text = the\n' +
     '        // checkpoint actor, value = its DisplayOrder), StartCompleteMission (text = the chapter, value = MapId).\n' +
     '        CampaignGameOver, CheckpointReached, MissionComplete,\n'],
    ['    void endSurvival(const std::string& reason) { endGame(-1, reason); }\n',
     '    void endSurvival(const std::string& reason) { endGame(-1, reason); }\n' +
     '    // Campaign chapters (World): the chapter\'s starts, InProgress.CampaignGameOver -> InProgressOver, the campaign events.\n' +
     '    void setCampaignStarts(const std::vector<Start>& s) { starts_ = s; clusters_.clear(); }\n' +
     '    void campaignGameOver() { if (state_ == State::InProgress) { state_ = State::InProgressOver; stateTime_ = 0.0f; } }\n' +
     '    void emitCampaignEvent(MatchEvent::Type t, int player, const std::string& text, int value) { emit(t, player, value, text); }\n'],
]);
ed('src/game/Match.cpp', [
    ['        case Match::State::Returned: return "Returned";\n',
     '        case Match::State::Returned: return "Returned"; case Match::State::InProgressOver: return "InProgressOver";\n'],
    ['    else { s.modeTag = "TDM"; s.teamGame = true;',
     '    // SOLO / CP / CCP (TnCampaignGameSolo / TnCoopGame): no score goal, no time limit; StartMatch as soon as the host has a character\n' +
     '    // (CanStartMatch; the 1 s timer here) [C, RE CAMPAIGN_RUNTIME_EVENTS_RE section 2].\n' +
     '    else if (tag == "SOLO" || tag == "CP" || tag == "CCP") { s.teamGame = true; s.goalScore = 0; s.timeLimit = 0; s.gameType = "TNGT_CAMPAIGN"; s.teamScoreAmount = 0;\n' +
     '                             s.reportKills = false; s.reportPoints = false; s.campaign = true; s.matchAutoStartCountdown = 0; }\n' +
     '    else { s.modeTag = "TDM"; s.teamGame = true;'],
    ['        if (s_.teamGame && !s_.survival) tickForfeit(dt);\n',
     '        if (s_.teamGame && !s_.survival && !s_.campaign) tickForfeit(dt);\n'],
    ['    if (state_ == State::InProgress) V.timeToRespawn = s_.survival ? -1.0f : s_.waveRespawnTime;\n',
     '    if (state_ == State::InProgress) V.timeToRespawn = s_.survival || s_.campaign ? -1.0f : s_.waveRespawnTime;\n'],
]);
ed('src/game/World.h', [
    ['#include "game/Survival.h"\n', '#include "game/Survival.h"\n#include "game/Campaign.h"\n'],
    ['    int localSeat = -1;                // online: this machine\'s index in humans (match player localSeat on every peer)\n',
     '    int localSeat = -1;                // online: this machine\'s index in humans (match player localSeat on every peer)\n' +
     '    // Campaign (the Frontend C6 contract): ?Checkpoint=<path> (absent = the chapter start), ?WfcDifficulty=0|1|2, ?MapId; PROV ?Character\n' +
     '    // (the in-map CharacterSelect: a trio CharacterName or 0..2; default the first).\n' +
     '    std::string checkpoint; int difficulty = 1; int mapId = -1; std::string character;\n'],
    ['    int svWave = 0; int svNextWaveTime = -1; bool svGameOver = false;\n',
     '    int svWave = 0; int svNextWaveTime = -1; bool svGameOver = false;\n' +
     '    // Campaign: the game over shown (UI event 14 -> CampaignGameOver_GFX), MissionComplete, the active checkpoint actor.\n' +
     '    bool cpActive = false, cpGameOver = false, cpMissionComplete = false; std::string cpCheckpoint;\n'],
    ['    const SurvivalData& survivalData() const { return svData_; }\n',
     '    const SurvivalData& survivalData() const { return svData_; }\n' +
     '    // Campaign chapters (WorldCampaign.cpp)\n' +
     '    bool campaignMap() const { return campaignMap_; }\n' +
     '    const CampaignChapter& campaignChapter() const { return cpChapter_; }\n' +
     '    const CampaignState& campaign() const { return cp_; }\n' +
     '    bool cpKismetActivate(KismetVM& vm, int op, int input);   // the level Kismet\'s host ops\n' +
     '    void cpKismetLatent(KismetVM& vm, int op, float dt);\n'],
    ['    void setupSurvival();\n',
     '    void setupSurvival();\n' +
     '    // Campaign (WorldCampaign.cpp): the squad + Kismet at launch; the game over / squad respawn / Kismet per step (after match_.tick).\n' +
     '    void setupCampaign(const MatchLaunch& l);\n' +
     '    void tickCampaign(float dt);\n' +
     '    bool loadCampaignMesh(const std::string& file, render::MeshData& out);\n' +
     '    void cpSetStarts(const std::string& checkpoint);\n' +
     '    void cpGameOver(int victim);\n' +
     '    const Character* cpLeader(int bot) const;\n' +
     '    bool cpSquadGoal(BotBrain& b, BotGoal& g);\n' +
     '    bool cpLeaderMoved(const BotBrain& b) const;\n'],
    ['    SurvivalState sv_;\n',
     '    SurvivalState sv_;\n' +
     '    CampaignChapter cpChapter_;                 // the loaded chapter (campaignMap_)\n' +
     '    CampaignState cp_;\n' +
     '    bool campaignMap_ = false;\n' +
     '    std::string cpLaunchCheckpoint_;\n' +
     '    std::unique_ptr<KismetVM::Host> cpHost_;\n'],
]);
ed('src/game/World.cpp', [
    ['    renderer.loadMapRenderData(mapName_);   // original-data shader path (if generated)\n',
     '    // Campaign chapters (D5_OME_BASE_m / D5_OME_BASE / D5_OME): AssetTools\' per-unit chapter data; the renderer takes the persistent\n' +
     '    // level and merges the other units itself (Rendering\'s chapter loader); collision / spawns / nav come from the units here.\n' +
     '    campaignMap_ = false;\n' +
     '    {\n' +
     '        const std::string ch = CampaignChapter::chapterOf(mapName_);\n' +
     '        if (!ch.empty() && CampaignChapter::load(ch, cpChapter_)) {\n' +
     '            campaignMap_ = true; mapName_ = ch;\n' +
     '            LOG_INFO("campaign: chapter %s (MapId %d), %zu units at the start (state %s)", ch.c_str(), cpChapter_.mapId, cpChapter_.units.size(), cpChapter_.initialState.c_str());\n' +
     '        } else if (!ch.empty()) LOG_ERROR("campaign: chapter %s has no data under %s", ch.c_str(), CampaignChapter::root().c_str());\n' +
     '    }\n' +
     '    renderer.loadMapRenderData(campaignMap_ ? cpChapter_.persistentDir : mapName_);   // original-data shader path (if generated)\n' +
     '    int chapterUnitsDrawn = 0;\n' +
     '    if (campaignMap_) {\n' +
     '        std::vector<std::string> others;\n' +
     '        for (const std::string& u : cpChapter_.units) if (u != cpChapter_.persistentDir) others.push_back(u);\n' +
     '        chapterUnitsDrawn = renderer.loadChapterUnits(others);\n' +
     '    }\n'],
    ['    bool okMap = assets::loadGlb(mapDir() + "world.glb", mapMesh);\n',
     '    // campaign: the persistent level\'s world.glb (no triangles of its own) when the renderer drew the units, else every unit merged\n' +
     '    bool okMap = campaignMap_ ? (chapterUnitsDrawn > 0 ? (assets::loadGlb(mapDir() + "world.glb", mapMesh), true) : (loadCampaignMesh("world.glb", mapMesh), true))\n' +
     '                              : assets::loadGlb(mapDir() + "world.glb", mapMesh);\n'],
    ['    bool authored = assets::loadGlb(overlayed(mapDir + "collision_pawn.glb"), colMesh);\n',
     '    bool authored = campaignMap_ ? loadCampaignMesh("collision_pawn.glb", colMesh) : assets::loadGlb(overlayed(mapDir + "collision_pawn.glb"), colMesh);\n'],
    ['    respawnPlayer();\n    // Test spawn selection',
     '    if (campaignMap_) { spawnPos_ = cpChapter_.playerStart.pos; spawnYaw_ = 0.0f; }   // the chapter start (TnCampaignPlayerStart)\n' +
     '    respawnPlayer();\n    // Test spawn selection'],
    ['    if (match_.starts().empty()) {\n        std::string root = assetRoot();\n        match_.loadSpawnData(mapDir() + "gameplay.json");\n    }\n',
     '    if (campaignMap_ && s.campaign) cpSetStarts(cpLaunchCheckpoint_);   // the active checkpoint / the chapter start (+ squad aux spots)\n' +
     '    else if (match_.starts().empty()) {\n        std::string root = assetRoot();\n        match_.loadSpawnData(mapDir() + "gameplay.json");\n    }\n'],
    ['    if (!known) { LOG_WARN("match: unknown mode %s", l.modeTag.c_str()); return false; }\n',
     '    if (l.settings.campaign) { mode = MatchMode::TDM; known = true; }   // campaign: the TDM map state (no versus objectives) + TnCampaignGame\n' +
     '    if (!known) { LOG_WARN("match: unknown mode %s", l.modeTag.c_str()); return false; }\n' +
     '    if (l.settings.campaign != campaignMap_) {\n' +
     '        LOG_WARN("match: %s %s - campaign modes run on campaign chapters only", l.map.c_str(), l.modeTag.c_str());\n' +
     '        return false;\n    }\n'],
    ['    startLocalMatch(l.settings, svTeam >= 0 ? svTeam : l.localTeam);   // [integration M08b] the lobby team\n',
     '    cpLaunchCheckpoint_ = l.checkpoint;\n' +
     '    startLocalMatch(l.settings, svTeam >= 0 ? svTeam : l.settings.campaign ? cpChapter_.faction : l.localTeam);   // [integration M08b] the lobby team\n'],
    ['    setupSurvival();   // Escalation: the horde slots (no-op otherwise)\n',
     '    setupSurvival();   // Escalation: the horde slots (no-op otherwise)\n' +
     '    setupCampaign(l);   // campaign: the human\'s pick, the AI squad, the level Kismet (no-op otherwise)\n'],
    ['    tickSurvival(dt);   // Escalation director (no-op otherwise)\n',
     '    tickSurvival(dt);   // Escalation director (no-op otherwise)\n    tickCampaign(dt);   // campaign: Kismet, game over, squad respawn (no-op otherwise)\n'],
    ['    if (sv_.phase != SurvivalState::Phase::Off) {\n        h.svWave',
     '    h.cpActive = cp_.active; h.cpGameOver = cp_.gameOverSent; h.cpMissionComplete = cp_.missionComplete; h.cpCheckpoint = cp_.checkpoint;\n' +
     '    if (sv_.phase != SurvivalState::Phase::Off) {\n        h.svWave'],
    ['std::string World::mapDir() const { return assetRoot() + "/Maps/" + mapName_ + "/"; }',
     'std::string World::mapDir() const {\n' +
     '    if (campaignMap_) return cpChapter_.dir + cpChapter_.persistentDir + "/";   // the persistent unit (physics.json "world": KillZ)\n' +
     '    return assetRoot() + "/Maps/" + mapName_ + "/";\n}'],
    ['    for (const char* k : kMaps) if (lower(base) == lower(k)) return k;\n',
     '    for (const char* k : kMaps) if (lower(base) == lower(k)) return k;\n' +
     '    { const std::string ch = CampaignChapter::chapterOf(m); if (!ch.empty()) return ch; }   // campaign chapters: by the chapter id\n'],
    ['    if (opt.count("IconicMode")) out.settings.iconicMode = std::atoi(opt["IconicMode"].c_str()) != 0;   // GRI _bIconicMode (Frontend URL)\n',
     '    if (opt.count("IconicMode")) out.settings.iconicMode = std::atoi(opt["IconicMode"].c_str()) != 0;   // GRI _bIconicMode (Frontend URL)\n' +
     '    if (opt.count("Checkpoint")) out.checkpoint = opt["Checkpoint"];   // campaign (C6): a menu checkpoint\n' +
     '    if (opt.count("WfcDifficulty")) out.difficulty = std::clamp(std::atoi(opt["WfcDifficulty"].c_str()), 0, 2);\n' +
     '    if (opt.count("MapId")) out.mapId = std::atoi(opt["MapId"].c_str());\n' +
     '    if (opt.count("Character")) out.character = opt["Character"];\n'],
]);
ed('src/game/WorldBots.cpp', [
    ['    const std::string path = navDir ? std::string(navDir) + "/" + mapName_ + "/bot_nav.json" : overlayed(mapDir() + "bot_nav.json");\n',
     '    // campaign chapters: <chapter>/bot_nav.json (AssetTools, from the chapter\'s NavigationCells)\n' +
     '    const std::string path = navDir ? std::string(navDir) + "/" + mapName_ + "/bot_nav.json"\n' +
     '                           : campaignMap_ ? cpChapter_.dir + "bot_nav.json" : overlayed(mapDir() + "bot_nav.json");\n'],
    ['    const auto& objs = mapState_.objectives();\n    switch (matchMode_) {\n',
     '    const auto& objs = mapState_.objectives();\n' +
     '    if (cpSquadGoal(b, g)) return true;   // campaign AI squad: Move-To-Buddy (Kill outranks it)\n' +
     '    switch (matchMode_) {\n'],
    ['             now - b.goalTime > 40.0f ||\n',
     '             now - b.goalTime > 40.0f || cpLeaderMoved(b) ||   // campaign squad: the buddy moved on\n'],
]);
ed('src/core/Application.h', [
    ['    void runEscalationTest();   // WFC_ESCTEST (Application_EscTest.cpp)\n',
     '    void runEscalationTest();   // WFC_ESCTEST (Application_EscTest.cpp)\n    void runCampaignTest();     // WFC_CAMPAIGNTEST (Application_CampaignTest.cpp)\n'],
]);
ed('src/core/Application.cpp', [
    ['    if (std::getenv("WFC_ESCTEST")) { runEscalationTest(); return false; }',
     '    if (std::getenv("WFC_CAMPAIGNTEST")) { runCampaignTest(); return false; }   // campaign chapter boot (WFC_MAP=D5_OME)\n' +
     '    if (std::getenv("WFC_ESCTEST")) { runEscalationTest(); return false; }'],
]);
ed('src/core/Application_Frontend.cpp', [
    ['"WFC_ESCTEST", ', '"WFC_ESCTEST", "WFC_CAMPAIGNTEST", '],
]);
console.log('ok');
