// DIAGNOSTIC ONLY (after diag_follow_alone.js): one log line per "alone" follow reversal (WFC_FOLLOWTRACE=1):
//   FOLLOWREV p<n> t<s> lead v<m/s> d<m> | me v<m/s> goal <kind> d<m> r<m> moved<m> wp <i>/<n> wedge <0/1> hold <0/1> slotOff (<x>,<z>)
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/core/Application.cpp', [[
    '                                            if (dg < b->goal.radius + 3.0f) ++a.aNear; else if (moved > 3.0f) ++a.aJump; else ++a.aOther;\n',
    '                                            if (dg < b->goal.radius + 3.0f) ++a.aNear; else if (moved > 3.0f) ++a.aJump; else ++a.aOther;\n' +
    '                                            static const bool ft = std::getenv("WFC_FOLLOWTRACE") != nullptr;\n' +
    '                                            if (ft) {\n' +
    '                                                const game::SmartBot* sb = world_.smartBot(pl);\n' +
    '                                                LOG_INFO("FOLLOWREV p%d t%.2f lead v%.2f d%.1f | me v%.2f goal %s d%.1f r%.1f moved%.1f wp %zu/%zu wedge %d hold %d slotOff (%.1f,%.1f)",\n' +
    '                                                         pl, i * dt, core::length(lv), core::length(dd), core::length(v), game::botGoalName(b->goal.kind), dg, b->goal.radius, moved,\n' +
    '                                                         b->wp, b->path.size(), sb ? (int)sb->formWedge : -1, sb ? (int)sb->formHold : -1,\n' +
    '                                                         sb ? sb->formOffset.x : 0.0f, sb ? sb->formOffset.z : 0.0f);\n' +
    '                                            }\n'
]]);
console.log('ok');
