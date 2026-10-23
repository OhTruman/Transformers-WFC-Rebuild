// Escalation E3b part 2: the Soldier Jet's boosters (125 while hovering, AI x0.5, destroyed -> TnKillAttachedPawn RobotBooster) and a
// PROV HoverFromTactical trigger; the loot table (TnAbilityResourcePickups by the spawn-list row's LootClassification, the boss Tank's rare
// weapon, the per-kill extras, +3 ability resource on touch). WorldSurvival.cpp / Survival.h / Application_EscTest.cpp copied from argv[2].
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
for (const [src, dst] of [['Survival.h', 'src/game/Survival.h'], ['WorldSurvival.cpp', 'src/game/WorldSurvival.cpp'], ['Application_EscTest.cpp', 'src/core/Application_EscTest.cpp']])
    fs.copyFileSync(path.join(G, 'docs/handoff/escalation', src), dst);
ed('src/game/World.h', [
    ['    void svBackpackExplode(SurvivalState::Slot& s);\n',
     '    void svBackpackExplode(SurvivalState::Slot& s);\n    void svDropLoot(const KillFeedEntry& e);   // E3b part 2: the loot table on a horde death\n'],
]);
console.log('ok');
