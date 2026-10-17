import json, re, difflib, collections
R = 'F:/Transformers Rebuild/Rebuild-Systems/work/matscan/'
per = json.load(open(R + 'scan_list.json'))
lit = re.compile(r'\(vec4\(([^()]*)\)\)\.([xyzw])')
rt = re.compile(r'uRT_')
out = {'value_change': [], 'equivalent': [], 'runtime_param': [], 'other': []}
detail = {}
for k, maps in sorted(per.items()):
    m = maps[0]
    a = json.load(open(R + 'cur/%s/materials_glsl.json' % m, encoding='utf-8'))[k]['glsl']; b = json.load(open(R + 'vc/%s/materials_glsl.json' % m, encoding='utf-8'))[k]['glsl']
    added = [l for l in difflib.unified_diff(a.splitlines(), b.splitlines(), lineterm='', n=0) if l.startswith('+') and not l.startswith('+++')]
    sels = []
    for l in added:
        for g in lit.finditer(l):
            try: vals = [float(x) for x in g.group(1).split(",")]
            except ValueError: sels.append((None, 0)); continue
            sels.append((vals, 'xyzw'.index(g.group(2))))
    hasrt = any(rt.search(l) and ').' in l for l in added)
    if hasrt: cls = 'runtime_param'
    elif not sels: cls = 'other'
    else:
        if any(s[0] is None for s in sels): sels = [s for s in sels if s[0] is not None]; nonlit = True
        else: nonlit = False
        diff = [s for s in sels if any(abs(v - s[0][s[1]]) > 1e-6 for v in s[0][:3])]   # old code used the rgb (or all) components
        cls = 'value_change' if diff else ('other' if nonlit else 'equivalent')
    out[cls].append(k); detail[k] = {'maps': len(maps), 'selected': [('(%s).%s' % (', '.join('%g' % v for v in s[0]), 'xyzw'[s[1]])) for s in sels][:4]}
for c in out: print('%-14s %d' % (c, len(out[c])))
for c in ('value_change', 'runtime_param', 'other'):
    print('==', c)
    for k in out[c]: print('  %-72s maps %2d  %s' % (k, detail[k]['maps'], '; '.join(detail[k]['selected'])[:120]))
json.dump({'classes': out, 'detail': detail}, open(R + 'scan_classes.json', 'w'), indent=1)
