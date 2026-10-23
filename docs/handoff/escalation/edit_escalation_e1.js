// Escalation E1 (GAMEPLAY_ESCALATION_SPEC.md §9.1): the ESC launch tag, the director / wave budget / spawn finder (WorldSurvival.cpp,
// Survival.h, EscalationData.inc copied in from docs/handoff/escalation + src/game), no respawn timer in survival, the wave-end respawn,
// the all-dead loss, the HUD fields agreed with the Frontend (svWave / svNextWaveTime / svGameOver filled; the E2 / E4 ones declared),
// WFC_ESCTEST. Run from a tree root; argv[2] = the Gameplay worktree (sources of the new files).
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
for (const [src, dst] of [['docs/handoff/escalation/Survival.h', 'src/game/Survival.h'], ['docs/handoff/escalation/WorldSurvival.cpp', 'src/game/WorldSurvival.cpp'],
                          ['src/game/EscalationData.inc', 'src/game/EscalationData.inc'], ['docs/handoff/escalation/Application_EscTest.cpp', 'src/core/Application_EscTest.cpp']])
    fs.copyFileSync(path.join(G, src), dst);

ed('src/game/Match.h', [
    ['    bool extendedSlots = false;\n',
     '    bool extendedSlots = false;\n' +
     '    bool survival = false;        // Escalation (GameModeTag ESC): the director spawns the horde; TnRespawnHelperRoundEnd (no timer)\n'],
    ['    int currentKillStreak = 0;\n',
     '    int currentKillStreak = 0;\n' +
     '    bool directorSpawned = false;      // Escalation horde: spawned only by the director (never at the match start, never respawned)\n'],
    ['    void requireCharacterSelection(int p) {',
     '    // Escalation: spawn p at a director-chosen point (a TnSurvivalSpawnPoint); the respawn helper of a survival game for p; the\n' +
     '    // all-dead loss; the Match RNG for the director\'s picks (host-seeded online, so every peer draws alike).\n' +
     '    void restartPlayerAt(int p, const core::Vec3& pos, float yaw);\n' +
     '    void respawnPlayer(int p) { if (p >= 0 && (size_t)p < players_.size() && !players_[(size_t)p].alive) restartPlayer(p); }\n' +
     '    void endSurvival(const std::string& reason) { endGame(-1, reason); }\n' +
     '    int drawInt(int n) { return randomInt(n); }\n' +
     '    float drawFloat() { rng_ = rng_ * 1664525u + 1013904223u; return (float)((rng_ >> 8) & 0xFFFFFFu) / 16777216.0f; }\n' +
     '    void requireCharacterSelection(int p) {'],
    ['    unsigned rng_ = 0x1234ABCDu;\n',
     '    unsigned rng_ = 0x1234ABCDu;\n' +
     '    int forcedStart_ = -1;               // restartPlayerAt: the start restartPlayer takes instead of FindPlayerStart\n' +
     '    std::vector<int> directorStart_;     // per player: its reusable "SurvivalSpawnPoint" start (team 254: never a FindPlayerStart pick)\n'],
]);
ed('src/game/Match.cpp', [
    ['    else { s.modeTag = "TDM"; s.teamGame = true; s.goalScore = 40;',
     '    // ESC (Escalation, TnSurvivalGame): co-op against the horde, no score goal, no time limit, up to 4 players (the horde is ours\n' +
     '    // to size: one participant per spawn-list entry x MaxCount) [CONF RE MP_ESCALATION_RE §1].\n' +
     '    else if (tag == "ESC") { s.teamGame = true; s.goalScore = 0; s.timeLimit = 0; s.gameType = "TNGT_SV"; s.teamScoreAmount = 0;\n' +
     '                             s.reportKills = false; s.reportPoints = false; s.survival = true; s.maxPlayers = 128; s.maxPerTeam = 128; s.maxBotsPerTeam = 128; }\n' +
     '    else { s.modeTag = "TDM"; s.teamGame = true; s.goalScore = 40;'],
    ['    if (state_ == State::InProgress) V.timeToRespawn = s_.waveRespawnTime;\n}\n',
     '    // Survival: TnRespawnHelperRoundEnd - no timer; the director respawns the dead players at the wave end (the horde never).\n' +
     '    if (state_ == State::InProgress) V.timeToRespawn = s_.survival ? -1.0f : s_.waveRespawnTime;\n}\n'],
    ['    for (size_t i = 0; i < players_.size(); ++i) {\n        if (stagger && players_[i].kind == ParticipantKind::Bot)',
     '    for (size_t i = 0; i < players_.size(); ++i) {\n' +
     '        if (players_[i].directorSpawned) { players_[i].alive = false; players_[i].timeToRespawn = -1.0f; continue; }   // Escalation horde\n' +
     '        if (stagger && players_[i].kind == ParticipantKind::Bot)'],
    ['    int st = findPlayerStart(p);\n    // Extended matches',
     '    int st = forcedStart_ >= 0 ? forcedStart_ : findPlayerStart(p);\n    // Extended matches'],
    ['    winnerTeam_ = winner;\n',
     '    if (s_.survival) winner = -1;   // no win condition: AllDead ends it\n    winnerTeam_ = winner;\n'],
    ['void Match::setGeneratedStarts(',
     'void Match::restartPlayerAt(int p, const core::Vec3& pos, float yaw) {\n' +
     '    if (p < 0 || (size_t)p >= players_.size()) return;\n' +
     '    if (directorStart_.size() < players_.size()) directorStart_.resize(players_.size(), -1);\n' +
     '    int& si = directorStart_[(size_t)p];\n' +
     '    if (si < 0 || (size_t)si >= starts_.size() || starts_[(size_t)si].actor != "SurvivalSpawnPoint") {\n' +
     '        Start s; s.actor = "SurvivalSpawnPoint"; s.team = 254;\n' +
     '        starts_.push_back(s);\n' +
     '        si = (int)starts_.size() - 1;\n' +
     '    }\n' +
     '    starts_[(size_t)si].pos = pos; starts_[(size_t)si].yaw = yaw;\n' +
     '    forcedStart_ = si;\n' +
     '    restartPlayer(p);\n' +
     '    forcedStart_ = -1;\n' +
     '}\n\n' +
     'void Match::setGeneratedStarts('],
]);
ed('src/game/World.h', [
    ['#include "game/Match.h"\n', '#include "game/Match.h"\n#include "game/Survival.h"\n'],
    ['    std::vector<Row> scoreboard;\n',
     '    std::vector<Row> scoreboard;\n' +
     '    // Escalation (MP_ESCALATION_RE §8; 0 / -1 / false outside it): the wave (DisplayWave + 1), the PreGame / next-wave countdown (whole\n' +
     '    // s), the game over (TnSurvivalGameOverMessage wave = svWave). E2: the local seat\'s power and the generator prompt; E4: downed / revive.\n' +
     '    int svWave = 0; int svNextWaveTime = -1; bool svGameOver = false;\n' +
     '    int svPower = 0; std::string svGeneratorName; int svGeneratorNeeded = -1;\n' +
     '    bool svDowned = false; float svDownedHealth = 0.0f; bool svReviveTarget = false; float svReviveProgress = -1.0f;\n'],
    ['    MatchOpponent* addMatchOpponent(const std::string& name, bool drawn);\n',
     '    MatchOpponent* addMatchOpponent(const std::string& name, bool drawn);\n' +
     '    // Escalation: the director state and the map\'s data (tests, HUD).\n' +
     '    const SurvivalState& survival() const { return sv_; }\n' +
     '    const SurvivalData& survivalData() const { return svData_; }\n'],
    ['    void tickMatch(float dt);\n',
     '    void tickMatch(float dt);\n' +
     '    // Escalation (WorldSurvival.cpp): the horde slots + the spawn finder at launch; the director per step (after match_.tick).\n' +
     '    void setupSurvival();\n' +
     '    void tickSurvival(float dt);\n' +
     '    float svZoneScore(int zone, int type) const;\n' +
     '    void svSort(int type);\n' +
     '    void svTouch(int zone, int sign);\n' +
     '    bool svSpawn(int slot);\n' +
     '    SurvivalData svData_;\n' +
     '    SurvivalState sv_;\n'],
]);
ed('src/game/World.cpp', [
    ['        if (l.modeTag == gameModeName(m)) { mode = m; known = true; }\n',
     '        if (l.modeTag == gameModeName(m)) { mode = m; known = true; }\n' +
     '    if (l.modeTag == "ESC") { mode = MatchMode::TDM; known = true; }   // Escalation: the TDM map state (no versus objectives) + the director\n'],
    ['    startLocalMatch(l.settings, l.localTeam);   // [integration M08b] the lobby team\n',
     '    int svTeam = -1;   // Escalation: every player on the map\'s co-op team (CoopInfo faction)\n' +
     '    if (l.settings.survival) { SurvivalData d; if (SurvivalData::load(canonicalMapName(l.map), d)) svTeam = d.coopTeam; }\n' +
     '    startLocalMatch(l.settings, svTeam >= 0 ? svTeam : l.localTeam);   // [integration M08b] the lobby team\n'],
    ['            if (match_.settings().teamGame) me.team = l.humans[(size_t)ls].team;\n',
     '            if (match_.settings().teamGame) me.team = svTeam >= 0 ? svTeam : l.humans[(size_t)ls].team;\n'],
    ['        for (size_t i = 0; i < l.humans.size(); ++i) if ((int)i != ls) order[i] = addRemoteParticipant(l.humans[i], true);\n',
     '        for (size_t i = 0; i < l.humans.size(); ++i)\n' +
     '            if ((int)i != ls) { RemoteSeat h = l.humans[i]; if (svTeam >= 0) h.team = svTeam; order[i] = addRemoteParticipant(h, true); }\n'],
    ['        for (const RemoteSeat& r : l.remotes) addRemoteParticipant(r);',
     '        for (const RemoteSeat& r : l.remotes) { RemoteSeat h = r; if (svTeam >= 0) h.team = svTeam; addRemoteParticipant(h, svTeam >= 0); }'],
    ['    const int nb = addBots(l.bots);\n',
     '    const int nb = addBots(l.bots);\n    setupSurvival();   // Escalation: the horde slots (no-op otherwise)\n'],
    ['    match_.tick(dt);\n', '    match_.tick(dt);\n    tickSurvival(dt);   // Escalation director (no-op otherwise)\n'],
    ['        h.tags.push_back(t);\n    }\n    return h;\n}\n',
     '        h.tags.push_back(t);\n    }\n' +
     '    if (sv_.phase != SurvivalState::Phase::Off) {\n' +
     '        h.svWave = std::max(1, sv_.displayWave + 1);\n' +
     '        if (sv_.phase == SurvivalState::Phase::PreGame) h.svNextWaveTime = (int)std::ceil(std::max(0.0f, svData_.preGameTime - sv_.phaseTime));\n' +
     '        else if (sv_.phase == SurvivalState::Phase::BetweenWaves) h.svNextWaveTime = (int)std::ceil(std::max(0.0f, svData_.timeBetweenWaves - sv_.phaseTime));\n' +
     '        h.svGameOver = sv_.phase == SurvivalState::Phase::GameOver;\n' +
     '    }\n' +
     '    return h;\n}\n'],
]);
ed('src/core/Application.h', [['    void runTdmSessionTest();\n', '    void runTdmSessionTest();\n    void runEscalationTest();   // WFC_ESCTEST (Application_EscTest.cpp)\n']]);
ed('src/core/Application.cpp', [
    ['    if (std::getenv("WFC_TDMTEST")) { runTdmSessionTest(); return false; }     // measurements only\n',
     '    if (std::getenv("WFC_TDMTEST")) { runTdmSessionTest(); return false; }     // measurements only\n' +
     '    if (std::getenv("WFC_ESCTEST")) { runEscalationTest(); return false; }     // Escalation E1 director (WFC_MAP=MP_ESC_BrokenHope)\n'],
]);
ed('src/core/Application_Frontend.cpp', [['"WFC_TDMTEST", "WFC_MATCH",', '"WFC_TDMTEST", "WFC_ESCTEST", "WFC_MATCH",']]);
console.log('ok');
