// Escalation E3a (on top of E2b part 2): the horde's real bodies (AssetTools out/escalation_characters/<id> loaded as chassis "ESC_<id>";
// a chassis without a vehicle form - the Spider - never transforms), the entries' weapons and ROBODEF health set at each horde spawn,
// and the wave buffs on damage (DecreaseDamageTaken / IncreaseDamage, applied at spawn). Files copied from argv[2].
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
for (const [src, dst] of [['docs/handoff/escalation/Survival.h', 'src/game/Survival.h'], ['docs/handoff/escalation/WorldSurvival.cpp', 'src/game/WorldSurvival.cpp'],
                          ['src/game/EscalationData.inc', 'src/game/EscalationData.inc'], ['docs/handoff/escalation/Application_EscTest.cpp', 'src/core/Application_EscTest.cpp']])
    fs.copyFileSync(path.join(G, src), dst);
ed('src/game/ChassisDef.cpp', [
    ['    const std::string dir = vsRoot + "/Characters/" + id + "/";\n',
     '    // "ESC_<id>": an Escalation horde body from AssetTools out/escalation_characters/<id> (its paths are ExtractedAssets-relative too).\n' +
     '    const std::string dir = id.rfind("ESC_", 0) == 0 ? vsRoot + "/../../AssetTools/out/escalation_characters/" + id.substr(4) + "/"\n' +
     '                                                     : vsRoot + "/Characters/" + id + "/";\n'],
]);
ed('src/game/ChassisDef.cpp', [
    ['    if (d.vehicleGlb.empty() || platform::dataFileSize(extRoot + d.vehicleGlb) < 0) { d.loadError = "vehicle.glb missing for " + id; return false; }\n',
     '    // A horde body without a vehicle form (the Spider: forms.vehicle skipped) never transforms; World loads its robot as the stand-in.\n' +
     '    if (id.rfind("ESC_", 0) == 0 && d.vehicleGlb.empty()) d.flyerNoTransform = true;\n' +
     '    else if (d.vehicleGlb.empty() || platform::dataFileSize(extRoot + d.vehicleGlb) < 0) { d.loadError = "vehicle.glb missing for " + id; return false; }\n'],
    ['    if (const assets::Json* re = rosterEntry(id)) {\n',
     '    // Escalation horde bodies: their roster entry is keyed without the ESC_ prefix; none yet -> the default cylinder (PROV, warned).\n' +
     '    const bool esc = id.rfind("ESC_", 0) == 0;\n' +
     '    if (esc && !rosterEntry(id.substr(4))) {\n' +
     '        static bool warned = false;\n' +
     '        if (!warned) { warned = true; LOG_WARN("chassis %s: no roster_package entry yet - the default collision cylinder (PROV)", id.c_str()); }\n' +
     '    } else if (const assets::Json* re = rosterEntry(esc ? id.substr(4) : id)) {\n'],
]);
ed('src/game/World.cpp', [
    ['    } else if (!assets::loadSkinnedGlb(ext + a->def.vehicleGlb, a->vehicle) || (chProf[2] = profNowMs(), !a->vehicle.valid())) {\n',
     '    } else if (a->def.vehicleGlb.empty() && (a->def.flyerNoTransform = true, !assets::loadSkinnedGlb(ext + a->def.robotGlb, a->vehicle))) {\n' +
     '        a->error = "robot.glb (as the vehicle stand-in) failed to load for " + id;   // no vehicle form (the Spider): never transforms\n' +
     '    } else if (!a->def.vehicleGlb.empty() && (!assets::loadSkinnedGlb(ext + a->def.vehicleGlb, a->vehicle) || (chProf[2] = profNowMs(), !a->vehicle.valid()))) {\n'],
    ['    float applied = h->applyDamage(amount);\n',
     '    if (sv_.phase != SurvivalState::Phase::Off) amount *= svDamageMul(victim, instigator);   // Escalation wave buffs\n' +
     '    float applied = h->applyDamage(amount);\n'],
    ['                        o->spawnAt(p);\n',
     '                        o->spawnAt(p);\n' +
     '                        if (chassisOk && op.directorSpawned) svApplyHordeStats(o->pawn(), o->matchPlayer());   // E3a: weapons + health (after respawnReset)\n'],
]);
ed('src/game/World.h', [
    ['    void svTickWeaponPickups(float dt);\n',
     '    void svTickWeaponPickups(float dt);\n    void svApplyHordeStats(Character& c, int player);\n    float svDamageMul(int victim, int instigator) const;\n'],
]);
console.log('ok');
