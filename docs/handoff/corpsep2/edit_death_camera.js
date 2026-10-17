// Death-body patch 2, part 3: the authored death camera (Death_STRATEGY / Death_Vehicle_STRATEGY; RE DEATH_CAMERA_RE, CONFIRMED).
// HmCamera keeps ONE CameraState across strategies, so the death strategy inherits the live strategy's orbit rotation and orbit
// location offset FROZEN at death; nothing turns the view and no stick input is read. Per frame:
//   camera = anchor + (inherited orbit offset + the death anchor offset) rotated by the frozen orbit rotation
//   anchor = the corpse bone (robot C_Spine01_Lumbar01_XB, vehicle C_Reference_XR), translating with the body, frozen once the pawn
//            is destroyed (gib) or the body is gone
//   death anchor offset (camera axes, UE X fwd / Y right / Z up): robot (0, -300, 150) UU (cancels the +300 shoulder shift: the body
//            sits centred), vehicle (-100, 0, 50)
//   rotation: robot = the frozen view; vehicle = TnFixedOrbitRotation relative to the actor: the wreck's yaw, pitch -30
//   FOV 85 (SmoothTime 0.4); the strategy blend lerps the old-to-new POV delta to 0 over TransitionTime (robot 1.25 s, vehicle 1.0)
//   robot TnWiggler3 idle rotation noise: pitch 0.3 / yaw 0.2 deg at 1 Hz [waveform PROV, as the vehicle wiggler]; its 0.4 UU
//            translation is below a pixel and is omitted; roll (wiggle 0.1, vehicle DutchRoll -1 deg): render::Camera has no roll
//   TnThirdPersoncollision: the existing pull-in toward the anchor in front of the first hit
// Held until respawn. [Follow-up: after MinRespawnDelay 3.0 s the original enters PlayerSpectating with the killer cam
// (CameraStrategyManager.StartSpectating, target _LastPawnWhoKilledMe); not in this edit.]
// Presentation only (camera); the sim never reads it. Run from a tree root, after edit_momentum_picker.js.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}

ed('src/game/PlayerController.h', [
    ['    // Camera strategy override with pitch / FOV (TnGuidedMissileCameraStrategyType).\n',
     '    // Death_STRATEGY / Death_Vehicle_STRATEGY (local player death): freezes the live orbit rotation and offset, then views from the\n' +
     '    // corpse bone the World feeds each tick (setDeathAnchor) until endDeathCamera (respawn). wreckYaw: the vehicle\'s yaw at death.\n' +
     '    void beginDeathCamera(bool vehicle, float wreckYaw);\n' +
     '    void setDeathAnchor(const core::Vec3& boneWorld) { deathAnchor_ = boneWorld; deathAnchorOk_ = true; }\n' +
     '    void endDeathCamera() { deathCam_ = false; deathAnchorOk_ = false; }\n' +
     '    bool deathCamera() const { return deathCam_; }\n' +
     '    // Camera strategy override with pitch / FOV (TnGuidedMissileCameraStrategyType).\n'],
    ['    bool spectating_ = false;\n',
     '    bool spectating_ = false;\n' +
     '    // Death camera state (beginDeathCamera): the frozen orbit, the anchor, the strategy blend from the live view.\n' +
     '    bool deathCam_ = false, deathVehicle_ = false, deathAnchorOk_ = false;\n' +
     '    float deathT_ = 0.0f, deathYaw_ = 0.0f, deathPitch_ = 0.0f;\n' +
     '    core::Vec3 deathAnchor_{0, 0, 0}, deathOff_{0, 0, 0};   // deathOff_: orbit space (x forward, y right, z up), m\n' +
     '    core::Vec3 deathFromPos_{0, 0, 0}; float deathFromYaw_ = 0.0f, deathFromPitch_ = 0.0f;\n' +
     '    void deathCameraView(render::Camera& cam) const;\n'],
]);

