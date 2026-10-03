"""WFC material graph -> GLSL translator (offline), mirroring UE3's HLSLMaterialTranslator.

The ORIGINAL cooked material expression graphs (master Material + WFC MaterialFunctions + the
MaterialInstanceConstant parameter/static-switch/TextureSet values) are the specification. For each
material instance we evaluate static switches, inline functions, keep WFC's HLSL ShaderCode
snippets (translated HLSL->GLSL), and emit one GLSL function:

    void wfcMaterial(in MatIn m, out MatOut o)

computing the UE3 material inputs (DiffuseColor, SpecularColor, SpecularPower, Normal [tangent
space], EmissiveColor, Opacity, OpacityMask). Scalar/vector parameters are folded to constants
(per instance), textures become sampler slots listed in the output JSON.
"""
import json, math, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue3obj import Repo, struct_dict, tags_to_dict  # noqa: E402
import hlslexpr  # noqa: E402

_VCP = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vector_channel_proven.txt')
VECTOR_CHANNEL_PROVEN = {l.split('#')[0].strip() for l in open(_VCP, encoding='utf-8')} - {''} if os.path.exists(_VCP) else set()

GLSL_T = {1: 'float', 2: 'vec2', 3: 'vec3', 4: 'vec4'}


class CompileError(Exception):
    pass


def glf(x):
    s = repr(float(x))
    if 'e' in s or 'inf' in s or 'nan' in s:
        s = '%.9g' % float(x)
        if 'e' not in s and '.' not in s: s += '.0'
    return s


def cast(code, t_from, t_to):
    """UE3-style coercion: scalar broadcasts; vectors truncate (lenient) or zero-pad."""
    if t_from == t_to: return code
    if t_from == 1: return '%s(%s)' % (GLSL_T[t_to], code)
    if t_to == 1: return '(%s).x' % code
    if t_to < t_from: return '(%s).%s' % (code, 'xyzw'[:t_to])
    pad = ', '.join(['0.0'] * (t_to - t_from - (1 if t_to == 4 else 0)) + (['1.0'] if t_to == 4 else []))
    return '%s(%s, %s)' % (GLSL_T[t_to], code, pad)


class Ctx:
    """One function-call frame (master graph or an inlined MaterialFunction)."""
    def __init__(self, inputs=None, name='root'):
        self.inputs = inputs or {}       # IOBlock pin Id -> (code, type) of the caller's argument
        self.name = name
        self.input_block = None


