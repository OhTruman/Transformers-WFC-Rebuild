// Weapon batch, part 1: per-weapon skeletal recoil. Each shot restarts the pawn's spine / right-hand recoil controls with the FIRING weapon's
// SpineRecoil / RightHandRecoil (TnWeaponMesh archetype overlaid with the weapon mesh's authored members; WeaponRecoilTable.inc from
// tools/gameplay/gen_weapon_recoil.py, RE weapon presentation audit). Ours used IonBlaster's for every weapon. A weapon without authored
// recoil (vehicle weapons, grenades, melee) uses the archetype row "". Sim change (shot origins leave the hand sockets): Classic re-baseline.
// Run from a tree root with src/game/WeaponRecoilTable.inc generated.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
if (!fs.existsSync('src/game/WeaponRecoilTable.inc')) throw new Error('generate src/game/WeaponRecoilTable.inc first');
ed('src/game/Recoil.h', [[
    '// Runtime state of one recoil skel-control.',
    '// Per-weapon recoil: the firing weapon\'s resolved {spine, right hand} pair (WeaponRecoilTable.inc); unknown / null id = the archetype.\n' +
    'struct WeaponRecoilDef { const char* id; RecoilDef spine, hand; };\n' +
    'inline const WeaponRecoilDef& weaponRecoil(const char* weaponId) {\n' +
    '    static const WeaponRecoilDef kTable[] = {\n#include "game/WeaponRecoilTable.inc"\n    };\n' +
    '    if (weaponId)\n' +
    '        for (const WeaponRecoilDef& d : kTable) {\n' +
    '            const char* a = d.id; const char* b = weaponId;\n' +
    '            while (*a && *a == *b) { ++a; ++b; }\n' +
    '            if (!*a && !*b) return d;\n' +
    '        }\n' +
    '    return kTable[0];\n' +
    '}\n\n' +
    '// Runtime state of one recoil skel-control.'
]]);
ed('src/game/Character.h', [[
    '    void notifyFired() {   // deterministic per pawn and shot (the recoil phase reaches the simulation through hand-bone sockets)\n' +
    '        const unsigned s = (++recoilShots_ * 2654435761u) ^ (unsigned)(uintptr_t)stepCounter_ * 0x85ebca6bu ^ 0x9e3779b9u;\n',
    '    void notifyFired(const char* weaponId) {   // deterministic per pawn and shot (the recoil phase reaches the simulation through hand-bone sockets)\n' +
    '        const WeaponRecoilDef& r = weaponRecoil(weaponId);   // the firing weapon\'s SpineRecoil / RightHandRecoil\n' +
    '        recoilSpine_.def = r.spine; recoilHand_.def = r.hand;\n' +
    '        const unsigned s = (++recoilShots_ * 2654435761u) ^ (unsigned)(uintptr_t)stepCounter_ * 0x85ebca6bu ^ 0x9e3779b9u;\n'
]]);
ed('src/game/PlayerController.cpp', [[
    '            if (!noRecoil) pawn_->notifyFired();   // per-shot skeletal recoil (TnRecoiler)\n',
    '            if (!noRecoil) pawn_->notifyFired(w.def ? w.def->id : nullptr);   // per-shot skeletal recoil (TnRecoiler), the weapon\'s values\n'
]]);
ed('src/game/WorldBots.cpp', [[
    '    pc.notifyFired();\n',
    '    pc.notifyFired(w.def ? w.def->id : nullptr);\n'
]]);
console.log('ok');
