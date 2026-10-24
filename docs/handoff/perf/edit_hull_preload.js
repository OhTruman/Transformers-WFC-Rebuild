// 300 fps sim plan (D3B step 4310, 4.4-4.7 ms): a vehicle-form death's gib hull (TnSkeletalGibSpawner's DefaultSkeletalGibTemplate,
// e.g. VH_Megatron_HULL_SKEL) loaded + uploaded in gibCorpse at the first death of that chassis in vehicle form. Every participant
// chassis' hull now loads at the match load into the same cache (hullMeshes_, same load + texture + prewarm), after the bots, the
// Escalation slots and the campaign squad / factories exist. Load scheduling only (presentation data). Run from the tree root.
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
ed('src/game/World.cpp', [
    ['    setupCampaign(l);   // campaign: the human\'s pick, the AI squad, the level Kismet (no-op otherwise)' + NL,
     '    setupCampaign(l);   // campaign: the human\'s pick, the AI squad, the level Kismet (no-op otherwise)' + NL +
     '    ensureHullMeshes();   // every participant chassis\' vehicle-death gib hull (gibCorpse loaded it at the first vehicle death)' + NL],
    ['void World::ensureGibModels() {' + NL,
     '// The vehicle-death gib hull of every participant chassis (the cache gibCorpse fills; same load, textures and prewarm).' + NL +
     'void World::ensureHullMeshes() {' + NL +
     '    if (!renderer_) return;' + NL +
     '    for (size_t q = 0; q < match_.players().size(); ++q) {' + NL +
     '        const ChassisDeathDef* cd = chassisDeath(resolveChassis(match_.players()[q].selection, match_.faction((int)q)));' + NL +
     '        if (!cd || !*cd->hull || hullMeshes_.count(cd->hull)) continue;' + NL +
     '        std::unique_ptr<render::MeshData> md = std::make_unique<render::MeshData>();' + NL +
     '        if (assets::loadGlb(assetRoot() + "/../" + cd->hull, *md)) { for (render::Material& M : md->mats) { M.tex = resolveTexture(M.baseColorUri); M.emissiveTexHandle = resolveTexture(M.emissiveUri); } fxPrewarm(*renderer_, *md, 0); }' + NL +
     '        else md.reset();' + NL +
     '        hullMeshes_.emplace(cd->hull, std::move(md));' + NL +
     '    }' + NL +
     '}' + NL + NL +
     'void World::ensureGibModels() {' + NL],
]);
ed('src/game/World.h', [
    ['    void ensureGibModels();', '    void ensureGibModels();' + NL + '    void ensureHullMeshes();'],
]);
console.log('ok');
