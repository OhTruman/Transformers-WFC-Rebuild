// NET.identity follow-up 4 (Systems' tick-1299 split: a seat's tank shell spawned 0.2 mm apart by perspective, so its splash on another
// seat differed by 0.003 hp): online, the local pawn still took the offline per-step animation update while dead / not yet spawned
// (the condition was !(onlineSeats_ && !localDead_)), so its pose (animation phases, the tank cannon lag) reached each spawn advanced
// differently from the same seat's remote pawn, which animates only while spawned. Online the local seat animates only in the
// participants' pool, like every seat. Offline unchanged. Run from a tree root with GAMEPLAY_SEAT_ORDER.
const fs = require('fs');
const f = 'src/game/World.cpp';
let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
const a = '    if (!(onlineSeats_ && !localDead_)) { sysprof::Scope sp(sysprof::Anim); player_.pawn().updateAnimation(dt); }   // online: in the pool\n';
if (s.split(a).length !== 2) throw new Error('anchor');
s = s.replace(a, '    if (!onlineSeats_) { sysprof::Scope sp(sysprof::Anim); player_.pawn().updateAnimation(dt); }   // online: in the pool while spawned (as every seat)\n');
fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
console.log('ok');
