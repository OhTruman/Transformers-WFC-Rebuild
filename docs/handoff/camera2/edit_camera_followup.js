// Camera recoil follow-up (RE WEAPON_CAMERA_SHAKE_RE.md section 5; the kick / shake itself is integration 1f1d4e17):
// (a) TnBuffReduceRecoil (TnSkillReduceRecoil.BuffToApply): PitchAdjustmentDeg, FovAdjustment and RecoilTime x 0.35 (delay and shake
//     unaffected). Skills are not applied elsewhere in the rebuild yet: this reads the local selection's "ReduceRecoil" skill at spawn.
// (b) Section 5.6: the wheeled Car_ / Truck_ driving strategies have no recoil behaviour (the shake still plays); the hover / flying /
//     tank / turret strategies keep both. (First person and the guided missile have no fire path in the rebuild.)
// (c) The table from authored.db (tools/gameplay/gen_weapon_camera.py): every weapon class, e.g. TnWeaponEmpShotgun (PlayerData: 4.0 deg),
//     missing from data/gameplay/weapons_mp.json. Ids match case-insensitively (class TnWeaponEmpShotgun vs our weapon id EMPShotgun).
// Camera only: SIMHASH identical. Run from a tree root with src/game/WeaponCameraTable.inc and tools/gameplay/gen_weapon_camera.py
// copied from docs/handoff/camera2/.
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
    ['    void onLocalShot(const char* weaponId);\n',
     '    void onLocalShot(const char* weaponId);\n' +
     '    // TnBuffReduceRecoil (the selection\'s ReduceRecoil skill): x 0.35 on the pitch step, the FOV kick and RecoilTime; 1 without.\n' +
     '    void setRecoilScale(float s) { recoilScale_ = s; }\n'],
    ['    float fovKick_ = 0.0f, recoilTime_ = 0.0f, recoilDelay_ = 0.0f, sinceShot_ = 1e9f;',
     '    float recoilScale_ = 1.0f;\n' +
     '    float fovKick_ = 0.0f, recoilTime_ = 0.0f, recoilDelay_ = 0.0f, sinceShot_ = 1e9f;'],
]);
ed('src/game/PlayerController.cpp', [
    ['        if (std::strcmp(d.id, weaponId)) continue;\n',
     '        {   // class suffix vs weapon id, case-insensitively (TnWeaponEmpShotgun / EMPShotgun)\n' +
     '            const char* x = d.id; const char* y = weaponId;\n' +
     '            while (*x && *y && std::tolower((unsigned char)*x) == std::tolower((unsigned char)*y)) { ++x; ++y; }\n' +
     '            if (*x || *y) continue;\n' +
     '        }\n' +
     '        // Section 5.6: the wheeled driving strategies (Car_ / Truck_) carry no TnRecoilCameraBehavior; the shake still plays.\n' +
     '        const bool wheeledDrive = pawn_ && pawn_->moveForm() == Form::Vehicle && pawn_->vehicleParams().form != VehicleFormType::Jet && pawn_->vehicleState().driving;\n' +
     '        if (wheeledDrive) { startShake(d.shakeInt, d.shakeRoll, d.rollFalloff, d.shakeFalloff); return; }\n'],
    ['        if (d.pitchDeg != 0.0f) camPitch_ = core::clampf(camPitch_ + d.pitchDeg * 0.0174533f, -75.0f * 0.0174533f, 75.0f * 0.0174533f);\n' +
     '        fovKick_ = d.fovAdj; recoilTime_ = d.recoilTime; recoilDelay_ = d.recoilDelay; sinceShot_ = 0.0f;\n',
     '        if (d.pitchDeg != 0.0f) camPitch_ = core::clampf(camPitch_ + d.pitchDeg * recoilScale_ * 0.0174533f, -75.0f * 0.0174533f, 75.0f * 0.0174533f);\n' +
     '        fovKick_ = d.fovAdj * recoilScale_; recoilTime_ = d.recoilTime * recoilScale_; recoilDelay_ = d.recoilDelay; sinceShot_ = 0.0f;   // ReduceRecoil x 0.35\n'],
    ['#include <cstring>\n', '#include <cctype>\n#include <cstring>\n'],
]);
ed('src/game/World.cpp', [[
    '                    recordSpawnEvent(localPlayer_);\n',
    '                    {   // TnSkillReduceRecoil -> TnBuffReduceRecoil: the local camera recoil x 0.35 (the only skill the camera reads)\n' +
    '                        bool rr = false;\n' +
    '                        for (const std::string& sk : mp.selection.skills) rr |= sk == "ReduceRecoil";\n' +
    '                        player_.controller().setRecoilScale(rr ? 0.35f : 1.0f);\n' +
    '                    }\n' +
    '                    recordSpawnEvent(localPlayer_);\n'
]]);
console.log('ok');
