# M5 item 2: verify the temporal inputs dumped by WFC_TEMPORALDUMP=<dir>,<frame>,<count>.
#  * velocity sign / scale: warp frame N-1's colour to frame N with N's velocity (prevUV = uv + vel, bilinear) and compare
#    with frame N on pixels whose source is on screen; the same with zero, negated and doubled velocity. A correct field
#    beats all three by a clear margin (the residual is jitter / shading change / disocclusion).
#  * velocity vs geometry: velocity recomputed from N's depth and the dumped matrices (static-world reprojection) must
#    match the dumped field outside moving objects (where it differs = object motion, reported as coverage).
#  * jitter: Halton(2,3) - 0.5 in the dumped index; the jittered VP minus the unjittered VP is the clip offset 2 j / size.
#  * reactive: coverage (> 0) and mean.
#  * cut frames: camera_cut true => velocity all zero.
#     python temporal_check.py <dump dir>   (lockstep harness: Rendering work/temporal/lockt.cmd)
import sys, os, json, glob
import numpy as np

D = sys.argv[1]
frames = sorted(int(os.path.basename(p)[1:-5]) for p in glob.glob(os.path.join(D, 'f*.json')))
def load(n):
    m = json.load(open(os.path.join(D, 'f%d.json' % n)))
    w, h = m['w'], m['h']
    rd = lambda k, c, dt: np.fromfile(os.path.join(D, 'f%d_%s.bin' % (n, k)), dtype=dt).reshape(h, w, c) if os.path.exists(os.path.join(D, 'f%d_%s.bin' % (n, k))) else None
    m['color'] = rd('color', 4, np.float32)[..., :3]
    m['vel'] = rd('vel', 2, np.float32)
    m['depth'] = rd('depth', 1, np.float32)[..., 0]
    m['reactive'] = rd('reactive', 1, np.uint8)
    return m

