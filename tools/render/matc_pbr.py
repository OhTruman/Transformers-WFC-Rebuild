"""matc "pbr" backend (M6 Modern materials, PC ADAPTATION): each material's GLSL body (materials_glsl.json, unchanged)
re-emitted for the Modern shading, following Systems' mapping (docs/handoff/SYSTEMS_M6_AUDIT.md, tools/systems/m6):

  baseColor  = DiffuseColor evaluated with every cube-map sample at 0 - the REFLECTION_ON masters add the cube reflection
               into DiffuseColor; Modern's image-based lighting supplies the specular, so keeping it would count it twice.
               Emitted without a second material evaluation: the temporaries DiffuseColor needs that depend on a cube
               sample are re-emitted once more as t<100000 + N> with the cube samples replaced by vec4(0.0), and o.DiffuseColor
               reads those. Everything else (SpecularPower, SpecularColor, Normal, Emissive, Opacity ...) is unchanged.
  roughness  = clamp(sqrt(2 / (SpecularPower + 2)), 0.02, 1) per pixel - computed by the Modern shading from o.SpecularPower;
               where the HD pack has a roughness map for the material's roughness parent (the texture feeding SpecularPower,
               else SpecularColor, else DiffuseColor), the body also sets wfcPbrRoughMap = that map's .r, sampled with the
               parent's own UV (a new texture slot, listed in the output; -1 when the runtime has no map: uPbrRoughOn 0).
  metallic 0, F0 0.04 (no reviewed metal rows yet; tint consumers forced dielectric).

Output per map: <out>/materials_pbr.json {material: {glsl, roughness_parent, rough_slot, rough_uv, base_stripped}}.
Check: --check <m6_vectors.json> runs Systems' oracle (glsl_emu) on every pbr body and compares baseColor / roughness /
emissive with the expected values (1e-6).

  python tools/render/matc_pbr.py <map render dir> <out dir> [--check <m6_vectors.json> --emu <glsl_emu.py dir>]
"""
import ast, json, os, re, sys

_CUBE = re.compile(r'\bwfcSampleCube(?:Bias)?\(')
_TMP = re.compile(r'\bt\d+\b')
_STMT = re.compile(r'^(\s*)(?:(float|vec[234])\s+)?([\w.]+)\s*=\s*(.*);\s*$')


def _replace_calls(expr, rx, repl):
    """Replaces every call matched by rx (name + '(') with repl, balancing parentheses."""
    out, i = [], 0
    while True:
        m = rx.search(expr, i)
        if not m: out.append(expr[i:]); break
        out.append(expr[i:m.start()])
        depth, j = 0, m.end() - 1
        while j < len(expr):
            if expr[j] == '(': depth += 1
            elif expr[j] == ')':
                depth -= 1
                if depth == 0: break
            j += 1
        out.append(repl)
        i = j + 1
    return ''.join(out)


def _first_call_args(expr, name):
    """Args text of the first call `name(` in expr (top-level comma split)."""
    m = re.search(r'\b%s\(' % re.escape(name), expr)
    if not m: return None
    depth, j, start, args = 0, m.end() - 1, m.end(), []
    while j < len(expr):
        c = expr[j]
        if c == '(': depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0: args.append(expr[start:j].strip()); return args
        elif c == ',' and depth == 1: args.append(expr[start:j].strip()); start = j + 1
        j += 1
    return None


def info_of(m):
    i = m.get('info')
    if isinstance(i, str):
        try: i = ast.literal_eval(i)
        except Exception: i = {}
    return i or {}


