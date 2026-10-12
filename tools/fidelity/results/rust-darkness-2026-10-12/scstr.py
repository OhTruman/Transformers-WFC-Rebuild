import re,sys,collections
for f in sys.argv[1:]:
    b=open(f,"rb").read()
    c=collections.Counter(m.group().decode() for m in re.finditer(rb"[A-Za-z_][A-Za-z0-9_<>,]{5,}",b))
    print("==",f.split("/")[-1],len(b))
    for k,v in sorted(c.items(),key=lambda x:-x[1]):
        if re.search("lightmap|specular",k,re.I): print("  %5d %s"%(v,k))
