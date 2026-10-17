// Camera item, part 2: the local player's camera recoil, FOV kick and per-shot shake (RE camera recoil answer, CONFIRMED unless marked H):
// - TnRecoilCameraBehavior: on a shot, OrbitRotation.Pitch += PitchAdjustmentDeg - a PERMANENT orbit step (the next frame's orbit starts
//   from it), accumulating per shot, no recovery, limited only by the orbit pitch range; FOV: every frame Fov -= FovAdjustment x L,
//   L = 1 - smoothstep(0, RecoilTime, clamp(TimeSinceLastShot - RecoilDelay, 0, RecoilTime)): the full offset at the shot, held for
//   RecoilDelay, eased back over RecoilTime; no accumulation (each shot restarts the timer and replaces the params); RecoilTime 0: no kick.
// - Per-shot shake: TnPlayerController.OnWeaponFired -> StartCameraShake(weapon CameraShakeParams) (ignored if Intensity 0) -> the same
//   TnCameraShaker as the gibbed shake: yaw / pitch = the strategy's spectra (5 Hz x 3 deg / 4 Hz x 5 deg) x linear fade over
//   ShakeFalloffDuration, roll = Roll deg x (fade over RollFalloffDuration)^2, the whole x Intensity; each shot restarts it.
// - Values per weapon from WeaponCameraTable.inc (tools/gameplay/gen_weapon_camera.py: MultiplayerData if the class authors one, else
//   PlayerData, missing fields 0).
// - Applied in every live strategy (vehicle weapons author shakes too: TankCannon 0.38 / 0.65) [H: the live vehicle strategies' behaviour
//   lists not re-read]; the gibbed shake moves onto the same parameterised shaker.
// Camera only for the local player (the pitch kick moves the local aim, as in the original: aim follows the orbit). Bots: no change.
// Run from a tree root after edit_hud_buffs.js, with src/game/WeaponCameraTable.inc generated.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
if (!fs.existsSync('src/game/WeaponCameraTable.inc')) throw new Error('generate src/game/WeaponCameraTable.inc first');
ed('src/game/PlayerController.h', [
    ['    void gibbedShake() { shakeRemain_ = 0.89f; }\n',
     '    void gibbedShake() { startShake(0.55f, 0.75f, 0.89f, 0.89f); }\n' +
     '    // TnCamera.StartShake (ignored at Intensity 0): restarts the yaw / pitch fade and the roll fade, replacing the intensity.\n' +
     '    void startShake(float intensity, float roll, float rollFalloff, float shakeFalloff) {\n' +
     '        if (intensity <= 0.0f) return;\n' +
     '        shakeInt_ = intensity; shakeRoll_ = roll; shakeDur_ = shakeFalloff; rollDur_ = rollFalloff; shakeRemain_ = shakeFalloff; rollRemain_ = rollFalloff;\n' +
     '    }\n' +
     '    // A local shot of the weapon class `weaponId` (TnWeapon<id>): the camera recoil (pitch step + FOV kick) and the per-shot shake.\n' +
     '    void onLocalShot(const char* weaponId);\n'],
    ['    float shakeRemain_ = 0.0f, shakeGenT_ = 0.0f;',
     '    float shakeInt_ = 0.55f, shakeRoll_ = 0.75f, shakeDur_ = 0.89f, rollDur_ = 0.89f, rollRemain_ = 0.0f;   // the shake in progress\n' +
     '    float fovKick_ = 0.0f, recoilTime_ = 0.0f, recoilDelay_ = 0.0f, sinceShot_ = 1e9f;                   // TnRecoilCameraBehavior FOV\n' +
     '    float shakeRemain_ = 0.0f, shakeGenT_ = 0.0f;'],
]);
ed('src/game/PlayerController.cpp', [
    ['#include "game/PlayerController.h"\n', '#include "game/PlayerController.h"\n#include <cstring>\n'],
    ['    shakeGenT_ += dt; shakeRemain_ = std::max(0.0f, shakeRemain_ - dt);\n',
     '    shakeGenT_ += dt; shakeRemain_ = std::max(0.0f, shakeRemain_ - dt); rollRemain_ = std::max(0.0f, rollRemain_ - dt); sinceShot_ += dt;\n'],
    ['    deathFromPos_ = from.pos; deathFromYaw_ = from.yaw; deathFromPitch_ = from.pitch;\n    shakeRemain_ = 0.0f;   // Activate -> Shaker.Reset\n',
     '    deathFromPos_ = from.pos; deathFromYaw_ = from.yaw; deathFromPitch_ = from.pitch;\n    shakeRemain_ = 0.0f; rollRemain_ = 0.0f;   // Activate -> Shaker.Reset\n'],
    ['    deathCam_ = false;\n    specTarget_ = true;\n    shakeRemain_ = 0.0f;   // Activate -> Shaker.Reset\n',
     '    deathCam_ = false;\n    specTarget_ = true;\n    shakeRemain_ = 0.0f; rollRemain_ = 0.0f;   // Activate -> Shaker.Reset\n'],
    ['    cam.fovXDeg = fovCur_;\n    if (strategy_ == 0) applyShake(cam);   // OverTheShoulder has TnShakeCameraBehavior [vehicle strategies: PROV, not applied]\n}\n',
     '    // TnRecoilCameraBehavior FOV: the full offset at the shot, held for RecoilDelay, eased back (smoothstep) over RecoilTime.\n' +
     '    float L = 0.0f;\n' +
     '    if (recoilTime_ > 0.0f) L = 1.0f - smoothstep01(core::clampf(sinceShot_ - recoilDelay_, 0.0f, recoilTime_) / recoilTime_);\n' +
     '    cam.fovXDeg = fovCur_ - fovKick_ * L;\n' +
     '    applyShake(cam);   // TnShakeCameraBehavior (gibbed and per-shot shakes) [H: every live strategy carries it]\n}\n'],
    ['void PlayerController::applyShake(render::Camera& cam) const {\n' +
     '    if (shakeRemain_ <= 0.0f) return;\n' +
     '    const float f = shakeRemain_ / 0.89f, k = 0.55f * 0.0174533f;   // linear fades; Intensity 0.55; degrees\n' +
     '    cam.yaw += k * 3.0f * std::sin(6.2831853f * 5.0f * shakeGenT_) * f;\n' +
     '    cam.pitch += k * 5.0f * std::sin(6.2831853f * 4.0f * shakeGenT_) * f;\n' +
     '    cam.roll += k * 0.75f * f * f;   // RollFalloff: one-sided, quadratic\n' +
     '}\n',
     'void PlayerController::applyShake(render::Camera& cam) const {\n' +
     '    if (shakeRemain_ <= 0.0f && rollRemain_ <= 0.0f) return;\n' +
     '    const float k = shakeInt_ * 0.0174533f;   // degrees x Intensity\n' +
     '    const float f = shakeDur_ > 0.0f ? shakeRemain_ / shakeDur_ : 0.0f, r = rollDur_ > 0.0f ? rollRemain_ / rollDur_ : 0.0f;   // linear fades\n' +
     '    cam.yaw += k * 3.0f * std::sin(6.2831853f * 5.0f * shakeGenT_) * f;    // the strategy\'s YawSpectrum 5 Hz x 3\n' +
     '    cam.pitch += k * 5.0f * std::sin(6.2831853f * 4.0f * shakeGenT_) * f;  // PitchSpectrum 4 Hz x 5\n' +
     '    cam.roll += k * shakeRoll_ * r * r;   // RollFalloff: one-sided, quadratic\n' +
     '}\n\n' +
     'namespace {\n' +
     'struct WeaponCameraDef { const char* id; float pitchDeg, fovAdj, recoilTime, recoilDelay, shakeInt, shakeRoll, rollFalloff, shakeFalloff; };\n' +
     'const WeaponCameraDef kWeaponCamera[] = {\n#include "game/WeaponCameraTable.inc"\n};\n' +
     '} // namespace\n\n' +
     'void PlayerController::onLocalShot(const char* weaponId) {\n' +
     '    if (!weaponId) return;\n' +
     '    for (const WeaponCameraDef& d : kWeaponCamera) {\n' +
     '        if (std::strcmp(d.id, weaponId)) continue;\n' +
     '        // OrbitRotation.Pitch += PitchAdjustmentDeg: a permanent step (the orbit, and with it the aim, keeps it).\n' +
     '        if (d.pitchDeg != 0.0f) camPitch_ = core::clampf(camPitch_ + d.pitchDeg * 0.0174533f, -75.0f * 0.0174533f, 75.0f * 0.0174533f);\n' +
     '        fovKick_ = d.fovAdj; recoilTime_ = d.recoilTime; recoilDelay_ = d.recoilDelay; sinceShot_ = 0.0f;\n' +
     '        startShake(d.shakeInt, d.shakeRoll, d.rollFalloff, d.shakeFalloff);\n' +
     '        return;\n' +
     '    }\n' +
     '}\n'],
    // The local fire sites: robot shots and vehicle-weapon shots.
    ['            if (!noRecoil) pawn_->notifyFired();   // per-shot skeletal recoil (TnRecoiler)\n',
     '            if (!noRecoil) pawn_->notifyFired();   // per-shot skeletal recoil (TnRecoiler)\n' +
     '            onLocalShot(w.def ? w.def->id : nullptr);   // TnPlayerController.OnWeaponFired: camera recoil + shake\n'],
    ['                if (vw->projectile()) world.fireWeapon(*vw, origin, dir);\n',
     '                onLocalShot(vw->def ? vw->def->id : nullptr);   // camera recoil + shake (vehicle weapons too)\n' +
     '                if (vw->projectile()) world.fireWeapon(*vw, origin, dir);\n'],
]);
console.log('ok');
