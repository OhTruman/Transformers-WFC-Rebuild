// WFC_STUCKLOG=1 (DEV / TEST, log only): one line per bot stuck event (the steering's stuck rule) with the position, nav cell, level and
// form, for locating where bots wedge on a map (AssetTools collision / nav work). Unset = unchanged. Run from a tree root.
const fs = require('fs');
const f = 'src/game/WorldBots.cpp';
let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
const a = '                    ++b.stuckLevel; ++b.stucks;\n';
if (s.split(a).length !== 2) throw new Error('anchor');
s = s.replace(a, a + '                    { static const bool sl = std::getenv("WFC_STUCKLOG") != nullptr;\n' +
    '                      if (sl) LOG_INFO("STUCK p%d t %.1f at (%.2f %.2f %.2f) cell %d level %d %s", b.player, match_.matchTime(), pos.x, pos.y, pos.z, botNav_.findCell(pos, 2.0f), b.stuckLevel, vehicle ? "vehicle" : "robot"); }\n');
fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
console.log('ok');
