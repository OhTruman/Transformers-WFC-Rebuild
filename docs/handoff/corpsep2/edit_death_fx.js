// Death-body patch 2, part 4c: the original's per-entry death effects from Systems' generated table (agents/systems 299d85da,
// docs/handoff/SYSTEMS_DEATH_FX.md; DeathFx.inc from tools/systems/gen_death_fx.py over authored.db): per blueprint + picked entry,
// the gibs blueprint's effects (template + offset at PositionSocket), whether it has chunks, skeletal gibs and a decal, and the
// PlayEffect modifier's effects (template + socket). Replaces the hand-written choices:
// - Poke: PokeGibs_DSYS = TF_Death_MED_FX, no chunks / skeletal gib / decal (closes PROV item 2); Energy: no gibs at all and
//   TF_Death_Energy_Med_FX at Chest_FX; TractorBeam: Explosion_TractorBeam_FX, no chunks; SingleShot adds Exp_Death_Sm_FX at Chest_FX;
//   HeadShot / momentum / Melee / RobotBooster play no effects;
// - vehicles: PlayEffect plays BOTH TF_Death_Buildup_FX and TF_Death_MED_FX at TargetingSocket (it picked one at random before); jets
//   Smoke_Trail_Jet_Fast_FX at VaporTrailSocket_R + MED.
// The decal stays robot-only (RE s12 add. 23: TnRobotForm.OnPlayGib; the vehicle rows' decal flag is not used - raised with Systems).
// Sockets other than the head bone are placed as before [PROV: BoosterSocket_C 1.5 m up, Chest_FX at C_Spine02_Lumbar02_XB,
// TargetingSocket 1 m up, VaporTrailSocket_R at the origin]. No row (the default death, or an unknown blueprint): the defaults as
// before. Presentation only. Run from a tree root after edit_killer_cam.js, with corpsep2/new/DeathFx.inc copied to src/game/.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
if (!fs.existsSync('src/game/DeathFx.inc')) throw new Error('copy docs/handoff/corpsep2/new/DeathFx.inc to src/game/ first');

ed('src/game/World.h', [[
    '        float fxAt = -1.0f;                               // PlayEffect modifier: start time (-1 none / done)\n',
    '        float fxAt = -1.0f;                               // PlayEffect modifier: start time (-1 none / done)\n' +
    '        int fxRow = -1;                                   // the blueprint entry\'s row in DeathFx.inc (-1: the default death)\n'
]]);

