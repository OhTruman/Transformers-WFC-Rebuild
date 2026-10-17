// Local-seat follow-up (Systems' request, contract 10.2): online is an explicit MatchLaunch.online set by the session from the launch
// record, never inferred from the seed (a deterministically seeded offline match must keep the direct path). The seed stays the RNG seed.
// Run from a tree root that has GAMEPLAY_LOCAL_SEAT (integration 578e21bb+).
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/World.h', [
    ['    uint32_t seed = 0;                 // online: the host\'s match seed (every peer alike); 0 = the local source (offline)\n',
     '    uint32_t seed = 0;                 // online: the host\'s match seed (every peer alike); 0 = the local source (offline)\n' +
     '    bool online = false;               // a session launch (from the launch record): every human through the wire path; offline false\n'],
    ['    bool onlineSeats_ = false;          // online: the local seat through the wire path (MatchLaunch.seed set, or the test override)\n',
     '    bool onlineSeats_ = false;          // online: the local seat through the wire path (MatchLaunch.online, or the test override)\n'],
]);
ed('src/game/World.cpp', [[
    '        onlineSeats_ = localWireOverride_ >= 0 ? localWireOverride_ == 1 : l.seed != 0;   // online: the local seat through the wire path\n',
    '        onlineSeats_ = localWireOverride_ >= 0 ? localWireOverride_ == 1 : l.online;   // online (a session launch; never from the seed)\n'
]]);
ed('src/core/Application_Frontend.cpp', [[
    '    gl.seed = m.seed;   // online: the host\'s match seed (0 offline)\n',
    '    gl.seed = m.seed;   // online: the host\'s match seed (0 offline)\n' +
    '    gl.online = !m.onlineSlots.empty();   // a session launch: the wire path for every human (offline: none)\n'
]]);
ed('src/core/Application.cpp', [[
    '            S.localTeam = 0; S.seed = 0x5eed4321u;\n',
    '            S.localTeam = 0; S.seed = 0x5eed4321u; S.online = true;\n'
]]);
console.log('ok');
