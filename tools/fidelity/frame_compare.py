# One eye-pass frame across builds and modes, side by side, with the near-white share and the mean colour of the near-white
# pixels per panel (Integration 2026-10-16: the Gorge step-1080 explosion check - Off keeps a warm white; a Realistic fix row
# must not turn it yellow-green nor wash the frame whiter than Off).
#   python frame_compare.py <out.png> <step e.g. 01080> <map> <build> [<build> ...]     (builds = work/eyepass/<build>)
import os, sys
import numpy as np
from PIL import Image, ImageDraw
out, step, mp, builds = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
B = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "work", "eyepass")
modes = ["off", "real", "realhd"]; W, H = 960, 540
S = Image.new("RGB", (len(modes) * W, len(builds) * (H + 30)), "black"); d = ImageDraw.Draw(S)
for i, b in enumerate(builds):
    for j, m in enumerate(modes):
        p = os.path.join(B, b, f"{mp}-{m}", "shots", f"m{step}.bmp")
        if not os.path.exists(p): d.text((j * W + 6, i * (H + 30) + 6), f"{b} {m}: missing", fill="red"); continue
        a = np.asarray(Image.open(p).convert("RGB"), dtype=np.float32); br = a.max(-1) > 235
        r, g, bl = a[br].mean(0) if br.sum() else (0, 0, 0)
        warm = (r - bl) if br.sum() else 0
        R, G, Bc = a[..., 0], a[..., 1], a[..., 2]
        yg = ((G > 150) & (G >= R - 10) & (G - Bc > 60)).mean() * 100   # yellow-green: bright green >= red, far above blue
        S.paste(Image.fromarray(a.astype(np.uint8)).resize((W, H)), (j * W, i * (H + 30) + 30))
        txt = f"{b} - {m}: near-white {br.mean() * 100:.1f}%  mean RGB {r:.0f},{g:.0f},{bl:.0f}  warmth (R-B) {warm:+.0f}  yellow-green {yg:.1f}%"
        d.text((j * W + 6, i * (H + 30) + 6), txt, fill="white"); print(txt)
S.save(out); print("->", out)
