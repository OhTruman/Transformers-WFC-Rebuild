// Death-body patch 2, part 4b: the faithful death picker and light lifetime (RE GAMEPLAY_DEATH_PICKER_RE, CONFIRMED). On top of the
// existing authored-order first-match picker (DeathEntries.inc) and edit_momentum_picker.js:
// - the momentum type is one of four: TearOffMomentum yaw in pawn space within +-45 deg -> Forward, the sides -> Left / Right, else
//   Backward; zero momentum -> Forward (SharedRobotDeaths authors only Forward / Backward, so Left / Right fall to the default death);
// - ShouldInstaGib right after the pick (its modifiers, the light included, have started): every playable blueprint authors
//   CanInstaGib, so it is the damage type's ShouldGib: KillZ (Engine.DmgType_Fell here), BruteBackpack and DeathAnim always,
//   EmpShotgun when another pawn killed from within GibDistance 1000 UU; nobody is downed in multiplayer and no Matinee plays;
// - per-entry gibs: Melee (and the unreachable Energy) have no gibs, no skeletal gibs and no decal (the Defrag dissolve alone); Poke
//   has PokeGibs_DSYS and no skeletal gibs or decal [PokeGibs_DSYS content not yet read: the default gibs stand in, PROV];
// - the death light (the pawn's DeathLight0) ends with the pawn: Dying.Gib sets LifeSpan 1, so the light is cut 1 s after the gib
//   (robot lit entries complete their 2 s ramp; an insta-gib cuts it at ~1 s; car / tank lights last 1-1.5 s, not LightTime 8).
// Martyrdom (TnBuffExplodeOnDeath*) stays unmodelled: no such buff in the rebuild's multiplayer. Presentation only.
// Run from a tree root after edit_death_camera_roll.js.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}

ed('src/game/World.h', [
    ['        unsigned mods = 0; const char* entry = "Default"; const char* animName = "";',
     '        unsigned mods = 0; const char* entry = "Default"; const char* animName = "";\n' +
     '        bool instaGib = false, noGibs = false, noSkelGib = false, noDecal = false;   // ShouldInstaGib; the entry\'s custom gibs'],
    ['                                rampOut = 0.0f, age = 0.0f; int key = 0; bool rtOnly = false; };',
     '                                rampOut = 0.0f, age = 0.0f; int key = 0; bool rtOnly = false;\n' +
     '                                float cutAt = -1.0f; };   // cutAt >= 0: the owning pawn is destroyed at that age (the light goes with it)'],
]);

const W = 'src/game/World.cpp';
ed(W, [
    ['        active.push_back(cosA >= 0.7071f ? "ForwardMomentum" : cosA <= -0.7071f ? "BackwardMomentum" : "SideMomentum");\n',
     '        const core::Vec3 rgt = core::normalize(core::cross(core::Vec3{q.fwd.x, 0.0f, q.fwd.z}, core::Vec3{0, 1, 0}));\n' +
     '        const float sinA = ml > 1e-3f ? core::dot(m * (1.0f / ml), rgt) : 0.0f;\n' +
     '        active.push_back(cosA >= 0.7071f ? "ForwardMomentum" : cosA <= -0.7071f ? "BackwardMomentum" : sinA > 0.0f ? "RightMomentum" : "LeftMomentum");\n'],
    ['    c.total = tMin + (tMax - tMin) * c.frand();\n',
     '    c.total = tMin + (tMax - tMin) * c.frand();\n' +
     '    // The entry\'s custom gibs: Melee / Energy dissolve with no gibs, skeletal gibs or decal; Poke has PokeGibs_DSYS (PROV: default gibs).\n' +
     '    if (!std::strcmp(c.entry, "Melee") || !std::strcmp(c.entry, "Energy")) c.noGibs = c.noSkelGib = c.noDecal = true;\n' +
     '    if (!std::strcmp(c.entry, "Poke")) c.noSkelGib = c.noDecal = true;\n' +
     '    // TnPawn.Dying.BeginState: ShouldInstaGib after PlayDeath (CanInstaGib on every playable blueprint): the damage type\'s ShouldGib.\n' +
     '    {   // (both forms: the same pawn and blueprint)\n' +
     '        const std::string& d = q.damageType;\n' +
     '        const bool insta = d == "Engine.DmgType_Fell" || d == "TransGame.TnDamageTypeBruteBackpack" || d == "TransGame.TnDamageTypeDeathAnim" ||\n' +
     '                           (d == "TransGame.TnDamageTypeEmpShotgun" && q.haveKiller && core::length(q.killerPos - q.pos) <= 10.0f);\n' +
     '        if (insta) { c.instaGib = true; c.total = 0.0f; }\n' +
     '    }\n'],
    // gibCorpse: the light ends with the pawn (LifeSpan 1); entries without gibs stop here.
    ['void World::gibCorpse(Corpse& c) {\n    c.gibbed = true;\n',
     'void World::gibCorpse(Corpse& c) {\n    c.gibbed = true;\n' +
     '    for (LiveTransientLight& l : transientLights_) if (l.key == 1000 + c.player && l.cutAt < 0.0f) l.cutAt = l.age + 1.0f;   // LifeSpan 1\n' +
     '    if (c.noGibs) return;   // Melee: the Defrag dissolve alone\n'],
    ['    if (!c.vehicle && renderer_ && collision_.valid()) {\n        const float y0 = c.pos.y;\n',
     '    if (!c.vehicle && !c.noDecal && renderer_ && collision_.valid()) {\n        const float y0 = c.pos.y;\n'],
    ['        if ((l.age += dt) >= std::max(l.lightTime, l.rampIn) + l.rampOut) { l = transientLights_.back(); transientLights_.pop_back(); }\n',
     '        if ((l.age += dt) >= std::max(l.lightTime, l.rampIn) + l.rampOut || (l.cutAt >= 0.0f && l.age >= l.cutAt)) { l = transientLights_.back(); transientLights_.pop_back(); }\n'],
]);
console.log('ok');
