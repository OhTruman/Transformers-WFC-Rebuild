// Campaign: the AI squad never suicides (Experimental's D5 soak: the versus strand rule killed Breakdown at t 262); a stranded member is
// flagged (BotBrain::cpRecover) and moved back beside its buddy by tickCampaign. WorldCampaign.cpp copied from argv[2].
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
fs.copyFileSync(path.join(G, 'docs/handoff/campaign/WorldCampaign.cpp'), 'src/game/WorldCampaign.cpp');
ed('src/game/BotBrain.h', [
    ['    float offMeshRun = 0.0f;',
     '    bool cpRecover = false;    // campaign AI squad: stranded 30 s -> moved back beside its buddy (World::tickCampaign), never a suicide\n    float offMeshRun = 0.0f;'],
]);
ed('src/game/World.h', [
    ['    bool cpLeaderMoved(const BotBrain& b) const;\n',
     '    bool cpLeaderMoved(const BotBrain& b) const;\n' +
     '    bool cpSquadMember(int p) const { return cp_.active && std::find(cp_.squad.begin(), cp_.squad.end(), p) != cp_.squad.end(); }\n'],
]);
ed('src/game/WorldBots.cpp', [
    ['                    if (nowS - b.noWaySince >= 30.0f && !noSuicide && !match_.players()[(size_t)b.player].directorSpawned) b.suicideWish = true;\n',
     '                    if (nowS - b.noWaySince >= 30.0f && !noSuicide && !match_.players()[(size_t)b.player].directorSpawned) {\n' +
     '                        if (cpSquadMember(b.player)) b.cpRecover = true; else b.suicideWish = true;   // the campaign squad: moved back, no suicide\n' +
     '                    }\n'],
    ['        if ((b.offMeshRun += dt) >= 30.0f && !noSuicide && !b.suicideWish && !match_.players()[(size_t)b.player].directorSpawned) {\n',
     '        b.offMeshRun += dt;\n' +
     '        if (b.offMeshRun >= 30.0f && !noSuicide && !b.suicideWish && cpSquadMember(b.player)) b.cpRecover = true;   // the campaign squad: moved back\n' +
     '        else if (b.offMeshRun >= 30.0f && !noSuicide && !b.suicideWish && !match_.players()[(size_t)b.player].directorSpawned) {\n'],
]);
console.log('ok');
