"""Which material covers a pixel? Bisects WFC_SKIPMAT over the active materials (deterministic lockstep).

Usage: python pick_material.py <exe> <render dir> <x> <y> [ENV=VAL ...]
Needs <render dir>/active_dump.jsonl (WFC_AUDIT_DUMP). Each probe renders with WFC_LOCKSTEP so frames are
bit-identical; the material whose removal changes the pixel is reported (with its info + permutation).
"""
import json
import os
import subprocess
import sys
import tempfile

from PIL import Image


def render(exe, env, skip, out):
    e = dict(os.environ)
    e.update(env)
    e.update({'WFC_LOCKSTEP': '1', 'WFC_SHOT': out})
    e.setdefault('WFC_SMOKE_FRAMES', '60')
    if skip:
        e['WFC_SKIPMAT'] = ';'.join(skip)
    else:
        e.pop('WFC_SKIPMAT', None)
    subprocess.run([exe], env=e, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=600)
    return Image.open(out).convert('RGB')


def main():
    exe, rd, x, y = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    env = dict(a.split('=', 1) for a in sys.argv[5:])
    mats = sorted({json.loads(l)['material'] for l in open(os.path.join(rd, 'active_dump.jsonl')) if l.strip()} - {''})
    tmp = tempfile.mkdtemp()
    base = render(exe, env, [], os.path.join(tmp, 'base.bmp')).getpixel((x, y))
    print('base pixel', base, 'candidates', len(mats))

    def changed(skip):
        p = render(exe, env, skip, os.path.join(tmp, 'p.bmp')).getpixel((x, y))
        return sum(abs(a - b) for a, b in zip(p, base)) > 3

    cand = mats
    while len(cand) > 1:
        half = cand[:len(cand) // 2]
        cand = half if changed(half) else cand[len(cand) // 2:]
    if not cand or not changed(cand):
        print('no single material controls this pixel (background, fog/sky, GL1 overlay, or several layers)')
        return
    m = cand[0]
    M = json.load(open(os.path.join(rd, 'materials_glsl.json')))
    M = M.get('materials', M)
    P = json.load(open(os.path.join(rd, 'permutation_check.json')))
    P = P.get('materials', P)
    info = (M.get(m) or {}).get('info') or {}
    subs = [json.loads(l) for l in open(os.path.join(rd, 'active_dump.jsonl')) if l.strip() and json.loads(l)['material'] == m]
    print('material', m)
    print('  blend', info.get('blend_mode'), 'lighting', info.get('lighting_model'), 'connected', info.get('connected'))
    print('  permutation match', (P.get(m) or {}).get('match'), (P.get(m) or {}).get('diff'))
    print('  submeshes', len(subs), 'lightmapped', sum(s['lightmapped'] for s in subs))
    comps = sorted({s['component'] for s in subs if s['component']})
    if len(comps) > 1:                       # second level: which component of that material
        cand = comps
        while len(cand) > 1:
            half = cand[:len(cand) // 2]
            cand = half if changed(['comp:' + c for c in half]) else cand[len(cand) // 2:]
        comps = cand
    print('  component', comps[0] if comps else None)


if __name__ == '__main__':
    main()
