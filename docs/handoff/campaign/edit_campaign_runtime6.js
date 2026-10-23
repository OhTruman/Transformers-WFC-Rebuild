// Campaign runtime step 6 (anchor edits; on RUNTIME5): streaming (every unit loaded - collision merged, Rendering preloads -, a
// SeqAct_SetStreamingState sets each unit's visibility through IRenderer::setChapterUnitVisible), TnSeqAct_ForceIntoForm, shared AI pools
// per body, Restart From Last Checkpoint (World::cpRestartFromCheckpoint). WorldCampaignRuntime.cpp / WorldCampaignMatinee.cpp copied.
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
for (const f of ['WorldCampaignRuntime.cpp', 'WorldCampaignMatinee.cpp'])
    fs.copyFileSync(path.join(G, 'docs/handoff/campaign', f), 'src/game/' + f);
// Rendering's setChapterUnitVisible (their stage-3 patch): declared here as a default no-op only when it is not in the head yet.
{
    const r = fs.readFileSync('src/render/Renderer.h', 'utf8');
    if (!r.includes('setChapterUnitVisible'))
        ed('src/render/Renderer.h', [
            ['    virtual int loadChapterUnits(',
             '    // (campaign chapters, stage 3) a loaded unit shown / hidden by the chapter\'s streaming state (Gameplay calls it per changed unit)\n' +
             '    virtual void setChapterUnitVisible(const std::string& unit, bool visible) { (void)unit; (void)visible; }\n' +
             '    virtual int loadChapterUnits('],
        ]);
}
ed('src/game/Campaign.h', [
    ['    std::vector<std::string> units;',
     '    std::vector<std::string> allUnits;   // every sublevel dir of the chapter (streaming: all loaded at the start, shown per state)\n' +
     '    std::map<std::string, std::vector<std::string>> unitStates;   // unit dir -> the LevelStreamingStates it is visible in\n' +
     '    std::vector<std::string> units;'],
    ['    std::string humanChassis;',
     '    std::map<std::string, std::vector<int>> bodyPools;   // step 6: AI participants per body (shared by the factories)\n' +
     '    std::string streamingState;\n' +
     '    std::map<std::string, bool> unitVisible;\n' +
     '    std::string humanChassis;'],
]);
ed('src/game/World.h', [
    ['    void cpSyncPicks();\n',
     '    void cpSyncPicks();\n' +
     '    void cpApplyStreamingState(const std::string& state);\n' +
     '    bool cpSetStreamingState(KismetVM& vm, int op);\n' +
     '    bool cpForceIntoForm(KismetVM& vm, int op);\n' +
     '    MatchLaunch cpLaunch_;                        // the campaign launch (Restart From Last Checkpoint)\n'],
    ['    const CampaignState& campaign() const { return cp_; }\n',
     '    const CampaignState& campaign() const { return cp_; }\n' +
     '    bool cpRestartFromCheckpoint();   // RestartFromActiveCheckpoint (host; the Frontend\'s Restart From Last Checkpoint)\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['        if (sl[i]["in_initial_state"].asBool(false) || sl[i]["dir"].asString() == out.persistentDir) out.units.push_back(sl[i]["dir"].asString());\n',
     '        if (sl[i]["in_initial_state"].asBool(false) || sl[i]["dir"].asString() == out.persistentDir) out.units.push_back(sl[i]["dir"].asString());\n' +
     '    for (size_t i = 0; i < sl.size(); ++i) {   // every unit and its states (streaming)\n' +
     '        const std::string d = sl[i]["dir"].asString();\n' +
     '        out.allUnits.push_back(d);\n' +
     '        for (size_t k = 0; k < sl[i]["visible_in_states"].size(); ++k) out.unitStates[d].push_back(sl[i]["visible_in_states"][k].asString());\n' +
     '    }\n'],
    ['    for (const std::string& u : cpChapter_.units) {',
     '    for (const std::string& u : file == "world.glb" ? cpChapter_.units : cpChapter_.allUnits) {   // collision: every unit (streaming, PROV)'],
    ['    if (c == "SeqAct_SetStreamingState") {', '    if (c == "SeqAct_SetStreamingState") return cpSetStreamingState(vm, op);\n    if (c == "TnSeqAct_ForceIntoForm") return cpForceIntoForm(vm, op);\n    if (false) {'],
    ['        cpLoadMatinees();\n',
     '        cpLoadMatinees();\n' +
     '        {   // the start\'s streaming state: the launch checkpoint\'s, else WorldInfo.InitialStreamingState\n' +
     '            const CampaignChapter::Checkpoint* lc = cpChapter_.checkpoint(cp_.checkpoint);\n' +
     '            cp_.unitVisible.clear();\n' +
     '            cpApplyStreamingState(lc && !lc->state.empty() ? lc->state : cpChapter_.initialState);\n' +
     '        }\n' +
     '        cpLaunch_ = l;\n'],
]);
ed('src/game/World.cpp', [
    ['        for (const std::string& u : cpChapter_.units) if (u != cpChapter_.persistentDir) others.push_back(u);\n',
     '        for (const std::string& u : cpChapter_.allUnits) if (u != cpChapter_.persistentDir) others.push_back(u);   // every unit (streaming)\n'],
]);
console.log('ok');
