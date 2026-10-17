// DIAGNOSTIC ONLY (NAVQ; not in a patch; after diag_follow_split.js): classifies the "alone" follow reversals (the member turned back
// while its leader kept its heading) by the member's goal at that moment:
//   near   - within goal radius + 3 m of its goal (arrival dithering around a slot);
//   jumped - its goal moved > 3 m within the last ~1 s (slot / goal jitter);
//   other  - neither (a path / steering issue).
// Prints: NAVQ follow alone: near <a> / jumped <b> / other <c>; goal kinds roam <r> attack <k> other <o>.
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
    ['    struct Acc { core::Vec3 dirHist[4]{}; int dirN = 0; int followWith = 0, followAlone = 0;   // diag: follow split\n',
     '    struct Acc { core::Vec3 dirHist[4]{}; int dirN = 0; int followWith = 0, followAlone = 0;   // diag: follow split\n' +
     '                 core::Vec3 goalHist[4]{}; int goalN = 0; int aNear = 0, aJump = 0, aOther = 0, gRoam = 0, gAttack = 0, gOther = 0;   // diag: alone\n'],
    ['                                    if (turned) ++a.followWith; else ++a.followAlone;\n',
     '                                    if (turned) ++a.followWith; else {\n' +
     '                                        ++a.followAlone;\n' +
     '                                        if (b && b->hasGoal) {\n' +
     '                                            const float dg = std::hypot(b->goal.pos.x - p.x, b->goal.pos.z - p.z);\n' +
     '                                            const core::Vec3 g1 = a.goalN >= 4 ? a.goalHist[a.goalN % 4] : b->goal.pos;\n' +
     '                                            const float moved = std::hypot(b->goal.pos.x - g1.x, b->goal.pos.z - g1.z);\n' +
     '                                            if (dg < b->goal.radius + 3.0f) ++a.aNear; else if (moved > 3.0f) ++a.aJump; else ++a.aOther;\n' +
     '                                            if (b->goal.kind == game::BotGoalKind::Roam) ++a.gRoam; else if (b->goal.kind == game::BotGoalKind::Attack) ++a.gAttack; else ++a.gOther;\n' +
     '                                        }\n' +
     '                                    }\n'],
    ['                    a.dirHist[a.dirN % 4] = dir; ++a.dirN;\n',
     '                    a.dirHist[a.dirN % 4] = dir; ++a.dirN;\n'],
    ['            if ((a.sampleT += dt) >= 0.25f) {\n',
     '            if ((a.sampleT += dt) >= 0.25f) {\n' +
     '                if (b && b->hasGoal) { a.goalHist[a.goalN % 4] = b->goal.pos; ++a.goalN; }   // diag: the goal ~1 s back\n'],
    ['      LOG_INFO("NAVQ follow split: with-leader %d / alone %d", fw, fa); }\n',
     '      LOG_INFO("NAVQ follow split: with-leader %d / alone %d", fw, fa);\n' +
     '      int n1 = 0, n2 = 0, n3 = 0, r = 0, k = 0, o = 0;\n' +
     '      for (const auto& kv : acc) { n1 += kv.second.aNear; n2 += kv.second.aJump; n3 += kv.second.aOther; r += kv.second.gRoam; k += kv.second.gAttack; o += kv.second.gOther; }\n' +
     '      LOG_INFO("NAVQ follow alone: near %d / jumped %d / other %d; goal kinds roam %d attack %d other %d", n1, n2, n3, r, k, o); }\n'],
]);
console.log('ok');
