// Death-body patch 2, part 4d: (a) the death effects at their authored sockets: Systems' kDeathSockets (agents/systems 409b7f39,
// SkeletalMeshSocket rows: mesh opath, socket, bone, RelativeLocation UU in the UE bone frame), keyed by the chassis' source skeletal
// mesh ("content/<pkg>/<mesh>.gltf" -> "<pkg>.<mesh>"); the bone frame is the glTF export's (UE with Y / Z swapped), so the offset is
// (x, z, y) x 0.01. Head_FX / BoosterSocket_C / Chest_FX / TargetingSocket / VaporTrailSocket_R for the PlayEffect modifier, and
// PositionSocket for the gibs blueprint's effects. A mesh without the row keeps the earlier placement.
// (b) TnPawn.GibbedCameraShake (RE DEATH_CAMERA_RE addendum, 51cca62, CONFIRMED): Dying.Gib shakes every local controller whose
// pawn exists within VSize < 1000 UU of the gibbed pawn (all or nothing; the victim feels nothing). TnShakeCameraBehavior: yaw =
// 3 deg x sin(2 pi 5 t), pitch = 5 deg x sin(2 pi 4 t) (HmMultiFrequencyWaveGenerator, t running since the generator's creation,
// never reset), each x f = remaining / 0.89 s (linear); the rotator x Intensity 0.55; roll = 0.75 deg x r^2 x 0.55, r the linear
// RollFalloff over 0.89 s (one-sided, no oscillation). A new shake restarts the fades; a camera strategy change cuts a running one.
// Applied in the robot OverTheShoulder, Death and SpectateOverTheShoulder views (strategies with TnShakeCameraBehavior)
// [vehicle live strategies: not applied, PROV]. Presentation only.
// Run from a tree root after edit_death_fx.js, with corpsep2/new/DeathFx.inc (409b7f39) copied to src/game/.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
if (!fs.readFileSync('src/game/DeathFx.inc', 'utf8').includes('kDeathSockets')) throw new Error('DeathFx.inc lacks kDeathSockets: copy the 409b7f39 version');

ed('src/game/ChassisDef.h', [[
    '    std::string robotSkelGltf;',
    '    std::string vehicleSkelGltf;   // character.json vehicle.source_gltf (the death sockets\' mesh key)\n    std::string robotSkelGltf;'
]]);
ed('src/game/ChassisDef.cpp', [[
    '    d.robotSkelGltf = c["robot"]["source_gltf"].asString();\n',
    '    d.robotSkelGltf = c["robot"]["source_gltf"].asString();\n    d.vehicleSkelGltf = c["vehicle"]["source_gltf"].asString();\n'
]]);

ed('src/game/World.h', [
    ['        std::string socketBone; core::Mat4 socketLocal;   // PositionSocket\n',
     '        std::string socketBone; core::Mat4 socketLocal;   // PositionSocket\n' +
     '        std::string meshKey;   // the source skeletal mesh opath ("<pkg>.<mesh>") of the dying form (kDeathSockets)\n'],
    ['        std::string socketBone; core::Mat4 socketLocal;   // the robot\'s PositionSocket (bone + local), for the gib location',
     '        std::string socketBone; core::Mat4 socketLocal;   // the robot\'s PositionSocket (bone + local), for the gib location\n' +
     '        std::string meshKey;   // kDeathSockets key of the dying form\'s mesh\n' +
     '        bool deathSocketWorld(const char* socket, core::Vec3& out) const;   // an authored death socket\'s world position'],
]);

ed('src/game/PlayerController.h', [
    ['    int takeSpectateCycle() {',
     '    // TnPawn.GibbedCameraShake (a pawn gibbed within 1000 UU of the local pawn): restarts the fades.\n' +
     '    void gibbedShake() { shakeRemain_ = 0.89f; }\n' +
     '    int takeSpectateCycle() {'],
    ['    void spectateView(render::Camera& cam) const;\n',
     '    void spectateView(render::Camera& cam) const;\n' +
     '    float shakeRemain_ = 0.0f, shakeGenT_ = 0.0f;   // TnShakeCameraBehavior: the fade left (s); the wave generator\'s time\n' +
     '    void applyShake(render::Camera& cam) const;\n'],
]);

ed('src/game/PlayerController.cpp', [
    // The generator runs from creation; the fade counts down per frame; strategy changes cut it.
    ['    {   // SwitchSpectatorTarget input (LB / \']\' = +1, RB / \'[\' = -1), edges of the UiKey bits and the bound keys\n',
     '    shakeGenT_ += dt; shakeRemain_ = std::max(0.0f, shakeRemain_ - dt);\n' +
     '    {   // SwitchSpectatorTarget input (LB / \']\' = +1, RB / \'[\' = -1), edges of the UiKey bits and the bound keys\n'],
    ['    deathFromPos_ = from.pos; deathFromYaw_ = from.yaw; deathFromPitch_ = from.pitch;\n',
     '    deathFromPos_ = from.pos; deathFromYaw_ = from.yaw; deathFromPitch_ = from.pitch;\n    shakeRemain_ = 0.0f;   // Activate -> Shaker.Reset\n'],
    ['    deathCam_ = false;\n    specTarget_ = true;\n',
     '    deathCam_ = false;\n    specTarget_ = true;\n    shakeRemain_ = 0.0f;   // Activate -> Shaker.Reset\n'],
    // Views: the robot live view, Death, SpectateOverTheShoulder.
    ['    if (specTarget_) { spectateView(cam); return; }\n    if (deathCam_) { deathCameraView(cam); return; }\n',
     '    if (specTarget_) { spectateView(cam); applyShake(cam); return; }\n    if (deathCam_) { deathCameraView(cam); if (!deathVehicle_) applyShake(cam); return; }\n'],
    ['    cam.fovXDeg = fovCur_;\n}\n\nvoid PlayerController::beginSpectateTarget(float targetYaw) {\n',
     '    cam.fovXDeg = fovCur_;\n' +
     '    if (strategy_ == 0) applyShake(cam);   // OverTheShoulder has TnShakeCameraBehavior [vehicle strategies: PROV, not applied]\n' +
     '}\n\n' +
     'void PlayerController::applyShake(render::Camera& cam) const {\n' +
     '    if (shakeRemain_ <= 0.0f) return;\n' +
     '    const float f = shakeRemain_ / 0.89f, k = 0.55f * 0.0174533f;   // linear fades; Intensity 0.55; degrees\n' +
     '    cam.yaw += k * 3.0f * std::sin(6.2831853f * 5.0f * shakeGenT_) * f;\n' +
     '    cam.pitch += k * 5.0f * std::sin(6.2831853f * 4.0f * shakeGenT_) * f;\n' +
     '    cam.roll += k * 0.75f * f * f;   // RollFalloff: one-sided, quadratic\n' +
     '}\n\n' +
     'void PlayerController::beginSpectateTarget(float targetYaw) {\n'],
]);

