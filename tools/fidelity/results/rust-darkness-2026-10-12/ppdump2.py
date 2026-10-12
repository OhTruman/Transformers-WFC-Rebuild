import json,sys
R=r"F:/Transformers Rebuild/Rebuild-Experimental/work/ab/m9c_9c3560e/work/render/"
for m in sys.argv[1:]:
    P=json.load(open(R+m+"/lighting.json"))["postprocess"]
    s=P["settings"]
    print(m, {k:v for k,v in P.items() if k!="settings"})
    print("  ", {k:s[k] for k in s if k.startswith("Scene_") or k.startswith("bOverride") or "CLUT" in k.upper() or "Tone" in k})
