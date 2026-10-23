// Playtest 2026-10-23 #11 (spread / shots not matching the aim), RE PLAYTEST_2026-10-23_RE.md §#11 + the follow-up (C, bytecode):
// 1. The random offset is the stock UE3 Weapon.AddSpread: RandY = FRand() - 0.5, RandZ = Sqrt(0.5 - RandY^2) * (FRand() - 0.5),
//    aim = X + RandY*S*Y + RandZ*S*Z (about a +-0.5*S disc). Ours drew +-1*S per axis independently: about 2x wider (sqrt 2 more on the
//    diagonal) - the "shots nowhere near the crosshair".
// 2. S = the weapon's CurrentSpread at the shot (IncrementSpread runs once per fire call, AFTER the shot: ours sampled after onFired's
//    increment) x the airborne multiplier x FineAimSpreadModifier when fine aiming - both robot form only (vehicle spread unmodified);
//    the FIRING weapon's spread (ours always read the robot weapon, also for vehicle shots).
// 3. Shotgun / EMPShotgun use a fixed SpreadPattern: pellet k = X + P[k].x*M*Y + P[k].y*M*Z, M = GetSpread() / GetInitialSpread() (no
//    randomness).
// 4. Projectile weapons take the same AddSpread on their fire direction (ProjectileFire -> GetAdjustedAim).
// Sim change (every shot's direction): bot accuracy / TTK re-baseline. Run from a tree root (integration f34817ac+).
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/Weapon.h', [
    ['    float spread        = 0.08f;    // current bloom\n',
     '    float spread        = 0.08f;    // current bloom\n' +
     '    float shotSpread    = 0.08f;    // CurrentSpread at the last shot (before its IncrementSpread): the value that shot\'s AddSpread uses\n'],
    ['        sinceFire = 0.0f;\n        spread = spread + spreadPerShot < spreadMax ? spread + spreadPerShot : spreadMax;\n',
     '        sinceFire = 0.0f;\n        shotSpread = spread;                    // this shot uses the spread before its increment\n' +
     '        spread = spread + spreadPerShot < spreadMax ? spread + spreadPerShot : spreadMax;\n'],
]);
ed('src/game/Character.h', [[
    '    float effectiveSpread() const {\n        return weapon().spread * airSpreadMult_ * (fineAiming_ ? weapon().fineAimSpreadMult : 1.0f);\n    }\n',
    '    float effectiveSpread() const {\n        return weapon().spread * airSpreadMult_ * (fineAiming_ ? weapon().fineAimSpreadMult : 1.0f);\n    }\n' +
    '    // The S of a shot of weapon w (TnPlayerPawn.GetWeaponSpreadModifier): w\'s CurrentSpread at the shot x, in robot form only, the\n' +
    '    // airborne multiplier x FineAimSpreadModifier when fine aiming; vehicle weapons are unmodified [CONF RE playtest #11].\n' +
    '    float shotSpreadFor(const Weapon& w) const {\n' +
    '        const bool robot = moveForm() == Form::Robot && &w != vehicleWeaponPtr();\n' +
    '        return w.shotSpread * (robot ? airSpreadMult_ * (fineAiming_ ? w.fineAimSpreadMult : 1.0f) : 1.0f);\n' +
    '    }\n' +
    '    const Weapon* vehicleWeaponPtr() const;\n'
]]);
ed('src/game/World.h', [[
    '    void setPelletIndex(int k) { pellet_ = k; }\n',
    '    void setPelletIndex(int k) { pellet_ = k; }\n' +
    '    // Weapon.AddSpread (stock UE3; TnWeapon\'s SpreadPattern for the shotguns): the shot direction for spread S of weapon w, pellet pellet_.\n' +
    '    core::Vec3 addSpread(const core::Vec3& dir, float S, const Weapon& w) const;\n'
]]);
// Character::vehicleWeaponPtr (vehicleWeapon() is non-const in Character).
let cc = fs.readFileSync('src/game/Character.cpp', 'utf8');
if (!cc.includes('Character::vehicleWeaponPtr')) {
    const crlf = cc.includes('\r\n');
    cc = cc.replace(/\r\n/g, '\n') + '\nconst game::Weapon* game::Character::vehicleWeaponPtr() const { return const_cast<game::Character*>(this)->vehicleWeapon(); }\n';
    fs.writeFileSync('src/game/Character.cpp', crlf ? cc.replace(/\n/g, '\r\n') : cc);
}
ed('src/game/World.cpp', [
    // The local instant-hit trace.
    ['        auto rf = [] { return core::simRandSigned(); };   // simulation stream (not shared with FX)\n' +
     '        // Fine aim scales spread by the weapon\'s FineAimSpreadModifier (Ion Blaster 0.5) [CONF data].\n' +
     '        float spread = player_.pawn().effectiveSpread();   // bloom x airborne x fine aim (HmWeapon.GetSpread)\n' +
     '        dir = core::normalize(dir + rt * (rf() * spread) + u2 * (rf() * spread));\n',
     '        (void)rt; (void)u2;\n' +
     '        dir = addSpread(dir, player_.pawn().shotSpreadFor(w), w);   // Weapon.AddSpread [CONF RE playtest #11]\n'],
    // The local projectile.
    ['            spawnProjectile(o, d * w.projSpeed, w, localPlayer_);   // Spawn at RealStartLoc (callers pass the muzzle)\n',
     '            spawnProjectile(o, addSpread(d, player_.pawn().shotSpreadFor(w), w) * w.projSpeed, w, localPlayer_);   // RealStartLoc; AddSpread\n'],
    // Seat projectiles.
    ['            spawnProjectile(origin, core::normalize(aimPt - origin) * w.projSpeed, w, p);\n',
     '            spawnProjectile(origin, addSpread(core::normalize(aimPt - origin), pc.shotSpreadFor(w), w) * w.projSpeed, w, p);\n'],
    ['        spawnProjectile(muzzle, d * w.projSpeed, w, p);\n',
     '        spawnProjectile(muzzle, addSpread(d, pc.shotSpreadFor(w), w) * w.projSpeed, w, p);\n'],
    // The shared AddSpread.
    ['void World::spawnProjectile(const core::Vec3& pos, const core::Vec3& vel, const Weapon& w, int instigator) {\n',
     'core::Vec3 World::addSpread(const core::Vec3& dirIn, float S, const Weapon& w) const {\n' +
     '    const core::Vec3 X = core::normalize(dirIn);\n' +
     '    core::Vec3 Y = core::cross(X, core::Vec3{0, 1, 0});   // right\n' +
     '    if (core::length(Y) < 1e-5f) Y = core::Vec3{1, 0, 0};\n' +
     '    Y = core::normalize(Y);\n' +
     '    const core::Vec3 Z = core::normalize(core::cross(Y, X));   // up\n' +
     '    // TnWeapon.AddSpread: the weapons with a SpreadPattern (fixed pellet offsets, scaled by GetSpread / GetInitialSpread).\n' +
     '    static const float kShotgun[8][2] = {{0, 0}, {.025f, -.025f}, {-.02f, .02f}, {.03f, .015f}, {-.035f, -.04f}, {.013f, .04f}, {-.013f, -.03f}, {-.03f, -.015f}};\n' +
     '    static const float kEmp[13][2] = {{0, 0}, {.015f, -.015f}, {-.01f, .01f}, {.02f, .015f}, {-.025f, -.035f}, {-.02f, -.03f}, {-.05f, -.02f},\n' +
     '                                      {.035f, .02f}, {-.035f, .01f}, {.015f, -.02f}, {0, .04f}, {-.045f, -.035f}, {0, -.015f}};\n' +
     '    const char* id = w.def ? w.def->id : "";\n' +
     '    const float (*pat)[2] = nullptr; int n = 0;\n' +
     '    if (!std::strcmp(id, "Shotgun")) { pat = kShotgun; n = 8; }\n' +
     '    else if (!std::strcmp(id, "EmpShotgun")) { pat = kEmp; n = 13; }\n' +
     '    if (pat && pellet_ >= 0 && pellet_ < n) {\n' +
     '        const float M = w.spreadMin > 1e-6f ? S / w.spreadMin : 1.0f;\n' +
     '        return core::normalize(X + Y * (pat[pellet_][0] * M) + Z * (pat[pellet_][1] * M));\n' +
     '    }\n' +
     '    // Weapon.AddSpread (stock UE3): RandY = FRand() - 0.5; RandZ = Sqrt(0.5 - RandY^2) * (FRand() - 0.5).\n' +
     '    const float ry = core::simRand01() - 0.5f;\n' +
     '    const float rz = std::sqrt(std::max(0.0f, 0.5f - ry * ry)) * (core::simRand01() - 0.5f);\n' +
     '    return core::normalize(X + Y * (ry * S) + Z * (rz * S));\n' +
     '}\n\n' +
     'void World::spawnProjectile(const core::Vec3& pos, const core::Vec3& vel, const Weapon& w, int instigator) {\n'],
]);
ed('src/game/WorldBots.cpp', [
    ['        auto rf = [] { return core::simRandSigned(); };   // simulation stream (not shared with FX)\n' +
     '        const float spread = shooter.effectiveSpread();\n' +
     '        dir = core::normalize(dir + rt * (rf() * spread) + u2 * (rf() * spread));\n',
     '        (void)rt; (void)u2;\n' +
     '        dir = addSpread(dir, shooter.shotSpreadFor(w), w);   // Weapon.AddSpread [CONF RE playtest #11]\n'],
    ['        spawnProjectile(muzzle, d * w.projSpeed, w, o.matchPlayer());\n',
     '        spawnProjectile(muzzle, addSpread(d, pc.shotSpreadFor(w), w) * w.projSpeed, w, o.matchPlayer());\n'],
]);
ed('src/core/Application.cpp', [[
    '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n',
    '    {   // Weapon.AddSpread (playtest #11): the stock disc within +-0.5 S (right) / +-0.354 S (up); the Shotgun\'s fixed pattern x M\n' +
    '        const game::WeaponDef* ar = game::findWeaponDef("AssaultRifle"); const game::WeaponDef* sg = game::findWeaponDef("Shotgun");\n' +
    '        bool ok = ar && sg; float maxY = 0.0f, maxZ = 0.0f;\n' +
    '        if (ok) {\n' +
    '            const game::Weapon w = game::Weapon::fromDef(*ar);\n' +
    '            const core::Vec3 X{0, 0, -1}, Y{1, 0, 0}, Z{0, 1, 0};\n' +
    '            world_.setPelletIndex(0);\n' +
    '            for (int i = 0; i < 4000; ++i) {\n' +
    '                const core::Vec3 d = world_.addSpread(X, 0.1f, w);\n' +
    '                const float k = 1.0f / core::dot(d, X);   // back to the tangent plane\n' +
    '                maxY = std::max(maxY, std::fabs(core::dot(d, Y) * k)); maxZ = std::max(maxZ, std::fabs(core::dot(d, Z) * k));\n' +
    '            }\n' +
    '            ok = maxY <= 0.0501f && maxZ <= 0.0355f && maxY > 0.045f;\n' +
    '            game::Weapon s = game::Weapon::fromDef(*sg);\n' +
    '            world_.setPelletIndex(4);   // pattern (-.035, -.04) x M, M = S / spreadMin\n' +
    '            const float S = s.spreadMin * 2.0f;\n' +
    '            const core::Vec3 d = world_.addSpread(X, S, s);\n' +
    '            const float k = 1.0f / core::dot(d, X);\n' +
    '            ok = ok && std::fabs(core::dot(d, Y) * k - (-0.035f * 2.0f)) < 1e-4f && std::fabs(core::dot(d, Z) * k - (-0.04f * 2.0f)) < 1e-4f;\n' +
    '            world_.setPelletIndex(0);\n' +
    '        }\n' +
    '        LOG_INFO("SESSIONTEST AddSpread: S 0.1 -> max right %.4f up %.4f (stock bounds 0.05 / 0.0354)", maxY, maxZ);\n' +
    '        check(ok, "Weapon.AddSpread: the stock +-0.5 S disc; the Shotgun\'s fixed pellet pattern scaled by GetSpread / GetInitialSpread");\n' +
    '    }\n' +
    '    LOG_INFO("SESSIONTEST SUMMARY: %d/%d checks passed", checks - fails, checks);\n}\n'
]]);
console.log('ok');
