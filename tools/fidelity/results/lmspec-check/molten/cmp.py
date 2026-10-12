# Compare Molten lightmap-specular off/on shots per cam: luminance stats, changed-pixel shares, tile grid, heatmap + side-by-side.
import sys, os, numpy as np
from PIL import Image
D = sys.argv[1]; F = "f2400.bmp"
def lum(a): return 0.2126*a[...,0] + 0.7152*a[...,1] + 0.0722*a[...,2]
rows = []
for cam in ["pick", "c11", "c9", "c15", "c14"]:
    po, pn = os.path.join(D, cam+"-off", F), os.path.join(D, cam+"-on", F)
    if not (os.path.exists(po) and os.path.exists(pn)): print(cam, "missing"); continue
    A = np.asarray(Image.open(po).convert("RGB")).astype(np.float32); B = np.asarray(Image.open(pn).convert("RGB")).astype(np.float32)
    la, lb = lum(A), lum(B); d = lb - la
    h, w = d.shape; gh, gw = 9, 16
    g = d[:h//gh*gh, :w//gw*gw].reshape(gh, h//gh, gw, w//gw).mean(axis=(1, 3))
    r = dict(cam=cam, off=la.mean(), on=lb.mean(), dmean=d.mean(), rel=100*(lb.mean()/max(la.mean(),1e-6)-1),
             gt2=100*(d > 2).mean(), gt8=100*(d > 8).mean(), gt24=100*(d > 24).mean(), lt2=100*(d < -2).mean(),
             p99=np.percentile(d, 99), mx=d.max(), tilemax=g.max(), tilemin=g.min())
    rows.append(r)
    hm = np.clip(d * 8, 0, 255).astype(np.uint8)
    Image.fromarray(np.stack([hm, hm//3, np.zeros_like(hm)], -1)).save(os.path.join(D, cam+"-delta.png"))
    sbs = np.concatenate([A, B], 1).astype(np.uint8); Image.fromarray(sbs).resize((1920, 540)).save(os.path.join(D, cam+"-offon.png"))
    np.savetxt(os.path.join(D, cam+"-tiles.txt"), g, fmt="%6.2f")
print("cam   | luma off | luma on | delta | rel % | px >2 | px >8 | px >24 | px <-2 | p99 d | max d | worst tile")
for r in rows:
    print("{cam:5} | {off:8.2f} | {on:7.2f} | {dmean:5.2f} | {rel:5.1f} | {gt2:5.1f} | {gt8:5.1f} | {gt24:6.2f} | {lt2:6.2f} | {p99:5.1f} | {mx:5.0f} | {tilemax:5.2f}".format(**r))
