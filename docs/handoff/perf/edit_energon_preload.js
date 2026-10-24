// 300 fps sim plan, the first measured step (step 61, 3.4-6.2 ms on every chapter): tickVehicleBoost -> chassisEnergonDefault read
// and parsed the local chassis' character.json on its first call (2.4-2.5 ms in the remainder at the match start). Its per-chassis
// cache is now filled at the match load for every participant's chassis (after the bots, the Escalation slots and the campaign
// squad / factories exist). Load scheduling only: the same values, read earlier. Run from the tree root.
const fs = require('fs');
const NL = String.fromCharCode(10), CR = String.fromCharCode(13);
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes(CR + NL); s = s.split(CR + NL).join(NL);
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.split(NL).join(CR + NL) : s);
}
ed('src/game/World.cpp', [
    ['    setupCampaign(l);   // campaign: the human\'s pick, the AI squad, the level Kismet (no-op otherwise)' + NL,
     '    setupCampaign(l);   // campaign: the human\'s pick, the AI squad, the level Kismet (no-op otherwise)' + NL +
     '    {   // every participant chassis\' energon default (its character.json) now, not in the first vehicle-FX / mine step. Load scheduling only.' + NL +
     '        float e[3];' + NL +
     '        for (size_t q = 0; q < match_.players().size(); ++q) {' + NL +
     '            const std::string ch = resolveChassis(match_.players()[q].selection, match_.faction((int)q));' + NL +
     '            if (!ch.empty()) chassisEnergonDefault(ch, e);' + NL +
     '        }' + NL +
     '    }' + NL],
]);
console.log('ok');
