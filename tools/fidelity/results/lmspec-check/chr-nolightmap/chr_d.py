# (chr_emis.py: derived from lmspec_geom.py) Lightmap specular verification (Integration 2026-10-12, condition 1). For every lit translated material of a map
# whose ORIGINAL lightmapped base-pass PS is in the map's ShaderCache:
#  * emulate the original PS (xemu.py) on random texture values (fixed interpolators), and measure the lightmap
#    response K_i = dOut / dLightMap_i (the output is linear in each lightmap coefficient texel, per channel);
#  * evaluate OUR translated material (gl2py.py) on the same texture values -> DiffuseColor D, SpecularColor S,
#    SpecularPower P, Normal N;
#  * model: K_i = a * Scale_i * (D * dot(N,B_i)^2 + S * pow(max(sat(dot(normalize(N + B_i), R)), 1e-4), P)),
#    R = reflect(-V, N) in tangent space, V the (unknown, fitted) tangent-space camera vector, a the (unknown) fog
#    factor of the interpolated vertex fog. Also the diffuse-only model (today's kFSMainLM).
# lmspec_geom.py: the same with KNOWN geometry - the PS's world-position / TangentZ(+sign) / TangentX interpolators are
# identified in its code and driven per trial; V_t = UE3 tangent-space camera vector (B = cross(Z, X) * w), no V fit.
# Writes lmspec_<map>.json and prints a summary. Usage: lmspec_verify.py MAP [material-substring]
import sys, os, re, json, struct, collections, numpy as np
sys.path.insert(0, 'F:/Transformers Rebuild/Rebuild-Rendering/tools/render')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # EXPERIMENTAL copy (chr_d.py): local gl2py / xemu / pk first
H = os.path.dirname(os.path.abspath(__file__))
import xenos_dis as X, xemu, gl2py
from ue3obj import Repo, map_packages

MAP = sys.argv[1]
FILT = sys.argv[2] if len(sys.argv) > 2 else None
RD = os.environ.get('LMS_RD', 'F:/Transformers Rebuild/Rebuild/work/render') + '/' + MAP   # LMS_RD: another render root
mats = json.load(open(RD + '/materials_glsl.json', encoding='utf-8'))
scm = [x for x in json.load(open(os.path.join(H, os.environ.get('CHR_SCALL', 'scall_%s_all.json' % MAP)))) if 'NoLightMapPolicy' in x['type']]
pk, _ = map_packages(MAP)
REPO = Repo(list(reversed(pk)), fallback=['TransGame.xxx', 'TR_AllShader_p.xxx'] + [q for q in os.environ.get('CHR_PKGS', '').split(',') if q] + sorted({k.split('.')[0] + '.xxx' for k in mats if k.startswith(('TR_', 'WEP_'))}))   # + the character / weapon packages (MIC parameter values)
blobs = {}
def blob(x):
    k = 'sc_%s_%s.bin' % (os.environ.get('CHR_BLOBTAG', MAP), x['export'].split('.')[-1])
    if k not in blobs: blobs[k] = open(os.path.join(H, k), 'rb').read()
    return blobs[k]
by_owner = collections.defaultdict(list)
for x in scm:
    for mname in x['materials']: by_owner[mname.lower()].append(x)

B3 = np.array([[0.0, 0.81649658, 0.57735027], [-0.70710678, -0.40824829, 0.57735027], [0.70710678, -0.40824829, 0.57735027]])

