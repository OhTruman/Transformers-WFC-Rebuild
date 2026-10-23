// Campaign transforming world actors (Integration gate item 2026-10-23): TnSeqAct_TransformWorldProp (A1 174 ops, A2 184) drives the
// chapter's TnTransformingWorldActors (doors, tube doors, the lift pods) through TransformingActors, loaded on campaign maps from
// AssetTools' <CH>/transforming_actors_gameplay.json (the MP manifest's schema, no "map" filter; start_transformed = the authored
// start state). Versus / Escalation unchanged (TransformingActors::load). WorldCampaignRuntime.cpp copied from argv[2].
const fs = require('fs');
const path = require('path');
const G = process.argv[2] || path.resolve(__dirname, '..', '..', '..');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
for (const f of ['WorldCampaignRuntime.cpp', 'WorldCampaignMatinee.cpp']) fs.copyFileSync(path.join(G, 'docs/handoff/campaign', f), path.join('src/game', f));
ed('src/game/TransformingActors.h', [
    ['        bool hidden = false, collide = true, bridge = false;\n',
     '        bool hidden = false, collide = true, bridge = false;\n' +
     '        bool startTransformed = false;       // campaign: the authored start state (initial_state Transformed)\n' +
     '        core::Mat4 baseMatrix;               // the authored gltf_matrix (matrix = a matinee\'s translation x it)\n'],
    ['    int find(const std::string& name) const {',
     '    // Campaign: a matinee Move track moves the actor (A1\'s lift pod): its matrix = translate(offset) x the authored one; re-posed next tick.\n' +
     '    void setOffset(size_t i, const core::Vec3& d) {\n' +
     '        if (i >= actors_.size()) return;\n' +
     '        Actor& a = actors_[i];\n' +
     '        a.matrix = core::Mat4::translate(d) * a.baseMatrix;\n' +
     '        a.posedT = -1.0f;\n' +
     '    }\n' +
     '    int find(const std::string& name) const {'],
    ['    bool load(const std::string& mapName, const std::string& manifestRoot, CollisionWorld& pawn, CollisionWorld* weapon);\n',
     '    bool load(const std::string& mapName, const std::string& manifestRoot, CollisionWorld& pawn, CollisionWorld* weapon);\n' +
     '    // A campaign chapter\'s actors (every row of the file; glb paths relative to toolsRoot); they start in their authored state.\n' +
     '    bool loadChapter(const std::string& path, const std::string& toolsRoot, CollisionWorld& pawn, CollisionWorld* weapon);\n'],
    ['private:\n    std::vector<Actor> actors_;\n',
     'private:\n    std::vector<Actor> actors_;\n    bool campaign_ = false;   // loaded by loadChapter: the authored start states, no versus door rule\n' +
     '    bool loadFile(const std::string& path, const std::string& mapName, const std::string& toolsRoot, CollisionWorld& pawn, CollisionWorld* weapon);\n'],
]);
ed('src/game/TransformingActors.cpp', [
    ['bool TransformingActors::load(const std::string& mapName, const std::string& manifestRoot, CollisionWorld& pawn, CollisionWorld* weapon) {\n    actors_.clear();\n    const std::string path = manifestRoot + "/maps/mp_transforming_actors.json";\n',
     'bool TransformingActors::load(const std::string& mapName, const std::string& manifestRoot, CollisionWorld& pawn, CollisionWorld* weapon) {\n' +
     '    campaign_ = false;\n' +
     '    return loadFile(manifestRoot + "/maps/mp_transforming_actors.json", mapName, manifestRoot + "/..", pawn, weapon);\n' +
     '}\n\n' +
     'bool TransformingActors::loadChapter(const std::string& path, const std::string& toolsRoot, CollisionWorld& pawn, CollisionWorld* weapon) {\n' +
     '    campaign_ = true;\n' +
     '    return loadFile(path, std::string(), toolsRoot, pawn, weapon);\n' +
     '}\n\n' +
     'bool TransformingActors::loadFile(const std::string& path, const std::string& mapName, const std::string& toolsRoot, CollisionWorld& pawn, CollisionWorld* weapon) {\n' +
     '    actors_.clear();\n'],
    ['    const std::string toolsRoot = manifestRoot + "/..";   // glb paths are AssetTools-relative ("manifests/maps/transforming_actors/...")\n',
     '    // glb paths are AssetTools-relative ("manifests/maps/transforming_actors/..."): toolsRoot\n'],
    ['        if (j["map"].asString() != mapName) continue;\n', '        if (!mapName.empty() && j["map"].asString() != mapName) continue;   // (a chapter file: every row)\n'],
    ['        a.bridge = meshName.rfind("NEU_Bridge", 0) == 0;\n',
     '        a.bridge = meshName.rfind("NEU_Bridge", 0) == 0;\n' +
     '        a.startTransformed = j["start_transformed"].asBool(false) || j["initial_state"].asString() == "Transformed";\n' +
     '        a.baseMatrix = a.matrix;\n'],
    ['        if (!escalation && a.startDesired == 0 && !a.bridge) {\n', '        if (!escalation && !campaign_ && a.startDesired == 0 && !a.bridge) {\n'],
    ['        a.t = 0.0f;                          // Escalation (and Rust in every mode): Untransformed at the start (bridges: PreGameplaySetup\'s Untransform)\n        a.desired = a.startDesired;          // Rust doors: Transform at level start\n',
     '        a.t = campaign_ && a.startTransformed ? a.length : 0.0f;   // Escalation (and Rust in every mode): Untransformed at the start (bridges: PreGameplaySetup\'s Untransform); campaign: authored\n' +
     '        a.desired = campaign_ && a.startTransformed ? 1 : a.startDesired;   // Rust doors: Transform at level start\n'],
]);
ed('src/game/World.cpp', [
    ['            transforming_.load(mapName_, mr ? mr : "F:/Transformers Rebuild/AssetTools/manifests", collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);\n',
     '            const std::string mroot = mr ? mr : "F:/Transformers Rebuild/AssetTools/manifests";\n' +
     '            if (campaignMap_) {   // a campaign chapter: AssetTools\' <CH>/transforming_actors_gameplay.json\n' +
     '                transforming_.loadChapter(cpChapter_.dir + "transforming_actors_gameplay.json", mroot + "/..", collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);\n' +
     '                LOG_INFO("campaign: %zu transforming world actors", transforming_.actors().size());\n' +
     '                // drawn posed by TransformingActorsDraw: the bind-pose copies in the units\' world.glb are hidden (Rendering 2026-10-23)\n' +
     '                for (const TransformingActors::Actor& ta : transforming_.actors()) renderer.setActorHidden(ta.name, true);\n' +
     '            } else transforming_.load(mapName_, mroot, collision_, weaponCollision_.valid() ? &weaponCollision_ : nullptr);\n'],
]);
ed('src/game/Campaign.h', [
    ['    CampaignOmega omega;\n',
     '    CampaignOmega omega;\n    std::vector<std::pair<int, std::vector<int>>> transformOps;   // TnSeqAct_TransformWorldProp waiting for Finished: op, actors\n' +
     '    bool transformMoved = false;               // a matinee moved a transforming actor (nav blocks re-applied at rest)\n'],
]);
ed('src/game/World.h', [
    ['    void cpTickAiUse();\n',
     '    void cpTickAiUse();\n    bool cpTransformWorldProp(KismetVM& vm, int op, int input);\n    void cpTickTransformOps();\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['    if (cpOmegaActivate(vm, op, input)) return true;', '    if (cpTransformWorldProp(vm, op, input)) return true;   // doors / pods\n    if (cpOmegaActivate(vm, op, input)) return true;'],
    ['    cpTickAiUse();\n', '    cpTickAiUse();\n    cpTickTransformOps();\n'],
]);
console.log('ok');
