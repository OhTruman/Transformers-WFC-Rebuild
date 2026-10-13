"""M6 Modern materials - the classification audit + the "pbr" mapping table (Systems; docs/handoff/SYSTEMS_RT_REALISTIC_M6_PLAN.md
section 4 / 4c). Reads every map's materials_glsl.json (matc's output: the original graphs as GLSL) read-only, and writes into
--out:
  m6_mapping.json     one row per material (per map): class, the PBR mapping (base colour / F0 / roughness / metal), the
                      reflection-in-diffuse split, the texture that sources roughness (the hdPbrTexture parent), tint flags
  m6_worklist.json    AssetTools' work list: unique parent textures needing a roughness map (BC4), and the ORM candidates
  m6_vectors.json     test vectors for matc's "pbr" mode: sample tables + the expected mapped outputs (the oracle)
  m6_summary.md       counts per family / class, emulation coverage, the identity checks

The mapping (reference; matc's "pbr" mode must reproduce it, checked with m6_vectors.json):
  roughness  = clamp(sqrt(2 / (SpecularPower + 2)), 0.02, 1)  - per pixel, from the material's own SpecularPower expression;
               a generated roughness map (hd_pbr_index role "roughness") replaces it where present
  metallic   = 0 (dielectric) unless the material's class is a reviewed metal row (PROVISIONAL candidates are listed, not
               applied); customisation-tint consumers are forced dielectric (Experimental's gate 1)
  F0         = 0.04 for dielectrics; for metals the SpecularColor
  baseColor  = DiffuseColor with the cube-map reflection removed when the original adds it into DiffuseColor (REFLECTION_ON
               masters: image-based lighting supplies the specular in Modern; keeping it would count it twice); metals add
               the SpecularColor to it
  emissive / normal / opacity: unchanged

Identity checks (numeric, via glsl_emu): SpecularPower -> roughness -> SpecularPower round trip; baseColor == DiffuseColor
where nothing is stripped; baseColor + reflection == DiffuseColor where it is (the split is exact).

  m6_audit.py --render <work/render root> --out <dir> [--samples 6]
"""
import colorsys
import json
import os
import re
import sys
from collections import Counter, defaultdict

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import glsl_emu as E  # noqa: E402

LUM = np.array([0.2126, 0.7152, 0.0722])
TINT_RE = re.compile(r'(?i)cust_?colou?r|energon|faction|uRT_Cust|teamcolor|team_color')
METAL_NAME_RE = re.compile(r'(?i)metal|chrome|steel|iron|gold|copper|brass|alumin|bronze|tin_')
EXCLUDED_BLEND = {'BLEND_Translucent', 'BLEND_Additive', 'BLEND_Modulate', 'BLEND_SoftMasked'}


