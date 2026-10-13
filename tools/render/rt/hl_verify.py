# matc HLSL backend verification (M7 stage 2): every material body evaluated twice in numpy on the same random inputs -
# the GLSL (materials_glsl.json, GLSL semantics: gl2py's environment) and its HLSL translation (tools/render/matc_hlsl.py,
# HLSL semantics: lerp / frac / wfcMod / mul(v, float2x2) / scalar .xxx splats evaluated as HLSL defines them) - and
# every temporary and output compared. Unlike gl2py alone, mat2 is evaluated for real here (GLSL column-major vs the
# HLSL row form), so the rotators' sample coordinates are checked too. Texture fetches return per-slot random values
# (shared); screen-space helpers (depth-biased alpha, scene colour / UV) are stubbed identically on both sides.
#   python hl_verify.py <render_rt dir> [trials]
import glob, json, os, re, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import gl2py  # noqa: E402
from gl2py import G, g  # noqa: E402
import matc_hlsl  # noqa: E402

M_IN = matc_hlsl.M_FIELDS


def common(E, T):
    A = lambda x: x if isinstance(x, np.ndarray) else g(x, T)
    E.update({'length': lambda v: np.sqrt((np.asarray(A(v)) ** 2).sum(axis=0, keepdims=True)).view(G),
              'distance': lambda a, b: np.sqrt(((np.asarray(A(a)) - np.asarray(A(b))) ** 2).sum(axis=0, keepdims=True)).view(G),
              'step': lambda e, x: (np.asarray(A(x)) >= np.asarray(A(e))).astype(np.float64).view(G),
              'smoothstep': lambda a, b, x: (lambda t: (t * t * (3 - 2 * t)).view(G))(np.clip((np.asarray(A(x)) - A(a)) / (np.asarray(A(b)) - A(a)), 0, 1)),
              'sign': lambda x: np.sign(A(x)).view(G), 'exp': lambda x: np.exp(A(x)).view(G),
              'exp2': lambda x: np.exp2(A(x)).view(G), 'log': lambda x: np.log(A(x)).view(G),
              'log2': lambda x: np.log2(A(x)).view(G), 'tan': lambda x: np.tan(A(x)).view(G),
              'asin': lambda x: np.arcsin(A(x)).view(G), 'acos': lambda x: np.arccos(A(x)).view(G),
              'atan': lambda x: np.arctan(A(x)).view(G)})
    return A


class GMat2:                                     # GLSL mat2(a, b, c, d): columns (a, b), (c, d); M * v
    def __init__(self, a): self.a = a

    def __mul__(self, v):
        a, b, c, d = [np.asarray(x) for x in self.a]
        v = np.asarray(v)
        return np.concatenate([a * v[0:1] + c * v[1:2], b * v[0:1] + d * v[1:2]], axis=0).view(G)


def run(body, E, T, minp, hlsl):
    class Obj: pass
    m, o = Obj(), Obj()
    for k, v in minp.items():
        if k != 'tbnUE': setattr(m, k, v)
    for k, n in matc_hlsl.O_FIELDS.items(): setattr(o, k, g(np.zeros(n), T))   # (HLSL: o = (MatOut)0)
    E['m'] = m; E['o'] = o
    vals = {}
    code = body if hlsl else re.sub(r'\(uRTSet_\w+ != 0 \? uRT_\w+(?:\.[xyzw]+)? : ', '(', body)
    for line in code.splitlines():
        s = line.strip()
        if not s or s.startswith('//'): continue
        if s.startswith('wfcSetNormal('):
            n = E['normalize'](o.Normal); m.normal = n
            m.reflectionVector = (-np.asarray(m.cameraVector) + np.asarray(n) * np.asarray(E['dot'](n, m.cameraVector)) * 2.0).view(G)
            continue
        if s.startswith('o = (MatOut)0'): continue
        mm = re.match(r'(?:(?:float[234]?|vec[234])\s+)?([\w.]+)\s*=\s*(.*);$', s)
        if not mm: raise SyntaxError(s)
        lhs, rhs = mm.groups()
        if hlsl:                                 # (literals as arrays: (1.0).xxx splats need swizzles)
            rhs = re.sub(r'(?<![\w.])(\d+\.\d*(?:[eE][-+]?\d+)?)', r'_lit(\1)', rhs)
        val = eval(rhs, E)
        if not isinstance(val, np.ndarray): val = g(val, T)
        val = np.asarray(val, dtype=np.float64)
        dt = re.match(r'(float[234]?|vec[234])\s', s)
        if dt:                                   # declared size (a scalar result broadcasts on the GLSL side)
            n = {'float': 1, 'float2': 2, 'float3': 3, 'float4': 4, 'vec2': 2, 'vec3': 3, 'vec4': 4}[dt.group(1)]
            if val.shape[0] == 1 and n > 1: val = np.repeat(val, n, axis=0)
        val = val.view(G)
        if lhs.startswith('o.'): setattr(o, lhs[2:], val)
        elif lhs.startswith('m.'): setattr(m, lhs[2:], val)
        else: E[lhs] = val
        vals[lhs] = np.asarray(val)
    return vals, {k: np.asarray(getattr(o, k)) for k in matc_hlsl.O_FIELDS}


