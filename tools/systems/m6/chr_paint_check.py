"""M6: the robot "metallic paint" exit check (CHR_Transformer_NormSpec_Cust_E; Systems, Experimental's constraints).

Modern for a robot body (m6_audit.py's metallic_paint hook): baseColor = f0 = P, the tinted paint (rule 3's colour
operand) bounded as P = paint / max(1, max channel); metallic = m; the shading's diffuse = P x (1 - m) and
F0 = mix(0.04, P, m); roughness from the material's own SpecularPower; EnergonColor / the emissive untouched.

The image-based light is modelled as the renderer's split sum: env x (F0 x A + B) x occ, with (A, B) Karis' analytic
environment BRDF (EnvBRDFApprox) at the pixel's roughness, averaged over NoV in NOV_SET, env = the cube map's mean colour
(its faces' average, linear) and occ = 1. The B term adds white and desaturates metals.

Compared with Original, per customised pixel (its paint changes with the palette swatch), for all 10 character-select
swatches as Cust A and B, in three lighting modes:
  diffuse-only   Original: DiffuseColor with the cube at 0         Modern: P x (1 - m)
  IBL-only       Original: the cube term (DiffuseColor - above)     Modern: env x (F0 A + B)
  combined       Original: DiffuseColor (cube at its mean)          Modern: the sum
as CIEDE2000 (dE00) on CIE Lab (D65), the median per (material, swatch); the gate is that median <= GATE_DE00.
Reported per m (0.8 is the contract; 0.6 / 0.7 for the per-family sheets): the share of (material, swatch) passing,
the worst and p90 medians, and the median dH* (the hue part).

  chr_paint_check.py --render <work/render root> --out <file.json> [--points 16]
"""
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import glsl_emu as E  # noqa: E402
import m6_audit as A  # noqa: E402

MASTER = 'TR_AllShader_p.Release.CHR_Transformer_NormSpec_Cust_E_Mat'
M_VALUES = [0.6, 0.7, 0.8]
NOV_SET = [0.3, 0.6, 0.9]
GATE_DE00 = 5.0
MODES = ('diffuse', 'ibl', 'combined', 'lit')
# 'lit' (Integration's like-for-like ask): both sides under the same unit directional light, N = +Z, averaged over LIGHTS
# (L, V pairs); the cube at its mean on both sides.
#   Original: DiffuseColor x NdotL + SpecularColor x pow(max(R.V, 0), SpecularPower) x NdotL (UE3 Phong; R = reflect(-L, N))
#   Modern:   P (1 - m) x NdotL + pi x GGX(D G F) / (4 NdotL NdotV) x NdotL (Lambert without 1/pi, as the Original,
#             so the light radiance carries pi for the specular) + env x (F0 A + B) (the split-sum IBL, NoV of the pair)
LIGHTS = [((0.0, 0.0, 1.0), (0.5, 0.0, 0.866)), ((0.5, 0.0, 0.866), (-0.5, 0.0, 0.866)), ((0.707, 0.0, 0.707), (0.0, 0.3, 0.954)),
          ((0.0, 0.6, 0.8), (0.6, 0.0, 0.8)), ((0.3, -0.3, 0.906), (-0.4, 0.2, 0.894))]


def env_brdf(rough, nov):
    """Karis' EnvBRDFApprox: (A, B) for F0 x A + B."""
    c0 = np.array([-1.0, -0.0275, -0.572, 0.022])
    c1 = np.array([1.0, 0.0425, 1.04, -0.04])
    r = rough * c0 + c1
    a004 = min(r[0] * r[0], 2.0 ** (-9.28 * nov)) * r[0] + r[1]
    return -1.04 * a004 + r[2], 1.04 * a004 + r[3]


def _n(v):
    v = np.asarray(v, dtype=np.float64)
    return v / np.linalg.norm(v)


