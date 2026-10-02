import sys,json; sys.path.insert(0,r'F:/Transformers Rebuild/AssetTools/scripts/wfc')
import objtree, typed_props as tp
ps=sys.argv[1]; pk=objtree.owner(ps); p=objtree.package(pk); pre=ps.lower()+'.'
print(ps, pk, tp.simplify(p._tr.read_dict(p._idx[ps.lower()])).get('LODDistances'))
for k,i in p._idx.items():
  if k.startswith(pre) and p.class_name(p.exports[i-1])=='ParticleLODLevel':
    td={t['name']:t.get('value') for t in p._tr.read_object(i)[0]}
    if td.get('Level',0)!=0: continue
    em=k.split('.')[-2]; en=p._tr.read_dict(p._idx[[x for x in p._idx if x.endswith(em)][0]]).get('EmitterName')
    r=tp.simplify(p._tr.read_dict(p._idx[td['RequiredModule']['ref'].lower()]))
    td2=td.get('TypeDataModule'); mesh=None
    if td2: mesh=tp.simplify(p._tr.read_dict(p._idx[td2['ref'].lower()])).get('Mesh')
    print(' %-28s mat=%s loops=%s dur=%s delay=%s/%s rate=%s burst=%s align=%s mesh=%s sub=%s,%s' % (en, r.get('Material'), r.get('EmitterLoops'), r.get('EmitterDuration'), r.get('EmitterDelay'), r.get('bDelayFirstLoopOnly'), r.get('SpawnRate',{}).get('LookupTable'), [b['Count'] for b in r.get('BurstList',[])], r.get('ScreenAlignment'), mesh, r.get('SubImages_Horizontal'), r.get('SubImages_Vertical')))
