// TEST ONLY harness switches for Experimental's 300 fps rows (inert unless set; on the direct-boot list):
//   WFC_ESC_STARTWAVE=<n>: the first Escalation wave is wave n - its authored row (budget / alive cap / buffs), Round Begin count n
//                          (the round-armed map Kismet: power outage, round gates); generator purchases stay the players'.
//   WFC_ESC_INVULN=1:      the local player takes no damage in Escalation (the QA god-mode check, without WFC_QA).
// Run from the tree root.
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
ed('src/game/WorldSurvival.cpp', [
    ['        ++sv_.roundBegins;   // TnSeqEvent_SurvivalEvents "Round Begin" (ActivateSequenceEvent 1) [RE E1 review F3]' + NL,
     '        ++sv_.roundBegins;   // TnSeqEvent_SurvivalEvents "Round Begin" (ActivateSequenceEvent 1) [RE E1 review F3]' + NL +
     '        static const int startWave = std::getenv("WFC_ESC_STARTWAVE") ? std::atoi(std::getenv("WFC_ESC_STARTWAVE")) : 0;' + NL +
     '        if (startWave > 1 && sv_.displayWave == 0) {   // TEST ONLY (WFC_ESC_STARTWAVE): the first wave is wave n' + NL +
     '            sv_.currentWave = std::min(startWave - 1, (int)svData_.waves.size() - 1);' + NL +
     '            sv_.displayWave = startWave - 1;' + NL +
     '            sv_.roundBegins = startWave;' + NL +
     '        }' + NL],
]);
ed('src/game/World.cpp', [
    ['    if (qaGod_ && victim == localPlayer_) return false;   // DEV / QA TOOLING god mode',
     '    static const bool escInvuln = std::getenv("WFC_ESC_INVULN") != nullptr;   // TEST ONLY: the local player cannot die in Escalation' + NL +
     '    if ((qaGod_ || (escInvuln && match_.settings().survival)) && victim == localPlayer_) return false;   // DEV / QA TOOLING god mode'],
    // KillZ falls and the player bot's stranded suicide bypass applyMatchDamage: under WFC_ESC_INVULN they put the pawn back at its spawn
    ['    if (!matchActive_ || localDead_) return;' + NL + '    const Match::KillContext kc = ctx ? *ctx : killContext(killer, localPlayer_, damageType);',
     '    if (!matchActive_ || localDead_) return;' + NL +
     '    static const bool escInvuln = std::getenv("WFC_ESC_INVULN") != nullptr;   // TEST ONLY: no Escalation death (falls / stranded)' + NL +
     '    if (escInvuln && match_.settings().survival) { respawnPlayer(); return; }' + NL +
     '    const Match::KillContext kc = ctx ? *ctx : killContext(killer, localPlayer_, damageType);'],
]);
ed('src/core/Application_Frontend.cpp', [
    ['"WFC_ESCTEST", ', '"WFC_ESCTEST", "WFC_ESC_STARTWAVE", "WFC_ESC_INVULN", '],
]);
console.log('ok');
