// Escalation E3b part 1 (on NAV3): the Brute's backpack - the attached RB_Brute*_Backpack_DES as its only kill path (the chassis' 100,000
// health), the 25 / 50 / 75 state chain from behind (DamageAngle 100), AI x0 / melee x0.5 / radius x0.25, the critical 3 s, the 500 / 40 m
// explosion by the last hitter and the Brute's death by TnDamageTypeBruteBackpack [C, RE ESCALATION_HORDE_AI sections 2 / 6].
// WorldSurvival.cpp / Survival.h / Application_EscTest.cpp copied from argv[2].
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
    ['    bool svSpawn(int slot);\n',
     '    bool svSpawn(int slot);\n' +
     '    bool svBackpackHit(int victim, int instigator, float amount, const std::string& damageType);   // E3b: the Brute backpack took it\n' +
     '    void svBackpackExplode(SurvivalState::Slot& s);\n' +
     '    bool svRadiusHit_ = false;     // inside radiusDamage (the backpack\'s radius x0.25)\n' +
     '    bool svKillingBrute_ = false;  // TnKillAttachedPawn in progress (bypasses the backpack test)\n'],
    ['    const SurvivalData& survivalData() const { return svData_; }\n',
     '    const SurvivalData& survivalData() const { return svData_; }\n' +
     '    int svTestSpawn(const std::string& typePrefix);   // WFC_ESCTEST: spawn one horde entry of that type now (its match player, or -1)\n'],
]);
ed('src/game/World.cpp', [
    ['    if (instigator != victim && match_.sameTeam(instigator, victim) && !aoe) return false;\n',
     '    if (instigator != victim && match_.sameTeam(instigator, victim) && !aoe) return false;\n' +
     '    // Escalation E3b: a hit on a Brute\'s backpack (an attached destructible) never reaches the pawn\n' +
     '    if (sv_.phase != SurvivalState::Phase::Off && !svKillingBrute_ && svBackpackHit(victim, instigator, amount, damageType)) return false;\n'],
    ['void World::radiusDamage(const core::Vec3& at, float damage, float radius, int instigator, const std::string& type) {\n',
     'void World::radiusDamage(const core::Vec3& at, float damage, float radius, int instigator, const std::string& type) {\n' +
     '    struct RadiusFlag { bool& f; bool was; ~RadiusFlag() { f = was; } } radiusFlag{svRadiusHit_, svRadiusHit_};\n' +
     '    svRadiusHit_ = true;\n'],
]);
console.log('ok');
