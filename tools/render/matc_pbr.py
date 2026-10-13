"""matc "pbr" backend (M6 Modern materials, PC ADAPTATION): each material's GLSL body (materials_glsl.json, unchanged)
re-emitted for the Modern shading, following Systems' contract (docs/handoff/SYSTEMS_M6_AUDIT.md; tools/systems/m6
glsl_emu.py is the definition):

  baseColor  = DiffuseColor under the view strip, then max(., 0). Every value carries a "depends on the view" flag - set at
               m.cameraVector / m.reflectionVector / m.lightVector and every cube sample, propagated through every operation
               and function (a 2D sample's result is not view-dependent). Rule 1: at a + / - of two vec3 / vec4 operands
               where exactly one depends on the view, the result is the view-independent operand (A + V -> A, V + A -> A,
               A - V -> A, V - A -> -A): the cube reflection added into the diffuse, the anisotropic camera specular of
               WEP_PhongMain / ENV_PhongMain_NS. Rule 2: a view dependence still present at o.DiffuseColor (a multiplicative
               view factor) is replaced by its mean over glsl_emu's 32 fixed directions VIEW_SET (camera = reflection =
               light = the direction; cube samples held at their value), emitted unrolled. Only the base colour is stripped:
               the temporaries it needs that change are re-emitted as t<100000 + N> (rule 2's per-direction copies
               t<10000000 + 100000 i + N>, the sums t<9000000 + i>); every other output keeps the original expressions.
  roughness  = clamp(sqrt(2 / (SpecularPower + 2)), 0.02, 1) per pixel - the Modern shading computes it from o.SpecularPower;
               where the HD pack has a roughness map for the material's roughness parent (the texture feeding SpecularPower,
               else SpecularColor, else DiffuseColor; mask / spec / gloss-named first, then by name - Systems'
               pick_parent), the body also sets wfcPbrRoughMap = that map's .r, sampled with the parent's own UV (a new
               texture slot; the runtime replaces uPbrRoughOn != 0 by whether the map exists).
  environment: wfcPbrEnv = the material's own first cube map along its own direction expression, at wfcPbrEnvLod (from the
               roughness) - the Modern image-based specular (the RT composite replaces it through the cube helpers).
  metallic 0, F0 0.04 (no reviewed metal rows yet). HUD-marker / team-state materials (FriendlyColor, EnemyColor, ...,
  PingOpacity) are never remapped: no entry (the runtime keeps the original program).

Output per map: <out>/materials_pbr.json {material: {glsl, roughness_parent, rough_slot, rough_uv, base_rule, env_slot}}.
Check: --check <m6_vectors.json> --emu <glsl_emu dir>: every pbr body (the wfcPbr* hook lines aside) evaluated with the
oracle's emulator on each vector (runtime parameters set where the vector says) - baseColor (= max(expected, 0)),
roughness and emissive within 1e-6.

  python tools/render/matc_pbr.py <map render dir> <out dir> [--check <m6_vectors.json> --emu <glsl_emu.py dir>]
"""
import ast, json, math, os, re, sys



# ------------------------------------------------------------------ the matc GLSL subset's expressions (as matc_hlsl.py)
class TranslateError(Exception):
    pass


# ------------------------------------------------------------------ lexer / parser (expressions of the matc subset)
_TOK = re.compile(r'\s*(?:(\d+\.\d*(?:[eE][-+]?\d+)?|\.\d+(?:[eE][-+]?\d+)?|\d+(?:[eE][-+]?\d+)?)|([A-Za-z_]\w*)|(.))')


def lex(s):
    out, i = [], 0
    while i < len(s):
        m = _TOK.match(s, i)
        if not m: break
        i = m.end()
        if m.group(1) is not None: out.append(('num', m.group(1)))
        elif m.group(2) is not None: out.append(('id', m.group(2)))
        elif m.group(3) is not None and not m.group(3).isspace(): out.append(('op', m.group(3)))
    return out


