"""Structural diff of regenerated map_fx_runtime.json vs the current render data.
argv[1] = handoff dir (<map>/map_fx_runtime.json), argv[2] = current render dir, argv[3] = out json."""
import json, os, sys, collections, re

def walk(a, b, path, out):
    if type(a) != type(b):
        out.append((path, a, b)); return
    if isinstance(a, dict):
        for k in set(a) | set(b):
            if k not in a: out.append((path + '/' + k, '<absent>', b[k]))
            elif k not in b: out.append((path + '/' + k, a[k], '<absent>'))
            else: walk(a[k], b[k], path + '/' + k, out)
    elif isinstance(a, list):
        if len(a) != len(b): out.append((path + '[len]', len(a), len(b)))
        for i in range(min(len(a), len(b))): walk(a[i], b[i], '%s[%d]' % (path, i), out)
    elif a != b:
        out.append((path, a, b))

res = {}
for m in sorted(os.listdir(sys.argv[1])):
    new = os.path.join(sys.argv[1], m, 'map_fx_runtime.json')
    if not os.path.isfile(new): continue
    old = json.load(open(os.path.join(sys.argv[2], m, 'map_fx_runtime.json'), encoding='utf-8'))
    nw = json.load(open(new, encoding='utf-8'))
    d = []
    walk(old, nw, '', d)
    cats = collections.Counter(); lods = collections.defaultdict(set); systems = set()
    for p, a, b in d:
        if p == '/source': cats['source string'] += 1; continue
        g = re.match(r'/systems/([^/]+)/emitters\[(\d+)\]/lods\[(\d+)\](.*)$', p)
        key = re.sub(r'\[\d+\]', '[]', p.split('/lods[')[1].split(']', 1)[1]) if g else re.sub(r'\[\d+\]', '[]', p)
        cats[key] += 1
        if g: lods[key].add((g.group(1), g.group(2), g.group(3))); systems.add(g.group(1))
    res[m] = {'changed_values': len(d), 'systems_touched': sorted(systems),
              'by_path': dict(cats.most_common()), 'lods_by_path': {k: len(v) for k, v in lods.items()},
              'samples': [[p, str(a)[:120], str(b)[:120]] for p, a, b in d[:12]]}
    print('%-18s changed %5d values in %3d systems | %s' % (m, len(d), len(systems),
          '; '.join('%s x%d (%d LODs)' % (k, c, len(lods.get(k, ()))) for k, c in cats.most_common(6))))
json.dump(res, open(sys.argv[3], 'w', encoding='utf-8'), indent=1)