const W = 'src/game/World.cpp';
ed(W, [
    // The mesh key on the request / corpse.
    ['    if (pawn.chassis().positionSocket.valid) { q.socketBone = pawn.chassis().positionSocket.bone; q.socketLocal = pawn.chassis().positionSocket.local; }\n',
     '    if (pawn.chassis().positionSocket.valid) { q.socketBone = pawn.chassis().positionSocket.bone; q.socketLocal = pawn.chassis().positionSocket.local; }\n' +
     '    {   // "content/<pkg>/<mesh>.gltf" -> "<pkg>.<mesh>" (Systems\' kDeathSockets key)\n' +
     '        const std::string& g = q.vehicle ? pawn.chassis().vehicleSkelGltf : pawn.chassis().robotSkelGltf;\n' +
     '        const size_t s1 = g.rfind(\'/\'), s0 = s1 == std::string::npos ? std::string::npos : g.rfind(\'/\', s1 - 1), dot = g.rfind(\'.\');\n' +
     '        if (s0 != std::string::npos && dot != std::string::npos && dot > s1) q.meshKey = g.substr(s0 + 1, s1 - s0 - 1) + "." + g.substr(s1 + 1, dot - s1 - 1);\n' +
     '    }\n'],
    ['c.socketBone = q.socketBone; c.socketLocal = q.socketLocal;\n',
     'c.socketBone = q.socketBone; c.socketLocal = q.socketLocal; c.meshKey = q.meshKey;\n'],
    // The socket lookup.
    ['void World::gibCorpse(Corpse& c) {\n',
     'bool World::Corpse::deathSocketWorld(const char* socket, core::Vec3& out) const {\n' +
     '    if (meshKey.empty() || !model || glob.empty()) return false;\n' +
     '    for (const DeathSocket& d : kDeathSockets) {\n' +
     '        if (meshKey != d.mesh || std::strcmp(socket, d.socket)) continue;\n' +
     '        const int bn = model->nodeByName(d.bone);\n' +
     '        if (bn < 0 || (size_t)bn >= glob.size()) return false;\n' +
     '        out = core::transformPoint(world * glob[(size_t)bn], core::Vec3{d.x, d.z, d.y} * 0.01f);   // UE bone frame -> the export\'s\n' +
     '        return true;\n' +
     '    }\n' +
     '    return false;\n' +
     '}\n\n' +
     'void World::gibCorpse(Corpse& c) {\n'],
    // TnPawn.GibbedCameraShake: the local pawn within 1000 UU (the victim feels nothing).
    ['    if (c.noGibs) return;   // Melee: the Defrag dissolve alone\n',
     '    if (!localDead_ && core::length(player_.pawn().actorLocation() - c.pos) < 10.0f) player_.controller().gibbedShake();   // GibbedCameraShake\n' +
     '    if (c.noGibs) return;   // Melee: the Defrag dissolve alone\n'],
    // The gibs blueprint's effects at PositionSocket.
    ['        for (int i = 0; i < fxr->nGibFx && renderer_; ++i)\n' +
     '            fxStepSpawn(fxr->gibFx[i].tmpl, c.pos + core::Vec3{fxr->gibFx[i].x, fxr->gibFx[i].z, fxr->gibFx[i].y} * 0.01f, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1}, -1, true);\n',
     '        core::Vec3 ps = c.pos; c.deathSocketWorld("PositionSocket", ps);\n' +
     '        for (int i = 0; i < fxr->nGibFx && renderer_; ++i)\n' +
     '            fxStepSpawn(fxr->gibFx[i].tmpl, ps + core::Vec3{fxr->gibFx[i].x, fxr->gibFx[i].z, fxr->gibFx[i].y} * 0.01f, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1}, -1, true);\n'],
    // PlayEffect at the authored socket when the mesh has it.
    ['                        else if (bone && !std::strcmp(so, "Head_FX")) at = c.pos + core::Vec3{0, 3.0f, 0};\n',
     '                        else if (bone && !std::strcmp(so, "Head_FX")) at = c.pos + core::Vec3{0, 3.0f, 0};\n' +
     '                        c.deathSocketWorld(so, at);   // the authored socket (kDeathSockets) overrides the placement above\n'],
]);
console.log('ok');
