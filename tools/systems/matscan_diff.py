import json, collections, re
maps = ['MP_IAC_Streets', 'MP_ESC_BrokenHope', 'MP_ESC_Remnant', 'MP_IAC_Berth', 'MP_IAC_Rust', 'MP_IAC_Seed', 'MP_KON_Molten', 'MP_ORB_Debris', 'MP_UND_Complex', 'MP_UND_Gorge']
R = 'F:/Transformers Rebuild/Rebuild-Systems/work/matscan/'
per = collections.defaultdict(list); errs = collections.defaultdict(list); sample = {}
for m in maps:
    A = json.load(open(R + 'cur/%s/materials_glsl.json' % m, encoding='utf-8')); B = json.load(open(R + 'vc/%s/materials_glsl.json' % m, encoding='utf-8'))
    def n(o, r): return json.dumps(o, sort_keys=True).replace(R + r + '/' + m + '/', 'ROOT/').replace(r + '/' + m + '/', 'ROOT/')
    for k in sorted(set(A) & set(B)):
        if n(A[k], 'cur') != n(B[k], 'vc'):
            per[k].append(m)
            if B[k].get('error'): errs[k].append(B[k]['error'][:120])
            if k not in sample: sample[k] = (A[k], B[k])
print('materials whose GLSL changes with vector channels honoured: %d' % len(per))
fieldsdiff = collections.Counter()
for k in sorted(per, key=lambda k: (-len(per[k]), k)):
    a, b = sample[k]
    f = [x for x in sorted(set(a) | set(b)) if json.dumps(a.get(x), sort_keys=True) != json.dumps(b.get(x), sort_keys=True)]
    print('%-70s maps %2d  fields %s%s' % (k, len(per[k]), ','.join(f)[:60], '  VC-ERROR ' + errs[k][0] if errs.get(k) else ''))
json.dump({k: per[k] for k in per}, open(R + 'scan_list.json', 'w'), indent=1)