# ------------------------------------------------------------------ uniform expressions of a material resource
class UE:
    def __init__(self, p, t):
        self.p, self.t = p, t
    def name(self, o):
        i, n = struct.unpack_from('>ii', self.t, o)
        return self.p.names[i] if 0 <= i < len(self.p.names) and n == 0 else None
    def path(self, ref):
        try: return self.p.object_path(ref) if ref else None
        except Exception: return None
    def expr(self, o):
        """-> (tree, next offset). tree = (class, args...)"""
        c = self.name(o)
        if not c or not c.startswith('FMaterialUniformExpression'): raise ValueError('not an expression @%d' % o)
        k = c[len('FMaterialUniformExpression'):]
        o += 8
        if k == 'VectorParameter':
            nm = self.name(o); v = struct.unpack_from('>4f', self.t, o + 8); return ('vparam', nm, v), o + 24
        if k == 'ScalarParameter':
            nm = self.name(o); v = struct.unpack_from('>f', self.t, o + 8)[0]; return ('sparam', nm, v), o + 12
        if k == 'Constant':
            v = struct.unpack_from('>4f', self.t, o); return ('const', v), o + 17
        if k in ('Time', 'RealTime'): return (k.lower(),), o
        if k in ('Sine',):
            a, o = self.expr(o); cosf = struct.unpack_from('>i', self.t, o)[0]; return ('sine', a, cosf), o + 4
        if k in ('SquareRoot', 'Length', 'Floor', 'Ceil', 'Frac', 'Abs', 'Periodic'):
            a, o = self.expr(o); return (k.lower(), a), o
        if k == 'FoldedMath':
            a, o = self.expr(o); b, o = self.expr(o); op = self.t[o]; return ('math', a, b, op), o + 1
        if k in ('Min', 'Max'):
            a, o = self.expr(o); b, o = self.expr(o); return (k.lower(), a, b), o
        if k == 'Clamp':
            a, o = self.expr(o); b, o = self.expr(o); c2, o = self.expr(o); return ('clamp', a, b, c2), o
        if k == 'AppendVector':
            a, o = self.expr(o); b, o = self.expr(o); n = struct.unpack_from('>i', self.t, o)[0]; return ('append', a, b, n), o + 4
        if k == 'TextureSetIntermediateParameter':
            nm = self.name(o); ref = struct.unpack_from('>i', self.t, o + 8)[0]; idx = struct.unpack_from('>i', self.t, o + 12)[0]
            return ('texset', nm, self.path(ref), idx), o + 16
        if k == 'TextureParameter':
            nm = self.name(o); ref = struct.unpack_from('>i', self.t, o + 8)[0]; return ('texparam', nm, self.path(ref)), o + 12
        if k == 'Texture':
            ref = struct.unpack_from('>i', self.t, o)[0]; return ('tex', self.path(ref)), o + 4
        raise ValueError('unknown expression ' + c)
    def lists(self):
        for s0 in range(0, 96, 4):
            try:
                o = s0; out = []
                for _ in range(4):
                    n = struct.unpack_from('>i', self.t, o)[0]; o += 4
                    if not 0 <= n < 200: raise ValueError('count')
                    L = []
                    for _ in range(n):
                        e, o = self.expr(o); L.append(e)
                    out.append(L)
                if sum(len(L) for L in out) == 0: continue
                if not all(e[0] in ('texset', 'texparam', 'tex') for L in out[2:] for e in L): continue
                return out
            except (ValueError, struct.error):
                continue
        return None

def param_values(chain):
    from ue3obj import struct_dict
    sc, vc = {}, {}
    for path in chain:                       # child first: the first value found wins
        ob = REPO.obj(path) or {}
        for e in ob.get('ScalarParameterValues') or []:
            d = struct_dict(e); nm = d.get('ParameterName')
            if nm and nm not in sc: sc[nm] = float(d.get('ParameterValue') or 0.0)
        for e in ob.get('VectorParameterValues') or []:
            d = struct_dict(e); nm = d.get('ParameterName'); v = d.get('ParameterValue')
            if isinstance(v, dict): v = [v.get(k, 0.0) for k in 'RGBA']
            if nm and nm not in vc and v is not None: vc[nm] = tuple(float(c) for c in v)
    return sc, vc

def tex_values(chain):
    from ue3obj import struct_dict
    tv = {}
    for path in chain:
        ob = REPO.obj(path) or {}
        for fld in ('TextureParameterValues', 'TextureSetParameterValues'):
            for e in ob.get(fld) or []:
                d = struct_dict(e); nm = d.get('ParameterName'); r = (d.get('ParameterValue') or {})
                r = r.get('ref') if isinstance(r, dict) else None
                if nm and (fld, nm) not in tv: tv[(fld, nm)] = r
    return tv

def assignments(t2d, chain, slots2d):
    import itertools
    tv = tex_values(chain)
    cands = []
    for e in t2d:
        objs = [tx['object'].lower() for tx in slots2d]
        if e[0] == 'tex': r = e[1]; c = [j for j, o in enumerate(objs) if r and o == r.lower()]
        elif e[0] == 'texparam':
            r = tv.get(('TextureParameterValues', e[1]), e[2]); c = [j for j, o in enumerate(objs) if r and o == r.lower()]
        else:
            r = tv.get(('TextureSetParameterValues', e[1]), e[2]); c = [j for j, o in enumerate(objs) if r and o.startswith(r.lower() + '.')]
        cands.append(c or list(range(len(slots2d))))
    out = []
    for combo in itertools.product(*cands):
        if len(set(combo)) == len(combo): out.append(combo)
        if len(out) >= 24: break
    return out or [tuple(range(min(len(t2d), len(slots2d))))]

