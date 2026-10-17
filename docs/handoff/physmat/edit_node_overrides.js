// Hit physmat follow-up (AssetTools 83b669d): collision_weapon_physmat.json node_overrides { "<glb node index>": physmat_index } REPLACE
// that node's mesh runs (component PhysMaterialOverride / the StaticMeshActor owner's first material slot; 53 MP nodes). Submeshes now
// record their glTF node index; the static per-triangle expansion uses the node's override when one is listed. Presentation data
// only (SIMHASH identical). Run from a tree root that has GAMEPLAY_HIT_PHYSMAT (integration 5fc2e552+).
const fs = require('fs');
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); s = s.replace(/\r\n/g, '\n');
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.replace(/\n/g, '\r\n') : s);
}
ed('src/render/Mesh.h', [[
    '    uint32_t meshTriOffset = 0;       // first triangle of this primitive within that glTF mesh\n',
    '    uint32_t meshTriOffset = 0;       // first triangle of this primitive within that glTF mesh\n' +
    '    int gltfNode = -1;                // glTF node index (collision physmat node_overrides)\n'
]]);
ed('src/assets/Gltf.cpp', [[
    '            sm.gltfMesh = meshIdx; sm.meshTriOffset = meshTri; meshTri += (uint32_t)(idx.size() / 3);\n',
    '            sm.gltfMesh = meshIdx; sm.meshTriOffset = meshTri; meshTri += (uint32_t)(idx.size() / 3);\n' +
    '            sm.gltfNode = nodeIdx;\n'
]]);
ed('src/game/World.cpp', [
    ['    std::vector<uint16_t> perTri;\n' +
     '    for (const render::SubMesh& sm : glbMesh.subs) {\n',
     '    std::vector<uint16_t> perTri;\n' +
     '    const assets::Json& overrides = root["node_overrides"];   // a listed node takes this physmat for all its triangles\n' +
     '    int overridden = 0;\n' +
     '    for (const render::SubMesh& sm : glbMesh.subs) {\n'],
    ['        const std::vector<uint16_t>* v = sm.gltfMesh >= 0 && perMesh.count(sm.gltfMesh) ? &perMesh[sm.gltfMesh] : nullptr;\n' +
     '        for (uint32_t t = 0; t < sm.indexCount / 3; ++t) perTri.push_back(v && sm.meshTriOffset + t < v->size() ? (*v)[sm.meshTriOffset + t] : 0);\n',
     '        const std::string nodeKey = std::to_string(sm.gltfNode);\n' +
     '        if (sm.gltfNode >= 0 && overrides.has(nodeKey)) {\n' +
     '            const uint16_t o = (uint16_t)overrides[nodeKey].asInt(0);\n' +
     '            for (uint32_t t = 0; t < sm.indexCount / 3; ++t) perTri.push_back(o);\n' +
     '            ++overridden;\n' +
     '            continue;\n' +
     '        }\n' +
     '        const std::vector<uint16_t>* v = sm.gltfMesh >= 0 && perMesh.count(sm.gltfMesh) ? &perMesh[sm.gltfMesh] : nullptr;\n' +
     '        for (uint32_t t = 0; t < sm.indexCount / 3; ++t) perTri.push_back(v && sm.meshTriOffset + t < v->size() ? (*v)[sm.meshTriOffset + t] : 0);\n'],
    ['%d submesh source names checked",\n' +
     '                 mapName_.c_str(), n, mismatched, nameChecked);\n',
     '%d submesh source names checked, %d node-override submeshes",\n' +
     '                 mapName_.c_str(), n, mismatched, nameChecked, overridden);\n'],
]);
console.log('ok');