class Parser:
    def __init__(self, toks): self.t, self.i = toks, 0

    def peek(self, k=0): return self.t[self.i + k] if self.i + k < len(self.t) else ('eof', '')

    def eat(self, v=None):
        tk = self.peek()
        if v is not None and tk[1] != v: raise TranslateError('expected %r, got %r' % (v, tk[1]))
        self.i += 1
        return tk

    def expr(self):
        a = self.term()
        while self.peek()[1] in ('+', '-'):
            op = self.eat()[1]; a = ('bin', op, a, self.term())
        return a

    def term(self):
        a = self.unary()
        while self.peek()[1] in ('*', '/'):
            op = self.eat()[1]; a = ('bin', op, a, self.unary())
        return a

    def unary(self):
        if self.peek()[1] == '-': self.eat(); return ('neg', self.unary())
        if self.peek()[1] == '+': self.eat(); return self.unary()
        return self.postfix()

    def postfix(self):
        a = self.primary()
        while self.peek()[1] == '.':
            self.eat(); a = ('member', a, self.eat()[1])
        return a

    def primary(self):
        k, v = self.peek()
        if k == 'num': self.eat(); return ('num', v)
        if v == '(':
            self.eat(); e = self.expr(); self.eat(')'); return ('paren', e)
        if k == 'id':
            self.eat()
            if self.peek()[1] == '(':
                self.eat(); args = []
                if self.peek()[1] != ')':
                    args.append(self.expr())
                    while self.peek()[1] == ',': self.eat(); args.append(self.expr())
                self.eat(')')
                return ('call', v, args)
            return ('id', v)
        raise TranslateError('unexpected token %r' % v)

_CUBE = re.compile(r'\bwfcSampleCube(?:Bias)?\(')
_TMP = re.compile(r'\bt\d+\b')
_STMT = re.compile(r'^(\s*)(?:(float|vec[234])\s+)?([\w.]+)\s*=\s*(.*);\s*$')
_RT = re.compile(r'\(uRTSet_(\w+) != 0 \? (uRT_\w+(?:\.[xyzw]+)?) : ')
NEVER_REMAP_RE = re.compile(r'\b(?:uRT_)?(?:FriendlyColor|EnemyColor|AutobotColor|DecepticonColor|TeamID|NeutralColor|Flashing|PingOpacity)\b')
GL_T = {'float': 1, 'vec2': 2, 'vec3': 3, 'vec4': 4}
M_FIELDS = {'uv0': 2, 'uv1': 2, 'subUV2': 3, 'vertexColor': 4, 'worldPosUE': 3, 'cameraVector': 3, 'reflectionVector': 3,
            'lightVector': 3, 'normal': 3, 'time': 1, 'pixelDepth': 1, 'screenPos': 4, 'sceneDepth': 1, 'dynParam': 4}
O_FIELDS = {'Distortion': 3, 'DiffuseColor': 3, 'SpecularColor': 3, 'SpecularPower': 1, 'Normal': 3, 'EmissiveColor': 3,
            'Opacity': 1, 'OpacityMask': 1, 'CustomLighting': 3, 'ScreenAlpha': 1}
VIEW_INPUTS = ('cameraVector', 'reflectionVector', 'lightVector')
# glsl_emu.VIEW_SET: 32 directions on the upper hemisphere (golden spiral)
VIEW_SET = []
for _i in range(32):
    _z = 1.0 - (_i + 0.5) / 32.0
    _r = math.sqrt(max(0.0, 1.0 - _z * _z))
    _phi = 2.399963229728653 * _i
    VIEW_SET.append((_r * math.cos(_phi), _r * math.sin(_phi), _z))


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


def pick_parent(objs):
    """Systems' m6_audit.pick_parent: mask / spec / gloss-named first, then by name."""
    return sorted(objs, key=lambda o: (0 if any(k in o.lower() for k in ('mask', 'spec', 'gloss')) else 1, o.lower()))[0] if objs else None


