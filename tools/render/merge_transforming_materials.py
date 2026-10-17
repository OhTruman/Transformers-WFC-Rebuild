# Transforming-actor materials into the LIVE materials_glsl.json (only the keys it lacks; tex paths -> the live map tex/): after
# build_materials_only.py <live root> work/mat_trans MP_ESC_BrokenHope MP_ESC_Remnant MP_IAC_Rust -> work/mat_trans_merged/<map>/
import json, os, subprocess, sys, shutil
PY = r"F:/Transformers Rebuild/AssetTools/bin/py/python.exe"
LIVE = r"F:/Transformers Rebuild/Rebuild/work/render"
for m in ["MP_ESC_BrokenHope", "MP_ESC_Remnant", "MP_IAC_Rust"]:
    a = json.load(open(f"{LIVE}/{m}/materials_glsl.json", encoding="utf-8"))
    b = json.load(open(f"work/mat_trans/{m}/materials_glsl.json", encoding="utf-8"))
    A = a.get("materials", a); B = b.get("materials", b)
    want = subprocess.run([PY, "-I", "tools/render/transforming_materials.py", m], capture_output=True, text=True).stdout.split()
    add = [w for w in want if w not in A]
    bad = [w for w in add if w not in B or B[w].get("error") not in (None, "None") or not B[w].get("glsl")]
    for w in add:
        if w in B: A[w] = json.loads(json.dumps(B[w]).replace(f"work/mat_trans/{m}/tex/", f"F:/Transformers Rebuild/Rebuild/work/render/{m}/tex/"))
    os.makedirs(f"work/mat_trans_merged/{m}", exist_ok=True)
    json.dump(a, open(f"work/mat_trans_merged/{m}/materials_glsl.json", "w", encoding="utf-8"), indent=0)
    tex = sorted({f for w in add for f in (t.get("file") if isinstance(t, dict) else t for t in A[w]["info"].get("textures", [])) if isinstance(f, str)})
    missing = [f for f in tex if not os.path.exists(f)]
    print(m, "added", add, "bad", bad, "| textures", len(tex), "missing in live", missing)
