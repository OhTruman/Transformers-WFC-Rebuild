"""matc HLSL backend (M7 stage 2, PC ADAPTATION): the translated material graphs as HLSL hit-shading functions.

matc.py emits each material instance as one GLSL function body (a closed subset: typed temporaries, vecN constructors,
swizzles, + - * /, a fixed set of intrinsics and wfc* helpers; materials_glsl.json 'glsl'). This backend parses that
body with full type tracking and re-emits it as HLSL with GLSL's semantics kept explicit, so the GL path stays
untouched and both backends evaluate the same graph:

    vecN(s) scalar splat   -> (s).xxx           mod(a, b)        -> wfcMod(a, b)   (a - b * floor(a / b), not fmod)
    mix / fract            -> lerp / frac       mat2(a,b,c,d) * v -> mul(v, float2x2(a, b, c, d))   (GLSL columns)
    float(v) of a vector   -> (v).x             scalar arguments of the component-wise intrinsics splat to the result
    wfcSample2D(k, uv)     -> wfcTex2D(m, k, uv)        wfcSampleCube(Bias)(k, d[, b]) -> wfcTexCube(m, k, d, b)

Runtime character parameters (uRTSet_X != 0 ? uRT_X : authored) keep the authored value (world hits).
Output per map: one HLSL file with   void wfcMat_<i>(inout MatIn m, out MatOut o)   per material and an uber-switch
    void wfcEvalMaterial(uint id, inout MatIn m, out MatOut o)
(inline RayQuery has no callable shaders), plus a JSON manifest (id -> material name, texture slots, blend / lighting).
The struct / helper definitions the functions use live in src/render/d3d12/shaders/rt_material.hlsli.

  python tools/render/matc_hlsl.py <map render dir> <out dir> [--world-only] [--compile <dxc.exe> <shader dir>]
(--compile: the reflection pass with this map's materials, rt_reflect.hlsl -DWFC_RT_MATERIALS -> reflect_mat.cso,
 loaded at runtime from WFC_RT_MATDIR/<map>/ with materials_rt.txt)
"""
import json, os, re, sys

M_FIELDS = {'uv0': 2, 'uv1': 2, 'subUV2': 3, 'vertexColor': 4, 'worldPosUE': 3, 'cameraVector': 3,
            'reflectionVector': 3, 'normal': 3, 'time': 1, 'pixelDepth': 1, 'screenPos': 4, 'sceneDepth': 1,
            'dynParam': 4}
O_FIELDS = {'Distortion': 3, 'DiffuseColor': 3, 'SpecularColor': 3, 'SpecularPower': 1, 'Normal': 3,
            'EmissiveColor': 3, 'Opacity': 1, 'OpacityMask': 1, 'CustomLighting': 3, 'ScreenAlpha': 1}
GL_T = {'float': 1, 'vec2': 2, 'vec3': 3, 'vec4': 4}
HL_T = {1: 'float', 2: 'float2', 3: 'float3', 4: 'float4'}


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


# ------------------------------------------------------------------ typed emission
def splat(code, t, n):
    if t == n: return code
    if t == 1: return '(%s).%s' % (code, 'x' * n)
    raise TranslateError('cannot splat a %d-vector to %d' % (t, n))


