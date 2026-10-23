// Campaign runtime step 7 (anchor edits; on RUNTIME6): mover-attached nav cells - BotNav reads flags.mover_base, takes AssetTools'
// MoverPathNode "special" links as walk links (PROV), and BotNav::translateCells moves a mover's cells (polygons, portals, link ends,
// re-bucketed on an xz move); the campaign binds them to the chapter's movers and moves them when a matinee leaves the mover at rest.
// WorldCampaignMatinee.cpp copied from argv[2].
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
fs.copyFileSync(path.join(G, 'docs/handoff/campaign/WorldCampaignMatinee.cpp'), 'src/game/WorldCampaignMatinee.cpp');
ed('src/game/BotNav.h', [
    ['        bool versusOnly = false;',
     '        bool versusOnly = false;\n        std::string moverBase;     // campaign nav: the InterpActor a mover-attached cell (MoverPathNode) rides (full path)'],
    ['    void recomputePieces() { computePieces(); }\n',
     '    void recomputePieces() { computePieces(); }\n' +
     '    // Campaign movers: move these cells (polygons, centroids, bounds, portals, link ends) by d; an xz move re-buckets them.\n' +
     '    void translateCells(const std::vector<int>& cells, const core::Vec3& d);\n'],
]);
ed('src/game/BotNav.cpp', [
    ['        o.versusOnly = c["flags"]["versus_only"].asBool(false);\n',
     '        o.versusOnly = c["flags"]["versus_only"].asBool(false);\n        o.moverBase = c["flags"]["mover_base"].asString();\n'],
    ['        if (kind != "jump_up" && kind != "drop_down" && kind != "double_jump_up" && kind != "walk") continue;\n',
     '        // "special" MoverPathNode links (AssetTools: a mover-attached cell to its static neighbours) are walked (PROV); other specials skipped\n' +
     '        const bool moverLink = kind == "special" && l["special_actor_class"].asString() == "MoverPathNode";\n' +
     '        if (kind != "jump_up" && kind != "drop_down" && kind != "double_jump_up" && kind != "walk" && !moverLink) continue;\n'],
    ['        L.walk = kind == "walk";\n', '        L.walk = kind == "walk" || moverLink;\n'],
    ['void BotNav::computePieces() {',
     'void BotNav::translateCells(const std::vector<int>& ids, const core::Vec3& d) {\n' +
     '    std::vector<uint8_t> moved(cells_.size(), 0);\n' +
     '    for (int id : ids) if (id >= 0 && (size_t)id < cells_.size()) moved[(size_t)id] = 1;\n' +
     '    const bool xz = std::fabs(d.x) > 1e-4f || std::fabs(d.z) > 1e-4f;\n' +
     '    if (xz) for (auto& b : grid_) b.erase(std::remove_if(b.begin(), b.end(), [&](int c) { return moved[(size_t)c] != 0; }), b.end());\n' +
     '    for (size_t i = 0; i < cells_.size(); ++i) {\n' +
     '        if (!moved[i]) continue;\n' +
     '        Cell& c = cells_[i];\n' +
     '        for (core::Vec3& p : c.poly) p = p + d;\n' +
     '        c.centroid = c.centroid + d; c.bmin = c.bmin + d; c.bmax = c.bmax + d;\n' +
     '        for (Portal& P : c.portals) { P.a = P.a + d; P.b = P.b + d; }\n' +
     '        if (xz && !grid_.empty()) {\n' +
     '            const int ax = std::clamp((int)((c.bmin.x - gx0_) / gcell_), 0, gw_ - 1), bx = std::clamp((int)((c.bmax.x - gx0_) / gcell_), 0, gw_ - 1);\n' +
     '            const int az = std::clamp((int)((c.bmin.z - gz0_) / gcell_), 0, gh_ - 1), bz = std::clamp((int)((c.bmax.z - gz0_) / gcell_), 0, gh_ - 1);\n' +
     '            for (int z = az; z <= bz; ++z) for (int x = ax; x <= bx; ++x) grid_[(size_t)z * (size_t)gw_ + (size_t)x].push_back((int)i);\n' +
     '        }\n' +
     '    }\n' +
     '    for (Link& L : links_) { if (moved[(size_t)L.from]) L.fromPos = L.fromPos + d; if (moved[(size_t)L.to]) L.toPos = L.toPos + d; }\n' +
     '}\n\n' +
     'void BotNav::computePieces() {'],
]);
ed('src/game/Campaign.h', [
    ['    int pawnSet = -1;\n};',
     '    int pawnSet = -1;\n' +
     '    std::vector<int> navCells;      // its mover-attached nav cells\n' +
     '    core::Vec3 navOffset{0, 0, 0};  // the offset those cells were last moved to\n' +
     '};'],
]);
ed('src/game/World.h', [
    ['    void cpRegisterMovers();\n',
     '    void cpRegisterMovers();\n    void cpBindMoverNav();\n    void cpSyncMoverNav();\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['        cpLoadMatinees();\n', '        cpLoadMatinees();\n        cpBindMoverNav();   // (the nav is loaded by addBots above)\n'],
]);
ed('src/game/Kismet.h', [
    ['        int named = -1;', '        int buddyIndex = 1;   // TnSeqVar_Buddy BuddyIndex (1-based, CDO default 1)\n        int named = -1;'],
]);
ed('src/game/Kismet.cpp', [
    ['v.varName = a["VarName"].asString();', 'v.varName = a["VarName"].asString(); v.buddyIndex = a["BuddyIndex"].asInt(1);'],
]);
ed('src/game/Campaign.h', [
    ['    std::map<std::string, std::vector<int>> bodyPools;',
     '    struct FormOp { int op = -1; bool vehicle = false; std::vector<int> targets; };\n' +
     '    std::vector<FormOp> formOps;              // step 7: TnSeqAct_ForceIntoForm in progress\n' +
     '    std::map<std::string, std::vector<int>> bodyPools;'],
]);
ed('src/game/World.h', [
    ['    bool cpForceIntoForm(KismetVM& vm, int op);\n', '    bool cpForceIntoForm(KismetVM& vm, int op);\n    void cpTickForms();\n'],
]);
ed('src/game/WorldCampaign.cpp', [
    ['    cpTickMatinees(dt);\n', '    cpTickMatinees(dt);\n    cpTickForms();\n'],
]);
fs.copyFileSync(path.join(G, 'docs/handoff/campaign/WorldCampaignRuntime.cpp'), 'src/game/WorldCampaignRuntime.cpp');
console.log('ok');
