// Death camera roll (needs render::Camera::roll, Rendering e5fc6d65 on agents/rendering-rt2: radians about the view axis after yaw /
// pitch, UE Rotator.Roll sign). Death_Vehicle_STRATEGY TnDutchRollCameraBehavior RollDeg -1; the robot Death strategy's TnWiggler3
// roll 0.1 deg at 1 Hz [waveform PROV]. Every other view sets roll 0 (the application reuses one render::Camera across frames, so a
// death roll must not survive the respawn). Run from a tree root after edit_death_camera.js, on a head with e5fc6d65.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
if (!fs.readFileSync('src/render/Camera.h', 'utf8').includes('roll')) throw new Error('render::Camera has no roll field: apply Rendering e5fc6d65 first');
ed('src/game/PlayerController.cpp', [
    ['void PlayerController::updateCamera(render::Camera& cam) const {\n    if (!pawn_) return;\n',
     'void PlayerController::updateCamera(render::Camera& cam) const {\n    if (!pawn_) return;\n    cam.roll = 0.0f;   // only the death strategies roll\n'],
    ['    cam.fovXDeg = fovCur_;\n}\n\n} // namespace game',
     '    cam.fovXDeg = fovCur_;\n' +
     '    cam.roll = deathVehicle_ ? -1.0f * 0.0174533f                                            // TnDutchRoll RollDeg -1\n' +
     '                             : 0.1f * 0.0174533f * std::sin(6.2831853f * deathT_ + 2.0f);   // TnWiggler3 roll 0.1 deg, 1 Hz\n' +
     '    cam.roll *= 1.0f - k;   // blended in with the strategy transition (the live view has no roll)\n' +
     '}\n\n} // namespace game'],
]);
console.log('ok');