def arg(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def family(name):
    pkg = name.split('.')[0]
    return pkg.split('_')[0].upper() if '_' in pkg else pkg.upper()


def sat(rgb):
    r, g, b = [float(np.clip(x, 0, 1)) for x in rgb[:3]]
    return colorsys.rgb_to_hsv(r, g, b)[1]


def hue(rgb):
    r, g, b = [float(np.clip(x, 0, 1)) for x in rgb[:3]]
    return colorsys.rgb_to_hsv(r, g, b)[0] * 360.0


def audit_material(mname, mat, rng, nsamples):
    info = mat.get('info', {})
    glsl = mat.get('glsl', '')
    texs = info.get('textures', [])
    slot2d = {t['slot']: t for t in texs if t.get('kind', '2d') == '2d' and 'slot' in t}
    slotcube = {t['slot']: t for t in texs if t.get('kind') == 'cube' and 'slot' in t}
    blend = info.get('blend_mode', 'BLEND_Opaque')
    lm = info.get('lighting_model', 'MLM_Phong')
    switches = info.get('switches', {}) or {}
    params = info.get('params_read', {}) or {}
    names = ' '.join(params.get('Vector', []) + params.get('Scalar', []) + [str(x) for x in info.get('runtime_params', [])])
    tint = bool(TINT_RE.search(glsl) or TINT_RE.search(json.dumps(info.get('runtime_params', []))) or
                switches.get('Enable_CustomColorTint') or switches.get('Enable_FactionColor') or
                switches.get('Enable_CustomColorTint_AltAlpha'))
    row = dict(material=mname, family=family(mname), master=info.get('master', ''), blend=blend, lighting_model=lm, tint_consumer=tint)
    if blend in EXCLUDED_BLEND or lm in ('MLM_Unlit',) or not glsl:
        row['class'] = 'excluded'
        row['why'] = 'blend %s / lighting %s' % (blend, lm)
        return row, None
    d = E.deps(glsl)
    tex_of = lambda slots: sorted(slot2d[s]['object'] for s in slots if s in slot2d)
    row['spec_power_sources'] = tex_of(d.get('SpecularPower', {}).get('2d', set()))
    row['spec_color_sources'] = tex_of(d.get('SpecularColor', {}).get('2d', set()))
    row['diffuse_sources'] = tex_of(d.get('DiffuseColor', {}).get('2d', set()))
    row['reflection_in_diffuse'] = bool(d.get('DiffuseColor', {}).get('cube'))
    src = row['spec_power_sources'] or row['spec_color_sources'] or row['diffuse_sources']
    row['roughness_parent'] = src[0] if src else None
    # ---- numeric emulation: nsamples random sample tables; outputs with and without the cube reflection
    vecs, emu = [], dict(ok=True)
    spec_rgb, diff_rgb, refl_frac, sp_vals = [], [], [], []
    try:
        for k in range(nsamples):
            s2 = {s: rng.uniform(0.05, 0.95, 4).tolist() for s in slot2d}
            sc = {s: rng.uniform(0.05, 0.95, 4).tolist() for s in slotcube}
            full = E.run(glsl, E.Material(s2, sc))
            nocube = E.run(glsl, E.Material(s2, sc, cube_scale=0.0))
            D = full.get('DiffuseColor', np.zeros(3))[:3]
            D0 = nocube.get('DiffuseColor', np.zeros(3))[:3]
            S = full.get('SpecularColor', np.zeros(3))[:3]
            SP = float(full.get('SpecularPower', np.array([15.0]))[0])
            R = D - D0
            spec_rgb.append(S); diff_rgb.append(D0); sp_vals.append(SP)
            refl_frac.append((float(R @ LUM), float(D @ LUM)))
            # the reference mapping
            rough = float(E.roughness_from_power(SP))
            base = D0 if row['reflection_in_diffuse'] else D
            vecs.append(dict(samples2d={str(s): v for s, v in s2.items()}, samples_cube={str(s): v for s, v in sc.items()},
                             expect=dict(baseColor=base.tolist(), roughness=rough, metallic=0.0, f0=[0.04, 0.04, 0.04],
                                         emissive=full.get('EmissiveColor', np.zeros(3))[:3].tolist(),
                                         reflection=R.tolist(), specularPower=SP)))
            # identity checks
            emu.setdefault('roundtrip_max_rel', 0.0)
            if SP > 0:
                emu['roundtrip_max_rel'] = max(emu['roundtrip_max_rel'], abs(float(E.power_from_roughness(rough)) - SP) / SP if 0.02 < rough < 1 else 0.0)
            emu.setdefault('split_max_abs', 0.0)
            emu['split_max_abs'] = max(emu['split_max_abs'], float(np.max(np.abs((base + (R if row['reflection_in_diffuse'] else 0)) - D))))
            if not np.all(np.isfinite(D)) or not np.all(np.isfinite(S)):
                emu['nonfinite'] = True
    except E.Unsupported as e:
        emu = dict(ok=False, why=str(e))
    row['emulation'] = emu
    # ---- classification (PROVISIONAL metal candidates; reviewed rows decide)
    cls, why = 'dielectric', 'default'
    if emu.get('ok') and spec_rgb:
        S = np.mean(spec_rgb, axis=0); D0 = np.mean(diff_rgb, axis=0)
        row['spec_mean'] = S.tolist(); row['diffuse_mean'] = D0.tolist(); row['spec_power_mean'] = float(np.mean(sp_vals))
        rs, ds = sum(a for a, _ in refl_frac), sum(b for _, b in refl_frac); row["reflection_share"] = float(rs / ds) if ds > 1e-6 else 0.0
        strong = float(S @ LUM) > 0.6 * max(float(D0 @ LUM), 1e-6) and float(S @ LUM) > 0.15
        coloured = sat(S) > 0.3
        if strong and coloured:
            cls, why = 'metal_candidate', 'strong, coloured specular (sat %.2f, spec/diffuse lum %.2f)' % (sat(S), float(S @ LUM) / max(float(D0 @ LUM), 1e-6))
    if cls == 'dielectric' and METAL_NAME_RE.search(mname):
        cls, why = 'metal_candidate', 'name'
    if tint:
        if cls == 'metal_candidate':
            why = 'tint consumer: forced dielectric (was a metal candidate: %s)' % why
        cls = 'dielectric_tint'
    row['class'] = cls
    row['why'] = why
    row['mapping'] = dict(roughness='derived: clamp(sqrt(2/(SpecularPower+2)), 0.02, 1)',
                          metallic=0.0, f0='0.04' if cls != 'metal_reviewed' else 'SpecularColor',
                          baseColor='DiffuseColor without the cube reflection' if row['reflection_in_diffuse'] else 'DiffuseColor',
                          roughness_map=dict(parent=row['roughness_parent'], role='roughness') if row['roughness_parent'] else None)
    return row, vecs


def main():
    root, out = arg('--render'), arg('--out')
    nsamples = int(arg('--samples', '6'))
    if not root or not out:
        print(__doc__)
        return 2
    os.makedirs(out, exist_ok=True)
    rng = np.random.default_rng(20261012)
    rows, vectors = [], {}
    maps = sorted(d for d in os.listdir(root) if d.startswith('MP_') and os.path.isfile(os.path.join(root, d, 'materials_glsl.json')))
    for mp in maps:
        mats = json.load(open(os.path.join(root, mp, 'materials_glsl.json'), encoding='utf-8'))
        for name, mat in mats.items():
            row, vecs = audit_material(name, mat, rng, nsamples)
            row['map'] = mp
            rows.append(row)
            if vecs and name not in vectors:
                vectors[name] = dict(map=mp, vectors=vecs[:3])
    json.dump(rows, open(os.path.join(out, 'm6_mapping.json'), 'w', encoding='utf-8'), indent=1)
    json.dump(vectors, open(os.path.join(out, 'm6_vectors.json'), 'w', encoding='utf-8'))
    # ---- AssetTools' work list: unique roughness parents (+ the families / maps using them), ORM candidates
    wl = defaultdict(lambda: dict(materials=set(), maps=set(), families=set(), classes=set()))
    for r in rows:
        p = r.get('roughness_parent')
        if not p or r['class'] == 'excluded':
            continue
        w = wl[p]
        w['materials'].add(r['material']); w['maps'].add(r['map']); w['families'].add(r['family']); w['classes'].add(r['class'])
    worklist = [dict(tex=k, role='roughness', format='BC4', space='linear', orm_candidate='metal_candidate' in v['classes'],
                     materials=sorted(v['materials']), maps=sorted(v['maps']), families=sorted(v['families'])) for k, v in sorted(wl.items())]
    json.dump(dict(generated_by='tools/systems/m6/m6_audit.py', rows=worklist), open(os.path.join(out, 'm6_worklist.json'), 'w', encoding='utf-8'), indent=1)
    # ---- summary
    uniq = {}
    for r in rows:
        uniq.setdefault(r['material'], r)
    U = list(uniq.values())
    by_cls = Counter(r['class'] for r in U)
    by_fam = defaultdict(Counter)
    for r in U:
        by_fam[r['family']][r['class']] += 1
    emu_ok = [r for r in U if r.get('emulation', {}).get('ok')]
    emu_bad = [r for r in U if r['class'] != 'excluded' and not r.get('emulation', {}).get('ok')]
    why_bad = Counter(r['emulation'].get('why', '?').split(':')[0] for r in emu_bad)
    rt = max((r['emulation'].get('roundtrip_max_rel', 0.0) for r in emu_ok), default=0.0)
    sp = max((r['emulation'].get('split_max_abs', 0.0) for r in emu_ok), default=0.0)
    refl = [r for r in U if r.get('reflection_in_diffuse')]
    lines = ['# M6 classification audit (tools/systems/m6/m6_audit.py)', '',
             'Maps: %s. Material instances: %d rows, %d unique.' % (', '.join(maps), len(rows), len(U)), '',
             '| class | unique materials |', '|---|---|'] + ['| %s | %d |' % kv for kv in sorted(by_cls.items())] + [
             '', '## Per family', '', '| family | ' + ' | '.join(sorted(by_cls)) + ' |', '|---|' + '---|' * len(by_cls)]
    for f in sorted(by_fam):
        lines.append('| %s | ' % f + ' | '.join(str(by_fam[f][c]) for c in sorted(by_cls)) + ' |')
    lines += ['', '## Emulation (glsl_emu)', '',
              '- emulated: %d of %d non-excluded materials; not emulated: %d (%s)' % (
                  len(emu_ok), len(emu_ok) + len(emu_bad), len(emu_bad), ', '.join('%s %d' % kv for kv in why_bad.most_common(6))),
              '- identity: SpecularPower -> roughness -> SpecularPower max relative error %.2e; base + reflection == DiffuseColor max abs %.2e' % (rt, sp),
              '- the cube reflection is added into DiffuseColor in %d materials (stripped from the Modern base colour; median share of the diffuse '
              'luminance %.1f %%)' % (len(refl), 100 * float(np.median([r.get('reflection_share', 0.0) for r in refl])) if refl else 0.0),
              '- customisation-tint consumers (forced dielectric): %d' % sum(1 for r in U if r.get('tint_consumer')),
              '- metal candidates (PROVISIONAL, for review; not applied): %d' % by_cls.get('metal_candidate', 0),
              '', '## AssetTools work list', '',
              '- %d unique parent textures need a roughness map (BC4); %d of them feed a metal candidate (ORM BC7 candidates)' % (
                  len(worklist), sum(1 for w in worklist if w['orm_candidate'])), '',
              'Metal candidates (review list):', '']
    for r in sorted((r for r in U if r['class'] == 'metal_candidate'), key=lambda r: (r['family'], r['material']))[:80]:
        lines.append('- %s (%s): %s' % (r['material'], r['family'], r['why']))
    open(os.path.join(out, 'm6_summary.md'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    print('\n'.join(lines[:40]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
