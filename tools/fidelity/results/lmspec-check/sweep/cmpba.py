# Before/after (normal-node fix) exact counts per map: world and all materials; materials that changed class.
import json, glob, re, collections
CHAR = re.compile(r'^(TR_|WEP_|RB_|VH_|CHR_|FX_|TPRO_|DES_Prisoner)|Frenzy|Rumble|Laserbeak', re.I)
def cls(x):
    sh = [s for s in x.get('shaders', []) if s.get('status') == 'ok']
    if not sh: return 'n/e'
    return 'exact' if any(s[s['best_mode']]['spec_resid'] < 1e-3 for s in sh) else 'differs'
print('map | world exact before -> after | world differs before -> after | all exact before -> after | all differs before -> after')
T = collections.Counter(); moved = collections.Counter(); regress = []
for f in sorted(glob.glob('lmspecg_*_sharedfinal.json')):
    M = f[8:-17]; A = json.load(open(f)); B = json.load(open('before/' + f))
    c = collections.Counter()
    for k in set(A) | set(B):
        w = not (CHAR.search(k) or CHAR.search(k.split('.')[-1]))
        b, a = cls(B.get(k, {})), cls(A.get(k, {}))
        for tag, v in (('b', b), ('a', a)):
            c[(tag, 'all', v)] += 1
            if w: c[(tag, 'world', v)] += 1
        if b != a: moved[(b, a, 'world' if w else 'char')] += 1
        if b == 'exact' and a != 'exact': regress.append((M, k.split('.')[-1], a))
    print('%s | %d -> %d | %d -> %d | %d -> %d | %d -> %d' % (M, c[('b','world','exact')], c[('a','world','exact')], c[('b','world','differs')], c[('a','world','differs')],
          c[('b','all','exact')], c[('a','all','exact')], c[('b','all','differs')], c[('a','all','differs')]))
    T.update(c)
print('TOTAL | %d -> %d | %d -> %d | %d -> %d | %d -> %d' % (T[('b','world','exact')], T[('a','world','exact')], T[('b','world','differs')], T[('a','world','differs')],
      T[('b','all','exact')], T[('a','all','exact')], T[('b','all','differs')], T[('a','all','differs')]))
print('class moves (map-material pairs):', dict(moved)); print('regressions (exact before, not after):', regress)
