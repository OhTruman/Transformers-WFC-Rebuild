// Playtest 2026-10-23 #10 / #13 (Seed of Corruption collision), dev-run pickup of AssetTools' PC-adapted data: WFC_OVERLAY_ROOT=<dir>
// makes the map loader read an ExtractedAssets-relative file from <dir> when the overlay holds it (else ExtractedAssets), the same rule
// as the package overlay (build_slim --overlay). Covered: the map's physics.json (objective / forced-dir volumes) and collision_pawn.glb.
// Unset = unchanged. Run from a tree root.
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/game/World.cpp', [
    ['    const std::string mapDir = this->mapDir();\n    mapState_.load(mapDir + "gameplay.json", matchMode_);\n' +
     '    mapState_.loadObjectiveVolumes(mapDir + "physics.json");\n    loadForcedDirVolumes(mapDir + "physics.json");\n',
     '    const std::string mapDir = this->mapDir();\n' +
     '    // WFC_OVERLAY_ROOT (dev runs): an ExtractedAssets-relative file the overlay holds wins (the package overlay rule).\n' +
     '    auto overlayed = [](const std::string& path) {\n' +
     '        static const char* root = std::getenv("WFC_OVERLAY_ROOT");\n' +
     '        if (!root || !*root) return path;\n' +
     '        std::string p = path; for (char& c : p) if (c == \'\\\\\') c = \'/\';\n' +
     '        const size_t k = p.find("ExtractedAssets/");\n' +
     '        if (k == std::string::npos) return path;\n' +
     '        const std::string o = std::string(root) + "/" + p.substr(k + 16);\n' +
     '        std::ifstream f(o, std::ios::binary);\n' +
     '        if (!f.good()) return path;\n' +
     '        LOG_INFO("overlay: %s", o.c_str());\n' +
     '        return o;\n' +
     '    };\n' +
     '    mapState_.load(mapDir + "gameplay.json", matchMode_);\n' +
     '    mapState_.loadObjectiveVolumes(overlayed(mapDir + "physics.json"));\n    loadForcedDirVolumes(overlayed(mapDir + "physics.json"));\n'],
    ['    bool authored = assets::loadGlb(mapDir + "collision_pawn.glb", colMesh);\n',
     '    bool authored = assets::loadGlb(overlayed(mapDir + "collision_pawn.glb"), colMesh);\n'],
]);
console.log('ok');
