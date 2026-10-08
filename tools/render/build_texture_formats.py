"""Texture upload formats for the renderer (<render data root>/texture_formats.json).

Joins AssetTools' original compressed-texture export (manifests/extracted/textures_dds.jsonl: the cooked DXT /
A8R8G8B8 / G8 blocks with their original mip chains, as DDS under AssetTools/out/dds) with an alpha-read analysis of
every compiled material (all maps' and UI scenes' materials_glsl.json: the MP chassis roster, weapons and FX are in
every map's set). Output, keyed by the PNG path relative to ExtractedAssets (lower case, '/'):
    {"dds": <path relative to the DDS root>, "fmt": "DXT1" | "DXT5" | "A8R8G8B8" | "G8", "alpha": <bool>}
alpha = some material samples the texture's alpha (.a / .w / a swizzle with it / the whole vec4); textures no material
references are alpha = true (the safe choice: the renderer keeps their alpha). Lightmap pages (sampled .rgb only) are
alpha = false, keyed "verticalslice/maps/<map>/lightmaps/<name>.png" as in the export. A PNG that several exported
objects map onto is left out (the renderer then loads the PNG as before), and so is every texture whose DDS top level
does not decode to exactly its PNG (verified here for all of them: the renderer's top level must stay identical).
AssetTools row flags: png_is_other_object (the PNG path belongs to another object) -> left out; png null (lightmap
pages with no PNG anywhere) -> keyed by the DDS path, unverifiable, included; alpha_all_zero (the cooked data has
alpha 0 everywhere, the PNG was saved opaque) -> verified on RGB, "opaque": true (the renderer keeps today's opaque
alpha; whether the original sampled them opaque is an open RE question).

usage: build_texture_formats.py <render data root> <AssetTools root> [<ExtractedAssets root>]
"""
import collections
import glob
import json
import os
import re
import struct
import sys

import numpy as np
from PIL import Image

render_root, at_root = sys.argv[1], sys.argv[2]
ea_root = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(at_root)), 'ExtractedAssets')
dds_root = os.path.join(at_root, 'out', 'dds')


def _c565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.int32)


