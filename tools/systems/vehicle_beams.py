"""INVENTED PLACEMENT (Integration 2026-10-16): per-vehicle headlight sockets derived from the emissive lamp areas of each MP
vehicle form (the original authors no headlight light: SYSTEMS_VEHICLE_LIGHTS.md). Per chassis (ExtractedAssets/VerticalSlice/
Characters/<id>/vehicle.glb + textures/vehicle_<n>_<material>_emissive.png, read only):
  - every triangle of the vehicle mesh in bind pose (glTF Y-up metres; UE +X = glTF +X = forward, checked against the rear
    JumpBoostSocket at -X), its emissive luminance sampled at the UV centroid;
  - lamp triangles: emissive luminance >= LUM, normal facing forward (n.x >= 0.5), in the front third of the body length;
  - clusters: single-linkage at 0.15 m over the lamp-triangle centroids; clusters under MIN_AREA m2 dropped; per cluster the
    area-weighted centroid, mean normal (the beam direction, forward-tilted), mean emissive colour (linear) and area;
  - mirrored pairs (|y| matching within 5 cm, same x / z) are reported as one L / R pair.
Writes json + markdown. argv[1] = out json, argv[2] = out md."""
import json, os, struct, sys, glob, re
import numpy as np
from PIL import Image

ROOT = 'F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Characters'
LUM, MIN_AREA, LINK = 0.35, 0.0015, 0.15

def glb(path):
    b = open(path, 'rb').read()
    n = struct.unpack_from('<I', b, 12)[0]
    g = json.loads(b[20:20 + n])
    off = 20 + n
    bn = struct.unpack_from('<I', b, off)[0]
    return g, b[off + 8: off + 8 + bn]

def acc(g, bin_, i):
    a = g['accessors'][i]; bv = g['bufferViews'][a['bufferView']]
    comp = {5126: np.float32, 5123: np.uint16, 5125: np.uint32, 5121: np.uint8}[a['componentType']]
    k = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
    start = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    stride = bv.get('byteStride', 0) or k * np.dtype(comp).itemsize
    raw = np.frombuffer(bin_, dtype=np.uint8, count=stride * (a['count'] - 1) + k * np.dtype(comp).itemsize, offset=start)
    out = np.lib.stride_tricks.as_strided(raw.view(comp) if stride == k * np.dtype(comp).itemsize else raw,
                                          shape=(a['count'], k), strides=(stride, np.dtype(comp).itemsize)) if stride == k * np.dtype(comp).itemsize else \
          np.stack([np.frombuffer(bin_, dtype=comp, count=k, offset=start + j * stride) for j in range(a['count'])])
    return np.array(out, dtype=np.float64).reshape(a['count'], k)

def clusters(pts, link):
    n = len(pts); lab = -np.ones(n, int); c = 0
    for i in range(n):
        if lab[i] >= 0: continue
        stack = [i]; lab[i] = c
        while stack:
            j = stack.pop()
            near = np.nonzero((lab < 0) & (np.linalg.norm(pts - pts[j], axis=1) < link))[0]
            lab[near] = c; stack.extend(near.tolist())
        c += 1
    return lab, c

