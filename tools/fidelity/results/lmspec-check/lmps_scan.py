import struct,sys
sys.path.insert(0,r"F:/Transformers Rebuild/RE-Workspace/tools")
import xenosdis as X
for f in sys.argv[1:]:
    d=open(f,"rb").read()
    i=0
    while True:
        i=d.find(b"\x10\x2A\x11\x00",i)
        if i<0: break
        try:
            cs=struct.unpack_from(">I",d,i+0x24)[0]
            ver,tgt,consts=X.parse_ctab(d,i+0x28)
            names=[c[0] for c in consts]
            if any("LightMap" in n for n in names): print(f.split("/")[-1], hex(i), names)
        except Exception as e: pass
        i+=4
