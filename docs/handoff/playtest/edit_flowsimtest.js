// WFC_FLOWSIMTEST=<MP|ESC|CP>: the sim half of the playtest section 43 flows (Application_FlowSimTest.cpp from argv[2]). The body is
// appended to Application_Frontend.cpp, where fillOnlineLaunch (the frontend's launch builder) is file-local. Run from the tree root.
const fs = require('fs');
const path = require('path');
const G = process.argv[2] || path.resolve(__dirname, '..', '..', '..');
const NL = String.fromCharCode(10), CR = String.fromCharCode(13);
function norm(s) { return s.split(CR + NL).join(NL); }
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes(CR + NL); s = norm(s);
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.split(NL).join(CR + NL) : s);
}
{
    const body = norm(fs.readFileSync(path.join(G, 'docs/handoff/playtest/Application_FlowSimTest.cpp'), 'utf8'))
        .split(NL).filter(l => !l.startsWith('#include') && !l.startsWith('void fillOnlineLaunch(')).join(NL);
    const f = 'src/core/Application_Frontend.cpp';
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes(CR + NL);
    s = norm(s) + NL + body;
    fs.writeFileSync(f, crlf ? s.split(NL).join(CR + NL) : s);
}
ed('src/core/Application.h', [
    ['    void runSoloLaunchTest();', '    void runFlowSimTest();     // WFC_FLOWSIMTEST=<MP|ESC|CP>: the playtest section 43 flows\' sim half (solo launch -> play -> QA)' + NL + '    void runSoloLaunchTest();'],
]);
ed('src/core/Application.cpp', [
    ['    if (std::getenv("WFC_SOLOLAUNCHTEST")) { runSoloLaunchTest(); return false; }',
     '    if (std::getenv("WFC_FLOWSIMTEST")) { runFlowSimTest(); return false; }   // the section 43 flows\' sim half, headless' + NL +
     '    if (std::getenv("WFC_SOLOLAUNCHTEST")) { runSoloLaunchTest(); return false; }'],
]);
console.log('ok');
