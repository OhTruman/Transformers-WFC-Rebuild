"""A numeric emulator for matc's generated material GLSL (M6, Systems; tools/render/matc.py emits one wfcMaterial body
per material instance: SSA lines "TYPE tN = EXPR;", "o.Output = EXPR;", wfcSample2D / cube samples, mix / dot / clamp /
pow ...). It is the oracle for the M6 "pbr" mapping: evaluate a material's original body with chosen inputs, evaluate the
mapped variant on the same inputs, compare.

Not a GLSL compiler: the subset matc emits, evaluated in float64 with numpy. Anything outside the subset raises
Unsupported (the caller records the material as not emulated). Inputs:
  - texture samples: a table (slot -> 4 floats), or real images (slot -> image array, sampled at the UV, wrap, nearest);
  - the view inputs (m.cameraVector, m.reflectionVector, m.lightVector) and cube samples: non-degenerate defaults;
  - runtime parameters (uRTSet_X ? uRT_X : default): unset (the default branch) or set (uRT_X = a given value).
Tangent / world transforms are identity (the mapping does not change them).

**The base-colour strip** (the M6 contract for matc's "pbr" mode; strip_view=True):
  every value carries a flag "depends on the view" (set at m.cameraVector / m.reflectionVector / m.lightVector and every
  cube sample, propagated through every operation and function). Two rules:
  1. additive view terms are zeroed: at a + / - whose one operand depends on the view and the other does not, the result
     is the view-independent operand alone (the cube reflection added into DiffuseColor, the anisotropic camera specular
     of WEP_PhongMain / ENV_PhongMain_NS: pow(sqrt(1 - dot(V, T)^2), n) * colour * k, added to the diffuse chain);
  2. a view dependence still present at o.DiffuseColor (a multiplicative Fresnel / view factor) is replaced by its
     view-independent mean: DiffuseColor averaged over the fixed direction set VIEW_SET (cube samples held fixed).
  3. a cube-derived factor multiplying a texture-derived colour (vec3 / vec4) is not a view term: the product is kept,
     the cube standing for its view-independent mean (matc: the cube map's mean colour, i.e. its 1 x 1 mip). This is the
     robot paint x cube of CHR_Transformer_NormSpec_Cust_E and the ENV tint x cube; a camera-dependent factor (the
     anisotropic specular) is not covered (rule 1 drops it), nor is a cube x constant tint (the ENV reflection term).
  The base colour is then clamped to >= 0 (oversaturation mixes such as mix(c, lum(c), -0.15) can go negative).
  Only DiffuseColor (the base colour) is stripped; EmissiveColor and every other output keep the view terms.
  Known limit: the view flag does not propagate through a sample's UV (a texture sampled at a view-dependent UV counts
  as view-independent); 1 material today.
"""
import math
import re

import numpy as np


class Unsupported(Exception):
    pass


_SW = {'x': 0, 'y': 1, 'z': 2, 'w': 3, 'r': 0, 'g': 1, 'b': 2, 'a': 3, 's': 0, 't': 1, 'p': 2, 'q': 3}
_STRIP = [False]                     # (set by run() while evaluating with strip_view)

# the fixed view-direction set for rule 2's mean: 32 directions on the upper hemisphere (golden spiral), deterministic
VIEW_SET = []
for _i in range(32):
    _z = 1.0 - (_i + 0.5) / 32.0
    _r = math.sqrt(max(0.0, 1.0 - _z * _z))
    _phi = 2.399963229728653 * _i
    VIEW_SET.append((_r * math.cos(_phi), _r * math.sin(_phi), _z))
DEFAULT_CAMERA = (0.28, -0.42, 0.86)
DEFAULT_REFLECTION = (-0.33, 0.21, 0.92)
DEFAULT_LIGHT = (0.45, 0.30, 0.84)