const W = 'src/game/World.cpp';
ed(W, [
    ['#include "game/DeathEntries.inc"\n',
     '#include "game/DeathEntries.inc"\n' +
     '#include "game/DeathFx.inc"   // [Systems] the entries\' gibs / PlayEffect effects (agents/systems 299d85da)\n'],
    // The row for the picked entry (HoloBrute and unknown robot sets share SharedRobotDeaths' rows).
    ['    // The entry\'s custom gibs: Melee / Energy dissolve with no gibs, skeletal gibs or decal; Poke has PokeGibs_DSYS (PROV: default gibs).\n' +
     '    if (!std::strcmp(c.entry, "Melee") || !std::strcmp(c.entry, "Energy")) c.noGibs = c.noSkelGib = c.noDecal = true;\n' +
     '    if (!std::strcmp(c.entry, "Poke")) c.noSkelGib = c.noDecal = true;\n',
     '    // The entry\'s custom gibs and effects (DeathFx.inc): gibs blueprint effects, chunks, skeletal gibs, decal, PlayEffect.\n' +
     '    if (e) {\n' +
     '        const std::string bpFx = q.vehicle && bpName.empty() ? std::string("SharedCarDeaths") : bpName;\n' +
     '        for (int pass = 0; pass < 2 && c.fxRow < 0; ++pass)\n' +
     '            for (size_t i = 0; i < sizeof(kDeathFx) / sizeof(kDeathFx[0]); ++i)\n' +
     '                if (bpFx == (pass == 0 ? kDeathFx[i].blueprint : (q.vehicle ? "SharedCarDeaths" : "SharedRobotDeaths")) && !std::strcmp(kDeathFx[i].entry, c.entry)) { c.fxRow = (int)i; break; }\n' +
     '    }\n' +
     '    if (c.fxRow >= 0) {\n' +
     '        const DeathFxEntry& R = kDeathFx[(size_t)c.fxRow];\n' +
     '        c.noGibs = R.nGibFx == 0 && !R.chunks && !R.skeletalGibs; c.noSkelGib = !R.skeletalGibs; c.noDecal = !R.decals;\n' +
     '        float dm = 0.0f; for (int i = 0; i < R.nPlay; ++i) dm = std::max(dm, R.play[i].delayMax);\n' +
     '        if (c.fxAt >= 0.0f && dm <= 0.0f) c.fxAt = 0.0f;   // no authored Delay (vehicles, Energy): at once (the frand draw stays)\n' +
     '    }\n'],
    // Gib: the row's gibs blueprint effects; chunks only when it has them.
    ['    const char* fx = c.jet ? "FX_Explosives_p.FX.Exp_Death_Lrg_FX" : c.vehicle ? "FX_Explosives_p.FX.TF_Death_MED_FX" : "FX_Explosives_p.FX.Exp_Death_FX";\n' +
     '    const core::Vec3 at = c.pos + core::Vec3{0, c.vehicle ? 0.0f : 0.5f, 0};\n' +
     '    if (renderer_) fxStepSpawn(fx, at, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1}, -1, true);\n',
     '    const DeathFxEntry* fxr = c.fxRow >= 0 ? &kDeathFx[(size_t)c.fxRow] : nullptr;\n' +
     '    if (fxr) {   // the gibs blueprint\'s effect state at PositionSocket + its offset (UU: Z up)\n' +
     '        for (int i = 0; i < fxr->nGibFx && renderer_; ++i)\n' +
     '            fxStepSpawn(fxr->gibFx[i].tmpl, c.pos + core::Vec3{fxr->gibFx[i].x, fxr->gibFx[i].z, fxr->gibFx[i].y} * 0.01f, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1}, -1, true);\n' +
     '    } else {\n' +
     '        const char* fx = c.jet ? "FX_Explosives_p.FX.Exp_Death_Lrg_FX" : c.vehicle ? "FX_Explosives_p.FX.TF_Death_MED_FX" : "FX_Explosives_p.FX.Exp_Death_FX";\n' +
     '        const core::Vec3 at = c.pos + core::Vec3{0, c.vehicle ? 0.0f : 0.5f, 0};\n' +
     '        if (renderer_) fxStepSpawn(fx, at, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1}, -1, true);\n' +
     '    }\n'],
    ['    for (int i = 0; i < 4 && !gibMeshes_.empty(); ++i) {\n',
     '    for (int i = 0; i < 4 && !gibMeshes_.empty() && (!fxr || fxr->chunks); ++i) {\n'],
    // PlayEffect: the row's effects at their sockets.
    ['                if (renderer_) {\n' +
     '                    if (c.vehicle) fxStepSpawn(c.frand() < 0.5f ? "FX_Explosives_p.FX.TF_Death_Buildup_FX" : "FX_Explosives_p.FX.TF_Death_MED_FX", c.pos + core::Vec3{0, 1.0f, 0}, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1}, -1, true);\n',
     '                if (renderer_ && c.fxRow >= 0) {\n' +
     '                    const DeathFxEntry& R = kDeathFx[(size_t)c.fxRow];\n' +
     '                    for (int i = 0; i < R.nPlay; ++i) {\n' +
     '                        const char* so = R.play[i].socket;\n' +
     '                        core::Vec3 at = c.pos + core::Vec3{0, c.vehicle ? 1.0f : 1.5f, 0};   // BoosterSocket_C / TargetingSocket [PROV]\n' +
     '                        const char* bone = !std::strcmp(so, "Head_FX") ? "C_Spine04_Head_XB" : !std::strcmp(so, "Chest_FX") ? "C_Spine02_Lumbar02_XB" : nullptr;\n' +
     '                        if (!std::strcmp(so, "VaporTrailSocket_R")) at = c.pos;\n' +
     '                        const int bn = bone && c.model ? c.model->nodeByName(bone) : -1;\n' +
     '                        if (bn >= 0 && (size_t)bn < c.glob.size()) { const core::Mat4 w = c.world * c.glob[(size_t)bn]; at = core::Vec3{w.m[12], w.m[13], w.m[14]}; }\n' +
     '                        else if (bone && !std::strcmp(so, "Head_FX")) at = c.pos + core::Vec3{0, 3.0f, 0};\n' +
     '                        fxStepSpawn(R.play[i].tmpl, at, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1}, -1, true);\n' +
     '                    }\n' +
     '                } else if (renderer_) {\n' +
     '                    if (c.vehicle) fxStepSpawn(c.frand() < 0.5f ? "FX_Explosives_p.FX.TF_Death_Buildup_FX" : "FX_Explosives_p.FX.TF_Death_MED_FX", c.pos + core::Vec3{0, 1.0f, 0}, core::Vec3{0, 1, 0}, core::Vec3{0, 0, -1}, -1, true);\n'],
]);
console.log('ok');