class Strip:
    """The view-flag walk of one body: per temporary its type, its stripped text (None = unchanged) and its view flag."""
    def __init__(self):
        self.ty, self.view, self.changed, self.rt = {}, {}, {}, []
        self.rename = lambda t: 't%d' % (100000 + int(t[1:]))
        self.inputs = None                         # (rule 2) view inputs replaced by a direction literal
        self.fixed = set()                         # (rule 2) temporaries held at their value (cube samples)
        self.loop_name = None                      # (rule 2) the per-direction copy name of a temporary

    def name(self, t):
        if self.loop_name and t in self.loop_name: return self.loop_name[t]
        return self.rename(t) if self.changed.get(t) else t

    def w(self, n):
        """-> (text, type, view, changed)"""
        k = n[0]
        if k == 'num': return n[1], 1, False, False
        if k == 'paren':
            t, ty, v, c = self.w(n[1]); return '(' + t + ')', ty, v, c
        if k == 'neg':
            t, ty, v, c = self.w(n[1]); return '-' + t, ty, v, c
        if k == 'id':
            nm = n[1]
            if nm in self.ty:
                return self.name(nm), self.ty[nm], self.view[nm], self.name(nm) != nm
            return nm, 'struct', False, False
        if k == 'member':
            b = n[1]
            if b[0] == 'id' and b[1] == 'm':
                if self.inputs is not None and n[2] in VIEW_INPUTS: return self.inputs, 3, True, True
                return 'm.' + n[2], M_FIELDS.get(n[2], 3), n[2] in VIEW_INPUTS, False
            if b[0] == 'id' and b[1] == 'o': return 'o.' + n[2], O_FIELDS.get(n[2], 3), False, False
            t, ty, v, c = self.w(b)
            return t + '.' + n[2], len(n[2]), v, c
        if k == 'bin':
            op = n[1]
            ta, tya, va, ca = self.w(n[2]); tb, tyb, vb, cb = self.w(n[3])
            if op in '+-' and va != vb and isinstance(tya, int) and isinstance(tyb, int) and tya >= 3 and tyb >= 3:
                if va: return ('(' + tb + ')' if op == '+' else '(-(' + tb + '))'), tyb, False, True   # (V + A, V - A)
                return '(' + ta + ')', tya, False, True                                              # (A + V, A - V)
            if tya == 'm2': ty = 2
            elif isinstance(tya, int) and isinstance(tyb, int): ty = max(tya, tyb)
            else: ty = tya if isinstance(tya, int) else tyb
            return '%s %s %s' % (ta, op, tb), ty, va or vb, ca or cb
        if k == 'call':
            f, args = n[1], n[2]
            A = [self.w(a) for a in args]
            txt = lambda: '%s(%s)' % (f, ', '.join(a[0] for a in A))
            anyv = any(a[2] for a in A); anyc = any(a[3] for a in A)
            if f.startswith('wfcRtParam_'):          # a runtime parameter's "set ? value : default" (not view-dependent)
                s, val = self.rt[int(f[len('wfcRtParam_'):])]
                return '(uRTSet_%s != 0 ? %s : %s)' % (s, val, A[0][0]), A[0][1], False, anyc
            if f in GL_T:
                return txt(), GL_T[f], anyv, anyc
            if f == 'mat2': return txt(), 'm2', anyv, anyc
            if f == 'wfcSample2D': return txt(), 4, False, anyc
            if f in ('wfcSampleCube', 'wfcSampleCubeBias'): return txt(), 4, True, anyc
            if f in ('tangentToWorldUE', 'tangentToView', 'tangentToLocal'): return txt(), 3, A[1][2], anyc
            if f == 'wfcNormalNode': return txt(), 3, False, False
            if f in ('dot', 'length', 'distance', 'wfcDepthBiasedAlpha'): return txt(), 1, anyv, anyc
            if f == 'cross': return txt(), 3, anyv, anyc
            if f == 'wfcSceneColor': return txt(), 4, anyv, anyc
            if f == 'wfcScreenUV': return txt(), 2, False, False
            tys = [a[1] for a in A if isinstance(a[1], int)]
            return txt(), max(tys) if tys else 1, anyv, anyc
        raise TranslateError('node %s' % k)


def _parse(rhs, S):
    def sub(m):
        S.rt.append((m.group(1), m.group(2)))
        return 'wfcRtParam_%d(' % (len(S.rt) - 1)
    p = Parser(lex(_RT.sub(sub, rhs)))
    node = p.expr()
    if p.peek()[0] != 'eof': raise TranslateError('trailing tokens: %s' % rhs[:60])
    return node


