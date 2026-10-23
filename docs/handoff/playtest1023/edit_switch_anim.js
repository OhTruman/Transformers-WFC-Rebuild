// Playtest 2026-10-23 #15 (weapon switching looks instant), RE PLAYTEST_2026-10-23_RE.md §#15 (C): a switch is the old weapon's
// PutDownTime then the new weapon's EquipTime, unusable throughout (already simulated: tickWeaponSwitch / weaponUsable); the
// presentation was missing. Now: the additive upper-body ADD_Shooting_UnEquip_Weapon from the start of the put-down (blend in 0.3 s,
// out 0.0 at the swap) and ADD_Shooting_equip_weapon from the swap (blend in 0.2 s, out 0.3 s), robot form.
// Presentation only (the pose of the upper body during a switch; no sim reads it but socket presentation). Run from a tree root.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/Character.h', [
    ['        int reloadClip = -1, idleClip = -1, landClip = -1, land2Clip = -1, land3Clip = -1;   // Nav_Land / _02 / _03\n',
     '        int reloadClip = -1, idleClip = -1, landClip = -1, land2Clip = -1, land3Clip = -1;   // Nav_Land / _02 / _03\n' +
     '        int equipClip = -1, unequipClip = -1;   // ADD_Shooting_equip_weapon / ADD_Shooting_UnEquip_Weapon (additive, upper body)\n'],
    ['    float reloadW_ = 0.0f, reloadT_ = 0.0f;\n',
     '    float reloadW_ = 0.0f, reloadT_ = 0.0f;\n' +
     '    float unequipW_ = 0.0f, unequipT_ = 0.0f, equipW_ = 0.0f, equipT_ = 0.0f; int switchPhase_ = 0;   // the weapon switch overlay\n'],
]);
ed('src/game/Character.cpp', [
    ['    R.reloadClip = mdl.clipByName("Shooting_Reload_IonBlaster_ROBO");\n',
     '    R.reloadClip = mdl.clipByName("Shooting_Reload_IonBlaster_ROBO");\n' +
     '    R.equipClip = mdl.clipByName("ADD_Shooting_equip_weapon");\n' +
     '    R.unequipClip = mdl.clipByName("ADD_Shooting_UnEquip_Weapon");\n'],
    ['        assets::blendPoseMeshSpace(mdl, finalPose_, layerPose_, reloadW_, R.upperMask, finalPose_);\n    }\n',
     '        assets::blendPoseMeshSpace(mdl, finalPose_, layerPose_, reloadW_, R.upperMask, finalPose_);\n    }\n' +
     '    {   // Weapon switch [CONF RE playtest #15]: ADD_Shooting_UnEquip_Weapon while putting down (blend 0.3 in, 0.0 out at the swap),\n' +
     '        // ADD_Shooting_equip_weapon while equipping (0.2 in, 0.3 out); additive over the upper body.\n' +
     '        const int phase = switchTo_ < 0 ? 0 : (activeWeapon_ != switchTo_ ? 1 : 2);\n' +
     '        if (phase == 1 && switchPhase_ != 1) unequipT_ = 0.0f;\n' +
     '        if (phase == 2 && switchPhase_ != 2) equipT_ = 0.0f;\n' +
     '        switchPhase_ = phase;\n' +
     '        unequipT_ += dt; equipT_ += dt;\n' +
     '        unequipW_ = phase == 1 ? std::min(1.0f, unequipW_ + dt / 0.3f) : 0.0f;\n' +
     '        equipW_ = phase == 2 ? std::min(1.0f, equipW_ + dt / 0.2f) : std::max(0.0f, equipW_ - dt / 0.3f);\n' +
     '        if (robotRig && unequipW_ > 0.0f && R.unequipClip >= 0) {\n' +
     '            assets::samplePose(mdl, R.unequipClip, unequipT_, false, layerPose_, /*additive*/ true);\n' +
     '            traceClip(mdl, R.unequipClip, unequipT_, false, unequipW_);\n' +
     '            assets::addPose(finalPose_, layerPose_, unequipW_, &R.upperMask);\n' +
     '        }\n' +
     '        if (robotRig && equipW_ > 0.0f && R.equipClip >= 0) {\n' +
     '            assets::samplePose(mdl, R.equipClip, equipT_, false, layerPose_, /*additive*/ true);\n' +
     '            traceClip(mdl, R.equipClip, equipT_, false, equipW_);\n' +
     '            assets::addPose(finalPose_, layerPose_, equipW_, &R.upperMask);\n' +
     '        }\n' +
     '    }\n'],
]);
console.log('ok');