ed('src/game/PlayerController.cpp', [
    ['void PlayerController::updateCameraStrategy(const platform::InputFrame& in, float dt) {\n    namespace cfg = core::config;\n',
     'void PlayerController::updateCameraStrategy(const platform::InputFrame& in, float dt) {\n    namespace cfg = core::config;\n' +
     '    if (deathCam_) {   // Death strategies: the live strategy state stays frozen; FOV 85, SmoothTime 0.4\n' +
     '        deathT_ += dt;\n' +
     '        fovCur_ = fovS_.smooth(fovCur_, 85.0f, 0.4f, dt);\n' +
     '        return;\n' +
     '    }\n'],
    ['    if (spectating_) { cam.pos = specPos_; cam.yaw = specYaw_; cam.pitch = specPitch_; cam.fovXDeg = specFov_ > 0.0f ? specFov_ : fovCur_; return; }\n    cam.pos = cameraPos() + pawn_->renderOffset();',
     '    if (spectating_) { cam.pos = specPos_; cam.yaw = specYaw_; cam.pitch = specPitch_; cam.fovXDeg = specFov_ > 0.0f ? specFov_ : fovCur_; return; }\n' +
     '    if (deathCam_) { deathCameraView(cam); return; }\n' +
     '    cam.pos = cameraPos() + pawn_->renderOffset();'],
    ['    cam.fovXDeg = fovCur_;\n}\n\n} // namespace game',
     '    cam.fovXDeg = fovCur_;\n}\n\n' +
     'void PlayerController::beginDeathCamera(bool vehicle, float wreckYaw) {\n' +
     '    if (deathCam_ || spectating_) return;\n' +
     '    render::Camera from; updateCamera(from);   // the live POV: the strategy blend starts from it\n' +
     '    deathFromPos_ = from.pos; deathFromYaw_ = from.yaw; deathFromPitch_ = from.pitch;\n' +
     '    deathVehicle_ = vehicle;\n' +
     '    // The inherited CameraState: orbit rotation and orbit location offset as the live strategy left them.\n' +
     '    deathYaw_ = vehicle ? wreckYaw : viewYaw_;\n' +
     '    deathPitch_ = vehicle ? -30.0f * 0.0174533f : viewPitch_;   // TnFixedOrbitRotation PitchDeg -30, bRelativeToActor\n' +
     '    deathOff_ = core::Vec3{offset_.x - distCur_, offset_.y, offset_.z};\n' +
     '    // The death strategy\'s HmOffsetAnchorPointRelativeToActor (OffsetUsesCameraRotation): robot (0, -300, 150), vehicle (-100, 0, 50).\n' +
     '    deathOff_ = deathOff_ + (vehicle ? core::Vec3{-1.0f, 0.0f, 0.5f} : core::Vec3{0.0f, -3.0f, 1.5f});\n' +
     '    deathAnchor_ = pawn_ ? pawn_->actorLocation() : from.pos; deathAnchorOk_ = false;\n' +
     '    deathT_ = 0.0f;\n' +
     '    deathCam_ = true;\n' +
     '}\n\n' +
     'void PlayerController::deathCameraView(render::Camera& cam) const {\n' +
     '    float yaw = deathYaw_, pitch = deathPitch_;\n' +
     '    if (!deathVehicle_) {   // TnWiggler3 DefaultProfile rotation noise (pitch 0.3, yaw 0.2 deg at 1 Hz) [waveform PROV]\n' +
     '        pitch += 0.3f * 0.0174533f * std::sin(6.2831853f * deathT_);\n' +
     '        yaw += 0.2f * 0.0174533f * std::sin(6.2831853f * deathT_ + 1.0f);\n' +
     '    }\n' +
     '    const core::Vec3 F = core::forwardFromYawPitch(yaw, pitch);\n' +
     '    const core::Vec3 R = core::normalize(core::cross(core::forwardFromYawPitch(yaw, 0.0f), core::Vec3{0, 1, 0}));\n' +
     '    const core::Vec3 U = core::cross(R, F);\n' +
     '    const core::Vec3 focus = deathAnchor_;\n' +
     '    core::Vec3 to = focus + F * deathOff_.x + R * deathOff_.y + U * deathOff_.z;\n' +
     '    if (col_) {   // TnThirdPersoncollision: pull in toward the anchor in front of the first hit (as the live default)\n' +
     '        const core::Vec3 d = to - focus; const float len = core::length(d); float t;\n' +
     '        if (len > 1e-3f && col_->segmentHit(focus, to, t)) to = focus + d * (std::max(0.0f, t * len - 0.3f) / len);\n' +
     '    }\n' +
     '    // The strategy blend: the old-to-new POV delta lerps to 0 over TransitionTime (robot 1.25 s, vehicle 1.0) [curve PROV, as live].\n' +
     '    const float k = 1.0f - smoothstep01(deathT_ / (deathVehicle_ ? 1.0f : 1.25f));\n' +
     '    cam.pos = to + (deathFromPos_ - to) * k;\n' +
     '    cam.yaw = yaw + std::remainder(deathFromYaw_ - yaw, 6.2831853f) * k;\n' +
     '    cam.pitch = pitch + (deathFromPitch_ - pitch) * k;\n' +
     '    cam.fovXDeg = fovCur_;\n' +
     '}\n\n} // namespace game'],
]);