def envs(T, slots, minp):
    tbn = minp['tbnUE']
    Egl = gl2py.env(T, slots, minp); common(Egl, T)
    Egl['mat2'] = lambda *a: GMat2(a)
    Egl['wfcDepthBiasedAlpha'] = lambda m, a, b, s: a
    Egl['wfcSceneColor'] = lambda uv: g(np.zeros(4), T)
    Egl['wfcScreenUV'] = lambda: g(np.full(2, 0.5), T)
    Ehl = gl2py.env(T, slots, minp); A = common(Ehl, T)
    for k in ('vec2', 'vec3', 'vec4', 'mix', 'fract', 'mod', 'mat2', 'wfcSample2D', 'wfcSampleCube', 'wfcSampleCubeBias'):
        Ehl.pop(k, None)                         # (the HLSL text must not reach GLSL-only names)
    Ehl.update({'_lit': lambda x: g(x, T), 'float2': Egl['vec2'], 'float3': Egl['vec3'], 'float4': Egl['vec4'],
                'lerp': lambda a, b, t: (A(a) * (1.0 - A(t)) + A(b) * A(t)).view(G),
                'frac': lambda x: (A(x) - np.floor(A(x))).view(G),
                'wfcMod': lambda a, b: (A(a) - A(b) * np.floor(A(a) / A(b))).view(G),
                'float2x2': lambda *a: tuple(np.asarray(A(x)) for x in a),
                'mul': lambda v, M: np.concatenate([np.asarray(v)[0:1] * M[0] + np.asarray(v)[1:2] * M[2],
                                                    np.asarray(v)[0:1] * M[1] + np.asarray(v)[1:2] * M[3]], axis=0).view(G),
                'wfcTex2D': lambda m, k, uv: slots[('2d', int(k))],
                'wfcTexCube': lambda m, k, d, b: slots[('cube', int(k))],
                'tangentToWorldUE': lambda m, v: tbn(v), 'tangentToView': lambda m, v: tbn(v),
                'tangentToLocal': lambda m, v: v, 'wfcNormalNode': lambda m: tbn(m.normal),
                'wfcDepthBiasedAlpha': lambda m, a, b, s: a,
                'wfcSceneColor': lambda m, uv: g(np.zeros(4), T), 'wfcScreenUV': lambda m: g(np.full(2, 0.5), T)})
    return Egl, Ehl


def inputs(rng, T, nslot):
    minp = {}
    for k, n in M_IN.items():
        minp[k] = g(rng.uniform(-1, 1, (n, T)) if k not in ('time', 'pixelDepth', 'sceneDepth') else rng.uniform(0.1, 50, (n, T)), T)
    for k in ('cameraVector', 'normal'):
        v = np.asarray(minp[k]); minp[k] = (v / np.linalg.norm(v, axis=0, keepdims=True)).view(G)
    Q = np.linalg.qr(rng.normal(size=(3, 3)))[0]
    minp['tbnUE'] = lambda v: (Q @ np.asarray(v)).view(G)
    slots = {}
    for k in range(nslot):
        slots[('2d', k)] = g(rng.uniform(0.02, 1, (4, T)), T)
        slots[('cube', k)] = g(rng.uniform(0.02, 1, (4, T)), T)
    return minp, slots


def main():
    root = sys.argv[1]; T = int(sys.argv[2]) if len(sys.argv) > 2 else 64
    tot = bad = skipped = 0
    worst = []
    for f in sorted(glob.glob(os.path.join(root, 'MP_*', 'materials_glsl.json'))):
        mapn = os.path.basename(os.path.dirname(f))
        mats = json.load(open(f, encoding='utf-8'))
        for k, (name, mt) in enumerate(sorted(mats.items())):
            if mt.get('error') not in (None, 'None') or not mt.get('glsl'): continue
            glsl = mt['glsl']
            hl = matc_hlsl.translate_body(glsl)
            nslot = 1 + max([int(x) for x in re.findall(r'wfcSample(?:2D|Cube|CubeBias)\((\d+)', glsl)] or [0])
            rng = np.random.default_rng(k * 7919 + len(name))
            minp, slots = inputs(rng, T, nslot)
            Egl, Ehl = envs(T, slots, minp)
            with np.errstate(all='ignore'):
                try:
                    vg, og = run(glsl, Egl, T, minp, False)
                except Exception as ex:          # (the GLSL emulator itself cannot run it: not a translation result)
                    skipped += 1; continue
                minp2, slots2 = inputs(np.random.default_rng(k * 7919 + len(name)), T, nslot)
                Egl2, Ehl = envs(T, slots2, minp2)
                vh, oh = run(hl, Ehl, T, minp2, True)
            tot += 1
            err = 0.0; where = ''
            for key in list(vg) + ['o.' + x for x in og]:
                a = vg.get(key) if not key.startswith('o.') else og[key[2:]]
                b = vh.get(key) if not key.startswith('o.') else oh[key[2:]]
                if b is None or a.shape != b.shape: err = float('inf'); where = key + ' shape'; break
                fin = np.isfinite(a) & np.isfinite(b)
                if (np.isfinite(a) != np.isfinite(b)).any(): err = float('inf'); where = key + ' nan/inf'; break
                d = np.abs(a - b)[fin] / np.maximum(1.0, np.abs(a[fin]))
                if d.size and d.max() > err: err = float(d.max()); where = key
            if err > 1e-9:
                bad += 1; worst.append((err, mapn, name, where))
    worst.sort(reverse=True)
    print('%d materials compared (%d trials each, every temporary + output): %d differ (> 1e-9 relative), %d skipped (GLSL emulator)'
          % (tot, T, bad, skipped))
    for w in worst[:15]: print('  %.3g %s %s @ %s' % w)


if __name__ == '__main__':
    main()
