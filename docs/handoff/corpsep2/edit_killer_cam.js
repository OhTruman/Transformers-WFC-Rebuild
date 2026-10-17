// The killer cam (RE KILLER_CAM_RE, 3895fdb; CONFIRMED unless marked): TnPC.Dead.BeginState SetTimer(MinRespawnDelay 3.0,
// 'StartSpectating') -> PlayerSpectating: CameraStrategyManager.StartSpectating(_LastPawnWhoKilledMe) with SpectatingCameraStrategyAsset
// = SpectateOverTheShoulder_STRATEGY (always, for death spectating).
// - Every target change: SetActiveCameraStrategy + Camera.Reset(): a hard cut (TransitionTime 1.5 never seen); TnOrbitRotation.Reset:
//   OrbitRotation = (0, target yaw, 0), behind the target and level; the spectator then orbits with their own look input (default
//   look speeds, pitch -75..75). TnSpectateCameraInitialView acts only on CameraActor targets (nothing for pawns).
// - Camera: anchor = target + (0, 0, 200) UU; TnLocationOffset DefaultOrbitDistance 1600; TnScreenSpaceOffsetByPitch (150, 0,
//   150 / -35 / 150) over pitch -75..75, SmoothTime 0.3; TnFov 80 (0.4); no PCS rows ever apply (Spectating clears the PCS; the target's
//   fine aim never reaches the spectator); HmOrbitSmoother unset; third-person collision (the live pull-in). TnShake only while a
//   shake plays (none here).
// - Targets: TnPawn, alive, same team as the spectator OR the killer; AllPawns order, wrapping [order here: match player index, PROV];
//   start = the killer if valid, else the first valid pawn (suicide / KillZ / no killer); the first cycle press drops the killer
//   exemption. SwitchSpectatorTarget: LB / ']' = +1, RB / '[' = -1. A 1.0 s recheck moves to the first valid pawn when the target dies
//   or leaves; none valid: TnWorldInfo.LevelCamera, else stay [the rebuild has no LevelCamera: stays].
//   [H: the original's TnPlayerPawn cast fails for bot killers; the rebuild's bots stand in for players, so they are valid targets.]
// - Respawn: the wave timer (5 s from death), not input; RestartPlayer -> the OverTheShoulder strategy + PlayerCamera.Reset(): a hard
//   cut behind the new pawn.
// Presentation only (camera). Run from a tree root after edit_skeletal_gibs.js.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}

ed('src/platform/Input.h', [[
    '    RenderMode,   // PC ADAPTATION: F9 cycles the Ray Tracing mode (Off / Faithful / Realistic) for side-by-side comparison\n',
    '    RenderMode,   // PC ADAPTATION: F9 cycles the Ray Tracing mode (Off / Faithful / Realistic) for side-by-side comparison\n' +
    '    SpectateNext, SpectatePrev,   // SwitchSpectatorTarget +1 / -1 while spectating: keyboard \']\' / \'[\' (pad LB / RB arrive as UiKey bits)\n'
]]);
ed('src/platform/win32/Win32Window.cpp', [[
    '        case Button::RenderMode:   return VK_F9;    // PC ADAPTATION: the Ray Tracing mode cycle\n',
    '        case Button::RenderMode:   return VK_F9;    // PC ADAPTATION: the Ray Tracing mode cycle\n' +
    '        case Button::SpectateNext: return VK_OEM_6;   // \']\' SwitchSpectatorTarget +1 [CONF RE KILLER_CAM_RE]\n' +
    '        case Button::SpectatePrev: return VK_OEM_4;   // \'[\' -1\n'
]]);

ed('src/game/PlayerController.h', [
    ['    bool deathCamera() const { return deathCam_; }\n',
     '    bool deathCamera() const { return deathCam_; }\n' +
     '    // SpectateOverTheShoulder_STRATEGY on a target pawn (the killer cam): begin / re-target = a hard cut behind the target, level.\n' +
     '    void beginSpectateTarget(float targetYaw);\n' +
     '    void setSpectateAnchor(const core::Vec3& targetActorLoc) { specAnchor_ = targetActorLoc; }\n' +
     '    void endSpectateTarget() { specTarget_ = false; }\n' +
     '    bool spectatingTarget() const { return specTarget_; }\n' +
     '    int takeSpectateCycle() { const int c = specCycle_; specCycle_ = 0; return c; }   // SwitchSpectatorTarget presses since the last call\n'],
    ['    void deathCameraView(render::Camera& cam) const;\n',
     '    void deathCameraView(render::Camera& cam) const;\n' +
     '    bool specTarget_ = false; core::Vec3 specAnchor_{0, 0, 0}, specOff_{1.5f, 0.0f, -0.35f}; int specCycle_ = 0; uint32_t specUiPrev_ = 0;\n' +
     '    C2Smoother specOffXS_, specOffZS_;\n' +
     '    void spectateView(render::Camera& cam) const;\n'],
]);

