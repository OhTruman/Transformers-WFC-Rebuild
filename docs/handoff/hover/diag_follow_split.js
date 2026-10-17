// DIAGNOSTIC ONLY (NAVQ; not in a patch): splits NAVQ "follow" reversals (a squad member reversing within 30 m of its leader) into
//   withLeader - the leader's own heading also reversed within the last ~1 s (the member follows a leader that turned round: legitimate);
//   alone      - the leader kept its heading (the member turned back on its own: the fidget to remove).
// Each bot keeps its last 4 quarter-second sample directions. Prints: NAVQ follow split: with-leader <n> / alone <m>.
// Run from a tree root.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/core/Application.cpp', [
    ['    struct Acc { double hoverT = 0,',
     '    struct Acc { core::Vec3 dirHist[4]{}; int dirN = 0; int followWith = 0, followAlone = 0;   // diag: follow split\n' +
     '                 double hoverT = 0,'],
    ['                                else if (core::length(dd) < 30.0f) ++a.follow;   // moving with / after its leader (formation following)\n',
     '                                else if (core::length(dd) < 30.0f) {\n' +
     '                                    ++a.follow;   // moving with / after its leader (formation following)\n' +
     '                                    const Acc& la = acc[ld];   // diag: did the leader turn round within ~1 s?\n' +
     '                                    bool turned = false;\n' +
     '                                    if (la.dirN >= 4) turned = core::dot(la.dirHist[(la.dirN - 1) % 4], la.dirHist[la.dirN % 4]) < 0.0f;\n' +
     '                                    if (turned) ++a.followWith; else ++a.followAlone;\n' +
     '                                }\n'],
    ['                    a.prevDir = dir; a.havePrev = true;\n',
     '                    a.prevDir = dir; a.havePrev = true;\n' +
     '                    a.dirHist[a.dirN % 4] = dir; ++a.dirN;\n'],
    ['    LOG_INFO("NAVQ SUMMARY map %s ai %s bots %d,',
     '    { int fw = 0, fa = 0; for (const auto& kv : acc) { fw += kv.second.followWith; fa += kv.second.followAlone; }\n' +
     '      LOG_INFO("NAVQ follow split: with-leader %d / alone %d", fw, fa); }\n' +
     '    LOG_INFO("NAVQ SUMMARY map %s ai %s bots %d,'],
]);
console.log('ok');
