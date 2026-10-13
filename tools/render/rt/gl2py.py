# Evaluate a translated material body (materials_glsl.json 'glsl', the matc.py subset of GLSL) in numpy, vectorised
# over trials: every value is a G array of shape (n, T) (n = 1 for float). Texture samples return per-slot values
# supplied by the caller (the sample coordinates are ignored), m.* inputs come from the caller.
import re, numpy as np


class G(np.ndarray):
    def __getattr__(self, k):
        if k.startswith('_') or not re.fullmatch(r'[xyzw]{1,4}|[rgba]{1,4}', k):
            raise AttributeError(k)
        idx = ['xyzwrgba'.index(c) % 4 for c in k]
        return np.asarray(self)[idx].view(G)


def g(x, T):
    a = np.asarray(x, dtype=np.float64)
    if a.ndim == 0: a = np.full((1, T), float(a))
    elif a.ndim == 1: a = a[:, None] * np.ones((1, T))
    elif a.shape[1] == 1: a = a * np.ones((1, T))
    return a.view(G)


class Mat2:
    def __mul__(self, v): return v        # uv rotators: only sample coordinates use them


def env(T, slots, minp):
    def ctor(n):
        def f(*args):
            parts = [np.asarray(a if isinstance(a, np.ndarray) else g(a, T)) for a in args]
            parts = [p if p.ndim == 2 else p[None] for p in parts]
            if len(parts) == 1 and parts[0].shape[0] == 1:
                return np.repeat(parts[0], n, axis=0).view(G)
            c = np.concatenate(parts, axis=0)
            return c[:n].view(G)
        return f

    def A(x): return x if isinstance(x, np.ndarray) else g(x, T)

    def dot(a, b): return (np.asarray(A(a)) * np.asarray(A(b))).sum(axis=0, keepdims=True).view(G)

    def mix(a, b, t): return A(a) * (1.0 - A(t)) + A(b) * A(t)

    def clamp(x, lo, hi): return np.minimum(np.maximum(A(x), A(lo)), A(hi)).view(G)

    def pw(x, y):
        x = A(x); y = A(y)
        with np.errstate(all='ignore'):
            r = np.where(x > 0, np.power(np.maximum(x, 1e-300), y), np.where(x == 0, np.where(y > 0, 0.0, np.nan), np.nan))
        return r.view(G)

    def normalize(v):
        v = A(v)
        return (v / np.sqrt((np.asarray(v) ** 2).sum(axis=0, keepdims=True))).view(G)

    def cross(a, b):
        a = np.asarray(A(a)); b = np.asarray(A(b))
        return np.stack([a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]).view(G)

    def sample2d(n, uv, *rest): return slots[('2d', n)]

    def samplecube(n, d, *rest): return slots[('cube', n)]

    def unsupported(*a): raise NotImplementedError('unsupported GLSL helper')

    e = {'vec2': ctor(2), 'vec3': ctor(3), 'vec4': ctor(4), 'float': ctor(1), 'dot': dot, 'mix': mix, 'clamp': clamp,
         'pow': pw, 'sqrt': lambda x: np.sqrt(A(x)).view(G), 'sin': lambda x: np.sin(A(x)).view(G),
         'cos': lambda x: np.cos(A(x)).view(G), 'abs': lambda x: np.abs(A(x)).view(G),
         'max': lambda a, b: np.maximum(A(a), A(b)).view(G), 'min': lambda a, b: np.minimum(A(a), A(b)).view(G),
         'normalize': normalize, 'fract': lambda x: (A(x) - np.floor(A(x))).view(G),
         'floor': lambda x: np.floor(A(x)).view(G), 'ceil': lambda x: np.ceil(A(x)).view(G),
         'mod': lambda a, b: (A(a) - A(b) * np.floor(A(a) / A(b))).view(G), 'cross': cross,
         'mat2': lambda *a: Mat2(), 'wfcSample2D': sample2d, 'wfcSampleCube': samplecube,
         'wfcSampleCubeBias': samplecube, 'tangentToWorldUE': lambda m, v: minp['tbnUE'](v),
         'tangentToView': lambda m, v: minp['tbnUE'](v), 'tangentToLocal': lambda m, v: v,
         'wfcNormalNode': lambda m: minp['tbnUE'](m.normal),   # matc x_Normal: the world-space material normal
         'wfcDepthBiasedAlpha': unsupported, 'wfcSceneColor': unsupported, 'wfcScreenUV': unsupported}
    return e


class Obj:
    pass


def evaluate(glsl, T, slots, minp):
    """-> dict of material outputs (o.*) as G arrays. minp: m.* values plus 'tbnUE' (callable)."""
    E = env(T, slots, minp)
    m = Obj()
    for k, v in minp.items():
        if k != 'tbnUE': setattr(m, k, v)
    o = Obj()
    E['m'] = m; E['o'] = o
    code = re.sub(r'\(uRTSet_\w+ != 0 \? [^:]+ : ', '(', glsl)
    for line in code.splitlines():
        s = line.strip()
        if not s or s.startswith('//'): continue
        if s.startswith('wfcSetNormal('):
            n = E['normalize'](o.Normal)
            m.normal = n
            m.reflectionVector = (-np.asarray(m.cameraVector) + np.asarray(n) * np.asarray(E['dot'](n, m.cameraVector)) * 2.0).view(G)
            continue
        if not s.endswith(';'): raise SyntaxError(s)
        s = s[:-1]
        mm = re.match(r'(?:(?:float|vec[234])\s+)?([\w.]+)\s*=\s*(.*)$', s)
        if not mm: raise SyntaxError(s)
        lhs, rhs = mm.groups()
        rhs = rhs.replace('&&', ' and ').replace('||', ' or ')
        val = eval(rhs, E)
        if not isinstance(val, np.ndarray): val = g(val, T)
        if lhs.startswith('o.'): setattr(o, lhs[2:], val)
        elif lhs.startswith('m.'): setattr(m, lhs[2:], val)
        else: E[lhs] = val
    return o
