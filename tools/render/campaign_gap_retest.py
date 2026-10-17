# Re-test the campaign materials AssetTools' dry run (work/matc_dryrun/<CH>.json) found not compiling, with Rendering's matc (rt2
# tools/render), in the same chapter package sets. Usage: retest.py
import json, os, sys, collections
sys.dont_write_bytecode = True
sys.path.insert(0, r"F:/Transformers Rebuild/Rebuild-Rendering/tools/render")
import matc
from ue3obj import Repo, COOKED
AT = r"F:/Transformers Rebuild/AssetTools"
D = AT + "/work/matc_dryrun"
FALLBACK = ['TransGame.xxx', 'TR_AllShader_p.xxx', 'UI_GFxHud_p.xxx']
class StubTex:
    def __init__(self, repo): self.R = repo
    def __call__(self, path):
        if not path: return None
        return {'object': path, 'file': None, 'class': self.R.cls(path), 'srgb': True, 'dry_run': True}
cooked = {f.lower(): f for f in os.listdir(COOKED)}
tot = collections.Counter(); out = {}
for f in sorted(os.listdir(D)):
    if not f.endswith(".json"): continue
    ch = f[:-5]; r = json.load(open(os.path.join(D, f), encoding="utf-8"))
    fails = [k for k, v in r["results"].items() if v["result"] != "clean" and not v["control"]]
    if not fails: continue
    R = Repo(r["packages"], fallback=FALLBACK); tr = StubTex(R)
    for mp in fails:
        try:
            g, info = matc.MatCompiler(R, mp, tr).build(); err = None
        except Exception as ex:
            err = '%s: %s' % (type(ex).__name__, ex)
        out[mp] = {"chapter": ch, "before": r["results"][mp]["result"], "error": err}
        tot["ok" if err is None else "fail"] += 1
        print(("OK  " if err is None else "ERR ") + ch, mp, "" if err is None else err[:160])
json.dump(out, open(r"F:/Transformers Rebuild/Rebuild-Rendering/work/camp_mat/retest.json", "w"), indent=1)
print(dict(tot))
