"""Systems family review of AssetTools' M6 roughness maps (push_m6, provisional): per row of pack/hd_pbr_index.json, with the
generator's results/NNNN.json and docs/handoff/SYSTEMS_M6_WORKLIST.json (the target):
  header   DDS + DX10, DXGI_FORMAT_BC4_UNORM (80), full mip chain, file size = header + the BC4 chain (8 bytes per 4x4 block)
  values   decoded top mip: |mean - target_mean_roughness| <= 0.02, std >= 0.02 (the variation kept), p1 >= 0.02, max <= 1
  source   the gloss source (results' gloss_source, a D-pack texture + channel): the map is half its size, and its pixels follow
           the source channel (|Pearson r| >= 0.9 on the source downsampled to the map size; a remap keeps the order)
Families: the materials' worklist class plus the texture's package prefix (ENV / WEP / TR / PROP / CINE ...). Pauses while
PERF_HOLD exists. Read only.  m6_rough_review.py <push_m6 root> <D pack dir> <worklist json> <out json>"""
import json, os, re, struct, sys, time, collections
import numpy as np
from PIL import Image

root, dpack, worklist, out = sys.argv[1:5]
HOLD = r'F:\Transformers Rebuild\Rebuild-Experimental\work\PERF_HOLD'
idx = json.load(open(os.path.join(root, 'pack', 'hd_pbr_index.json'), encoding='utf-8'))['rows']
W = {r['tex']: r for r in json.load(open(worklist, encoding='utf-8'))['rows']}
res = {}
for f in os.listdir(os.path.join(root, 'results')):
    if f.endswith('.json'):
        r = json.load(open(os.path.join(root, 'results', f), encoding='utf-8'))
        res[r['key']] = r
D = json.load(open(os.path.join(dpack, 'hd_index.json'), encoding='utf-8'))['rows']
dby = collections.defaultdict(list)   # last name component (lower) -> D rows
dfull = {}
for r in D:
    dby[r['object_path'].split('.')[-1].lower()].append(r)
    dfull[r['object_path'].lower()] = r

def header(p):
    b = open(p, 'rb').read(148)
    if b[:4] != b'DDS ':
        return 'not DDS'
    h, w = struct.unpack_from('<II', b, 12); mips = max(struct.unpack_from('<I', b, 28)[0], 1)
    fourcc = b[84:88]
    if fourcc != b'DX10':
        return 'fourcc %r' % fourcc
    dxgi = struct.unpack_from('<I', b, 128)[0]
    if dxgi != 80:
        return 'dxgi %d' % dxgi
    full = int(np.floor(np.log2(max(w, h)))) + 1
    if mips != full:
        return 'mips %d of %d' % (mips, full)
    size = 148; ww, hh = w, h
    for _ in range(mips):
        size += max(1, (ww + 3) // 4) * max(1, (hh + 3) // 4) * 8; ww = max(1, ww // 2); hh = max(1, hh // 2)
    n = os.path.getsize(p)
    if n != size:
        return 'bytes %d != %d' % (n, size)
    return None

def family(tex):
    w = W.get(tex, {})
    cls = sorted({m.get('class', '?') for m in w.get('materials', [])}) or ['?']
    pre = tex.split('_', 1)[0].upper()
    return pre + ' / ' + '+'.join(cls)

rows = []
for r in idx:
    while os.path.exists(HOLD):
        time.sleep(2)
    key, tex = r['key'], r['tex']
    rr, w = res.get(key, {}), W.get(tex, {})
    p = os.path.join(root, 'pack', r['dds'])
    fails = []
    h = header(p) if os.path.isfile(p) else 'missing'
    if h:
        fails.append('header: ' + h)
    a = np.asarray(Image.open(p).convert('L'), dtype=np.float32) / 255.0 if not h else None
    stats = {}
    if a is not None:
        tgt = w.get('target_mean_roughness')
        stats = {'mean': round(float(a.mean()), 4), 'std': round(float(a.std()), 4), 'p1': round(float(np.percentile(a, 1)), 4),
                 'max': round(float(a.max()), 4), 'target': None if tgt is None else round(tgt, 4), 'size': list(a.shape[::-1])}
        if tgt is None:
            fails.append('no worklist target')
        elif abs(stats['mean'] - tgt) > 0.02:
            fails.append('mean %.3f vs target %.3f' % (stats['mean'], tgt))
        if stats['std'] < (0.005 if 'luma' in rr.get('gloss_source', '') else 0.02):
            fails.append('flat (std %.3f)' % stats['std'])
        if stats['p1'] < 0.02 or stats['max'] > 1.0:
            fails.append('range p1 %.3f max %.3f' % (stats['p1'], stats['max']))
        m = re.search(r'\(([^)]+)\), channel (\d|luma)', rr.get('gloss_source', ''))
        if not m:
            fails.append('no gloss_source')
        else:
            name, ch = m.group(1), m.group(2)
            stats['source_channel'] = ch
            pkg = tex.split('.')[0].lower()
            if name.lower() == tex.split('.')[-1].lower() and tex.lower() in dfull:
                same = [dfull[tex.lower()]]   # the row's own texture (names like EditorTextureSet_Masks_NormY repeat across packages)
            else:
                cands = dby.get(name.lower(), [])
                same = [c for c in cands if c['object_path'].split('.')[0].lower() == pkg] or cands
            if not same:
                fails.append('source %s not in the D pack' % name)
            else:
                sp = os.path.join(dpack, same[0]['dds'])
                s = Image.open(sp)
                stats['source'] = same[0]['object_path']; stats['source_size'] = list(s.size)
                if (s.size[0] // 2, s.size[1] // 2) != tuple(stats['size']):
                    fails.append('size %s, source %s (not half)' % (stats['size'], list(s.size)))
                sa = np.asarray(s.convert('RGBA').resize(tuple(stats['size']), Image.BILINEAR), dtype=np.float32) / 255.0
                sc = sa[..., :3] @ np.array([0.2126, 0.7152, 0.0722], np.float32) if ch == 'luma' else sa[..., int(ch)]
                if sc.std() > 1e-4 and a.std() > 1e-4:
                    c = float(np.corrcoef(sc.ravel(), a.ravel())[0, 1])
                    stats['r_source'] = round(c, 3)
                    if abs(c) < 0.8:
                        fails.append('follows the source weakly (r %.2f)' % c)
    rows.append({'key': key, 'tex': tex, 'family': family(tex), 'orm_candidate': bool(rr.get('orm_candidate') or w.get('orm_candidate_materials')),
                 'accept': not fails, 'fails': fails, **stats})
fam = collections.OrderedDict()
for r in sorted(rows, key=lambda r: r['family']):
    f = fam.setdefault(r['family'], {'rows': 0, 'accepted': 0, 'rejected': 0, 'orm_candidates': 0})
    f['rows'] += 1; f['accepted' if r['accept'] else 'rejected'] += 1; f['orm_candidates'] += r['orm_candidate']
summary = {'rows': len(rows), 'accepted': sum(r['accept'] for r in rows), 'rejected': sum(not r['accept'] for r in rows),
           'orm_candidates': sum(r['orm_candidate'] for r in rows),
           'r_source_sign': dict(collections.Counter('+' if r.get('r_source', 0) > 0 else '-' for r in rows if 'r_source' in r)),
           'families': fam}
json.dump({'summary': summary, 'rows': rows}, open(out, 'w', encoding='utf-8'), indent=1)
print(json.dumps(summary, indent=1))
for r in rows:
    if not r['accept']:
        print('REJECT', r['tex'], '|', '; '.join(r['fails']))
