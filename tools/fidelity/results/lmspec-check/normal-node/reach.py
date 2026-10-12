# Which material outputs depend on the Normal node (m.normal) in a translated GLSL: dependency closure over the tN temporaries.
import json, re, sys, collections
def reach(g):
    deps = {}; outs = {}
    for l in g.split('\n'):
        l = l.strip()
        m = re.match(r'(?:\w+ )?(t\d+) = (.*);$', l)
        if m: deps[m.group(1)] = set(re.findall(r'\bt\d+\b', m.group(2))) | ({'NORMAL'} if 'm.normal' in m.group(2) else set())
        m = re.match(r'o\.(\w+) = (.*);$', l)
        if m: outs[m.group(1)] = set(re.findall(r'\bt\d+\b', m.group(2))) | ({'NORMAL'} if 'm.normal' in m.group(2) else set())
    memo = {}
    def uses(t):
        if t == 'NORMAL': return True
        if t in memo: return memo[t]
        memo[t] = False; memo[t] = any(uses(d) for d in deps.get(t, ())); return memo[t]
    return sorted(o for o, ds in outs.items() if any(uses(d) for d in ds))
if __name__ == '__main__':
    M = sys.argv[1]
    m = json.load(open(r'F:/Transformers Rebuild/Rebuild/work/render/%s/materials_glsl.json' % M, encoding='utf-8'))
    r = json.load(open('lmspecg_%s_sharedfinal.json' % M))
    ex = {k for k, x in r.items() if any(s.get('status') == 'ok' and s[s['best_mode']]['spec_resid'] < 1e-3 for s in x.get('shaders', []))}
    c = collections.Counter(); lit = []
    for k, v in m.items():
        g = v.get('glsl') or ''
        if ' = m.normal;' not in g: continue
        o = tuple(reach(g)); c[(o, k in ex)] += 1
        if set(o) & {'DiffuseColor', 'SpecularColor', 'SpecularPower'}: lit.append((k.split('.')[-1], o, k in ex))
    for (o, e), n in c.most_common(): print(n, 'exact' if e else 'not-exact/not-emulated', o)
    print('lit-output users:', len(lit)); [print('  ', x) for x in sorted(lit)[:40]]
