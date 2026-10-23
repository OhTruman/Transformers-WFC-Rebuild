// Campaign runtime step 10 (anchor edits; on RUNTIME9): streaming for real - the map load gives the renderer the initial-state units only;
// a SeqAct_SetStreamingState loads the units that become visible (loadChapterUnits({unit})) and hides + unloads the ones that become
// hidden (IRenderer::unloadChapterUnit, declared here as a default no-op when the head lacks it). Collision stays preloaded.
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
{
    const r = fs.readFileSync('src/render/Renderer.h', 'utf8');
    if (!r.includes('unloadChapterUnit'))
        ed('src/render/Renderer.h', [
            ['    virtual void setChapterUnitVisible(',
             '    // (campaign chapters, stage 3) free a unit\'s render data when its streaming state hides it (Gameplay; loadChapterUnits({unit}) loads it again)\n' +
             '    virtual void unloadChapterUnit(const std::string& unit) { (void)unit; }\n' +
             '    virtual void setChapterUnitVisible('],
        ]);
}
ed('src/game/World.cpp', [
    ['        for (const std::string& u : cpChapter_.allUnits) if (u != cpChapter_.persistentDir) others.push_back(u);   // every unit (streaming)\n',
     '        cpUnitLoaded_.clear();\n' +
     '        for (const std::string& u : cpChapter_.units) if (u != cpChapter_.persistentDir) { others.push_back(u); cpUnitLoaded_.insert(u); }   // the initial state\'s units (streaming loads the rest)\n'],
]);
ed('src/game/World.h', [
    ['    void cpApplyStreamingState(const std::string& state);\n',
     '    void cpApplyStreamingState(const std::string& state);\n' +
     '    std::set<std::string> cpUnitLoaded_;          // the chapter units whose render data is loaded (kept across launches: loaded with the map)\n'],
]);
console.log('ok');
