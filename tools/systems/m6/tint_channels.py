"""Which texture channel masks the customisation / Energon tint, per master material (M6 / HD "E" pass guard; Systems).

For every material that consumes a customisation colour (runtime uRT_Cust_Color_* / uRT_EnergonColor..., or the masters'
folded Cust_Color constants behind Enable_CustomColorTint), finds the mix(a, b, f) calls whose a or b carries a tint colour and
resolves the factor f back through swizzles / vec constructors to the texture sample and channel it reads. Tallies
(master, texture role, channel) over all maps' materials_glsl.json (read-only) and prints the table + writes JSON.

  tint_channels.py --render <work/render root> --out <file.json>
"""
import json
import os
import re
import sys
from collections import Counter, defaultdict

DECL = re.compile(r'^\s*(?:float|vec[234])\s+(t\d+)\s*=\s*(.*);\s*$')
SAMPLE = re.compile(r'^wfcSample2D\w*\(\s*(\d+)')
TINT_RT = re.compile(r'uRT_(?:Cust_Colou?r_\w+|Energon\w*|Faction\w*|TeamColor\w*)')
CH = {'x': 'R', 'r': 'R', 'y': 'G', 'g': 'G', 'z': 'B', 'b': 'B', 'w': 'A', 'a': 'A'}


def split_args(s):
    out, depth, cur = [], 0, ''
    for c in s:
        if c == ',' and depth == 0:
            out.append(cur.strip()); cur = ''
            continue
        depth += c == '('
        depth -= c == ')'
        cur += c
    if cur.strip():
        out.append(cur.strip())
    return out


def resolve_channel(expr, defs, depth=0):
    """expr -> (sample slot, channel letter) when it reads exactly one channel of one sample through simple chains."""
    if depth > 12:
        return None
    e = expr.strip()
    while e.startswith('(') and e.endswith(')') and e.count('(') == e.count(')'):
        inner = e[1:-1]
        if inner.count('(') != inner.count(')'):
            break
        e = inner.strip()
    m = re.fullmatch(r'(t\d+)', e)
    if m:
        d = defs.get(m.group(1))
        if d is None:
            return None
        sm = SAMPLE.match(d)
        return (int(sm.group(1)), None) if sm else resolve_channel(d, defs, depth + 1)
    m = re.fullmatch(r'(.+)\.([xyzwrgba])', e)                  # a single-component swizzle
    if m:
        base = m.group(1).strip()
        comp = m.group(2)
        # (vec4(tX, 1.0)).z  -> component of tX; tX.rgb -> tY; tY = sample
        bm = re.fullmatch(r'\(?\s*vec4\(\s*(t\d+)\s*,\s*[-\d.e]+\s*\)\s*\)?', base)
        if bm:
            base = bm.group(1)
        src = resolve_swizzled(base, defs, depth + 1)
        if src:
            slot, sw = src
            idx = 'xyzw'.index({'r': 'x', 'g': 'y', 'b': 'z', 'a': 'w'}.get(comp, comp))
            if sw is None:
                return slot, CH[comp]
            if idx < len(sw):
                return slot, CH[sw[idx]]
    return None


def resolve_swizzled(name, defs, depth):
    """name -> (slot, swizzle string or None) for tX = sample / tX = tY.rgb chains."""
    if depth > 12:
        return None
    m = re.fullmatch(r'(t\d+)', name.strip())
    if not m:
        return None
    d = defs.get(m.group(1))
    if d is None:
        return None
    sm = SAMPLE.match(d.strip())
    if sm:
        return int(sm.group(1)), None
    m2 = re.fullmatch(r'(t\d+)\.([xyzwrgba]{2,4})', d.strip())
    if m2:
        src = resolve_swizzled(m2.group(1), defs, depth + 1)
        if src:
            slot, sw = src
            s = ''.join({'r': 'x', 'g': 'y', 'b': 'z', 'a': 'w'}.get(c, c) for c in m2.group(2))
            return slot, (s if sw is None else ''.join(sw[('xyzw').index(c)] for c in s))
    return None


def carries_tint(expr, defs, tint_temps):
    return any(t in tint_temps for t in re.findall(r'\bt\d+\b', expr)) or bool(TINT_RT.search(expr))


def main():
    root = sys.argv[sys.argv.index('--render') + 1]
    out = sys.argv[sys.argv.index('--out') + 1]
    tally = defaultdict(Counter)
    examples = {}
    for mp in sorted(d for d in os.listdir(root) if d.startswith('MP_')):
        mats = json.load(open(os.path.join(root, mp, 'materials_glsl.json'), encoding='utf-8'))
        for name, mat in mats.items():
            info, g = mat.get('info', {}), mat.get('glsl', '')
            sw = info.get('switches', {}) or {}
            rt = bool(TINT_RT.search(g))
            if not rt and not (sw.get('Enable_CustomColorTint') or sw.get('Enable_FactionColor')):
                continue
            defs, order = {}, []
            for line in g.split('\n'):
                md = DECL.match(line)
                if md:
                    defs[md.group(1)] = md.group(2); order.append(md.group(1))
            # tint carriers: temps whose definition reads a runtime tint, or (folded masters) a product with a colour constant
            # that is later blended with another such product in a mix()
            tint = set(t for t in order if TINT_RT.search(defs[t]))
            changed = True
            while changed:
                changed = False
                for t in order:
                    if t not in tint and any(u in tint for u in re.findall(r'\bt\d+\b', defs[t])) and not defs[t].startswith('mix('):
                        tint.add(t); changed = True
            slots = {t['slot']: t['object'] for t in info.get('textures', []) if 'slot' in t}
            for t in order:
                d = defs[t]
                mm = re.match(r'^mix\((.*)\)$', d.strip())
                if not mm:
                    continue
                args = split_args(mm.group(1))
                if len(args) != 3:
                    continue
                a, b, f = args
                tinted = carries_tint(a, defs, tint) or carries_tint(b, defs, tint)
                folded = not rt and re.search(r'\*\s*\(t\d+\)\.xyz', defs.get(a.strip(), '')) and re.search(r'\*\s*\(t\d+\)\.xyz', defs.get(b.strip(), ''))
                if not (tinted or folded):
                    continue
                ch = resolve_channel(f, defs)
                if not ch:
                    continue
                slot, c = ch
                obj = slots.get(slot, '?')
                role = obj.split('_')[-2] + '_' + obj.split('_')[-1] if '_' in obj else obj
                role = re.sub(r'.*(Masks|Color|Mask|Spec|Emis|Normal)', r'\1', role)
                key = (info.get('master', ''), role, c)
                tally[info.get('master', '')][(role, c)] += 1
                examples.setdefault(key, name)
    rows = []
    for master, cnt in sorted(tally.items()):
        for (role, c), n in cnt.most_common():
            rows.append(dict(master=master, texture_role=role, channel=c, materials=n, example=examples[(master, role, c)]))
            print('%-60s %-14s %s  %4d   e.g. %s' % (master, role, c, n, examples[(master, role, c)]))
    json.dump(rows, open(out, 'w', encoding='utf-8'), indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
