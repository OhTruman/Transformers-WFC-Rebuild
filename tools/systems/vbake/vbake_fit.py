"""M7 "RT Realistic" R1 - fit the sun's baked share and write V_bake (Systems; docs/handoff/SYSTEMS_RT_REALISTIC_M6_PLAN.md R1).

Reads vbake_trace's output (per lightmap page: coverage, V, N.L, the sun in the texel's tangent frame, component) and the
map's render data (lighting.json lightmap table + the coefficient pages), read-only. Writes into the trace folder:
  vbake_<page>.png   R8: the baked-sun visibility V_bake (0..255), dilated as the tracer wrote it
  fit.json           the sun fit (k, p, basis handedness, residuals, the pair count) + agreement metrics per page
  preview_<page>.png the largest pages: lightmap (flat-normal luminance) | V_bake | estimated indirect (lightmap - sun)

The decode is the original's (lighting.json "note"): L = sum_i dot(N_t, B_i)^2 * tex_i.rgb(sRGB-decoded) * scales[i],
B0 = (0, sqrt(2/3), 1/sqrt3), B1 = (-1/sqrt2, -1/sqrt6, 1/sqrt3), B2 = (1/sqrt2, -1/sqrt6, 1/sqrt3). The sun's share of
coefficient i at a texel is modelled as k * max(B_i . L_t, 0)^p * sunColor (L_t: the sun in the texel's tangent frame).
k and p (and the tangent frame's handedness, which the tracer can only guess from UV0) are fitted on pairs of nearby
texels of one component, one lit and one shadowed by the trace, with similar N.L: their difference is the sun's share
(the indirect light cancels to first order). The result splits each texel: indirect_i = L_i - V_bake * share_i.

  vbake_fit.py --trace <vbake_trace out dir> --render <render map dir> [--k-quantile 0.3 | --max-clamp 0.10]
  --k-quantile Q  k = the Q-quantile of the per-pair estimates D / x (lit minus shadowed coefficient over the model's unit
                  share, pairs with x above 10 % of its median): a robust low k (Rendering 2026-10-13, "a touch strong");
                  p and the handedness stay the least-squares fit's
  --per-page N    also fit k (and p) per lightmap page from that page's own pairs where it has >= N of them (k by the
                  same estimator as the map: the --k-quantile quantile, else least squares; the map's handedness kept);
                  fit.json pages[] gain k / p / pairs / k_source ("page" or "map": too few pairs, the map's k). For maps
                  whose single k is weak (Molten; Rendering 2026-10-13); the runtime may read pages[].k where present.
  --per-page-p    also fit p per page (default: the map p; per-page p overfits on small pages)
  --max-clamp F   lower k (keeping p and the handedness) until the >= 0 clamp of the indirect removes at most F of the sun
                  energy over the covered texels (bisection; Rendering 2026-10-13: the first fits' 11-30 % clamp read "a touch
                  strong" in Realistic). fit.json keeps the least-squares k as k_fit; k is what the runtime uses.
"""
import json
import os
import sys

import numpy as np
from PIL import Image

B = np.array([[0.0, np.sqrt(2.0 / 3.0), 1.0 / np.sqrt(3.0)],
              [-1.0 / np.sqrt(2.0), -1.0 / np.sqrt(6.0), 1.0 / np.sqrt(3.0)],
              [1.0 / np.sqrt(2.0), -1.0 / np.sqrt(6.0), 1.0 / np.sqrt(3.0)]])
LUM = np.array([0.2126, 0.7152, 0.0722])
P_GRID = [0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0]