class G:
    """A GLSL value: float (size 1) or vecN, with swizzles, broadcasting arithmetic and three flags:
    view (depends on the view: camera / reflection / light vectors or a cube sample), cam (depends on the camera / reflection
    / light vectors themselves: not only through a cube sample), tex (depends on a 2D texture sample) and bc (a scalar
    broadcast into a vector, e.g. vec3(mask.y): a factor, not a colour)."""
    __slots__ = ('v', 'view', 'cam', 'tex', 'bc')

    def __init__(self, v, view=False, cam=False, tex=False, bc=False):
        self.v = np.atleast_1d(np.asarray(v, dtype=np.float64))
        self.view, self.cam, self.tex, self.bc = view, cam, tex, bc

    def _like(self, v):
        return G(v, self.view, self.cam, self.tex, self.bc)

    def __getattr__(self, name):
        if name and all(c in _SW for c in name):
            idx = [_SW[c] for c in name]
            if max(idx) >= self.v.size:
                if self.v.size == 1:
                    return self._like(np.repeat(self.v, len(idx)))      # (scalar.xxx)
                raise Unsupported('swizzle .%s of a vec%d' % (name, self.v.size))
            return self._like(self.v[idx])
        raise AttributeError(name)

    @staticmethod
    def _o(other):
        return other.v if isinstance(other, G) else np.float64(other)

    def _join(self, other, v):
        if isinstance(other, G):
            return G(v, self.view or other.view, self.cam or other.cam, self.tex or other.tex, self.bc and other.bc)
        return self._like(v)

    def _add(self, other, sign, reverse=False):
        """reverse False: self + sign * other; reverse True: other + sign * self."""
        o = self._o(other)
        ov = isinstance(other, G) and other.view
        # rule 1 applies at colour sums (both operands vec3 / vec4): scalar arithmetic inside a view term (1 - dot^2, the
        # N.H falloff) is not a boundary
        if _STRIP[0] and self.view != ov and self.v.size >= 3 and isinstance(other, G) and other.v.size >= 3:
            if self.view:                                              # (self is the view term: keep the other operand)
                return G(o if reverse else sign * o, False, False, other.tex)
            return G(sign * self.v if reverse else self.v, False, False, self.tex)
        return self._join(other, o + sign * self.v if reverse else self.v + sign * o)

    def _mul(self, other, v):
        # rule 3: a cube-derived factor (view, not camera-dependent) multiplying a texture-derived colour (vec3 / vec4, not
        # view-dependent) stands for the cube map's view-independent mean: the product is kept as a colour (the robot paint
        # x cube x 0.2 of CHR_Transformer_NormSpec_Cust_E, ENV tint x cube), not dropped as a view term by rule 1
        if _STRIP[0] and isinstance(other, G) and self.view != other.view:
            vt, ct = (self, other) if self.view else (other, self)
            if not vt.cam and not ct.view and ct.tex and not ct.bc and ct.v.size >= 3:
                return G(v, False, False, True)
        return self._join(other, v)

    def __add__(self, o): return self._add(o, 1)
    def __radd__(self, o): return self._add(o, 1, reverse=True)
    def __sub__(self, o): return self._add(o, -1)
    def __rsub__(self, o): return self._add(o, -1, reverse=True)
    def __mul__(self, o): return self._mul(o, self.v * self._o(o))
    def __rmul__(self, o): return self._mul(o, self._o(o) * self.v)
    def __truediv__(self, o):
        with np.errstate(divide='ignore', invalid='ignore'):
            return self._join(o, self.v / self._o(o))
    def __rtruediv__(self, o):
        with np.errstate(divide='ignore', invalid='ignore'):
            return self._join(o, self._o(o) / self.v)
    def __neg__(self): return self._like(-self.v)
    def __float__(self): return float(self.v[0])

    def __repr__(self):
        f = [n for n in ('view', 'cam', 'tex') if getattr(self, n)]
        return 'G(%s%s)' % (np.array2string(self.v, precision=6), (', ' + ','.join(f)) if f else '')


def _anyview(args):
    return any(isinstance(a, G) and a.view for a in args)


def _setflags(r, args):
    gs = [a for a in args if isinstance(a, G)]
    r.view = r.view or any(a.view for a in gs)
    r.cam = r.cam or any(a.cam for a in gs)
    r.tex = r.tex or any(a.tex for a in gs)
    r.bc = bool(gs) and all(a.bc for a in gs)
    return r


def _vf(fn):
    """A GLSL function on G values: each flag is set when any argument has it."""
    def w(*args):
        r = fn(*args)
        return _setflags(r, args) if isinstance(r, G) else r
    return w


def _flat(args):
    out = []
    for a in args:
        out.extend((a.v if isinstance(a, G) else np.atleast_1d(np.float64(a))).tolist())
    return out


def _ctor(n):
    def f(*args):
        vals = _flat(args)
        broadcast = len(vals) == 1
        if broadcast:
            vals = vals * n
        if len(vals) < n:
            raise Unsupported('vec%d from %d values' % (n, len(vals)))
        r = _setflags(G(vals[:n]), args)
        r.bc = r.bc or broadcast
        return r
    return f


def _v(x):
    return x.v if isinstance(x, G) else np.float64(x)


def _mix(a, b, t):
    if _STRIP[0]:                                                       # (a view-dependent blend: rule 2, via the mean)
        pass
    return G(_v(a) * (1.0 - _v(t)) + _v(b) * _v(t))