res = {}
for d in sorted(glob.glob(ROOT + '/*/vehicle.glb')):
    cid = os.path.basename(os.path.dirname(d))
    ch = json.load(open(os.path.join(os.path.dirname(d), 'character.json'), encoding='utf-8'))
    g, bin_ = glb(d)
    texdir = os.path.join(os.path.dirname(d), 'textures')
    lamp = []   # (centroid, normal, area, rgb)
    for mesh in g['meshes']:
        for prim in mesh['primitives']:
            mi = prim.get('material')
            mname = g['materials'][mi]['name'] if mi is not None else ''
            em = [f for f in glob.glob(os.path.join(texdir, 'vehicle_*_emissive.png')) if re.sub(r'^vehicle_\d+_|_emissive\.png$', '', os.path.basename(f)) == mname]
            if not em or 'TEXCOORD_0' not in prim['attributes']:
                continue
            E = np.asarray(Image.open(em[0]).convert('RGB'), dtype=np.float64) / 255.0
            P = acc(g, bin_, prim['attributes']['POSITION'])[:, :3]
            UV = acc(g, bin_, prim['attributes']['TEXCOORD_0'])[:, :2]
            I = acc(g, bin_, prim['indices']).astype(int).reshape(-1, 3) if 'indices' in prim else np.arange(len(P)).reshape(-1, 3)
            a, b, c = P[I[:, 0]], P[I[:, 1]], P[I[:, 2]]
            cr = np.cross(b - a, c - a); area = 0.5 * np.linalg.norm(cr, axis=1)
            nrm = cr / np.maximum(np.linalg.norm(cr, axis=1, keepdims=True), 1e-9)
            cen = (a + b + c) / 3.0
            uv = (UV[I[:, 0]] + UV[I[:, 1]] + UV[I[:, 2]]) / 3.0
            h, w = E.shape[:2]
            px = np.clip((uv[:, 0] % 1.0) * w, 0, w - 1).astype(int); py = np.clip((uv[:, 1] % 1.0) * h, 0, h - 1).astype(int)
            rgb = E[py, px]
            lum = rgb @ np.array([0.2126, 0.7152, 0.0722])
            xmin, xmax = P[:, 0].min(), P[:, 0].max()
            sel = (lum >= LUM) & (nrm[:, 0] >= 0.5) & (cen[:, 0] >= xmax - (xmax - xmin) / 3.0) & (area > 0)
            for k in np.nonzero(sel)[0]:
                lamp.append((cen[k], nrm[k], area[k], rgb[k] ** 2.2))
    if not lamp:
        res[cid] = {'form': ch['vehicle'].get('vehicle_form'), 'lamps': [], 'note': 'no forward-facing emissive area'}
        continue
    pts = np.array([l[0] for l in lamp]); lab, nc = clusters(pts, LINK)
    lamps = []
    for c in range(nc):
        m = lab == c
        ar = np.array([l[2] for l, mm in zip(lamp, m) if mm])
        if ar.sum() < MIN_AREA:
            continue
        wts = ar / ar.sum()
        cen = (pts[m] * wts[:, None]).sum(0)
        n = (np.array([l[1] for l, mm in zip(lamp, m) if mm]) * wts[:, None]).sum(0); n /= np.linalg.norm(n)
        col = (np.array([l[3] for l, mm in zip(lamp, m) if mm]) * wts[:, None]).sum(0)
        lamps.append({'pos_m': [round(float(v), 3) for v in cen], 'dir': [round(float(v), 3) for v in n], 'emissive_linear': [round(float(v), 3) for v in col],
                      'area_m2': round(float(ar.sum()), 4)})
    lamps.sort(key=lambda l: (-l['area_m2']))
    # mirrored pairs: (x, height) equal within 5 cm, lateral coordinate (glTF z) opposite
    pairs, used = [], set()
    for i, l in enumerate(lamps):
        if i in used: continue
        for j in range(i + 1, len(lamps)):
            if j in used: continue
            p, q = l['pos_m'], lamps[j]['pos_m']
            if abs(p[0] - q[0]) < 0.05 and abs(p[1] - q[1]) < 0.05 and abs(p[2] + q[2]) < 0.05 and abs(p[2]) > 0.05:
                pairs.append({'L': l if p[2] > 0 else lamps[j], 'R': lamps[j] if p[2] > 0 else l}); used |= {i, j}; break
    singles = [l for i, l in enumerate(lamps) if i not in used]
    res[cid] = {'form': ch['vehicle'].get('vehicle_form'), 'display': ch.get('display', {}).get('name') if isinstance(ch.get('display'), dict) else ch.get('display'),
                'pairs': pairs, 'singles': singles}
