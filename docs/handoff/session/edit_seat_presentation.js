// Seat presentation follow-ups (Integration 2026-10-17, the local-seat PARTIALs):
// 1. The online local pawn's inter-step presentation yaw: PlayerController::presentationStep (the direct path's per-step reference for the
//    per-frame draw-yaw offset) is called by the local seat too, so the robot does not step-turn at the sim rate.
// 2. Vehicle shots of every seat leave from the vehicle's WeaponSocket_Primary / _Primary2 (alternating where the weapon and chassis author
//    both; the bone pose is sim state, so every peer agrees): projectiles from the socket toward the aim point, as the direct path does.
//    The local seat's vehicle shot also notes the socket for the local muzzle-flash presentation (noteVehicleShot).
// 3. The local seat's shots get the camera recoil / shake (onLocalShot), and its robot shots' participant presentation is anchored to the
//    local held weapon's MuzzleFlash socket (participantMuzzleWorld knew only opponents: the flash fell back to the eye).
// Presentation / seat-only: offline identical. Run from a tree root (integration 53548ac3+ with GAMEPLAY_ONLINE_FLAG).
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/PlayerController.h', [[
    '    void onLocalShot(const char* weaponId);\n',
    '    void onLocalShot(const char* weaponId);\n' +
    '    // The per-step reference for the per-frame presentation yaw (draw-yaw offset until the next step): the direct path and the online\n' +
    '    // local seat call it once per simulation step with the step\'s face yaw.\n' +
    '    void presentationStep(float faceYaw, float dt);\n'
]]);
ed('src/game/PlayerController.cpp', [
    ['    // Presentation-yaw reference for the frames until the next step.\n' +
     '    stepYawRate_ = std::remainder(pawn_->yaw() - stepBodyYaw_, 6.2831853f) / std::max(dt, 1e-4f);\n' +
     '    stepBodyYaw_ = pawn_->yaw(); stepFaceYaw_ = step.faceYaw; sinceStep_ = 0.0f;\n' +
     '    pawn_->setDrawYawOffset(0.0f);\n',
     '    presentationStep(step.faceYaw, dt);   // presentation-yaw reference for the frames until the next step\n'],
    ['void PlayerController::onLocalShot(const char* weaponId) {\n',
     'void PlayerController::presentationStep(float faceYaw, float dt) {\n' +
     '    if (!pawn_) return;\n' +
     '    stepYawRate_ = std::remainder(pawn_->yaw() - stepBodyYaw_, 6.2831853f) / std::max(dt, 1e-4f);\n' +
     '    stepBodyYaw_ = pawn_->yaw(); stepFaceYaw_ = faceYaw; sinceStep_ = 0.0f;\n' +
     '    pawn_->setDrawYawOffset(0.0f);\n' +
     '}\n\n' +
     'void PlayerController::onLocalShot(const char* weaponId) {\n'],
]);
ed('src/game/World.cpp', [
    ['    if (pc.moveForm() == Form::Vehicle && !pc.isTransforming()) if (Weapon* vw = pc.vehicleWeapon()) vw->tick(dt);\n}\n',
     '    if (pc.moveForm() == Form::Vehicle && !pc.isTransforming()) if (Weapon* vw = pc.vehicleWeapon()) vw->tick(dt);\n' +
     '    player_.controller().presentationStep(localSeatIntent_.faceYaw, dt);   // the per-frame draw yaw between steps\n}\n'],
    ['    const core::Vec3 eye = botEye(pc), d = core::normalize(aimPt - eye);   // from the eye toward the aim point\n',
     '    if (p == localPlayer_) player_.controller().onLocalShot(w.def ? w.def->id : nullptr);   // the local seat: camera recoil + shake\n' +
     '    const core::Vec3 eye = botEye(pc), d = core::normalize(aimPt - eye);   // from the eye toward the aim point\n' +
     '    if (pc.moveForm() == Form::Vehicle && !pc.isTransforming() && &w == pc.vehicleWeapon()) {\n' +
     '        // The vehicle weapon\'s CurrentSocket: WeaponSocket_Primary, alternating with _Primary2 per shot where both are authored [CONF RE\n' +
     '        // pass 5 9g]; the posed bone x socket (sim state), else the actor + 1 m.\n' +
     '        const bool two = w.alternatesMuzzle() && pc.chassis().vehicleWeapon2.valid;\n' +
     '        if (!two) w.muzzleSocket = 0;\n' +
     '        const int sock = w.muzzleSocket;\n' +
     '        core::Vec3 origin = pc.actorLocation() + core::Vec3{0, 1.0f, 0};\n' +
     '        const SocketDef& vs = sock == 1 ? pc.chassis().vehicleWeapon2 : pc.chassis().vehicleWeapon;\n' +
     '        core::Mat4 bm;\n' +
     '        if (vs.valid && pc.form() == Form::Vehicle && pc.boneWorld(vs.bone, bm)) { const core::Mat4 m = bm * vs.local; origin = core::Vec3{m.m[12], m.m[13], m.m[14]}; }\n' +
     '        if (p == localPlayer_) noteVehicleShot(sock, origin);   // the local muzzle-flash presentation\n' +
     '        if (two) w.muzzleSocket = (sock + 1) % 2;\n' +
     '        if (w.projectile()) {   // RealStartLoc = the socket, aimed at the aim point\n' +
     '            spawnProjectile(origin, core::normalize(aimPt - origin) * w.projSpeed, w, p);\n' +
     '            participantShots_.push_back({p, w.def ? w.def->id : "", origin, aimPt, false, -1});\n' +
     '            return;\n' +
     '        }\n' +
     '    }\n'],
    ['bool World::participantMuzzleWorld(int player, core::Mat4& out) const {\n',
     'bool World::participantMuzzleWorld(int player, core::Mat4& out) const {\n' +
     '    if (player == localPlayer_) return !localDead_ && player_.pawn().hasWeapon() && weaponSocketWorld("MuzzleFlash", out);   // the online local seat\n'],
]);
console.log('ok');
