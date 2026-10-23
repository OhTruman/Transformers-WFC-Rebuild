"""Campaign post-process chain (RE CAMPAIGN_POSTPROCESS_RE.md): a chapter's PostProcessVolumes as runtime data.

UE3 (RE): a view takes the highest-Priority enabled PostProcessVolume containing the view point, else the persistent
WorldInfo.DefaultPostProcessSettings; each settings group (Bloom / DOF / MotionBlur / Scene) blends over its own
*_InterpolationDuration (1 s default). Kismet toggles volumes (SeqAct_Toggle on a (Dynamic)PostProcessVolume) - the
renderer takes those by volume name (IRenderer::setPostProcessVolumeEnabled).

Reads (read-only): RE's AssetTools/manifests/authored.db (the volumes, their Settings over Default__PostProcessVolume, the
brush's BrushAggGeom convex elements, the world default over Default__WorldInfo), AssetTools' chapter.json (the units),
the cooked packages (each ColorCorrectionTexture, a Texture3D, decoded as build_lighting.postprocess does).
Writes <out>/postprocess_volumes.json and <out>/pp/<lut>.png (1024 x 32 strips, u = x + 32 z, v = y).

usage: build_campaign_pp.py <CHAPTER> <out dir>      e.g. D5_OME <campaign_render>/D5_OME
"""
import json, os, sqlite3, sys

DB = os.environ.get('WFC_AUTHORED_DB', r'F:/Transformers Rebuild/AssetTools/manifests/authored.db')
CAMPAIGN = os.environ.get('WFC_CAMPAIGN_ROOT', r'F:/Transformers Rebuild/AssetTools/out/campaign_render')
CLASSES = ('PostProcessVolume', 'DynamicPostProcessVolume')


def vec(v):
    return [v.get('X', 0.0), v.get('Y', 0.0), v.get('Z', 0.0)] if isinstance(v, dict) else v


def flat(settings):
    """authored.db FPostProcessSettings -> plain values (vectors as [x, y, z], object refs as their path)."""
    out = {}
    for k, v in (settings or {}).items():
        if isinstance(v, dict) and set(v) <= {'X', 'Y', 'Z'}:
            v = vec(v)
        out[k] = v
    return out


def ue_to_gltf(p):
    return [p[0] * 0.01, p[2] * 0.01, p[1] * 0.01]


def brush_polygons(db, vpath, props):
    """The volume's convex elements as triangles (glTF m): BrushAggGeom vertices are brush-local; world = local x DrawScale
    (x DrawScale3D) + Location (D5_OME PPV_12453: the local box + Location lands inside D5_OME_ART's bounds)."""
    bc = props.get('BrushComponent')
    row = db.execute('select props from objects where opath=?', (bc,)).fetchone() if bc else None
    if not row:
        return [], 'no BrushComponent'
    agg = (json.loads(row[0]) or {}).get('BrushAggGeom') or {}
    loc = vec(props.get('Location') or {'X': 0, 'Y': 0, 'Z': 0})
    s = float(props.get('DrawScale') or 1.0)
    s3 = vec(props.get('DrawScale3D') or {'X': 1, 'Y': 1, 'Z': 1})
    note = 'rotated (ignored)' if props.get('Rotation') else ''
    polys = []
    for el in agg.get('ConvexElems') or []:
        V = [vec(v) for v in el.get('VertexData') or []]
        W = [[loc[i] + V[j][i] * s * s3[i] for i in range(3)] for j in range(len(V))]
        T = el.get('FaceTriData') or []
        for t in range(0, len(T) - 2, 3):
            polys.append([ue_to_gltf(W[T[t]]), ue_to_gltf(W[T[t + 1]]), ue_to_gltf(W[T[t + 2]])])
    return polys, note


