"""Per ENV TEXSET Masks texture: the CustA / CustB colours of every tint-enabled ENV_PhongMain_NS material using it, parsed
from the translated GLSL: diffuse = mix(base * A, base * B, mask.b). Read-only; writes work/tintab.json."""
import json, os, re, collections
R = r'F:/Transformers Rebuild/Rebuild/work/render'
mats = {}
for mp in sorted(os.listdir(R)):
    p = os.path.join(R, mp, 'materials_glsl.json')
    if not (mp.startswith('MP_') or mp.startswith('UI_')) or not os.path.exists(p): continue
    J = json.load(open(p, encoding='utf-8')); M = J['materials'] if 'materials' in J else J
    for k, m in M.items():
        if k in mats or not isinstance(m, dict) or 'info' not in m: continue
        i = m['info']
        if i.get('master') != 'ENV_AllShader_p.Release.ENV_PhongMain_NS_Mat' or not i.get('switches', {}).get('Enable_CustomColorTint'): continue
        mats[k] = (i, m.get('glsl', ''))
res = {}
unparsed = []
for k, (i, g) in mats.items():
    slots = {t['slot']: t['object'].split('.')[-1] for t in i.get('textures', [])}
    mslot = next((s for s, n in slots.items() if 'Mask' in n), None)
    defs = {}
    for line in g.splitlines():
        mm = re.match(r'\s*\w+ (t\d+) = (.*);\s*$', line)
        if mm: defs[mm.group(1)] = mm.group(2)
    mask_var = next((v for v, e in defs.items() if e == 'wfcSample2D(%d, t2)' % mslot or re.fullmatch(r'wfcSample2D\(%d, t\d+\)' % mslot, e)), None)
    rgb = next((v for v, e in defs.items() if mask_var and e == '%s.rgb' % mask_var), None)
    bvar = next((v for v, e in defs.items() if rgb and e == '(vec4(%s, 1.0)).z' % rgb), None)
    mixv = next(((v, e) for v, e in defs.items() if bvar and re.fullmatch(r'mix\((t\d+), (t\d+), %s\)' % bvar, e)), None)
    if not mixv:
        unparsed.append(k); continue
    a, b = re.fullmatch(r'mix\((t\d+), (t\d+), \w+\)', mixv[1]).groups()

    def colour(v):
        e = defs.get(v, '')
        mm = re.fullmatch(r'\((t\d+) \* \((t\d+|uRT_\w+[^)]*)\)\.xyz\)', e)
        if not mm: return e
        c = defs.get(mm.group(2), mm.group(2))
        cm = re.fullmatch(r'vec4\(([^)]*)\)', c)
        return [round(float(x), 4) for x in cm.group(1).split(',')[:3]] if cm else c
    res[k] = dict(mask=slots[mslot], A=colour(a), B=colour(b))
by_mask = collections.defaultdict(list)
for k, v in res.items(): by_mask[v['mask']].append(dict(material=k, A=v['A'], B=v['B']))
white = lambda c: isinstance(c, list) and all(abs(x - 1.0) < 1e-3 for x in c)
nA = sum(1 for v in res.values() if white(v['A'])); nB = sum(1 for v in res.values() if white(v['B']))
print('tint-enabled ENV materials %d, parsed %d (unparsed %d); A white %d, B white %d, both white %d; masks %d' %
      (len(mats), len(res), len(unparsed), nA, nB, sum(1 for v in res.values() if white(v['A']) and white(v['B'])), len(by_mask)))
nonwhiteA = [(k, v['A']) for k, v in res.items() if not white(v['A'])]
print('A not white:', len(nonwhiteA), nonwhiteA[:8])
print('runtime (uRT) colours:', [k for k, v in res.items() if not isinstance(v['A'], list) or not isinstance(v['B'], list)][:10])
json.dump(dict(by_mask=by_mask, unparsed=unparsed), open(os.environ.get('TINT_AB_OUT', r'F:/Transformers Rebuild/Rebuild-Systems/work/tintab.json'), 'w'), indent=1)