def lit_pair(D, S, SP, P, m, rough, env):
    """(Original, Modern) under LIGHTS (see MODES)."""
    F0 = 0.04 * (1 - m) + P * m
    a = max(rough * rough, 1e-4)
    o_acc, m_acc = np.zeros(3), np.zeros(3)
    for L, V in LIGHTS:
        L, V = _n(L), _n(V)
        nl, nv = max(L[2], 0.0), max(V[2], 1e-4)
        R = 2 * nl * np.array([0, 0, 1.0]) - L
        o_acc += D * nl + S * (max(float(R @ V), 0.0) ** SP) * nl
        H = _n(L + V)
        nh, vh = max(H[2], 0.0), max(float(V @ H), 0.0)
        d = a * a / (math.pi * (nh * nh * (a * a - 1) + 1) ** 2)
        k = a / 2
        g = (nl / (nl * (1 - k) + k)) * (nv / (nv * (1 - k) + k))
        F = F0 + (1 - F0) * (1 - vh) ** 5
        spec = math.pi * d * g * F / max(4 * nl * nv, 1e-4) * nl
        A_, B_ = env_brdf(rough, nv)
        m_acc += P * (1 - m) * nl + spec + env * (F0 * A_ + B_)
    return o_acc / len(LIGHTS), m_acc / len(LIGHTS)


def lab(rgb):
    """Linear sRGB -> CIE Lab (D65), with the scale so that white = L 100."""
    c = np.clip(np.asarray(rgb, dtype=np.float64), 0.0, None)
    M = np.array([[0.4124564, 0.3575761, 0.1804375], [0.2126729, 0.7151522, 0.0721750], [0.0193339, 0.1191920, 0.9503041]])
    xyz = M @ c / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 216 / 24389, np.cbrt(xyz), (24389 / 27 * xyz + 16) / 116)
    return np.array([116 * f[1] - 16, 500 * (f[0] - f[1]), 200 * (f[1] - f[2])])


