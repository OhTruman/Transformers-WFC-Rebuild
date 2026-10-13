"""M6: the robot "metallic paint" exit check (CHR_Transformer_NormSpec_Cust_E; Systems, Experimental's constraints).

Modern for a robot body: baseColor = paint (the tinted colour, the colour operand of rule 3's cube x colour product),
metallic = m (0.6 - 0.8), so diffuse = paint x (1 - m) and F0 = mix(0.04, paint, m); roughness from the gloss mask (the
derived SpecularPower); EnergonColor emissive untouched. The check: on the customised pixels (the paint changes with the
palette swatch), for all 10 character-select swatches as the custom colour (Cust A and B both set to the swatch), the hue
and CIE L* of Modern vs Original in three lighting modes:
  diffuse-only   Original: DiffuseColor with the cube at 0        Modern: paint x (1 - m)
  IBL-only       Original: the cube term (DiffuseColor - above)    Modern: F0 x cube mean
  combined       Original: DiffuseColor                            Modern: the sum
with the cube at its map's mean colour (its faces' average, linear). Reported per m: the worst hue difference (degrees,
the median over pixels per material / swatch, max over them) and the median L* difference.

  chr_paint_check.py --render <work/render root> --out <file.json> [--points 48]
"""
import colorsys
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import glsl_emu as E  # noqa: E402
import m6_audit as A  # noqa: E402

LUM = np.array([0.2126, 0.7152, 0.0722])
MASTER = 'TR_AllShader_p.Release.CHR_Transformer_NormSpec_Cust_E_Mat'
M_VALUES = [0.6, 0.7, 0.8]


def hue(rgb):
    r, g, b = np.clip(rgb, 0, None)
    mx = max(r, g, b)
    if mx <= 0:
        return None
    h, s_, _ = colorsys.rgb_to_hsv(r / mx, g / mx, b / mx)
    return h * 360.0 if s_ > 0.15 else None   # (hue is meaningful only for coloured values)


def lstar(rgb):
    y = float(np.clip(rgb, 0, None) @ LUM)
    f = y ** (1.0 / 3.0) if y > 0.008856 else 7.787 * y + 16.0 / 116.0
    return 116.0 * f - 16.0


def hdiff(a, b):
    if a is None or b is None:
        return None
    d = abs(a - b) % 360.0
    return min(d, 360.0 - d)


def cube_mean(t):
    from PIL import Image
    acc, n = np.zeros(3), 0
    for f in t.get('faces') or []:
        try:
            a = np.asarray(Image.open(f).convert('RGB'), dtype=np.float64) / 255.0
            acc += E.srgb_to_linear(a.reshape(-1, 3).mean(axis=0)); n += 1
        except Exception:                                               # noqa: BLE001
            pass
    return (acc / n).tolist() + [1.0] if n else [0.5, 0.5, 0.5, 1.0]