class Emitter:
    def __init__(self):
        self.vars = {}

    def ty_member(self, base_t, name):
        if not re.fullmatch(r'[xyzw]{1,4}|[rgba]{1,4}', name): raise TranslateError('member .%s' % name)
        return len(name)

    def e(self, n):
        """-> (hlsl code, type); type 1..4, or 'm2' for a mat2."""
        k = n[0]
        if k == 'num':
            v = n[1]
            if not re.search(r'[.eE]', v): v += '.0'
            if v.startswith('.'): v = '0' + v
            if v.endswith('.'): v += '0'
            return v, 1
        if k == 'paren':
            c, t = self.e(n[1]); return '(%s)' % c, t
        if k == 'neg':
            c, t = self.e(n[1]); return '-%s' % c, t
        if k == 'id':
            nm = n[1]
            if nm in self.vars: return nm, self.vars[nm]
            if nm in ('m', 'o'): return nm, 'struct'
            raise TranslateError('unknown identifier %s' % nm)
        if k == 'member':
            base = n[1]
            if base[0] == 'id' and base[1] == 'm' and n[2] in M_FIELDS: return 'm.' + n[2], M_FIELDS[n[2]]
            if base[0] == 'id' and base[1] == 'o' and n[2] in O_FIELDS: return 'o.' + n[2], O_FIELDS[n[2]]
            c, t = self.e(base)
            return '%s.%s' % (c, n[2]), self.ty_member(t, n[2])
        if k == 'bin':
            op = n[1]
            a, ta = self.e(n[2]); b, tb = self.e(n[3])
            if ta == 'm2':
                if op != '*' or tb != 2: raise TranslateError('mat2 %s' % op)
                return 'mul(%s, %s)' % (b, a), 2           # GLSL mat2(cols) * v == mul(v, float2x2(same values))
            if ta == tb or ta == 1 or tb == 1: return '%s %s %s' % (a, op, b), max(ta, tb)
            raise TranslateError('operand sizes %s %s %s' % (ta, op, tb))
        if k == 'call':
            return self.call(n[1], n[2])
        raise TranslateError('node %s' % k)

    def call(self, f, args):
        A = [self.e(a) for a in args]
        if f in GL_T:
            n = GL_T[f]
            if n == 1:
                c, t = A[0]
                return (c if t == 1 else '(%s).x' % c), 1
            if len(A) == 1:
                c, t = A[0]
                if t == 1: return splat(c, 1, n), n
                if t == n: return c, n
                if t > n: return '(%s).%s' % (c, 'xyzw'[:n]), n
                raise TranslateError('%s from a %d-vector' % (f, t))
            if sum(t for _, t in A) != n: raise TranslateError('%s(%s) component count' % (f, [t for _, t in A]))
            return '%s(%s)' % (HL_T[n], ', '.join(c for c, _ in A)), n
        if f == 'mat2':
            if len(A) != 4 or any(t != 1 for _, t in A): raise TranslateError('mat2 form')
            return 'float2x2(%s)' % ', '.join(c for c, _ in A), 'm2'
        if f in ('sin', 'cos', 'tan', 'asin', 'acos', 'atan', 'sqrt', 'abs', 'floor', 'ceil', 'normalize', 'sign', 'exp',
                 'exp2', 'log', 'log2'):
            return '%s(%s)' % (f, A[0][0]), A[0][1]
        if f == 'length': return 'length(%s)' % A[0][0], 1
        if f == 'distance': return 'distance(%s, %s)' % (A[0][0], A[1][0]), 1
        if f in ('step', 'smoothstep'):
            n = max(t for _, t in A)
            return '%s(%s)' % (f, ', '.join(splat(c, t, n) for c, t in A)), n
        if f == 'fract': return 'frac(%s)' % A[0][0], A[0][1]
        if f in ('mix', 'clamp', 'pow', 'max', 'min', 'mod'):
            n = max(t for _, t in A)
            hf = {'mix': 'lerp', 'mod': 'wfcMod'}.get(f, f)
            return '%s(%s)' % (hf, ', '.join(splat(c, t, n) for c, t in A)), n
        if f == 'dot':
            (a, ta), (b, tb) = A
            n = max(ta, tb)
            return 'dot(%s, %s)' % (splat(a, ta, n), splat(b, tb, n)), 1
        if f == 'cross': return 'cross(%s, %s)' % (A[0][0], A[1][0]), 3
        if f == 'wfcSample2D':
            if args[0][0] != 'num': raise TranslateError('dynamic texture slot')
            return 'wfcTex2D(m, %d, %s)' % (int(float(args[0][1])), A[1][0]), 4
        if f in ('wfcSampleCube', 'wfcSampleCubeBias'):
            if args[0][0] != 'num': raise TranslateError('dynamic texture slot')
            bias = A[2][0] if f == 'wfcSampleCubeBias' else '0.0'
            return 'wfcTexCube(m, %d, %s, %s)' % (int(float(args[0][1])), A[1][0], bias), 4
        if f in ('tangentToWorldUE', 'tangentToView', 'tangentToLocal'): return '%s(m, %s)' % (f, A[1][0]), 3
        if f == 'wfcNormalNode': return 'wfcNormalNode(m)', 3
        if f == 'wfcDepthBiasedAlpha': return 'wfcDepthBiasedAlpha(m, %s, %s, %s)' % tuple(c for c, _ in A[1:]), 1
        if f == 'wfcSceneColor': return 'wfcSceneColor(m, %s)' % A[0][0], 4
        if f == 'wfcScreenUV': return 'wfcScreenUV(m)', 2
        raise TranslateError('unsupported function %s' % f)


_RT_PARAM = re.compile(r'\(uRTSet_\w+ != 0 \? uRT_\w+(?:\.[xyzw]+)? : ')


def translate_body(glsl):
    """matc GLSL body -> HLSL body (statements, 4-space indented)."""
    E = Emitter()
    out = []
    for line in glsl.splitlines():
        s = line.strip()
        if not s or s.startswith('//'): continue
        s = _RT_PARAM.sub('(', s)
        if not s.endswith(';'): raise TranslateError('statement: %s' % s)
        s = s[:-1]
        if re.fullmatch(r'wfcSetNormal\(m, o\.Normal\)', s):
            out.append('    wfcSetNormal(m, o.Normal);'); continue
        mm = re.match(r'(?:(float|vec[234])\s+)?([\w.]+)\s*=\s*(.*)$', s)
        if not mm: raise TranslateError('statement: %s' % s)
        decl, lhs, rhs = mm.groups()
        p = Parser(lex(rhs)); node = p.expr()
        if p.peek()[0] != 'eof': raise TranslateError('trailing tokens in %s' % rhs)
        code, t = E.e(node)
        if t == 'm2': raise TranslateError('mat2 value')
        if decl:
            n = GL_T[decl]
            if t != n and t != 1: raise TranslateError('%s = %d-vector' % (decl, t))
            E.vars[lhs] = n
            out.append('    %s %s = %s;' % (HL_T[n], lhs, splat(code, t, n)))
        else:
            if lhs.startswith('o.') and lhs[2:] in O_FIELDS: n = O_FIELDS[lhs[2:]]
            elif lhs.startswith('m.') and lhs[2:] in M_FIELDS: n = M_FIELDS[lhs[2:]]
            elif lhs in E.vars: n = E.vars[lhs]
            else: raise TranslateError('assignment to %s' % lhs)
            if t != n and t != 1: raise TranslateError('%s = %d-vector' % (lhs, t))
            out.append('    %s = %s;' % (lhs, splat(code, t, n)))
    return '\n'.join(out)