def de2000(l1, l2):
    """CIEDE2000 colour difference and its hue part dH' / (S_H)."""
    L1, a1, b1 = l1
    L2, a2, b2 = l2
    C1, C2 = math.hypot(a1, b1), math.hypot(a2, b2)
    Cb = (C1 + C2) / 2
    G = 0.5 * (1 - math.sqrt(Cb ** 7 / (Cb ** 7 + 25 ** 7)))
    a1p, a2p = (1 + G) * a1, (1 + G) * a2
    C1p, C2p = math.hypot(a1p, b1), math.hypot(a2p, b2)
    h1p = math.degrees(math.atan2(b1, a1p)) % 360 if C1p else 0.0
    h2p = math.degrees(math.atan2(b2, a2p)) % 360 if C2p else 0.0
    dLp, dCp = L2 - L1, C2p - C1p
    dh = 0.0
    if C1p * C2p:
        dh = h2p - h1p
        if dh > 180:
            dh -= 360
        elif dh < -180:
            dh += 360
    dHp = 2 * math.sqrt(C1p * C2p) * math.sin(math.radians(dh / 2))
    Lbp, Cbp = (L1 + L2) / 2, (C1p + C2p) / 2
    if C1p * C2p == 0:
        hbp = h1p + h2p
    elif abs(h1p - h2p) <= 180:
        hbp = (h1p + h2p) / 2
    else:
        hbp = (h1p + h2p + 360) / 2 if h1p + h2p < 360 else (h1p + h2p - 360) / 2
    T = (1 - 0.17 * math.cos(math.radians(hbp - 30)) + 0.24 * math.cos(math.radians(2 * hbp))
         + 0.32 * math.cos(math.radians(3 * hbp + 6)) - 0.20 * math.cos(math.radians(4 * hbp - 63)))
    dth = 30 * math.exp(-((hbp - 275) / 25) ** 2)
    Rc = 2 * math.sqrt(Cbp ** 7 / (Cbp ** 7 + 25 ** 7))
    Sl = 1 + 0.015 * (Lbp - 50) ** 2 / math.sqrt(20 + (Lbp - 50) ** 2)
    Sc, Sh = 1 + 0.045 * Cbp, 1 + 0.015 * Cbp * T
    Rt = -math.sin(math.radians(2 * dth)) * Rc
    de = math.sqrt((dLp / Sl) ** 2 + (dCp / Sc) ** 2 + (dHp / Sh) ** 2 + Rt * (dCp / Sc) * (dHp / Sh))
    return de, abs(dHp / Sh)


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
    npts = int(sys.argv[sys.argv.index('--points') + 1]) if '--points' in sys.argv else 16
    pal = A.PALETTE
    imgs = A.Images()
    seen = set()
    acc = {m: {k: {} for k in MODES} for m in M_VALUES}               # m -> mode -> (material, swatch) -> [(dE, dH)]
    over1 = [0, 0]
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
            env = np.array(next(iter(cube.values()))[:3]) if cube else np.array([0.5, 0.5, 0.5])
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
                    over1[0] += 1; over1[1] += int(r1['_paint'].max() > 1.0)
                    P = A.paint_bounded(r1['_paint'])
                    rough = float(E.roughness_from_power(float(r1.get('SpecularPower', np.array([15.0]))[0])))
                    ab = [env_brdf(rough, nv) for nv in NOV_SET]
                    o_comb = r1['_diffuse_original'][:3]
                    o_diff = n0['DiffuseColor'][:3]
                    orig = dict(diffuse=o_diff, ibl=np.maximum(o_comb - o_diff, 0.0), combined=o_comb)
                    for m in M_VALUES:
                        F0 = 0.04 * (1 - m) + P * m
                        md = P * (1 - m)
                        mi = env * np.mean([F0 * a + b for a, b in ab], axis=0)
                        mod = dict(diffuse=md, ibl=mi, combined=md + mi)
                        orig['lit'], mod['lit'] = lit_pair(o_comb, np.asarray(r1.get('SpecularColor', np.zeros(3))[:3]),
                                                           float(r1.get('SpecularPower', np.array([15.0]))[0]), P, m, rough, env)
                        for k in MODES:
                            lo, lm = lab(orig[k]), lab(mod[k])
                            lmL = np.array([lo[0], lm[1], lm[2]])          # (Modern at Original's L*: colour without exposure)
                            acc[m][k].setdefault((name, si), []).append(de2000(lo, lm) + (lm[0] - lo[0], de2000(lo, lmL)[0]))
    summary = {}
    for m in M_VALUES:
        sm = {}
        line = []
        for k in MODES:
            med = [float(np.median([x[0] for x in v])) for v in acc[m][k].values()]
            mdh = [float(np.median([x[1] for x in v])) for v in acc[m][k].values()]
            mdl = [float(np.median([x[2] for x in v])) for v in acc[m][k].values()]
            mlm = [float(np.median([x[3] for x in v])) for v in acc[m][k].values()]
            sm[k] = dict(cases=len(med), pass_share=float(np.mean([x <= GATE_DE00 for x in med])) if med else None,
                         median_dE00=float(np.median(med)) if med else None, p90_dE00=float(np.percentile(med, 90)) if med else None,
                         worst_dE00=max(med) if med else None, median_dH=float(np.median(mdh)) if mdh else None,
                         median_dL=float(np.median(mdl)) if mdl else None,
                         Lmatched_pass_share=float(np.mean([x <= GATE_DE00 for x in mlm])) if mlm else None,
                         Lmatched_median_dE00=float(np.median(mlm)) if mlm else None)
            line.append('%s pass %.0f %% (median %.1f, p90 %.1f, worst %.1f, dH %.1f, dL* %+.1f; L*-matched pass %.0f %%, median %.1f)' % (
                k, 100 * (sm[k]['pass_share'] or 0), sm[k]['median_dE00'] or 0, sm[k]['p90_dE00'] or 0, sm[k]['worst_dE00'] or 0,
                sm[k]['median_dH'] or 0, sm[k]['median_dL'] or 0, 100 * (sm[k]['Lmatched_pass_share'] or 0), sm[k]['Lmatched_median_dE00'] or 0))
        summary[str(m)] = sm
        print('m %.1f: dE00 per (material, swatch) median <= %.0f:\n    %s' % (m, GATE_DE00, '\n    '.join(line)))
    # per swatch at the contract m (0.8): the combined pass share
    per_sw = {}
    for (name, si), v in acc[0.8]['combined'].items():
        per_sw.setdefault(si, []).append(float(np.median([x[0] for x in v])))
    for si in sorted(per_sw):
        c = pal[si][:3]
        print('  swatch %d rgb(linear) %.2f %.2f %.2f: combined pass %.0f %%, median dE00 %.1f, worst %.1f' % (
            si, c[0], c[1], c[2], 100 * np.mean([x <= GATE_DE00 for x in per_sw[si]]), np.median(per_sw[si]), max(per_sw[si])))
    print('customised pixels with a paint channel > 1 (bounded): %d of %d (%.1f %%)' % (over1[1], over1[0], 100.0 * over1[1] / max(1, over1[0])))
    json.dump(dict(materials=len(seen), swatches=len(pal), points=npts, nov=NOV_SET, gate_dE00=GATE_DE00, summary=summary,
                   paint_over_1=[over1[1], over1[0]]), open(out, 'w', encoding='utf-8'), indent=1)
    print('materials %d, swatches %d' % (len(seen), len(pal)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
