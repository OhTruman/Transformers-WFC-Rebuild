import sys, json, struct; sys.path.insert(0,r'F:/Transformers Rebuild/AssetTools/scripts/wfc')
import objtree
ps=sys.argv[1]; pk=sys.argv[2] if len(sys.argv)>2 else objtree.owner(ps)
p=objtree.package(pk); pre=ps.lower()+'.'
def scan(d, start):
    out=[]; i=start
    while i+8<=len(d):
        t,op,n,ch=d[i],d[i+1],d[i+2],d[i+3]
        if 1<=t<=12 and op<=3 and 1<=n<=64 and 1<=ch<=16 and i+8<=len(d):
            cnt=struct.unpack_from('>i',d,i+4)[0]
            if cnt>=1 and cnt<=2048 and i+8+cnt*4<=len(d) and True:
                fl=struct.unpack_from('>%df'%cnt,d,i+8)
                if all(abs(x)<1e7 for x in fl):
                    out.append((i,t,op,n,ch,[round(x,4) for x in fl][:40])); i+=8+cnt*4; continue
        i+=1
    return out
for k,i in sorted(p._idx.items(), key=lambda kv: kv[1]):
    if not k.startswith(pre) or p.class_name(p.exports[i-1])!='ParticleLODLevel': continue
    tags,used=p._tr.read_object(i)
    td={t['name']:t.get('value') for t in tags}
    if td.get('Level',0)!=0: continue
    e=p.exports[i-1]; d=p.data[e['serial_offset']:e['serial_offset']+e['serial_size']]
    em=k.split('.')[-2]
    emitter=[x for x in p._idx if x.endswith(em)][0]
    en=p._tr.read_dict(p._idx[emitter]).get('EmitterName')
    req=td['RequiredModule']['ref'] if td.get('RequiredModule') else None
    rq=p._tr.read_dict(p._idx[req.lower()]) if req else {}
    print('\n=== EMITTER', en, 'mat=',rq.get('Material',{}).get('ref') if isinstance(rq.get('Material'),dict) else rq.get('Material'), 'align=',rq.get('ScreenAlignment'),'dur=',rq.get('EmitterDuration'),'burst=',rq.get('BurstList'),'local=',rq.get('bUseLocalSpace'), 'td=', td.get('TypeDataModule',{}) and td['TypeDataModule']['ref'].split('.')[-1])
    print('   tail', len(d)-used, 'bytes')
    print('   hex', d[used:used+64].hex())
    for (o,t,op,n,ch,fl) in scan(d,used): print('   @%d type=%d op=%d n=%d ch=%d'%(o-used,t,op,n,ch), fl)
