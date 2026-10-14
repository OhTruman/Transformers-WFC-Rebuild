import sys,os
from PIL import Image
import numpy as np
A,B=sys.argv[1:3]; out=[]
# optional 3rd arg: the first step where the two runs' SIMHASH differ; captures from it on are a different simulation, not a render diff
simdiv=int(sys.argv[3]) if len(sys.argv)>3 and sys.argv[3].isdigit() else None; nsd=0
for f in sorted(os.listdir(A+"/shots")):
    pb=B+"/shots/"+f
    if not os.path.exists(pb): out.append(f+":missing"); continue
    a=np.asarray(Image.open(A+"/shots/"+f).convert('RGB')).astype(int); b=np.asarray(Image.open(pb).convert('RGB')).astype(int)
    d=np.abs(a-b).max(axis=2); n=int((d>0).sum())
    if simdiv is not None and int(f[1:6])>=simdiv:
        if n: nsd+=1; out.append(f"{f[1:6]}:SIM-DIVERGED (the sim differs from step {simdiv}; {n}px - not a render diff)")
        continue
    if n:
        ys,xs=np.nonzero(d); out.append(f"{f[1:6]}:{n}px max{d.max()} ({xs.min()},{ys.min()})-({xs.max()},{ys.max()})")
print(A,B,"diffs",len(out)-nsd,("render diffs before the sim divergence at step %d; %d later captures SIM-DIVERGED" % (simdiv,nsd)) if simdiv is not None else ""); [print("  ",o) for o in out]