TIME = 7.3
def ev(e, sc, vc):
    k = e[0]
    if k == 'vparam': return np.array(vc.get(e[1], e[2]))
    if k == 'sparam': return np.array([sc.get(e[1], e[2])] * 4)
    if k == 'const': return np.array(e[1])
    if k in ('time', 'realtime'): return np.array([TIME] * 4)
    if k == 'sine': a = ev(e[1], sc, vc); return np.cos(a) if e[2] else np.sin(a)
    if k == 'squareroot': return np.sqrt(ev(e[1], sc, vc))
    if k == 'abs': return np.abs(ev(e[1], sc, vc))
    if k == 'floor': return np.floor(ev(e[1], sc, vc))
    if k == 'ceil': return np.ceil(ev(e[1], sc, vc))
    if k == 'frac': a = ev(e[1], sc, vc); return a - np.floor(a)
    if k == 'periodic': a = ev(e[1], sc, vc); return a - np.floor(a)
    if k == 'math':
        a = ev(e[1], sc, vc); b = ev(e[2], sc, vc)
        return [a + b, a - b, a * b, a / np.where(b == 0, 1e-30, b), np.array([np.dot(a, b)] * 4)][e[3]]
    if k == 'min': return np.minimum(ev(e[1], sc, vc), ev(e[2], sc, vc))
    if k == 'max': return np.maximum(ev(e[1], sc, vc), ev(e[2], sc, vc))
    if k == 'clamp': return np.clip(ev(e[1], sc, vc), ev(e[2], sc, vc), ev(e[3], sc, vc))
    if k == 'append':
        a = ev(e[1], sc, vc); b = ev(e[2], sc, vc); n = e[3]; return np.concatenate([a[:n], b])[:4]
    if k == 'length': a = ev(e[1], sc, vc); return np.array([np.sqrt((a[:3] ** 2).sum())] * 4)
    raise ValueError(k)

# ------------------------------------------------------------------ one material
def fib(n):
    i = np.arange(n) + 0.5; phi = np.arccos(1 - 2 * i / n); th = np.pi * (1 + 5 ** 0.5) * i
    return np.stack([np.cos(th) * np.sin(phi), np.sin(th) * np.sin(phi), np.cos(phi)], 1)

