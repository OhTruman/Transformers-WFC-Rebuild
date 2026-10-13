"""A numeric emulator for matc's generated material GLSL (M6, Systems; tools/render/matc.py emits one wfcMaterial body
per material instance: SSA lines "TYPE tN = EXPR;", "o.Output = EXPR;", wfcSample2D / cube samples, mix / dot / clamp /
pow ...). Used as the oracle for the M6 "pbr" mapping: evaluate a material's original body with chosen texture samples,
evaluate a mapped variant on the same inputs, compare.

Not a GLSL compiler: the subset matc emits, evaluated in float64 with numpy. Anything outside the subset raises
Unsupported (the caller records the material as not emulated). Texture samples come from a table the caller provides
(slot -> 4 floats), so a test is reproducible; tangent / world transforms are identity (the mapping does not change them).
"""
import math
import re

import numpy as np


class Unsupported(Exception):
    pass


_SW = {'x': 0, 'y': 1, 'z': 2, 'w': 3, 'r': 0, 'g': 1, 'b': 2, 'a': 3, 's': 0, 't': 1, 'p': 2, 'q': 3}


class G:
    """A GLSL value: float (size 1) or vecN, with swizzles and broadcasting arithmetic."""
    __slots__ = ('v',)

    def __init__(self, v):
        self.v = np.atleast_1d(np.asarray(v, dtype=np.float64))

    def __getattr__(self, name):
        if name and all(c in _SW for c in name):
            idx = [_SW[c] for c in name]
            if max(idx) >= self.v.size:
                if self.v.size == 1:
                    return G(np.repeat(self.v, len(idx)))           # (scalar.xxx)
                raise Unsupported('swizzle .%s of a vec%d' % (name, self.v.size))
            return G(self.v[idx])
        raise AttributeError(name)

    def _o(self, other):
        return other.v if isinstance(other, G) else np.float64(other)

    def __add__(self, o): return G(self.v + self._o(o))
    def __radd__(self, o): return G(self._o(o) + self.v)
    def __sub__(self, o): return G(self.v - self._o(o))
    def __rsub__(self, o): return G(self._o(o) - self.v)
    def __mul__(self, o): return G(self.v * self._o(o))
    def __rmul__(self, o): return G(self._o(o) * self.v)
    def __truediv__(self, o):
        with np.errstate(divide='ignore', invalid='ignore'):
            return G(self.v / self._o(o))
    def __rtruediv__(self, o):
        with np.errstate(divide='ignore', invalid='ignore'):
            return G(self._o(o) / self.v)
    def __neg__(self): return G(-self.v)
    def __float__(self): return float(self.v[0])

    def __repr__(self):
        return 'G(%s)' % np.array2string(self.v, precision=6)


def _flat(args):
    out = []
    for a in args:
        out.extend((a.v if isinstance(a, G) else np.atleast_1d(np.float64(a))).tolist())
    return out


def _ctor(n):
    def f(*args):
        vals = _flat(args)
        if len(vals) == 1:
            vals = vals * n
        if len(vals) < n:
            raise Unsupported('vec%d from %d values' % (n, len(vals)))
        return G(vals[:n])
    return f


def _v(x):
    return x.v if isinstance(x, G) else np.float64(x)


def _mix(a, b, t): return G(_v(a) * (1.0 - _v(t)) + _v(b) * _v(t))
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


class Mat2:
    """GLSL mat2 (column-major constructor: a, b = column 0; c, d = column 1); only mat2 * vec2 is needed."""
    def __init__(self, a, b, c, d):
        self.m = np.array([[float(_v(a)[0]) if isinstance(a, G) else float(a), float(_v(c)[0]) if isinstance(c, G) else float(c)],
                           [float(_v(b)[0]) if isinstance(b, G) else float(b), float(_v(d)[0]) if isinstance(d, G) else float(d)]])
    def __mul__(self, o):
        return G(self.m @ _v(o)[:2])


def _mat2(a, b, c, d): return Mat2(a, b, c, d)


class Material:
    """One emulation context: m (inputs), o (outputs), the sample tables."""

    def __init__(self, samples2d, samples_cube, uv0=(0.25, 0.75), refl=(0.0, 0.0, 1.0), vertex_color=(1, 1, 1, 1),
                 time=0.0, cube_scale=1.0):
        self.samples2d, self.samples_cube, self.cube_scale = samples2d, samples_cube, cube_scale

        class M:
            def __getattr__(self, name):                                  # (inputs this emulator does not model: 0)
                return G((0.0, 0.0, 0.0))
        m = M()
        m.uv0 = G(uv0); m.uv1 = G(uv0); m.reflectionVector = G(refl); m.vertexColor = G(vertex_color)
        m.cameraVector = G((0.0, 0.0, 1.0)); m.worldPosition = G((0.0, 0.0, 0.0)); m.time = G([time])
        m.screenPos = G((0.5, 0.5, 0.0, 1.0)); m.worldNormal = G((0.0, 0.0, 1.0)); m.lightVector = G((0.0, 0.0, 1.0))
        m.normal = G((0.0, 0.0, 1.0)); m.worldPosUE = G((0.0, 0.0, 0.0))
        self.m = m

        class O:
            pass
        self.o = O()

    def sample2d(self, slot, *rest):
        s = int(float(slot))
        if s not in self.samples2d:
            raise Unsupported('no sample for 2D slot %d' % s)
        return G(self.samples2d[s])

    def sample_cube(self, slot, *rest):
        s = int(float(slot))
        if s not in self.samples_cube:
            raise Unsupported('no sample for cube slot %d' % s)
        return G(np.asarray(self.samples_cube[s], dtype=np.float64) * self.cube_scale)


