// Harness switches for perf bisects (env-gated, presentation only, SIMHASH-neutral): WFC_NOCORPSE=1 creates no death body / gibs / hull
// wreck (the death light and sound still play: requestCorpse falls through to the light-only path); WFC_NOMINEPRES=1 creates no Kamikaze
// mine presentation (mesh / trail / detonation FX entries). Run from the tree root.
const fs = require('fs');
function ed(f, a, b) {
  let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
  const n = s.split(a).length - 1; if (n !== 1) throw new Error(f + ' ' + n + ' ' + a.slice(0, 70));
  s = s.replace(a, () => b); fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/World.cpp', `    const assets::SkinnedModel* mdl = pawn.currentModel();
    if (!mdl || !mdl->valid()) { spawnDeathLight(`, `    const assets::SkinnedModel* mdl = pawn.currentModel();
    static const bool noCorpse = std::getenv("WFC_NOCORPSE") != nullptr;   // harness (perf bisects): the death light only, no body / gibs / wreck
    if (noCorpse) mdl = nullptr;
    if (!mdl || !mdl->valid()) { spawnDeathLight(`);
ed('src/game/World.cpp', `            {   // [Gameplay] presentation: spawned with the owner's rotation, the flight trail from spawn
                ensureAbilityModels();`, `            static const bool noMinePres = std::getenv("WFC_NOMINEPRES") != nullptr;   // harness (perf bisects): no mine presentation
            if (!noMinePres) {   // [Gameplay] presentation: spawned with the owner's rotation, the flight trail from spawn
                ensureAbilityModels();`);
console.log('ok');