def main():
    root = sys.argv[sys.argv.index('--render') + 1]
    out = sys.argv[sys.argv.index('--out') + 1]
    npts = int(sys.argv[sys.argv.index('--points') + 1]) if '--points' in sys.argv else 48
    pal = A.PALETTE
    imgs = A.Images()
    seen, res = set(), {m: dict(diffuse=[], ibl=[], combined=[], dl=[]) for m in M_VALUES}
    per = []
    for mp in sorted(d for d in os.listdir(root) if d.startswith('MP_')):
        mats = json.load(open(os.path.join(root, mp, 'materials_glsl.json'), encoding='utf-8'))
        for name, mat in mats.items():
            info = mat.get('info', {})
            if name in seen or info.get('master') != MASTER:
                continue
            seen.add(name)
            glsl = mat['glsl']
            texs = info.get('textures', [])
            slot2d = {t['slot']: t for t in texs if t.get('kind', '2d') == '2d' and 'slot' in t}
            images = {s: imgs.get(t) for s, t in slot2d.items()}
            images = {s: a for s, a in images.items() if a is not None}
            fallback = {s: [0.5, 0.5, 0.5, 1.0] for s in slot2d if s not in images}
            cube = {t['slot']: cube_mean(t) for t in texs if t.get('kind') == 'cube' and 'slot' in t}
            rng = np.random.default_rng(A.seed_of(name))
            uvs = [tuple(rng.uniform(0, 1, 2)) for _ in range(npts)]
            for si, sw in enumerate(pal):
                other = pal[(si + 5) % len(pal)]
                for uv in uvs:
                    rts = [{'uRT_Cust_Color_A': c, 'uRT_Cust_COLOR_B': c, 'uRT_EnergonColor': A.ENERGON_DEFAULT} for c in (sw, other)]
                    try:
                        r1 = E.run(glsl, E.Material(fallback, cube, uv0=uv, images=images, strip_view=True, rt_value=rts[0]))
                        r2 = E.run(glsl, E.Material(fallback, cube, uv0=uv, images=images, strip_view=True, rt_value=rts[1]))
                        n0 = E.run(glsl, E.Material(fallback, {s: [0, 0, 0, 1] for s in cube}, uv0=uv, images=images, rt_value=rts[0]))
                    except E.Unsupported:
                        break
                    if '_paint' not in r1 or '_paint' not in r2 or np.abs(r1['_paint'] - r2['_paint']).sum() < 0.05:
                        continue                                       # (not a customised pixel)
                    paint = r1['_paint']
                    o_comb = r1['_diffuse_original'][:3]
                    o_diff = n0['DiffuseColor'][:3]
                    o_ibl = o_comb - o_diff
                    cm = np.array(next(iter(cube.values()))[:3]) if cube else np.array([0.5, 0.5, 0.5])
                    for m in M_VALUES:
                        md = paint * (1 - m)
                        mi = (0.04 * (1 - m) + paint * m) * cm
                        mc = md + mi
                        for key, a, b in (('diffuse', md, o_diff), ('ibl', mi, o_ibl), ('combined', mc, o_comb)):
                            h = hdiff(hue(a), hue(b))
                            if h is not None:
                                res[m][key].append((name, si, h))
                        res[m]['dl'].append((name, si, lstar(mc) - lstar(o_comb)))
    summary = {}
    for m in M_VALUES:
        s = {}
        for key in ('diffuse', 'ibl', 'combined'):
            groups = {}
            for name, si, h in res[m][key]:
                groups.setdefault((name, si), []).append(h)
            med = [float(np.median(v)) for v in groups.values()]
            s[key] = dict(worst_median_hue_deg=max(med) if med else None, p90=float(np.percentile(med, 90)) if med else None, cases=len(med))
        dl = [x[2] for x in res[m]['dl']]
        s['combined_Lstar_diff_median'] = float(np.median(dl)) if dl else None
        s['combined_Lstar_diff_p10_p90'] = [float(np.percentile(dl, 10)), float(np.percentile(dl, 90))] if dl else None
        summary[str(m)] = s
        print('m %.1f: hue (worst median / p90 deg): diffuse %s, IBL %s, combined %s; combined L* diff median %.1f (p10 %.1f, p90 %.1f)' % (
            m, *['%.1f / %.1f' % (s[k]['worst_median_hue_deg'] or 0, s[k]['p90'] or 0) for k in ('diffuse', 'ibl', 'combined')],
            s['combined_Lstar_diff_median'] or 0, *(s['combined_Lstar_diff_p10_p90'] or [0, 0])))
    # per swatch (m = 0.8): the worst median combined hue difference and the swatch's own saturation
    sw = {}
    for name, si, h in res[0.8]['combined']:
        sw.setdefault(si, {}).setdefault(name, []).append(h)
    for si in sorted(sw):
        c = pal[si][:3]
        sat = colorsys.rgb_to_hsv(*(np.array(c) / max(max(c), 1e-9)))[1]
        meds = [float(np.median(v)) for v in sw[si].values()]
        print('  swatch %d rgb(linear) %.2f %.2f %.2f sat %.2f: combined hue worst %.1f, median %.1f deg' % (si, c[0], c[1], c[2], sat, max(meds), float(np.median(meds))))
    json.dump(dict(materials=len(seen), swatches=len(pal), points=npts, summary=summary), open(out, 'w', encoding='utf-8'), indent=1)
    print('materials %d, swatches %d' % (len(seen), len(pal)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
