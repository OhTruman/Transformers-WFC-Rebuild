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
  environment: wfcPbrEnv = the material's own first cube map along its own direction expression, at the roughness level
               (wfcSampleCubeRough: textureLod at roughness x (the cube's levels - 1)) - the Modern image-based specular.
  metallic 0, F0 0.04 (no reviewed metal rows yet). HUD-marker / team-state materials (FriendlyColor, EnemyColor, ...,
  PingOpacity) are never remapped: no entry (the runtime keeps the original program).

Output per map: <out>/materials_pbr.json {material: {glsl, roughness_parent, rough_slot, rough_uv, base_rule, env_slot}}.
Check: --check <m6_vectors.json> --emu <glsl_emu dir>: every pbr body (the wfcPbr* hook lines aside) evaluated with the
oracle's emulator on each vector (runtime parameters set where the vector says) - baseColor (= max(expected, 0)),
roughness and emissive within 1e-6.

  python tools/render/matc_pbr.py <map render dir> <out dir> [--hooks <m6_mapping.json>] [--check <m6_vectors.json> --emu <glsl_emu dir>]
  (--hooks: Systems' m6_audit.py output; its per-material pbr_hooks rows emit wfcPbrMetallic / wfcPbrF0 and, where given,
   the hook's base colour - the robots' metallic paint, the weapons' metal mask)
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


class F:
    """glsl_emu's per-value flags: view (camera / reflection / light vectors or a cube sample), cam (the vectors
    themselves), tex (a 2D sample), bc (a scalar broadcast into a vector: a factor); g = a GLSL value object (a bare number
    literal is not one: it takes the other operand's flags)."""
    __slots__ = ('view', 'cam', 'tex', 'bc', 'g')
    def __init__(self, view=False, cam=False, tex=False, bc=False, g=True):
        self.view, self.cam, self.tex, self.bc, self.g = view, cam, tex, bc, g
    def copy(self): return F(self.view, self.cam, self.tex, self.bc, self.g)


def _join(a, b):
    """glsl_emu G._join: or the flags, and the broadcast flag (a number literal: the other operand's flags)."""
    if not a.g and not b.g: return F(g=False)
    if not b.g: return a.copy()
    if not a.g: return b.copy()
    return F(a.view or b.view, a.cam or b.cam, a.tex or b.tex, a.bc and b.bc)


def _setflags(fs, broadcast=False):
    """glsl_emu _setflags (functions, constructors): or over the value arguments; bc = all of them broadcast (+ a ctor's own)."""
    gs = [f for f in fs if f.g]
    return F(any(f.view for f in gs), any(f.cam for f in gs), any(f.tex for f in gs),
             broadcast or (bool(gs) and all(f.bc for f in gs)))


class Strip:
    """The flag walk of one body (glsl_emu's strip_view): per temporary its type, flags, stripped text and whether it
    changed; rule 3's cube means are re-emitted copies (mean mode: cube samples -> wfcSampleCubeMean(k))."""
    def __init__(self):
        self.ty, self.fl, self.changed, self.rt = {}, {}, {}, []
        self.nodes = {}
        self.rename = lambda t: 't%d' % (100000 + int(t[1:]))
        self.mean_name = lambda t: 't%d' % (20000000 + int(t[1:]))
        self.mean_text = {}                       # temporary -> its mean-mode text (rule 3)
        self.meanmode = False
        self.inputs = None                         # (rule 2) view inputs replaced by a direction literal
        self.loop_name = None                      # (rule 2) the per-direction copy name of a temporary

    def name(self, t):
        if self.loop_name and t in self.loop_name: return self.loop_name[t]
        if self.meanmode and self.fl[t].view and not self.fl[t].cam:
            if t not in self.mean_text:
                self.mean_text[t] = None           # (recursion guard)
                self.mean_text[t] = self.w(self.nodes[t])[0]
            return self.mean_name(t)
        return self.rename(t) if self.changed.get(t) else t

    def mean(self, node):
        old = self.meanmode; self.meanmode = True
        try: return self.w(node)[0]
        finally: self.meanmode = old

    def w(self, n):
        """-> (text, type, flags, changed)"""
        k = n[0]
        if k == 'num': return n[1], 1, F(g=False), False
        if k == 'paren':
            t, ty, f, c = self.w(n[1]); return '(' + t + ')', ty, f, c
        if k == 'neg':
            t, ty, f, c = self.w(n[1]); return '-' + t, ty, f, c
        if k == 'id':
            nm = n[1]
            if nm in self.ty:
                x = self.name(nm)
                return x, self.ty[nm], self.fl[nm].copy(), x != nm
            return nm, 'struct', F(), False
        if k == 'member':
            b = n[1]
            if b[0] == 'id' and b[1] == 'm':
                if n[2] in VIEW_INPUTS:
                    if self.inputs is not None: return self.inputs, 3, F(True, True), True
                    return 'm.' + n[2], 3, F(True, True), False
                return 'm.' + n[2], M_FIELDS.get(n[2], 3), F(), False
            if b[0] == 'id' and b[1] == 'o': return 'o.' + n[2], O_FIELDS.get(n[2], 3), F(), False
            t, ty, f, c = self.w(b)
            return t + '.' + n[2], len(n[2]), f, c
        if k == 'bin':
            op = n[1]
            ta, tya, fa, ca = self.w(n[2]); tb, tyb, fb, cb = self.w(n[3])
            sz = lambda ty: ty if isinstance(ty, int) else 0
            if op in '+-' and fa.g and fb.g and fa.view != fb.view and sz(tya) >= 3 and sz(tyb) >= 3:     # rule 1
                if fa.view: return ('(' + tb + ')' if op == '+' else '(-(' + tb + '))'), tyb, F(tex=fb.tex), True
                return '(' + ta + ')', tya, F(tex=fa.tex), True
            if tya == 'm2':
                return '%s %s %s' % (ta, op, tb), 2, _setflags([fa, fb]), ca or cb
            ty = max(sz(tya), sz(tyb)) or (tya if isinstance(tya, int) else tyb)
            if op == '*' and fa.g and fb.g and fa.view != fb.view:                                      # rule 3
                vf, cf, cty = (fa, fb, tyb) if fa.view else (fb, fa, tya)
                if not vf.cam and cf.tex and not cf.bc and sz(cty) >= 3:
                    if fa.view: return '%s * %s' % (self.mean(n[2]), tb), ty, F(tex=True), True
                    return '%s * %s' % (ta, self.mean(n[3])), ty, F(tex=True), True
            return '%s %s %s' % (ta, op, tb), ty, _join(fa, fb), ca or cb
        if k == 'call':
            f, args = n[1], n[2]
            A = [self.w(a) for a in args]
            txt = lambda: '%s(%s)' % (f, ', '.join(a[0] for a in A))
            anyc = any(a[3] for a in A)
            fl = [a[2] for a in A]
            if f.startswith('wfcRtParam_'):          # a runtime parameter's "set ? value : default" (no flags)
                s, val = self.rt[int(f[len('wfcRtParam_'):])]
                return '(uRTSet_%s != 0 ? %s : %s)' % (s, val, A[0][0]), A[0][1], F(g=A[0][2].g), anyc
            if f in GL_T:
                broadcast = f != 'float' and len(A) == 1 and A[0][1] == 1
                return txt(), GL_T[f], _setflags(fl, broadcast), anyc
            if f == 'mat2': return txt(), 'm2', _setflags(fl), anyc
            if f == 'wfcSample2D': return txt(), 4, F(tex=True), anyc
            if f in ('wfcSampleCube', 'wfcSampleCubeBias'):
                if self.meanmode: return 'wfcSampleCubeMean(%s)' % A[0][0], 4, F(view=True), True
                return txt(), 4, F(view=True), anyc
            if f in ('tangentToWorldUE', 'tangentToView', 'tangentToLocal'): return txt(), 3, A[1][2].copy(), anyc
            if f == 'wfcNormalNode': return txt(), 3, F(), False
            if f in ('dot', 'length', 'distance', 'wfcDepthBiasedAlpha'): return txt(), 1, _setflags(fl), anyc
            if f == 'cross': return txt(), 3, _setflags(fl), anyc
            if f == 'wfcSceneColor': return txt(), 4, _setflags(fl), anyc
            if f == 'wfcScreenUV': return txt(), 2, F(), False
            tys = [a[1] for a in A if isinstance(a[1], int)]
            return txt(), max(tys) if tys else 1, _setflags(fl), anyc
        raise TranslateError('node %s' % k)


def _parse(rhs, S):
    def sub(m):
        S.rt.append((m.group(1), m.group(2)))
        return 'wfcRtParam_%d(' % (len(S.rt) - 1)
    p = Parser(lex(_RT.sub(sub, rhs)))
    node = p.expr()
    if p.peek()[0] != 'eof': raise TranslateError('trailing tokens: %s' % rhs[:60])
    return node


def glsl_hash(glsl):
    """A material body's hash for Systems' hook rows (which name its temporaries by number): sha1 of the UTF-8 text."""
    import hashlib
    return hashlib.sha1(glsl.encode('utf-8')).hexdigest()


def check_hook(hook, glsl, S, defs, diff_line):
    """True, or why the hook row is rejected: its glsl hash (when the row has one) must match this body; each expression
    must parse, name only temporaries defined before the base-colour line, and have the right type."""
    h = hook.get('glsl_hash') or hook.get('glsl_sha1')
    if h and h != glsl_hash(glsl): return 'glsl hash mismatch (the row was made for another body)'
    if not h:   # (rows name temporaries by number: without a hash a changed body is not detected - warned, still used)
        print('  WARNING hook row without glsl_hash (unverified against this body)', file=sys.stderr)
    want = {'metallic': (1,), 'f0': (3, 4), 'baseColor': (3, 4)}
    for key, tys in want.items():
        e = hook.get(key)
        if e is None or (key == 'baseColor' and not e):
            if key == 'baseColor': continue
            return '%s missing' % key
        try:
            node = _parse(str(e), S)
        except TranslateError as ex:
            return '%s does not parse: %s' % (key, ex)
        for t in _TMP.findall(str(e)):
            if t not in defs or defs[t][0] >= diff_line: return '%s names %s, not defined before the base colour' % (key, t)
        bad = [w for w in re.findall(r'\b[A-Za-z_]\w*\b', str(e))
               if not _TMP.fullmatch(w) and w not in GL_T and w not in ('clamp', 'max', 'min', 'mix', 'dot', 'abs', 'saturate')
               and not re.fullmatch(r'[xyzwrgba]{1,4}', w) and not re.fullmatch(r'\d', w)]
        if bad: return '%s names unknown identifiers %s' % (key, bad)
        ty = S.w(node)[1]
        if ty not in tys: return '%s has type %s (expected %s)' % (key, ty, ' / '.join('vec%d' % x if x > 1 else 'float' for x in tys))
    return True


def pbr_body(glsl, info, hook=None):
    lines = glsl.splitlines()
    stmts = []                                  # (index, indent, decl, lhs, rhs)
    for k, l in enumerate(lines):
        m = _STMT.match(l)
        if m: stmts.append((k,) + m.groups())
    defs = {s[3]: s for s in stmts if s[2]}
    S = Strip()
    hook_rejected = None
    nodes, stext = S.nodes, {}
    out = list(lines)
    base_rule = 0
    diff = [s for s in stmts if s[3] == 'o.DiffuseColor']
    def base_of(nm):
        """a copy name -> (family, original temporary): 's' stripped, 'm' mean (rule 3), None original"""
        if not nm[1:].isdigit(): return None, None
        v = int(nm[1:])
        if 20000000 <= v < 30000000: return 'm', 't%d' % (v - 20000000)
        if 100000 <= v < 10000000: return 's', 't%d' % (v - 100000)
        return None, nm
    for k, ind, decl, lhs, rhs in stmts:
        if not decl: continue
        nodes[lhs] = _parse(rhs, S)
        tx, ty, fl, c = S.w(nodes[lhs])
        S.ty[lhs] = GL_T[decl]; S.fl[lhs] = fl; S.changed[lhs] = c; stext[lhs] = tx
    if diff:
        dnode = _parse(diff[-1][4], S)
        dt, dty, dfl, dc = S.w(dnode)
        need, stack, seen = [], list(_TMP.findall(dt)), set()
        while stack:                                     # the copies the stripped base colour reads (both families)
            fam, tb = base_of(stack.pop())
            if fam is None or tb not in defs or (fam, tb) in seen: continue
            seen.add((fam, tb))
            need.append((fam, tb))
            stack += _TMP.findall(stext[tb] if fam == 's' else S.mean_text[tb])
        k, ind, decl, lhs, rhs = diff[-1]
        copies = ['%s%s %s = %s;' % (ind, defs[tb][2], S.rename(tb) if fam == 's' else S.mean_name(tb),
                                      stext[tb] if fam == 's' else S.mean_text[tb])
                  for fam, tb in sorted(need, key=lambda x: defs[x[1]][0])]
        if dfl.view:                                     # rule 2: the mean over VIEW_SET, unrolled
            base_rule = 2
            chain, stack, seen = [], list(_TMP.findall(dt)), set()
            while stack:
                fam, tb = base_of(stack.pop())
                if tb not in defs or tb in seen: continue
                seen.add(tb); chain.append(tb); stack += _TMP.findall(stext[tb])
            vt = sorted([x for x in chain if S.fl[x].view and not _CUBE.search(defs[x][4])], key=lambda x: defs[x][0])
            sums = []
            for i, d in enumerate(VIEW_SET):
                S.inputs = 'vec3(%r, %r, %r)' % d
                S.loop_name = {x: 't%d' % (10000000 + 100000 * i + int(x[1:])) for x in vt}
                for x in vt:
                    copies.append('%s%s %s = %s;' % (ind, defs[x][2], S.loop_name[x], S.w(nodes[x])[0]))
                copies.append('%svec3 t%d = %s;' % (ind, 9000000 + i, S.w(dnode)[0]))
                sums.append('t%d' % (9000000 + i))
            S.inputs = None; S.loop_name = None
            dt = '(%s) / 32.0' % ' + '.join(sums)
        elif dc:
            base_rule = 1
        lines_out = copies + ['%so.DiffuseColor = max(%s, vec3(0.0));' % (ind, dt)]
        # Systems' metallic hook rows (m6_mapping.json pbr_hooks: metallic_paint / weapon_metal_mask): metallic, F0
        # (unmixed; the shading mixes F0 = mix(0.04, f0, metallic) and scales the diffuse by 1 - metallic) and, where
        # given, the base colour (the robots' tinted-paint temporary instead of the stripped base)
        hook_ok = None
        if hook:
            hook_ok = check_hook(hook, glsl, S, defs, diff[-1][0])
        if hook and hook_ok is not True:
            hook_rejected = hook_ok
            hook = None                                  # (rejected: the material keeps the dielectric Modern mapping)
        if hook:
            def v3(e):                                   # (any vec4-typed expression as the vec3 the hooks mean)
                e = str(e).strip()
                return '(%s).xyz' % e if S.w(_parse(e, S))[1] == 4 else e
            if hook.get('baseColor'):
                lines_out.append('%so.DiffuseColor = max(%s, vec3(0.0));' % (ind, v3(hook['baseColor'])))
            lines_out.append('%swfcPbrMetallic = %s;' % (ind, hook['metallic']))
            lines_out.append('%swfcPbrF0 = max(%s, vec3(0.0));' % (ind, v3(hook['f0'])))   # (the paint can go negative: clamped like the base)
        out[k] = '\n'.join(lines_out)
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
            out.append('    wfcPbrEnv = wfcSampleCubeRough(%d, %s, wfcPbrRoughness(o.SpecularPower)).rgb;' % (env_slot, a[1]))
        break
    meta = dict(roughness_parent=parent, rough_slot=rough_slot, rough_uv=rough_uv, base_rule=base_rule, env_slot=env_slot)
    if hook: meta['hook'] = True                         # (only when true: output without --hooks stays byte-identical)
    if hook_rejected: meta['hook_rejected'] = hook_rejected
    return '\n'.join(out), meta


def load_hooks(mapping_path):
    """Systems' m6_mapping.json -> {material: pbr_hooks} (rows with a metallic hook only)."""
    M = json.load(open(mapping_path, encoding='utf-8'))
    rows = M['rows'] if isinstance(M, dict) and 'rows' in M else (M if isinstance(M, list) else list(M.values()))
    return {r['material']: r['pbr_hooks'] for r in rows if isinstance(r, dict) and r.get('pbr_hooks')}


def build_map(render_dir, out_dir, hooks=None):
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
            body, meta = pbr_body(m['glsl'], info, (hooks or {}).get(name))
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
        body = '\n'.join(l.replace('wfcPbrMetallic =', 'o.PbrMetallic =').replace('wfcPbrF0 =', 'o.PbrF0 =')
                         for l in res[name]['glsl'].splitlines()
                         if 'wfcPbr' not in l or l.strip().startswith(('wfcPbrMetallic =', 'wfcPbrF0 =')))
        body = re.sub(r'wfcSampleCubeMean\((\d+)\)', r'wfcSampleCube(\1, vec3(0.0, 0.0, 1.0))', body)   # (the emulator's cube value is its mean)
        for vec in vv['vectors']:
            s2 = {int(k): v for k, v in vec['samples2d'].items()}; sc = {int(k): v for k, v in vec['samples_cube'].items()}
            o = E.run(body, E.Material(s2, sc, rt_value=1.0 if vec.get('runtime_params_set') else None))
            ex = vec['expect']
            got_base = o.get('DiffuseColor', np.zeros(3))[:3]
            want_base = np.maximum(np.asarray(ex['baseColor'], dtype=np.float64), 0.0)
            got_rough = float(E.roughness_from_power(float(o.get('SpecularPower', np.array([15.0]))[0])))
            got_em = o.get('EmissiveColor', np.zeros(3))[:3]
            got_m = float(o.get('PbrMetallic', np.array([0.0]))[0])
            got_f0 = o.get('PbrF0', np.array([0.04, 0.04, 0.04]))[:3]
            err = max(float(np.max(np.abs(got_base - want_base))), abs(got_rough - ex['roughness']),
                      float(np.max(np.abs(got_em - np.asarray(ex['emissive'])))),
                      abs(got_m - float(ex.get('metallic', 0.0))),
                      float(np.max(np.abs(got_f0 - np.asarray(ex.get('f0', [0.04, 0.04, 0.04]), dtype=np.float64)))))
            n += 1
            rr = (ex.get('base_rule'), res[name].get('base_rule'))
            rules[rr] = rules.get(rr, 0) + 1
            if not err <= 1e-6: bad += 1; worst.append((err, name))
    worst.sort(reverse=True)
    return n, bad, worst[:8], rules


if __name__ == '__main__':
    hooks = load_hooks(sys.argv[sys.argv.index('--hooks') + 1]) if '--hooks' in sys.argv else None
    a = [x for i, x in enumerate(sys.argv[1:], 1) if not x.startswith('--') and sys.argv[i - 1] not in ('--hooks', '--check', '--emu')]
    res, skipped = build_map(a[0], a[1], hooks)
    cnt = {r: sum(1 for x in res.values() if x['base_rule'] == r) for r in (0, 1, 2)}
    for k, v in res.items():
        if v.get('hook_rejected'): print('  WARNING hook rejected for %s: %s' % (k, v['hook_rejected']))
    print('%d materials (base colour: %d unchanged, %d additive view terms dropped, %d view mean); %d with a roughness-map hook; %d metallic hooks; %d skipped'
          % (len(res), cnt[0], cnt[1], cnt[2], sum(1 for r in res.values() if r['rough_slot'] is not None),
             sum(1 for r in res.values() if r.get('hook')), len(skipped)))
    for k, v in list(skipped.items())[:5]: print('  skipped %s: %s' % (k, v))
    if '--check' in sys.argv:
        vp = sys.argv[sys.argv.index('--check') + 1]; ed = sys.argv[sys.argv.index('--emu') + 1]
        n, bad, worst, rules = check(res, vp, ed, os.path.basename(os.path.normpath(a[0])))
        print('oracle: %d vectors, %d differ (> 1e-6); (expected rule, emitted rule) counts %s' % (n, bad, rules))
        for w in worst: print('  %.3g %s' % w)
