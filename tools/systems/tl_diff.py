"""Pair the on / off captures of tl_ab.ps1 by name: per pair the share of pixels brighter by > 8/255 and the mean gain there;
writes side-by-side crops (on | off | diff x4) of the strongest pairs. argv[1] = work/tl_ab."""
import sys, os, numpy as np
from PIL import Image
root = sys.argv[1]
for mode in ('rt2', 'rt0'):
    a, b = os.path.join(root, mode + '_on', 'shots'), os.path.join(root, mode + '_off', 'shots')
    rows = []
    for f in sorted(os.listdir(a)):
        if not f.endswith('.bmp') or not os.path.exists(os.path.join(b, f)):
            continue
        A = np.asarray(Image.open(os.path.join(a, f)).convert('RGB')).astype(np.int16)
        B = np.asarray(Image.open(os.path.join(b, f)).convert('RGB')).astype(np.int16)
        if A.shape != B.shape:
            continue
        d = (A - B).max(axis=2)
        lit = d > 8
        dark = (A - B).min(axis=2) < -8
        rows.append((lit.mean(), dark.mean(), float(d[lit].mean()) if lit.any() else 0.0, f, A, B))
    if not rows:
        print(mode, 'no pairs'); continue
    lits = [r[0] for r in rows]
    print(f'{mode}: {len(rows)} pairs; frames with >0.1% lit pixels {sum(x > 0.001 for x in lits)}; lit share max {max(lits)*100:.2f}% '
          f'median {np.median(lits)*100:.3f}%; darker-than-off pixels max {max(r[1] for r in rows)*100:.3f}%')
    for lshare, dshare, gain, f, A, B in sorted(rows, key=lambda r: -r[0])[:3]:
        D = np.clip((A - B) * 4, 0, 255)
        img = np.concatenate([A, B, D], axis=1).astype(np.uint8)
        out = os.path.join(root, f'crop_{mode}_{os.path.splitext(f)[0]}.png')
        im = Image.fromarray(img)
        im.thumbnail((2400, 2400))
        im.save(out)
        print(f'  {f}: lit {lshare*100:.2f}% (mean +{gain:.0f}/255), darker {dshare*100:.3f}% -> {out}')
