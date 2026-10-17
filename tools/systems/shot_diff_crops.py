"""Diff two capture folders (same file names, BMP / PNG): per differing frame, the differing-pixel count, max channel delta and
the bounding box of the differing pixels; saves side-by-side crops (A | B | |A-B| x 4, the box + 24 px) for eye review.
Usage: python -I shot_diff_crops.py <dir A> <dir B> <out dir> [threshold=0]"""
import os, sys
import numpy as np
from PIL import Image
a_dir, b_dir, out = sys.argv[1], sys.argv[2], sys.argv[3]
thr = int(sys.argv[4]) if len(sys.argv) > 4 else 0
os.makedirs(out, exist_ok=True)
names = sorted(n for n in os.listdir(a_dir) if n.lower().endswith(('.bmp', '.png')))
same = 0
for n in names:
    pb = os.path.join(b_dir, n)
    if not os.path.isfile(pb): print('%s: missing in B' % n); continue
    A = np.asarray(Image.open(os.path.join(a_dir, n)).convert('RGB')).astype(np.int16)
    B = np.asarray(Image.open(pb).convert('RGB')).astype(np.int16)
    if A.shape != B.shape: print('%s: size differs %s %s' % (n, A.shape, B.shape)); continue
    d = np.abs(A - B).max(axis=2)
    m = d > thr
    if not m.any(): same += 1; continue
    ys, xs = np.nonzero(m)
    y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
    print('%s: %d px differ (max %d), box x %d-%d y %d-%d (%dx%d)' % (n, int(m.sum()), int(d.max()), x0, x1, y0, y1, x1 - x0 + 1, y1 - y0 + 1))
    pad = 24
    Y0, Y1 = max(0, y0 - pad), min(A.shape[0], y1 + pad + 1); X0, X1 = max(0, x0 - pad), min(A.shape[1], x1 + pad + 1)
    ca, cb = A[Y0:Y1, X0:X1].astype(np.uint8), B[Y0:Y1, X0:X1].astype(np.uint8)
    cd = np.clip(np.abs(A[Y0:Y1, X0:X1] - B[Y0:Y1, X0:X1]) * 4, 0, 255).astype(np.uint8)
    Image.fromarray(np.concatenate([ca, cb, cd], axis=1)).save(os.path.join(out, os.path.splitext(n)[0] + '_crop.png'))
print('frames %d, identical %d, differing %d' % (len(names), same, len(names) - same))
