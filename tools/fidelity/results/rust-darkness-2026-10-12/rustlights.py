import json,collections,sys
for m in sys.argv[1:]:
    L=json.load(open(r"F:/Transformers Rebuild/Rebuild-Experimental/work/ab/m9c_9c3560e/work/render/%s/lighting.json"%m))
    ls=L["lights"]
    c=collections.Counter((l.get("class"), l.get("built_into_lightmap"), l.get("enabled",True)) for l in ls)
    print(m, len(ls)); [print("  ",v,k) for k,v in c.most_common()]
    ub=[l for l in ls if not l.get("built_into_lightmap") and l.get("enabled",True)]
    br=sorted(ub,key=lambda l:-l.get("brightness",1)*l.get("radius_m",0))[:6]
    for l in br: print("    unbaked", l.get("class"), l.get("name"), "b", l.get("brightness"), "r", l.get("radius_m"), "ch", {k:v for k,v in (l.get("channels") or {}).items() if v})