def _dot(a, b): return G([float(np.dot(_v(a), _v(b)))])
def _clamp(x, lo, hi): return G(np.clip(_v(x), _v(lo), _v(hi)))
def _pow(a, b):
    with np.errstate(invalid='ignore', over='ignore', divide='ignore'):
        return G(np.power(_v(a), _v(b)))
def _sqrt(x):
    with np.errstate(invalid='ignore'):
        return G(np.sqrt(_v(x)))
def _normalize(x):
    v = _v(x); n = np.linalg.norm(v)
    return G(v / n if n > 0 else v)
def _length(x): return G([float(np.linalg.norm(_v(x)))])
def _abs(x): return G(np.abs(_v(x)))
def _floor(x): return G(np.floor(_v(x)))
def _fract(x): return G(_v(x) - np.floor(_v(x)))
def _min(a, b): return G(np.minimum(_v(a), _v(b)))
def _max(a, b): return G(np.maximum(_v(a), _v(b)))
def _step(e, x): return G((_v(x) >= _v(e)).astype(np.float64))
def _smoothstep(a, b, x):
    t = np.clip((_v(x) - _v(a)) / (_v(b) - _v(a)), 0, 1)
    return G(t * t * (3 - 2 * t))
def _exp2(x): return G(np.exp2(_v(x)))
def _log2(x):
    with np.errstate(divide='ignore', invalid='ignore'):
        return G(np.log2(_v(x)))
def _sin(x): return G(np.sin(_v(x)))
def _cos(x): return G(np.cos(_v(x)))
def _cross(a, b): return G(np.cross(_v(a), _v(b)))
def _sign(x): return G(np.sign(_v(x)))
def _mod(a, b): return G(np.mod(_v(a), _v(b)))
def _reflect(i, n): return G(_v(i) - 2.0 * np.dot(_v(n), _v(i)) * _v(n))
def _float(x): return G([float(np.atleast_1d(_v(x))[0])])


class Mat2:
    """GLSL mat2 (column-major constructor: a, b = column 0; c, d = column 1); only mat2 * vec2 is needed."""
    def __init__(self, a, b, c, d):
        f = lambda x: float(np.atleast_1d(_v(x))[0])
        self.m = np.array([[f(a), f(c)], [f(b), f(d)]])
        self.args = (a, b, c, d)
    def __mul__(self, o):
        return _setflags(G(self.m @ _v(o)[:2]), self.args + (o,))


def _mat2(a, b, c, d): return Mat2(a, b, c, d)


def srgb_to_linear(c):
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


class Material:
    """One emulation context: m (inputs), o (outputs), the samples / images, the view, the runtime parameters."""

    def __init__(self, samples2d=None, samples_cube=None, uv0=(0.25, 0.75), camera=DEFAULT_CAMERA, refl=DEFAULT_REFLECTION,
                 light=DEFAULT_LIGHT, vertex_color=(1, 1, 1, 1), time=0.0, images=None, rt_value=None, strip_view=False):
        self.samples2d, self.samples_cube = samples2d or {}, samples_cube or {}
        self.images = images or {}           # slot -> (h, w, 4) float array (already decoded / unpacked)
        self.rt_value = rt_value             # None: runtime parameters unset; a float: every uRT_* = that value
        self.strip_view = strip_view

        class M:
            def __getattr__(self, name):                                  # (inputs this emulator does not model: 0)
                return G((0.0, 0.0, 0.0))
        m = M()
        m.uv0 = G(uv0); m.uv1 = G(uv0); m.vertexColor = G(vertex_color)
        m.cameraVector = G(camera, True, True); m.reflectionVector = G(refl, True, True); m.lightVector = G(light, True, True)
        m.worldPosition = G((0.0, 0.0, 0.0)); m.time = G([time]); m.screenPos = G((0.5, 0.5, 0.0, 1.0))
        m.worldNormal = G((0.0, 0.0, 1.0)); m.normal = G((0.0, 0.0, 1.0)); m.worldPosUE = G((0.0, 0.0, 0.0))
        self.m = m

        class O:
            pass
        self.o = O()

    def sample2d(self, slot, uv=None, *rest):
        s = int(float(slot))
        if s in self.images and uv is not None:
            img = self.images[s]
            h, w = img.shape[:2]
            u, v = _v(uv)[:2]
            x = int(math.floor((u - math.floor(u)) * w)) % w
            y = int(math.floor((v - math.floor(v)) * h)) % h
            return G(img[y, x], tex=True)
        if s not in self.samples2d:
            raise Unsupported('no sample for 2D slot %d' % s)
        return G(self.samples2d[s], tex=True)

    def sample_cube(self, slot, *rest):
        s = int(float(slot))
        if s not in self.samples_cube:
            raise Unsupported('no sample for cube slot %d' % s)
        return G(np.asarray(self.samples_cube[s], dtype=np.float64), True)   # (a cube sample depends on the view)


