// Campaign runtime step 3 (anchor edits only: Systems' audio hooks in WorldCampaign.cpp / Campaign.h stay): the pawn factories
// (WorldCampaignRuntime.cpp, a new file), the in-map pick swap, the campaign skip of the iconic-mode refusal, CAMPAIGNTEST checks.
// WorldCampaignRuntime.cpp copied from argv[2].
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
fs.copyFileSync(path.join(G, 'docs/handoff/campaign/WorldCampaignRuntime.cpp'), 'src/game/WorldCampaignRuntime.cpp');
ed('src/game/Campaign.h', [
    ['struct CampaignState {\n',
     '// A SeqAct_TnPawnFactory of the level Kismet (AssetTools next_map/<CH>_pawn_factories.json) and its pooled AI participants.\n' +
     'struct CampaignFactory {\n' +
     '    std::string action, cls, aicd, chassis, spawnedVar, note;\n' +
     '    int op = -1, team = 0, count = 1, pending = 0, spawned = 0;\n' +
     '    bool ai = false, vehicle = false, alert = true, heldLogged = false;\n' +
     '    float delay = 0.5f, timer = 0.0f;\n' +
     '    std::vector<core::Vec3> points;\n' +
     '    std::vector<std::string> outputs;\n' +
     '    std::vector<int> pool;\n' +
     '};\n\n' +
     'struct CampaignState {\n'],
    ['    bool kismetLoaded = false, kismetBegun = false;\n',
     '    bool kismetLoaded = false, kismetBegun = false;\n' +
     '    std::vector<CampaignFactory> factories;   // step 3: the pawn factories\n' +
     '    std::vector<int> formPending;             // spawned with StartingFormOverride TF_Vehicle (the form change the step after)\n' +
     '    int enemiesSpawned = 0;\n' +
     '    std::vector<std::string> trioChassis;     // CampaignCharacters\' chassis, trio order\n' +
     '    std::string humanChassis;                 // the human\'s current pick (the squad is the trio minus it)\n'],
]);
ed('src/game/Kismet.h', [
    ['    Var* var(int i) {', '    int varCount() const { return (int)vars_.size(); }\n    Var* var(int i) {'],
]);
ed('src/game/World.h', [
    ['    bool cpLeaderMoved(const BotBrain& b) const;\n',
     '    bool cpLeaderMoved(const BotBrain& b) const;\n' +
     '    void cpLoadFactories();                       // WorldCampaignRuntime.cpp\n' +
     '    bool cpPawnFactoryActivate(KismetVM& vm, int op, int input);\n' +
     '    void cpTickFactories(float dt);\n' +
     '    void cpSyncPicks();\n'],
    ['    const CampaignState& campaign() const { return cp_; }\n',
     '    const CampaignState& campaign() const { return cp_; }\n' +
     '    int cpTestFireFactory(int k);   // WFC_CAMPAIGNTEST: Spawn Actor into factory k (its pool size, or -1)\n'],
]);
ed('src/game/Match.h', [
    ['        if (s_.iconicMode && s.type == 1)\n',
     '        if (s_.iconicMode && !s_.campaign && s.type == 1)   // campaign: the in-map pick takes a squad member\'s character (World swaps)\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['    const std::string mine = chassisForCharacter(cpChapter_.trio[(size_t)pick]);\n',
     '    const std::string mine = chassisForCharacter(cpChapter_.trio[(size_t)pick]);\n' +
     '    for (const std::string& n : cpChapter_.trio) cp_.trioChassis.push_back(chassisForCharacter(n));\n' +
     '    cp_.humanChassis = mine;\n'],
    ['        LOG_INFO("campaign: Kismet %s (%zu ops)", cp_.kismetLoaded ? "loaded" : "MISSING", cp_.vm.opCount());\n',
     '        LOG_INFO("campaign: Kismet %s (%zu ops)", cp_.kismetLoaded ? "loaded" : "MISSING", cp_.vm.opCount());\n' +
     '        cpLoadFactories();\n'],
    ['    if (c == "SeqAct_Conversation" && input == 0)',
     '    if (c == "SeqAct_TnPawnFactory") return cpPawnFactoryActivate(vm, op, input);\n    if (c == "SeqAct_Conversation" && input == 0)'],
    ['c == "SeqAct_TnPawnFactory" || ', ''],
    ['    if (cp_.kismetBegun && !cp_.gameOver) cp_.vm.tick(dt);\n',
     '    if (cp_.kismetBegun && !cp_.gameOver) cp_.vm.tick(dt);\n' +
     '    cpSyncPicks();\n' +
     '    if (!cp_.gameOver) cpTickFactories(dt);\n'],
]);
ed('src/core/Application_CampaignTest.cpp', [
    ['    LOG_INFO("CAMPAIGNTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n',
     '    {   // step 3: the pawn factories and the in-map pick\n' +
     '        int ai = 0, pooled = 0, firstAi = -1;\n' +
     '        for (size_t k = 0; k < S.factories.size(); ++k) if (S.factories[k].ai) { ++ai; pooled += (int)S.factories[k].pool.size(); if (firstAi < 0 && !S.factories[k].pool.empty()) firstAi = (int)k; }\n' +
     '        check(S.factories.size() == 30 && ai == 26 && pooled >= 24, "D5 pawn factories: 30 (26 AI), the Jet factories pooled");\n' +
     '        const int n = world_.cpTestFireFactory(firstAi);\n' +
     '        run(2.0f);\n' +
     '        bool alive = false; int team = -1;\n' +
     '        if (firstAi >= 0) for (int p : S.factories[(size_t)firstAi].pool) if (m.players()[(size_t)p].alive) { alive = true; team = m.players()[(size_t)p].team; }\n' +
     '        check(n > 0 && alive && team == 0 && S.enemiesSpawned >= 1, "Spawn Actor: the factory spawns its Autobot AI at its spawn point");\n' +
     '        game::CharacterSelection pick; pick.type = 1; pick.chassisId = "Car3";   // the in-map pick: Breakdown\n' +
     '        const bool ok = world_.match().selectCharacter(world_.localMatchPlayer(), pick);\n' +
     '        run(0.2f);\n' +
     '        const bool swapped = std::find(S.squadChassis.begin(), S.squadChassis.end(), "Car3") == S.squadChassis.end() &&\n' +
     '                             std::find(S.squadChassis.begin(), S.squadChassis.end(), "Tank") != S.squadChassis.end();\n' +
     '        check(ok && swapped, "the in-map pick of Breakdown: accepted; the squad becomes Megatron + Soundwave");\n' +
     '    }\n' +
     '    LOG_INFO("CAMPAIGNTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n'],
]);
console.log('ok');