ed('src/game/World.h', [[
    '    void tickCorpses(float dt);\n',
    '    void tickCorpses(float dt);\n' +
    '    void tickDeathCamera();   // the local player\'s death camera anchor (presentation; after tickCorpses)\n'
]]);

const W = 'src/game/World.cpp';
ed(W, [
    ['    presentHook([this, q] { spawnCorpse(q); });\n',
     '    presentHook([this, q] {\n' +
     '        spawnCorpse(q);\n' +
     '        if (q.player == localPlayer_) player_.controller().beginDeathCamera(q.vehicle, std::atan2(-q.fwd.x, -q.fwd.z));\n' +
     '    });\n'],
    ['    tickCorpses(pendingDt_);   // [Gameplay] presentation only (main thread, after the deferred corpse requests ran)\n',
     '    tickCorpses(pendingDt_);   // [Gameplay] presentation only (main thread, after the deferred corpse requests ran)\n' +
     '    tickDeathCamera();\n'],
    ['void World::tickCorpses(float dt) {\n',
     '// Death camera anchor: the local corpse\'s bone (robot C_Spine01_Lumbar01_XB, vehicle C_Reference_XR) while the body stands; once\n' +
     '// the pawn is destroyed (gib) or the body is gone the anchor stays where it was (the original\'s anchor freezes). Ends at respawn.\n' +
     'void World::tickDeathCamera() {\n' +
     '    PlayerController& ctl = player_.controller();\n' +
     '    if (!ctl.deathCamera()) return;\n' +
     '    if (!localDead_) { ctl.endDeathCamera(); return; }\n' +
     '    for (auto it = corpses_.rbegin(); it != corpses_.rend(); ++it) {\n' +
     '        const Corpse& c = *it;\n' +
     '        if (c.player != localPlayer_) continue;\n' +
     '        if (c.gibbed || !c.model || c.glob.empty()) break;\n' +
     '        const int n = c.model->nodeByName(c.vehicle ? "C_Reference_XR" : "C_Spine01_Lumbar01_XB");\n' +
     '        ctl.setDeathAnchor(n >= 0 && (size_t)n < c.glob.size() ? core::transformPoint(c.world * c.glob[(size_t)n], core::Vec3{0, 0, 0}) : c.pos);\n' +
     '        break;\n' +
     '    }\n' +
     '}\n\n' +
     'void World::tickCorpses(float dt) {\n'],
]);
console.log('ok');
