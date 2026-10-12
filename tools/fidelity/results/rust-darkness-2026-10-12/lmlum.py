# Expected irradiance per lightmapped component: sum_k mean(degamma(texel_k)) * scale_k (flat-normal weights 1/3 each), pixel-area weighted
import json,sys,statistics as st
import numpy as np
from PIL import Image
R=r"F:/Transformers Rebuild/Rebuild-Experimental/work/ab/m9c_9c3560e/work/render/"
for m in sys.argv[1:]:
    L=json.load(open(R+m+"/lighting.json")); P=L["lightmaps"]["props"]
    cache={}
    def tex(n):
        if n not in cache:
            a=np.asarray(Image.open(R+m+"/lightmaps/"+n+".png").convert("RGB"),dtype=np.float32)/255.0
            cache[n]=np.power(np.maximum(a,1e-4),2.2)
        return cache[n]
    vals=[]
    for v in P.values():
        cs,cb=v["coordScale"],v["coordBias"]
        tot=np.zeros(3)
        for k in range(3):
            a=tex(v["coeffs"][k]); h,w,_=a.shape
            x0,y0=int(cb[0]*w),int(cb[1]*h); x1,y1=max(x0+1,int((cb[0]+cs[0])*w)),max(y0+1,int((cb[1]+cs[1])*h))
            tot+=a[y0:y1,x0:x1].reshape(-1,3).mean(0)*np.array(v["scales"][k])/3
        vals.append(float(tot.mean()))
    vals=np.array(vals)
    print("%-15s comps %d  irradiance med %.3f p25 %.3f p75 %.3f  frac<0.05 %.2f frac<0.02 %.2f"%(m,len(vals),np.median(vals),np.percentile(vals,25),np.percentile(vals,75),(vals<0.05).mean(),(vals<0.02).mean()))
