"""RT-era light hook proposals (Integration 2026-10-16; NOT original lights - the original authors none for these effects): per
effect template the game spawns (weapon impact squibs and projectile explosions from the character-audio weapon table,
vehicle BoostFx from character.json, pickup-factory FX from the maps), the authored effect colour, size and duration from
Rendering's decoded templates (Rebuild/work/render/MP_*/map_fx_runtime.json, read only), and a proposed light.
  bright emitters: lod 0 emitters whose material is not smoke / cloud / debris / dust / decal / distortion
  colour: StartColor (PMI_Color, t=0; default 1) x the colour-over-life (PMI_ColorScaleOverLife / PMI_ColorOverLife) at its
          peak; per emitter peak magnitude = max component; light colour = the magnitude-weighted mean, normalised to max 1
  size:   max StartSize (UU) x the peak size-over-life multiplier (PMI_SizeMultiplyLife), over the bright emitters
  time:   emitter duration (+ max lifetime) of the bright emitters; looping (EmitterLoops 0) = continuous
Proposal: radius = clamp(2.5 x size) per hook (explosion 3-12 m, impact 1-4 m, boost 1-6 m, pickup 1-4 m); brightness = clamp(2 x log2(1 + peak), 1, 16); duration = the bright time
(capped at 1.5 s for one-shots; continuous for loops), fading linearly over the last half. argv: out json, out md."""
import json, glob, math, re, sys, collections

TPL = json.load(open('F:/Transformers Rebuild/Rebuild-Systems/work/lights/fx_templates.json'))
systems = {}
for p in sorted(glob.glob('F:/Transformers Rebuild/Rebuild/work/render/MP_*/map_fx_runtime.json')):
    d = json.load(open(p, encoding='utf-8'))
    for k, v in d['systems'].items():
        systems.setdefault(k, v)

DIM = re.compile(r'smoke|cloud|debris|dust|decal|distort|refract|shadow|dark|black', re.I)
def comps(kind):
    return 3 if kind.startswith('vector') else 1