_DECL = re.compile(r'^\s*(?:float|vec[234]|int|bool|mat[34])\s+(t\d+)\s*=\s*(.*);\s*$')
_ASSIGN = re.compile(r'^\s*(o\.\w+)\s*=\s*(.*);\s*$')
_CALL = re.compile(r'^\s*(wfc\w+)\s*\((.*)\)\s*;\s*$')
_IDENT = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
_ALLOWED_FUNCS = {'vec2', 'vec3', 'vec4', 'float', 'mix', 'dot', 'clamp', 'pow', 'sqrt', 'normalize', 'length', 'abs', 'floor',
                  'fract', 'min', 'max', 'step', 'smoothstep', 'exp2', 'log2', 'sin', 'cos', 'cross', 'sign', 'mod', 'reflect',
                  'wfcSample2D', 'wfcSample2DBias', 'wfcSample2DLevel', 'wfcSampleCube', 'wfcSampleCubeBias', 'wfcSampleCubeLevel',
                  'tangentToWorldUE', 'worldToTangentUE', 'mat2', 'wfcNormalNode'}
# a runtime parameter's "set ? value : default" (customisation tints): emulated as unset - the material's own default
_RT_TERNARY = re.compile(r'\(\s*uRTSet_\w+\s*!=\s*0\s*\?\s*uRT_\w+\s*:\s*(vec[234]\([^()]*\)|[-\d.e]+)\s*\)')


def run(glsl, ctx):
    """Evaluates a wfcMaterial body; returns ctx.o's attributes as a dict of numpy arrays."""
    env = {'m': ctx.m, 'o': ctx.o, 'vec2': _ctor(2), 'vec3': _ctor(3), 'vec4': _ctor(4),
           'float': lambda x: G([float(np.atleast_1d(_v(x))[0])]), 'mat2': _mat2, 'wfcNormalNode': lambda m: m.normal,
           'mix': _mix, 'dot': _dot, 'clamp': _clamp, 'pow': _pow, 'sqrt': _sqrt, 'normalize': _normalize, 'length': _length,
           'abs': _abs, 'floor': _floor, 'fract': _fract, 'min': _min, 'max': _max, 'step': _step, 'smoothstep': _smoothstep,
           'exp2': _exp2, 'log2': _log2, 'sin': _sin, 'cos': _cos, 'cross': _cross, 'sign': _sign, 'mod': _mod,
           'reflect': _reflect, 'wfcSample2D': ctx.sample2d, 'wfcSample2DBias': ctx.sample2d, 'wfcSample2DLevel': ctx.sample2d,
           'wfcSampleCube': ctx.sample_cube, 'wfcSampleCubeBias': ctx.sample_cube, 'wfcSampleCubeLevel': ctx.sample_cube,
           'tangentToWorldUE': lambda m, v: v, 'worldToTangentUE': lambda m, v: v, '__builtins__': {}}
    for raw in glsl.split('\n'):
        line = raw.strip()
        if not line or line.startswith('//'):
            continue
        mc = _CALL.match(line)
        if mc and mc.group(1) in ('wfcSetNormal', 'wfcSetCustomUV', 'wfcClip'):
            continue                                                   # (state calls: no effect on the outputs)
        md = _DECL.match(line) or _ASSIGN.match(line)
        if not md:
            raise Unsupported('statement: %s' % line[:80])
        target, expr = md.group(1), md.group(2)
        expr = _RT_TERNARY.sub(lambda mm: '(' + mm.group(1) + ')', expr)
        for fn in _IDENT.findall(expr):
            if fn not in _ALLOWED_FUNCS:
                raise Unsupported('function %s' % fn)
        if '?' in expr or '[' in expr:
            raise Unsupported('ternary / index: %s' % expr[:60])
        py = re.sub(r'(?<![\w.])(\d+)\.(?![\w])', r'\1.0', expr)        # (GLSL "1." literals; not t31.rgb)
        try:
            val = eval(py, env)                                            # (matc's own output, local data)
        except Unsupported:
            raise
        except Exception as e:                                             # noqa: BLE001
            raise Unsupported('%s: %s' % (type(e).__name__, line[:80]))
        if not isinstance(val, G):
            val = G(val)
        if target.startswith('o.'):
            setattr(ctx.o, target[2:], val)
        else:
            env[target] = val
    return {k: v.v.copy() for k, v in vars(ctx.o).items()}


def deps(glsl):
    """Static dataflow: for each output (o.X) and temporary, the set of 2D / cube sample slots it depends on, and whether a
    cube (reflection) sample reaches it. Returns {output: {'2d': set, 'cube': set}}."""
    dep = {}
    out = {}
    for raw in glsl.split('\n'):
        line = raw.strip()
        md = _DECL.match(line) or _ASSIGN.match(line)
        if not md:
            continue
        target, expr = md.group(1), md.group(2)
        d2, dc = set(), set()
        for m in re.finditer(r'wfcSample2D\w*\(\s*(\d+)', expr):
            d2.add(int(m.group(1)))
        for m in re.finditer(r'wfcSampleCube\w*\(\s*(\d+)', expr):
            dc.add(int(m.group(1)))
        for t in re.findall(r'\bt\d+\b', expr):
            if t in dep:
                d2 |= dep[t]['2d']; dc |= dep[t]['cube']
        rec = {'2d': d2, 'cube': dc}
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
