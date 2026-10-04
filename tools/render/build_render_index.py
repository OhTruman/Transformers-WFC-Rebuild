"""Generic map render index -> the runtime render_index.json schema the renderer reads (<out>/render_index.json).

[integration M06] Streets ships a hand-validated runtime index (ExtractedAssets/VerticalSlice/Maps/MP_IAC_Streets/
render_index.json, AssetTools m04_complete.py). Every other multiplayer map ships only the generic index of the
production map pipeline (AssetTools manifests/maps/<MAP>/render_index_generic.json, map_complete.py; AssetTools
MAP_PIPELINE.md / MAP_GORGE_COMPLETE.md). This tool converts the part of the generic index whose runtime form can be
checked against Streets field by field, and nothing else:

  * pickup_factory_visuals: ammo-crate meshes (PickupFactoryMesh) and the health / overshield factories as their
    "mesh particle" kind (the renderer skips those: their mesh is drawn by the pickup FX emitter, as on Streets).

Not converted (PARTIAL, reported): Conquest totems, KOTH zone rings and destructible state meshes (the generic index
lists those actors by name / in another schema; their runtime entries are not derived here), objective-factory
resting meshes (CTF / EXT, not implemented by Gameplay).

Verification: `--check` converts Streets' generic index and compares every converted field with Streets' runtime
index (actor, mesh, glTF path, UE matrix, yaw rate, cull distance).

usage: build_render_index.py <MAP> <out_dir>      (no-op when the map ships a runtime render_index.json)
       build_render_index.py --check
"""
import json, os, re, sys

VS_MAPS = r'F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps'
CONTENT = r'F:/Transformers Rebuild/ExtractedAssets/content'
MANI = os.environ.get('WFC_ASSETTOOLS_MANIFESTS', r'F:/Transformers Rebuild/AssetTools/manifests')

# Class template values the generic index does not repeat per factory. Taken from Streets' runtime index, where every
# factory of the class carries the same value (TransGame.Default__TnAmmoCratePickup.MeshComponentA) [CONF data].
AMMO_CULL_DISTANCE = 8000.0


def gltf_to_ue(m):
    """glTF column-major 4x4 (metres, Y up: glTF (x, y, z) = UE (x, z, y) / 100) -> UE row-vector matrix rows."""
    # glTF basis vectors (columns 0..2) and translation (column 3)
    col = [[m[c * 4 + r] for r in range(4)] for c in range(4)]
    P = lambda v: [v[0], v[2], v[1]]          # swap Y / Z
    # UE row i = image of UE axis i = P(glTF column P(i))
    axis = {0: 0, 1: 2, 2: 1}
    rows = [P(col[axis[i]][:3]) + [0.0] for i in range(3)]
    t = P(col[3][:3])
    rows.append([t[0] * 100.0, t[1] * 100.0, t[2] * 100.0, 1.0])
    return rows


def mesh_gltf(mesh):
    """Object path Package.Group.Name -> content/Package/Group/Name.gltf (the AssetTools export layout)."""
    return 'content/' + mesh.replace('.', '/') + '.gltf'


def yaw_rate(text):
    m = re.search(r'PickupRotationRate\s+(-?\d+)', text or '')
    return int(m.group(1)) if m else 0


def convert(mapname, generic):
    out = {'map': mapname,
           'generated_by': 'Rebuild tools/render/build_render_index.py from AssetTools manifests/maps/%s/'
                           'render_index_generic.json (pickup visuals only; see the tool docstring)' % mapname,
           'pickup_factory_visuals': [],
           'not_in_world_glb (authored renderables)': []}
    missing = []
    for f in generic.get('pickup_factories', []):
        cls, vis = f.get('class', ''), f.get('visual') or {}
        mesh = vis.get('mesh', '')
        e = {'actor': '%s_BASE_m.TheWorld.PersistentLevel.%s' % (mapname, f['actor']), 'class': cls, 'mesh': mesh,
             'gltf': mesh_gltf(mesh) if mesh else '', 'ue_matrix': gltf_to_ue(f['gltf_matrix_factory']),
             'gltf_matrix': f['gltf_matrix_factory'],
             'rotation_rate': {'Pitch': 0, 'Yaw': yaw_rate(vis.get('rotation')), 'Roll': 0}}
        if cls == 'TnAmmoCratePickupFactory':
            e['kind'] = 'ammo crate pickup mesh (inventory PickupFactoryMesh)'
            e['CullDistance'] = AMMO_CULL_DISTANCE
        else:
            e['kind'] = '%s mesh particle (%s)' % (cls, vis.get('beam', ''))
        if e['gltf'] and not os.path.exists(os.path.join(CONTENT, e['gltf'][8:])):
            missing.append(e['gltf'])
        out['pickup_factory_visuals'].append(e)
    return out, missing


def check():
    rt = json.load(open(os.path.join(VS_MAPS, 'MP_IAC_Streets', 'render_index.json'), encoding='utf-8'))
    gen = json.load(open(os.path.join(MANI, 'maps', 'MP_IAC_Streets', 'render_index_generic.json'), encoding='utf-8'))
    conv, missing = convert('MP_IAC_Streets', gen)
    ref = {e['actor'].split('.')[-1]: e for e in rt['pickup_factory_visuals']
           if 'ammo crate' in e['kind']}
    bad = 0
    n = 0
    for e in conv['pickup_factory_visuals']:
        if 'ammo crate' not in e['kind']:
            continue
        n += 1
        r = ref.get(e['actor'].split('.')[-1])
        if not r:
            print('MISSING in runtime index', e['actor']); bad += 1; continue
        errs = []
        for k in ('mesh', 'gltf'):
            if e[k] != r[k]: errs.append('%s %s != %s' % (k, e[k], r[k]))
        d = max(abs(e['ue_matrix'][i][j] - r['ue_matrix'][i][j]) for i in range(4) for j in range(4))
        if d > 0.05: errs.append('ue_matrix differs by %.4f' % d)
        if e['rotation_rate']['Yaw'] != r['rotation_rate']['Yaw']: errs.append('yaw')
        if abs(e['CullDistance'] - r.get('CullDistance', 0)) > 1e-3: errs.append('cull')
        if errs: bad += 1; print('DIFF', e['actor'], '; '.join(errs))
    print('check: %d ammo-crate visuals converted from the generic index, %d differ from the runtime index, '
          '%d runtime entries, %d missing glTF' % (n, bad, len(ref), len(missing)))
    return 0 if bad == 0 and n == len(ref) and not missing else 1


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--check':
        sys.exit(check())
    mapname, out_dir = sys.argv[1], sys.argv[2]
    if os.path.exists(os.path.join(VS_MAPS, mapname, 'render_index.json')):
        print('render index: %s ships a runtime render_index.json (used as is)' % mapname)
        return
    gp = os.path.join(MANI, 'maps', mapname, 'render_index_generic.json')
    if not os.path.exists(gp):
        print('render index: no runtime or generic index for %s' % mapname)
        return
    conv, missing = convert(mapname, json.load(open(gp, encoding='utf-8')))
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, 'render_index.json'), 'w', encoding='utf-8') as f:
        json.dump(conv, f, indent=1)
    print('render index: %s from the generic index: %d pickup visuals (%d ammo-crate meshes), %d missing glTF; '
          'totems / KOTH rings / destructibles not converted (PARTIAL)'
          % (mapname, len(conv['pickup_factory_visuals']),
             sum('ammo crate' in e['kind'] for e in conv['pickup_factory_visuals']), len(missing)))


if __name__ == '__main__':
    main()
