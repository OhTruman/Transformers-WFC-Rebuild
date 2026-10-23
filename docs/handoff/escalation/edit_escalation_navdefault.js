// Escalation: the original NavigationCell nav ON by default (nav v3 floor-lowered + overlay h passed both maps' 300 s soaks; WFC_ESCNAV=0
// = the versus nav), and the energon_default lookup routes "ESC_<id>" chassis to AssetTools out/escalation_characters/<id>/ (as
// loadChassisDef). WorldSurvival.cpp / Application_EscTest.cpp copied from argv[2].
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
for (const [src, dst] of [['WorldSurvival.cpp', 'src/game/WorldSurvival.cpp'], ['Application_EscTest.cpp', 'src/core/Application_EscTest.cpp']])
    fs.copyFileSync(path.join(G, 'docs/handoff/escalation', src), dst);
ed('src/game/World.cpp', [
    ['        if (!chassis.empty() && readTextFile(assetRoot() + "/Characters/" + chassis + "/character.json", txt) && assets::Json::parse(txt, j) &&\n',
     '        // an Escalation horde body ("ESC_<id>") lives in AssetTools out/escalation_characters/<id>/ (loadChassisDef\'s rule)\n' +
     '        const std::string cj = chassis.rfind("ESC_", 0) == 0 ? assetRoot() + "/../../AssetTools/out/escalation_characters/" + chassis.substr(4) + "/character.json"\n' +
     '                                                         : assetRoot() + "/Characters/" + chassis + "/character.json";\n' +
     '        if (!chassis.empty() && readTextFile(cj, txt) && assets::Json::parse(txt, j) &&\n'],
]);
console.log('ok');
