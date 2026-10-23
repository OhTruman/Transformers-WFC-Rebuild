// Escalation horde rule (RE ESCALATION_HORDE_AI section 1 / MP_ESCALATION 9.1, C): melee is a 1 s-interval action whenever adjacent -
// a director-spawned bot's melee cooldown is 1.0 s (the versus bots keep their skill-scaled 1.2-2.0 s + difficulty).
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/WorldBots.cpp', [
    ['            if (pc.isMeleeing()) { ++b.melees; b.meleeCooldown = b.frange(1.2f, 2.0f) + (2 - b.difficulty) * 0.5f; return; }',
     '            if (pc.isMeleeing()) {\n' +
     '                ++b.melees;\n' +
     '                // the Escalation horde: a 1 s-interval melee action whenever adjacent [C, RE ESCALATION_HORDE_AI 1]\n' +
     '                b.meleeCooldown = match_.players()[(size_t)o.matchPlayer()].directorSpawned ? 1.0f : b.frange(1.2f, 2.0f) + (2 - b.difficulty) * 0.5f;\n' +
     '                return;\n' +
     '            }'],
]);
console.log('ok');
