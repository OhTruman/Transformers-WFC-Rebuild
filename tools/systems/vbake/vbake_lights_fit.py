"""Rendering's runtime RT lightmap (S1 sky, S2 / S4 local lights): calibrate the sky and the point / spot lights against the
original (Beast) bake, the way vbake_fit.py calibrates the sun. Writes <out>/fit_lights.json (Rendering reads it as
<render data>/<map>/vbake/fit_lights.json beside fit.json).

Inputs (read-only): vbake_trace --lights output (page_<page>.bin, lights_<page>.bin, lights_index.json), the map's sun
fit (fit.json: k, p, tangent_y_sign, sun_linear), its render data (lighting.json + the coefficient pages) and the
bake-lights audit (docs/handoff/SYSTEMS_BAKE_LIGHTS_AUDIT.json: the lights no lightmap lists, excluded).

Decode (as fit.json): coefficient i of a texel, linear RGB = sRGB-decoded page x the component's scale vector; basis
B_i (HL2) in the texel's tangent frame with fit.json's handedness. Model per texel sample, coefficient i, channel c:
  Lc_i,c - V_sun * k * max(B_i . L_sun, 0)^p * sun_c                                     (the sun, fit.json)
    = sky_c * S_i                                                                          (S_i: visible upper sky, below)
    + s_point * sum_{points j} b_j col_j,c F(d_j) V_j max(B_i . L_j, 0)^p
    + s_spot  * sum_{spots j}  b_j col_j,c F(d_j) cone_j^2 V_j max(B_i . L_j, 0)^p
    + bounce_i,c (>= 0: the residual; the S3 bounce stage must reproduce it)
  S_i = (1/pi) * integral over the visible sky (directions above the horizon, +y, unoccluded) of max(B_i . w, 0), so a
  uniform sky of linear radiance sky_c in the decode's units gives sky_c * S_i (S_i = 1/3-ish for a flat open texel: the
  three coefficients of an unoccluded upward texel sum to ~1.6 S units, the same convention as the sun's share).
  b_j = Brightness, col_j = sRGB-decoded LightColor (as the sun), d in metres, cone = saturate((cos_axis - cos O) /
  (cos I - cos O)) (UE3), V_j the traced visibility (static shadows). Falloff forms tried:
    ue3     F = max(0, 1 - (d/R)^2)^FalloffExponent        (the UE3 runtime, RE: WfcDirectLightEnv.cpp)
    invsq   F = 1 / max(d, 0.1)^2 for d < R, else 0         (physically based with Beast's range clamp at the radius)
    invsq_w F = 1 / max(d, 0.1)^2 * (1 - (d/R)^4)^2 (d < R) (a smooth range window)
  Estimator (per form): sky_c = q25 of lightmap / S over texels no local light reaches; each light's scale = q25 of
  (lightmap - sky) / its unit term over texels where it gives >= 80 % of the local light (the lower envelope: the bake's
  bounce only adds); the class scale = the median over its lights; lights more than 2x off their class are listed as
  overrides. The form whose per-light ratio stays flattest (median (q75 - q25) / q50) wins, UE3 kept within 15 %.
  (A global quantile regression was tried first: texels where the trace and the bake disagree on a shadow pull it to
  half the per-light value, so it was dropped.)

  vbake_lights_fit.py --trace <vbake_trace --lights out dir> --sunfit <fit.json> --render <render map dir>
                      --audit <SYSTEMS_BAKE_LIGHTS_AUDIT.json> --out <dir>
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
REMNANT_TOGGLEABLE = {'name': 'PointLightToggleable_1918', 'component': 'PointLightComponent_1792', 'class': 'PointLightComponent',
                      'level': 'MP_ESC_Remnant_ART_m', 'brightness': 1.0, 'radius_m': 40.96, 'falloff_exponent': 2.0,
                      'color_srgb8': [255, 255, 255], 'dynamic': True,
                      'note': 'placed PointLightToggleable (class defaults; not baked, not in lighting.json): a dynamic light in the original'}


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


SAMPLE = np.dtype([('x', '<i4'), ('y', '<i4'), ('sky', '<f4', 6), ('start', '<u4'), ('count', '<u4')])
REC = np.dtype([('light', '<i4'), ('V', '<f4'), ('d', '<f4'), ('lt', '<f4', 3), ('cosAxis', '<f4')])


def load_lights(path):
    b = open(path, 'rb').read()
    w, h, stride, ns = np.frombuffer(b, '<i4', 4)
    o = 16
    S = np.frombuffer(b, SAMPLE, int(ns), o); o += int(ns) * SAMPLE.itemsize
    nr = int(np.frombuffer(b, '<i4', 1, o)[0]); o += 4
    R = np.frombuffer(b, REC, nr, o)
    return S, R


def main():
    tdir, rdir, out = arg('--trace'), arg('--render'), arg('--out')
    sf = json.load(open(arg('--sunfit'), encoding='utf-8'))
    L = json.load(open(os.path.join(rdir, 'lighting.json'), encoding='utf-8'))
    mapname = L.get('map') or os.path.basename(os.path.normpath(rdir))
    audit = next((a for a in json.load(open(arg('--audit'), encoding='utf-8')) if a['map'] == mapname), None)
    excluded = {x['name']: x for x in (audit['lighting_json_not_in_any_lightmap'] if audit else [])}
    props = L['lightmaps']['props']
    lights = {l['name']: l for l in L['lights']}
    tr = json.load(open(os.path.join(tdir, 'vbake_trace.json'), encoding='utf-8'))
    lidx = json.load(open(os.path.join(tdir, 'lights_index.json'), encoding='utf-8'))
    names = [x['name'] for x in lidx['lights']]
    k, p, flip = float(sf['k']), float(sf['p']), float(sf['tangent_y_sign'])
    page_k = {pg['page']: float(pg.get('k', k)) for pg in sf.get('pages', [])}   # (per-page sun k where fit.json has one)
    sun_lin = np.array(sf['sun_linear'])
    comps = tr['components']
    comp_bsp = np.array(['#' in c for c in comps] + [False])               # BSP elements are keyed '<ModelComponent>#<k>'
    # per light: class (0 point, 1 spot), b * col (linear), R, exponent, cos inner / outer, excluded
    nL = len(names)
    cls = np.zeros(nL, int); bcol = np.zeros((nL, 3)); Rad = np.ones(nL); Exp = np.full(nL, 2.0)
    cosI = np.ones(nL); cosO = np.zeros(nL); excl = np.zeros(nL, bool); ch_bsp = np.ones(nL, bool); ch_static = np.ones(nL, bool)
    for j, n in enumerate(names):
        l = lights[n]
        cls[j] = 1 if l['class'] == 'SpotLightComponent' else 0
        bcol[j] = srgb_to_linear(np.array(l['color_srgb8']) / 255.0) * float(l['brightness'] or 0.0)
        Rad[j] = max(float(l['radius_m'] or 0.0), 1e-3); Exp[j] = float(l['falloff_exponent'] or 2.0)
        if cls[j]:
            cosI[j] = np.cos(np.radians(float(l.get('inner_cone_deg') or 0.0)))
            cosO[j] = np.cos(np.radians(float(l.get('outer_cone_deg') or 44.0)))
        excl[j] = n in excluded or not l.get('built_into_lightmap', False)
        ch = l.get('channels') or {}
        ch_bsp[j] = ch.get('BSP', True); ch_static[j] = ch.get('Static', True)
    tex_cache = {}

    def tex(name):
        if name not in tex_cache:
            im = Image.open(os.path.join(rdir, 'lightmaps', name + '.png')).convert('RGB')
            tex_cache[name] = srgb_to_linear(np.asarray(im, dtype=np.float64) / 255.0)
        return tex_cache[name]

    # ---- gather per sample: Y (3 coeff x 3 rgb) = lightmap - sun share, sky S_i, and per light record the unit terms
    bsp_s = []
    LMs, SUNs, PGs, page_names = [], [], [], []
    Ys, Ss, recs_s, recs_l, recs_V, recs_d, recs_proj, recs_cos = [], [], [], [], [], [], [], []
    nsamp = 0
    for pg in tr['pages']:
        name = pg['page']
        lp = os.path.join(tdir, 'lights_%s.bin' % name)
        if not os.path.exists(lp):
            continue
        w, h, cov, V, ndl, lt, comp = load_page(os.path.join(tdir, 'page_%s.bin' % name))
        S, R = load_lights(lp)
        if len(S) == 0:
            continue
        xs, ys = S['x'], S['y']
        cs = comp[ys, xs]
        Lc = np.zeros((len(S), 3, 3))
        for ci in np.unique(cs[cs >= 0]):
            pr = props[comps[ci]]
            m = cs == ci
            for i in range(3):
                t = tex(pr['coeffs'][i])
                if t.shape[:2] != (h, w):
                    continue
                Lc[m, i] = t[ys[m], xs[m]] * np.array(pr['scales'][i])
        lt_s = lt[ys, xs] * np.array([1.0, flip, 1.0])
        sun_share = (np.clip(lt_s @ B.T, 0.0, None) ** p)[:, :, None] * (page_k.get(name, k) * sun_lin)[None, None, :] * V[ys, xs][:, None, None]
        Ys.append(Lc - sun_share)
        LMs.append(Lc); SUNs.append(sun_share); PGs.append(np.full(len(S), len(page_names))); page_names.append(name)
        Ss.append(S['sky'][:, 3:6] if flip < 0 else S['sky'][:, 0:3])
        bsp_s.append(comp_bsp[np.where(cs >= 0, cs, len(comps))])
        cnt = S['count'].astype(np.int64); st = S['start'].astype(np.int64)
        sid = np.repeat(np.arange(len(S)) + nsamp, cnt)
        ridx = np.concatenate([np.arange(a, a + c) for a, c in zip(st, cnt)]) if cnt.sum() else np.zeros(0, np.int64)
        rr = R[ridx]
        recs_s.append(sid); recs_l.append(rr['light']); recs_V.append(rr['V']); recs_d.append(rr['d'])
        recs_proj.append(np.clip((rr['lt'] * np.array([1.0, flip, 1.0])) @ B.T, 0.0, None))
        recs_cos.append(rr['cosAxis'])
        nsamp += len(S)
    Y = np.concatenate(Ys)                       # (n, 3 coeff, 3 rgb)
    S = np.concatenate(Ss)                       # (n, 3)
    rs = np.concatenate(recs_s); rl = np.concatenate(recs_l); rV = np.concatenate(recs_V).astype(np.float64)
    rd = np.concatenate(recs_d).astype(np.float64); rproj = np.concatenate(recs_proj); rcos = np.concatenate(recs_cos).astype(np.float64)
    recv_bsp = np.concatenate(bsp_s)
    keep = ~excl[rl] & np.where(recv_bsp[rs], ch_bsp[rl], ch_static[rl])   # (lighting channels)
    rs, rl, rV, rd, rproj, rcos = rs[keep], rl[keep], rV[keep], rd[keep], rproj[keep], rcos[keep]
    cone = np.where(cls[rl] == 1, np.clip((rcos - cosO[rl]) / np.maximum(cosI[rl] - cosO[rl], 1e-6), 0.0, 1.0) ** 2, 1.0)
    within = rd < Rad[rl]
    forms = {
        'ue3': np.clip(1.0 - (rd / Rad[rl]) ** 2, 0.0, None) ** Exp[rl],
        'invsq': np.where(within, 1.0 / np.maximum(rd, 0.1) ** 2, 0.0),
        'invsq_w': np.where(within, 1.0 / np.maximum(rd, 0.1) ** 2 * np.clip(1.0 - (rd / Rad[rl]) ** 4, 0.0, None) ** 2, 0.0),
        'ue3_e1': np.clip(1.0 - (rd / Rad[rl]) ** 2, 0.0, None),
        'linear': np.clip(1.0 - rd / Rad[rl], 0.0, None),
        'const': within.astype(np.float64),
    }
    n = len(Y)
    lum_y = (Y.sum(axis=1) / 3.0) @ LUM                                  # flat-normal luminance per sample (sun removed)
    Sflat = S.mean(axis=1)                                               # the sky integral at the flat normal
    yflat = Y.sum(axis=1) / 3.0                                          # (n, 3 rgb)
    sunlit = np.zeros(n, bool)
    sunlit[:] = False
    results = {}
    for fname, F in forms.items():
        t = (F * cone * rV)[:, None, None] * (rproj ** p)[:, :, None] * bcol[rl][:, None, :]   # per record, unit scale
        tl = (t.sum(axis=1) / 3.0) @ LUM
        loc_unit = np.zeros(n); np.add.at(loc_unit, rs, tl)
        # sky first, on samples no local light reaches (unit local < 2 % of the lightmap): lower envelope (q25) of
        # lightmap / sky integral per channel over samples that see a fair part of the sky
        skyset = (loc_unit < 0.02 * np.maximum(lum_y, 1e-6)) & (Sflat > 0.1)
        sky = np.array([float(np.quantile(yflat[skyset, c] / Sflat[skyset], 0.25)) for c in range(3)]) if skyset.sum() >= 50 else np.zeros(3)
        sky_l = Sflat * float(sky @ LUM)
        # each light alone: records carrying >= 80 % of the sample's local unit sum; ratio = (lightmap - sky) / unit term
        dom = (tl >= 0.8 * np.maximum(loc_unit[rs], 1e-12)) & (tl > 1e-6)
        ratio = (lum_y[rs] - sky_l[rs]) / np.maximum(tl, 1e-12)
        per_light = {}
        for j in np.unique(rl[dom]):
            m = dom & (rl == j)
            if m.sum() < 30:
                continue
            q25, q50, q75 = np.quantile(ratio[m], [0.25, 0.5, 0.75])
            per_light[int(j)] = dict(q25=float(q25), q50=float(q50), spread=float((q75 - q25) / max(abs(q50), 1e-9)), n=int(m.sum()))
        b = np.zeros(5); b[:3] = sky
        spreads = [v['spread'] for v in per_light.values()]
        for ci in (0, 1):
            v = [per_light[j]['q25'] for j in per_light if cls[j] == ci and per_light[j]['spread'] < 1.5 and per_light[j]['q25'] > 0]
            b[3 + ci] = float(np.median(v)) if v else float('nan')
        for ci in (0, 1):                                                # no estimate for a class: the other class's scale
            if np.isnan(b[3 + ci]): b[3 + ci] = b[4 - ci] if not np.isnan(b[4 - ci]) else 1.0
        Tp = np.zeros((n, 3, 3)); Ts = np.zeros((n, 3, 3))
        np.add.at(Tp, rs[cls[rl] == 0], t[cls[rl] == 0]); np.add.at(Ts, rs[cls[rl] == 1], t[cls[rl] == 1])
        model = S[:, :, None] * sky[None, None, :] + Tp * b[3] + Ts * b[4]
        res = Y - model
        lum_m = (model.sum(axis=1) / 3.0) @ LUM
        local = ((Tp * b[3] + Ts * b[4]).sum(axis=1) / 3.0) @ LUM
        reach = loc_unit > 0
        results[fname] = dict(b=b, Tp=Tp, Ts=Ts, res=res, per_light=per_light, t=t, tl=tl,
                              med_rel=float(np.median(spreads)) if spreads else float('inf'), n_dom=len(per_light),
                              share=dict(sky=float(sky_l.mean() / max(lum_y.mean(), 1e-12)), local=float(local.mean() / max(lum_y.mean(), 1e-12)),
                                         local_on_lit_set=float(local[reach].mean() / max(lum_y[reach].mean(), 1e-12)) if reach.any() else 0.0,
                                         ambient=0.0),
                              sky_samples=int(skyset.sum()))
        print('%-8s sky (%.4f %.4f %.4f) on %d samples  s_point %.4f  s_spot %.4f  | lights with a ratio %d, median ratio spread '
              '(q75 - q25) / q50 %.3f | mean share of the lightmap: sky %.1f %%, local %.1f %% (%.1f %% where lights reach)' %
              (fname, sky[0], sky[1], sky[2], int(skyset.sum()), b[3], b[4], len(per_light), results[fname]['med_rel'],
               100 * results[fname]['share']['sky'], 100 * results[fname]['share']['local'], 100 * results[fname]['share']['local_on_lit_set']))
    best = min(results, key=lambda f: results[f]['med_rel'])
    # the UE3 runtime's own falloff unless another form is clearly flatter (> 15 %): on Streets / Molten / Rust its scale is
    # the same on every map (0.85 - 0.93), linear's is not, and the ratio spreads are within 15 %
    if best != 'ue3' and results['ue3']['med_rel'] <= 1.15 * results[best]['med_rel']:
        best = 'ue3'
    R_ = results[best]
    bfit, res, Tp, Ts = R_['b'], R_['res'], R_['Tp'], R_['Ts']
    overrides = {}
    unreliable = {}
    all_ratios = []
    for j, v in R_['per_light'].items():
        sc = bfit[4] if cls[j] == 1 else bfit[3]
        all_ratios.append((names[j], int(cls[j]), v['q25'], v['n']))
        if sc > 0 and (v['q25'] / sc < 0.5 or v['q25'] / sc > 2.0):
            if v['q25'] > 0 and (v['spread'] < 1.5 or v['n'] >= 1000):   # (a stable ratio, or seen on >= 1000 texels; the rest: unreliable)
                overrides[names[j]] = dict(scale=v['q25'], ratio_to_class=v['q25'] / sc, n_texels=v['n'], spread=v['spread'])
            else:
                unreliable[names[j]] = dict(q25=v['q25'], spread=v['spread'], n_texels=v['n'])
    for cl_, nm_ in ((0, 'point'), (1, 'spot')):
        v = np.array([r[2] for r in all_ratios if r[1] == cl_])
        if len(v):
            print('per-light scale q25 (%s, %d lights with >= 30 samples where they give >= 80 %% of the local light): p10 %.3f  p25 %.3f  '
                  'median %.3f  p75 %.3f  p90 %.3f' % (nm_, len(v), *np.percentile(v, [10, 25, 50, 75, 90])))
    # sky / bounce summaries
    open_sky = S.sum(axis=1) > 0.5 * np.percentile(S.sum(axis=1), 95)
    corr = np.zeros((n, 3, 3))
    if overrides:
        tt = R_['t']
        fix = np.array([overrides[names[j]]['scale'] / (bfit[4] if cls[j] == 1 else bfit[3]) - 1.0 if names[j] in overrides else 0.0 for j in range(nL)])
        np.add.at(corr, rs, tt * (fix[rl] * np.where(cls[rl] == 1, bfit[4], bfit[3]))[:, None, None])
        res = res - corr
    # --page-report N: per lightmap page (the N with the most samples, plus the map), the flat-normal mean (sum of the three
    # coefficients / 3, luminance) of the bake vs the direct model (sun at the page's k, sky, lamps with overrides)
    if arg('--page-report'):
        LM = np.concatenate(LMs); SUN = np.concatenate(SUNs); PG = np.concatenate(PGs)
        fl = lambda a: (a.sum(axis=1) / 3.0) @ LUM
        lm_l, sun_l = fl(LM), fl(SUN)
        sky_l = fl(S[:, :, None] * bfit[None, None, :3]); lamp_l = fl(Tp * bfit[3] + Ts * bfit[4] + corr)
        order = sorted(range(len(page_names)), key=lambda i: -int((PG == i).sum()))[:int(arg('--page-report'))]
        print('page                           samples   bake    sun     sky     lamps   direct  direct/bake')
        for i in order + [None]:
            m = (PG == i) if i is not None else np.ones(len(PG), bool)
            d = sun_l[m].mean() + sky_l[m].mean() + lamp_l[m].mean()
            print('%-30s %7d  %.4f  %.4f  %.4f  %.4f  %.4f  %.3f' % (page_names[i] if i is not None else 'ALL (map)', int(m.sum()), lm_l[m].mean(),
                  sun_l[m].mean(), sky_l[m].mean(), lamp_l[m].mean(), d, d / max(lm_l[m].mean(), 1e-12)))
    bounce = np.clip(res, 0.0, None)
    bounce_mean = (bounce.sum(axis=1) / 3.0).mean(axis=0)
    total_mean = (Y.sum(axis=1) / 3.0).mean(axis=0)
    neg = float(np.clip(-res, 0, None).sum() / max(np.abs(Y).sum(), 1e-12))
    exclude = [dict(name=x['name'], **{kk: x[kk] for kk in ('class', 'built_into_lightmap', 'enabled')},
                    reason=('disabled' if not x['enabled'] else 'never built into a lightmap' if not x['built_into_lightmap']
                            else 'built but in no lightmap (no receiver)'))
               for x in excluded.values()]
    for nmx, l in lights.items():
        if nmx not in excluded and not l.get('built_into_lightmap', False):
            exclude.append(dict(name=nmx, **{kk: l.get(kk) for kk in ('class', 'built_into_lightmap', 'enabled')}, reason='never built into a lightmap'))
    rep = {
        'map': mapname, 'generated_by': 'tools/systems/vbake/vbake_lights_fit.py', 'samples': int(n),
        'trace': dict(stride=lidx['stride'], sky_rays=lidx['sky_rays'], light_range=lidx['light_range']),
        'conventions': 'lightmap decode as fit.json (sRGB-decoded page x the component scale vector, HL2 basis, tangent y sign '
                       'from fit.json); distances in metres; light colour = sRGB-decoded LightColor x Brightness (as sun_linear); '
                       'a coefficient share = scale x b x col x F(d) x cone^2 (spots) x V x max(B_i . L_t, 0)^p (p from fit.json)',
        'sun': dict(k=k, p=p, tangent_y_sign=flip, from_fit_json=True),
        'sky': dict(color_linear=[float(x) for x in bfit[:3]],
                    definition='coefficient i share = color_linear x S_i, S_i = (1/pi) integral over the visible sky (above the '
                               'horizon, unoccluded) of max(B_i . w, 0) dw',
                    open_sky_samples=int(open_sky.sum())),
        'point': dict(scale=float(bfit[3]), falloff=best, exp='per light FalloffExponent' if best == 'ue3' else None,
                      range_clamp='radius' if best != 'ue3' else None, fit_q=float(R_['med_rel']), n_texels=int((Tp.any(axis=(1, 2))).sum())),
        'spot': dict(scale=float(bfit[4]), falloff=best, exp='per light FalloffExponent' if best == 'ue3' else None,
                     range_clamp='radius' if best != 'ue3' else None, cone='UE3: saturate((cos - cos O) / (cos I - cos O))^2',
                     fit_q=float(R_['med_rel']), n_texels=int((Ts.any(axis=(1, 2))).sum())),
        'falloff_forms': {f: dict(sky=[float(x) for x in results[f]['b'][:3]], s_point=float(results[f]['b'][3]),
                                  s_spot=float(results[f]['b'][4]), median_ratio_spread=results[f]['med_rel'],
                                  lights_with_ratio=results[f]['n_dom'], sky_samples=results[f]['sky_samples'], mean_share=results[f]['share']) for f in forms},
        'fit_q_definition': 'median over lights of the spread (q75 - q25) / q50 of the per-texel ratio (lightmap - sky) / unit '
                            'term on texels where that light gives >= 80 % of the local light: a falloff that matches the bake keeps '
                            'the ratio flat over distance and angle; lower is better',
        'estimator': 'scale = median over lights of each light\'s q25 ratio (the lower envelope: the bake\'s bounce only adds); '
                     'sky = q25 of lightmap / sky integral per channel on texels no local light reaches (< 2 %) with S > 0.1',
        'overrides': overrides,
        'overrides_unreliable': unreliable,
        'channels': 'records dropped where the light\'s lighting channel excludes the receiver (BSP channel for BSP elements, '
                    'Static for static meshes), as UE3 lights only reach overlapping channels',
        'bounce_residual_mean': [float(x) for x in bounce_mean],
        'bounce_residual_fraction': float((bounce_mean @ LUM) / max(total_mean @ LUM, 1e-12)),
        'model_over_bake_fraction': neg,
        'exclude': exclude,
        'extra': [REMNANT_TOGGLEABLE] if mapname == 'MP_ESC_Remnant' else [],
    }
    os.makedirs(out, exist_ok=True)
    json.dump(rep, open(os.path.join(out, 'fit_lights.json'), 'w', encoding='utf-8'), indent=1)
    print('best falloff %s; sky linear (%.4f %.4f %.4f); s_point %.4f, s_spot %.4f; overrides %d; bounce residual %.1f %% of the '
          'lightmap (flat luminance); model above the bake %.1f %%; wrote %s' %
          (best, bfit[0], bfit[1], bfit[2], bfit[3], bfit[4], len(overrides), 100 * rep['bounce_residual_fraction'], 100 * neg,
           os.path.join(out, 'fit_lights.json')))
    return 0


if __name__ == '__main__':
    sys.exit(main())
