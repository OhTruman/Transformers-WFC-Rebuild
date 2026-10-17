// HUD state: the spectate target's display name for Frontend (RE audit 3: MultiplayerRespawn_GFX _global.SetSpectateTarget(name) fades the
// "currently spectating <name>" label in; "" fades it out). World::HudState.spectateTarget = the match name of the pawn the killer cam
// (PlayerSpectating on SpectateOverTheShoulder) is following; "" otherwise (alive, the death camera on the own corpse, free view).
// Presentation (HUD state only). Run from a tree root after edit_hud_buffs.js.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/World.h', [[
    '    std::vector<HudBuff> buffs;\n',
    '    std::vector<HudBuff> buffs;\n' +
    '    std::string spectateTarget;   // the killer cam\'s target (SetSpectateTarget); "" when following no one\n'
]]);
ed('src/game/World.cpp', [[
    '    h.buffs.clear();\n',
    '    h.buffs.clear();\n' +
    '    h.spectateTarget.clear();\n' +
    '    if (localDead_ && player_.controller().spectatingTarget() && specTarget_ >= 0 && (size_t)specTarget_ < match_.players().size())\n' +
    '        h.spectateTarget = match_.players()[(size_t)specTarget_].name;\n'
]]);
console.log('ok');
