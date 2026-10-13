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
# representative customisation tints (Experimental's re-review b): the character-select palette swatches (Autobot +
# Decepticon; TnCharacterApplier PrimaryColor -> Cust_Color_A, SecondaryColor -> Cust_COLOR_B) and the chassis materials'
# default energon (AssetTools energon_default) - not the 0.1 grey placeholder of an unset runtime parameter
CHAR_UI = 'F:/Transformers Rebuild/AssetTools/manifests/mp_content/character_select_ui.json'
CONTENT = 'F:/Transformers Rebuild/ExtractedAssets/'
ENERGON_DEFAULT = [0.843, 0.302, 0.029, 1.0]


def palette():
    try:
        j = json.load(open(CHAR_UI, encoding='utf-8'))
    except Exception:                                                   # noqa: BLE001
        return []
    found = []
    def walk(x):
        if isinstance(x, dict):
            if str(x.get('resource', '')).startswith(('autobotPalette', 'decepticonPalette')) and x.get('png'):
                found.append(x['png'])
            for v in x.values():
                walk(v)
        elif isinstance(x, list):
            for v in x:
                walk(v)
    walk(j)
    cols = []
    for png in sorted(set(found)):
        try:
            a = np.asarray(Image.open(CONTENT + png).convert('RGB'), dtype=np.float64) / 255.0
            c = E.srgb_to_linear(a.reshape(-1, 3).mean(axis=0))
            cols.append([float(c[0]), float(c[1]), float(c[2]), 1.0])
        except Exception:                                               # noqa: BLE001
            continue
    return cols


PALETTE = palette()
# two reviewed metal decisions (Integration / Experimental, 2026-10-12; revised after Experimental's review of c9e92922):
#  - robot bodies (CHR_Transformer_NormSpec_Cust_E): "metallic paint" - baseColor = f0 = the tinted paint (rule 3's colour
#    operand) bounded to [0, 1] keeping the hue (paint / max(1, max channel): the paint reaches ~2x through
#    mask.r + sigmoid), metallic = M_PAINT; the shading's diffuse = baseColor x (1 - m) keeps a dielectric share;
#    EnergonColor / the emissive untouched
#  - player weapons (WEP_PhongMain): per-pixel metallic = (1 - mask.b) x mask.g (B = the tint mask, G = the gloss / reflection
#    mask: matte parts had no reflection in the original and stay dielectric); f0 = the reflection's constant colour per unit
#    cube light, clamped to [0, 1] per channel; baseColor = the stripped base (the hook's baseColor is null), so the shading's
#    diffuse = stripped base x (1 - metallic)
MASTER_PAINT = 'TR_AllShader_p.Release.CHR_Transformer_NormSpec_Cust_E_Mat'
MASTER_WEP = 'WEP_Shared_p.WEP_PhongMain_MAT'
M_PAINT = 0.8


def paint_bounded(paint):
    """The metallic-paint base / F0: paint / max(1, max channel) (energy-bounded, hue kept)."""
    p = np.maximum(np.asarray(paint[:3], dtype=np.float64), 0.0)
    return p / max(1.0, float(p.max()))


