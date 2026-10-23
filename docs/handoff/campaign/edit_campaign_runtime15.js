// Campaign runtime step 15 (anchor edits; on the head + RUNTIME14): H3 (SeqAct_TnAiShootAtActor = a persistent forced fire target,
// SeqAct_TnAiUseInteract = the AI's interact use, SeqAct_TnAiUseTurret PROV hold), M5 (TnAiController.RangeSet radii), M1 (the
// checkpoints' authored AuxilliarySpawnPoints for the squad). WorldCampaignRuntime.cpp / WorldCampaignInteract.cpp copied from argv[2].
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
for (const f of ['WorldCampaignRuntime.cpp', 'WorldCampaignInteract.cpp'])
    fs.copyFileSync(path.join(G, 'docs/handoff/campaign', f), path.join('src/game', f));
ed('src/game/Campaign.h', [
    ['    CampaignOmega omega;\n',
     '    CampaignOmega omega;\n' +
     '    std::map<int, std::string> shootAt;        // SeqAct_TnAiShootAtActor: AI player -> its forced fire target\n' +
     '    std::map<int, std::string> aiUse;          // SeqAct_TnAiUseInteract: AI player -> the interact it goes to use\n' +
     '    std::map<std::string, std::vector<core::Vec3>> auxSpawns;   // checkpoint actor -> its AuxilliarySpawnPoints\n' +
     '    bool auxLoaded = false;\n'],
]);
ed('src/game/World.h', [
    ['    void cpTickOmega(float dt);\n',
     '    void cpTickOmega(float dt);\n' +
     '    void cpTickAiUse();\n' +
     '    bool cpShootAtPoint(int player, core::Vec3& out) const;\n' +
     '    bool cpAuxSpawn(const std::string& actor, int k, core::Vec3& out);\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['    cpTickInteracts(dt);\n', '    cpTickInteracts(dt);\n    cpTickAiUse();\n'],
    ['        s.pos = at + core::Vec3{k == 1 ? 5.0f : k == 2 ? -5.0f : 0.0f, 0.0f, 0.0f};   // clear of a pawn on the start (Match safe())\n',
     '        // the squad: the checkpoint\'s authored AuxilliarySpawnPoints (M1); else 5 m to its sides (clear of a pawn on the start)\n' +
     '        if (k == 0 || !cpAuxSpawn(actor, k - 1, s.pos)) s.pos = at + core::Vec3{k == 1 ? 5.0f : k == 2 ? -5.0f : 0.0f, 0.0f, 0.0f};\n'],
]);
ed('src/game/WorldBots.cpp', [
    ['    Weapon* w = pc.moveForm() == Form::Vehicle ? pc.vehicleWeapon() : &pc.weapon();\n    // Repairing a teammate:',
     '    Weapon* w = pc.moveForm() == Form::Vehicle ? pc.vehicleWeapon() : &pc.weapon();\n' +
     '    // Campaign SeqAct_TnAiShootAtActor: a forced fire target suspends the threat choice - aim at it and fire the normal weapon.\n' +
     '    core::Vec3 forcedAt;\n' +
     '    if (cp_.active && w && cpShootAtPoint(o.matchPlayer(), forcedAt)) {\n' +
     '        const core::Vec3 d = forcedAt - eye;\n' +
     '        const float maxStep = sk.turnRateDeg * 0.0174533f * dt;\n' +
     '        b.yaw = wrapPi(b.yaw + core::clampf(wrapPi(yawOf(d) - b.yaw), -maxStep, maxStep));\n' +
     '        b.pitch = core::clampf(b.pitch + core::clampf(pitchOf(d) - b.pitch, -maxStep, maxStep), -1.2f, 1.2f);\n' +
     '        if (!pbSandbox_) pc.setAimPitch(b.pitch);\n' +
     '        if (pc.weaponUsable() && w->ammo == 0 && w->canReload()) { w->beginReload(); ++b.reloads; }\n' +
     '        const core::Vec3 aim = core::forwardFromYawPitch(b.yaw, b.pitch);\n' +
     '        if (pc.weaponUsable() && w->canFire() && core::dot(aim, core::normalize(d)) > 0.995f) botFire(o, b, *w, forcedAt);\n' +
     '        return;\n' +
     '    }\n' +
     '    // Repairing a teammate:'],
]);
console.log('ok');
