// Escalation DLC map mechanisms (District lasers / sentries / power outage / detpacks, Tranquillity floodgates / base doors):
// SurvivalMech.h + WorldSurvivalMech.cpp + gen_escalation.py copied from argv[2], the hooks below; then run gen_escalation.py to
// regenerate data/gameplay + EscalationData.inc. Run from the tree root.
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
const H = path.join(G, 'docs/handoff/escalation');
fs.copyFileSync(path.join(H, 'SurvivalMech.h'), 'src/game/SurvivalMech.h');
fs.copyFileSync(path.join(H, 'WorldSurvivalMech.cpp'), 'src/game/WorldSurvivalMech.cpp');
fs.copyFileSync(path.join(H, 'gen_escalation.py'), 'tools/gameplay/gen_escalation.py');
ed('src/game/Survival.h', [
    ['#include "game/Weapon.h"\n', '#include "game/Weapon.h"\n#include "game/SurvivalMech.h"\n'],
    ['    std::map<std::string, std::string> dropMeshes;',
     '    SvMech mech;                                     // the DLC maps\' mechanisms (SurvivalMech.h; empty elsewhere)\n    std::map<std::string, std::string> dropMeshes;'],
    ['    int roundBegins = 0, roundEnds = 0;\n', '    int roundBegins = 0, roundEnds = 0;\n    SvMechState mech;                     // the DLC map mechanisms\' state (WorldSurvivalMech.cpp)\n'],
]);
ed('src/game/WorldSurvival.cpp', [
    ['        for (const auto& kv : j["dropMeshes"].obj) out.dropMeshes[kv.first] = kv.second.asString();\n',
     '        for (const auto& kv : j["dropMeshes"].obj) out.dropMeshes[kv.first] = kv.second.asString();\n' +
     '        SvMech::parse(j["mechanisms"], out.mech);\n'],
    ['    const bool hadEscNav = sv_.escNav;\n    sv_ = SurvivalState{};\n',
     '    const bool hadEscNav = sv_.escNav;\n    sv_ = SurvivalState{};\n' +
     '    for (HazardVolume& h : hazards_) h.on = true;   // a previous Escalation launch\'s mechanism states\n    svMechSetBlockers(true);\n'],
    ['    sv_.crashDone.assign(svData_.crashWalls.size(), 0);\n',
     '    sv_.crashDone.assign(svData_.crashWalls.size(), 0);\n    svMechSetup();   // the DLC maps\' mechanisms: their PreGameplaySetup states\n'],
    ['             levelDrop ? "level drop (Taken)" : "bought", done, pending);\n',
     '             levelDrop ? "level drop (Taken)" : "bought", done, pending);\n    svMechGenerator(gi, levelDrop);   // a DLC mechanism\'s generator\n'],
    ['            if (!sv_.generatorDone[g] && !sv_.generatorOff[g] && svData_.generators[g].contains(c->position())) { sv_.nearGenerator[q] = (int)g; break; }',
     '            if (!sv_.generatorDone[g] && !sv_.generatorOff[g] && !svMechBuyBlocked() && svData_.generators[g].contains(c->position())) { sv_.nearGenerator[q] = (int)g; break; }'],
    ['    svTickKismet();\n    svTickWeaponPickups(dt);\n', '    svTickKismet();\n    svMechTick(dt);   // the DLC maps\' mechanisms\n    svTickWeaponPickups(dt);\n'],
]);
ed('src/game/World.h', [
    ['    struct HazardVolume { std::string actor, damageType; float damagePerSec = 0, painInterval = 1.0f; bool entryPain = true;\n',
     '    struct HazardVolume { std::string actor, damageType; float damagePerSec = 0, painInterval = 1.0f; bool entryPain = true;\n' +
     '                          bool on = true;     // Escalation mechanisms toggle it (SurvivalMech.h: District\'s lasers, Tranquillity\'s volumes)\n'],
    ['    void svTickKismet();   // Wall_Crashers (map Kismet as data)\n',
     '    void svTickKismet();   // Wall_Crashers (map Kismet as data)\n' +
     '    // The DLC maps\' mechanisms (WorldSurvivalMech.cpp / SurvivalMech.h)\n' +
     '    void svMechSetup();\n    void svMechTick(float dt);\n    void svMechGenerator(int generator, bool levelDrop);\n' +
     '    bool svMechBuyBlocked() const;          // a power outage: no generator can be bought\n' +
     '    bool svMechHazard(const std::string& actor, bool on);\n    void svMechResetGenerator(int generator);\n' +
     '    static std::vector<std::string> svMechBlockerNames(const std::string& map);   // switchable collision brushes (at map load)\n' +
     '    void svMechAddBlockers();\n    void svMechSetBlockers(bool on);\n' +
     '    std::vector<std::string> escBlockerNames_;\n' +
     '    std::vector<std::pair<std::string, std::vector<core::Vec3>>> escBlockerTris_;\n' +
     '    std::vector<int> escBlockerSets_;      // their collision_ dynamic sets\n' +
     '    bool escBlockersOn_ = true;\n'],
    ['    int svWave = 0; int svNextWaveTime = -1; bool svGameOver = false;',
     '    int svWave = 0; int svNextWaveTime = -1; bool svGameOver = false;\n    bool svPowerOutage = false;   // District: a power outage is on (no buying; the dark grade, the screens off)'],
]);
ed('src/game/World.cpp', [
    ['        bool in = !hazards_[i].planes.empty();\n', '        if (!hazards_[i].on) continue;   // switched off (Escalation mechanisms)\n        bool in = !hazards_[i].planes.empty();\n'],
    ['            bool mover = std::find(names.begin(), names.end(), sm.nodeName) != names.end();\n',
     '            bool mover = std::find(names.begin(), names.end(), sm.nodeName) != names.end();\n' +
     '            if (!escBlockerNames_.empty() && std::find(escBlockerNames_.begin(), escBlockerNames_.end(), sm.nodeName) != escBlockerNames_.end()) {\n' +
     '                escBlockerTris_.push_back({sm.nodeName, {}});   // an Escalation mechanism\'s blocking volume: a switchable set\n' +
     '                for (uint32_t i = sm.indexOffset; i < sm.indexOffset + sm.indexCount; ++i) {\n' +
     '                    const uint32_t v = in.indices[i];\n' +
     '                    escBlockerTris_.back().second.push_back({in.positions[v * 3], in.positions[v * 3 + 1], in.positions[v * 3 + 2]});\n' +
     '                }\n' +
     '                continue;\n' +
     '            }\n'],
    ['    render::MeshData colMesh, pawnStatic, weaponColMesh, weaponStatic;\n',
     '    escBlockerNames_ = svMechBlockerNames(mapName_); escBlockerTris_.clear(); escBlockerSets_.clear();\n' +
     '    render::MeshData colMesh, pawnStatic, weaponColMesh, weaponStatic;\n'],
    ['        collision_.build(pawnStatic);\n', '        collision_.build(pawnStatic);\n        svMechAddBlockers();\n'],
    ['        h.svGameOver = sv_.phase == SurvivalState::Phase::GameOver;\n',
     '        h.svGameOver = sv_.phase == SurvivalState::Phase::GameOver;\n        h.svPowerOutage = sv_.mech.outageLeft > 0.0f;\n'],
]);
{   // WFC_ESCTEST DLC branch: the mechanism checks (esc_mech_check.cpp.txt) after the door generator check
    const anchor = '            } else if (map == "MP_ESC_District") check(false, "District: a door generator with loaded doors");\n        }\n';
    const block = fs.readFileSync(path.join(H, 'esc_mech_check.cpp.txt'), 'utf8').replace(/\r\n/g, '\n');
    ed('src/core/Application_EscTest.cpp', [[anchor, anchor + block]]);
}
console.log('ok');
