// Playtest 2026-10-23 #18 (B) "bots float": a bot Dodge (PHYS_Flying, no gravity, DodgeTime 0.5 s) carried the bot flat over a drop.
// RE (TnAcrobaticsManager / TnAiActionDodge*, C): the flat dodge is original for everyone; original AI dodges are nav-limited:
// - TnAiActionDodgePlanningUtil.GetDodgeDirection: left / right, random first;
// - CheckDodgeAvailable: AnchorCell.WalkLine(pawn -> pawn + dir x DodgeTraceDistance 1800 UU, radius), clear = walked 2D / 1800,
//   needs >= DodgeTraceMinimumThreshold 0.3 (neither side: no dodge);
// - MaxDodgeDistance = clear x 1800 - the collision radius: Dodging ends when exceeded (HasExceededMaxDodgeDistance), and with a
//   MaxDodgeDistance set EndState zeroes the horizontal velocity (the AI stops dead).
// Human dodges are unchanged (no cap). Sim change for bot dodges only. Run from a tree root (integration 4bcfde3d+).
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
    '    float dodgeRemain_ = 0.0f;\n',
    '    float dodgeRemain_ = 0.0f;\n' +
    '    // AI dodges (TnAiActionDodge*): MaxDodgeDistance from the nav WalkLine (0 = none: a human dodge); armed by the bot, taken at the\n' +
    '    // dodge start; the start position for HasExceededMaxDodgeDistance.\n' +
    '    float pendingMaxDodge_ = 0.0f, maxDodgeDist_ = 0.0f; core::Vec3 dodgeStart_{0, 0, 0};\n'
]]);
ed('src/game/CharacterMovement.cpp', [
    ['        v = d * RPd.dodgeSpeed;\n        c.dodgeRemain_ = RPd.dodgeTime;\n',
     '        v = d * RPd.dodgeSpeed;\n        c.dodgeRemain_ = RPd.dodgeTime;\n' +
     '        c.maxDodgeDist_ = c.pendingMaxDodge_; c.pendingMaxDodge_ = 0.0f; c.dodgeStart_ = c.position();   // AI dodge cap (0: none)\n'],
    ['    } else if (dodging) {\n        c.dodgeRemain_ -= dt;',
     '    } else if (dodging) {\n' +
     '        if (c.maxDodgeDist_ > 0.0f) {   // HasExceededMaxDodgeDistance (AI dodges)\n' +
     '            const core::Vec3 dd = c.position() - c.dodgeStart_;\n' +
     '            if (std::sqrt(dd.x * dd.x + dd.z * dd.z) >= c.maxDodgeDist_) c.dodgeRemain_ = 0.0f;\n' +
     '        }\n' +
     '        c.dodgeRemain_ -= dt;'],
    ['        float l = core::length(h);\n        if (l > cap && l > 1e-4f) h = h * (cap / l);\n        v = core::Vec3{h.x, 0.0f, h.z};\n    }\n',
     '        float l = core::length(h);\n        if (l > cap && l > 1e-4f) h = h * (cap / l);\n        v = core::Vec3{h.x, 0.0f, h.z};\n' +
     '        if (c.maxDodgeDist_ > 0.0f) { v = core::Vec3{0, 0, 0}; c.maxDodgeDist_ = 0.0f; }   // an AI dodge stops dead (EndState)\n    }\n'],
]);
ed('src/game/World.h', [[
    '    bool ensureBotNav();\n',
    '    bool ensureBotNav();\n' +
    '    // TnAiActionDodgePlanningUtil.CheckDodgeAvailable: the walkable fraction of 18 m along the pawn\'s left (1) / right (2) on the nav.\n' +
    '    float botDodgeClear(const Character& pc, int side) const;\n'
]]);
ed('src/game/WorldBots.cpp', [
    ['        } else if (a.id == "Dodge") b.pendingDodge = b.frand() < 0.5f ? 1 : 2;\n',
     '        } else if (a.id == "Dodge") {\n' +
     '            // TnAiActionDodgePlanningUtil [CONF RE playtest #18]: left / right (random first) with the nav WalkLine clear >= 0.3;\n' +
     '            // MaxDodgeDistance = clear x 18 m - the collision radius; neither side clear: no dodge (no cooldown).\n' +
     '            const int first = b.frand() < 0.5f ? 1 : 2;\n' +
     '            int side = 0; float clear = 0.0f;\n' +
     '            for (int k = 0; k < 2 && side == 0; ++k) {\n' +
     '                const int s = k == 0 ? first : 3 - first;\n' +
     '                const float cl = botDodgeClear(pc, s);\n' +
     '                if (cl >= 0.3f) { side = s; clear = cl; }\n' +
     '            }\n' +
     '            if (side == 0) return false;\n' +
     '            b.pendingDodge = side;\n' +
     '            pc.pendingMaxDodge_ = std::max(0.01f, clear * 18.0f - pc.cylinderRadius(Form::Robot));\n' +
     '        }\n'],
    ['void World::botPathFailed(BotBrain& b, bool vehicle) {\n',
     'float World::botDodgeClear(const Character& pc, int side) const {\n' +
     '    if (!botNav_.valid()) return 1.0f;   // no nav: the original\'s cap cannot be computed (the dodge runs its time)\n' +
     '    const core::Vec3 X = core::forwardFromYawPitch(pc.yaw(), 0.0f);\n' +
     '    core::Vec3 Y = core::normalize(core::cross(X, core::Vec3{0, 1, 0}));\n' +
     '    if (side == 1) Y = Y * -1.0f;\n' +
     '    BotNav::Agent ag; ag.radius = pc.cylinderRadius(Form::Robot);\n' +
     '    const core::Vec3 p = pc.position();\n' +
     '    if (botNav_.directWalkable(p, p + Y * 18.0f, ag)) return 1.0f;\n' +
     '    float lo = 0.0f, hi = 18.0f;   // the WalkLine stop point: the longest walkable prefix\n' +
     '    for (int i = 0; i < 8; ++i) { const float m = 0.5f * (lo + hi); if (botNav_.directWalkable(p, p + Y * m, ag)) lo = m; else hi = m; }\n' +
     '    return lo / 18.0f;\n' +
     '}\n\n' +
     'void World::botPathFailed(BotBrain& b, bool vehicle) {\n'],
]);
console.log('ok');
