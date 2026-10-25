// Playtest 4 (map escape by double jump strands the player): the bots' stranded rule for the local human. Off every nav cell
// beyond the stranded search's tolerance (no cell within 25 m horizontally / 34 m below: MaxFallHeight; the bots' beginSearch
// fallback) for 30 s (the bots' offMeshRun threshold) in robot form -> the original's out-of-world handling: KillZ's death
// (Engine.DmgType_Fell) and the normal respawn. Inert anywhere a floor of the nav is in reach (props, ledges, short drops).
// Versus and Escalation (not the campaign: its chapter nav is partial).
// WFC_STRANDTEST=1 (TEST ONLY): 3 s into the match the local pawn is placed on the first standable surface of a 4 m grid over the
// nav bounds that the stranded search does not reach, to prove the 30 s respawn headless. Run from the tree root.
const fs = require('fs');
const NL = String.fromCharCode(10), CR = String.fromCharCode(13);
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes(CR + NL); s = s.split(CR + NL).join(NL);
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.split(NL).join(CR + NL) : s);
}
ed('src/game/World.cpp', [
    ['        else respawnPlayer();' + NL + '    }' + NL + '    { TickTimer tt(10); for (auto& a : actors_) if (a->alive()) a->tick(*this, dt); }' + NL,
     '        else respawnPlayer();' + NL + '    }' + NL +
     '    if (matchActive_ && !cp_.active && !localDead_ && botNav_.valid() && match_.state() == Match::State::InProgress) {' + NL +
     '        Character& lp = player_.pawn();' + NL +
     '        static const bool strandTest = std::getenv("WFC_STRANDTEST") != nullptr;   // TEST ONLY' + NL +
     '        if (strandTest && !strandTestDone_ && match_.matchTime() > 3.0f) {' + NL +
     '            strandTestDone_ = true;' + NL +
     '            core::Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};' + NL +
     '            for (const BotNav::Cell& c : botNav_.cells()) { lo = {std::min(lo.x, c.bmin.x), std::min(lo.y, c.bmin.y), std::min(lo.z, c.bmin.z)}; hi = {std::max(hi.x, c.bmax.x), std::max(hi.y, c.bmax.y), std::max(hi.z, c.bmax.z)}; }' + NL +
     '            bool placed = false;' + NL +
     '            for (float x = lo.x; x <= hi.x && !placed; x += 4.0f)' + NL +
     '                for (float z = lo.z; z <= hi.z && !placed; z += 4.0f) {' + NL +
     '                    float gy; core::Vec3 n;' + NL +
     '                    if (!collision_.groundHeight(x, z, hi.y + 60.0f, 0.0f, gy, n) || n.y < 0.8f) continue;' + NL +
     '                    if (botNav_.findCell(core::Vec3{x, gy, z}, 25.0f, 34.0f) >= 0) continue;' + NL +
     '                    lp.setPosition(core::Vec3{x, gy + 0.05f, z}); lp.velocity() = {0, 0, 0}; lp.groundY = gy; placed = true;' + NL +
     '                    LOG_INFO("STRANDTEST: the local pawn placed at (%.1f %.1f %.1f), no nav within 25 m / 34 m below", x, gy, z);' + NL +
     '                }' + NL +
     '            if (!placed) LOG_INFO("STRANDTEST: no stranded surface found");' + NL +
     '        }' + NL +
     '        // the bots\' stranded rule for the human (playtest 4): no nav floor in reach for 30 s in robot form -> KillZ\'s handling' + NL +
     '        const bool stranded = lp.moveForm() == Form::Robot && !lp.isTransforming() && botNav_.findCell(lp.position(), 25.0f, 34.0f) < 0;' + NL +
     '        localStrandRun_ = stranded ? localStrandRun_ + dt : 0.0f;' + NL +
     '        if (localStrandRun_ >= 30.0f) {' + NL +
     '            LOG_INFO("World: local player stranded off the nav for 30 s at (%.1f %.1f %.1f): out-of-world handling (KillZ)", lp.position().x, lp.position().y, lp.position().z);' + NL +
     '            localStrandRun_ = 0.0f;' + NL +
     '            killLocalPlayer(-1, false, "Engine.DmgType_Fell");' + NL +
     '        }' + NL +
     '    }' + NL +
     '    { TickTimer tt(10); for (auto& a : actors_) if (a->alive()) a->tick(*this, dt); }' + NL],
]);
ed('src/game/World.h', [
    ['    void ensureHullMeshes();' + NL, '    void ensureHullMeshes();' + NL + '    float localStrandRun_ = 0.0f; bool strandTestDone_ = false;   // the human stranded rule (playtest 4) / WFC_STRANDTEST' + NL],
]);
console.log('ok');
