// NET.identity follow-up 3 (the in-process 2400-step launch identity with WFC_PLAYERBOT, split at step 1772): the player bot's
// opportunistic out-of-combat reload (end of botThink) still called beginReload() on the local pawn directly; online it is a Reload press
// like the player bot's other actions. Offline unchanged. Run from a tree root with GAMEPLAY_NET_IDENTITY (pbSandbox_ / pbPress_).
const fs = require('fs');
const f = 'src/game/WorldBots.cpp';
let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
const a = '        if (!visible && cw.ammo < cw.magSize / 2 && cw.canReload()) { cw.beginReload(); ++b.reloads; }\n';
if (s.split(a).length !== 2) throw new Error('anchor');
s = s.replace(a, '        if (!visible && cw.ammo < cw.magSize / 2 && cw.canReload()) { if (pbSandbox_) pbPress_ |= 1u << (int)platform::Button::Reload; else cw.beginReload(); ++b.reloads; }\n');
fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
console.log('ok');
