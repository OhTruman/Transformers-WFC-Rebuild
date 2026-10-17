// Harness only (eye checks, both arms alike; not part of any patch): WFC_LOADOUT=<w1>,<w2>,<w3> gives the WFC_CHASSIS boot selection those
// weapons (CharacterSelection.weapons, used as given). Run from a tree root.
const fs = require('fs');
const f = 'src/core/Application.cpp';
let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
const a = '            game::CharacterSelection sel; sel.type = 1; sel.chassisId = bootChassis;\n';
if (s.split(a).length !== 2) throw new Error('anchor');
s = s.replace(a, a +
    '            if (const char* lo = std::getenv("WFC_LOADOUT")) {   // harness: the boot selection\'s weapons\n' +
    '                std::string l = lo; size_t p0 = 0;\n' +
    '                while (p0 <= l.size()) { size_t p1 = l.find(\',\', p0); if (p1 == std::string::npos) p1 = l.size(); if (p1 > p0) sel.weapons.push_back(l.substr(p0, p1 - p0)); p0 = p1 + 1; }\n' +
    '            }\n');
fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
console.log('ok');
