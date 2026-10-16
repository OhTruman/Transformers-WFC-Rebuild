"""Pair two rt_lights_ab arms by capture name (argv[1] root, argv[2] arm A = base, argv[3] arm B): share of pixels brighter by
> 8/255, darker share, and side-by-side crops (B | A | (B - A) x4) of the strongest pairs (argv[4] = how many, default 4)."""
import sys, os, numpy as np
from PIL import Image
root, a_arm, b_arm = sys.argv[1:4]
top = int(sys.argv[4]) if len(sys.argv) > 4 else 4
a, b = os.path.join(root, a_arm, 'shots'), os.path.join(root, b_arm, 'shots')
rows = []
for f in sorted(os.listdir(b)):
    if not f.endswith('.bmp') or not os.path.exists(os.path.join(a, f)):
        continue
    A = np.asarray(Image.open(os.path.join(a, f)).convert('RGB')).astype(np.int16)
    B = np.asarray(Image.open(os.path.join(b, f)).convert('RGB')).astype(np.int16)
    if A.shape != B.shape:
        continue
    d = B - A
    lit, dark = d.max(axis=2) > 8, d.min(axis=2) < -8
    rows.append((lit.mean(), dark.mean(), f, A, B))
print('%s vs %s: %d pairs, %d with > 0.1%% lit pixels, max lit %.2f%%, max darker %.3f%%' % (
    b_arm, a_arm, len(rows), sum(r[0] > 0.001 for r in rows), max((r[0] for r in rows), default=0) * 100, max((r[1] for r in rows), default=0) * 100))
for lit, dark, f, A, B in sorted(rows, key=lambda r: -r[0])[:top]:
    D = np.clip((B - A) * 4, 0, 255)
    im = Image.fromarray(np.concatenate([B, A, D], axis=1).astype(np.uint8))
    im.thumbnail((2400, 2400))
    out = os.path.join(root, 'crop_%s_%s.png' % (b_arm, os.path.splitext(f)[0]))
    im.save(out)
    print('  %s: lit %.2f%%, darker %.3f%% -> %s' % (f, lit * 100, dark * 100, out))
