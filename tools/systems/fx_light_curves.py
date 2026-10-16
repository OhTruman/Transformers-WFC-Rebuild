"""Row 7 / effect-light calibration: per effect template in SYSTEMS_RT_LIGHT_HOOKS.json, the effect's own brightness over time,
so a light's fade matches it. From Rendering's decoded templates (Rebuild/work/render/MP_*/map_fx_runtime.json, read only):
for every bright emitter (the hooks table's rule: not smoke / cloud / debris / dust / dark), particles spawn by the burst list
and the spawn rate over the emitter duration (looping emitters: one loop); each lives the mean of its Lifetime distribution
and contributes peak colour x ColorScaleOverLife / ColorOverLife x AlphaScaleOverLife / AlphaOverLife at its normalised age
(the runtime's curve layout: [min, max, t0, scale] + samples). The sum over time, normalised to its maximum, is the curve
(24 samples over the effect time). Fit: rampIn = time to 90 % of the peak, hold until it falls below 50 %, rampOut to 5 %.
Adds 'intensity_curve' and 'fade_fit' to each row of argv[1] (in place)."""
import json, glob, re, sys

hooks_path = sys.argv[1]
H = json.load(open(hooks_path, encoding='utf-8'))
systems = {}
for p in sorted(glob.glob('F:/Transformers Rebuild/Rebuild/work/render/MP_*/map_fx_runtime.json')):
    for k, v in json.load(open(p, encoding='utf-8'))['systems'].items():
        systems.setdefault(k, v)
DIM = re.compile(r'smoke|cloud|debris|dust|decal|distort|refract|shadow|dark|black', re.I)

def ev(dist, t, comp=0):
    """FxDist::eval at time t, component comp; uniform kinds take the mean"""
    if not dist: return 1.0
    k, v = dist['kind'], dist['values']
    c = 3 if k.startswith('vector') else 1
    if not v: return 0.0
    if 'curve' not in k:
        if 'uniform' in k:
            a = v[min(comp, len(v) - 1)]; b = v[min(c + comp, len(v) - 1)]
            return 0.5 * (a + b)
        return v[min(comp, len(v) - 1)]
    stride = c * (2 if 'uniform' in k else 1)
    n = (len(v) - 4) // stride
    if n <= 0: return 0.0
    f = min(max((t - v[2]) * v[3], 0.0), n - 1.0)
    i0 = int(f); i1 = min(i0 + 1, n - 1); a = f - i0
    s0, s1 = v[4 + i0 * stride + comp], v[4 + i1 * stride + comp]
    val = s0 + (s1 - s0) * a
    if 'uniform' in k:
        e0, e1 = v[4 + i0 * stride + c + comp], v[4 + i1 * stride + c + comp]
        val = 0.5 * (val + e0 + (e1 - e0) * a)
    return val

def emitter_terms(tpl):
    s = systems.get(tpl)
    if not s: return None
    terms = []
    for e in s['emitters']:
        l = (e.get('lods') or [{}])[0]
        if DIM.search(l.get('material') or '') or DIM.search(e.get('name') or ''):
            continue
        mods = {m['module']: m['dists'] for m in l.get('modules', [])}
        req = l.get('required') or {}
        dur = float(req.get('emitter_duration') or 0.0)
        life = 0.5 * (ev(mods.get('PMI_Lifetime', {}).get('Lifetime'), 0, 0) + ev(mods.get('PMI_Lifetime', {}).get('Lifetime'), 0, 0)) or 1.0
        lt = mods.get('PMI_Lifetime', {}).get('Lifetime')
        if lt and 'uniform' in lt['kind'] and len(lt['values']) >= 2: life = 0.5 * (lt['values'][0] + lt['values'][1])
        elif lt and lt['values']: life = lt['values'][0]
        rate = mods and ev((req.get('spawn_rate') or {}) if (req.get('spawn_rate') or {}).get('values') else None, 0, 0) if req.get('spawn_rate') else 0.0
        bursts = [(float(b.get('Time', 0.0)) * max(dur, 1e-3), int(b.get('Count', 0))) for b in (req.get('burst_list') or [])]
        start = [1.0, 1.0, 1.0]
        if 'StartColor' in mods.get('PMI_Color', {}):
            start = [ev(mods['PMI_Color']['StartColor'], 0, i) for i in range(3)]
        def bright(age):   # age normalised 0..1
            col = list(start)
            if 'ColorOverLife' in mods.get('PMI_ColorOverLife', {}):
                col = [ev(mods['PMI_ColorOverLife']['ColorOverLife'], age, i) for i in range(3)]
            if 'ColorScaleOverLife' in mods.get('PMI_ColorScaleOverLife', {}):
                col = [c * ev(mods['PMI_ColorScaleOverLife']['ColorScaleOverLife'], age, i) for i, c in enumerate(col)]
            al = 1.0
            for mn, dn in (('PMI_ColorOverLife', 'AlphaOverLife'), ('PMI_ColorScaleOverLife', 'AlphaScaleOverLife')):
                if dn in mods.get(mn, {}): al *= ev(mods[mn][dn], age, 0)
            return max(0.0, max(col)) * max(0.0, al)
        terms.append({'dur': dur, 'life': max(life, 1e-3), 'rate': rate if isinstance(rate, (int, float)) else 0.0, 'bursts': bursts,
                      'bright': bright, 'loops': req.get('emitter_loops', 1)})
    return terms

for r in H['rows']:
    terms = emitter_terms(r['template'])
    if not terms:
        continue
    T = max(t['dur'] + t['life'] for t in terms)
    looping = any(t['loops'] == 0 for t in terms)
    N = 24
    ts = [T * i / (N - 1) for i in range(N)]
    curve = []
    for t in ts:
        s = 0.0
        for e in terms:
            for bt, cnt in e['bursts']:                          # bursts
                if cnt > 0 and bt <= t <= bt + e['life']:
                    s += cnt * e['bright']((t - bt) / e['life'])
            if e['rate'] > 0 and e['dur'] > 0:                   # continuous spawn over the emitter duration (sampled)
                steps = 16
                for j in range(steps):
                    t0 = e['dur'] * (j + 0.5) / steps
                    if t0 <= t <= t0 + e['life']:
                        s += e['rate'] * e['dur'] / steps * e['bright']((t - t0) / e['life'])
        curve.append(s)
    mx = max(curve) or 1.0
    k = [round(c / mx, 3) for c in curve]
    pk = k.index(1.0)
    t90 = next((ts[i] for i, v in enumerate(k) if v >= 0.9), ts[pk])
    after = [i for i in range(pk, N) if k[i] < 0.5]
    hold_end = ts[after[0]] if after else ts[-1]
    end5 = next((ts[i] for i in range(pk, N) if k[i] < 0.05), ts[-1])
    r['intensity_curve'] = {'t_s': [round(t, 3) for t in ts], 'k': k, 'looping': looping}
    r['fade_fit'] = {'rampIn_s': round(t90, 3), 'lightTime_s': round(hold_end, 3), 'rampOut_s': round(max(end5 - hold_end, 0.0), 3),
                     'note': 'looping: continuous while the effect runs' if looping else 'one-shot: ramp in to the peak, hold above 50 %, fade to 5 %'}
    print('%-50s T %.2f s  rampIn %.3f  hold to %.3f  out %.3f  %s' % (r['template'].split('.')[-1], T, r['fade_fit']['rampIn_s'],
          r['fade_fit']['lightTime_s'], r['fade_fit']['rampOut_s'], 'loop' if looping else ''))
json.dump(H, open(hooks_path, 'w', encoding='utf-8'), indent=1)
