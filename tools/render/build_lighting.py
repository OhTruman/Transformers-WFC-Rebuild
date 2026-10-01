"""Extract a map's ORIGINAL baked/static lighting data for the runtime renderer.

Writes <out>/lighting.json:
  lightmaps.props   component object path -> {coeffs:[atlas x3], scales:[[r,g,b] x3], coordScale, coordBias}
                    (directional texture lightmap, FLightMap2D: 3 coefficients, verified in-shader basis)
  lights            every light component of the StaticLightCollectionActor(s), in glTF space, with
                    authored colour/brightness/radius/falloff/cone/channels (+ class defaults)
  fog               HeightFogComponent (Height, Density, LightColor, LightBrightness, Start/Extinction)
and copies all LightMapTexture2D atlases (umodel PNGs from <umodel_dir>) into <out>/lightmaps/.

    python build_lighting.py MP_IAC_Streets <out_dir> <umodel_png_dir>
"""
import json, os, shutil, struct, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue3obj import Repo, tags_to_dict, ASSETTOOLS  # noqa: E402
sys.path.insert(0, ASSETTOOLS)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Engine class defaults (Engine.xxx Default__*): verified by reading the CDOs.
LIGHT_DEFAULTS = {'Brightness': 1.0, 'LightColor': [255, 255, 255, 0], 'bEnabled': True, 'CastShadows': True,
                  'CastStaticShadows': True, 'CastDynamicShadows': True}
POINT_DEFAULTS = {'Radius': 1024.0, 'FalloffExponent': 2.0, 'ShadowFalloffExponent': 2.0}
SPOT_DEFAULTS = {'OuterConeAngle': 44.0, 'InnerConeAngle': 0.0}
CHANNEL_DEFAULTS = {'BSP': True, 'Static': True, 'Dynamic': True, 'CompositeDynamic': True}
FOG_DEFAULTS = {'bEnabled': True, 'Density': 5e-05, 'LightBrightness': 0.1, 'LightColor': [255, 255, 255, 0],
                'ExtinctionDistance': 100000000.0, 'StartDistance': 0.0, 'Height': 0.0}


def ue_to_gltf_pos(v):
    return [v[0] * 0.01, v[2] * 0.01, v[1] * 0.01]


def ue_to_gltf_dir(v):
    n = np.array([v[0], v[2], v[1]], 'f8')
    l = np.linalg.norm(n)
    return (n / l).tolist() if l > 0 else [0.0, -1.0, 0.0]


def lm_records(repo, pkg_index):
    p = repo.pkgs[pkg_index]; pr = repo.readers[pkg_index]
    shared = None
    out = {}
    kinds = {}

    def lm_name(idx):
        if idx > 0 and idx - 1 < len(p.exports) and p.class_name(p.exports[idx - 1]) == 'LightMapTexture2D':
            return p.obj_name(idx)
    for i, e in enumerate(p.exports):
        if p.class_name(e) != 'StaticMeshComponent': continue
        try:
            used = pr.read_object(i + 1)[1]
        except Exception:
            continue
        nat = p.data[e['serial_offset'] + used:e['serial_offset'] + e['serial_size']]
        if len(nat) <= 40: continue
        ltype = struct.unpack_from('<i', nat, 16)[0]
        if ltype in (0, 1, 2): kinds[ltype] = kinds.get(ltype, 0) + 1
        if shared is None and ltype == 2: shared = nat[21:37]
        if shared is None or ltype != 2: continue
        g = nat.find(shared)
        if g < 1: continue
        o = g + nat[g - 1] * 16 + 4
        co = []
        while o + 20 <= len(nat):
            nm = lm_name(struct.unpack_from('>i', nat, o)[0])
            if not nm: break
            co.append((nm, list(struct.unpack_from('>3f', nat, o + 4)))); o += 20
        if len(co) != 3 or o + 16 > len(nat): continue
        out[p.object_path(i + 1)] = {'coeffs': [c[0] for c in co], 'scales': [c[1] for c in co],
                                     'coordScale': list(struct.unpack_from('>2f', nat, o)),
                                     'coordBias': list(struct.unpack_from('>2f', nat, o + 8))}
    return out, kinds


