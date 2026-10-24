// 300 fps sim preload, item 4 (delta on 5975369b): cpCharacterTargets' ROBODEF -> chassis table (roster_package.json) builds at the
// chapter load - the first TnSeqVar_Character-targeted op (TnAiUseInteract / SetMoveToObjective / Teleport) parsed it in its step
// (4-6 ms: A1 2452, A3 1844, D3B 3703, D5 76). And the two manifest-root fallbacks in WorldCampaignMatinee.cpp go through
// core::projectRoot() (DataRoots). Run from the tree root.
const fs = require('fs');
const NL = String.fromCharCode(10), CR = String.fromCharCode(13);
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes(CR + NL); s = s.split(CR + NL).join(NL);
    for (const [a, b, count] of pairs) {
        const n = s.split(a).length - 1;
        if (n !== (count || 1)) throw new Error(f + ': anchor count ' + n + ': ' + a.slice(0, 90));
        s = s.split(a).join(b);
    }
    fs.writeFileSync(f, crlf ? s.split(NL).join(CR + NL) : s);
}
ed('src/game/WorldCampaignMatinee.cpp', [
    ['    if (preloaded) LOG_INFO("campaign: %d matinee pawn clip files preloaded", preloaded);' + NL,
     '    if (preloaded) LOG_INFO("campaign: %d matinee pawn clip files preloaded", preloaded);' + NL +
     '    cpCharacterTargets("");   // its ROBODEF -> chassis table (roster_package.json) builds now, not in the first Character-targeted op' + NL],
    ['std::string(mr ? mr : "F:/Transformers Rebuild/AssetTools/manifests")', '(mr ? std::string(mr) : core::projectRoot() + "/AssetTools/manifests")', 2],
]);
console.log('ok');
