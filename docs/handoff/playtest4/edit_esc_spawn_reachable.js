// Playtest 4 (2026-10-24), Escalation wave 1 never completes (Forsaken = Tranquillity; all five maps headless, invulnerable
// bot-driven human, 360 s: no map clears wave 1). Cause: horde pawns spawn where our generated bot nav does not connect to the
// players - authored TnSurvivalSpawnPoints with no nav cell nearby (40 on Tranquillity, 35 on Remnant) or in a nav piece sealed
// from the players (Remnant's opening top zones 11696 / 1584 / 9840). They get no path, stand still behind barriers / in walls,
// and the wave (budget spent AND horde dead) can never clear. The original AI reached them over its own path network.
// PC ADAPTATION for our nav's coverage, in SpawnDude:
//   - the candidate zones are the type's sorted zones that have a point reachable from the players' nav piece (a nav cell of that
//     piece within 40 m on the same storey); the original's weighted top-3 draw then runs over those (all zones when none qualify);
//   - a point is used where it is when its own cell (4 m) is in the players' piece, else at that reachable cell's centroid, else
//     the try is spent (as a zone without points).
// Run from the tree root.
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
ed('src/game/WorldSurvival.cpp', [
    ['    const std::vector<int>& sorted = sv_.sorted[(size_t)t];' + NL + '    const int top = std::min<int>(3, (int)sorted.size());' + NL,
     '    // PC ADAPTATION (playtest 4): only zones with a point our bot nav connects to the players (see svReachablePoint)' + NL +
     '    const int want = svPlayersNavPiece();' + NL +
     '    static std::vector<int> reach;' + NL +
     '    reach.clear();' + NL +
     '    for (int z : sv_.sorted[(size_t)t]) {' + NL +
     '        const SurvivalData::Zone& zz = svData_.zones[(size_t)z];' + NL +
     '        for (size_t k = 0; k < zz.spawnPoints.size(); ++k) { core::Vec3 tmp{zz.spawnPoints[k][0], zz.spawnPoints[k][1], zz.spawnPoints[k][2]}; if (svReachablePoint(tmp, want)) { reach.push_back(z); break; } }' + NL +
     '    }' + NL +
     '    const std::vector<int>& sorted = reach.empty() ? sv_.sorted[(size_t)t] : reach;' + NL +
     '    const int top = std::min<int>(3, (int)sorted.size());' + NL],
    ['        }' + NL + '        float gy = 0.0f; core::Vec3 gn;' + NL +
     '        if (!collision_.valid() || !collision_.groundHeight(at.x, at.z, at.y + 0.5f, 0.0f, gy, gn)) {' + NL,
     '        }' + NL +
     '        if (!reach.empty() && !svReachablePoint(at, want)) {   // PC ADAPTATION (playtest 4): this point is not reachable: the try is spent' + NL +
     '            if (slog) LOG_INFO("ESCLOG   point %d (%.2f %.2f %.2f): not reachable on the bot nav (zone %s)", idx, at.x, at.y, at.z, zone.id.c_str());' + NL +
     '            continue;' + NL +
     '        }' + NL +
     '        float gy = 0.0f; core::Vec3 gn;' + NL +
     '        if (!collision_.valid() || !collision_.groundHeight(at.x, at.z, at.y + 0.5f, 0.0f, gy, gn)) {' + NL],
    ['bool World::svSpawn(int slot) {' + NL,
     '// PC ADAPTATION (playtest 4): the nav piece the players stand in (closed doors split the nav), else the main piece.' + NL +
     'int World::svPlayersNavPiece() const {' + NL +
     '    for (size_t q = 0; q < match_.players().size(); ++q)' + NL +
     '        if (match_.players()[q].kind != ParticipantKind::Bot) if (const Character* c = participantPawn((int)q)) { const int cell = botNav_.findCell(c->position(), 12.0f); if (cell >= 0) return botNav_.pieceOf(cell); }' + NL +
     '    return botNav_.mainPieceId();' + NL +
     '}' + NL + NL +
     '// A spawn point the horde can path from: its own nav cell (4 m) in the players\' piece, else a cell of that piece within 40 m on the' + NL +
     '// same storey (the point moves to its centroid). False: our nav does not connect it (the original\'s path network did).' + NL +
     'bool World::svReachablePoint(core::Vec3& at, int want) const {' + NL +
     '    if (!botNav_.valid()) return true;' + NL +
     '    int c = botNav_.findCell(at, 4.0f);' + NL +
     '    if (c >= 0 && !botNav_.isBlocked(c) && botNav_.pieceOf(c) == want) return true;' + NL +
     '    c = botNav_.findCell(at, 40.0f, 12.0f);' + NL +
     '    if (c < 0 || botNav_.isBlocked(c) || botNav_.pieceOf(c) != want || std::fabs(botNav_.cells()[(size_t)c].centroid.y - at.y) > 12.0f) return false;' + NL +
     '    at = botNav_.cells()[(size_t)c].centroid;' + NL +
     '    return true;' + NL +
     '}' + NL + NL +
     'bool World::svSpawn(int slot) {' + NL],
]);
ed('src/game/World.h', [
    ['    bool svSpawn(int slot);', '    bool svSpawn(int slot);' + NL + '    int svPlayersNavPiece() const;   // PC ADAPTATION (playtest 4)' + NL + '    bool svReachablePoint(core::Vec3& at, int want) const;'],
]);
console.log('ok');