class MatCompiler:
    # TnCharacterApplier (TransGame.Default__TnCharacterApplier) pushes these vector parameters onto
    # character meshes at runtime (robot, vehicle, separate arm, weapon); an all-zero value skips the
    # override, leaving each expression's authored value.
    RUNTIME_PARAMS = ('Cust_Color_A', 'Cust_COLOR_B', 'EnergonColor')

    def __init__(self, repo, inst_path, texture_resolver, runtime_params=False):
        self.R = repo
        self.inst = inst_path
        self.texres = texture_resolver
        self.lines = []
        self.memo = {}
        self.ntmp = 0
        self.tex_slots = []              # [{'name','kind','file','srgb',...}]
        self.tex_index = {}
        self.uses = set()
        self.notes = []
        self.runtime_params = runtime_params
        self.rt_used = {}
        self.params_read = {'Scalar': set(), 'Vector': set(), 'Texture': set()}   # vs compiled permutation                # name -> MIC-level authored value (or None = per-expression default)
        # resolve instance chain -> master + params
        self.scalars, self.vectors, self.textures, self.texsets = {}, {}, {}, {}
        self.switches, self.masks = {}, {}
        chain = []
        p = inst_path
        while p and self.R.cls(p) == 'MaterialInstanceConstant' and len(chain) < 16:
            chain.append(p)
            o = self.R.obj(p) or {}
            p = (o.get('Parent') or {}).get('ref')
        self.chain = chain
        self.master = p if p and self.R.cls(p) == 'Material' else None
        if not self.master:
            raise CompileError('no master material for %s' % inst_path)
        for mic in reversed(chain):      # parent first; child overrides
            o = self.R.obj(mic) or {}
            for e in o.get('ScalarParameterValues') or []:
                d = struct_dict(e); self.scalars[d.get('ParameterName')] = d.get('ParameterValue')
            for e in o.get('VectorParameterValues') or []:
                d = struct_dict(e); self.vectors[d.get('ParameterName')] = d.get('ParameterValue')
            for e in o.get('TextureParameterValues') or []:
                d = struct_dict(e); self.textures[d.get('ParameterName')] = (d.get('ParameterValue') or {}).get('ref')
            for e in o.get('TextureSetParameterValues') or []:
                d = struct_dict(e); self.texsets[d.get('ParameterName')] = (d.get('ParameterValue') or {}).get('ref')
        if chain:
            sw, mk = self.R.mic_static_params(chain[0])
            self.switches = {k: v[0] for k, v in sw.items()}
            self.masks = {k: v[0] for k, v in mk.items()}
        self.mat = self.R.obj(self.master) or {}

    # ------------------------------------------------------------------ helpers
    def tmp(self, t, code):
        self.ntmp += 1
        n = 't%d' % self.ntmp
        self.lines.append('    %s %s = %s;' % (GLSL_T[t], n, code))
        return n

    def tex_slot(self, key, kind, info):
        if key in self.tex_index: return self.tex_index[key]
        k = len(self.tex_slots)
        d = dict(info); d['slot'] = k; d['kind'] = kind; d['key'] = key
        self.tex_slots.append(d)
        self.tex_index[key] = k
        return k

    def expr_ref(self, inp):
        """ExpressionInput struct -> (expr path, output index, mask) or None."""
        d = struct_dict(inp)
        if not isinstance(d, dict): return None
        e = d.get('Expression')
        if not e or not isinstance(e, dict) or 'ref' not in e: return None
        mask = None
        if d.get('Mask'):
            mask = ''.join(c for c, k in zip('xyzw', ('MaskR', 'MaskG', 'MaskB', 'MaskA')) if d.get(k))
        return e['ref'], int(d.get('ExpressionOutputIndex') or 0), mask

    def input(self, ctx, node, name, default=None):
        """Compile a named input of an expression node. Returns (code, type) or default."""
        r = self.expr_ref(node.get(name))
        if not r: return default
        code, t = self.compile(ctx, r[0], r[1])
        if r[2]:
            code = '(%s).%s' % (cast(code, t, 4), r[2]); t = len(r[2])
        return code, t

    def req(self, ctx, node, name, path):
        v = self.input(ctx, node, name)
        if v is None:
            raise CompileError('missing input %s on %s' % (name, path))
        return v

    # ------------------------------------------------------------------ main entry
    def compile(self, ctx, path, out=0):
        key = (ctx.name, path, out)
        if key in self.memo: return self.memo[key]
        cls = self.R.cls(path)
        node = self.R.obj(path)
        if node is None:
            raise CompileError('unresolved expression %s' % path)
        fn = getattr(self, 'x_' + (cls or '').replace('MaterialExpression', ''), None)
        if fn is None:
            raise CompileError('unsupported expression %s (%s)' % (cls, path))
        code, t = fn(ctx, node, path, out)
        if not re.fullmatch(r'[A-Za-z_]\w*|[-0-9.e]+', code):
            code = self.tmp(t, code)
        self.memo[key] = (code, t)
        return code, t

    def binop(self, ctx, node, path, op):
        a = self.input(ctx, node, 'A', ('0.0', 1))
        b = self.input(ctx, node, 'B', ('0.0', 1))
        t = max(a[1], b[1])
        if a[1] != b[1] and a[1] != 1 and b[1] != 1:
            t = min(a[1], b[1])          # UE3 errors here; be lenient (truncate)
            self.type_violations = getattr(self, 'type_violations', 0) + 1
        return '(%s %s %s)' % (cast(a[0], a[1], t), op, cast(b[0], b[1], t)), t

    # ------------------------------------------------------------------ arithmetic
    def x_Add(self, c, n, p, o): return self.binop(c, n, p, '+')
    def x_Subtract(self, c, n, p, o): return self.binop(c, n, p, '-')
    def x_Multiply(self, c, n, p, o): return self.binop(c, n, p, '*')
    def x_Divide(self, c, n, p, o): return self.binop(c, n, p, '/')

    def x_Constant(self, c, n, p, o): return glf(n.get('R', 0.0)), 1
    def x_Constant2Vector(self, c, n, p, o): return 'vec2(%s, %s)' % (glf(n.get('R', 0)), glf(n.get('G', 0))), 2
    def x_Constant3Vector(self, c, n, p, o):
        return 'vec3(%s, %s, %s)' % (glf(n.get('R', 0)), glf(n.get('G', 0)), glf(n.get('B', 0))), 3
    def x_Constant4Vector(self, c, n, p, o):
        return 'vec4(%s, %s, %s, %s)' % tuple(glf(n.get(k, 0)) for k in 'RGBA'), 4

    def x_ScalarParameter(self, c, n, p, o):
        nm = n.get('ParameterName')
        self.params_read['Scalar'].add(nm)
        v = self.scalars.get(nm, n.get('DefaultValue', 0.0))
        return glf(v), 1

    def x_VectorParameter(self, c, n, p, o):
        nm = n.get('ParameterName')
        self.params_read['Vector'].add(nm)
        v = self.vectors.get(nm, n.get('DefaultValue') or [0, 0, 0, 1])
        authored = 'vec4(%s)' % ', '.join(glf(x) for x in v)
        if self.runtime_params and nm in self.RUNTIME_PARAMS:
            # Runtime applier override; when not set (all-zero = skip) every same-named expression
            # keeps its OWN authored value (MIC value, else that expression's default).
            self.rt_used[nm] = self.vectors.get(nm)
            code = '(uRTSet_%s != 0 ? uRT_%s : %s)' % (nm, nm, authored)
        else:
            code = authored
        # VectorParameter outputs 1..4 = R, G, B, A. Applied only to materials proven by their compiled
        # permutation (vector_channel_proven.txt), or to all with WFC_MATC_VECTOR_CHANNELS=1 (evaluation).
        if 1 <= o <= 4 and (os.environ.get('WFC_MATC_VECTOR_CHANNELS') == '1' or self.inst in VECTOR_CHANNEL_PROVEN):
            return '(%s).%s' % (code, 'xyzw'[o - 1]), 1
        return code, 4

    def x_StaticSwitchParameter(self, c, n, p, o):
        nm = n.get('ParameterName')
        val = self.switches.get(nm, bool(n.get('DefaultValue', False)))
        r = self.input(c, n, 'A' if val else 'B')
        if r is None:
            return '0.0', 1
        return r

    def x_StaticComponentMaskParameter(self, c, n, p, o):
        nm = n.get('ParameterName')
        m = self.masks.get(nm, tuple(int(bool(n.get('Default' + k))) for k in 'RGBA'))
        x, t = self.req(c, n, 'Input', p)
        sel = ''.join(ch for ch, on in zip('xyzw', m) if on) or 'x'
        return '(%s).%s' % (cast(x, t, 4), sel), len(sel)

    def x_ComponentMask(self, c, n, p, o):
        x, t = self.req(c, n, 'Input', p)
        sel = ''.join(ch for ch, k in zip('xyzw', 'RGBA') if n.get(k)) or 'x'
        return '(%s).%s' % (cast(x, t, 4), sel), len(sel)

    def x_AppendVector(self, c, n, p, o):
        a = self.req(c, n, 'A', p); b = self.req(c, n, 'B', p)
        if a[1] + b[1] > 4:              # UE3: "Cannot append" compile error
            self.type_violations = getattr(self, 'type_violations', 0) + 1
        t = min(4, a[1] + b[1])
        return '%s(%s, %s)' % (GLSL_T[t], a[0], b[0]), t

    def x_OneMinus(self, c, n, p, o):
        x, t = self.req(c, n, 'Input', p); return '(1.0 - %s)' % x, t

    def x_Abs(self, c, n, p, o):
        x, t = self.req(c, n, 'Input', p); return 'abs(%s)' % x, t

    def x_Floor(self, c, n, p, o):
        x, t = self.req(c, n, 'Input', p); return 'floor(%s)' % x, t

    def x_Ceil(self, c, n, p, o):
        x, t = self.req(c, n, 'Input', p); return 'ceil(%s)' % x, t

    def x_Frac(self, c, n, p, o):
        x, t = self.req(c, n, 'Input', p); return 'fract(%s)' % x, t

    def x_SquareRoot(self, c, n, p, o):
        x, t = self.req(c, n, 'Input', p); return 'sqrt(%s)' % x, t

    def x_Normalize(self, c, n, p, o):
        x, t = self.req(c, n, 'VectorInput', p) if n.get('VectorInput') else self.req(c, n, 'Input', p)
        return 'normalize(%s)' % x, t

    def x_Power(self, c, n, p, o):
        b, tb = self.req(c, n, 'Base', p) if n.get('Base') else self.req(c, n, 'A', p)
        e = self.input(c, n, 'Exponent') or self.input(c, n, 'B') or (glf(n.get('ConstExponent', 2.0)), 1)
        return 'pow(max(%s, %s(0.0)), %s)' % (b, GLSL_T[tb], cast(e[0], e[1], tb)), tb

    def x_Clamp(self, c, n, p, o):
        x, t = self.req(c, n, 'Input', p)
        lo = self.input(c, n, 'Min', ('0.0', 1)); hi = self.input(c, n, 'Max', ('1.0', 1))
        return 'clamp(%s, %s, %s)' % (x, cast(lo[0], lo[1], t), cast(hi[0], hi[1], t)), t

    def x_LinearInterpolate(self, c, n, p, o):
        a = self.input(c, n, 'A', ('0.0', 1)); b = self.input(c, n, 'B', ('1.0', 1))
        al = self.input(c, n, 'Alpha', ('0.5', 1))
        t = max(a[1], b[1])
        return 'mix(%s, %s, %s)' % (cast(a[0], a[1], t), cast(b[0], b[1], t),
                                    al[0] if al[1] == 1 else cast(al[0], al[1], t)), t

    def x_DotProduct(self, c, n, p, o):
        a = self.req(c, n, 'A', p); b = self.req(c, n, 'B', p)
        t = min(a[1], b[1]) if a[1] != 1 and b[1] != 1 else max(a[1], b[1])
        return 'dot(%s, %s)' % (cast(a[0], a[1], t), cast(b[0], b[1], t)), 1

    def x_CrossProduct(self, c, n, p, o):
        a = self.req(c, n, 'A', p); b = self.req(c, n, 'B', p)
        return 'cross(%s, %s)' % (cast(a[0], a[1], 3), cast(b[0], b[1], 3)), 3

    def x_Desaturation(self, c, n, p, o):
        x, t = self.req(c, n, 'Input', p)
        lf = n.get('LuminanceFactors') or [0.3, 0.59, 0.11, 0]
        pc = self.input(c, n, 'Percent', ('1.0', 1))
        x3 = cast(x, t, 3)
        return 'mix(%s, vec3(dot(%s, vec3(%s, %s, %s))), %s)' % (x3, x3, glf(lf[0]), glf(lf[1]), glf(lf[2]),
                                                               cast(pc[0], pc[1], 1)), 3

    def x_Sine(self, c, n, p, o):
        x, t = self.req(c, n, 'Input', p)
        per = n.get('Period', 1.0)
        return ('sin(%s * %s)' % (x, glf(2 * math.pi / per)) if per > 0 else 'sin(%s)' % x), t

    def x_Cosine(self, c, n, p, o):
        x, t = self.req(c, n, 'Input', p)
        per = n.get('Period', 1.0)
        return ('cos(%s * %s)' % (x, glf(2 * math.pi / per)) if per > 0 else 'cos(%s)' % x), t

    def x_Time(self, c, n, p, o):
        self.uses.add('time'); return 'm.time', 1

    def x_VertexColor(self, c, n, p, o):
        self.uses.add('vcolor')
        return ['m.vertexColor', 'm.vertexColor.r', 'm.vertexColor.g', 'm.vertexColor.b', 'm.vertexColor.a'][min(o, 4)], \
            (4 if o == 0 else 1)

    def x_PixelDepth(self, c, n, p, o):
        self.uses.add('depth'); return 'm.pixelDepth', 1

    def x_ScreenPosition(self, c, n, p, o):
        # bScreenAlign: xy/w mapped to [0,1] screen UV (ScreenPositionScaleBias); otherwise clip position
        if n.get('bScreenAlign'):
            return 'vec4(m.screenPos.xy / m.screenPos.w * vec2(0.5, -0.5) + 0.5, m.screenPos.zw)', 4
        return 'm.screenPos', 4

    def x_CameraVector(self, c, n, p, o):
        self.uses.add('camvec')
        if n.get('CoordinateSpace') == 'CS_World':      # UE world space (Z up)
            self.uses.add('tbn'); return 'tangentToWorldUE(m, m.cameraVector)', 3
        return 'm.cameraVector', 3

    def x_ReflectionVector(self, c, n, p, o):
        self.uses.add('reflvec')
        if n.get('CoordinateSpace') == 'CS_World':      # UE world space (Z up)
            self.uses.add('tbn'); return 'tangentToWorldUE(m, m.reflectionVector)', 3
        return 'm.reflectionVector', 3

    def x_WorldPosition(self, c, n, p, o):
        return 'm.worldPosUE', 3

    def x_TextureCoordinate(self, c, n, p, o):
        idx = int(n.get('CoordinateIndex') or 0)
        uv = 'm.uv%d' % min(idx, 1)
        ut = self.input(c, n, 'UTile'); vt = self.input(c, n, 'VTile')
        # [MED] WFC extension: UTile/VTile inputs. Folded as an override of the constant tiling
        # (matches the compiled uniform-expression lists, which carry only the parameters).
        us = cast(ut[0], ut[1], 1) if ut else glf(n.get('UTiling', 1.0))
        vs = cast(vt[0], vt[1], 1) if vt else glf(n.get('VTiling', 1.0))
        if us == '1.0' and vs == '1.0': return uv, 2
        return '(%s * vec2(%s, %s))' % (uv, us, vs), 2

    def x_Panner(self, c, n, p, o):
        uv = self.input(c, n, 'Coordinate', ('m.uv0', 2))
        tm = self.input(c, n, 'Time')
        if not tm: self.uses.add('time'); tm = ('m.time', 1)
        return '(%s + %s * vec2(%s, %s))' % (cast(uv[0], uv[1], 2), tm[0], glf(n.get('SpeedX', 0.0)),
                                             glf(n.get('SpeedY', 0.0))), 2

    def x_Rotator(self, c, n, p, o):
        uv = self.input(c, n, 'Coordinate', ('m.uv0', 2))
        tm = self.input(c, n, 'Time')
        if not tm: self.uses.add('time'); tm = ('m.time', 1)
        cx, cy, sp = n.get('CenterX', 0.5), n.get('CenterY', 0.5), n.get('Speed', 0.25)
        a = self.tmp(1, '%s * %s' % (tm[0], glf(sp)))
        return ('(mat2(cos(%s), -sin(%s), sin(%s), cos(%s)) * (%s - vec2(%s, %s)) + vec2(%s, %s))'
                % (a, a, a, a, cast(uv[0], uv[1], 2), glf(cx), glf(cy), glf(cx), glf(cy))), 2

    def x_Transform(self, c, n, p, o):
        x, t = self.req(c, n, 'Input', p)
        src = n.get('TransformSourceType', 'TRANSFORMSOURCE_Tangent')
        dst = n.get('TransformType', 'TRANSFORM_World')
        if src != 'TRANSFORMSOURCE_Tangent':
            self.notes.append('Transform from %s treated as tangent' % src)
        fnm = {'TRANSFORM_World': 'tangentToWorldUE', 'TRANSFORM_View': 'tangentToView',
               'TRANSFORM_Local': 'tangentToLocal'}.get(dst, 'tangentToWorldUE')
        self.uses.add('tbn')
        return '%s(m, %s)' % (fnm, cast(x, t, 3)), 3

    def x_Normal(self, c, n, p, o):
        # the material's own (tangent-space) normal, as UE3's normal-dependent expressions see it
        self.uses.add('normal'); return 'm.normal', 3

    def x_MeshEmitterVertexColor(self, c, n, p, o):
        # mesh-particle emitter colour (the particle colour); static world meshes see white.
        # UE3 compiles it as a VectorParameter named MeshEmitterVertexColor.
        self.uses.add('vcolor')
        self.params_read['Vector'].add('MeshEmitterVertexColor')
        return ['m.vertexColor', 'm.vertexColor.r', 'm.vertexColor.g', 'm.vertexColor.b', 'm.vertexColor.a'][min(o, 4)], \
            (4 if o == 0 else 1)

    def x_ConstantClamp(self, c, n, p, o):
        x, t = self.req(c, n, 'Input', p)
        return 'clamp(%s, %s, %s)' % (x, glf(n.get('Min', 0.0)), glf(n.get('Max', 1.0))), t

    def x_FlipBookSample(self, c, n, p, o):
        # TextureFlipBook: sub-image grid advanced at FrameRate (UE3 UTextureFlipBook).
        tex = (n.get('Texture') or {}).get('ref')
        fb = self.R.obj(tex) or {}
        h = int(fb.get('HorizontalImages', 1) or 1); v = int(fb.get('VerticalImages', 1) or 1)
        rate = float(fb.get('FrameRate', 4.0) or 4.0)
        self.uses.add('time')
        uv = self.input(c, n, 'Coordinates', ('m.uv0', 2))
        frame = self.tmp(1, 'floor(mod(m.time * %s, %s))' % (glf(rate), glf(h * v)))
        cell = self.tmp(2, 'vec2(mod(%s, %s), floor(%s / %s))' % (frame, glf(h), frame, glf(h)))
        uvf = '((%s + %s) * vec2(%s, %s))' % (cast(uv[0], uv[1], 2), cell, glf(1.0 / h), glf(1.0 / v))
        info = self._tex_info(tex) or {'file': None, 'object': tex}
        slot = self.tex_slot(('2d', tex), '2d', info)
        s = self.tmp(4, 'wfcSample2D(%d, %s)' % (slot, uvf))
        if o == 0: return s, 4
        return '%s.%s' % (s, 'rgba'[min(o, 4) - 1]), 1

    def x_Fresnel(self, c, n, p, o):
        nrm = self.input(c, n, 'Normal', ('vec3(0.0, 0.0, 1.0)', 3))
        self.uses.add('camvec')
        ex = self.input(c, n, 'Exp')            # WFC extension: exponent driven by an expression
        exc = cast(ex[0], ex[1], 1) if ex else glf(n.get('Exponent', 3.0))
        return 'pow(max(1.0 - max(dot(%s, m.cameraVector), 0.0), 0.0), %s)' % (cast(nrm[0], nrm[1], 3), exc), 1

    def x_DepthBiasedAlpha(self, c, n, p, o):
        # UE3 DepthBiasedAlpha: Alpha * saturate((SceneDepth - PixelDepth) / max((1 - Bias) * BiasScale, 0.001)),
        # depths in UE units. WFC adds BiasScaleInput (expression overriding the BiasScale constant).
        a = self.input(c, n, 'Alpha', ('1.0', 1))
        b = self.input(c, n, 'Bias', ('0.0', 1))
        sc = self.input(c, n, 'BiasScaleInput')
        scale = cast(sc[0], sc[1], 1) if sc else glf(n.get('BiasScale', 1.0))
        self.uses.add('scenedepth')
        return 'wfcDepthBiasedAlpha(m, %s, %s, %s)' % (cast(a[0], a[1], 1), cast(b[0], b[1], 1), scale), 1

    def x_SceneDepth(self, c, n, p, o):
        self.uses.add('scenedepth'); return 'm.sceneDepth', 1

    def x_DestDepth(self, c, n, p, o):
        self.uses.add('scenedepth'); return 'm.sceneDepth', 1

    def x_DynamicParameter(self, c, n, p, o):
        # particle DynamicParameter (Param1..4); emitters do not drive it here -> authored default 1
        return ['m.dynParam', 'm.dynParam.x', 'm.dynParam.y', 'm.dynParam.z', 'm.dynParam.w'][min(o, 4)], \
            (4 if o == 0 else 1)

    def x_ParticleSubUV(self, c, n, p, o):
        tex = (n.get('Texture') or {}).get('ref')
        return self._sample2d(c, n, p, o, tex)

    def x_BumpOffset(self, c, n, p, o):
        uv = self.input(c, n, 'Coordinate', ('m.uv0', 2))
        h = self.req(c, n, 'Height', p)
        self.uses.add('camvec')
        ratio = n.get('HeightRatio', 0.05); ref = n.get('ReferencePlane', 0.5)
        return '(%s + m.cameraVector.xy * ((%s - %s) * %s))' % (cast(uv[0], uv[1], 2), cast(h[0], h[1], 1),
                                                                glf(ref), glf(ratio)), 2

    # ------------------------------------------------------------------ textures
    def _tex_info(self, tex_path):
        return self.texres(tex_path)

    def sample(self, slot, kind, uv, out):
        if kind == 'cube':
            s = 'texture(uTex[%d] , %s)' % (slot, uv)   # replaced below
        s = 'wfcTex%d(%s)' % (slot, uv)
        if out == 0: return s, 4
        return '%s.%s' % (s, 'rgba'[min(out, 4) - 1]), 1

    def _sample2d(self, c, n, p, o, tex_path, pname=None):
        info = self._tex_info(tex_path)
        if info is None:
            self.notes.append('texture not found: %s' % tex_path)
            info = {'file': None, 'object': tex_path}
        slot = self.tex_slot(('2d', tex_path), '2d', info)
        uv = self.input(c, n, 'Coordinates', ('m.uv0', 2))
        s = self.tmp(4, 'wfcSample2D(%d, %s)' % (slot, cast(uv[0], uv[1], 2)))
        if o == 0: return s, 4
        return '%s.%s' % (s, 'rgba'[min(o, 4) - 1]), 1

    def x_TextureSample(self, c, n, p, o):
        tex = (n.get('Texture') or {}).get('ref')
        if tex and self.R.cls(tex) == 'TextureCube':
            return self._samplecube(c, n, p, o, tex)
        return self._sample2d(c, n, p, o, tex)

    def x_TextureSampleParameter2D(self, c, n, p, o):
        nm = n.get('ParameterName')
        self.params_read['Texture'].add(nm)
        tex = self.textures.get(nm) or (n.get('Texture') or {}).get('ref')
        return self._sample2d(c, n, p, o, tex, nm)

    def _samplecube(self, c, n, p, o, tex):
        info = self._tex_info(tex) or {'file': None, 'object': tex}
        slot = self.tex_slot(('cube', tex), 'cube', info)
        uv = self.input(c, n, 'Coordinates')
        if not uv:
            self.uses.add('reflvec'); self.uses.add('tbn')
            uv = ('tangentToWorldUE(m, m.reflectionVector)', 3)
        bias = self.input(c, n, 'LODBias')
        if bias:
            s = self.tmp(4, 'wfcSampleCubeBias(%d, %s, %s)' % (slot, cast(uv[0], uv[1], 3), cast(bias[0], bias[1], 1)))
        else:
            s = self.tmp(4, 'wfcSampleCube(%d, %s)' % (slot, cast(uv[0], uv[1], 3)))
        if o == 0: return s, 4
        return '%s.%s' % (s, 'rgba'[min(o, 4) - 1]), 1

    def x_TextureSampleParameterCube(self, c, n, p, o):
        nm = n.get('ParameterName')
        self.params_read['Texture'].add(nm)
        tex = self.textures.get(nm) or (n.get('Texture') or {}).get('ref')
        return self._samplecube(c, n, p, o, tex)

    def x_TextureSetSampleParameter(self, c, n, p, o):
        nm = n.get('ParameterName')
        self.params_read['Texture'].add(nm)
        ts = self.texsets.get(nm) or (n.get('DefaultValue') or {}).get('ref')
        tso = self.R.obj(ts) or {}
        inter = [x.get('ref') for x in (tso.get('Intermediates') or []) if isinstance(x, dict)]
        cn = next((x for x in inter if x.lower().endswith('_color_normx')), None)
        mn = next((x for x in inter if x.lower().endswith('_masks_normy')), None)
        uv = self.input(c, n, 'UVs', ('m.uv0', 2))
        uvc = cast(uv[0], uv[1], 2)
        key = ('texset', ts)
        if key not in self.memo:
            s0 = self.tex_slot(('2d', cn), '2d', self._tex_info(cn) or {'file': None, 'object': cn})
            s1 = self.tex_slot(('2d', mn), '2d', self._tex_info(mn) or {'file': None, 'object': mn})
            a = self.tmp(4, 'wfcSample2D(%d, %s)' % (s0, uvc))
            b = self.tmp(4, 'wfcSample2D(%d, %s)' % (s1, uvc))
            # TextureSet blueprint ENV_MainTexSet_BluePrint: Color_NormX = Color.rgb + Normal.r,
            # Masks_NormY = Masks.rgb + Normal.g; alpha UnpackMin=-1 (stored [0,1] -> [-1,1]).
            nz = self.tmp(3, 'vec3(%s.a, %s.a, sqrt(clamp(1.0 - %s.a * %s.a - %s.a * %s.a, 0.0, 1.0)))'
                          % (a, b, a, a, b, b))
            self.memo[key] = (a, b, nz)
        a, b, nz = self.memo[key]
        return [('%s.rgb' % a, 3), ('%s.rgb' % b, 3), (nz, 3)][min(o, 2)]

    # ------------------------------------------------------------------ custom code + functions
    def x_ShaderCode(self, c, n, p, o):
        code = n.get('ShaderCode') or '0'
        args = {}
        for k in 'ABCDEFGHIJ':
            if n.get(k) is not None:
                v = self.input(c, n, k)
                if v: args[k] = v
        try:
            return hlslexpr.translate(code, args)
        except Exception as ex:
            raise CompileError('ShaderCode %r: %s' % (code, ex))

    def x_Custom(self, c, n, p, o):
        return self.x_ShaderCode(c, n, p, o)

    def x_IOBlock(self, c, n, p, o):
        # reference to a function's input block: output index == pin Id. Arguments are compiled
        # lazily (in the caller's frame) so inputs a static switch leaves unused emit no code.
        if o in c.inputs:
            v = c.inputs[o]
            if callable(v):
                v = v()
                c.inputs[o] = v
            return v
        return '0.0', 1

    def x_Function(self, c, n, p, o):
        fpath = (n.get('Function') or {}).get('ref')
        f = self.R.obj(fpath)
        if not f:
            raise CompileError('missing function %s' % fpath)
        ib = self.R.obj((f.get('InputBlock') or {}).get('ref')) or {}
        ob = self.R.obj((f.get('OutputBlock') or {}).get('ref')) or {}
        in_pins = [struct_dict(x) for x in (ib.get('Pins') or [])]
        out_pins = [struct_dict(x) for x in (ob.get('Pins') or [])]
        call_inputs = n.get('Inputs') or []
        args = {}
        for k, pin in enumerate(in_pins):
            if k < len(call_inputs):
                r = self.expr_ref(call_inputs[k])
                if r:
                    def thunk(r=r, c=c):
                        code, t = self.compile(c, r[0], r[1])
                        if r[2]:
                            code = '(%s).%s' % (cast(code, t, 4), r[2]); t = len(r[2])
                        return code, t
                    args[int(pin.get('Id', k))] = thunk
        sub = Ctx(args, name='%s>%s' % (c.name, p))
        pin = next((x for x in out_pins if int(x.get('Id', -1)) == o), None)
        if pin is None:
            raise CompileError('function %s has no output pin %d' % (fpath, o))
        r = self.expr_ref(pin.get('Input'))
        if not r: return '0.0', 1
        code, t = self.compile(sub, r[0], r[1])
        if r[2]:
            code = '(%s).%s' % (cast(code, t, 4), r[2]); t = len(r[2])
        return code, t

    # ------------------------------------------------------------------ material
    def build(self):
        m = self.mat
        root = Ctx()
        outs = {}
        spec = [('Distortion', 3, 'vec3(0.0)'),
                ('DiffuseColor', 3, 'vec3(0.0)'), ('SpecularColor', 3, 'vec3(0.0)'), ('SpecularPower', 1, '15.0'),
                ('Normal', 3, 'vec3(0.0, 0.0, 1.0)'), ('EmissiveColor', 3, 'vec3(0.0)'),
                ('Opacity', 1, '1.0'), ('OpacityMask', 1, '1.0'), ('CustomLighting', 3, 'vec3(0.0)')]
        # Normal first: UE3 computes the material normal before inputs that depend on it.
        order = ['Normal'] + [s[0] for s in spec if s[0] != 'Normal']
        connected = {}
        for nm in order:
            t, dflt = next((s[1], s[2]) for s in spec if s[0] == nm)
            v = self.input(root, m, nm)
            if v is None:
                outs[nm] = dflt
            else:
                connected[nm] = True
                outs[nm] = cast(v[0], v[1], t)
            if nm == 'Normal':
                self.lines.append('    o.Normal = %s;' % outs['Normal'])
                self.lines.append('    wfcSetNormal(m, o.Normal);')
        for nm, t, d in spec:
            if nm == 'Normal': continue
            self.lines.append('    o.%s = %s;' % (nm, outs[nm]))
        info = {
            'master': self.master, 'chain': self.chain,
            'lighting_model': m.get('LightingModel', 'MLM_Phong'),
            'blend_mode': m.get('BlendMode', 'BLEND_Opaque'),
            'two_sided': bool(m.get('TwoSided', False)),
            'opacity_mask_clip': m.get('OpacityMaskClipValue', 0.3333),
            'connected': sorted(connected), 'uses': sorted(self.uses),
            'switches': self.switches, 'textures': self.tex_slots, 'notes': self.notes,
            'runtime_params': sorted(self.rt_used),
            'params_read': {k: sorted(x for x in v if x) for k, v in self.params_read.items()},
            'type_violations': getattr(self, 'type_violations', 0),
        }
        return '\n'.join(self.lines), info