def pbr_body(glsl, info):
    lines = glsl.splitlines()
    stmts = []                                  # (index, indent, decl, lhs, rhs)
    for k, l in enumerate(lines):
        m = _STMT.match(l)
        if m: stmts.append((k,) + m.groups())
    defs = {s[3]: s for s in stmts if s[2]}
    S = Strip()
    nodes, stext = {}, {}
    out = list(lines)
    base_rule = 0
    diff = [s for s in stmts if s[3] == 'o.DiffuseColor']
    try:
        for k, ind, decl, lhs, rhs in stmts:
            if not decl: continue
            nodes[lhs] = _parse(rhs, S)
            t, ty, v, c = S.w(nodes[lhs])
            S.ty[lhs] = GL_T[decl]; S.view[lhs] = v; S.changed[lhs] = c; stext[lhs] = t
        if diff:
            dnode = _parse(diff[-1][4], S)
            dt, dty, dv, dc = S.w(dnode)
            need, stack = [], [x for x in _TMP.findall(dt)]
            seen = set()
            while stack:                                 # the changed temporaries the stripped base colour reads
                t = stack.pop()
                base = t if t in defs else ('t%d' % (int(t[1:]) - 100000) if t[1:].isdigit() and int(t[1:]) >= 100000 else None)
                if base is None or base in seen or base not in defs: continue
                seen.add(base)
                if S.changed.get(base):
                    need.append(base); stack += _TMP.findall(stext[base])
            k, ind, decl, lhs, rhs = diff[-1]
            copies = ['%s%s %s = %s;' % (ind, defs[t][2], S.rename(t), stext[t]) for t in sorted(need, key=lambda x: defs[x][0])]
            if dv:                                       # rule 2: the mean over VIEW_SET, unrolled
                base_rule = 2
                chain, stack, seen = [], [x for x in _TMP.findall(dt)], set()
                while stack:                             # every temporary of the stripped chain (stripped names resolved)
                    t = stack.pop()
                    b = t if t in defs else ('t%d' % (int(t[1:]) - 100000) if t[1:].isdigit() and int(t[1:]) >= 100000 else None)
                    if b is None or b in seen or b not in defs: continue
                    seen.add(b); chain.append(b); stack += _TMP.findall(stext[b])
                vt = sorted([t for t in chain if S.view[t] and not _CUBE.search(defs[t][4])], key=lambda x: defs[x][0])
                sums = []
                for i, d in enumerate(VIEW_SET):
                    S.inputs = 'vec3(%r, %r, %r)' % d
                    S.loop_name = {t: 't%d' % (10000000 + 100000 * i + int(t[1:])) for t in vt}
                    for t in vt:
                        tt, _, _, _ = S.w(nodes[t])
                        copies.append('%s%s %s = %s;' % (ind, defs[t][2], S.loop_name[t], tt))
                    st, _, _, _ = S.w(dnode)
                    copies.append('%svec3 t%d = %s;' % (ind, 9000000 + i, st))
                    sums.append('t%d' % (9000000 + i))
                S.inputs = None; S.loop_name = None
                dt = '(%s) / 32.0' % ' + '.join(sums)
            elif dc:
                base_rule = 1
            out[k] = '\n'.join(copies + ['%so.DiffuseColor = max(%s, vec3(0.0));' % (ind, dt)])
    except TranslateError:
        raise
    # the roughness parent (Systems' order) and the UV its body samples it with
    texs = info.get('textures') or []
    slot_of = {t.get('object'): t.get('slot') for t in texs if t.get('kind', '2d') == '2d' and 'slot' in t and t.get('object')}
    def sources(out_name):
        s = [x for x in stmts if x[3] == 'o.' + out_name]
        if not s: return []
        seen, stack, slots = set(), [s[-1][4]], []
        while stack:
            e = stack.pop()
            for a in re.finditer(r'\bwfcSample2D\w*\(\s*(\d+)', e): slots.append(int(a.group(1)))
            for t in _TMP.findall(e):
                if t not in seen and t in defs: seen.add(t); stack.append(defs[t][4])
        return sorted(set(slots))
    parent = None
    for out_name in ('SpecularPower', 'SpecularColor', 'DiffuseColor'):
        cand = [o for o, sl in slot_of.items() if sl in sources(out_name)]
        if cand: parent = pick_parent(cand); break
    rough_slot = rough_uv = None
    if parent is not None:
        ps = slot_of[parent]
        for k, ind, decl, lhs, rhs in stmts:
            a = _first_call_args(rhs, 'wfcSample2D') if re.search(r'\bwfcSample2D\(%d,' % ps, rhs) else None
            if a and a[0] == str(ps):
                rough_uv = a[1]; break
        if rough_uv is not None:
            rough_slot = 1 + max([t.get('slot', -1) for t in texs] or [-1])
            out.append('    wfcPbrRoughMap = uPbrRoughOn != 0 ? wfcSample2D(%d, %s).r : -1.0;' % (rough_slot, rough_uv))
    env_slot = None
    for k, ind, decl, lhs, rhs in stmts:
        mc = _CUBE.search(rhs)
        if not mc: continue
        a = _first_call_args(rhs[mc.start():], rhs[mc.start():mc.end() - 1])
        if a and len(a) >= 2:
            env_slot = int(a[0])
            out.append('    wfcPbrEnv = wfcSampleCubeBias(%d, %s, wfcPbrEnvLod(o.SpecularPower)).rgb;' % (env_slot, a[1]))
        break
    return '\n'.join(out), dict(roughness_parent=parent, rough_slot=rough_slot, rough_uv=rough_uv, base_rule=base_rule,
                                env_slot=env_slot)