def verify(key, v, x, T=200, seed=3):
    info = v['info']
    s = X.Shader(blob(x), x['off'])
    cn = {c['name']: c for c in s.consts}
    owner = info['_owner']
    if owner.startswith('BRUTE:'): owner = x['materials'][0]          # EXPERIMENTAL: the candidate map's compiling material
    p, t = REPO.native_tail(owner)
    L = UE(p, t).lists()
    if L is None: return {'status': 'uniforms-unparsed'}
    vecs, scal, t2d, tcube = L
    sc, vc = param_values(info.get('chain') or [key])
    rng = np.random.default_rng(seed)
    consts = {}
    for c in s.consts:
        if c['set'] != 'float': continue
        nm, r0, n = c['name'], c['reg'], max(1, c['count'])
        m = re.fullmatch(r'Uniform(Vector|Scalar)_(\d+)', nm)
        if m:
            lst = vecs if m.group(1) == 'Vector' else scal
            j = int(m.group(2))
            if j >= len(lst): return {'status': 'uniform-index'}
            val = ev(lst[j], sc, vc)
            consts[r0] = val if m.group(1) == 'Vector' else np.array([val[0]] * 4)
        elif nm == 'SceneColorBiasFactor': consts[r0] = np.array([1.0, 1, 1, 1])
        elif nm == 'NoLightMapColor': consts[r0] = np.zeros(4)                 # emissive only (+ NoLightMapColor * Diffuse)
        elif nm == 'LightMapScale':
            sv = rng.uniform(0.5, 2.0, (3, 4))
            for k in range(n): consts[r0 + k] = sv[k % 3]
        elif nm == 'LightMapLerpAmount': consts[r0] = np.array([0.37] * 4)
        elif nm == 'WorldCameraPosition': consts[r0] = None                                     # per trial, below
        else:
            for k in range(n): consts[r0 + k] = rng.uniform(-1, 1, 4)
    WCP = cn.get('WorldCameraPosition')
    regs0 = {r: np.repeat(rng.uniform(-1, 1, (4, 1)), T, 1) for r in range(32)}
    d = chr(10).join(s.disasm())
    wp = re.findall(r'add r\d+\.\w+, r(\d+)\.\w+, -c\d+\{PreViewTranslation\}', d)
    nr = re.findall(r'sgt r\d+\.\w+, r(\d+)\.wwww, -r\1\.wwww', d)
    tg = set()
    if nr:
        for a_, b_ in re.findall(r'mul r\d+\.\w+, r(\d+)\.yzxx, r(\d+)\.zxyy', d) + re.findall(r'mul r\d+\.\w+, r(\d+)\.zxyy, r(\d+)\.yzxx', d):
            if a_ == nr[0]: tg.add(b_)
            if b_ == nr[0]: tg.add(a_)
    if not (wp and nr and len(tg) == 1): return {'status': 'geometry-registers-unknown'}
    if WCP is None: return {'status': 'no-camera'}
    wp, nr, tg = int(wp[0]), int(nr[0]), int(tg.pop())
    if FILT: print("geometry registers", wp, nr, tg)
    def unit(a): return a / np.linalg.norm(a, axis=0, keepdims=True)
    Pw = rng.uniform(-500, 500, (3, T)); PVT = rng.uniform(-100, 100, 3)
    Zg = unit(rng.normal(size=(3, T))); Xg = rng.normal(size=(3, T)); Xg = unit(Xg - Zg * (Xg * Zg).sum(0))
    wsg = rng.choice([-1.0, 1.0], T)
    Bg = np.cross(Zg.T, Xg.T).T * wsg
    Cw = Pw + 400 * unit(Zg + 0.25 * rng.normal(size=(3, T)))   # (chr_emis) near the vertex normal: dot(N, V) high
    Vw = unit(Cw - Pw)
    Vt = np.stack([(Vw * Xg).sum(0), (Vw * Bg).sum(0), (Vw * Zg).sum(0)])
    regs0[wp] = np.vstack([Pw + PVT[:, None], np.ones((1, T))])
    regs0[nr] = np.vstack([Zg, wsg[None]])
    regs0[tg] = np.vstack([Xg, np.zeros((1, T))])
    consts[WCP['reg']] = np.vstack([Cw, np.ones((1, T))])
    # uv0: the input register of the first Texture2D fetch that is not written before it (texture-coordinate interpolator)
    UV0 = rng.uniform(-2, 2, (2, T))
    written = set(); uvreg = None
    for ln in s.disasm():
        mm = re.match(r'\s*\d+\s+tfetch r\d+\.\S+, r(\d+)\.(\w+), Texture2D_', ln)
        if mm and uvreg is None and int(mm.group(1)) not in written:
            uvreg = int(mm.group(1)); uvsw = mm.group(2)
        for w_ in re.findall(r'(?:^|\|\|)\s*(?:\d+\s+)?\w+ r(\d+)\.', ln): written.add(int(w_))
    if uvreg is not None and uvreg not in (wp, nr, tg):
        col = np.array(regs0[uvreg]); cx, cy = 'xyzw'.index(uvsw[0]), 'xyzw'.index(uvsw[1])
        col[cx] = UV0[0]; col[cy] = UV0[1]; regs0[uvreg] = col
    if 'PreViewTranslation' in cn: consts[cn['PreViewTranslation']['reg']] = np.array([*PVT, 0.0])
    # texture values: raw texels in [0,1]; ours receives raw * (1 - unpack_min) + unpack_min
    slots2d = [tx for tx in info['textures'] if tx['kind'] == '2d']
    slotsc = [tx for tx in info['textures'] if tx['kind'] == 'cube']
    raw = {}
    for k in range(8): raw['Texture2D_%d' % k] = rng.uniform(0.3, 0.7, (4, T))
    for k in range(4): raw['TextureCube_%d' % k] = rng.uniform(0.3, 0.7, (4, T))
    lm = rng.uniform(0.1, 0.9, (3, 4, T))
    # the original no-lightmap base pass: one emulation; output = Emissive (x fog) in UE3's FNoLightMapPolicy base pass
    def fetch(name, coord):
        if name.startswith('Texture2D_'): return raw[name]
        if name.startswith('TextureCube_'): return raw[name]
        return np.full((4, T), 0.5)
    try:
        o0, _ = xemu.run(s, regs0, consts, fetch, T)
    except (ValueError, KeyError) as ex:
        return {'status': 'emulate: %s' % ex}
    Ot = o0[('o', 0)][:3]
    NLC = cn.get('NoLightMapColor')
    if NLC is None: return {'status': 'no-NoLightMapColor'}
    c1 = dict(consts); c1[NLC['reg']] = np.ones(4)
    Dt = xemu.run(s, regs0, c1, fetch, T)[0][('o', 0)][:3] - Ot
    if os.environ.get('CHR_PROBE'):
        tr = []; xemu.run(s, regs0, consts, fetch, T, trace=tr)
        PROBE.update({'trace': tr, 'Vw': Vw, 'Xg': Xg, 'Bg': Bg, 'Zg': Zg, 'raw': raw, 'UV0': UV0})
    # a second camera for the same pixels: O1 - O2 keeps only the view-dependent terms (the sheen dot(N, V); the
    # cube fetch is direction-independent on both sides here)
    Cw2 = Pw + 400 * unit(Zg + 0.25 * rng.normal(size=(3, T)))
    Vw2 = unit(Cw2 - Pw)
    Vt2 = np.stack([(Vw2 * Xg).sum(0), (Vw2 * Bg).sum(0), (Vw2 * Zg).sum(0)])
    c2 = dict(consts); c2[WCP['reg']] = np.vstack([Cw2, np.ones((1, T))])
    o2, _ = xemu.run(s, regs0, c2, fetch, T)
    dO = Ot - o2[('o', 0)][:3]
    dO_rel = float(np.sqrt((dO ** 2).mean() / max((Ot ** 2).mean(), 1e-30)))
    best = None
    for combo in assignments(t2d, info.get('chain') or [key], slots2d):
        A = {k: j for k, j in enumerate(combo)}
        inv = {j: k for k, j in A.items()}
        slots = {}
        for j, tx in enumerate(slots2d):
            rv = raw['Texture2D_%d' % inv[j]] if j in inv else rng.uniform(0.3, 0.7, (4, T))
            mn = np.array(tx.get('unpack_min') or [0, 0, 0, 0])[:, None]   # the runtime's sample-time unpack (wfcSample2D)
            slots[('2d', tx['slot'])] = gl2py.g(rv * (1 - mn) + mn, T)
        for j, tx in enumerate(slotsc): slots[('cube', tx['slot'])] = gl2py.g(raw['TextureCube_%d' % j], T)
        mi = {'uv0': gl2py.g(UV0, T), 'uv1': gl2py.g(rng.uniform(0, 1, (2, 1)), T),
              'subUV2': gl2py.g(np.zeros((3, 1)), T), 'vertexColor': gl2py.g(np.ones((4, 1)), T),
              'worldPosUE': gl2py.g(Pw, T), 'time': gl2py.g(TIME, T),
              'pixelDepth': gl2py.g(500.0, T), 'screenPos': gl2py.g(np.array([0.1, 0.2, 400, 500.0]), T),
              'sceneDepth': gl2py.g(1e9, T), 'dynParam': gl2py.g(np.ones(4), T),
              'tbnUE': lambda vv: gl2py.g(Xg * np.asarray(vv)[0] + Bg * np.asarray(vv)[1] + Zg * np.asarray(vv)[2], T),
              'normal': gl2py.g(np.array([0, 0, 1.0]), T),
              'cameraVector': gl2py.g(Vt, T), 'reflectionVector': gl2py.g(np.stack([-Vt[0], -Vt[1], Vt[2]]), T)}
        out = {'status': 'ok', 'cand': x['materials'][0], 'dO_rel': dO_rel, 'shader_off': x['off'], 'type': x['type'], 'owner': owner,
               'assignment': [slots2d[j]['object'].split('.')[-1] for j in combo], 'O_rms': float(np.sqrt((Ot ** 2).mean()))}
        try:
            o = gl2py.evaluate(v['glsl'], T, slots, dict(mi))
        except Exception as ex:
            out['status'] = 'ours-error %s %s' % (type(ex).__name__, str(ex)[:80]); return out
        E = np.asarray(getattr(o, 'EmissiveColor', gl2py.g(np.zeros(3), T)))[:3]
        D = np.asarray(getattr(o, 'DiffuseColor', gl2py.g(np.zeros(3), T)))[:3]
        num = (E * Ot).sum() + (D * Dt).sum(); den = (E * E).sum() + (D * D).sum()
        a = num / den if den > 0 else 1.0                                       # one fog factor for both
        def rr(M, Tt): return float(np.sqrt(((M - Tt) ** 2).sum() / max((Tt ** 2).sum(), 1e-30)))
        def fs(M, Tt): d_ = (M * M).sum(); return float((M * Tt).sum() / d_) if d_ > 0 else 0.0
        out.update({'fog': float(a), 'D_resid': rr(a * D, Dt), 'E_resid': rr(a * E, Ot), 'D_scale': fs(D, Dt), 'E_scale': fs(E, Ot),
                    'D_rms': float(np.sqrt((Dt ** 2).mean())), 'E_rms': float(np.sqrt((Ot ** 2).mean())),
                    'resid': rr(a * np.concatenate([D, E]), np.concatenate([Dt, Ot]))})
        if best is None or out['resid'] < best['resid']: best = out
        continue
    return best

