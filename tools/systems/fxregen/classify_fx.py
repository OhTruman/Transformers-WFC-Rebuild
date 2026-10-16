"""Per-LOD classification of regenerated map_fx_runtime.json vs current render data (AssetTools 7b1908a categories).
argv[1] = handoff dir, argv[2] = current render dir, argv[3] = out json."""
import json, os, sys, math, collections

def same(a, b):
    if isinstance(a, float) and isinstance(b, float) and math.isnan(a) and math.isnan(b): return True
    if type(a) != type(b): return False
    if isinstance(a, dict): return a.keys() == b.keys() and all(same(a[k], b[k]) for k in a)
    if isinstance(a, list): return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
    return a == b

def strip_absolute(L):
    L = json.loads(json.dumps(L))
    for m in L.get('modules') or []:
        p = m.get('props')
        if isinstance(p, dict) and p.get('Absolute') is False:
            p.pop('Absolute')
            if not p: m.pop('props')
    return L

res, tot = {}, collections.Counter()
for m in sorted(os.listdir(sys.argv[1])):
    nf = os.path.join(sys.argv[1], m, 'map_fx_runtime.json')
    if not os.path.isfile(nf): continue
    old = json.load(open(os.path.join(sys.argv[2], m, 'map_fx_runtime.json'), encoding='utf-8'))
    new = json.load(open(nf, encoding='utf-8'))
    c = collections.Counter(); other = []; vol = []; orbit = []
    top = [k for k in set(old) | set(new) if k not in ('systems', 'source') and not same(old.get(k), new.get(k))]
    if set(old['systems']) != set(new['systems']): top.append('system set')
    for sn in old['systems']:
        so, sw = old['systems'][sn], new['systems'].get(sn)
        if sw is None: continue
        if not same({k: v for k, v in so.items() if k != 'emitters'}, {k: v for k, v in sw.items() if k != 'emitters'}):
            other.append(sn + ' (system fields)')
        if len(so['emitters']) != len(sw['emitters']): other.append(sn + ' (emitter count)'); continue
        for ei, (eo, ew) in enumerate(zip(so['emitters'], sw['emitters'])):
            if not same({k: v for k, v in eo.items() if k != 'lods'}, {k: v for k, v in ew.items() if k != 'lods'}):
                other.append('%s e%d (emitter fields)' % (sn, ei))
            for li, (lo, lw) in enumerate(zip(eo['lods'], ew['lods'])):
                c['lods'] += 1
                if same(lo, lw): c['identical'] += 1; continue
                absn = sum(1 for md in lw.get('modules') or [] if isinstance(md.get('props'), dict) and md['props'].get('Absolute') is False)
                lw2 = strip_absolute(lw)
                if same(lo, lw2): c['Absolute=false only'] += 1; continue
                names = [md.get('module') for md in lw.get('modules') or []]
                vol_gain = any('VelOverLife' in (md.get('dists') or {}) and not any('VelOverLife' in (mo.get('dists') or {}) for mo in lo.get('modules') or [] if mo.get('module') == md.get('module'))
                               for md in lw.get('modules') or [])
                if absn: c['(also Absolute=false)'] += 1
                if vol_gain: c['VelOverLife gained'] += 1; vol.append('%s/%s LOD%d' % (sn.split('.')[-1], ew.get('name'), li))
                elif 'PMI_Orbit' in names and lo.get('assignment_complete') is False and lw.get('assignment_complete') is True:
                    c['Orbit completed'] += 1; orbit.append('%s/%s LOD%d' % (sn.split('.')[-1], ew.get('name'), li))
                else:
                    c['OTHER'] += 1; other.append('%s/%s LOD%d' % (sn, ew.get('name'), li))
    for k, v in c.items(): tot[k] += v
    res[m] = {'counts': dict(c), 'top_level_changes': top, 'velOverLife': vol, 'orbit': orbit, 'other': other}
    print('%-18s LODs %d: identical %d, Absolute-only %d, Orbit completed %d, VelOverLife %d, OTHER %d | top-level %s' % (
        m, c['lods'], c['identical'], c['Absolute=false only'], c['Orbit completed'], c['VelOverLife gained'], c['OTHER'], top or '-'))
    for o in other[:6]: print('    other:', o)
json.dump(res, open(sys.argv[3], 'w', encoding='utf-8'), indent=1)