def decode_lut(tex, package, out):
    """A Texture3D LUT from its cooked package -> <out>/pp/<name>.png (build_lighting.postprocess's decode)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from PIL import Image
    import xbox_texture
    from ue3obj import Repo, COOKED
    cooked = {f.lower(): f for f in os.listdir(COOKED)}
    pk = cooked.get((package + '.xxx').lower())
    if not pk:
        return None
    repo = Repo([pk])
    if not repo.find(tex):
        return None
    o = repo.obj(tex) or {}
    sx, sy, sz = o.get('SizeX', 32), o.get('SizeY', 32), o.get('SizeZ', 32)
    _, tail = repo.native_tail(tex)
    vol = xbox_texture.decode_volume_argb8(tail[20:20 + sx * sy * sz * 4], sx, sy, sz)
    strip = vol.transpose(1, 0, 2, 3).reshape(sy, sz * sx, 4)
    os.makedirs(os.path.join(out, 'pp'), exist_ok=True)
    rel = 'pp/' + tex.split('.')[-1] + '.png'
    Image.fromarray(strip).save(os.path.join(out, rel))
    return {'object': tex, 'file': rel, 'size': [sx, sy, sz]}


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    chapter, out = sys.argv[1], sys.argv[2]
    cj = json.load(open(os.path.join(CAMPAIGN, chapter, 'chapter.json'), encoding='utf-8'))
    units = {s['level']: s['dir'] for s in cj.get('sublevels', [])}
    units.setdefault(cj['persistent_level'], cj['persistent_dir'])
    db = sqlite3.connect('file:%s?mode=ro' % DB.replace('\\', '/'), uri=True)

    def props(path):
        r = db.execute('select props from objects where opath=?', (path,)).fetchone()
        return json.loads(r[0]) if r and r[0] else {}
    vol_default = flat(props('Engine.Default__PostProcessVolume').get('Settings'))
    wi_default = flat(props('Engine.Default__WorldInfo').get('DefaultPostProcessSettings'))
    world = dict(wi_default)
    wi = db.execute("select opath, props from objects where class in ('TnWorldInfo', 'WorldInfo') and opath like ?",
                    (cj['persistent_level'] + '.%',)).fetchone()
    if wi:
        world.update(flat(json.loads(wi[1]).get('DefaultPostProcessSettings')))
    luts, lut_of = {}, {}

    def lut(ref, package):
        if not ref or not isinstance(ref, str):
            return None
        if ref not in lut_of:
            lut_of[ref] = decode_lut(ref, package, out)
            if lut_of[ref]:
                luts[ref] = lut_of[ref]
        return lut_of[ref]['file'] if lut_of[ref] else None
    res = {'chapter': chapter, 'generated_by': 'Rebuild tools/render/build_campaign_pp.py (RE CAMPAIGN_POSTPROCESS_RE.md)',
           'rule': 'highest-Priority enabled volume containing the view point, else world_default; groups blend over their '
                   '*_InterpolationDuration', 'world_default': world, 'world_lut': lut(world.get('ColorCorrectionTexture'),
                                                                                          cj['persistent_level']),
           'volumes': [], 'luts': luts}
    notes = []
    for path, cls, p in db.execute('select opath, class, props from objects where class in (%s)' % ','.join('?' * len(CLASSES)),
                                   CLASSES):
        level = path.split('.')[0]
        if level not in units or '.Default__' in path:
            continue
        d = json.loads(p) if p else {}
        s = dict(vol_default)
        s.update(flat(d.get('Settings')))
        polys, note = brush_polygons(db, path, d)
        if note:
            notes.append((path.split('.')[-1], note))
        # (a DynamicPostProcessVolume without an authored bEnabled starts off: Kismet switches it - A1's thumper beam 2045, hard
        # attached to the thumper's bone, its brush only the authored pose)
        enabled = d.get('bEnabled', cls != 'DynamicPostProcessVolume') is not False
        if d.get('Base'):
            notes.append((path.split('.')[-1], 'attached to %s (the brush at its authored pose)' % d.get('BaseBoneName', d['Base'].split('.')[-1])))
        res['volumes'].append({'name': path.split('.')[-1], 'unit': units[level], 'class': cls,
                               'priority': float(d.get('Priority', 0.0) or 0.0), 'enabled': enabled, 'attached': bool(d.get('Base')),
                               'settings': s, 'lut': lut(s.get('ColorCorrectionTexture'), level), 'polygons_m': polys})
    os.makedirs(out, exist_ok=True)
    json.dump(res, open(os.path.join(out, 'postprocess_volumes.json'), 'w', encoding='utf-8'), indent=1)
    print('%s: %d volumes (%d enabled, %d without a brush), %d LUTs decoded%s -> %s/postprocess_volumes.json'
          % (chapter, len(res['volumes']), sum(v['enabled'] for v in res['volumes']),
             sum(not v['polygons_m'] for v in res['volumes']), len(luts),
             '; notes: %s' % notes if notes else '', out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