_RM = {}
def root_master(path):
    k = path.lower()
    if k in _RM: return _RM[k]
    cur = path
    for _ in range(12):
        try: par = ((REPO.obj(cur) or {}).get('Parent') or {}).get('ref')
        except Exception: par = None
        if not par: break
        cur = par
    _RM[k] = cur.lower(); return _RM[k]

def owner_of(key, info):
    chain = list(info.get('chain') or [key])
    def has_resource(path):
        try: p, t = REPO.native_tail(path)
        except Exception: return False
        return bool(UE(p, t).lists()) if len(t) > 40 else False
    if os.environ.get('CHR_BRUTE'):                                 # EXPERIMENTAL: every named map under the same root master
        rm = root_master(key); cands = [x for n, xs in by_owner.items() if root_master(n) == rm for x in xs]
        if cands: return 'BRUTE:' + rm, cands
    own = next((p for p in chain if has_resource(p)), None) or info.get('master')
    if own and own.lower() in by_owner: return own, by_owner[own.lower()]
    sm = CHRSHARE.get(key)                                          # EXPERIMENTAL: shared static-permutation map
    if sm and sm.lower() in by_owner: return sm, by_owner[sm.lower()]
    # a MIC with its own static permutation: try the master's map (UE3 shares maps by root master + switch set; a
    # residual near 0 for one variant means this MIC's switch set equals the cooked one)
    ms = (info.get('master') or '').lower()
    if own and ms in by_owner and REPO.obj(key) is not None: return info.get('master'), by_owner[ms]
    return own, []

