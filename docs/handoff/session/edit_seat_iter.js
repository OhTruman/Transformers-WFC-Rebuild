// Online perspective audit (Systems' catch, part 3): sim loops that handled the local pawn apart from the opponents (first, last, with a
// different test, or not at all) go by match player index online, the local pawn tested like every other. Offline (no seat order): the
// old loops, unchanged.
// - bots: the Warcry / SpawnAmmoCrate teammate checks (opponents only: a remote teammate counted, the local one did not);
// - objectives: carried-weapon drops and the objective pawn list (pickup contention order); HurtRadius;
// - radiusDamage and Shockwave victims (damage / kill order); grenade bounces (never bounced off the local pawn);
// - projectile hits (the local pawn had an 8-sample cylinder test, the others the ray test); the sentry's target (owner excluded);
// - the WFC_SIMHASH pawn order; the snapshot's fine-aim flag (the local controller's; online the seat command's FineAim bit).
// Run after edit_seat_step.js.
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
    '    void clearAllParticipants();\n',
    '    void clearAllParticipants();\n' +
    '    std::vector<char> seatFineAim_;     // online: each seat\'s last command FineAim bit (the snapshot\'s fine-aim flag)\n'
]]);
ed('src/game/WorldBots.cpp', [
    ['                for (const MatchOpponent* q : opponents_)\n' +
     '                    if (q->matchPlayer() != o.matchPlayer() && q->spawned() && match_.sameTeam(q->matchPlayer(), b.player) && core::length(q->pawn().position() - pc.position()) < 25.0f) mate = true;\n',
     '                if (seatOrdered_) {   // online: every teammate by match player index (the local human included)\n' +
     '                    for (size_t q = 0; q < match_.players().size(); ++q) {\n' +
     '                        const Character* qc = (int)q == o.matchPlayer() ? nullptr : participantPawn((int)q);\n' +
     '                        if (qc && match_.sameTeam((int)q, b.player) && core::length(qc->position() - pc.position()) < 25.0f) mate = true;\n' +
     '                    }\n' +
     '                } else\n' +
     '                for (const MatchOpponent* q : opponents_)\n' +
     '                    if (q->matchPlayer() != o.matchPlayer() && q->spawned() && match_.sameTeam(q->matchPlayer(), b.player) && core::length(q->pawn().position() - pc.position()) < 25.0f) mate = true;\n'],
    ['                for (const MatchOpponent* q : opponents_)\n' +
     '                    if (q->matchPlayer() != o.matchPlayer() && q->spawned() && match_.sameTeam(q->matchPlayer(), b.player) && core::length(q->pawn().position() - pc.position()) < 15.0f) mateNear = true;\n',
     '                if (seatOrdered_) {   // online: every teammate by match player index (the local human included)\n' +
     '                    for (size_t q = 0; q < match_.players().size(); ++q) {\n' +
     '                        const Character* qc = (int)q == o.matchPlayer() ? nullptr : participantPawn((int)q);\n' +
     '                        if (qc && match_.sameTeam((int)q, b.player) && core::length(qc->position() - pc.position()) < 15.0f) mateNear = true;\n' +
     '                    }\n' +
     '                } else\n' +
     '                for (const MatchOpponent* q : opponents_)\n' +
     '                    if (q->matchPlayer() != o.matchPlayer() && q->spawned() && match_.sameTeam(q->matchPlayer(), b.player) && core::length(q->pawn().position() - pc.position()) < 15.0f) mateNear = true;\n'],
]);
ed('src/game/World.cpp', [
    // Objectives: drops and the pawn list.
    ['        if (!localDead_ && (pc.isTransforming() || pc.form() == Form::Vehicle)) mapState_.dropCarriedBy(localPlayer_, pc.actorLocation());\n' +
     '        if (!localDead_ && pc.heavyDropRequested_) mapState_.dropCarriedBy(localPlayer_, pc.actorLocation());\n' +
     '        pc.heavyDropRequested_ = false;\n' +
     '        for (MatchOpponent* o : opponents_)\n' +
     '            if (o->spawned() && (o->pawn().isTransforming() || o->pawn().form() == Form::Vehicle)) mapState_.dropCarriedBy(o->matchPlayer(), o->pawn().actorLocation());\n',
     '        if (seatOrdered_) {   // online: by match player index\n' +
     '            for (size_t q = 0; q < match_.players().size(); ++q) {\n' +
     '                const Character* c = participantPawn((int)q);\n' +
     '                if (!c) continue;\n' +
     '                if (c->isTransforming() || c->form() == Form::Vehicle) mapState_.dropCarriedBy((int)q, c->actorLocation());\n' +
     '                if ((int)q == localPlayer_ && pc.heavyDropRequested_) mapState_.dropCarriedBy((int)q, c->actorLocation());\n' +
     '            }\n' +
     '            pc.heavyDropRequested_ = false;\n' +
     '        } else {\n' +
     '        if (!localDead_ && (pc.isTransforming() || pc.form() == Form::Vehicle)) mapState_.dropCarriedBy(localPlayer_, pc.actorLocation());\n' +
     '        if (!localDead_ && pc.heavyDropRequested_) mapState_.dropCarriedBy(localPlayer_, pc.actorLocation());\n' +
     '        pc.heavyDropRequested_ = false;\n' +
     '        for (MatchOpponent* o : opponents_)\n' +
     '            if (o->spawned() && (o->pawn().isTransforming() || o->pawn().form() == Form::Vehicle)) mapState_.dropCarriedBy(o->matchPlayer(), o->pawn().actorLocation());\n' +
     '        }\n'],
    ['        if (!localDead_) pawns.push_back({localPlayer_, match_.players()[(size_t)localPlayer_].team, pc.actorLocation(), true, robotForm(pc), localPickup});\n' +
     '        for (MatchOpponent* o : opponents_)\n' +
     '            if (o->spawned()) pawns.push_back({o->matchPlayer(), o->team(), o->pawn().actorLocation(), true, robotForm(o->pawn()), o->pressesPickup});\n',
     '        if (seatOrdered_) {   // online: by match player index (pickup contention resolves alike on every peer)\n' +
     '            for (size_t q = 0; q < match_.players().size(); ++q) {\n' +
     '                const Character* c = participantPawn((int)q);\n' +
     '                if (!c) continue;\n' +
     '                const MatchOpponent* o = (int)q == localPlayer_ ? nullptr : opponentByPlayer((int)q);\n' +
     '                pawns.push_back({(int)q, o ? o->team() : match_.players()[q].team, c->actorLocation(), true, robotForm(*c), o ? o->pressesPickup : localPickup});\n' +
     '            }\n' +
     '        } else {\n' +
     '        if (!localDead_) pawns.push_back({localPlayer_, match_.players()[(size_t)localPlayer_].team, pc.actorLocation(), true, robotForm(pc), localPickup});\n' +
     '        for (MatchOpponent* o : opponents_)\n' +
     '            if (o->spawned()) pawns.push_back({o->matchPlayer(), o->team(), o->pawn().actorLocation(), true, robotForm(o->pawn()), o->pressesPickup});\n' +
     '        }\n'],
    // HurtRadius.
    ['            if (!localDead_ && core::length(pc.actorLocation() - rd.pos) <= rd.radius && radiusVisible(rd.pos, pc.actorLocation()))\n' +
     '                { noteHitDir(pc.actorLocation() - rd.pos); applyMatchDamage(localPlayer_, rd.instigator, rd.damage, true, rd.damageType); }\n' +
     '            for (MatchOpponent* o : opponents_)\n',
     '            if (seatOrdered_) {   // online: by match player index\n' +
     '                for (size_t q = 0; q < match_.players().size(); ++q) {\n' +
     '                    const Character* c = participantPawn((int)q);\n' +
     '                    if (c && core::length(c->position() - rd.pos) <= rd.radius && radiusVisible(rd.pos, c->actorLocation()))\n' +
     '                        { noteHitDir(c->actorLocation() - rd.pos); applyMatchDamage((int)q, rd.instigator, rd.damage, true, rd.damageType); }\n' +
     '                }\n' +
     '                continue;\n' +
     '            }\n' +
     '            if (!localDead_ && core::length(pc.actorLocation() - rd.pos) <= rd.radius && radiusVisible(rd.pos, pc.actorLocation()))\n' +
     '                { noteHitDir(pc.actorLocation() - rd.pos); applyMatchDamage(localPlayer_, rd.instigator, rd.damage, true, rd.damageType); }\n' +
     '            for (MatchOpponent* o : opponents_)\n'],
    // radiusDamage.
    ['    if (matchActive_ && !localDead_) {\n' +
     '        float k = falloff(player_.pawn().actorLocation(), colR(player_.pawn()));\n',
     '    if (seatOrdered_ && matchActive_) {   // online: by match player index\n' +
     '        for (size_t q = 0; q < match_.players().size(); ++q) {\n' +
     '            const Character* c = participantPawn((int)q);\n' +
     '            if (!c) continue;\n' +
     '            const float k = falloff(c->actorLocation(), colR(*c));\n' +
     '            if (k > 0.0f && radiusVisible(at, c->actorLocation())) { noteHitDir(c->actorLocation() - at); applyMatchDamage((int)q, instigator, damage * k, false, type); }\n' +
     '        }\n' +
     '    } else {\n' +
     '    if (matchActive_ && !localDead_) {\n' +
     '        float k = falloff(player_.pawn().actorLocation(), colR(player_.pawn()));\n'],
    ['        if (k > 0.0f && radiusVisible(at, o->pawn().actorLocation())) { noteHitDir(o->pawn().actorLocation() - at); applyMatchDamage(o->matchPlayer(), instigator, damage * k, false, type); }\n    }\n',
     '        if (k > 0.0f && radiusVisible(at, o->pawn().actorLocation())) { noteHitDir(o->pawn().actorLocation() - at); applyMatchDamage(o->matchPlayer(), instigator, damage * k, false, type); }\n    }\n    }\n'],
    // Grenade bounces.
    ['                if (sl > 1e-5f)\n' +
     '                    for (MatchOpponent* o : opponents_) {\n',
     '                if (sl > 1e-5f && seatOrdered_) {   // online: every pawn but the instigator\'s, by match player index\n' +
     '                    for (size_t q = 0; q < match_.players().size() && !hitPawn; ++q) {\n' +
     '                        const Character* c = (int)q == p.instigator ? nullptr : participantPawn((int)q);\n' +
     '                        float th;\n' +
     '                        if (c && MatchOpponent::pawnRayHit(*c, p.pos, seg * (1.0f / sl), sl, th)) hitPawn = true;\n' +
     '                    }\n' +
     '                } else\n' +
     '                if (sl > 1e-5f)\n' +
     '                    for (MatchOpponent* o : opponents_) {\n'],
    // Projectile hits.
    ['            core::Vec3 dir = d * (1.0f / len);\n' +
     '            for (MatchOpponent* o : opponents_) {\n' +
     '                if (o->matchPlayer() == p.instigator) continue;\n',
     '            core::Vec3 dir = d * (1.0f / len);\n' +
     '            if (seatOrdered_) {   // online: every pawn but the instigator\'s, one test, by match player index\n' +
     '                for (size_t q = 0; q < match_.players().size(); ++q) {\n' +
     '                    const Character* c = (int)q == p.instigator ? nullptr : participantPawn((int)q);\n' +
     '                    float th;\n' +
     '                    if (c && MatchOpponent::pawnRayHit(*c, p.pos, dir, len, th) && th / len < best) { best = th / len; hit = true; worldHit = false; }\n' +
     '                }\n' +
     '            } else {\n' +
     '            for (MatchOpponent* o : opponents_) {\n' +
     '                if (o->matchPlayer() == p.instigator) continue;\n'],
    ['                    if (std::hypot(q.x - c.x, q.z - c.z) <= r && std::fabs(q.y - c.y) <= hh && k / 8.0f < best) { best = k / 8.0f; hit = true; worldHit = false; break; }\n' +
     '                }\n' +
     '            }\n',
     '                    if (std::hypot(q.x - c.x, q.z - c.z) <= r && std::fabs(q.y - c.y) <= hh && k / 8.0f < best) { best = k / 8.0f; hit = true; worldHit = false; break; }\n' +
     '                }\n' +
     '            }\n' +
     '            }\n'],
    // The sentry's target.
    ['                for (MatchOpponent* o : opponents_) { float th; if (o->rayHit(muzzle, dir, wall, th) && th < hd) { hd = th; hit = o->matchPlayer(); } }\n' +
     '                if (!localDead_ && localPlayer_ != s.owner) {',
     '                if (seatOrdered_) {   // online: by match player index, the owner excluded\n' +
     '                    for (size_t q = 0; q < match_.players().size(); ++q) {\n' +
     '                        const Character* c = (int)q == s.owner ? nullptr : participantPawn((int)q);\n' +
     '                        float th;\n' +
     '                        if (c && MatchOpponent::pawnRayHit(*c, muzzle, dir, wall, th) && th < hd) { hd = th; hit = (int)q; }\n' +
     '                    }\n' +
     '                } else\n' +
     '                for (MatchOpponent* o : opponents_) { float th; if (o->rayHit(muzzle, dir, wall, th) && th < hd) { hd = th; hit = o->matchPlayer(); } }\n' +
     '                if (!seatOrdered_ && !localDead_ && localPlayer_ != s.owner) {'],
    // Shockwave victims.
    ['    for (MatchOpponent* o : opponents_) if (o->spawned()) hit(o->matchPlayer(), o->pawn());\n' +
     '    if (self != localPlayer_ && !localDead_) hit(localPlayer_, player_.pawn());\n',
     '    if (seatOrdered_) {   // online: by match player index\n' +
     '        for (size_t q = 0; q < match_.players().size(); ++q) if (const Character* c = participantPawn((int)q)) hit((int)q, *c);\n' +
     '    } else {\n' +
     '    for (MatchOpponent* o : opponents_) if (o->spawned()) hit(o->matchPlayer(), o->pawn());\n' +
     '    if (self != localPlayer_ && !localDead_) hit(localPlayer_, player_.pawn());\n' +
     '    }\n'],
    // WFC_SIMHASH pawn order.
    ['        if (!localDead_) pawn(player_.pawn(), localPlayer_);\n' +
     '        for (MatchOpponent* o : opponents_) if (o->spawned()) pawn(o->pawn(), o->matchPlayer());\n',
     '        if (seatOrdered_) { for (size_t q = 0; q < match_.players().size(); ++q) if (const Character* c = participantPawn((int)q)) pawn(*c, (int)q); }   // online: by index\n' +
     '        else {\n' +
     '        if (!localDead_) pawn(player_.pawn(), localPlayer_);\n' +
     '        for (MatchOpponent* o : opponents_) if (o->spawned()) pawn(o->pawn(), o->matchPlayer());\n' +
     '        }\n'],
    // Fine aim.
    ['    s.fineAim = player == localPlayer_ && player_.controller().fineAiming();\n',
     '    s.fineAim = onlineSeats_ ? (player >= 0 && (size_t)player < seatFineAim_.size() && seatFineAim_[(size_t)player] != 0)   // online: the seat command\n' +
     '                             : player == localPlayer_ && player_.controller().fineAiming();\n'],
    ['        in.wantBoost = held(platform::Button::FineAim);          // RightMouseButton = "ToggleFineAim | Boost" (the original\'s move flag 512)\n',
     '        in.wantBoost = held(platform::Button::FineAim);          // RightMouseButton = "ToggleFineAim | Boost" (the original\'s move flag 512)\n' +
     '        if (seatFineAim_.size() <= (size_t)p) seatFineAim_.resize((size_t)p + 1, 0);\n' +
     '        seatFineAim_[(size_t)p] = in.wantBoost ? 1 : 0;\n'],
]);
console.log('ok');