def lights(repo, pkg_index):
    p = repo.pkgs[pkg_index]; pr = repo.readers[pkg_index]
    res = []
    for i, e in enumerate(p.exports):
        if p.class_name(e) != 'StaticLightCollectionActor': continue
        tags, used = pr.read_object(i + 1)
        d = tags_to_dict(tags)
        comps = [x.get('ref') if isinstance(x, dict) else None for x in (d.get('LightComponents') or [])]
        raw = p.data[e['serial_offset'] + used:e['serial_offset'] + e['serial_size']]
        mats = np.frombuffer(raw[:64 * len(comps)], '>f4').reshape(-1, 4, 4).astype('f8')
        for k, cref in enumerate(comps):
            if not cref: continue
            cls = repo.cls(cref)
            c = dict(LIGHT_DEFAULTS)
            if cls == 'PointLightComponent': c.update(POINT_DEFAULTS)
            if cls == 'SpotLightComponent': c.update(POINT_DEFAULTS); c.update(SPOT_DEFAULTS)
            c.update({k2: v for k2, v in (repo.obj(cref) or {}).items()})
            ch = dict(CHANNEL_DEFAULTS)
            for t in c.get('LightingChannels') or []:
                if isinstance(t, dict) and 'name' in t: ch[t['name']] = bool(t['value'])
            M = mats[k]
            pos = M[3, :3]; xaxis = M[0, :3]
            col = c.get('LightColor') or [255, 255, 255, 0]   # FColor stored B,G,R,A? -> see note
            res.append({
                'name': cref.split('.')[-1], 'class': cls,
                'position': ue_to_gltf_pos(pos), 'direction': ue_to_gltf_dir(xaxis),
                'color_srgb8': col[:3], 'brightness': c.get('Brightness'),
                'radius_m': (c.get('Radius') or 0) * 0.01, 'falloff_exponent': c.get('FalloffExponent'),
                'inner_cone_deg': c.get('InnerConeAngle'), 'outer_cone_deg': c.get('OuterConeAngle'),
                'enabled': bool(c.get('bEnabled', True)), 'cast_shadows': bool(c.get('CastShadows', True)),
                'cast_static_shadows': bool(c.get('CastStaticShadows', True)),
                'built_into_lightmap': bool(c.get('bHasLightEverBeenBuiltIntoLightMap', False)),
                'channels': ch, 'affects_classification': c.get('LightAffectsClassification'),
                'lower_color_srgb8': (c.get('LowerColor') or [255, 255, 255, 0])[:3],
                'lower_brightness': c.get('LowerBrightness', 0.0),
                'ue_matrix': M.tolist(),
            })
    return res


def fog(repo, pkg_index):
    p = repo.pkgs[pkg_index]
    for i, e in enumerate(p.exports):
        if p.class_name(e) == 'HeightFogComponent' and 'Default__' not in p.obj_name(i + 1):
            f = dict(FOG_DEFAULTS); f.update(repo.obj(p.object_path(i + 1)) or {})
            f['height_m'] = f['Height'] * 0.01
            return f
    return None


PF_INVISIBLE, PF_TWOSIDED, PF_PORTAL = 0x1, 0x100, 0x04000000


def parse_model_component(p, t, model_idx, lmset):
    """FModelComponent native tail (licensee 144): [1][Model][Elements.Num] then per element
    [pad][Component][Material][Nodes: TArray<WORD>][ShadowMaps: TArray<int>][IrrelevantLights:
    TArray<FGuid>][LightMapRef: type(2) LightGuids(int32 n x 16) 0 3x(tex, ScaleVector4)
    CoordinateScale(2f) CoordinateBias(2f)][TArray<FGuid>]. The tagged-property reader can end 2 bytes early, so
    anchor on the Model reference."""
    key = struct.pack('>i', model_idx)
    a = t.find(key, 0, 16)
    if a < 4: return None
    o = a + 4
    ne = struct.unpack_from('>i', t, o)[0]; o += 4
    els = []
    for _ in range(ne):
        _pad, comp, mat, nn = struct.unpack_from('>iiii', t, o); o += 16
        nodes = list(struct.unpack_from('>%dH' % nn, t, o)); o += 2 * nn
        ns = struct.unpack_from('>i', t, o)[0]; o += 4 + 4 * ns
        ni = struct.unpack_from('>i', t, o)[0]; o += 4 + 16 * ni
        lt = struct.unpack_from('>i', t, o)[0]; o += 4
        lm = None
        if lt == 2:
            ng = struct.unpack_from('>i', t, o)[0]; o += 4 + 16 * ng + 4
            co = []
            for _c in range(3):
                ref = struct.unpack_from('>i', t, o)[0]
                co.append((p.obj_name(ref) if ref in lmset else None, list(struct.unpack_from('>3f', t, o + 4))))
                o += 20
            cs = list(struct.unpack_from('>2f', t, o)); cb = list(struct.unpack_from('>2f', t, o + 8)); o += 16
            if all(c[0] for c in co):
                lm = {'coeffs': [c[0] for c in co], 'scales': [c[1] for c in co], 'coordScale': cs, 'coordBias': cb}
        elif lt != 0:
            raise ValueError('unsupported BSP lightmap type %d' % lt)
        ng2 = struct.unpack_from('>i', t, o)[0]  # trailing TArray<FGuid> (light GUIDs)
        o += 4 + 16 * ng2
        els.append({'material': mat, 'nodes': nodes, 'lightmap': lm})
    return els


