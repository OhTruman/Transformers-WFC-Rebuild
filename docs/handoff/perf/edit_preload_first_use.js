// 300 fps sim plan, the first-use loads (SOAK_TABLE_2026-10-24): load scheduling only, no sim change.
//  1. Melee weapon models join the match-load held-weapon preload (preloadHeldWeaponModels skipped typeCode 2: D3B / D5 loaded
//     SpikedMace / ViciousAxe / TortureSword at the human's first melee, A1 ShortSword / Axe: "weapon model ... loaded during play").
//  2. The gib meshes (11 static chunks + the 2 skeletal gib templates) load at the match load instead of at the first gib
//     (BrokenHope / Static: the first Shockwave's mass gib, 900 ms cold in "abilities").
//  3. The matinee pawn-group clips of every participant's chassis load at the chapter load instead of at their first play
//     (A1 step 781, A3 step 1887, D5 step 1781: cpPlayPawnClip's loadAnimationsByName).
// Run from the tree root.
const fs = require('fs');
const NL = String.fromCharCode(10), CR = String.fromCharCode(13);
function rd(f) { const s = fs.readFileSync(f, 'utf8'); return { crlf: s.includes(CR + NL), s: s.split(CR + NL).join(NL) }; }
function wr(f, o) { fs.writeFileSync(f, o.crlf ? o.s.split(NL).join(CR + NL) : o.s); }
function ed(f, pairs) {
    const o = rd(f);
    for (const [a, b] of pairs) {
        if (o.s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        o.s = o.s.replace(a, b);
    }
    wr(f, o);
}
// 1. melee in the held-weapon preload
ed('src/game/World.cpp', [
    ['        // Only weapons that can be the held weapon (primary / heavy, and grenade bags: held while tossed) and draw a mesh; the Ion Blaster uses the boot model.' + NL +
     '        if (!d || (d->typeCode != 0 && d->typeCode != 1 && d->typeCode != 4) ||',
     '        // Only weapons that can be the held weapon (primary / heavy, melee, and grenade bags: held while tossed) and draw a mesh; the Ion Blaster uses the boot model.' + NL +
     '        if (!d || (d->typeCode != 0 && d->typeCode != 1 && d->typeCode != 2 && d->typeCode != 4) ||'],
]);
// 2. the gib blocks move into ensureGibModels(), called at the match load and (as before) at the first gib
{
    const f = 'src/game/World.cpp', o = rd(f);
    const a0 = '    if (!gibMeshesTried_ && renderer_) {' + NL;
    const i0 = o.s.indexOf(a0); if (i0 < 0 || o.s.indexOf(a0, i0 + 1) >= 0) throw new Error('gib block');
    const e0 = o.s.indexOf(NL + '    }' + NL, i0) + (NL + '    }' + NL).length;
    const gib = o.s.slice(i0, e0);
    const a1 = '        if (!skelGibModelsTried_) {' + NL;
    const i1 = o.s.indexOf(a1); if (i1 < 0 || o.s.indexOf(a1, i1 + 1) >= 0) throw new Error('skel gib block');
    const e1 = o.s.indexOf(NL + '        }' + NL, i1) + (NL + '        }' + NL).length;
    const skel = o.s.slice(i1, e1);
    if (/\bc\./.test(gib) || /\bc\./.test(skel)) throw new Error('gib blocks use the corpse');
    o.s = o.s.slice(0, i1) + '        ensureGibModels();' + NL + o.s.slice(e1);
    o.s = o.s.replace(gib, '    ensureGibModels();' + NL);
    const fn = 'void World::gibCorpse(Corpse& c) {' + NL;
    if (o.s.split(fn).length !== 2) throw new Error('gibCorpse');
    const skelOut = skel.split(NL).map(l => l.startsWith('    ') ? l.slice(4) : l).join(NL);
    o.s = o.s.replace(fn, '// The gib meshes: the static chunks and the two skeletal gib templates (once; at the match load and at the first gib).' + NL +
        'void World::ensureGibModels() {' + NL + gib + '    if (renderer_) {' + NL + skelOut.split(NL).map(l => l ? '    ' + l : l).join(NL) + '    }' + NL + '}' + NL + NL + fn);
    wr(f, o);
}
ed('src/game/World.cpp', [
    ['        if (const WeaponDef* d = findWeaponDef(id)) weaponModelFor(*d);' + NL + '    ensureAbilityModels();' + NL + '}' + NL,
     '        if (const WeaponDef* d = findWeaponDef(id)) weaponModelFor(*d);' + NL + '    ensureAbilityModels();' + NL +
     '    ensureGibModels();   // the first gib (a Shockwave mass kill) loaded them in one step' + NL + '}' + NL],
]);
ed('src/game/World.h', [
    ['    void ensureAbilityModels();', '    void ensureAbilityModels();' + NL + '    void ensureGibModels();'],
]);
// 3. matinee pawn-group clips at the chapter load
ed('src/game/WorldCampaignMatinee.cpp', [
    ['    LOG_INFO("campaign: %zu matinee pawn groups bound to characters", cpPawnGroups_.size());' + NL,
     '    LOG_INFO("campaign: %zu matinee pawn groups bound to characters", cpPawnGroups_.size());' + NL +
     '    // Every participant chassis\' clips load now (cpPlayPawnClip loaded each at its first play: a 5-38 ms step). Load scheduling only.' + NL +
     '    int preloaded = 0;' + NL +
     '    for (size_t q = 0; q < match_.players().size(); ++q) {' + NL +
     '        const std::string ch = resolveChassis(match_.players()[q].selection, match_.faction((int)q));' + NL +
     '        const ChassisAssets* ca = chassisAssets(ch);' + NL +
     '        if (!ca || !ca->ok) continue;' + NL +
     '        assets::SkinnedModel& model = const_cast<assets::SkinnedModel&>(ca->robot);' + NL +
     '        for (const auto& kv : cpPawnGroups_) {' + NL +
     '            if (std::find(kv.second.chassis.begin(), kv.second.chassis.end(), ch) == kv.second.chassis.end()) continue;' + NL +
     '            for (const auto& cf : kv.second.clipFile)' + NL +
     '                if (model.clipByName(cf.first) < 0) { assets::loadAnimationsByName("F:/Transformers Rebuild/ExtractedAssets/" + cf.second, model); ++preloaded; }' + NL +
     '        }' + NL +
     '    }' + NL +
     '    if (preloaded) LOG_INFO("campaign: %d matinee pawn clip files preloaded", preloaded);' + NL],
]);
console.log('ok');
