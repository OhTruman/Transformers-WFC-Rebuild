"""Vertical ray queries against the AUTHORED pawn collision world (ExtractedAssets collision_pawn.glb).

Independent of the product: used by transform-stress.ps1 to judge where a pawn ended up.

    python collision_query.py <collision_pawn.glb> <queries.json> <out.json>

queries.json: [{"id": "...", "x": .., "y": .., "z": ..}, ...]  (glTF metres; y up)
out.json: per query
  floor_below   highest surface at or below y + 0.5 (null: none within 200 m = void / outside the map)
  above_up      nearest UP-facing surface above y + 0.1 within 6 m (a floor the point is underneath)
  above_any     nearest surface of any facing above y + 0.1 within 6 m
  thickness     distance from floor_below down to the next surface below it (thin floors)
"""
import json, struct, sys
import numpy as np


def load_glb(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'glTF'
    off, js, bin_ = 12, None, None
    while off < len(b):
        ln, typ = struct.unpack_from('<I4s', b, off)
        chunk = b[off + 8: off + 8 + ln]
        if typ == b'JSON': js = json.loads(chunk)
        elif typ[:3] == b'BIN': bin_ = chunk
        off += 8 + ln
    return js, bin_


def accessor(js, bin_, i):
    a = js['accessors'][i]; bv = js['bufferViews'][a['bufferView']]
    comp = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a['componentType']]
    ncomp = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4, 'VEC2': 2}[a['type']]
    start = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    stride = bv.get('byteStride', 0)
    n = a['count']
    if stride and stride != ncomp * np.dtype(comp).itemsize:
        raw = np.frombuffer(bin_, dtype=np.uint8, count=stride * n, offset=start).reshape(n, stride)
        return raw[:, :ncomp * np.dtype(comp).itemsize].copy().view(comp).reshape(n, ncomp)
    return np.frombuffer(bin_, dtype=comp, count=n * ncomp, offset=start).reshape(n, ncomp) if ncomp > 1 else np.frombuffer(bin_, dtype=comp, count=n, offset=start)


def node_matrix(n):
    if 'matrix' in n: return np.array(n['matrix'], dtype=np.float64).reshape(4, 4).T
    m = np.eye(4)
    if 'scale' in n: m = np.diag(list(n['scale']) + [1.0]) @ m
    if 'rotation' in n:
        x, y, z, w = n['rotation']
        r = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                      [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                      [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
        R = np.eye(4); R[:3, :3] = r; m = R @ m
    if 'translation' in n: T = np.eye(4); T[:3, 3] = n['translation']; m = T @ m
    return m


def triangles(path):
    js, bin_ = load_glb(path)
    tris = []
    def walk(ni, parent):
        n = js['nodes'][ni]; m = parent @ node_matrix(n)
        if 'mesh' in n:
            for pr in js['meshes'][n['mesh']]['primitives']:
                p = accessor(js, bin_, pr['attributes']['POSITION']).astype(np.float64)
                p = (np.c_[p, np.ones(len(p))] @ m.T)[:, :3]
                idx = accessor(js, bin_, pr['indices']).astype(np.int64) if 'indices' in pr else np.arange(len(p))
                tris.append(p[idx.reshape(-1, 3)])
        for c in n.get('children', []): walk(c, m)
    for s in js.get('scenes', [{'nodes': list(range(len(js['nodes'])))}]):
        for ni in s['nodes']: walk(ni, np.eye(4))
    return np.concatenate(tris, axis=0)


def main():
    T = triangles(sys.argv[1])
    Q = json.load(open(sys.argv[2]))
    a, b, c = T[:, 0], T[:, 1], T[:, 2]
    nrm = np.cross(b - a, c - a); nl = np.linalg.norm(nrm, axis=1); keep = nl > 1e-9
    a, b, c, nrm, nl = a[keep], b[keep], c[keep], nrm[keep], nl[keep]
    ny = nrm[:, 1] / nl
    # XZ grid binning (4 m)
    cell = 4.0
    mn = np.minimum(np.minimum(a, b), c); mx = np.maximum(np.maximum(a, b), c)
    gx0 = np.floor(mn[:, 0] / cell).astype(int); gx1 = np.floor(mx[:, 0] / cell).astype(int)
    gz0 = np.floor(mn[:, 2] / cell).astype(int); gz1 = np.floor(mx[:, 2] / cell).astype(int)
    grid = {}
    for i in range(len(a)):
        for gx in range(gx0[i], gx1[i] + 1):
            for gz in range(gz0[i], gz1[i] + 1):
                grid.setdefault((gx, gz), []).append(i)
    grid = {k: np.array(v) for k, v in grid.items()}
    out = []
    for q in Q:
        x, y, z = float(q['x']), float(q['y']), float(q['z'])
        ids = grid.get((int(np.floor(x / cell)), int(np.floor(z / cell))))
        hits = []
        if ids is not None and len(ids):
            A, B, C = a[ids], b[ids], c[ids]
            # 2D barycentric in XZ, then interpolate y
            v0 = B - A; v1 = C - A
            d = v0[:, 0] * v1[:, 2] - v0[:, 2] * v1[:, 0]
            ok = np.abs(d) > 1e-12
            px, pz = x - A[:, 0], z - A[:, 2]
            u = np.where(ok, (px * v1[:, 2] - pz * v1[:, 0]) / np.where(ok, d, 1), -1)
            v = np.where(ok, (v0[:, 0] * pz - v0[:, 2] * px) / np.where(ok, d, 1), -1)
            inside = ok & (u >= -1e-6) & (v >= -1e-6) & (u + v <= 1 + 1e-6)
            hy = A[:, 1] + u * v0[:, 1] + v * v1[:, 1]
            for k in np.nonzero(inside)[0]: hits.append((float(hy[k]), float(ny[ids[k]])))
        hits.sort()
        below = [h for h in hits if h[0] <= y + 0.5]
        above = [h for h in hits if h[0] > y + 0.1 and h[0] <= y + 6.0]
        fb = below[-1][0] if below else None
        r = {'id': q.get('id'), 'x': x, 'y': y, 'z': z, 'floor_below': fb,
             'above_up': next((h[0] for h in above if h[1] > 0.3), None),
             'above_any': above[0][0] if above else None,
             'thickness': (fb - below[-2][0]) if len(below) >= 2 else None}
        out.append(r)
    json.dump(out, open(sys.argv[3], 'w'), indent=0)
    print('collision_query: %d triangles, %d queries' % (len(a), len(Q)))


if __name__ == '__main__':
    main()