def build_map(render_dir, out_dir):
    mats = json.load(open(os.path.join(render_dir, 'materials_glsl.json'), encoding='utf-8'))
    res, skipped = {}, {}
    for name, m in sorted(mats.items()):
        if m.get('error') not in (None, 'None') or not m.get('glsl'): continue
        info = info_of(m)
        pr = info.get('params_read', {}) or {}
        if NEVER_REMAP_RE.search(m['glsl']) or NEVER_REMAP_RE.search(json.dumps(info.get('runtime_params', []))) or \
           NEVER_REMAP_RE.search(' '.join(pr.get('Vector', []) + pr.get('Scalar', []))):
            skipped[name] = 'never remapped (HUD marker / team state)'; continue
        try:
            body, meta = pbr_body(m['glsl'], info)
        except TranslateError as ex:
            skipped[name] = 'not translated: %s' % ex; continue
        meta['glsl'] = body
        res[name] = meta
    os.makedirs(out_dir, exist_ok=True)
    json.dump(res, open(os.path.join(out_dir, 'materials_pbr.json'), 'w', encoding='utf-8'), indent=0)
    return res, skipped


def check(res, vectors_path, emu_dir, map_name):
    sys.path.insert(0, emu_dir)
    import numpy as np
    import glsl_emu as E
    V = json.load(open(vectors_path, encoding='utf-8'))
    V = V.get('materials', V) if isinstance(V, dict) and 'materials' in V else V
    n = bad = 0; worst = []; rules = {}
    for name, vv in V.items():
        if not isinstance(vv, dict) or vv.get('map') != map_name or name not in res: continue
        body = '\n'.join(l for l in res[name]['glsl'].splitlines() if 'wfcPbr' not in l)
        for vec in vv['vectors']:
            s2 = {int(k): v for k, v in vec['samples2d'].items()}; sc = {int(k): v for k, v in vec['samples_cube'].items()}
            o = E.run(body, E.Material(s2, sc, rt_value=1.0 if vec.get('runtime_params_set') else None))
            ex = vec['expect']
            got_base = o.get('DiffuseColor', np.zeros(3))[:3]
            want_base = np.maximum(np.asarray(ex['baseColor'], dtype=np.float64), 0.0)
            got_rough = float(E.roughness_from_power(float(o.get('SpecularPower', np.array([15.0]))[0])))
            got_em = o.get('EmissiveColor', np.zeros(3))[:3]
            err = max(float(np.max(np.abs(got_base - want_base))), abs(got_rough - ex['roughness']),
                      float(np.max(np.abs(got_em - np.asarray(ex['emissive'])))))
            n += 1
            rr = (ex.get('base_rule'), res[name].get('base_rule'))
            rules[rr] = rules.get(rr, 0) + 1
            if not err <= 1e-6: bad += 1; worst.append((err, name))
    worst.sort(reverse=True)
    return n, bad, worst[:8], rules


if __name__ == '__main__':
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    res, skipped = build_map(a[0], a[1])
    cnt = {r: sum(1 for x in res.values() if x['base_rule'] == r) for r in (0, 1, 2)}
    print('%d materials (base colour: %d unchanged, %d additive view terms dropped, %d view mean); %d with a roughness-map hook; %d skipped'
          % (len(res), cnt[0], cnt[1], cnt[2], sum(1 for r in res.values() if r['rough_slot'] is not None), len(skipped)))
    for k, v in list(skipped.items())[:5]: print('  skipped %s: %s' % (k, v))
    if '--check' in sys.argv:
        vp = sys.argv[sys.argv.index('--check') + 1]; ed = sys.argv[sys.argv.index('--emu') + 1]
        n, bad, worst, rules = check(res, vp, ed, os.path.basename(os.path.normpath(a[0])))
        print('oracle: %d vectors, %d differ (> 1e-6); (expected rule, emitted rule) counts %s' % (n, bad, rules))
        for w in worst: print('  %.3g %s' % w)