_DECL = re.compile(r'^\s*(?:float|vec[234]|int|bool|mat[34])\s+(t\d+)\s*=\s*(.*);\s*$')
_ASSIGN = re.compile(r'^\s*(o\.\w+)\s*=\s*(.*);\s*$')
_CALL = re.compile(r'^\s*(wfc\w+)\s*\((.*)\)\s*;\s*$')
_IDENT = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
_ALLOWED_FUNCS = {'vec2', 'vec3', 'vec4', 'float', 'mix', 'dot', 'clamp', 'pow', 'sqrt', 'normalize', 'length', 'abs', 'floor',
                  'fract', 'min', 'max', 'step', 'smoothstep', 'exp2', 'log2', 'sin', 'cos', 'cross', 'sign', 'mod', 'reflect',
                  'wfcSample2D', 'wfcSample2DBias', 'wfcSample2DLevel', 'wfcSampleCube', 'wfcSampleCubeBias', 'wfcSampleCubeLevel',
                  'tangentToWorldUE', 'worldToTangentUE', 'mat2', 'wfcNormalNode'}
# a runtime parameter's "set ? value : default" (customisation tints, overheat, defrag ...): unset -> the default branch,
# set -> uRT_X (a scalar broadcast to the use)
_RT_TERNARY = re.compile(r'\(\s*uRTSet_\w+\s*!=\s*0\s*\?\s*(uRT_\w+(?:\.\w+)?)\s*:\s*(vec[234]\([^()]*\)|[-\d.e]+)\s*\)')
_RT_NAME = re.compile(r'\buRT_\w+')


def _env(ctx):
    w = _vf
    return {'m': ctx.m, 'o': ctx.o, 'vec2': _ctor(2), 'vec3': _ctor(3), 'vec4': _ctor(4), 'float': w(_float), 'mat2': _mat2,
            'wfcNormalNode': lambda m: m.normal, 'mix': w(_mix), 'dot': w(_dot), 'clamp': w(_clamp), 'pow': w(_pow),
            'sqrt': w(_sqrt), 'normalize': w(_normalize), 'length': w(_length), 'abs': w(_abs), 'floor': w(_floor),
            'fract': w(_fract), 'min': w(_min), 'max': w(_max), 'step': w(_step), 'smoothstep': w(_smoothstep),
            'exp2': w(_exp2), 'log2': w(_log2), 'sin': w(_sin), 'cos': w(_cos), 'cross': w(_cross), 'sign': w(_sign),
            'mod': w(_mod), 'reflect': w(_reflect), 'wfcSample2D': ctx.sample2d, 'wfcSample2DBias': ctx.sample2d,
            'wfcSample2DLevel': ctx.sample2d, 'wfcSampleCube': ctx.sample_cube, 'wfcSampleCubeBias': ctx.sample_cube,
            'wfcSampleCubeLevel': ctx.sample_cube, 'tangentToWorldUE': lambda m, v: v, 'worldToTangentUE': lambda m, v: v,
            '__builtins__': {}}


def _run_once(glsl, ctx):
    env = _env(ctx)
    _STRIP[0] = ctx.strip_view
    try:
        for raw in glsl.split('\n'):
            line = raw.strip()
            if not line or line.startswith('//'):
                continue
            mc = _CALL.match(line)
            if mc and mc.group(1) in ('wfcSetNormal', 'wfcSetCustomUV', 'wfcClip'):
                continue                                               # (state calls: no effect on the outputs)
            md = _DECL.match(line) or _ASSIGN.match(line)
            if not md:
                raise Unsupported('statement: %s' % line[:80])
            target, expr = md.group(1), md.group(2)
            if ctx.rt_value is None:
                expr = _RT_TERNARY.sub(lambda mm: '(' + mm.group(2) + ')', expr)
            elif isinstance(ctx.rt_value, dict):               # (named values set, every other runtime parameter unset)
                expr = _RT_TERNARY.sub(lambda mm: '(' + (mm.group(1) if mm.group(1).split('.')[0] in ctx.rt_value else mm.group(2)) + ')', expr)
                for n in _RT_NAME.findall(expr):
                    if n in ctx.rt_value:
                        env.setdefault(n, G(ctx.rt_value[n]))
            else:
                expr = _RT_TERNARY.sub(lambda mm: '(' + mm.group(1) + ')', expr)
                for n in _RT_NAME.findall(expr):
                    env.setdefault(n, G([float(ctx.rt_value)]))
            for fn in _IDENT.findall(expr):
                if fn not in _ALLOWED_FUNCS:
                    raise Unsupported('function %s' % fn)
            if '?' in expr or '[' in expr:
                raise Unsupported('ternary / index: %s' % expr[:60])
            py = re.sub(r'(?<![\w.])(\d+)\.(?![\w])', r'\1.0', expr)    # (GLSL "1." literals; not t31.rgb)
            try:
                val = eval(py, env)                                        # (matc's own output, local data)
            except Unsupported:
                raise
            except Exception as e:                                         # noqa: BLE001
                raise Unsupported('%s: %s' % (type(e).__name__, line[:80]))
            if not isinstance(val, G):
                val = G(val)
            if target.startswith('o.'):
                setattr(ctx.o, target[2:], val)
            else:
                env[target] = val
    finally:
        _STRIP[0] = False
    return {k: v for k, v in vars(ctx.o).items()}


