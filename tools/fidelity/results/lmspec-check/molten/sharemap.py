# For materials with no named LM shader map: find a NAMED material in the map's ShaderCache with the same master and the same
# effective static switch values (UE3 shares a static-permutation shader map between MICs with equal StaticParameterSets).
import sys, json, collections
sys.path.insert(0, 'F:/Transformers Rebuild/Rebuild-Rendering/tools/render')
from ue3obj import Repo, map_packages
MAP = sys.argv[1]
pk, _ = map_packages(MAP); R = Repo(list(reversed(pk)), fallback=['TransGame.xxx', 'TR_AllShader_p.xxx'])
mats = json.load(open('F:/Transformers Rebuild/Rebuild/work/render/%s/materials_glsl.json' % MAP, encoding='utf-8'))
res = json.load(open('lmspecg_%s.json' % MAP))
scm = json.load(open('scmap_%s.json' % MAP))
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
for k_, x in res.items():
    if x.get('status') != 'no-original-lm-shader': continue
    info = mats.get(k_, {}).get('info') or {}
    if info.get('lighting_model') in ('MLM_Unlit', None): continue
    k = key(k_)
    hit = nk.get(k) if k else None
    out[k_] = hit[0] if hit else None
    cnt['shares a named map' if hit else 'no named map with equal statics'] += 1
json.dump(out, open('sharemap_%s.json' % MAP, 'w'), indent=1)
print(cnt); print('Parasite_01 ->', out.get('ENV_KON_Prison_p.Materials.Parasite_01_MATINST'))
