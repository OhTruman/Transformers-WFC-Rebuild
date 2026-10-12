import json,sys,statistics as st
R=r"F:/Transformers Rebuild/Rebuild-Experimental/work/ab/m9c_9c3560e/work/render/"
for m in sys.argv[1:]:
    L=json.load(open(R+m+"/lighting.json"))
    P=L["lightmaps"]["props"]
    for k in range(3):
        ch=[[v["scales"][k][c] for v in P.values()] for c in range(3)]
        print(m, "coef",k, "median per channel", ["%.3f"%st.median(a) for a in ch], "ratio c0/c2 med %.3f"%st.median([v["scales"][k][0]/max(v["scales"][k][2],1e-6) for v in P.values()]))
