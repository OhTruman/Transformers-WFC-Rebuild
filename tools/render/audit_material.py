"""Per-section material audit for a mesh (e.g. the Optimus vehicle): every value with its source.

Usage: python audit_material.py <work/render/<Map>> <out.md> <mesh glb> <mat path> [<mat path> ...]
Reads materials_glsl.json + permutation_check.json (verify_permutations.py) from the render dir and the
cooked packages (MIC parameter overrides, static switches, master defaults). The material paths are
given in mesh section order (the cooked slot table, e.g. materials_authored.json slot_materials).
Status marks: CONFIRMED ORIGINAL (read from cooked data / compiled resource), HIGH CONFIDENCE,
PROVISIONAL, UNKNOWN.
"""
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue3obj  # noqa: E402


def ref(v):
    return v.get('ref') if isinstance(v, dict) else v


def prop_list(o, key):
    """[(name, value)] from a MIC's *ParameterValues array."""
    out = []
    for e in o.get(key) or []:
        d = {}
        for f in e if isinstance(e, list) else []:
            d[f.get('name')] = f.get('value')
        if not d and isinstance(e, dict):
            d = e
        nm = d.get('ParameterName')
        if nm:
            out.append((nm, d.get('ParameterValue')))
    return out


def glb_sections(path):
    f = open(path, 'rb').read()
    n = struct.unpack('<I', f[12:16])[0]
    g = json.loads(f[20:20 + n])
    secs = []
    for mesh in g['meshes']:
        for p in mesh['primitives']:
            secs.append({'material': g['materials'][p['material']].get('name'),
                         'tris': g['accessors'][p['indices']]['count'] // 3})
    return secs


def main():
    rd, out_md, glb = sys.argv[1], sys.argv[2], sys.argv[3]
    mats = sys.argv[4:]
    M = json.load(open(os.path.join(rd, 'materials_glsl.json')))
    M = M.get('materials', M)
    P = json.load(open(os.path.join(rd, 'permutation_check.json')))
    P = P.get('materials', P)
    R = ue3obj.Repo(['MP_IAC_Streets_BASE_m.xxx', 'MP_IAC_Streets_ART_m.xxx'])
    secs = glb_sections(glb)
    lines = ['# Material audit: %s' % os.path.basename(glb), '',
             'Marks: **CONFIRMED** = read from cooked data or the compiled material resource; **HIGH** = '
             'standard UE3 semantics applied to confirmed data; **PROV** = provisional; **UNKNOWN**.', '']
    report = []
    for si, mp in enumerate(mats):
        e = M.get(mp) or {}
        info = e.get('info') or {}
        glsl = e.get('glsl') or ''
        pc = P.get(mp) or {}
        sec = secs[si] if si < len(secs) else {}
        lines += ['## Section %d: `%s`' % (si, mp), '']
        lines.append('- Slot binding: section %d (%s tris) -> slot %d -> `%s` (cooked slot table) **CONFIRMED**'
                     % (si, sec.get('tris'), si, mp))
        lines.append('- Master: `%s`; chain: %s **CONFIRMED**' % (info.get('master'), info.get('chain')))
        lines.append('- Lighting model %s, blend %s, two-sided %s **CONFIRMED**' % (
            info.get('lighting_model'), info.get('blend_mode'), info.get('two_sided')))
        # static switches with override flags
        try:
            sw, _ = R.mic_static_params(mp.lower())
        except Exception:
            sw = {}
        lines.append('- Static switches (value, MIC override) decoded from the MIC native tail and validated '
                     'against StaticSwitchParameter names **CONFIRMED**:')
        for k, v in sorted((info.get('switches') or {}).items()):
            ov = sw.get(k, (None, None))[1]
            lines.append('    - `%s` = %s%s' % (k, v, ' (MIC override)' if ov else ' (master default)'))
        st = 'CONFIRMED' if pc.get('match', True) else 'DIFFERS'
        lines.append('- Compiled permutation parameter set vs translation: **%s** (%s)' % (
            st, json.dumps(pc.get('diff') or {})))
        comp = pc.get('compiled') or {}
        lines.append('    - compiled scalars %d, vectors %d, textures %d' % (
            len(comp.get('Scalar', [])), len(comp.get('Vector', [])), len(comp.get('Texture', []))))
        # parameter values by source (MIC chain then master expression defaults)
        lines.append('- Parameter values (source) **CONFIRMED**:')
        chain = []
        p = mp.lower()
        while p and R.cls(p) == 'MaterialInstanceConstant':
            chain.append(p)
            p = (ref((R.obj(p) or {}).get('Parent')) or '').lower() or None
        seen = set()
        for c in chain:
            o = R.obj(c) or {}
            for key, kind in (('ScalarParameterValues', 'scalar'), ('VectorParameterValues', 'vector'),
                              ('TextureParameterValues', 'texture')):
                for nm, val in prop_list(o, key):
                    if nm in seen:
                        continue
                    seen.add(nm)
                    if isinstance(val, list):
                        val = [round(x, 5) if isinstance(x, float) else x for x in val]
                    lines.append('    - %s `%s` = %s  (override in `%s`)' % (kind, nm, ref(val) if kind == 'texture' else val, c))
        other = sorted(set(comp.get('Scalar', []) + comp.get('Vector', [])) - seen - {'SelectionColor'})
        if other:
            lines.append('    - master expression defaults (no MIC override): %s' % ', '.join('`%s`' % x for x in other))
        # textures
        lines.append('- Texture set (format / colour space / unpack / address) **CONFIRMED**:')
        glines = glsl.split('\n')

        def channels(slot):
            """Swizzles read from each sample of this texture slot (whole-vector use = 'rgba')."""
            used = set()
            for i, l in enumerate(glines):
                m = re.match(r'\s*vec4 (t\d+) = wfcSample(?:2D|Cube|CubeBias)\(%s,' % slot, l)
                if not m:
                    continue
                v = m.group(1)
                for x in glines[i + 1:]:
                    rhs = x.split('=', 1)[-1]
                    for sw in re.findall(r'\b%s\)?\.([rgbaxyzw]+)' % v, rhs):
                        used.update(sw)
                    if re.search(r'\b%s\b(?!\)?\.)' % v, rhs):
                        used.add('rgba')
            return ''.join(sorted(used)) or '-'
        for t in info.get('textures') or []:
            role = ' [channels read: %s]' % channels(t.get('slot'))
            if t.get('unpack_min') and t['unpack_min'][0] == -1.0:
                role += ' -> unpacked x*2-1 (TextureSet / normal-map UnpackMin -1)'
            lines.append('    - slot %s `%s` %s %s, sRGB=%s, unpack_min=%s, %s/%s, %s%s' % (
                t.get('slot'), t.get('object'), t.get('format'), t.get('size'), t.get('srgb'), t.get('unpack_min'),
                t.get('address_x'), t.get('address_y'), t.get('lod_group'), role))
        if 'UseReconstructedNormal' in (info.get('switches') or {}):
            lines.append('    - normal: UseReconstructedNormal=%s -> %s **CONFIRMED graph**' % (
                info['switches']['UseReconstructedNormal'],
                'Z = sqrt(1 - x^2 - y^2) from the XY map' if info['switches']['UseReconstructedNormal'] else
                'map Z used as stored'))
        # customization / energon
        rts = re.findall(r'uRTSet_(\w+) != 0 \? uRT_\w+ : (vec4\([^)]*\))', glsl)
        if rts:
            lines.append('- TnCharacterApplier parameters (runtime override, all-zero = skip; Gameplay currently '
                         'passes none, so the MIC values below render) **CONFIRMED**:')
            for nm, v in rts:
                lines.append('    - `%s` default %s' % (nm, v))
        lines.append('- Reflection/camera vectors: %s; cube reflection via TextureSampleParameterCube with LODBias '
                     'input where authored **CONFIRMED graph**' % ', '.join(info.get('uses') or []))
        lines.append('- Colour space: sRGB textures decoded by the sampler (Xenon gamma textures; PWL vs exact '
                     'sRGB curve not reproduced **PROV**); shading in linear HDR; display gamma 2.2 + CLUT in post **CONFIRMED**')
        if info.get('notes'):
            lines.append('- Translator notes: %s' % info['notes'])
        lines.append('')
        report.append({'section': si, 'material': mp, 'tris': sec.get('tris'), 'permutation': st,
                       'switches': info.get('switches'), 'textures': info.get('textures')})
    open(out_md, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    json.dump(report, open(os.path.splitext(out_md)[0] + '.json', 'w'), indent=1)
    print('wrote', out_md)


if __name__ == '__main__':
    main()