def pbr_body(glsl, info):
    lines = glsl.splitlines()
    stmts = []                                  # (index, indent, decl, lhs, rhs)
    for k, l in enumerate(lines):
        m = _STMT.match(l)
        if m: stmts.append((k,) + m.groups())
    defs = {s[3]: s for s in stmts if s[2]}    # temp -> its statement
    # cube-dependent temporaries (SSA: in order)
    cubedep = set()
    for k, ind, decl, lhs, rhs in stmts:
        if decl and (_CUBE.search(rhs) or any(t in cubedep for t in _TMP.findall(rhs))): cubedep.add(lhs)
    diff = [s for s in stmts if s[3] == 'o.DiffuseColor']
    out = list(lines)
    stripped = False
    if diff and (_CUBE.search(diff[-1][4]) or any(t in cubedep for t in _TMP.findall(diff[-1][4]))):
        # the cube-dependent temporaries DiffuseColor needs (transitively)
        need, stack = set(), [t for t in _TMP.findall(diff[-1][4]) if t in cubedep]
        while stack:
            t = stack.pop()
            if t in need: continue
            need.add(t)
            stack += [u for u in _TMP.findall(defs[t][4]) if u in cubedep]
        ncName = lambda tn: 't%d' % (100000 + int(tn[1:]))   # (matc's SSA naming: the emulator / backends expect t<N>)
        nc = lambda e: _TMP.sub(lambda mm: ncName(mm.group(0)) if mm.group(0) in need else mm.group(0),
                                _replace_calls(e, _CUBE, 'vec4(0.0)'))
        k, ind, decl, lhs, rhs = diff[-1]
        copies = ['%s%s %s = %s;' % (ind, defs[t][2], ncName(t), nc(defs[t][4])) for t in sorted(need, key=lambda x: defs[x][0])]
        out[k] = '\n'.join(copies + ['%so.DiffuseColor = %s;' % (ind, nc(rhs))])
        stripped = True
    # the roughness parent and the UV its body samples it with
    texs = info.get('textures') or []
    slot_of = {t.get('object'): t.get('slot') for t in texs if t.get('kind', '2d') == '2d' and 'slot' in t}
    def sources(out_name):
        s = [x for x in stmts if x[3] == 'o.' + out_name]
        if not s: return []
        seen, stack, slots = set(), [s[-1][4]], []
        while stack:
            e = stack.pop()
            for a in re.finditer(r'\bwfcSample2D\((\d+),', e): slots.append(int(a.group(1)))
            for t in _TMP.findall(e):
                if t not in seen and t in defs: seen.add(t); stack.append(defs[t][4])
        return sorted(set(slots))
    parent = None
    for out_name in ('SpecularPower', 'SpecularColor', 'DiffuseColor'):
        cand = sorted(o for o, sl in slot_of.items() if sl in sources(out_name))
        if cand: parent = cand[0]; break
    rough_slot = rough_uv = None
    if parent is not None:
        ps = slot_of[parent]
        for k, ind, decl, lhs, rhs in stmts:
            a = _first_call_args(rhs, 'wfcSample2D') if re.search(r'\bwfcSample2D\(%d,' % ps, rhs) else None
            if a and a[0] == str(ps):
                rough_uv = a[1]; break
        if rough_uv is not None:
            rough_slot = 1 + max([t.get('slot', -1) for t in texs] or [-1])
            # (sampled at the end: every temporary the UV uses is defined by then)
            out.append('    wfcPbrRoughMap = uPbrRoughOn != 0 ? wfcSample2D(%d, %s).r : -1.0;' % (rough_slot, rough_uv))
    return '\n'.join(out), dict(roughness_parent=parent, rough_slot=rough_slot, rough_uv=rough_uv, base_stripped=stripped)


def build_map(render_dir, out_dir):
    mats = json.load(open(os.path.join(render_dir, 'materials_glsl.json'), encoding='utf-8'))
    res = {}
    for name, m in sorted(mats.items()):
        if m.get('error') not in (None, 'None') or not m.get('glsl'): continue
        body, meta = pbr_body(m['glsl'], info_of(m))
        meta['glsl'] = body
        res[name] = meta
    os.makedirs(out_dir, exist_ok=True)
    json.dump(res, open(os.path.join(out_dir, 'materials_pbr.json'), 'w', encoding='utf-8'), indent=0)
    return res


def check(res, vectors_path, emu_dir, map_name):
    sys.path.insert(0, emu_dir)
    import numpy as np
    import glsl_emu as E
    V = json.load(open(vectors_path, encoding='utf-8'))
    n = bad = 0; worst = []
    for name, vv in V.items():
        if vv.get('map') != map_name or name not in res: continue
        body = res[name]['glsl']
        # the emulator has no wfcPbrRoughMap: the hook is a separate line (checked by its presence, not numerically)
        body = '\n'.join(l for l in body.splitlines() if 'wfcPbrRoughMap' not in l)
        for vec in vv['vectors']:
            s2 = {int(k): v for k, v in vec['samples2d'].items()}; sc = {int(k): v for k, v in vec['samples_cube'].items()}
            o = E.run(body, E.Material(s2, sc))
            ex = vec['expect']
            got_base = o.get('DiffuseColor', np.zeros(3))[:3]
            got_rough = float(E.roughness_from_power(float(o.get('SpecularPower', np.array([15.0]))[0])))
            got_em = o.get('EmissiveColor', np.zeros(3))[:3]
            err = max(float(np.max(np.abs(got_base - np.asarray(ex['baseColor'])))), abs(got_rough - ex['roughness']),
                      float(np.max(np.abs(got_em - np.asarray(ex['emissive'])))))
            n += 1
            if not err <= 1e-6: bad += 1; worst.append((err, name))
    worst.sort(reverse=True)
    return n, bad, worst[:8]


if __name__ == '__main__':
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    res = build_map(a[0], a[1])
    print('%d materials; %d with the cube reflection stripped from the base colour; %d with a roughness-map hook'
          % (len(res), sum(1 for r in res.values() if r['base_stripped']), sum(1 for r in res.values() if r['rough_slot'] is not None)))
    if '--check' in sys.argv:
        vp = sys.argv[sys.argv.index('--check') + 1]; ed = sys.argv[sys.argv.index('--emu') + 1]
        n, bad, worst = check(res, vp, ed, os.path.basename(os.path.normpath(a[0])))
        print('oracle: %d vectors, %d differ (> 1e-6)' % (n, bad))
        for w in worst: print('  %.3g %s' % w)
