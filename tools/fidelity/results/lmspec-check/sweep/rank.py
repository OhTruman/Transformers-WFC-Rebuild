# Ranked world-material suspects across maps from lmspecg_<map>_sharedfinal.json: materials whose translated output does not match
# the original lightmapped base-pass PS (best shader residual > 1e-3), characters / weapons / FX excluded.
import json, glob, re, collections
CHAR = re.compile(r'^(TR_|WEP_|RB_|VH_|CHR_|FX_|TPRO_|DES_Prisoner)|Frenzy|Rumble|Laserbeak', re.I)
sus = collections.defaultdict(list); tot = collections.Counter()
for f in sorted(glob.glob('lmspecg_*_sharedfinal.json')):
    M = f[len('lmspecg_'):-len('_sharedfinal.json')]
    r = json.load(open(f))
    for k, x in r.items():
        if CHAR.search(k) or CHAR.search(k.split('.')[-1]): continue
        sh = [s for s in x.get('shaders', []) if s.get('status') == 'ok']
        if not sh: tot[(M, 'not emulated')] += 1; continue
        best = min(sh, key=lambda s: s[s['best_mode']]['spec_resid']); b = best[best['best_mode']]
        if b['spec_resid'] < 1e-3: tot[(M, 'exact')] += 1; continue
        tot[(M, 'differs')] += 1
        kind = 'spec colour only' if best.get('sfit_resid', 1) < 1e-3 else ('spec term extra' if b['diffuse_only_resid'] < 1e-3 else 'diffuse/normal')
        sus[k].append(dict(map=M, resid=b['spec_resid'], fog=b['fog'], dresid=b['diffuse_only_resid'], spec_share=best['spec_share'],
                           kind=kind, type=best['type'].replace('TBasePassPixelShaderFDirectional', '').replace('LightMapPolicy', ''), owner=best['owner'].split('.')[-1]))
for M in sorted({m for m, _ in tot}): print(M, {c: tot[(M, c)] for c in ('exact', 'differs', 'not emulated')})
rows = sorted(sus.items(), key=lambda kv: (-len(kv[1]), -max(e['resid'] for e in kv[1])))
print('\nrank | material | maps | worst resid | fitted scale | kind | spec share | policy | owner map')
for i, (k, es) in enumerate(rows, 1):
    w = max(es, key=lambda e: e['resid'])
    print('%d | %s | %s | %.3f | %.2f | %s | %.2f | %s | %s' % (i, k, ','.join(sorted(e['map'].split('_', 2)[-1] for e in es)), w['resid'], w['fog'], w['kind'], w['spec_share'], w['type'], w['owner']))
