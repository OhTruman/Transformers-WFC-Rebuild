// Campaign movies play once (Integration / Frontend 2026-10-23, A1's FMV_a1intro replayed after a skip): the Game.PlayMovie bridge
// hands the end to the World asynchronously (submit -> campaignMovieEnded), so on the next frame the presented snapshot still names
// the ended request and the bridge played it again (also FMV_a1text at 66.306 in handover5). A serial that has ended is never shown
// again. Anchor edit in src/core/Application_Frontend.cpp.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/core/Application_Frontend.cpp', [
    ['        static int shown = 0;   // the request serial on screen (0 none)\n' +
     '        if (!pf.campaignMovie.empty() && pf.campaignMovieSerial != shown && flow.scriptMovie().empty()) {\n',
     '        static int shown = 0;   // the request serial on screen (0 none)\n' +
     '        static int endedSerial = 0;   // the last serial handed back as ended (the snapshot can still name it for a frame or two)\n' +
     '        if (!pf.campaignMovie.empty() && pf.campaignMovieSerial != shown && pf.campaignMovieSerial != endedSerial && flow.scriptMovie().empty()) {\n'],
    ['            const int ended = shown;\n            shown = 0;\n',
     '            const int ended = shown;\n            shown = 0;\n            endedSerial = ended;\n'],
]);
console.log('ok');
