// Weapon batch, part 2: the per-chassis player melee sets (RE PRESENTATION_AUDIT_2_RE.md part 2; notes/data/melee_presentation_audit.json).
// Ours used one generic set (HeavyMedium / LightSmall values) for every chassis. The values that differ and are reachable in Versus:
// - LightMediumMeleeSet (Jet, Jet2-4, Jet6, Jet8, Car5, Truck2-4, Truck6): WeaponAttack (Any / Turret / Club) Impulse 20000 (ours 30000).
// - LightMediumMeleeSet Flag / Bomb (MWT_Flag / MWT_Bomb, Melee_Mace): AttackDash 3000 UU/s for 1 s, EndVelocityScaler 0.3 (the other
//   sets: Speed 0, no lunge). The dash goes through the melee-assist lunge (toward an enemy in the picker cone), as WeaponAttack's does.
// Not reachable in Versus (no trigger / weapon in ours): Stomp, Combo, HeavySmall Stampede, the Axe / Mace / Club / Hammer melee weapons.
// [H] a Speed-0 dash with EndVelocityScaler 0 / 1 (Whirlwind "2000 for 0 s (end x0)", HeavyMedium Bomb) is read as no dash.
// Sim change: Classic re-baseline. Run from a tree root.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/Character.h', [[
    '    core::Vec3 lungeDir_{0, 0, 0};\n',
    '    core::Vec3 lungeDir_{0, 0, 0};\n' +
    '    float lungeSpeed_ = 25.0f;   // AttackDash Speed (m/s) of the lunge in progress\n'
]]);
ed('src/game/CharacterMovement.cpp', [
    ['    // Melee AttackDash: yaw-only lunge at Speed 2500 UU/s for Time 0.25 s, then Velocity x EndVelocityScaler 0.3 [CONF].\n',
     '    // Melee AttackDash: yaw-only lunge at Speed (2500 UU/s; LightMedium flag / bomb 3000) for Time (0.25 s; 1 s), then Velocity x\n' +
     '    // EndVelocityScaler 0.3 [CONF].\n'],
    ['        v.x = c.lungeDir_.x * 25.0f; v.z = c.lungeDir_.z * 25.0f;\n',
     '        v.x = c.lungeDir_.x * c.lungeSpeed_; v.z = c.lungeDir_.z * c.lungeSpeed_;\n'],
]);
ed('src/game/World.cpp', [
    ['void World::startLocalMelee(bool whirlwind) {\n',
     '// The chassis\' player TnMeleeSet (CharacterData.MeleeSet): 0 HeavyMedium, 1 HeavySmall, 2 LightMedium, 3 LightSmall, -1 unlisted.\n' +
     'static int meleeSetOf(const char* chassisId) {\n' +
     '    static const struct { const char* id; int set; } kSets[] = {\n' +
     '        {"Tank", 0}, {"Truck", 0}, {"Truck7", 0}, {"Jet5", 0}, {"Jet7", 0}, {"Tank2", 0}, {"Tank3", 0}, {"Tank4", 0}, {"Tank5", 0}, {"Truck5", 0},\n' +
     '        {"Minion2", 1},\n' +
     '        {"Jet", 2}, {"Car5", 2}, {"Jet2", 2}, {"Jet3", 2}, {"Jet4", 2}, {"Jet6", 2}, {"Jet8", 2}, {"Truck2", 2}, {"Truck3", 2}, {"Truck4", 2}, {"Truck6", 2},\n' +
     '        {"Car", 3}, {"Car2", 3}, {"Car3", 3}, {"Car4", 3}, {"Car6", 3}, {"Car7", 3}};\n' +
     '    if (chassisId) for (const auto& s : kSets) if (std::strcmp(s.id, chassisId) == 0) return s.set;\n' +
     '    return -1;\n' +
     '}\n\n' +
     'void World::startLocalMelee(bool whirlwind) {\n'],
    ['        // FindAttack: current weapon MeleeWeaponType MWT_Flag / MWT_Bomb -> Melee_Mace (chooser _01/_02/_03), 9999, AttackDash\n' +
     '        // Speed 0 (no lunge), GroundSpeedMultiplier 0.75 [CONF TnMeleeSet]. Clip lengths / sweep times from the anim set when\n' +
     '        // the robot export lacks the clip.\n',
     '        // FindAttack: current weapon MeleeWeaponType MWT_Flag / MWT_Bomb -> Melee_Mace (chooser _01/_02/_03), 9999, AttackDash\n' +
     '        // Speed 0 (no lunge; LightMedium: 3000 for 1 s), GroundSpeedMultiplier 0.75 [CONF TnMeleeSet]. Clip lengths / sweep times\n' +
     '        // from the anim set when the robot export lacks the clip.\n'],
    ['        if (const assets::SkinnedModel* m = pc.currentModel()) { int ci = m->clipByName(kMace[k]); if (ci >= 0) pc.meleeLen_ = m->clips[(size_t)ci].duration; }\n' +
     '        return;\n' +
     '    }\n',
     '        if (const assets::SkinnedModel* m = pc.currentModel()) { int ci = m->clipByName(kMace[k]); if (ci >= 0) pc.meleeLen_ = m->clips[(size_t)ci].duration; }\n' +
     '        if (meleeSetOf(pc.chassis().id.c_str()) != 2) return;   // only LightMedium\'s flag / bomb attacks dash\n' +
     '    }\n'],
    ['    if (pc.meleePoke_) {\n        // fall through to the assist lunge\n    } else if (whirlwind) {\n',
     '    if (pc.meleePoke_ || pc.meleeCarrier_) {\n        // fall through to the assist lunge\n    } else if (whirlwind) {\n'],
    ['    if (!pc.meleePoke_) {\n    pc.playAction(clip, false);\n',
     '    if (!pc.meleePoke_ && !pc.meleeCarrier_) {\n    pc.playAction(clip, false);\n'],
    ['    if (best < 1e9f) { pc.lungeRemain_ = 0.25f; pc.setYaw(std::atan2(-pc.lungeDir_.x, -pc.lungeDir_.z)); }\n',
     '    if (best < 1e9f) {\n' +
     '        pc.lungeRemain_ = pc.meleeCarrier_ ? 1.0f : 0.25f; pc.lungeSpeed_ = pc.meleeCarrier_ ? 30.0f : 25.0f;   // AttackDash Time / Speed\n' +
     '        pc.setYaw(std::atan2(-pc.lungeDir_.x, -pc.lungeDir_.z));\n' +
     '    }\n'],
    ['            // Momentum = normal(victim - attacker) x Impulse (WeaponAttack 30000, flag / bomb 80000, Whirlwind 2000) [CONF].\n' +
     '            const float impulse = whirl ? 2000.0f : pc.meleeCarrier_ ? 80000.0f : pc.meleePoke_ ? 200000.0f : 30000.0f;\n',
     '            // Momentum = normal(victim - attacker) x Impulse (WeaponAttack 30000 / LightMedium 20000, flag / bomb 80000, Whirlwind 2000) [CONF].\n' +
     '            const float impulse = whirl ? 2000.0f : pc.meleeCarrier_ ? 80000.0f : pc.meleePoke_ ? 200000.0f\n' +
     '                                : meleeSetOf(pc.chassis().id.c_str()) == 2 ? 20000.0f : 30000.0f;\n'],
]);
console.log('ok');