PROBE = {}
CHRSHARE = json.load(open(os.path.join(H, 'chrshare_%s.json' % MAP))) if os.path.exists(os.path.join(H, 'chrshare_%s.json' % MAP)) else {}
KEYS = set(open(os.environ['CHR_KEYS']).read().split()) if os.environ.get('CHR_KEYS') else None
results = {}
for key, v in sorted(mats.items()):
    if FILT and FILT.lower() not in key.lower(): continue
    info = v.get('info') or {}
    if not v.get('glsl') or (KEYS and key.split('.')[-1] not in KEYS): continue
    owner, xs = owner_of(key, info)
    if not xs: results[key] = {'status': 'no-original-shader', 'owner': owner}; continue
    info['_owner'] = owner
    rr = []
    for x in xs:
        try: rr.append(verify(key, v, x))
        except Exception as ex: rr.append({'status': 'error %s %s' % (type(ex).__name__, str(ex)[:100])})
    results[key] = {'shaders': rr}
    print(key, json.dumps([{k: (round(q[k], 4) if isinstance(q.get(k), float) else q.get(k)) for k in ('status', 'type', 'resid', 'fog', 'D_resid', 'E_resid', 'D_scale', 'E_scale', 'D_rms', 'E_rms')} for q in rr]), flush=True)
json.dump(results, open(os.path.join(H, 'chr_d_%s.json' % MAP), 'w'), indent=1)
