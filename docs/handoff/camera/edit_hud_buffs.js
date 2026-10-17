// HUD buff list for Frontend's jam / disrupt display (RE PRESENTATION_AUDIT_2_RE.md part 1: Hud_GFX _global.ApplyBuff / UnapplyBuff drive
// AbilityType0/1Jammed for TnBuffAbilityJammed / TnBuffAbilityJammedKillstreak / TnBuffDifficultyJamAbilitiesRevived and TransformDisrupted
// for TnBuffTransformDisruptor). World::HudState gains `buffs` {id = the buff class name, remain = seconds left} for the local pawn.
// Character's jam timer is shared by the AbilityJammer projectile (TnBuffAbilityJammed, 15 s) and the killstreak (TnBuffAbilityJammedKillstreak,
// 30 s): applyJammed records which set the active timer. The revive jam (TnBuffDifficultyJamAbilitiesRevived) is campaign-only: not in MP.
// Presentation (HUD state only). Run from a tree root.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/Character.h', [
    ['    void applyJammed(float t) {\n        jammedRemain_ = std::max(jammedRemain_, t);\n',
     '    bool jammedByKillstreak_ = false;     // the active jam timer came from TnBuffAbilityJammedKillstreak (HUD buff id)\n' +
     '    void applyJammed(float t, bool killstreak = false) {\n' +
     '        if (t >= jammedRemain_) jammedByKillstreak_ = killstreak;\n' +
     '        jammedRemain_ = std::max(jammedRemain_, t);\n'],
]);
ed('src/game/World.h', [[
    '    float seeEnemies = 0.0f, refillOnKill = 0.0f, abilitiesJammed = 0.0f, hardLocked = 0.0f;   // killstreak buffs on the local pawn (s left)\n',
    '    float seeEnemies = 0.0f, refillOnKill = 0.0f, abilitiesJammed = 0.0f, hardLocked = 0.0f;   // killstreak buffs on the local pawn (s left)\n' +
    '    // Active HUD-relevant buffs on the local pawn (Hud_GFX ApplyBuff / UnapplyBuff): the buff class name and its seconds left.\n' +
    '    struct HudBuff { std::string id; float remain = 0.0f; };\n' +
    '    std::vector<HudBuff> buffs;\n'
]]);
ed('src/game/World.cpp', [
    ['        otherTeam([](int, Character& c) { c.applyJammed(30.0f); });   // ABT_OtherTeam TnBuffAbilityJammedKillstreak 30 s\n',
     '        otherTeam([](int, Character& c) { c.applyJammed(30.0f, true); });   // ABT_OtherTeam TnBuffAbilityJammedKillstreak 30 s\n'],
    ['    h.seeEnemies = pc.seeEnemiesRemain_; h.refillOnKill = pc.refillOnKillRemain_; h.abilitiesJammed = pc.jammedRemain_; h.hardLocked = pc.hardLockedRemain_;\n',
     '    h.seeEnemies = pc.seeEnemiesRemain_; h.refillOnKill = pc.refillOnKillRemain_; h.abilitiesJammed = pc.jammedRemain_; h.hardLocked = pc.hardLockedRemain_;\n' +
     '    h.buffs.clear();\n' +
     '    if (!localDead_ && pc.jammedRemain_ > 0.0f) h.buffs.push_back({pc.jammedByKillstreak_ ? "TnBuffAbilityJammedKillstreak" : "TnBuffAbilityJammed", pc.jammedRemain_});\n' +
     '    if (!localDead_ && pc.transformDisruptRemain_ > 0.0f) h.buffs.push_back({"TnBuffTransformDisruptor", pc.transformDisruptRemain_});\n'],
]);
console.log('ok');
