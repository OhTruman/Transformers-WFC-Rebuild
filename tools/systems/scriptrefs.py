import sys, struct; sys.path.insert(0,r'F:/Transformers Rebuild/AssetTools/scripts/wfc')
import objtree
p=objtree.package('TransGame')
def find(cls):
    return [i for i in range(1,len(p.exports)+1) if p.exports[i-1]['name']==cls and p.class_name(p.exports[i-1])=='Class'][0]
def kids(outer):
    return [i for i in range(1,len(p.exports)+1) if p.exports[i-1]['outer']==outer]
def name(i):
    try: return p.full_path(i)
    except Exception: return None
def refs(i):
    e=p.exports[i-1]; d=p.data[e['serial_offset']:e['serial_offset']+e['serial_size']]
    out=[]
    for o in range(0,len(d)-3):
        v=struct.unpack_from('>i',d,o)[0]
        if v==0 or abs(v)>max(len(p.exports),len(p.imports)): continue
        if v>0 and v<=len(p.exports):
            c=p.class_name(p.exports[v-1])
            if c in ('Function','ObjectProperty','FloatProperty','IntProperty','BoolProperty','StructProperty','ByteProperty','ArrayProperty','InterfaceProperty','NameProperty','State','ComponentProperty','ClassProperty','Const'):
                out.append(p.exports[v-1]['name'])
        elif v<0 and -v<=len(p.imports):
            im=p.imports[-v-1]
            if im['class'] in ('Function','ObjectProperty','FloatProperty','IntProperty','BoolProperty','StructProperty','ByteProperty','ArrayProperty','InterfaceProperty'):
                out.append('imp:'+im['name'])
    seen=[]; [seen.append(x) for x in out if x not in seen]
    return seen
for cls in sys.argv[1:]:
    ci=find(cls)
    for k in kids(ci):
        e=p.exports[k-1]; c=p.class_name(e)
        if c in ('Function','State'):
            r=refs(k)
            interesting=[x for x in r if any(s in x.lower() for s in ('ram','nitro','boost','fx','hover','jump','dash','sound','audio'))]
            print('%s.%s [%s %d] %s'%(cls,e['name'],c,e['serial_size'],interesting[:40]))
            if c=='State':
                for kk in kids(k):
                    ee=p.exports[kk-1]
                    if p.class_name(ee)=='Function':
                        r=refs(kk); interesting=[x for x in r if any(s in x.lower() for s in ('ram','nitro','boost','fx','hover','jump','dash','sound','audio'))]
                        print('    %s.%s [%d] %s'%(e['name'],ee['name'],ee['serial_size'],interesting[:40]))