def decode_top(fmt, data, w, h):
    """The renderer's CPU decode of the top level (WfcPipeline.cpp decodeLevel), vectorised; RGBA uint8 (h, w, 4)."""
    if fmt in ('DXT1', 'DXT5'):
        bs = 8 if fmt == 'DXT1' else 16
        bw, bh = (w + 3) // 4, (h + 3) // 4
        blk = np.frombuffer(data, np.uint8, bw * bh * bs).reshape(bh * bw, bs).astype(np.int64)
        cb = blk if fmt == 'DXT1' else blk[:, 8:]
        c0 = cb[:, 0] | cb[:, 1] << 8
        c1 = cb[:, 2] | cb[:, 3] << 8
        p0, p1 = _c565(c0), _c565(c1)
        four = (c0 > c1) if fmt == 'DXT1' else np.ones(len(c0), bool)
        p2 = np.where(four[:, None], (2 * p0 + p1) // 3, (p0 + p1) // 2)
        p3 = np.where(four[:, None], (p0 + 2 * p1) // 3, 0)
        pal = np.stack([np.concatenate([p, np.full((len(p), 1), 255)], 1) for p in (p0, p1, p2)] +
                       [np.concatenate([p3, np.where(four, 255, 0)[:, None]], 1)], 1)       # (n, 4, 4)
        bits = cb[:, 4] | cb[:, 5] << 8 | cb[:, 6] << 16 | cb[:, 7] << 24
        idx = (bits[:, None] >> (2 * np.arange(16))) & 3                                   # (n, 16)
        px = np.take_along_axis(pal, idx[:, :, None].repeat(4, 2), 1)                       # (n, 16, 4)
        if fmt == 'DXT5':
            a0, a1 = blk[:, 0], blk[:, 1]
            i = np.arange(1, 7)
            a6 = ((7 - i) * a0[:, None] + i * a1[:, None]) // 7
            j = np.arange(1, 5)
            a4 = np.concatenate([((5 - j) * a0[:, None] + j * a1[:, None]) // 5, np.zeros((len(a0), 1), np.int64),
                                 np.full((len(a0), 1), 255)], 1)
            apal = np.concatenate([a0[:, None], a1[:, None], np.where((a0 > a1)[:, None], a6, a4)], 1)
            ab = np.zeros(len(a0), np.int64)
            for k in range(6):
                ab |= blk[:, 2 + k] << (8 * k)
            aidx = (ab[:, None] >> (3 * np.arange(16))) & 7
            px[:, :, 3] = np.take_along_axis(apal, aidx, 1)
        img = px.reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)
        return img[:h, :w].astype(np.uint8)
    if fmt == 'G8':
        g = np.frombuffer(data, np.uint8, w * h).reshape(h, w)
        return np.stack([g, g, g, np.full_like(g, 255)], -1)
    a = np.frombuffer(data, np.uint8, w * h * 4).reshape(h, w, 4)
    return a[:, :, [2, 1, 0, 3]]


def verify(png_rel, dds_rel, fmt, rgb_only=False):
    try:
        f = open(os.path.join(dds_root, dds_rel), 'rb').read()
        h, w = struct.unpack('<II', f[12:20])
        mine = decode_top(fmt, f[128:], w, h)
        png = np.asarray(Image.open(os.path.join(ea_root, png_rel)).convert('RGBA'))
        if rgb_only:
            return png.shape == mine.shape and np.array_equal(png[..., :3], mine[..., :3])
        return png.shape == mine.shape and np.array_equal(png, mine)
    except Exception:
        return False


idx = os.path.join(at_root, 'manifests', 'extracted', 'textures_dds.jsonl')


def rel_png(f):
    f = f.replace('\\', '/')
    i = f.lower().find('extractedassets/')
    return (f[i + len('extractedassets/'):] if i >= 0 else f).lower()


alpha = collections.defaultdict(bool)
seen = set()
for mf in glob.glob(os.path.join(render_root, '*', 'materials_glsl.json')):
    d = json.load(open(mf, encoding='utf-8'))
    for v in d.values():
        g = v.get('glsl') or ''
        texs = (v.get('info') or {}).get('textures') or []
        samp = collections.defaultdict(list)
        for m in re.finditer(r'vec4 (t\d+) = wfcSample\w*\((\d+),', g):
            samp[int(m.group(2))].append(m.group(1))
        for i, t in enumerate(texs):
            f = t.get('file')
            if not f:
                continue
            key = rel_png(f)
            seen.add(key)
            used = False
            vars_ = samp.get(t.get('slot', i), [])
            if not vars_:
                used = True                              # sampled some other way: keep alpha
            for var in vars_:
                rest = g.split('vec4 %s =' % var, 1)[1].split('\n', 1)[1]
                if re.search(r'\b%s\.(?:[rgbxyz]*[aw][rgbaxyzw]*)\b' % var, rest) or \
                   re.search(r'(?<![.\w])%s\b(?!\.)' % var, rest):
                    used = True
            alpha[key] = alpha[key] or used

rows = [json.loads(l) for l in open(idx, encoding='utf-8') if l.strip()]
by_png = collections.defaultdict(list)
for r in rows:
    if r.get('status') != 'ok' or not r.get('dds') or r.get('png_is_other_object'):
        continue
    if not r.get('png'):                                   # no PNG anywhere (some lightmap pages): key by the DDS path
        r = dict(r, png=None, key=r['dds'][:-4] + '.png')
        by_png[r['key'].lower()].append(r)
    else:
        by_png[r['png'].lower()].append(r)
out, shared, mismatch = {}, 0, []
for png, rs in by_png.items():
    if len(rs) != 1:
        shared += 1
        continue
    r = rs[0]
    opaque = bool(r.get('alpha_all_zero'))
    if r['png'] is not None and not verify(r['png'], r['dds'], r['format'].replace('PF_', ''), rgb_only=opaque):
        mismatch.append(r['png'])
        continue
    lm = '/lightmaps/' in png
    out[png] = {'dds': r['dds'], 'fmt': r['format'].replace('PF_', ''),
                'alpha': False if lm else (alpha[png] if png in seen else True)}
    if opaque:
        out[png]['opaque'] = True
    if r['png'] is None:
        out[png]['no_png'] = True
dst = os.path.join(render_root, 'texture_formats.json')
json.dump({'generated_by': 'tools/render/build_texture_formats.py', 'source_index': 'AssetTools/manifests/extracted/textures_dds.jsonl',
           'textures': out}, open(dst, 'w', encoding='utf-8'), indent=0, sort_keys=True)
c = collections.Counter((v['fmt'], v['alpha']) for v in out.values())
print('%d textures (%d PNGs shared by several objects left out; %d left out: DDS top level != PNG); referenced by materials %d'
      % (len(out), shared, len(mismatch), len(seen)))
for m in sorted(mismatch)[:40]:
    print('  mismatch:', m)
for k, n in sorted(c.items()):
    print('  %-9s alpha %-5s %5d' % (k[0], k[1], n))
print('wrote', dst)
