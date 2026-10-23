// Escalation E2b part 1: BrokenHope's Wall_Crashers map Kismet run as data (crash walls, round gates, the terminal window; RE E2b
// answers 45a2a08) and the ESCTEST checks for it and for the item generator's Taken path (G1). WorldSurvival.cpp / Survival.h /
// EscalationData.inc / Application_EscTest.cpp are copied in from the Gameplay worktree (argv[2]). Run from a tree root (29f253cd+).
const fs = require('fs');
const path = require('path');
const G = process.argv[2] || path.resolve(__dirname, '..', '..', '..');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
for (const [src, dst] of [['docs/handoff/escalation/Survival.h', 'src/game/Survival.h'], ['docs/handoff/escalation/WorldSurvival.cpp', 'src/game/WorldSurvival.cpp'],
                          ['src/game/EscalationData.inc', 'src/game/EscalationData.inc'], ['docs/handoff/escalation/Application_EscTest.cpp', 'src/core/Application_EscTest.cpp']])
    fs.copyFileSync(path.join(G, src), dst);
ed('src/game/World.h', [
    ['    void svFireGenerator(int generator, bool levelDrop = false);\n',
     '    void svFireGenerator(int generator, bool levelDrop = false);\n    void svTickKismet();   // Wall_Crashers (map Kismet as data)\n' +
     '    // Escalation weapons (E2b part 2): the SV spawn loadout, the pickup / dropped weapon take rules, the factories and drops.\n' +
     '    Character* svMutablePawn(int player);\n    void svApplySpawnLoadout(Character& c);\n' +
     '    bool svTakeWeapon(Character& c, int player, const Weapon& got);\n    void svTickWeaponPickups(float dt);\n'],
]);
ed('src/game/Character.h', [
    ['    Weapon& weapon() { return inventory_[(size_t)activeWeapon_]; }\n',
     '    Weapon& weapon() { return inventory_[(size_t)activeWeapon_]; }\n' +
     '    // Escalation pickups: the robot inventory by index (a temp WT_Heavy weapon, when held, is the last); addWeapon keeps it last.\n' +
     '    int weaponCount() const { return (int)inventory_.size(); }\n' +
     '    Weapon& weaponAt(int i) { return inventory_[(size_t)i]; }\n' +
     '    const Weapon& weaponAt(int i) const { return inventory_[(size_t)i]; }\n' +
     '    void addWeapon(const Weapon& w) { if (tempWeapon_ && !inventory_.empty()) inventory_.insert(inventory_.end() - 1, w); else inventory_.push_back(w); }\n'],
]);
ed('src/game/World.cpp', [
    ['                        loadoutRefused_ = applyLoadout(&mp.selection);\n',
     '                        loadoutRefused_ = applyLoadout(&mp.selection);\n' +
     '                        if (sv_.phase != SurvivalState::Phase::Off) svApplySpawnLoadout(pc);   // SetPlayerDefaults (SV)\n'],
    ['                        if (chassisOk) applyCharacterTo(o->pawn(), &op.selection, &op);\n',
     '                        if (chassisOk) applyCharacterTo(o->pawn(), &op.selection, &op);\n' +
     '                        if (chassisOk && sv_.phase != SurvivalState::Phase::Off && op.kind != ParticipantKind::Bot) svApplySpawnLoadout(o->pawn());\n'],
    ['    if (pc.tempWeapon_ == 2 && (pc.isTransforming()',
     '    if (!match_.settings().survival && pc.tempWeapon_ == 2 && (pc.isTransforming()'],   // Escalation: the director tosses carried heavies
]);
console.log('ok');
