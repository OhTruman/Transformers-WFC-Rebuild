// Split screen follow-up (on the merged steps 1 + 2): seat 2 takes a bot slot on its team (campaign squad AI / versus bot) through
// the late-join handover, and World::draw gives the local pawn the absolute owner 100 + player while two seats play (Rendering's
// per-view setLocalOwner). WorldLocalSeat.cpp copied from argv[2].
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
fs.copyFileSync(path.join(G, 'docs/handoff/splitscreen/WorldLocalSeat.cpp'), 'src/game/WorldLocalSeat.cpp');
ed('src/game/World.cpp', [
    ['    if (!localPlayerDead()) { sysprof::Scope sp(sysprof::DrawPlayer); player_.draw(r); }',
     '    if (!localPlayerDead()) {\n' +
     '        sysprof::Scope sp(sysprof::DrawPlayer);\n' +
     '        const bool split = seat2_ >= 0;   // split screen: every pawn draws with its absolute owner (the views pick theirs)\n' +
     '        if (split) r.setDrawOwner(100 + localPlayer_);\n' +
     '        player_.draw(r);\n' +
     '        if (split) r.setDrawOwner(0);\n' +
     '    }'],
]);
console.log('ok');
