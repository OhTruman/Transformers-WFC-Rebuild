import sys, json; sys.path.insert(0,r'F:/Transformers Rebuild/AssetTools/scripts/wfc')
import objtree, typed_props as tp
ps=sys.argv[1]; pk=sys.argv[2] if len(sys.argv)>2 else objtree.owner(ps)
p=objtree.package(pk); pre=ps.lower()+'.'
out={}
for k,i in sorted(p._idx.items(), key=lambda kv: kv[1]):
    if not k.startswith(pre) and k!=ps.lower(): continue
    e=p.exports[i-1]; cls=p.class_name(e)
    if cls=='Texture2D': continue
    try: d=tp.simplify(p._tr.read_dict(i))
    except Exception as ex: d={'err':str(ex)}
    out[p.object_path(i)]={'class':cls,'props':d,'size':e['serial_size']}
    s=json.dumps(d,default=str); print(cls, p.object_path(i)[len(ps)+1:], e['serial_size'], s[:700])
json.dump(out,open(ps+'.mods.json','w'),indent=1,default=str)
