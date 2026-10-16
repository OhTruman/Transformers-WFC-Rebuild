"""Survey the extracted VerticalSlice weapon / character json for the original's transient lights (Rendering plan v2
item 4): every exported object whose class is a light component / actor, every property whose name mentions Light,
and the owning file. argv[1] = ExtractedAssets/VerticalSlice (read only), argv[2] = output json."""
import json, os, sys, collections

root, out = sys.argv[1], sys.argv[2]
lights, keys, classes = [], collections.Counter(), collections.Counter()
for dp, dn, fn in os.walk(root):
    for f in fn:
        if not f.endswith('.json'):
            continue
        p = os.path.join(dp, f)
        try:
            d = json.load(open(p, encoding='utf-8'))
        except Exception:
            continue
        rel = os.path.relpath(p, root).replace('\\', '/')
        stack, found = [('', d)], []
        while stack:   # every nested dict carrying a 'class' is an exported object (keyed by its path)
            n, o = stack.pop()
            if isinstance(o, dict):
                if isinstance(o.get('class'), str):
                    found.append((n, o))
                stack.extend(o.items())
            elif isinstance(o, list):
                stack.extend((n, x) for x in o)
        for name, o in found:
            c = o['class']
            classes[c] += 1
            if 'light' in c.lower():
                lights.append({'file': rel, 'object': name, **{k: v for k, v in o.items() if k not in ('LightGuid', 'LightmapGuid')}})
            for k, v in o.items():
                if 'light' in k.lower():
                    keys[(rel.split('/')[0], c, k, json.dumps(v)[:120])] += 1
res = {'light_objects': lights,
       'light_classes': {c: n for c, n in classes.items() if 'light' in c.lower()},
       'light_properties': [{'dir': a, 'class': b, 'key': k, 'value': v, 'n': n} for (a, b, k, v), n in sorted(keys.items())]}
json.dump(res, open(out, 'w', encoding='utf-8'), indent=1)
print(len(lights), 'light objects;', res['light_classes'])