def bsp_lighting(repo, out):
    """Rebuild the level BSP from the COOKED FModelVertexBuffer (TexCoord + ShadowTexCoord,
    indexed by FBspNode.iVertexIndex) split per ModelComponent element, with its lightmap."""
    import bsp as bspmod
    p = repo.pkgs[0]; pr = repo.readers[0]
    lmset = {i + 1 for i, e in enumerate(p.exports) if p.class_name(e) == 'LightMapTexture2D'}
    level = next(i + 1 for i, e in enumerate(p.exports) if p.class_name(e) == 'Level')
    models = [i + 1 for i, e in enumerate(p.exports) if p.class_name(e) == 'Model' and e['outer'] == level]
    best = None
    for mi in models:
        try:
            M = bspmod.read_model(p, mi)
            if best is None or len(M['nodes']) > len(best[1]['nodes']): best = (mi, M)
        except Exception:
            pass
    mi, M = best
    e = p.exports[mi - 1]
    t = p.data[e['serial_offset'] + M['end_offset']:e['serial_offset'] + e['serial_size']]
    vbo = None
    pts = M['points']
    for o in range(0, len(t) - 8, 4):
        es, n = struct.unpack_from('>ii', t, o)
        if es == 36 and o + 8 + 36 * n == len(t):
            vbo = o + 8; break
    if vbo is None: raise RuntimeError('FModelVertexBuffer not found')
    vb = np.frombuffer(t[vbo:], dtype=np.dtype([('pos', '>f4', 3), ('tx', '>u4'), ('tz', '>u4'),
                                                ('uv', '>f4', 2), ('suv', '>f4', 2)]))
    elements = []
    bad = 0
    for i, ex in enumerate(p.exports):
        if p.class_name(ex) != 'ModelComponent' or ex['outer'] != level: continue
        _, used = pr.read_object(i + 1)
        tail = p.data[ex['serial_offset'] + used - 8:ex['serial_offset'] + ex['serial_size']]
        try:
            els = parse_model_component(p, tail, mi, lmset)
        except Exception:
            els = None
        if els is None: bad += 1; continue
        for k, el in enumerate(els):
            el['key'] = '%s#%d' % (p.object_path(i + 1), k)
            m = el['material']
            el['material_path'] = (p.object_path(m) if m > 0 else p.full_path(m)) if m else None
            elements.append(el)
    # geometry per element
    prims = []
    lm_props = {}
    for el in elements:
        P, N, UV, SUV = [], [], [], []
        idx = []
        two = False
        for ni in el['nodes']:
            node = M['nodes'][ni]
            k = int(node['NumVertices'])
            if k < 3: continue
            s = M['surfs'][int(node['iSurf'])]
            fl = int(s['PolyFlags'])
            if fl & (PF_INVISIBLE | PF_PORTAL): continue
            two = two or bool(fl & PF_TWOSIDED)
            vi = int(node['iVertexIndex'])
            v = vb[vi:vi + k]
            gp = np.stack([v['pos'][:, 0], v['pos'][:, 2], v['pos'][:, 1]], 1).astype('f8') * 0.01
            nrm = M['vectors'][int(s['vNormal'])]
            gn = np.array([nrm[0], nrm[2], nrm[1]], 'f8'); gn /= (np.linalg.norm(gn) or 1)
            tris = [(0, j, j + 1) for j in range(1, k - 1)]
            c = np.cross(gp[1] - gp[0], gp[2] - gp[0])
            if np.dot(c, gn) < 0: tris = [(a, cc, b) for a, b, cc in tris]
            base = len(P)
            for j in range(k):
                P.append(gp[j]); N.append(gn); UV.append(v['uv'][j]); SUV.append(v['suv'][j])
            for tr in tris: idx += [base + x for x in tr]
        if not idx: continue
        prims.append({'key': el['key'], 'material': el['material_path'], 'two_sided': two,
                      'P': np.array(P, 'f4'), 'N': np.array(N, 'f4'), 'UV': np.array(UV, 'f4'),
                      'SUV': np.array(SUV, 'f4'), 'I': np.array(idx, 'u4')})
        if el['lightmap']: lm_props[el['key']] = el['lightmap']
    write_glb(os.path.join(out, 'bsp.glb'), prims)
    print('BSP: %d elements (%d lightmapped), %d unparsed components, %d tris' %
          (len(prims), len(lm_props), bad, sum(len(q['I']) // 3 for q in prims)))
    return lm_props


def write_glb(path, prims):
    bin_ = bytearray()
    acc, views, meshes, nodes, mats = [], [], [], [], []
    matidx = {}

    def add(arr, typ, target):
        nonlocal bin_
        while len(bin_) % 4: bin_ += b'\0'
        off = len(bin_); bin_ += arr.tobytes()
        views.append({'buffer': 0, 'byteOffset': off, 'byteLength': arr.nbytes, 'target': target})
        a = {'bufferView': len(views) - 1, 'componentType': 5125 if arr.dtype == np.uint32 else 5126,
             'count': int(arr.shape[0]), 'type': typ}
        if typ == 'VEC3' and arr.dtype != np.uint32:
            a['min'] = arr.min(0).tolist(); a['max'] = arr.max(0).tolist()
        acc.append(a)
        return len(acc) - 1
    for q in prims:
        mk = (q['material'], q['two_sided'])
        if mk not in matidx:
            mats.append({'name': (q['material'] or 'none').split('.')[-1], 'doubleSided': q['two_sided'],
                         'extras': {'wfc_material': q['material']}})
            matidx[mk] = len(mats) - 1
        attrs = {'POSITION': add(q['P'], 'VEC3', 34962), 'NORMAL': add(q['N'], 'VEC3', 34962),
                 'TEXCOORD_0': add(q['UV'], 'VEC2', 34962), 'TEXCOORD_1': add(q['SUV'], 'VEC2', 34962)}
        meshes.append({'primitives': [{'attributes': attrs, 'indices': add(q['I'], 'SCALAR', 34963),
                                       'material': matidx[mk]}]})
        nodes.append({'name': q['key'].split('.')[-1], 'mesh': len(meshes) - 1,
                      'extras': {'component': q['key'], 'kind': 'bsp_element'}})
    j = {'asset': {'version': '2.0', 'generator': 'Rebuild-Rendering tools/render/build_lighting.py'},
         'scene': 0, 'scenes': [{'nodes': list(range(len(nodes)))}], 'nodes': nodes, 'meshes': meshes,
         'materials': mats, 'accessors': acc, 'bufferViews': views, 'buffers': [{'byteLength': len(bin_)}]}
    js = json.dumps(j).encode()
    while len(js) % 4: js += b' '
    while len(bin_) % 4: bin_ += b'\0'
    total = 12 + 8 + len(js) + 8 + len(bin_)
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, total))
        f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js)
        f.write(struct.pack('<II', len(bin_), 0x004E4942)); f.write(bytes(bin_))