def hlsl_to_glsl(s):
    s = re.sub(r'\bfloat([234])\b', r'vec\1', s)
    s = re.sub(r'\bhalf([234])\b', r'vec\1', s)
    s = re.sub(r'\bhalf\b', 'float', s)
    s = re.sub(r'(\d)[fF]\b', r'\1', s)
    s = re.sub(r'\blerp\s*\(', 'wlerp(', s)
    s = re.sub(r'\bpow\s*\(', 'wpow(', s)
    s = re.sub(r'\bfrac\s*\(', 'fract(', s)
    s = re.sub(r'\brsqrt\s*\(', 'inversesqrt(', s)
    s = re.sub(r'\bclamp\s*\(', 'wclamp(', s)
    s = re.sub(r'(?<![\w.])\.(\d)', r'0.\1', s)
    # integer literals -> float (GLSL is strict in some overloads)
    s = re.sub(r'(?<![\w.])(\d+)(?![\w.])', r'\1.0', s)
    return s


def infer_shadercode_type(code, args):
    c = code.strip()
    m = re.match(r'(float|half)([234]?)\s*\(', c)
    if m and _balanced_whole(c, m.end() - 1):
        return int(m.group(2) or 1)
    m = re.match(r'(saturate|abs|normalize|sqrt)\s*\(\s*%([A-J])\s*\)$', c)
    if m and m.group(2) in args: return args[m.group(2)][1]
    m = re.match(r'lerp\s*\(\s*%([A-J])', c)
    if m and m.group(1) in args: return args[m.group(1)][1]
    m = re.match(r'(saturate|clamp)\s*\(', c)
    if m and _balanced_whole(c, m.end() - 1):
        inner = [args[k][1] for k in re.findall(r'%([A-J])', c) if k in args]
        return max(inner) if inner else 1
    m = re.match(r'saturate\s*\(\s*float([234]?)', c)
    if m: return int(m.group(1) or 1)
    inner = [args[k][1] for k in re.findall(r'%([A-J])', c) if k in args]
    return max(inner) if inner else 1


def _balanced_whole(s, open_idx):
    depth = 0
    for i in range(open_idx, len(s)):
        if s[i] == '(': depth += 1
        elif s[i] == ')':
            depth -= 1
            if depth == 0:
                return s[i + 1:].strip() == ''
    return False
