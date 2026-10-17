// Domination node marker: TnObjectiveMarkerTypeDomination sets the material scalar NodeID = TnDominationPoint.PointNumber on
// UI_HudMarkers_p.MPMarkerDominationNode_MAT (RE RENDERING_TIME_MATERIALS_LIGHTS_RE.md item 3, CONFIRMED script); ours only set the label.
// Presentation only. Run from a tree root.
const fs = require('fs');
const f = 'src/game/World.cpp';
let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
const a = '                m.label = std::to_string(ob.pointNumber);                     // NodeID\n';
if (s.split(a).length !== 2) throw new Error('anchor');
s = s.replace(a, a + '                param(m, "NodeID", (float)ob.pointNumber);                   // the marker material\'s point number\n');
fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
console.log('ok');