ed('src/game/PlayerController.cpp', [
    ['    if (deathCam_) {   // Death strategies: the live strategy state stays frozen; FOV 85, SmoothTime 0.4\n',
     '    {   // SwitchSpectatorTarget input (LB / \']\' = +1, RB / \'[\' = -1), edges of the UiKey bits and the bound keys\n' +
     '        const uint32_t lb = 1u << (int)platform::UiKey::LB, rb = 1u << (int)platform::UiKey::RB;\n' +
     '        if (((in.uiDown & lb) && !(specUiPrev_ & lb)) || in.wasPressed(platform::Button::SpectateNext)) ++specCycle_;\n' +
     '        if (((in.uiDown & rb) && !(specUiPrev_ & rb)) || in.wasPressed(platform::Button::SpectatePrev)) --specCycle_;\n' +
     '        specUiPrev_ = in.uiDown;\n' +
     '        if (!specTarget_) specCycle_ = 0;\n' +
     '    }\n' +
     '    if (specTarget_) {   // SpectateOverTheShoulder: FOV 80 (0.4); screen-space offset (150, 0, 150 / -35 / 150) by pitch, SmoothTime 0.3\n' +
     '        fovCur_ = fovS_.smooth(fovCur_, 80.0f, 0.4f, dt);\n' +
     '        const float f = pitchFraction(camPitch_ * 57.29578f, -75.0f, 75.0f);\n' +
     '        specOff_.x = specOffXS_.smooth(specOff_.x, 1.5f, 0.3f, dt);\n' +
     '        specOff_.z = specOffZS_.smooth(specOff_.z, curve3(1.5f, -0.35f, 1.5f, f), 0.3f, dt);\n' +
     '        return;\n' +
     '    }\n' +
     '    if (deathCam_) {   // Death strategies: the live strategy state stays frozen; FOV 85, SmoothTime 0.4\n'],
    ['    if (deathCam_) { deathCameraView(cam); return; }\n',
     '    if (specTarget_) { spectateView(cam); return; }\n' +
     '    if (deathCam_) { deathCameraView(cam); return; }\n'],
    ['void PlayerController::beginDeathCamera(bool vehicle, float wreckYaw) {\n',
     'void PlayerController::beginSpectateTarget(float targetYaw) {\n' +
     '    // StartSpectating / SwitchSpectatorTarget -> Camera.Reset(): the strategy\'s state starts over (a hard cut).\n' +
     '    deathCam_ = false;\n' +
     '    specTarget_ = true;\n' +
     '    camYaw_ = targetYaw; camPitch_ = 0.0f;   // TnOrbitRotation.Reset: (0, target yaw, 0)\n' +
     '    specOff_ = core::Vec3{1.5f, 0.0f, -0.35f}; specOffXS_.reset(); specOffZS_.reset();\n' +
     '    fovCur_ = 80.0f; fovS_.reset();\n' +
     '}\n\n' +
     'void PlayerController::spectateView(render::Camera& cam) const {\n' +
     '    const float yaw = camYaw_, pitch = core::clampf(camPitch_, -75.0f * 0.0174533f, 75.0f * 0.0174533f);\n' +
     '    const core::Vec3 F = core::forwardFromYawPitch(yaw, pitch);\n' +
     '    const core::Vec3 R = core::normalize(core::cross(core::forwardFromYawPitch(yaw, 0.0f), core::Vec3{0, 1, 0}));\n' +
     '    const core::Vec3 U = core::cross(R, F);\n' +
     '    const core::Vec3 focus = specAnchor_ + core::Vec3{0, 2.0f, 0};   // HmOffsetAnchorPointRelativeToActor (0, 0, 200)\n' +
     '    core::Vec3 to = focus + F * (specOff_.x - 16.0f) + R * specOff_.y + U * specOff_.z;   // TnLocationOffset 1600\n' +
     '    if (col_) {   // TnThirdPersoncollision: the live pull-in toward the anchor in front of the first hit\n' +
     '        const core::Vec3 d = to - focus; const float len = core::length(d); float t;\n' +
     '        if (len > 1e-3f && col_->segmentHit(focus, to, t)) to = focus + d * (std::max(0.0f, t * len - 0.3f) / len);\n' +
     '    }\n' +
     '    cam.pos = to; cam.yaw = yaw; cam.pitch = pitch; cam.fovXDeg = fovCur_; cam.roll = 0.0f;\n' +
     '}\n\n' +
     'void PlayerController::beginDeathCamera(bool vehicle, float wreckYaw) {\n'],
]);

