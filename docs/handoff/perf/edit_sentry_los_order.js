// 300 fps sim plan (D1: cp.sentries+mines 0.36 ms mean, 0.8 ms in the heavy steps): cpTickSentries picks the nearest visible enemy
// in SightRadius, ray-testing every candidate closer than the best so far in player order. The candidates are now tested nearest
// first (equal distances: the higher player index first, the one the old loop kept) and the first visible one wins - the same target
// and distance, with fewer rays (a visible nearest enemy: one ray). Run from the tree root.
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
ed('src/game/WorldCampaignRuntime.cpp', [
    ['        int tgt = -1; float best = D.sight;' + NL +
     '        for (size_t q = 0; q < match_.players().size(); ++q) {' + NL +
     '            const MatchPlayer& mp = match_.players()[q];' + NL +
     '            if (!mp.alive || mp.team == team) continue;' + NL +
     '            const Character* c = participantPawn((int)q);' + NL +
     '            if (!c) continue;' + NL +
     '            const float L = core::length(c->actorLocation() - eye);' + NL +
     '            if ((S.forced >= 0 && (int)q != S.forced) || L > best) continue;' + NL +
     '            if (!botLineOfSight(eye, c->actorLocation())) continue;' + NL +
     '            best = L; tgt = (int)q;' + NL +
     '        }' + NL,
     '        int tgt = -1; float best = D.sight;' + NL +
     '        {   // the nearest visible candidate: nearest first, equal distances the higher player first (the old in-order scan kept the last)' + NL +
     '            static std::vector<std::pair<float, int>> cand;' + NL +
     '            cand.clear();' + NL +
     '            for (size_t q = 0; q < match_.players().size(); ++q) {' + NL +
     '                const MatchPlayer& mp = match_.players()[q];' + NL +
     '                if (!mp.alive || mp.team == team) continue;' + NL +
     '                const Character* c = participantPawn((int)q);' + NL +
     '                if (!c) continue;' + NL +
     '                const float L = core::length(c->actorLocation() - eye);' + NL +
     '                if ((S.forced >= 0 && (int)q != S.forced) || L > D.sight) continue;' + NL +
     '                cand.push_back({L, (int)q});' + NL +
     '            }' + NL +
     '            std::sort(cand.begin(), cand.end(), [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first != b.first ? a.first < b.first : a.second > b.second; });' + NL +
     '            for (const auto& cq : cand)' + NL +
     '                if (botLineOfSight(eye, participantPawn(cq.second)->actorLocation())) { best = cq.first; tgt = cq.second; break; }' + NL +
     '        }' + NL],
]);
console.log('ok');
