"""M6 Modern materials - the classification audit + the "pbr" mapping table (Systems; docs/handoff/SYSTEMS_RT_REALISTIC_M6_PLAN.md
section 4 / 4c; contract in docs/handoff/SYSTEMS_M6_AUDIT.md). Reads every map's materials_glsl.json and the materials' own
textures (ExtractedAssets, read-only), and writes into --out:
  m6_mapping.json     one row per unique material: class, the PBR mapping, the base-colour strip, the roughness parent +
                      the derived mean roughness (its target), tint / never-remap flags, the maps using it
  m6_worklist.json    AssetTools' work list: unique parent textures needing a roughness map (BC4, half the colour size),
                      the normalisation target (mean derived roughness) per texture and per (texture, material), ORM
                      candidates per (texture, material)
  m6_vectors.json     test vectors for matc's "pbr" mode (the oracle): per material 3 sample tables (fixed seed) + 1 with
                      every runtime parameter set (Overheat / Defrag / EnergonColor ... = 1), the view inputs, the expected
                      outputs
  m6_summary.md       counts per family / class, emulation coverage, the identity checks

The mapping (reference; matc's "pbr" mode must reproduce it, checked with m6_vectors.json through glsl_emu.run):
  baseColor  = DiffuseColor under the base-colour strip (glsl_emu docstring): additive view terms (the cube reflection, the
               anisotropic camera specular) dropped at colour sums; a remaining multiplicative view factor -> its mean over
               glsl_emu.VIEW_SET. Only the base colour is stripped: EmissiveColor keeps its cube / view terms.
  roughness  = clamp(sqrt(2 / (SpecularPower + 2)), 0.02, 1) per pixel from the material's own SpecularPower; a generated
               roughness map (hd_pbr_index role "roughness") replaces it, its mean normalised to this material's target
  metallic   = 0 unless a reviewed metal row exists (the PROVISIONAL candidates are listed, never applied); tint consumers
               and HUD-marker materials are forced dielectric / never remapped
  F0         = 0.04 (dielectric); SpecularColor for reviewed metals
  emissive / normal / opacity: unchanged

  m6_audit.py --render <work/render root> --out <dir> [--points 128]
"""
import colorsys
import hashlib
import json
import os
import re
import sys
from collections import Counter, OrderedDict, defaultdict

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import glsl_emu as E  # noqa: E402

LUM = np.array([0.2126, 0.7152, 0.0722])
TINT_RE = re.compile(r'(?i)cust_?colou?r|energon|faction|uRT_Cust|teamcolor|team_color')
# HUD-marker / team-state materials: their colours are gameplay signals - never remapped (Experimental 1a)
NEVER_REMAP_RE = re.compile(r'\b(?:uRT_)?(?:FriendlyColor|EnemyColor|AutobotColor|DecepticonColor|TeamID|NeutralColor|Flashing|PingOpacity)\b')
METAL_WORDS = {'metal', 'metals', 'chrome', 'steel', 'iron', 'gold', 'copper', 'brass', 'aluminium', 'aluminum', 'bronze', 'tin'}
EXCLUDED_BLEND = {'BLEND_Translucent', 'BLEND_Additive', 'BLEND_Modulate', 'BLEND_SoftMasked'}


