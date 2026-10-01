import sys, struct; sys.path.insert(0,r'F:/Transformers Rebuild/AssetTools/scripts/wfc')
import objtree
ps=sys.argv[1]; want=sys.argv[2]
pk=objtree.owner(ps); p=objtree.package(pk)
for k,i in p._idx.items():
    if not k.startswith(ps.lower()+'.') or p.class_name(p.exports[i-1])!='ParticleLODLevel': continue
    tags,used=p._tr.read_object(i); td={t['name']:t.get('value') for t in tags}
    if td.get('Level',0)!=0: continue
    em=k.split('.')[-2]; emitter=[x for x in p._idx if x.endswith(em)][0]
    if p._tr.read_dict(p._idx[emitter]).get('EmitterName')!=want: continue
    e=p.exports[i-1]; d=p.data[e['serial_offset']+used:e['serial_offset']+e['serial_size']]
    print(len(d))
    for o in range(0,len(d),16):
        ch=d[o:o+16]
        # show hex and any plausible float at each byte alignment
        fl=[]
        for b in range(0,16):
            if o+b+4<=len(d):
                v=struct.unpack_from('>f',d,o+b)[0]
                if 1e-3<abs(v)<1e5 and v==v: fl.append('%d:%g'%(o+b,v))
        print('%4d %-35s %s'%(o, ch.hex(), ' '.join(fl)))
