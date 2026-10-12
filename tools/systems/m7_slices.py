# slices.py: M7 stage 0 - per map, the static world's triangles by blend mode (BLAS flags: opaque / alpha-tested /
# excluded) and the instancing ratio (unique mesh triangles vs triangles as placed), from world.glb's own node / mesh /
# material structure (materials carry extras.blend_mode). Reads the shared ExtractedAssets read-only.
import json, struct, sys
from collections import Counter

ROOT = 'F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps/'
MAPS = ['MP_ESC_BrokenHope', 'MP_ESC_Remnant', 'MP_IAC_Berth', 'MP_IAC_Rust', 'MP_IAC_Seed', 'MP_IAC_Streets', 'MP_KON_Molten',
        'MP_ORB_Debris', 'MP_UND_Complex', 'MP_UND_Gorge']

def glb_json(path):
    d = open(path, 'rb').read()
    n = struct.unpack_from('<I', d, 12)[0]
    return json.loads(d[20:20 + n])

print('%-18s %9s | %8s %8s %8s %8s | %9s %6s %6s' % ('map', 'placed M', 'opaque', 'masked', 'transl', 'other', 'unique M', 'ratio', 'meshes'))
tot = Counter()
for m in MAPS:
    j = glb_json(ROOT + m + '/world.glb')
    acc = j['accessors']; mats = j.get('materials', [])
    def blend(mi):
        if mi is None or mi >= len(mats): return 'BLEND_Opaque'
        return (mats[mi].get('extras') or {}).get('blend_mode', 'BLEND_Opaque')
    mesh_tris = []
    mesh_blend = []
    for me in j['meshes']:
        t = Counter()
        for pr in me['primitives']:
            if pr.get('mode', 4) != 4 or 'indices' not in pr: continue
            t[blend(pr.get('material'))] += acc[pr['indices']]['count'] // 3
        mesh_blend.append(t)
        mesh_tris.append(sum(t.values()))
    placed = Counter(); used = set()
    for nd in j['nodes']:
        if 'mesh' not in nd: continue
        used.add(nd['mesh'])
        placed.update(mesh_blend[nd['mesh']])
    allp = sum(placed.values())
    uniq = sum(mesh_tris[i] for i in used)
    op = placed['BLEND_Opaque']; ms = placed['BLEND_Masked']
    tr = placed['BLEND_Translucent'] + placed['BLEND_Additive'] + placed['BLEND_Modulate'] + placed['BLEND_AlphaComposite']
    other = allp - op - ms - tr
    print('%-18s %9.2f | %7.1f%% %7.1f%% %7.1f%% %7.1f%% | %9.2f %5.2fx %6d' % (m, allp / 1e6, 100 * op / allp, 100 * ms / allp, 100 * tr / allp,
          100 * other / allp, uniq / 1e6, allp / max(1, uniq), len(used)))
    tot.update(placed)
print('blend modes seen (placed triangles, all maps):', {k: round(v / 1e6, 3) for k, v in tot.most_common()})