ed('src/game/World.h', [
    ['    void tickDeathCamera();   // the local player\'s death camera anchor (presentation; after tickCorpses)\n',
     '    void tickDeathCamera(float dt);   // the local player\'s death camera anchor and the killer cam (presentation; after tickCorpses)\n' +
     '    int specTarget_ = -1, specKiller_ = -1; bool specKillerOk_ = true; float specRecheck_ = 0.0f;   // the killer cam\'s target state\n' +
     '    bool specValid(int p) const;\n'],
]);

const W = 'src/game/World.cpp';
ed(W, [
    ['    tickDeathCamera();\n', '    tickDeathCamera(pendingDt_);\n'],
    ['void World::tickDeathCamera() {\n    PlayerController& ctl = player_.controller();\n    if (!ctl.deathCamera()) return;\n    if (!localDead_) { ctl.endDeathCamera(); return; }\n',
     '// A valid spectator target: a spawned, live pawn, on the spectator\'s team or the killer (until the first cycle press).\n' +
     'bool World::specValid(int p) const {\n' +
     '    if (p < 0 || p == localPlayer_ || (size_t)p >= match_.players().size() || !match_.players()[(size_t)p].alive) return false;\n' +
     '    if (!participantPawn(p)) return false;\n' +
     '    return match_.sameTeam(p, localPlayer_) || (specKillerOk_ && p == specKiller_);\n' +
     '}\n\n' +
     'void World::tickDeathCamera(float dt) {\n' +
     '    PlayerController& ctl = player_.controller();\n' +
     '    if (!localDead_) {   // respawned: RestartPlayer -> OverTheShoulder + PlayerCamera.Reset (a hard cut)\n' +
     '        if (ctl.deathCamera()) ctl.endDeathCamera();\n' +
     '        if (ctl.spectatingTarget()) { ctl.endSpectateTarget(); if (renderer_) renderer_->notifyCameraCut("spectate end"); }\n' +
     '        specTarget_ = specKiller_ = -1; specKillerOk_ = true;\n' +
     '        return;\n' +
     '    }\n' +
     '    // PlayerSpectating after MinRespawnDelay 3.0 s: the killer cam (SpectateOverTheShoulder on the target).\n' +
     '    if (matchActive_ && localPlayer_ >= 0 && match_.spectating(localPlayer_)) {\n' +
     '        const int n = (int)match_.players().size();\n' +
     '        auto firstValid = [&](int from, int step) { for (int k = 1; k <= n; ++k) { const int p = ((from + step * k) % n + n) % n; if (specValid(p)) return p; } return -1; };\n' +
     '        auto cut = [&](int p) {\n' +
     '            specTarget_ = p;\n' +
     '            if (const Character* t = participantPawn(p)) { ctl.beginSpectateTarget(t->yaw()); ctl.setSpectateAnchor(t->actorLocation()); }\n' +
     '            if (renderer_) renderer_->notifyCameraCut("spectate target");\n' +
     '        };\n' +
     '        if (!ctl.spectatingTarget()) {   // StartSpectating(_LastPawnWhoKilledMe)\n' +
     '            specKiller_ = -1; specKillerOk_ = true; specRecheck_ = 0.0f;\n' +
     '            for (auto it = match_.gameplayEvents().rbegin(); it != match_.gameplayEvents().rend(); ++it)\n' +
     '                if (it->victim == localPlayer_ && (it->type == GameplayEventType::Kill || it->type == GameplayEventType::Suicide || it->type == GameplayEventType::EnvironmentDeath)) {\n' +
     '                    if (it->instigator != localPlayer_) specKiller_ = it->instigator;\n' +
     '                    break;\n' +
     '                }\n' +
     '            const int p = specValid(specKiller_) ? specKiller_ : firstValid(-1, 1);\n' +
     '            if (p >= 0) cut(p);\n' +
     '        } else {\n' +
     '            if (const int c = ctl.takeSpectateCycle()) {   // SwitchSpectatorTarget: the first press drops the killer exemption\n' +
     '                specKillerOk_ = false;\n' +
     '                const int p = firstValid(specTarget_, c > 0 ? 1 : -1);\n' +
     '                if (p >= 0 && p != specTarget_) cut(p);\n' +
     '            }\n' +
     '            if ((specRecheck_ += dt) >= 1.0f) {   // the 1.0 s recheck: a dead / gone target moves to the first valid pawn\n' +
     '                specRecheck_ = 0.0f;\n' +
     '                if (!specValid(specTarget_)) { const int p = firstValid(-1, 1); if (p >= 0) cut(p); }   // none: stay (no LevelCamera)\n' +
     '            }\n' +
     '            if (const Character* t = specTarget_ >= 0 && specValid(specTarget_) ? participantPawn(specTarget_) : nullptr) ctl.setSpectateAnchor(t->actorLocation());\n' +
     '        }\n' +
     '        if (ctl.spectatingTarget()) return;\n' +
     '    }\n' +
     '    if (!ctl.deathCamera()) return;\n'],
]);
console.log('ok');