def arg(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def family(name):
    pkg = name.split('.')[0]
    return pkg.split('_')[0].upper() if '_' in pkg else pkg.upper()


def words(name):
    """Name -> lower-case words (split at '_', '.', digits and camel-case humps): "Ironhide" stays one word."""
    out = []
    for p in re.split(r'[_.\d]+', name):
        out += [w.lower() for w in re.findall(r'[A-Z]+(?=[A-Z][a-z])|[A-Z]?[a-z]+|[A-Z]+', p)]
    return out


def sat(rgb):
    r, g, b = [float(np.clip(x, 0, 1)) for x in rgb[:3]]
    return colorsys.rgb_to_hsv(r, g, b)[1]


def seed_of(name):
    return int(hashlib.sha1(name.encode('utf-8')).hexdigest()[:8], 16)


class Images:
    """The materials' own 2D textures, decoded as the GPU sees them (sRGB -> linear on RGB, unpack range), at <= 128 px."""

    def __init__(self, cap=400):
        self.cache, self.cap = OrderedDict(), cap

    def get(self, t):
        key = t.get('file')
        if not key or not os.path.isfile(key):
            return None
        if key in self.cache:
            self.cache.move_to_end(key)
            return self.cache[key]
        try:
            im = Image.open(key).convert('RGBA')
            im.thumbnail((128, 128))
            a = np.asarray(im, dtype=np.float64) / 255.0
        except Exception:                                                   # noqa: BLE001
            return None
        if t.get('srgb'):
            a = a.copy()
            a[..., :3] = E.srgb_to_linear(a[..., :3])
        mn = t.get('unpack_min')
        if mn:
            mn = np.array([float(x) for x in mn])
            a = mn + a * (1.0 - mn)
        self.cache[key] = a
        if len(self.cache) > self.cap:
            self.cache.popitem(last=False)
        return a


def pick_parent(objs):
    """Deterministic roughness parent among the textures feeding an output: mask / spec / gloss-named first, then by name."""
    return sorted(objs, key=lambda o: (0 if any(k in o.lower() for k in ('mask', 'spec', 'gloss')) else 1, o.lower()))[0] if objs else None


def audit_material(mname, mat, imgs, npoints):
    info = mat.get('info', {})
    glsl = mat.get('glsl', '')
    texs = info.get('textures', [])
    slot2d = {t['slot']: t for t in texs if t.get('kind', '2d') == '2d' and 'slot' in t}
    slotcube = {t['slot']: t for t in texs if t.get('kind') == 'cube' and 'slot' in t}
    blend = info.get('blend_mode', 'BLEND_Opaque')
    lm = info.get('lighting_model', 'MLM_Phong')
    switches = info.get('switches', {}) or {}
    rtp = json.dumps(info.get('runtime_params', []))
    pr = info.get('params_read', {}) or {}
    params = ' '.join(pr.get('Vector', []) + pr.get('Scalar', []))
    tint = bool(TINT_RE.search(glsl) or TINT_RE.search(rtp) or switches.get('Enable_CustomColorTint') or
                switches.get('Enable_FactionColor') or switches.get('Enable_CustomColorTint_AltAlpha'))
    never = bool(NEVER_REMAP_RE.search(glsl) or NEVER_REMAP_RE.search(rtp) or NEVER_REMAP_RE.search(params))
    row = dict(material=mname, family=family(mname), master=info.get('master', ''), blend=blend, lighting_model=lm,
               tint_consumer=tint, never_remap=never)
    if blend in EXCLUDED_BLEND or lm in ('MLM_Unlit',) or not glsl:
        row['class'] = 'excluded'; row['why'] = 'blend %s / lighting %s' % (blend, lm)
        return row, None
    if never:
        row['class'] = 'never_remap'; row['why'] = 'HUD marker / team-state parameters'
        return row, None
    d = E.deps(glsl)
    tex_of = lambda slots: sorted(slot2d[s]['object'] for s in slots if s in slot2d and slot2d[s].get('object'))
    row['spec_power_sources'] = tex_of(d.get('SpecularPower', {}).get('2d', set()))
    row['spec_color_sources'] = tex_of(d.get('SpecularColor', {}).get('2d', set()))
    row['diffuse_sources'] = tex_of(d.get('DiffuseColor', {}).get('2d', set()))
    row['view_in_diffuse'] = bool(d.get('DiffuseColor', {}).get('view')) or bool(d.get('DiffuseColor', {}).get('cube'))
    row['roughness_parent'] = pick_parent(row['spec_power_sources'] or row['spec_color_sources'] or row['diffuse_sources'])
    rng = np.random.default_rng(seed_of(mname))                       # (deterministic per material: Experimental 1d)
    emu, vecs = dict(ok=True), []
    try:
        # ---- the oracle vectors: 3 fixed-seed sample tables + 1 with the runtime parameters set
        for k in range(4):
            s2 = {s: rng.uniform(0.05, 0.95, 4).tolist() for s in slot2d}
            sc = {s: rng.uniform(0.05, 0.95, 4).tolist() for s in slotcube}
            rt = 1.0 if k == 3 else None
            r = E.run(glsl, E.Material(s2, sc, strip_view=True, rt_value=rt))
            full = E.run(glsl, E.Material(s2, sc, rt_value=rt))
            D, B = full.get('DiffuseColor', np.zeros(3))[:3], r.get('DiffuseColor', np.zeros(3))[:3]
            SP = float(full.get('SpecularPower', np.array([15.0]))[0])
            br = int(r.get('_base_rule', [0])[0])
            vecs.append(dict(samples2d={str(s): v for s, v in s2.items()}, samples_cube={str(s): v for s, v in sc.items()},
                             runtime_params_set=rt is not None,
                             expect=dict(baseColor=B.tolist(), base_rule=br, diffuseOriginal=D.tolist(),
                                         roughness=float(E.roughness_from_power(SP)), metallic=0.0, f0=[0.04, 0.04, 0.04],
                                         emissive=full.get('EmissiveColor', np.zeros(3))[:3].tolist(), specularPower=SP)))
            emu['base_rule'] = max(emu.get('base_rule', 0), br)
            if br == 1:                                               # (rule 1 only removes terms: never brighter)
                emu['base_inflation_max'] = max(emu.get('base_inflation_max', 0.0), float(B @ LUM) / max(float(D @ LUM), 1e-6))
            if SP > 0:
                rough = float(E.roughness_from_power(SP))
                if 0.02 < rough < 1:
                    emu['roundtrip_max_rel'] = max(emu.get('roundtrip_max_rel', 0.0), abs(float(E.power_from_roughness(rough)) - SP) / SP)
        # ---- the material's own textures: classification means + the derived mean roughness (the map's target)
        images = {s: imgs.get(t) for s, t in slot2d.items()}
        images = {s: a for s, a in images.items() if a is not None}
        fallback = {s: [0.5, 0.5, 0.5, 1.0] for s in slot2d if s not in images}
        cube = {s: [0.5, 0.5, 0.5, 1.0] for s in slotcube}
        rs, sp, dd, ss = [], [], [], []
        for _ in range(npoints):
            uv = tuple(rng.uniform(0.0, 1.0, 2))
            r = E.run(glsl, E.Material(fallback, cube, uv0=uv, images=images, strip_view=True))
            SPv = float(r.get('SpecularPower', np.array([15.0]))[0])
            sp.append(SPv); rs.append(float(E.roughness_from_power(SPv)))
            dd.append(r.get('DiffuseColor', np.zeros(3))[:3]); ss.append(r.get('SpecularColor', np.zeros(3))[:3])
        row['real_textures'] = '%d of %d' % (len(images), len(slot2d))
        row['roughness_target'] = float(np.mean(rs)); row['spec_power_mean'] = float(np.mean(sp))
        row['base_mean'] = np.mean(dd, axis=0).tolist(); row['spec_mean'] = np.mean(ss, axis=0).tolist()
    except E.Unsupported as e:
        emu = dict(ok=False, why=str(e))
    row['emulation'] = emu
    # ---- classification (PROVISIONAL metal candidates; reviewed rows decide)
    cls, why = 'dielectric', 'default'
    if 'spec_mean' in row:
        S, D0 = np.array(row['spec_mean']), np.array(row['base_mean'])
        dl = float(D0 @ LUM)
        if dl >= 0.02 and float(S @ LUM) > 0.6 * dl and float(S @ LUM) > 0.15 and sat(S) > 0.3:   # (1d: no ratio at ~0 base)
            cls, why = 'metal_candidate', 'strong, coloured specular (sat %.2f, spec/base lum %.2f)' % (sat(S), float(S @ LUM) / dl)
    mw = METAL_WORDS & set(words(mname.split('.')[-1]))
    if cls == 'dielectric' and mw:
        cls, why = 'metal_candidate', 'name word: %s' % ', '.join(sorted(mw))
    if tint:
        if cls == 'metal_candidate':
            why = 'tint consumer: forced dielectric (was a metal candidate: %s)' % why
        cls = 'dielectric_tint'
    row['class'], row['why'] = cls, why
    row['mapping'] = dict(baseColor='DiffuseColor under the base-colour strip' + (' (view / cube terms present)' if row['view_in_diffuse'] else ''),
                          roughness='derived: clamp(sqrt(2/(SpecularPower+2)), 0.02, 1)', metallic=0.0, f0='0.04',
                          roughness_map=dict(parent=row['roughness_parent'], role='roughness', target_mean=row.get('roughness_target'))
                          if row['roughness_parent'] else None)
    return row, vecs


def main():
    root, out = arg('--render'), arg('--out')
    npoints = int(arg('--points', '128'))
    if not root or not out:
        print(__doc__)
        return 2
    os.makedirs(out, exist_ok=True)
    imgs = Images()
    uniq, maps_of, vectors = {}, defaultdict(set), {}
    maps = sorted(d for d in os.listdir(root) if d.startswith('MP_') and os.path.isfile(os.path.join(root, d, 'materials_glsl.json')))
    for mp in maps:
        mats = json.load(open(os.path.join(root, mp, 'materials_glsl.json'), encoding='utf-8'))
        for name, mat in mats.items():
            maps_of[name].add(mp)
            if name in uniq:                                           # (classified once per unique material: 1d)
                continue
            row, vecs = audit_material(name, mat, imgs, npoints)
            uniq[name] = row
            if vecs:
                vectors[name] = dict(map=mp, vectors=vecs)
    for name, row in uniq.items():
        row['maps'] = sorted(maps_of[name])
    U = list(uniq.values())
    json.dump(U, open(os.path.join(out, 'm6_mapping.json'), 'w', encoding='utf-8'), indent=1)
    json.dump(dict(view_set=E.VIEW_SET, camera=E.DEFAULT_CAMERA, reflection=E.DEFAULT_REFLECTION, light=E.DEFAULT_LIGHT,
                   materials=vectors), open(os.path.join(out, 'm6_vectors.json'), 'w', encoding='utf-8'))
    # ---- AssetTools' work list: per parent texture, the per-material targets and ORM candidacy (per (texture, material): 1b/1c)
    wl = defaultdict(list)
    for r in U:
        p = r.get('roughness_parent')
        if not p or r['class'] in ('excluded', 'never_remap'):
            continue
        wl[p].append(dict(material=r['material'], cls=r['class'], target=r.get('roughness_target')))
    worklist = []
    for tex, ms in sorted(wl.items()):
        tg = [m['target'] for m in ms if m['target'] is not None]
        worklist.append(dict(tex=tex, role='roughness', format='BC4', space='linear', srgb=False, resolution='half of the colour map',
                             target_mean_roughness=float(np.mean(tg)) if tg else None,
                             orm_candidate_materials=[m['material'] for m in ms if m['cls'] == 'metal_candidate'],
                             materials=[{'material': m['material'], 'class': m['cls'], 'target_mean_roughness': m['target']} for m in ms]))
    json.dump(dict(generated_by='tools/systems/m6/m6_audit.py',
                   note='normalise each generated map so its mean roughness equals target_mean_roughness (the derived '
                        'sqrt(2/(SpecularPower+2)) mean over the materials using it, from their own textures); the renderer reads '
                        'an ORM metal channel only for reviewed metal rows; orm_candidate_materials are per (texture, material)',
                   rows=worklist), open(os.path.join(out, 'm6_worklist.json'), 'w', encoding='utf-8'), indent=1)
    # ---- summary
    by_cls = Counter(r['class'] for r in U)
    by_fam = defaultdict(Counter)
    for r in U:
        by_fam[r['family']][r['class']] += 1
    act = [r for r in U if r['class'] not in ('excluded', 'never_remap')]
    ok = [r for r in act if r.get('emulation', {}).get('ok')]
    bad = [r for r in act if not r.get('emulation', {}).get('ok')]
    why_bad = Counter(r['emulation'].get('why', '?').split(':')[0] for r in bad)
    rt = max((r['emulation'].get('roundtrip_max_rel', 0.0) for r in ok), default=0.0)
    rule = Counter(r['emulation'].get('base_rule', 0) for r in ok)
    infl = [r for r in ok if r['emulation'].get('base_inflation_max', 0) > 1.0 + 1e-9]
    cls_names = sorted(by_cls)
    lines = ['# M6 classification audit (tools/systems/m6/m6_audit.py)', '',
             'Maps: %s. Unique materials: %d.' % (', '.join(maps), len(U)), '',
             '| class | unique materials |', '|---|---|'] + ['| %s | %d |' % kv for kv in sorted(by_cls.items())] + [
             '', '## Per family', '', '| family | ' + ' | '.join(cls_names) + ' |', '|---|' + '---|' * len(cls_names)]
    for f in sorted(by_fam):
        lines.append('| %s | ' % f + ' | '.join(str(by_fam[f][c]) for c in cls_names) + ' |')
    lines += ['', '## Emulation (glsl_emu; real textures for the targets)', '',
              '- emulated: %d of %d remapped materials; not emulated: %d (%s)' % (len(ok), len(act), len(bad),
                                                                                    ', '.join('%s %d' % kv for kv in why_bad.most_common(6))),
              '- base-colour strip: no view term %d, additive view terms dropped (rule 1) %d, multiplicative view factor averaged (rule 2) %d' % (
                  rule.get(0, 0), rule.get(1, 0), rule.get(2, 0)),
              '- rule-1 base colour brighter than the original diffuse (must be 0): %d%s' % (
                  len(infl), (' - ' + ', '.join(r['material'] for r in infl[:6])) if infl else ''),
              '- SpecularPower -> roughness -> SpecularPower: max relative error %.2e' % rt,
              '- customisation-tint consumers (forced dielectric): %d; HUD-marker materials (never remapped): %d' % (
                  sum(1 for r in U if r.get('tint_consumer')), by_cls.get('never_remap', 0)),
              '- metal candidates (PROVISIONAL, for review; not applied): %d' % by_cls.get('metal_candidate', 0),
              '', '## AssetTools work list', '',
              '- %d unique parent textures need a roughness map (BC4, half the colour size), each with its target mean roughness; '
              '%d have at least one (texture, material) ORM candidate' % (len(worklist), sum(1 for w in worklist if w['orm_candidate_materials'])),
              '', 'Metal candidates (review list):', '']
    for r in sorted((r for r in U if r['class'] == 'metal_candidate'), key=lambda r: (r['family'], r['material'])):
        lines.append('- %s (%s): %s' % (r['material'], r['family'], r['why']))
    open(os.path.join(out, 'm6_summary.md'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    print('\n'.join(lines))
    return 0


if __name__ == '__main__':
    sys.exit(main())