def peak(dist):
    """max over the distribution's samples, per component (constant / uniform / curves: [min, max, t0, scale] + samples)"""
    k, v = dist['kind'], dist['values']; c = comps(k)
    if not v: return [1.0] * c
    if 'curve' not in k:
        n = c * (2 if 'uniform' in k else 1)
        vals = v[:n]
        return [max(vals[i::c]) for i in range(c)] if len(vals) >= c else [max(vals)] * c
    stride = c * (2 if 'uniform' in k else 1)
    s = v[4:]
    if len(s) < c: return [max(v[:2])] * c
    return [max(s[i::stride][:len(s) // stride] or [0]) for i in range(c)]

rows = []
# per hook kind: radius min / max (m), one-shot duration cap (s)
LIM = {'explosion (radiusDamage / projectile impact)': (3.0, 12.0, 1.0), 'impact (spawnImpact)': (1.0, 4.0, 0.25),
       'vehicle boost (BoostFx socket)': (1.0, 6.0, 1.5), 'pickup factory': (1.0, 4.0, 1.5)}
def analyse(tpl, kind, users):
    s = systems.get(tpl)
    if not s:
        rows.append({'kind': kind, 'template': tpl, 'note': 'not decoded'}); return
    em, tot = [], 0.0
    for e in s['emitters']:
        l = (e.get('lods') or [{}])[0]
        mat = l.get('material') or ''
        if DIM.search(mat) or DIM.search(e.get('name') or ''):
            continue
        mods = {m['module']: m for m in l.get('modules', [])}
        start = [1.0, 1.0, 1.0]
        if 'PMI_Color' in mods and 'StartColor' in mods['PMI_Color']['dists']:
            start = (peak(mods['PMI_Color']['dists']['StartColor']) + [1, 1, 1])[:3]
        over = [1.0, 1.0, 1.0]
        for mn, dn in (('PMI_ColorOverLife', 'ColorOverLife'), ('PMI_ColorScaleOverLife', 'ColorScaleOverLife')):
            if mn in mods and dn in mods[mn]['dists']:
                pv = peak(mods[mn]['dists'][dn]); pv = (pv * 3)[:3]
                over = pv if mn == 'PMI_ColorOverLife' else [a * b for a, b in zip(over, pv)]
        col = [a * b for a, b in zip(start, over)]
        mag = max(col)
        if mag < 0.3:
            continue
        size = 0.0
        if 'PMI_Size' in mods and 'StartSize' in mods['PMI_Size']['dists']:
            size = peak(mods['PMI_Size']['dists']['StartSize'])[0]   # sprites size from X
        if 'PMI_SizeMultiplyLife' in mods and 'LifeMultiplier' in mods['PMI_SizeMultiplyLife']['dists']:
            size *= max(1.0, peak(mods['PMI_SizeMultiplyLife']['dists']['LifeMultiplier'])[0])
        life = 0.0
        if 'PMI_Lifetime' in mods and 'Lifetime' in mods['PMI_Lifetime']['dists']:
            life = max(peak(mods['PMI_Lifetime']['dists']['Lifetime']))
        req = l.get('required') or {}
        loops = req.get('emitter_loops', 1)
        dur = (req.get('emitter_duration') or 0.0) + life
        em.append({'emitter': e.get('name'), 'material': mat, 'colour': [round(x, 3) for x in col], 'peak': round(mag, 2),
                   'size_uu': round(size, 1), 'time_s': round(dur, 2), 'loops': loops})
    if not em:
        rows.append({'kind': kind, 'template': tpl, 'users': users, 'note': 'no bright emitter (smoke / debris only)'}); return
    w = sum(e['peak'] for e in em)
    col = [sum(e['colour'][i] for e in em) / w for i in range(3)]
    mx = max(col) or 1.0
    col = [round(c / mx, 3) for c in col]
    pk = max(e['peak'] for e in em)
    size_m = max(e['size_uu'] for e in em) / 100.0
    looping = any(e['loops'] == 0 for e in em)
    t = max(e['time_s'] for e in em)
    rows.append({'kind': kind, 'template': tpl, 'users': users, 'emitters': em, 'colour': col, 'peak': pk, 'size_m': round(size_m, 2),
                 'looping': looping, 'time_s': round(t, 2),
                 'proposal': {'radius_m': round(min(max(2.5 * size_m, LIM[kind][0]), LIM[kind][1]), 1),
                              'brightness': round(min(max(2.0 * math.log2(1.0 + pk), 1.0), 16.0), 1),
                              'duration_s': 'continuous' if looping else round(min(t, LIM[kind][2]), 2), 'falloff': 2.0}})

for t, n in sorted(TPL['explosions'].items(), key=lambda kv: -kv[1]): analyse(t, 'explosion (radiusDamage / projectile impact)', n)
for t, n in sorted(TPL['squibs'].items(), key=lambda kv: -kv[1]): analyse(t, 'impact (spawnImpact)', n)
for t, n in sorted(TPL['boost'].items(), key=lambda kv: -kv[1]): analyse(t, 'vehicle boost (BoostFx socket)', n)
for t, n in (('FX_Pickups_p.FX.Pickup_FX', 'ammo / weapon / objective factories'), ('FX_Pickups_p.FX.HealthPickup_FX', 'health factories'),
             ('FX_Pickups_p.FX.OvershieldPickup_FX', 'overshield factories')):
    analyse(t, 'pickup factory', n)
json.dump({'note': 'RT-era additions (no original light): proposals for Rendering', 'rows': rows}, open(sys.argv[1], 'w', encoding='utf-8'), indent=1)

L = ['# RT-era light hooks: proposals (Realistic only, off by default; NOT original lights)', '',
     'The original authors no lights for these effects (SYSTEMS_MATC58_AUDIT.md: no particle light module in this UE3 build). '
     'Each row is an effect the game already spawns, with its authored colour / size / time read from Rendering\'s decoded '
     'templates (map_fx_runtime.json) by tools/systems/rt_light_hooks.py, and a proposed light for Rendering to pick and calibrate. '
     'Hook = where World spawns the effect (the light would spawn there, same position). No code until Rendering picks rows; '
     'the patch would gate on Realistic + a WFC_ key (default off until adopted).', '',
     'Method: bright emitters = not smoke / cloud / debris / dust / dark; colour = StartColor x colour-over-life at its peak, '
     'magnitude-weighted, normalised; size = StartSize x size-over-life peak; time = emitter duration + lifetime. Proposal: '
     'radius = clamp(2.5 x size) per hook (explosion 3-12 m, impact 1-4 m, boost 1-6 m, pickup 1-4 m), brightness = clamp(2 log2(1 + peak), 1, 16), falloff 2, one-shots fade over '
     'their time (explosion max 1 s, impact flash max 0.25 s), loops continuous.', '',
     '| hook | template | users | colour (norm.) | peak | size m | time s | radius m | brightness | duration |', '|---|---|---|---|---|---|---|---|---|---|']
for r in rows:
    if 'proposal' not in r:
        L.append('| %s | %s | %s | - | - | - | - | - | - | %s |' % (r['kind'], r['template'].split('.')[-1], r.get('users', ''), r.get('note', '')))
        continue
    p = r['proposal']
    L.append('| %s | %s | %s | %s | %.1f | %.2f | %s | %.1f | %.1f | %s |' % (r['kind'], r['template'].split('.')[-1], r['users'], r['colour'],
             r['peak'], r['size_m'], 'loop' if r['looping'] else r['time_s'], p['radius_m'], p['brightness'], p['duration_s']))
L += ['', 'Users: weapon classes using the template (explosions / impacts), vehicle forms (boost), factory kinds (pickups). '
      'Per-emitter detail (material, colour, size, time): SYSTEMS_RT_LIGHT_HOOKS.json.']
open(sys.argv[2], 'w', encoding='utf-8').write('\n'.join(L) + '\n')
print('\n'.join(L[8:]))
