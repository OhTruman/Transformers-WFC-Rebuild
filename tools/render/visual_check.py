"""Visual sanity check for WFC_VISUALCHECK captures (M10).

Every screenshot taken with WFC_VISUALCHECK=1 writes <shot>.bmp.json next to the image: the renderer's verdict, render
path (original shader path vs legacy fallback), draw / material / lightmap counts, camera matrices, viewport,
framebuffer, the GL state the frame inherited, and image metrics of the 3D scene before 2D composition.

    python visual_check.py <capture dir or .json>... [--ref <dir>] [--max-diff 0.25] [--expect-map]

Checks (any failure -> exit 1):
  * the renderer's own verdict (legacy fallback, no map geometry, mostly black / flat scene);
  * --expect-map: the capture must be on the original path and draw world or BSP geometry;
  * --ref <dir>: the same-named capture in <dir> is a known-good fixed-camera reference; the fraction of pixels whose
    RGB differs by more than 24 must stay below --max-diff, and the reference's draw counts must not drop by more than
    half (geometry not submitted).

A load count is never accepted as proof: "1,900 meshes loaded" with a 90 % black scene fails.
"""
import argparse, glob, json, os, sys


def load_bmp(path):
    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        return None
    try:
        return np.asarray(Image.open(path).convert('RGB')).astype('int32')
    except OSError:
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inputs', nargs='+')
    ap.add_argument('--ref')
    ap.add_argument('--max-diff', type=float, default=0.25)
    ap.add_argument('--expect-map', action='store_true')
    a = ap.parse_args()
    files = []
    for i in a.inputs:
        files += sorted(glob.glob(os.path.join(i, '*.bmp.json'))) if os.path.isdir(i) else [i]
    if not files:
        print('visual_check: no *.bmp.json captures (run with WFC_VISUALCHECK=1)')
        return 1
    bad = 0
    for f in files:
        d = json.load(open(f, encoding='utf-8'))
        why = [d['reasons']] if d.get('verdict') != 'PASS' and d.get('reasons') else []
        if a.expect_map:
            if d.get('render_path') != 'original':
                why.append('not on the original render path')
            if d.get('world_draws', 0) + d.get('bsp_draws', 0) == 0:
                why.append('no world / BSP draws')
        if a.ref:
            rj = os.path.join(a.ref, os.path.basename(f))
            if os.path.exists(rj):
                r = json.load(open(rj, encoding='utf-8'))
                for k in ('world_draws', 'bsp_draws', 'distinct_materials'):
                    if r.get(k, 0) > 0 and d.get(k, 0) < r[k] * 0.5:
                        why.append('%s %d vs reference %d' % (k, d.get(k, 0), r[k]))
                img, ref = load_bmp(f[:-5]), load_bmp(rj[:-5])
                if img is not None and ref is not None and img.shape == ref.shape:
                    frac = float((abs(img - ref).sum(2) > 24).mean())
                    d['ref_diff'] = frac
                    if frac > a.max_diff:
                        why.append('%.0f%% of pixels differ from the reference' % (frac * 100))
            else:
                d['ref_note'] = 'no reference capture'
        s = d.get('scene', {})
        line = '%-34s %-8s path=%-8s draws=%5d world=%5d bsp=%4d mats=%4d black=%.3f flat=%.3f colours=%4d' % (
            os.path.basename(f)[:-9], 'FAIL' if why else 'PASS', d.get('render_path'), d.get('draws', 0),
            d.get('world_draws', 0), d.get('bsp_draws', 0), d.get('distinct_materials', 0), s.get('black', 0),
            s.get('flat', 0), s.get('colors', 0))
        if 'ref_diff' in d:
            line += ' refdiff=%.3f' % d['ref_diff']
        print(line)
        for w in why:
            print('    - ' + w)
        bad += bool(why)
    print('visual_check: %d / %d captures failed' % (bad, len(files)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
