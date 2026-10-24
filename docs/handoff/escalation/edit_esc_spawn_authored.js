// Escalation spawn finder as the base game (RE DLC_ESC_MECHANISMS_RE.md section 7, 42b9328): the pawn appears at the zone's authored
// TnSurvivalSpawnPoint (ground + capsule check, up to 5 tries); there is no navmesh requirement - the AI re-anchors to the nearest
// nav afterwards (our bots: the off-mesh walk to the mesh). The PROV stand-in (an off-nav point replaced by an on-nav one, else the
// try spent) is removed: it blocked Pulse entirely (every point off our reconstructed nav) and moved spawns on BrokenHope / Remnant.
// Run from the tree root.
const fs = require('fs');
const NL = String.fromCharCode(10), CR = String.fromCharCode(13);
const f = 'src/game/WorldSurvival.cpp';
let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes(CR + NL); s = s.split(CR + NL).join(NL);
const a = '        // PROV (Integration 2026-10-23, removed with E2b part 3';
const b = '        core::Vec3 at; float yaw = 0.0f;' + NL;
const i = s.indexOf(a), j = s.indexOf(b);
if (i < 0 || j < 0 || j < i || s.indexOf(a, i + 1) >= 0) throw new Error('anchors not found / not unique');
s = s.slice(0, i) +
    '        // The authored point (RE section 7: no navmesh requirement; the AI re-anchors to the nearest nav from there).' + NL +
    '        const SurvivalData::Zone* pz = &zone;' + NL +
    s.slice(j);
fs.writeFileSync(f, crlf ? s.split(NL).join(CR + NL) : s);
{   // ESCTEST DLC branch: "due" per enemy TYPE (a row limited to one wave may legitimately never be drawn by the budget: Pulse's
    // Soldier_Tank (1-1) beside its (0-1) row)
    const g = 'src/core/Application_EscTest.cpp';
    let t = fs.readFileSync(g, 'utf8'); const crlf2 = t.includes(CR + NL); t = t.split(CR + NL).join(NL);
    const r1 = '            if (D.enemies[e].minWave < S.currentWave - 1 && D.enemies[e].maxCount > 0 && spawnedByEntry[e] == 0) due = false;' + NL;
    const r2 = '        check(due, "every enemy row due by the reached wave spawned");' + NL;
    if (t.split(r1).length !== 2 || t.split(r2).length !== 2) throw new Error('ESCTEST anchors');
    t = t.replace(r1, '');
    t = t.replace(r2,
        '        for (size_t e = 0; e < D.enemies.size(); ++e) {   // every TYPE due by the reached wave spawned (any of its rows)' + NL +
        '            if (D.enemies[e].minWave >= S.currentWave - 1 || D.enemies[e].maxCount <= 0) continue;' + NL +
        '            int n = 0; for (size_t k = 0; k < D.enemies.size(); ++k) if (D.enemies[k].type == D.enemies[e].type) n += spawnedByEntry[k];' + NL +
        '            if (n == 0) { due = false; LOG_INFO("ESCTEST %s: %s due but never spawned", map.c_str(), D.enemies[e].type.c_str()); }' + NL +
        '        }' + NL +
        '        check(due, "every enemy type due by the reached wave spawned");' + NL);
    fs.writeFileSync(g, crlf2 ? t.split(NL).join(CR + NL) : t);
}
console.log('ok');