def srgb_to_linear(c):
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def arg(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def load_page(path):
    with open(path, 'rb') as f:
        w, h = np.frombuffer(f.read(8), dtype=np.int32)
        n = int(w) * int(h)
        cov = np.frombuffer(f.read(n), dtype=np.uint8).reshape(h, w)
        V = np.frombuffer(f.read(n * 4), dtype=np.float32).reshape(h, w)
        ndl = np.frombuffer(f.read(n * 4), dtype=np.float32).reshape(h, w)
        lt = np.frombuffer(f.read(n * 12), dtype=np.float32).reshape(h, w, 3)
        comp = np.frombuffer(f.read(n * 4), dtype=np.int32).reshape(h, w)
    return int(w), int(h), cov, V, ndl, lt, comp


def main():
    tdir, rdir = arg('--trace'), arg('--render')
    if not tdir or not rdir:
        print(__doc__)
        return 2
    tr = json.load(open(os.path.join(tdir, 'vbake_trace.json'), encoding='utf-8'))
    L = json.load(open(os.path.join(rdir, 'lighting.json'), encoding='utf-8'))
    props = L['lightmaps']['props']
    sun = next(l for l in L['lights'] if l['name'] == tr['sun'])
    sun_lin = srgb_to_linear(np.array(sun['color_srgb8']) / 255.0) * float(sun['brightness'])
    sun_lum = float(sun_lin @ LUM)
    comps = tr['components']
    tex_cache = {}

    def tex(name):
        if name not in tex_cache:
            im = Image.open(os.path.join(rdir, 'lightmaps', name + '.png')).convert('RGB')
            tex_cache[name] = srgb_to_linear(np.asarray(im, dtype=np.float64) / 255.0)
        return tex_cache[name]

    pages = []
    for pg in tr['pages']:
        w, h, cov, V, ndl, lt, comp = load_page(os.path.join(tdir, 'page_%s.bin' % pg['page']))
        # the decoded coefficients per texel (linear RGB x the component's scale vectors): Lc[i] (h, w, 3)
        Lc = np.zeros((3, h, w, 3))
        for ci in np.unique(comp[comp >= 0]):
            p = props[comps[ci]]
            m = comp == ci
            for i in range(3):
                t = tex(p['coeffs'][i])
                if t.shape[:2] != (h, w):
                    continue
                Lc[i][m] = t[m] * np.array(p['scales'][i])
        pages.append(dict(name=pg['page'], w=w, h=h, cov=cov, V=V, ndl=ndl, lt=lt, comp=comp, Lc=Lc))

    # ---- pairs: lit (V >= 0.95) next to shadowed (V <= 0.05), one component, similar N.L, both rasterised
    deltas, lts, pidx = [], [], []
    for pi_, P in enumerate(pages):
        cov, V, ndl, comp, Lc, lt = P['cov'], P['V'], P['ndl'], P['comp'], P['Lc'], P['lt']
        h, w = cov.shape
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                if dx == 0 and dy == 0:
                    continue
                ya, yb = slice(max(0, -dy), h - max(0, dy)), slice(max(0, dy), h - max(0, -dy))
                xa, xb = slice(max(0, -dx), w - max(0, dx)), slice(max(0, dx), w - max(0, -dx))
                ok = ((cov[ya, xa] == 1) & (cov[yb, xb] == 1) & (V[ya, xa] >= 0.95) & (V[yb, xb] <= 0.05) &
                      (comp[ya, xa] == comp[yb, xb]) & (np.abs(ndl[ya, xa] - ndl[yb, xb]) < 0.1) & (ndl[ya, xa] > 0.1))
                if not ok.any():
                    continue
                d = Lc[:, ya, xa][:, ok] - Lc[:, yb, xb][:, ok]          # (3 coeff, n, 3 rgb)
                deltas.append(d @ LUM)                                      # (3, n)
                lts.append(lt[ya, xa][ok])                                  # (n, 3)
                pidx.append(np.full(int(ok.sum()), pi_))
    if not deltas:
        print('no lit / shadowed texel pairs: the trace found no sun boundaries on lightmapped surfaces')
        return 1
    D = np.concatenate(deltas, axis=1)
    LT = np.concatenate(lts, axis=0)
    PI = np.concatenate(pidx)
    best = None
    for flip in (1.0, -1.0):
        lt_f = LT * np.array([1.0, flip, 1.0])
        proj = np.clip(lt_f @ B.T, 0.0, None).T                             # (3, n): B_i . L_t
        for p in P_GRID:
            x = (proj ** p) * sun_lum
            keep = np.ones(D.shape, dtype=bool)
            k = 0.0
            for _ in range(3):                                              # trimmed least squares (drop the worst 20 %)
                xs, ds = x[keep], D[keep]
                k = float((xs * ds).sum() / max((xs * xs).sum(), 1e-12))
                r = np.abs(D - k * x)
                keep = r <= np.quantile(r, 0.8)
            res = float(np.median(np.abs(D - k * x)) / max(np.median(np.abs(D)), 1e-9))
            if best is None or res < best['residual']:
                best = dict(k=k, p=p, flip=flip, residual=res)
    k, p, flip = best['k'], best['p'], best['flip']
    print('pairs %d; best fit k %.4f, p %.2f, tangent-frame y %s, median relative residual %.3f' %
          (D.shape[1], k, p, '+' if flip > 0 else '-', best['residual']))
    k_fit = k
    k_quantile = float(arg('--k-quantile')) if arg('--k-quantile') else None
    if k_quantile is not None:
        lt_f = LT * np.array([1.0, flip, 1.0])
        x = ((np.clip(lt_f @ B.T, 0.0, None).T) ** p) * sun_lum                # (3, n): the unit-k model share
        ok = x > 0.1 * np.median(x[x > 0]) if (x > 0).any() else x > 0
        ratios = (D[ok] / x[ok])
        k = float(np.quantile(ratios, k_quantile))
        print('k at the %.0f %% quantile of %d per-pair estimates: %.4f (%.0f %% of the fit %.4f; estimates p10 %.3f, median %.3f, p90 %.3f)' %
              (100 * k_quantile, ratios.size, k, 100 * k / max(k_fit, 1e-12), k_fit, np.quantile(ratios, 0.1), np.median(ratios), np.quantile(ratios, 0.9)))
    max_clamp = float(arg('--max-clamp')) if arg('--max-clamp') else None
    if max_clamp is not None:
        # the clamped fraction at a given k: sum(max(V k s1 - Lc, 0)) / sum(V k s1) over covered texels (s1 = the share at k 1)
        terms = []
        for P in pages:
            m = P['cov'] == 1
            lt_f = P['lt'] * np.array([1.0, flip, 1.0])
            s1 = np.moveaxis((np.clip(lt_f @ B.T, 0.0, None) ** p)[..., None] * sun_lin, 2, 0)   # (3, h, w, 3)
            terms.append(((P['V'][None, :, :, None] * s1)[:, m], P['Lc'][:, m]))
        def clamped(kk):
            neg = sum(float(np.clip(kk * vs - lc, 0, None).sum()) for vs, lc in terms)
            sun = sum(float((kk * vs).sum()) for vs, _ in terms)
            return neg / max(sun, 1e-12)
        if clamped(k) > max_clamp:
            lo, hi = 0.0, k
            for _ in range(30):
                mid = 0.5 * (lo + hi)
                if clamped(mid) > max_clamp: hi = mid
                else: lo = mid
            k = lo
        print('k for a clamp <= %.0f %%: %.4f (%.0f %% of the fit %.4f; clamp at the fit %.1f %%)' %
              (100 * max_clamp, k, 100 * k / max(k_fit, 1e-12), k_fit, 100 * clamped(k_fit)))

    # ---- --per-page N: each page's own k (and p) from its pairs, the map's handedness kept
    per_page_min = int(arg('--per-page')) if arg('--per-page') else None
    page_kp = {}
    if per_page_min:
        lt_all = LT * np.array([1.0, flip, 1.0])
        proj_all = np.clip(lt_all @ B.T, 0.0, None).T                         # (3, n)
        for pi_, P in enumerate(pages):
            m = PI == pi_
            if m.sum() < per_page_min:
                page_kp[P['name']] = dict(k=k, p=p, pairs=int(m.sum()), k_source='map')
                continue
            Dp, prj = D[:, m], proj_all[:, m]
            bestp = None
            for pp in (P_GRID if "--per-page-p" in sys.argv else [p]):   # (p per page only with --per-page-p)
                x = (prj ** pp) * sun_lum
                kk = 0.0; keep = np.ones(Dp.shape, dtype=bool)
                for _ in range(3):
                    xs, ds = x[keep], Dp[keep]
                    kk = float((xs * ds).sum() / max((xs * xs).sum(), 1e-12))
                    r = np.abs(Dp - kk * x); keep = r <= np.quantile(r, 0.8)
                res_ = float(np.median(np.abs(Dp - kk * x)) / max(np.median(np.abs(Dp)), 1e-9))
                if bestp is None or res_ < bestp[2]:
                    bestp = (kk, pp, res_)
            kk, pp, res_ = bestp
            if k_quantile is not None:
                x = (prj ** pp) * sun_lum
                okq = x > 0.1 * np.median(x[x > 0]) if (x > 0).any() else x > 0
                if okq.sum():
                    kk = float(np.quantile(Dp[okq] / x[okq], k_quantile))
            page_kp[P['name']] = dict(k=kk, p=pp, pairs=int(m.sum()), residual=res_, k_source='page')
        ks = [v['k'] for v in page_kp.values() if v['k_source'] == 'page']
        print('per page (>= %d pairs): %d of %d pages fitted; k p10 %.3f median %.3f p90 %.3f (map k %.3f)' %
              (per_page_min, len(ks), len(pages), *(np.percentile(ks, [10, 50, 90]) if ks else (0, 0, 0)), k))

    # ---- per page: V_bake, the split, agreement metrics, previews
    report = dict(map_render_dir=rdir, sun=tr['sun'], sun_linear=list(map(float, sun_lin)), k=k, k_fit=k_fit, k_quantile=k_quantile, max_clamp=max_clamp, p=p, per_page_min_pairs=per_page_min,
                  tangent_y_sign=flip, residual=best['residual'], pairs=int(D.shape[1]), pages=[],
                  basis='HL2 (lighting.json note)', cone_deg=tr['cone_deg'], supersample=tr['supersample'])
    tot = dict(cov=0, neg_energy=0.0, sun_energy=0.0, mm_lit=0, mm_shadow=0, lit=0, shadow=0)
    order = sorted(pages, key=lambda P: -int((P['cov'] == 1).sum()))
    for rank, P in enumerate(order):
        cov, V, comp, Lc, lt = P['cov'], P['V'], P['comp'], P['Lc'], P['lt']
        Image.fromarray(np.clip(np.round(V * 255.0), 0, 255).astype(np.uint8), 'L').save(os.path.join(tdir, 'vbake_%s.png' % P['name']))
        lt_f = lt * np.array([1.0, flip, 1.0])
        kp = page_kp.get(P['name'], dict(k=k, p=p))
        share = (np.clip(lt_f @ B.T, 0.0, None) ** kp['p'])[..., None] * (kp['k'] * sun_lin)   # (h, w, 3 coeff, 3 rgb)
        share = np.moveaxis(share, 2, 0)                                           # (3, h, w, 3)
        ind = Lc - V[None, :, :, None] * share
        m = cov == 1
        neg = float(np.clip(-ind[:, m], 0, None).sum())
        sun_e = float((V[None, :, :, None] * share)[:, m].sum())
        flat = (Lc.sum(axis=0) / 3.0) @ LUM                                        # flat-normal luminance (n = (0,0,1))
        sflat = (share.sum(axis=0) / 3.0) @ LUM
        lit = m & (V >= 0.95)
        shd = m & (V <= 0.05) & (P['ndl'] > 0.1)
        mm_lit = int((lit & (flat < 0.5 * sflat)).sum())                           # the trace says lit, the bake is darker than the sun alone
        mm_sh = 0
        for ci in np.unique(comp[lit]):                                            # the trace says shadowed, the bake as bright as its lit texels
            cl = lit & (comp == ci)
            if cl.sum() < 8:
                continue
            ref = np.median(flat[cl])
            mm_sh += int((shd & (comp == ci) & (flat > 0.8 * ref)).sum())
        pr = dict(page=P['name'], covered=int(m.sum()), lit=int(lit.sum()), shadowed=int(shd.sum()), mismatch_lit=mm_lit,
                  mismatch_shadow=mm_sh, negative_energy_fraction=neg / max(sun_e, 1e-9))
        if P['name'] in page_kp:
            pr.update(page_kp[P['name']])
        report['pages'].append(pr)
        tot['cov'] += pr['covered']; tot['lit'] += pr['lit']; tot['shadow'] += pr['shadowed']
        tot['mm_lit'] += mm_lit; tot['mm_shadow'] += mm_sh; tot['neg_energy'] += neg; tot['sun_energy'] += sun_e
        if rank < 4:                                                               # preview: lightmap | V_bake | indirect, 2x
            def tm(x):
                x = np.clip(x / max(np.percentile(flat[m], 99), 1e-6), 0, 1)
                return (np.sqrt(x) * 255).astype(np.uint8)
            indflat = (np.clip(ind, 0, None).sum(axis=0) / 3.0) @ LUM
            panels = [tm(flat), (V * 255).astype(np.uint8), tm(indflat)]
            for pn in panels:
                pn[~(cov > 0)] = 32
            img = np.concatenate([np.pad(pn, ((0, 0), (0, 4)), constant_values=255) for pn in panels], axis=1)
            Image.fromarray(img, 'L').resize((img.shape[1] * 2, img.shape[0] * 2), Image.NEAREST).save(
                os.path.join(tdir, 'preview_%s.png' % P['name']))
    report['totals'] = dict(covered=tot['cov'], lit=tot['lit'], shadowed=tot['shadow'],
                            mismatch_lit_rate=tot['mm_lit'] / max(tot['lit'], 1),
                            mismatch_shadow_rate=tot['mm_shadow'] / max(tot['shadow'], 1),
                            negative_energy_fraction=tot['neg_energy'] / max(tot['sun_energy'], 1e-9))
    json.dump(report, open(os.path.join(tdir, 'fit.json'), 'w', encoding='utf-8'), indent=1)
    t = report['totals']
    print('texels %d (lit %d, shadowed %d); mismatch: trace lit / bake dark %.1f %%, trace shadowed / bake lit %.1f %%; '
          'clamped (negative) indirect %.1f %% of the sun energy removed' %
          (t['covered'], t['lit'], t['shadowed'], 100 * t['mismatch_lit_rate'], 100 * t['mismatch_shadow_rate'],
           100 * t['negative_energy_fraction']))
    print('wrote', os.path.join(tdir, 'fit.json'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