# ------------------------------------------------------------------ per map
def info_of(m):
    i = m.get('info')
    if isinstance(i, str):
        try:
            import ast
            i = ast.literal_eval(i)
        except Exception:
            i = {}
    return i or {}


def build_map(render_dir, out_dir, world_only=False):
    mats = json.load(open(os.path.join(render_dir, 'materials_glsl.json'), encoding='utf-8'))
    names, funcs, manifest, failed = [], [], [], {}
    for name in sorted(mats):
        m = mats[name]
        if m.get('error') not in (None, 'None') or not m.get('glsl'): continue
        inf = info_of(m)
        blend = inf.get('blend_mode', 'BLEND_Opaque')
        if world_only and blend not in ('BLEND_Opaque', 'BLEND_Masked'): continue
        try:
            body = translate_body(m['glsl'])
        except TranslateError as ex:
            failed[name] = str(ex); continue
        k = len(names)
        names.append(name)
        funcs.append('void wfcMat_%d(inout MatIn m, out MatOut o) {   // %s\n    o = (MatOut)0;\n%s\n}\n' % (k, name, body))
        manifest.append({'id': k, 'name': name, 'blend_mode': blend, 'lighting_model': inf.get('lighting_model'),
                         'two_sided': inf.get('two_sided'), 'opacity_mask_clip': inf.get('opacity_mask_clip'),
                         'textures': [{kk: t.get(kk) for kk in ('slot', 'kind', 'file', 'srgb', 'object') if kk in t}
                                      for t in (inf.get('textures') or [])]})
    sw = ['void wfcEvalMaterial(uint id, inout MatIn m, out MatOut o) {', '    switch (id) {']
    sw += ['    case %du: wfcMat_%d(m, o); break;' % (k, k) for k in range(len(names))]
    sw += ['    default: o = (MatOut)0; o.DiffuseColor = float3(0.5, 0.5, 0.5); o.Normal = float3(0, 0, 1); break;', '    }', '}']
    os.makedirs(out_dir, exist_ok=True)
    hdr = ('// Generated by tools/render/matc_hlsl.py from %s/materials_glsl.json (%d materials%s) - do not edit.\n'
           '#include "rt_material.hlsli"\n\n' % (os.path.basename(os.path.normpath(render_dir)), len(names),
                                                 ', opaque / masked only' if world_only else ''))
    open(os.path.join(out_dir, 'materials_rt.hlsl'), 'w', encoding='utf-8', newline='\n').write(hdr + '\n'.join(funcs) + '\n' + '\n'.join(sw) + '\n')
    json.dump({'materials': manifest, 'failed': failed}, open(os.path.join(out_dir, 'materials_rt.json'), 'w', encoding='utf-8'), indent=1)
    # the runtime's lookup (render/gl/WfcRt.cpp rtBuildHit): one 'id<TAB>material name' line per material
    open(os.path.join(out_dir, 'materials_rt.txt'), 'w', encoding='utf-8', newline='\n').write(
        ''.join('%d\t%s\n' % (k, n) for k, n in enumerate(names)))
    return len(names), failed


def compile_reflect(out_dir, dxc, shader_dir):
    """The map's reflection pass with its materials (rt_reflect.hlsl, WFC_RT_MATERIALS) -> <out>/reflect_mat.cso."""
    import subprocess
    cso = os.path.join(out_dir, 'reflect_mat.cso')
    r = subprocess.run([dxc, '-nologo', '-T', 'cs_6_5', '-E', 'main', '-O3', '-DWFC_RT_MATERIALS=1', '-I', out_dir,
                        '-I', shader_dir, '-Fo', cso, os.path.join(shader_dir, 'rt_reflect.hlsl')],
                       capture_output=True, text=True)
    if r.returncode != 0: raise RuntimeError('DXC failed:\n' + (r.stderr or r.stdout)[-4000:])
    return os.path.getsize(cso)


if __name__ == '__main__':
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    n, failed = build_map(a[0], a[1], '--world-only' in sys.argv)
    print('%d materials translated, %d failed' % (n, len(failed)))
    if '--compile' in sys.argv:          # --compile <dxc.exe> <shader dir>
        i = sys.argv.index('--compile')
        print('reflect_mat.cso: %d bytes' % compile_reflect(a[1], sys.argv[i + 1], sys.argv[i + 2]))
    for k, v in list(failed.items())[:20]: print('  FAILED %s: %s' % (k, v))