def bilinear(img, x, y):
    h, w = img.shape[:2]
    x0 = np.clip(np.floor(x).astype(int), 0, w - 2); y0 = np.clip(np.floor(y).astype(int), 0, h - 2)
    fx = np.clip(x - x0, 0, 1)[..., None]; fy = np.clip(y - y0, 0, 1)[..., None]
    a = img[y0, x0]; b = img[y0, x0 + 1]; c = img[y0 + 1, x0]; d = img[y0 + 1, x0 + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy

def M(v): return np.array(v, dtype=np.float64).reshape(4, 4).T   # column-major -> row-major

def halton(i, b):
    f, r = 1.0, 0.0
    while i > 0: f /= b; r += f * (i % b); i //= b
    return r

ok = True
def check(cond, msg):
    global ok
    print(('PASS ' if cond else 'FAIL ') + msg); ok &= bool(cond)

prev = None
for n in frames:
    m = load(n); w, h = m['w'], m['h']
    print('frame %d  %dx%d  temporal=%s  cut=%s  have_prev_vp=%s  jitter=(%.4f, %.4f) idx %d' % (
        n, w, h, m['temporal'], m['camera_cut'], m['have_prev_vp'], m['jitter_px'][0], m['jitter_px'][1], m['jitter_index']))
    vel = m['vel'].astype(np.float64)
    # jitter
    if m['temporal']:
        j = m['jitter_index']
        check(abs(m['jitter_px'][0] - (halton(j, 2) - 0.5)) < 1e-6 and abs(m['jitter_px'][1] - (halton(j, 3) - 0.5)) < 1e-6,
              '  jitter = Halton(2,3)[%d] - 0.5' % j)
        dvp = M(m['view_proj']) - M(m['view_proj_nojitter'])
        # proj.m[8] / m[9] (column 2, rows 0 / 1) carry the offset: VP = P V, so the difference is a row-0/1 multiple of V's row 2
        Vz = None
        # offset in NDC per unit clip w: compare on a point
        p = np.array([1.0, 2.0, -30.0, 1.0])
        cj = M(m['view_proj']) @ p; cn = M(m['view_proj_nojitter']) @ p
        off = (cj[:2] / cj[3] - cn[:2] / cn[3]) * np.array([w, h]) / 2.0
        check(np.allclose(off, m['jitter_px'], atol=1e-3), '  jittered VP moves the image by the reported jitter (+x right, +y up, render px): measured (%.4f, %.4f)' % tuple(off))
    else:
        check(m['jitter_px'] == [0, 0], '  no jitter without temporal')
    if m['camera_cut']:
        check(np.abs(vel).max() == 0.0, '  cut frame: velocity all zero')
    # velocity from depth + matrices (static world)
    if m['have_prev_vp']:
        ys, xs = np.mgrid[0:h, 0:w]
        uv = np.stack([(xs + 0.5) / w, (ys + 0.5) / h], -1)
        ndc = np.concatenate([uv * 2 - 1, m['depth'][..., None] * 2 - 1, np.ones((h, w, 1))], -1)
        inv = np.linalg.inv(M(m['view_proj']))
        wp = ndc @ inv.T; wp /= wp[..., 3:4]
        c = wp @ M(m['view_proj_nojitter']).T; pp = wp @ M(m['prev_view_proj']).T
        v_geo = (pp[..., :2] / pp[..., 3:4] * 0.5 + 0.5) - (c[..., :2] / c[..., 3:4] * 0.5 + 0.5)
        sky = m['depth'] >= 1.0
        diff = np.abs(v_geo - vel).max(-1) * w
        # RG16F: ~11 mantissa bits, so a tolerance relative to the motion (0.2 %) plus 0.05 px
        stat = (~sky) & (diff < 0.05 + 0.002 * np.abs(v_geo).max(-1) * w)
        check(stat.mean() > 0.5, '  static-world velocity = depth reprojection on %.1f %% of pixels (rest: object motion %.1f %%, sky %.1f %%; max px err on matched %.4f)' % (
            100 * stat.mean(), 100 * ((~sky) & ~stat).mean(), 100 * sky.mean(), diff[stat].max() if stat.any() else -1))
        print('  velocity px: mean |v| %.3f  p99 %.3f  max %.3f' % (np.abs(vel * [w, h]).mean(), np.percentile(np.abs(vel * [w, h]), 99), np.abs(vel * [w, h]).max()))
    if m['reactive'] is not None:
        r = m['reactive'][..., 0]
        print('  reactive: coverage %.2f %%, mean %.3f, max %d' % (100 * (r > 0).mean(), r.mean() / 255, r.max()))
    # warp test
    if prev is not None and prev['frame'] == n - 1 and m['have_prev_vp']:
        ys, xs = np.mgrid[0:h, 0:w].astype(np.float64)
        cur = m['color']
        def err(scale):
            sx = xs + 0.5 + vel[..., 0] * w * scale - 0.5; sy = ys + 0.5 + vel[..., 1] * h * scale - 0.5
            inside = (sx >= 0) & (sx <= w - 1) & (sy >= 0) & (sy <= h - 1)
            war = bilinear(np.nan_to_num(prev['color'], nan=0.0, posinf=4.0), sx, sy)
            e = np.abs(np.clip(war, 0, 4) - np.clip(np.nan_to_num(cur, nan=0.0, posinf=4.0), 0, 4)).mean(-1)
            return e, inside
        moving = np.abs(vel * [w, h]).max(-1) > 0.25
        e1, in1 = err(1.0); e0, _ = err(0.0); en, _ = err(-1.0); e2, _ = err(2.0)
        sel = in1 & moving & (m['depth'] < 1.0)
        if sel.sum() < 1000:
            print('  warp test: too few moving pixels (%d): camera nearly still' % sel.sum())
        else:
            r = {k: float(v[sel].mean()) for k, v in (('v', e1), ('0', e0), ('-v', en), ('2v', e2))}
            print('  warp error on %d moving pixels: v %.4f | zero %.4f | -v %.4f | 2v %.4f' % (sel.sum(), r['v'], r['0'], r['-v'], r['2v']))
            check(r['v'] < 0.8 * min(r['0'], r['-v'], r['2v']), '  velocity sign / scale: v beats zero, -v and 2v')
    prev = m
print('ALL PASS' if ok else 'SOME FAIL')
