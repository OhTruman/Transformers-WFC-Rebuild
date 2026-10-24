// Playtest 3 aim, part 2 (item 30): the shot follows the camera's POV rotation, shake and wiggle included. UE3
// Pawn.GetBaseAimRotation -> Controller.GetPlayerViewPoint (the PlayerCamera's CameraCache.POV, after the camera behaviours /
// shakes) and GetWeaponStartTraceLocation the same view point [UE3 engine script; HIGH: TnPlayerPawn adds no override in the
// decompiled TransGame we have]. Ours traced along the unshaken orbit rotation (camYaw_ / camPitch_) while the drawn camera and
// the crosshair carried TnShakeCameraBehavior's per-shot shake (Rendering's audit: the crosshair is the projection centre within a
// pixel), so shots during a shake landed off the crosshair. Now robot and vehicle shots use updateCamera's rotation (the same
// one the frame draws). Deterministic: the shake / wiggle clocks advance in updateCameraStrategy on the sim step.
// WFC_AIMLOG's reference becomes the drawn rotation at the trigger. Run from the tree root.
const fs = require('fs');
const NL = String.fromCharCode(10), CR = String.fromCharCode(13);
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes(CR + NL); s = s.split(CR + NL).join(NL);
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.split(NL).join(CR + NL) : s);
}
ed('src/game/PlayerController.cpp', [
    ['                core::Vec3 camDir = core::forwardFromYawPitch(viewYaw_, viewPitch_);' + NL,
     '                render::Camera pov; updateCamera(pov);   // the camera POV the frame draws (strategy wiggle + shake): GetPlayerViewPoint' + NL +
     '                core::Vec3 camDir = core::forwardFromYawPitch(pov.yaw, pov.pitch);' + NL],
    ['            const float trigPitch = camPitch_, trigYaw = camYaw_;   // the rotation the crosshair showed at the trigger (WFC_AIMLOG)' + NL,
     '            render::Camera pov; updateCamera(pov);   // the camera POV the frame draws (shake included): GetPlayerViewPoint / GetBaseAimRotation' + NL +
     '            const float trigPitch = pov.pitch, trigYaw = pov.yaw;   // the rotation the crosshair showed at the trigger (WFC_AIMLOG)' + NL],
    ['            core::Vec3 camDir = core::forwardFromYawPitch(camYaw_, camPitch_);' + NL,
     '            core::Vec3 camDir = core::forwardFromYawPitch(pov.yaw, pov.pitch);' + NL],
    ['                LOG_INFO("AIMLOG %s shot-vs-trigger pitch %+.3f deg yaw %+.3f deg", w.def ? w.def->id : "IonBlaster",' + NL +
     '                         (sp - trigPitch) * 57.29578f, std::remainder(sy - trigYaw, 6.2831853f) * 57.29578f);' + NL,
     '                LOG_INFO("AIMLOG %s shot-vs-trigger pitch %+.3f deg yaw %+.3f deg | view-vs-orbit pitch %+.3f yaw %+.3f", w.def ? w.def->id : "IonBlaster",' + NL +
     '                         (sp - trigPitch) * 57.29578f, std::remainder(sy - trigYaw, 6.2831853f) * 57.29578f,' + NL +
     '                         (pov.pitch - camPitch_) * 57.29578f, std::remainder(pov.yaw - camYaw_, 6.2831853f) * 57.29578f);' + NL],
]);
console.log('ok');