def run(glsl, ctx):
    """Evaluates a wfcMaterial body; returns {output: numpy array}. With ctx.strip_view, DiffuseColor follows the base-colour
    strip (rules 1 and 2 above); the other outputs are evaluated normally."""
    outs = _run_once(glsl, _clone(ctx, strip_view=False))
    res = {k: v.v.copy() for k, v in outs.items()}
    if ctx.strip_view and 'DiffuseColor' in outs:
        res['_diffuse_original'] = res['DiffuseColor'].copy()
        s = _run_once(glsl, _clone(ctx, strip_view=True))['DiffuseColor']
        if s.view:                                                     # rule 2: the mean over VIEW_SET
            acc = np.zeros_like(s.v)
            for d in VIEW_SET:
                acc += _run_once(glsl, _clone(ctx, strip_view=True, camera=d, refl=d, light=d))['DiffuseColor'].v
            res['DiffuseColor'] = np.maximum(acc / len(VIEW_SET), 0.0)
            res['_base_rule'] = np.array([2.0])
        else:                                                          # (1: an additive view term was dropped; 0: none)
            res['_base_rule'] = np.array([1.0 if not np.allclose(s.v, res['DiffuseColor']) else 0.0])
            res['DiffuseColor'] = np.maximum(s.v, 0.0)
    return res


def _clone(ctx, **over):
    m = ctx.m
    kw = dict(samples2d=ctx.samples2d, samples_cube=ctx.samples_cube, uv0=tuple(m.uv0.v), camera=tuple(m.cameraVector.v),
              refl=tuple(m.reflectionVector.v), light=tuple(m.lightVector.v), vertex_color=tuple(m.vertexColor.v),
              time=float(m.time.v[0]), images=ctx.images, rt_value=ctx.rt_value, strip_view=ctx.strip_view)
    kw.update(over)
    return Material(**kw)


def deps(glsl):
    """Static dataflow: for each output (o.X), the 2D / cube sample slots it depends on, and whether a view input reaches it."""
    dep, out = {}, {}
    for raw in glsl.split('\n'):
        line = raw.strip()
        md = _DECL.match(line) or _ASSIGN.match(line)
        if not md:
            continue
        target, expr = md.group(1), md.group(2)
        d2, dc = set(), set()
        view = bool(re.search(r'm\.(cameraVector|reflectionVector|lightVector)', expr))
        for m in re.finditer(r'wfcSample2D\w*\(\s*(\d+)', expr):
            d2.add(int(m.group(1)))
        for m in re.finditer(r'wfcSampleCube\w*\(\s*(\d+)', expr):
            dc.add(int(m.group(1)))
        for t in re.findall(r'\bt\d+\b', expr):
            if t in dep:
                d2 |= dep[t]['2d']; dc |= dep[t]['cube']; view = view or dep[t]['view']
        rec = {'2d': d2, 'cube': dc, 'view': view}
        if target.startswith('o.'):
            out[target[2:]] = rec
        else:
            dep[target] = rec
    return out


def roughness_from_power(sp):
    """UE3 Phong / Blinn-Phong exponent -> GGX roughness (alpha = sqrt(2 / (n + 2)), the standard equivalence)."""
    sp = np.maximum(np.asarray(sp, dtype=np.float64), 0.0)
    return np.clip(np.sqrt(2.0 / (sp + 2.0)), 0.02, 1.0)


def power_from_roughness(r):
    return 2.0 / (np.asarray(r, dtype=np.float64) ** 2) - 2.0


assert math.isclose(float(power_from_roughness(roughness_from_power(40.0))), 40.0, rel_tol=1e-12)