def postprocess(mapname, out):
    """Persistent-level post settings: the map's TnWorldInfo.DefaultPostProcessSettings (the BASE
    package is the persistent level: TransLevels.ini MapFilename) layered over Engine
    Default__WorldInfo, plus the authored ColorCorrectionTexture (Texture3D CLUT) decoded from its
    Xbox-tiled A8R8G8B8 volume (xbox_texture.decode_volume_argb8) into a 1024x32 PNG strip
    (u = x + 32*z, v = y)."""
    from PIL import Image
    import xbox_texture
    eng = Repo(['Engine.xxx'])
    base = Repo(['%s_BASE_m.xxx' % mapname])

    def settings(tags):
        d = {}
        for t in tags or []:
            v = t['value']
            if isinstance(v, dict) and 'ref' in v: v = v['ref']
            elif isinstance(v, list) and v and isinstance(v[0], dict) and 'name' in v[0]: v = {x['name']: x['value'] for x in v}
            d[t['name']] = v
        return d
    wi_def = eng.obj('Engine.Default__WorldInfo') or {}
    pp = settings(wi_def.get('DefaultPostProcessSettings'))
    src = {k: 'Engine.Default__WorldInfo' for k in pp}
    wi = next((path for path in base.index if base.cls(path) == 'TnWorldInfo' and 'default__' not in path), None)
    over = settings((base.obj(wi) or {}).get('DefaultPostProcessSettings')) if wi else {}
    for k, v in over.items(): pp[k] = v; src[k] = wi
    res = {'settings': pp, 'source': src, 'world_info': wi, 'clut': None}
    clut = pp.get('ColorCorrectionTexture')
    if clut and base.find(clut):
        o = base.obj(clut) or {}
        sx, sy, sz = o.get('SizeX', 32), o.get('SizeY', 32), o.get('SizeZ', 32)
        _, tail = base.native_tail(clut)
        # bulk header [flags][count][size][offset][extra] then the top mip (verified: offset 20 decodes
        # to a smooth near-identity grade; offset 16 does not)
        vol = xbox_texture.decode_volume_argb8(tail[20:20 + sx * sy * sz * 4], sx, sy, sz)
        strip = vol.transpose(1, 0, 2, 3).reshape(sy, sz * sx, 4)       # row y, column z*sx + x
        fn = os.path.join(out, 'clut.png')
        Image.fromarray(strip).save(fn)
        res['clut'] = {'object': clut, 'file': fn.replace(os.sep, '/'), 'size': [sx, sy, sz],
                       'srgb': bool(o.get('SRGB', False))}       # Default__Texture3D SRGB=False
    return res


