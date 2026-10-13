import sys,os
from PIL import Image
import numpy as np
A,B=sys.argv[1:3]; out=[]
for f in sorted(os.listdir(A+"/shots")):
    pb=B+"/shots/"+f
    if not os.path.exists(pb): out.append(f+":missing"); continue
    a=np.asarray(Image.open(A+"/shots/"+f).convert('RGB')).astype(int); b=np.asarray(Image.open(pb).convert('RGB')).astype(int)
    d=np.abs(a-b).max(axis=2); n=int((d>0).sum())
    if n:
        ys,xs=np.nonzero(d); out.append(f"{f[1:6]}:{n}px max{d.max()} ({xs.min()},{ys.min()})-({xs.max()},{ys.max()})")
print(A,B,"diffs",len(out)); [print("  ",o) for o in out]
