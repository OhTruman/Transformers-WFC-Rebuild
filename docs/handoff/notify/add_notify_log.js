// Adds the WFC_ANIMNOTIFYLOG diagnostic to drainAnimNotifies: =1 logs the local player's sent windows, =p<N> those of match player N.
// Usage: node add_notify_log.js src/game/World.cpp (after edit_anim_notify_window.js).
const fs = require('fs');
const f = process.argv[2];
let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
const anchor = 'void World::drainAnimNotifies() {\n';
const cap = '    auto drain = [this](Character& c, int mp) {\n';
const call = '            onAnimNotifyWindow(mp, it->second.animSet.c_str(), it->second.sequence.c_str(), w.t0, w.t1, false, w.weight);\n';
for (const a of [anchor, cap, call]) if (s.split(a).length !== 2) throw new Error('anchor: ' + a.slice(0, 60));
s = s.replace(anchor, anchor +
    '    static const char* logEnv = std::getenv("WFC_ANIMNOTIFYLOG");   // diagnostic\n' +
    '    const int logMp = !logEnv ? -2 : (logEnv[0] == \'p\' ? std::atoi(logEnv + 1) : localPlayer_);\n');
s = s.replace(cap, '    auto drain = [this, logMp](Character& c, int mp) {\n');
s = s.replace(call,
    '            if (mp == logMp)\n' +
    '                LOG_INFO("ANIMNOTIFY %s.%s [%.3f, %.3f) w %.2f", it->second.animSet.c_str(), it->second.sequence.c_str(), w.t0, w.t1, w.weight);\n' +
    call);
fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
console.log('ok');