def main():
    mapname, out, umodel_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(os.path.join(out, 'lightmaps'), exist_ok=True)
    repo = Repo(['%s_ART_m.xxx' % mapname])
    props, kinds = lm_records(repo, 0)
    bsp_props = bsp_lighting(repo, out)
    props.update(bsp_props)
    L = lights(repo, 0)
    F = fog(repo, 0)
    PP = postprocess(mapname, out)
    atl = sorted({a for r in props.values() for a in r['coeffs']})
    copied = 0
    for root, _, files in os.walk(umodel_dir):
        for fn in files:
            if fn.startswith('LightMapTexture2D_') and fn.endswith('.png'):
                shutil.copyfile(os.path.join(root, fn), os.path.join(out, 'lightmaps', fn)); copied += 1
    json.dump({'map': mapname,
               'note': 'Directional lightmaps: L = sum_i dot(N_t, B_i)^2 * tex_i.rgb(sRGB-decoded) * scales[i]; '
                       'B0=(0,sqrt(2/3),1/sqrt3) B1=(-1/sqrt2,-1/sqrt6,1/sqrt3) B2=(1/sqrt2,-1/sqrt6,1/sqrt3) '
                       '(decoded from the original Xenon base-pass shader microcode).',
               'lightmap_type_counts': kinds,
               'lightmaps': {'props': props, 'atlases': atl},
               'lights': L, 'fog': F, 'postprocess': PP},
              open(os.path.join(out, 'lighting.json'), 'w'), indent=0)
    from collections import Counter
    print('lightmapped components: %d (types %s); atlases %d; copied %d PNGs' % (len(props), kinds, len(atl), copied))
    print('lights:', Counter(l['class'] for l in L))
    print('postprocess:', {k: v for k, v in PP['settings'].items() if 'Bloom' in k or 'DOF' in k or 'Scene' in k or 'Color' in k})
    print('fog:', {k: F[k] for k in ('Height', 'Density', 'LightColor', 'LightBrightness', 'StartDistance', 'ExtinctionDistance')})


if __name__ == '__main__':
    main()