def wep_mask_temp(glsl, slot):
    """The temporary of the mask-slot sample whose B channel is read (the tint mask), or None."""
    tns = re.findall(r'vec4 (t\d+) = wfcSample2D\w*\(%d,' % slot, glsl)
    for tn in tns:
        if re.search(r'\b%s\.(?:b|z)\b' % tn, glsl):
            return tn
    return tns[0] if tns else None                                 # (no Cust tint path in this instance: the mask's B still marks the paint)


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
    master = info.get('master', '')
    # the body's hash: hooks name temporaries by number, so regenerated render data could rebind them (matc_pbr skips +
    # warns on a mismatch; Rendering 42ec0a72): SHA-1 hex of the materials_glsl.json "glsl" string, UTF-8, as stored
    row['glsl_sha1'] = hashlib.sha1(glsl.encode('utf-8')).hexdigest()
    mask_slot = next((sl for sl, t in slot2d.items() if str(t.get('object', '')).lower().endswith('_mask')), None)
    f0_metal = None
    try:
        if master == MASTER_WEP and mask_slot is not None:        # the weapon metal F0: the reflection term per unit cube light
            ones = {sl: [1.0, 1.0, 1.0, 1.0] for sl in slot2d}
            cubes1 = {sl: [1.0, 1.0, 1.0, 1.0] for sl in slotcube}
            rr = E.run(glsl, E.Material(ones, cubes1, strip_view=True))
            f0_metal = np.maximum(rr['_diffuse_original'][:3] - rr['DiffuseColor'][:3], 0.0)
            f0_metal = np.clip(f0_metal, 0.0, 1.0)                 # (Experimental: clamp, not normalise)
            row['f0_metal'] = f0_metal.tolist(); row['metal_mask'] = '%s (metallic = (1 - B) x G)' % slot2d[mask_slot]['object']
            mt = wep_mask_temp(glsl, mask_slot)
            if mt is None or f0_metal.max() < 0.04:                # (no reflection to carry: stays dielectric)
                f0_metal = None
            else:
                row['pbr_hooks'] = dict(metallic='clamp((1.0 - %s.b) * %s.g, 0.0, 1.0)' % (mt, mt),
                                        f0='vec3(%r, %r, %r)' % tuple(float(x) for x in f0_metal), baseColor=None,
                                        glsl_hash=row['glsl_sha1'])
        if master == MASTER_PAINT:                                 # metallic paint: the paint temporary (rule 3's colour operand)
            rp = E.run(glsl, E.Material({sl: [0.5, 0.5, 0.5, 1.0] for sl in slot2d}, {sl: [0.5, 0.5, 0.5, 1.0] for sl in slotcube}, strip_view=True))
            if rp.get('_paint_expr'):
                pe = rp['_paint_expr']
                pc = pe[:-4] if pe.endswith('.rgb') else pe        # (the channels of a vec4 temporary's .rgb)
                bounded = '(%s / max(1.0, max(max(%s.r, %s.g), %s.b)))' % (pe, pc, pc, pc)
                row['pbr_hooks'] = dict(metallic='%r' % M_PAINT, f0=bounded, baseColor=bounded, glsl_hash=row['glsl_sha1'])
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
            # Rendering's shading: diffuse = baseColor x (1 - metallic), F0 = mix(0.04, f0, metallic); expect.f0 is the
            # hook's f0 (unmixed), [0.04]*3 with metallic 0 when the material has no hook
            metal, base_pbr, f0 = 0.0, B, np.array([0.04, 0.04, 0.04])
            hooks = row.get('pbr_hooks')
            if hooks and master == MASTER_PAINT and '_paint' in r:
                metal, base_pbr = M_PAINT, paint_bounded(r['_paint']); f0 = base_pbr
            elif hooks and f0_metal is not None:
                metal = float(np.clip((1.0 - float(s2[mask_slot][2])) * float(s2[mask_slot][1]), 0.0, 1.0)); f0 = f0_metal
            vecs.append(dict(samples2d={str(s): v for s, v in s2.items()}, samples_cube={str(s): v for s, v in sc.items()},
                             runtime_params_set=rt is not None,
                             expect=dict(baseColor=np.asarray(base_pbr).tolist(), baseStripped=B.tolist(), base_rule=br,
                                         diffuseOriginal=D.tolist(), roughness=float(E.roughness_from_power(SP)), metallic=metal,
                                         f0=np.asarray(f0).tolist(),
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
        rs, sp, dd, ss, oo = [], [], [], [], []
        rt_tint = 'uRT_Cust' in glsl or 'uRT_Energon' in glsl
        for k in range(npoints):
            uv = tuple(rng.uniform(0.0, 1.0, 2))
            rtv = None
            if rt_tint and PALETTE:                                   # (representative tints, cycling through the palette)
                rtv = {'uRT_Cust_Color_A': PALETTE[k % len(PALETTE)], 'uRT_Cust_COLOR_B': PALETTE[(k + 1) % len(PALETTE)],
                       'uRT_EnergonColor': ENERGON_DEFAULT}
            r = E.run(glsl, E.Material(fallback, cube, uv0=uv, images=images, strip_view=True, rt_value=rtv))
            SPv = float(r.get('SpecularPower', np.array([15.0]))[0])
            sp.append(SPv); rs.append(float(E.roughness_from_power(SPv)))
            dd.append(r.get('DiffuseColor', np.zeros(3))[:3]); ss.append(r.get('SpecularColor', np.zeros(3))[:3])
            oo.append(r.get('_diffuse_original', r.get('DiffuseColor', np.zeros(3)))[:3])
        ol = float(np.mean(oo, axis=0) @ LUM)
        row['base_share'] = float(np.mean(dd, axis=0) @ LUM) / ol if ol > 1e-6 else None
        row['tints'] = 'palette (%d swatches) + default energon' % len(PALETTE) if (rt_tint and PALETTE) else ('authored' if tint else None)
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
    if master == MASTER_PAINT and row.get('pbr_hooks'):
        cls, why = 'metallic_paint', 'reviewed: robot body, baseColor = tinted paint, metallic %.1f (sheets: 0.6 - 0.8)' % M_PAINT
    elif master == MASTER_WEP and row.get('pbr_hooks'):
        cls, why = 'weapon_metal_mask', 'reviewed: %s; metal F0 %s' % (row.get('metal_mask'), np.round(f0_metal, 3).tolist())
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
    low = sorted((r for r in ok if r.get('base_share') is not None and r['base_share'] < 0.25), key=lambda r: r['base_share'])
    json.dump([dict(material=r['material'], base_share=r['base_share'], tint_consumer=r['tint_consumer'], tints=r.get('tints'),
                    metal_review_candidate=not r['tint_consumer']) for r in low],
              open(os.path.join(out, 'm6_low_base.json'), 'w', encoding='utf-8'), indent=1)
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
              '- low base after the strip (< 25 %% of the original diffuse, real textures, palette tints for runtime-tint '
              'consumers): %d (%d tint consumers, %d non-tint = metal-review candidates: %s)' % (
                  len(low), sum(1 for r in low if r['tint_consumer']), sum(1 for r in low if not r['tint_consumer']),
                  ', '.join(r['material'] for r in low if not r['tint_consumer'])),
              '- palette tints loaded: %d swatches' % len(PALETTE),
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
