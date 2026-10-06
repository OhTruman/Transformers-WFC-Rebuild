"""Swept pass-through test against the AUTHORED pawn collision world (ExtractedAssets collision_pawn.glb).

Independent of the product: used by vehicle-collision.ps1 (and transform-stress.ps1) to find frames where a pawn's
logged path passes through a wall-like collision face, i.e. where the product let it through authored collision.

    python collision_sweep.py <collision_pawn.glb> <paths.json> <out.json>

paths.json: [{"id": "...", "heights": [0.5, 1.0, 1.6], "pts": [[frame, x, y, z], ...]}, ...]
  pts are glTF metres (y up) at the actor's ground level (the product's frame log 'pos'); every consecutive pair is
  swept at each height above that level.
out.json: {"crossings": [{"id", "frame", "h", "node", "ny", "x", "y", "z"}], "paths": n, "segments": n}
  Only faces with |normal.y| < 0.7 (walls / sides of blocks; floors and ramps are ignored) count.
"""
import json, sys
import numpy as np
from collision_query import load_glb, accessor, node_matrix


def triangles_named(path):
    js, bin_ = load_glb(path)
    tris, names = [], []
    def walk(ni, parent, label):
        n = js['nodes'][ni]; m = parent @ node_matrix(n)
        nm = n.get('name') or label
        if 'mesh' in n:
            for pr in js['meshes'][n['mesh']]['primitives']:
                p = accessor(js, bin_, pr['attributes']['POSITION']).astype(np.float64)
                p = (np.c_[p, np.ones(len(p))] @ m.T)[:, :3]
                idx = accessor(js, bin_, pr['indices']).astype(np.int64) if 'indices' in pr else np.arange(len(p))
                t = p[idx.reshape(-1, 3)]
                tris.append(t); names.extend([nm] * len(t))
        for c in n.get('children', []): walk(c, m, nm)
    for s in js.get('scenes', [{'nodes': list(range(len(js['nodes'])))}]):
        for ni in s['nodes']: walk(ni, np.eye(4), '')
    return np.concatenate(tris, axis=0), names


def main():
    T, names = triangles_named(sys.argv[1])
    P = json.load(open(sys.argv[2]))
    a, b, c = T[:, 0], T[:, 1], T[:, 2]
    nrm = np.cross(b - a, c - a); nl = np.linalg.norm(nrm, axis=1)
    keep = (nl > 1e-9)
    ny = np.zeros(len(a)); ny[keep] = nrm[keep, 1] / nl[keep]
    wall = keep & (np.abs(ny) < 0.7)
    wi = np.nonzero(wall)[0]
    a, b, c, ny = a[wi], b[wi], c[wi], ny[wi]
    nm = [names[i] for i in wi]
    cell = 2.0
    mn = np.minimum(np.minimum(a, b), c); mx = np.maximum(np.maximum(a, b), c)
    grid = {}
    for i in range(len(a)):
        for gx in range(int(np.floor(mn[i, 0] / cell)), int(np.floor(mx[i, 0] / cell)) + 1):
            for gz in range(int(np.floor(mn[i, 2] / cell)), int(np.floor(mx[i, 2] / cell)) + 1):
                grid.setdefault((gx, gz), []).append(i)
    grid = {k: np.array(v) for k, v in grid.items()}
    e1 = b - a; e2 = c - a
    out = []; nseg = 0
    for path in P:
        pts = path['pts']; hs = path.get('heights', [0.5, 1.0, 1.6])
        for k in range(1, len(pts)):
            f0, x0, y0, z0 = pts[k - 1]; f1, x1, y1, z1 = pts[k]
            d = np.array([x1 - x0, y1 - y0, z1 - z0])
            if np.hypot(d[0], d[2]) < 1e-4 or np.linalg.norm(d) > 8.0:   # standing still / teleport (respawn): skip
                continue
            nseg += 1
            ids = set()
            for gx in range(int(np.floor(min(x0, x1) / cell)), int(np.floor(max(x0, x1) / cell)) + 1):
                for gz in range(int(np.floor(min(z0, z1) / cell)), int(np.floor(max(z0, z1) / cell)) + 1):
                    g = grid.get((gx, gz))
                    if g is not None: ids.update(g.tolist())
            if not ids: continue
            ids = np.fromiter(ids, dtype=np.int64)
            A, E1, E2 = a[ids], e1[ids], e2[ids]
            pv = np.cross(d, E2); det = np.einsum('ij,ij->i', E1, pv)
            ok = np.abs(det) > 1e-12
            inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
            for h in hs:
                o = np.array([x0, y0 + h, z0])
                tv = o - A
                u = np.einsum('ij,ij->i', tv, pv) * inv
                qv = np.cross(tv, E1)
                v = (qv @ d) * inv
                t = np.einsum('ij,ij->i', E2, qv) * inv
                hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 0) & (t <= 1)
                for j in np.nonzero(hit)[0]:
                    ti = ids[j]; tt = float(t[j])
                    out.append({'id': path['id'], 'frame': int(f1), 'h': h, 'node': nm[ti], 'ny': round(float(ny[ti]), 3),
                                'x': round(x0 + d[0] * tt, 2), 'y': round(y0 + h + d[1] * tt, 2), 'z': round(z0 + d[2] * tt, 2)})
                    break   # one crossing per segment and height is enough
    json.dump({'crossings': out, 'paths': len(P), 'segments': nseg}, open(sys.argv[3], 'w'), indent=0)
    print('collision_sweep: %d wall triangles, %d paths, %d segments, %d crossings' % (len(a), len(P), nseg, len(out)))


if __name__ == '__main__':
    main()