json.dump({'note': 'INVENTED PLACEMENT: derived from emissive lamp areas (no original headlight lights exist); glTF Y-up metres, '
                   'vehicle root space, +X forward, +Z left (UE -Y); Realistic-only addition', 'params': {'lum': LUM, 'min_area_m2': MIN_AREA,
                   'link_m': LINK}, 'vehicles': res}, open(sys.argv[1], 'w', encoding='utf-8'), indent=1)
def pick(v):
    """the headlight pair: the mirrored pair facing most directly forward (dir.x), then frontmost; else the most forward-facing
    single on the centre line; confidence high = a pair with dir.x >= 0.9, medium = a pair with dir.x >= 0.7 or a single with
    dir.x >= 0.9, low otherwise"""
    ps = sorted(v.get('pairs', []), key=lambda p: (-round(p['L']['dir'][0], 1), -p['L']['pos_m'][0]))
    if ps:
        l = ps[0]['L']
        return {'kind': 'pair', 'L': l, 'R': ps[0]['R'], 'confidence': 'high' if l['dir'][0] >= 0.9 else 'medium' if l['dir'][0] >= 0.7 else 'low'}
    ss = sorted(v.get('singles', []), key=lambda s: (-round(s['dir'][0], 1), -s['area_m2']))
    if ss:
        return {'kind': 'single', 'C': ss[0], 'confidence': 'medium' if ss[0]['dir'][0] >= 0.9 else 'low'}
    return {'kind': 'none', 'confidence': 'none'}
for cid, v in res.items():
    v['pick'] = pick(v)
json.dump({'note': 'INVENTED PLACEMENT: derived from emissive lamp areas (no original headlight lights exist); glTF Y-up metres, '
                   'vehicle root space, +X forward, +Z left (UE +Y); Realistic-only addition', 'params': {'lum': LUM, 'min_area_m2': MIN_AREA,
                   'link_m': LINK}, 'vehicles': res}, open(sys.argv[1], 'w', encoding='utf-8'), indent=1)
L = ['# Vehicle headlight sockets: INVENTED PLACEMENT (Realistic-only addition)', '',
     'The original authors no vehicle headlights (SYSTEMS_VEHICLE_LIGHTS.md): this table is an addition, approved by Integration '
     '2026-10-16, for Realistic only. Sockets come from each MP vehicle emissive lamp areas (tools/systems/vehicle_beams.py: '
     'forward-facing triangles, n.x >= 0.5, in the front third whose emissive texture is bright, clustered at 0.15 m; mirrored '
     'pairs matched). Pick = the mirrored pair facing most directly forward, then frontmost. Positions in vehicle root space '
     '(the vehicle.glb bind pose), glTF Y-up metres, +X forward, +Z = UE +Y; the R lamp mirrors z. dir = the lamp surface normal '
     '(aim the spot along it, or along +X with the pitch from dir.y). Emissive = the lamp texture colour, linear: mostly the '
     'Autobot-blue energon glow of the default skin, so Rendering picks the light colour (team colour or warm white). '
     'Confidence: high = a forward-facing pair (dir.x >= 0.9); medium / low = angled or energon-trim lamps; none = no forward lamp '
     '(skip, or place by hand). Full clusters: SYSTEMS_VEHICLE_BEAMS.json.', '',
     '| vehicle | form | pick | lamp pos m (L; R = z mirrored) | dir | emissive (linear) | area m2 | confidence |', '|---|---|---|---|---|---|---|---|']
for cid, v in res.items():
    k = v['pick']
    if k['kind'] == 'none':
        L.append('| %s | %s | none | | | | | none |' % (cid, v['form'])); continue
    l = k['L'] if k['kind'] == 'pair' else k['C']
    L.append('| %s | %s | %s | %s | %s | %s | %.4f | %s |' % (cid, v['form'], k['kind'], l['pos_m'], l['dir'], l['emissive_linear'], l['area_m2'], k['confidence']))
open(sys.argv[2], 'w', encoding='utf-8').write('\n'.join(L) + '\n')
print('\n'.join(L))
