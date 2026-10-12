# Per-material classification of lmspecg_<map>.json (exact / spec colour only / diffuse-normal differs / spec hurts / not emulated)
import json, sys, collections
r = json.load(open(sys.argv[2] if len(sys.argv) > 2 else 'lmspecg_%s.json' % sys.argv[1]))
cnt = collections.Counter(); lists = collections.defaultdict(list)
for k, x in r.items():
    sh = [s for s in x.get('shaders', []) if s.get('status') == 'ok']
    if not sh: cnt['not emulated: ' + (x.get('status') or (x.get('shaders') or [{}])[0].get('status', '?'))[:28]] += 1; continue
    b = [(s[s['best_mode']], s) for s in sh]
    if any(m['spec_resid'] < 1e-3 for m, _ in b): c = 'exact'
    elif any(s.get('sfit_resid', 1) < 1e-3 for _, s in b): c = 'SPEC COLOUR differs only'
    elif any(m['diffuse_only_resid'] < 1e-3 for m, _ in b): c = 'SPEC TERM EXTRA (diffuse-only fits)'
    else: c = 'diffuse/normal differs (not the spec term)'
    cnt[c] += 1; lists[c].append(k.split('.')[-1])
for c, n in cnt.most_common(): print(n, c)
for c in lists:
    if c != 'exact': print(c, ':', ', '.join(sorted(lists[c])[:60]))
