// Robot hit reactions (RE CAMPAIGN_OMEGA_HITREACT_RE section 2, C: TnHitReactionAnimator on the additive service): every damaging hit
// on a robot-form pawn whose damage type ShouldCauseHitReaction plays the full-body additive Normal F / B (Turret F / B while holding a
// heavy weapon), front when the momentum points along the pawn's facing; blend in 0.05 s, out 0.4 s; no threshold, no cooldown (each
// hit restarts the clip); players and AI alike; vehicles have none. Anchor edits on the head.
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
    ['        int equipClip = -1, unequipClip = -1;   // ADD_Shooting_equip_weapon / ADD_Shooting_UnEquip_Weapon (additive, upper body)\n',
     '        int equipClip = -1, unequipClip = -1;   // ADD_Shooting_equip_weapon / ADD_Shooting_UnEquip_Weapon (additive, upper body)\n' +
     '        int hitClip[4] = {-1, -1, -1, -1};       // RobotHitReactions: Normal F / B, Turret F / B (ADD_HitReaction_*, additive, full body)\n'],
    ['    float unequipW_ = 0.0f, unequipT_ = 0.0f, equipW_ = 0.0f, equipT_ = 0.0f; int switchPhase_ = 0;   // the weapon switch overlay\n',
     '    float unequipW_ = 0.0f, unequipT_ = 0.0f, equipW_ = 0.0f, equipT_ = 0.0f; int switchPhase_ = 0;   // the weapon switch overlay\n' +
     '    int hitReact_ = -1; float hitReactT_ = 0.0f, hitReactW_ = 0.0f;   // the hit reaction overlay (index into robotRig_.hitClip)\n'],
    ['    void beginTransform();\n',
     '    // TnHitReactionAnimator.Play: a hit from `momentum` (world direction of the hit\'s push). Robot form only; restarts on every hit.\n' +
     '    void playHitReaction(const core::Vec3& momentum);\n' +
     '    void beginTransform();\n'],
]);
ed('src/game/Character.cpp', [
    ['    R.unequipClip = mdl.clipByName("ADD_Shooting_UnEquip_Weapon");\n',
     '    R.unequipClip = mdl.clipByName("ADD_Shooting_UnEquip_Weapon");\n' +
     '    R.hitClip[0] = mdl.clipByName("ADD_HitReaction_React_F");\n' +
     '    R.hitClip[1] = mdl.clipByName("ADD_HitReaction_React_B");\n' +
     '    R.hitClip[2] = mdl.clipByName("ADD_HitReaction_Turret_React_F");\n' +
     '    R.hitClip[3] = mdl.clipByName("ADD_HitReaction_Turret_React_B");\n'],
    ['    // 3b) Melee action layer: the attack clip over locomotion',
     '    // 3a) Hit reaction [C, RE CAMPAIGN_OMEGA_HITREACT_RE 2]: the additive clip over the whole body, blend in 0.05 s, out 0.4 s.\n' +
     '    if (hitReact_ >= 0 && robotRig && R.hitClip[hitReact_] >= 0) {\n' +
     '        const int ci = R.hitClip[hitReact_];\n' +
     '        const float len = mdl.clips[(size_t)ci].duration;\n' +
     '        hitReactT_ += dt;\n' +
     '        if (hitReactT_ >= len) hitReact_ = -1;\n' +
     '        else {\n' +
     '            hitReactW_ = std::min({1.0f, hitReactT_ / 0.05f, (len - hitReactT_) / 0.4f});\n' +
     '            assets::samplePose(mdl, ci, hitReactT_, false, layerPose_, /*additive*/ true);\n' +
     '            traceClip(mdl, ci, hitReactT_, false, hitReactW_);\n' +
     '            assets::addPose(finalPose_, layerPose_, hitReactW_);\n' +
     '        }\n' +
     '    }\n\n' +
     '    // 3b) Melee action layer: the attack clip over locomotion'],
]);
let c = fs.readFileSync('src/game/Character.cpp', 'utf8');
c += '\n' +
'namespace game {\n' +
'// TnHitReactionAnimator.Play [C]: front when (Momentum << Rotation).X > 0, Turret while holding a heavy weapon, else Normal; the\n' +
'// gate (not interacting, not WeaponType 7) is the caller\'s; vehicles and transitions play nothing.\n' +
'void Character::playHitReaction(const core::Vec3& momentum) {\n' +
'    if (form_ != Form::Robot || trans_ != Transition::None) return;\n' +
'    const float fx = -std::sin(yaw()), fz = -std::cos(yaw());   // the facing (forwardFromYawPitch convention)\n' +
'    const bool front = momentum.x * fx + momentum.z * fz > 0.0f;\n' +
'    hitReact_ = (carryingHeavy_ != 0 ? 2 : 0) + (front ? 0 : 1);\n' +
'    hitReactT_ = 0.0f;\n' +
'    static int logged = 0;   // the first few, for the logs\n' +
'    if (logged < 3) { ++logged; LOG_INFO("hit reaction: %s (%s)", front ? "front" : "back", carryingHeavy_ ? "turret" : "normal"); }\n' +
'}\n' +
'} // namespace game\n';
fs.writeFileSync('src/game/Character.cpp', c);
ed('src/game/World.cpp', [
    ['    float applied = h->applyDamage(amount);\n    if (applied > 0.0f) {                                   // TnPlayerPawn.TakeDamage -> ExposeSelf\n',
     '    float applied = h->applyDamage(amount);\n' +
     '    // TnPawn.PlayHit -> the robot form\'s hit reaction, unless the damage type has ShouldCauseHitReaction false [C, RE HITREACT 2]\n' +
     '    if (applied > 0.0f && instigator >= 0 && instigator != victim) {\n' +
     '        static const char* const kNoReact[] = {"AudioBlast", "DecoyDot", "Drain", "TransformDisruptorDot", "HealType"};   // Drain covers DownedDrain\n' +
     '        bool react = !(damageType.size() >= 13 && damageType.compare(damageType.size() - 13, 13, ".TnDamageType") == 0) && damageType != "TnDamageType";\n' +
     '        for (const char* k : kNoReact) if (damageType.find(k) != std::string::npos) react = false;\n' +
     '        Character* vc = victim == localPlayer_ ? &player_.pawn() : (opp ? &opp->pawn() : nullptr);\n' +
     '        if (react && vc) vc->playHitReaction(vc->actorLocation() - match_.playerLocation(instigator));\n' +
     '    }\n' +
     '    if (applied > 0.0f) {                                   // TnPlayerPawn.TakeDamage -> ExposeSelf\n'],
]);
console.log('ok');
