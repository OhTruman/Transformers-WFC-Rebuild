# For materials with no named LM shader map: find a NAMED material in the map's ShaderCache with the same master and the same
# effective static switch values (UE3 shares a static-permutation shader map between MICs with equal StaticParameterSets).
import sys, json, collections
sys.path.insert(0, 'F:/Transformers Rebuild/Rebuild-Rendering/tools/render')
from ue3obj import Repo, map_packages
MAP = sys.argv[1]
pk, _ = map_packages(MAP)
mats = json.load(open('F:/Transformers Rebuild/Rebuild/work/render/%s/materials_glsl.json' % MAP, encoding='utf-8'))
R = Repo(list(reversed(pk)), fallback=['TransGame.xxx', 'TR_AllShader_p.xxx'] + sorted({k.split('.')[0] + '.xxx' for k in mats if k.startswith(('TR_', 'WEP_'))}))
mats = json.load(open('F:/Transformers Rebuild/Rebuild/work/render/%s/materials_glsl.json' % MAP, encoding='utf-8'))
KEYS = set(open('chr_keys.txt').read().split())
scm = [x for x in json.load(open('scall_%s_all.json' % MAP)) if 'NoLightMapPolicy' in x['type']]
named = sorted({n for x in scm for n in x['materials']})
def key(path):
    try:
        sw, other = R.mic_static_params(path)
    except Exception as ex:
        return None
    master = path                                                   # root master: walk Parent refs
    for _ in range(12):
        try: par = (R.obj(master).get('Parent') or {}).get('ref')
        except Exception: par = None
        if not par: break
        master = par
    eff = tuple(sorted((k, bool(v[0])) for k, v in (sw or {}).items()))
    return (master, eff, json.dumps(other, sort_keys=True, default=str))
nk = collections.defaultdict(list)
for n in named:
    k = key(n)
    if k: nk[k].append(n)
out = {}; cnt = collections.Counter()
for k_ in mats:
    if k_.split('.')[-1] not in KEYS: continue
    k = key(k_)
    hit = nk.get(k) if k else None
    out[k_] = hit[0] if hit else None
    cnt['shares a named map' if hit else 'no named map with equal statics'] += 1
json.dump(out, open('chrshare_%s.json' % MAP, 'w'), indent=1)
print(cnt); [print(' ', k.split('.')[-1], '->', v) for k, v in sorted(out.items())]
