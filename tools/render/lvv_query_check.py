"""Independent Python port of the WFC LightsVisibilitiesVolume query (native pseudocode, b52dca9),
cross-checked against the renderer's C++ query dump (WFC_LVVDUMP).

Usage: python lvv_query_check.py <work/render/<Map>> <cpp dump>
float32 arithmetic throughout so the 16-bit fixed-point truncation matches the C++ implementation.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lvv_decode  # noqa: E402

F = np.float32


def build(rd):
    L = json.load(open(os.path.join(rd, 'lighting.json')))
    d = lvv_decode.decode(open(os.path.join(rd, L['light_visibility_volumes'][0]['file']), 'rb').read())
    names = {tuple(l['light_guid']): l['name'] for l in L['lights']}
    table = [names.get(tuple(g)) for g in d['guids']]
    return d, table


def contains(d, p):
    c, h = d['center'], F(d['half'])
    return all(abs(F(p[i]) - F(c[i])) < h for i in range(3))


def child(B, i):
    (cx, cy, cz), h = B
    hh = F(h) * F(0.5)
    return ((F(cx) + (hh if i & 4 else -hh), F(cy) + (hh if i & 2 else -hh), F(cz) + (hh if i & 1 else -hh)), hh)


def find_leaf(d, q):
    if not contains(d, q):
        return None, None
    n, B = d['root'], (tuple(F(x) for x in d['center']), F(d['half']))
    while n.children:
        i = (4 if q[0] > B[0][0] else 0) | (2 if q[1] > B[0][1] else 0) | (1 if q[2] > B[0][2] else 0)
        B = child(B, i)
        n = n.children[i]
    return n, B


def corner(B, k):
    (cx, cy, cz), h = B
    return (cx + (h if k & 1 else -h), cy + (h if k & 2 else -h), cz + (h if k & 4 else -h))


def accumulate(d, acc, s, w):
    cnt = d['counts'][s] & 0x7F
    for k in range(cnt):
        rec = d['pool'][d['starts'][s] + k]
        li = np.int16(np.uint16(rec >> 16))
        v = F(rec & 0xFFFF) / F(65535.0)
        if li in acc:
            acc[li] = int(np.int32((v * F(w) + F(acc[li]) / F(65535.0)) * F(65535.0))) & 0xFFFF
        else:
            acc[li] = int(np.int32(v * F(w) * F(65535.0))) & 0xFFFF


def query(d, p):
    p = tuple(F(x) for x in p)
    if not contains(d, p):
        return None
    n, B = d['root'], (tuple(F(x) for x in d['center']), F(d['half']))
    while n.children:
        i = (4 if p[0] > B[0][0] else 0) | (2 if p[1] > B[0][1] else 0) | (1 if p[2] > B[0][2] else 0)
        B = child(B, i)
        n = n.children[i]
    if not n.has_data:
        return None
    h = B[1]
    eps = F(d['finest']) * F(0.5)
    acc = {}
    nb = [None] * 6
    tj = False
    if F(d['finest']) < h:
        for f in range(6):
            ax = f >> 1
            q = list(p)
            q[ax] = B[0][ax] + (h + eps) if f & 1 else B[0][ax] - (h + eps)
            nn, nB = find_leaf(d, tuple(F(x) for x in q))
            nb[f] = (nn, nB) if nn is not None else None
            if nn is not None and nB[1] <= h - eps:
                tj = True
    empty = F(0.0)
    remap, counts = d['remap'], d['counts']
    if not tj:
        inv = F(1.0) / (F(2.0) * h)
        for k in range(8):
            ck = corner(B, k)
            w = (F(1) - abs(p[0] - ck[0]) * inv) * (F(1) - abs(p[1] - ck[1]) * inv) * (F(1) - abs(p[2] - ck[2]) * inv)
            cid = n.corners[k]
            s = remap[cid] if cid != 0xFFFF and cid < len(remap) else -1
            cnt = 0 if s < 0 else counts[s] & 0x7F
            if cnt == 0:
                empty += w
            else:
                accumulate(d, acc, s, w)
    else:
        faces = [(0, 2, 4, 6), (1, 3, 5, 7), (0, 1, 4, 5), (2, 3, 6, 7), (0, 1, 2, 3), (4, 5, 6, 7)]
        t = [(p[a] - (B[0][a] - h)) / (F(2) * h) for a in range(3)]
        for f in range(6):
            ax = f >> 1
            fw = (t[ax] if f & 1 else F(1) - t[ax]) * F(1.0 / 3.0)
            use_self = nb[f] is None or nb[f][1][1] >= h - eps
            src, sB = (n, B) if use_self else nb[f]
            invS = F(1.0) / (F(2.0) * sB[1])
            for k in faces[f]:
                ck = corner(sB, k)
                ws = [F(1) if a == ax else F(1) - abs(p[a] - ck[a]) * invS for a in range(3)]
                w3 = ws[0] * ws[1] * ws[2]
                cid = src.corners[k]
                invalid = cid == 0xFFFF or cid >= len(remap)
                s = -1 if invalid else remap[cid]
                flag = (not invalid) and (counts[s] >> 7) != 0
                if invalid or flag:
                    empty += w3 * fw
                else:
                    accumulate(d, acc, s, w3 * fw)
    if empty > 0:
        sc = F(1.0) / (F(1.0) - empty)
        acc = {k: int(np.int32(F(v) * sc)) & 0xFFFF for k, v in acc.items()}
    return acc


def main():
    rd, dump = sys.argv[1], sys.argv[2]
    d, table = build(rd)
    total = mism = 0
    for line in open(dump):
        f = line.split()
        p = (float(f[0]), float(f[1]), float(f[2]))
        cpp_hit = f[3] == '1'
        cpp = dict((x.rsplit(':', 1)[0], int(x.rsplit(':', 1)[1])) for x in f[4:])
        acc = query(d, p)
        py_hit = acc is not None
        py = {}
        if acc:
            for li, v in acc.items():
                if 0 <= li < len(table) and table[li]:
                    py[table[li]] = v
        total += 1
        if py_hit != cpp_hit or py != cpp:
            mism += 1
            if mism <= 5:
                print('MISMATCH at', p, 'cpp', cpp_hit, cpp, 'py', py_hit, py)
    print('points %d, mismatches %d' % (total, mism))


if __name__ == '__main__':
    main()
