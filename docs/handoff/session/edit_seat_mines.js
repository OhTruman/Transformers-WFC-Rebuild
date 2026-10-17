// Thermo Mine Re-Spawner (MinePooperStreak) per seat (an online-local-only path): online the killstreak works for every human seat (it
// ran only when the trigger's player was this machine's: a seat's streak spawned mines on its own peer only); the mines carry their owner,
// spawn from every seat with the buff (match player order), seek enemies of their owner by match player index, damage as their owner and
// die with their owner. Offline (the local player only): the same loop and order as before. Run after edit_seat_homing.js.
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
    '    struct KamikazeMine { core::Vec3 pos, vel; float t = 0.0f, health = 50.0f; int target = -1; int audioKey = 0; };   // [Systems M08p] audioKey\n',
    '    struct KamikazeMine { core::Vec3 pos, vel; float t = 0.0f, health = 50.0f; int target = -1; int audioKey = 0; int owner = -1; };   // [Systems M08p] audioKey; owner (online seats; -1 = the local player)\n'
]]);
ed('src/game/World.cpp', [
    ['    if (player != localPlayer_ && (id == "GuidedMissileStreak" || id == "MinePooperStreak")) return "";\n',
     '    if (player != localPlayer_ && (id == "GuidedMissileStreak" || (id == "MinePooperStreak" && !(onlineSeats_ && seatHuman(player))))) return "";   // online: every seat\n'],
    // The spawn: every seat with the buff online (by match player index), the local player offline.
    ['    if (pc.minePooperRemain_ > 0.0f && !localDead_) {\n' +
     '        pc.minePooperRemain_ -= dt; pc.minePooperTimer_ -= dt;\n' +
     '        if (pc.minePooperTimer_ <= 0.0f) {\n' +
     '            pc.minePooperTimer_ += 2.0f;\n' +
     '            const core::Vec3 f = core::forwardFromYawPitch(pc.yaw(), 0.0f), r = core::normalize(core::cross(f, core::Vec3{0, 1, 0}));\n' +
     '            KamikazeMine m; m.pos = pc.actorLocation() + f * 4.0f + r * 1.0f; m.vel = {0, 0, 0};\n',
     '    auto pooper = [&](Character& pc, int owner) {\n' +
     '    if (pc.minePooperRemain_ > 0.0f) {\n' +
     '        pc.minePooperRemain_ -= dt; pc.minePooperTimer_ -= dt;\n' +
     '        if (pc.minePooperTimer_ <= 0.0f) {\n' +
     '            pc.minePooperTimer_ += 2.0f;\n' +
     '            const core::Vec3 f = core::forwardFromYawPitch(pc.yaw(), 0.0f), r = core::normalize(core::cross(f, core::Vec3{0, 1, 0}));\n' +
     '            KamikazeMine m; m.pos = pc.actorLocation() + f * 4.0f + r * 1.0f; m.vel = {0, 0, 0};\n' +
     '            m.owner = onlineSeats_ ? owner : -1;\n'],
    ['                if (renderer_) { const core::Vec3 ff = f, uu{0, 1, 0}; mp.fx = fxStepSpawn("FX_KamikazeMine_p.FX.Trail_KamikazeMine_FX", m.pos, ff, uu, fxTeamOf(localPlayer_), false); }\n' +
     '            }\n' +
     '        }\n' +
     '    }\n',
     '                if (renderer_) { const core::Vec3 ff = f, uu{0, 1, 0}; mp.fx = fxStepSpawn("FX_KamikazeMine_p.FX.Trail_KamikazeMine_FX", m.pos, ff, uu, fxTeamOf(owner), false); }\n' +
     '            }\n' +
     '        }\n' +
     '    }\n' +
     '    };\n' +
     '    if (onlineSeats_) { for (size_t q = 0; q < match_.players().size(); ++q) if (seatHuman((int)q)) if (Character* oc = participantPawnMutable((int)q)) pooper(*oc, (int)q); }\n' +
     '    else if (!localDead_) pooper(pc, localPlayer_);\n'],
    // The tick: owner-relative (online by match player index).
    ['        bool boom = false, gone = m.t >= 60.0f || m.health <= 0.0f || localDead_;   // LifeSpan 60; destroyed with the owner [PROV]\n',
     '        const int mo = m.owner >= 0 ? m.owner : localPlayer_;\n' +
     '        bool boom = false, gone = m.t >= 60.0f || m.health <= 0.0f || (m.owner >= 0 ? participantPawn(mo) == nullptr : localDead_);   // LifeSpan 60; destroyed with the owner [PROV]\n'],
    ['            const MatchOpponent* tgt = nullptr; float best = 20.0f;\n' +
     '            if (matchActive_)\n' +
     '                for (const MatchOpponent* o : opponents_) {\n' +
     '                    if (!o->spawned() || match_.sameTeam(o->matchPlayer(), localPlayer_)) continue;\n' +
     '                    float d = core::length(o->pawn().actorLocation() - m.pos), tt;\n' +
     '                    if (d < best && !(line && line->segmentHit(m.pos, o->pawn().actorLocation(), tt))) { best = d; tgt = o; }\n' +
     '                }\n' +
     '            if (tgt) {\n' +
     '                m.vel = core::normalize(tgt->pawn().actorLocation() - m.pos) * 23.0f;\n' +
     '                const Character& e = tgt->pawn();\n',
     '            const Character* tgt = nullptr; float best = 20.0f;\n' +
     '            if (matchActive_ && m.owner >= 0) {   // online: the owner\'s enemies by match player index\n' +
     '                for (size_t q = 0; q < match_.players().size(); ++q) {\n' +
     '                    const Character* c = (int)q == mo || match_.sameTeam((int)q, mo) ? nullptr : participantPawn((int)q);\n' +
     '                    float tt;\n' +
     '                    if (!c) continue;\n' +
     '                    const float d = core::length(c->actorLocation() - m.pos);\n' +
     '                    if (d < best && !(line && line->segmentHit(m.pos, c->actorLocation(), tt))) { best = d; tgt = c; }\n' +
     '                }\n' +
     '            } else if (matchActive_)\n' +
     '                for (const MatchOpponent* o : opponents_) {\n' +
     '                    if (!o->spawned() || match_.sameTeam(o->matchPlayer(), localPlayer_)) continue;\n' +
     '                    float d = core::length(o->pawn().actorLocation() - m.pos), tt;\n' +
     '                    if (d < best && !(line && line->segmentHit(m.pos, o->pawn().actorLocation(), tt))) { best = d; tgt = &o->pawn(); }\n' +
     '                }\n' +
     '            if (tgt) {\n' +
     '                m.vel = core::normalize(tgt->actorLocation() - m.pos) * 23.0f;\n' +
     '                const Character& e = *tgt;\n'],
    ['        if (boom) radiusDamage(m.pos, 125.0f, 5.0f, localPlayer_, "TransGame.TnDamageTypeKamikazeMine");\n',
     '        if (boom) radiusDamage(m.pos, 125.0f, 5.0f, mo, "TransGame.TnDamageTypeKamikazeMine");\n'],
]);
ed('src/core/Application.cpp', [[
    '    // The online local seat through the wire path: two peers with the same seed and the same local command stream reach an equal state.\n',
    '    partialCase("remote Thermo Mine Re-Spawner: the seat\'s mines spawn and seek", game::Specialty::Soldier, {"AssaultRifle", "HomingRocket", "FlakGrenades"}, {"Hover", "Cloaking"},\n' +
    '        [&](int q, game::MatchOpponent*, game::RemoteCommand& cmd, const std::function<void()>& stepCmd, const std::function<void()>& aimAt) {\n' +
    '            world_.match().playerMutable(q).acquiredKillstreaks.push_back("MinePooperStreak");\n' +
    '            aimAt(); cmd.pressed = 1u << (int)platform::Button::Killstreak; stepCmd();\n' +
    '            size_t most = 0;\n' +
    '            for (int i = 0; i < 300; ++i) { aimAt(); stepCmd(); most = std::max(most, world_.kamikazeMines().size()); }\n' +
    '            int owned = 0; for (const auto& m : world_.kamikazeMines()) owned += m.owner == q;\n' +
    '            LOG_INFO("SESSIONTEST mines: up to %zu live, %d owned by the seat now", most, owned);\n' +
    '            return world_.match().players()[(size_t)q].acquiredKillstreaks.empty() && most >= 2;\n' +
    '        });\n' +
    '    // The online local seat through the wire path: two peers with the same seed and the same local command stream reach an equal state.\n'
]]);
console.log('ok');
