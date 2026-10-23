"""The MP smoke templates and their authored parameters, for Rendering's Smoke Original / Enhanced row (user 2026-10-17 / playtest
2026-10-23). Reads every MP map's Rebuild/work/render/<map>/map_fx_runtime.json (the runtime FX data: systems -> emitters -> LOD 0
modules) and AssetTools authored.db (read only) for each material's BlendMode (through MaterialInstance parents).
  python -I smoke_inventory.py <out.json>
A smoke emitter = its material or emitter name says smoke / steam / dust / fog / haze / exhaust / fume / vapor / cloud. Family by
the template path. Per emitter: material + blend, sprite / beam / trail, SubUV (method, sub-images), max peak count, spawn rate /
burst, lifetime, start size (UU), size-over-life peak, start velocity, colour / alpha over life (first / last), local space.
Per template: the maps and how it spawns there (placed level emitters by count; else 'runtime' = weapons / deaths / abilities)."""
import collections, glob, json, os, re, sqlite3, sys

RENDER = 'F:/Transformers Rebuild/Rebuild/work/render'
DB = 'F:/Transformers Rebuild/AssetTools/manifests/authored.db'
SMOKE = re.compile(r'smoke|steam|dust|fog|haze|exhaust|fume|vapou?r|cloud|puff', re.I)
FAMILIES = [('death', re.compile(r'death|corpse|gib', re.I)),
            ('mine / grenade', re.compile(r'mine|grenade|sticky|flak', re.I)),
            ('explosion', re.compile(r'expl|blast|detonat|impact_lrg|rocket_impact|boom', re.I)),
            ('vehicle exhaust / hover dust', re.compile(r'exhaust|booster|boost|hover|thrust|jet|nitro|trail_jet|tire|wheel|dust', re.I)),
            ('ambient map smoke', re.compile(r'fx_level|steam|chimney|vent|stack|ambient|fire_|burn|smolder|smoulder', re.I)),
            ('weapon impact / muzzle / shell', re.compile(r'impact|muzzle|shell|reload|magazine|tracer|projectile|ammo|fx_[a-z]+(rifle|pistol|launcher|cannon|blaster|shotgun|mg)', re.I))]
NOT_SMOKE = re.compile(r'distort|spark|glow|flare|flash|lens|shimmer|refract', re.I)

db = sqlite3.connect('file:%s?mode=ro' % DB, uri=True)
blend_cache = {}


def blend_of(mat, depth=0):
    if mat in blend_cache: return blend_cache[mat]
    r = db.execute('select class, props from objects where opath=?', (mat,)).fetchone()
    out = None
    if r:
        p = json.loads(r[1] or '{}')
        out = p.get('BlendMode')
        if not out and p.get('Parent') and depth < 6: out = blend_of(p['Parent'], depth + 1)
        if not out and r[0] == 'Material': out = 'BLEND_Opaque (class default)'
    blend_cache[mat] = out
    return out


def dist(mods, module, key):
    for m in mods:
        if m.get('module') == module and key in (m.get('dists') or {}):
            return m['dists'][key]
    return None


def summary(d, comps=1):
    """a distribution -> {kind, min, max} (first component(s) only for vectors)"""
    if not d: return None
    v = d.get('values') or []
    k = d.get('kind', '')
    if 'uniform' in k and 'curve' not in k:
        if k.startswith('vector') and len(v) >= 6: return {'kind': k, 'min': v[:3][:comps], 'max': v[3:6][:comps]}
        if len(v) >= 2: return {'kind': k, 'min': v[0], 'max': v[1]}
    if 'constant' in k and 'curve' not in k:
        return {'kind': k, 'value': v[:3] if k.startswith('vector') else (v[0] if v else None)}
    if 'curve' in k:   # a curve: report the key count and the value range (vector: per first component)
        return {'kind': k, 'n_values': len(v), 'note': 'curve (see map_fx_runtime.json for the keys)'}
    return {'kind': k, 'values': v[:6]}


def curve_peak(d):
    """SizeMultiplyLife-style constant curves stored as [in, out...] keys: the peak multiplier (approx.)"""
    if not d or 'curve' not in d.get('kind', ''): return None
    v = [x for x in (d.get('values') or []) if isinstance(x, (int, float))]
    return max(v) if v else None


systems, maps_of, placed = {}, collections.defaultdict(set), collections.Counter()
for f in sorted(glob.glob(RENDER + '/MP_*/map_fx_runtime.json')):
    m = os.path.basename(os.path.dirname(f))
    d = json.load(open(f, encoding='utf-8'))
    for name, s in (d.get('systems') or {}).items():
        systems.setdefault(name, s)
        maps_of[name].add(m)
    for inst in d.get('instances') or []:
        if inst.get('role') == 'level': placed[(inst.get('template'), m)] += 1

out = []
for name in sorted(systems):
    s = systems[name]
    ems = []
    for e in s.get('emitters') or []:
        lods = e.get('lods') or []
        if not lods: continue
        l0 = lods[0]
        mat = l0.get('material') or ''
        if not (SMOKE.search(mat) or SMOKE.search(e.get('name') or '')): continue
        req = l0.get('required') or {}
        mods = l0.get('modules') or []
        ems.append({
            'emitter': e.get('name'), 'material': mat, 'blend': blend_of(mat) if mat else None, 'type': l0.get('type_data'),
            'render_mode': e.get('render_mode'), 'subuv': {'method': l0.get('subuv_method'), 'subimages': req.get('subimages')},
            'max_peak_count': e.get('max_peak_count'), 'spawn_rate': summary(req.get('spawn_rate')),
            'bursts': [b for b in (req.get('burst_list') or []) if b.get('Count')], 'emitter_duration': req.get('emitter_duration'),
            'emitter_loops': req.get('emitter_loops'), 'local_space': req.get('use_local_space'),
            'lifetime_s': summary(dist(mods, 'PMI_Lifetime', 'Lifetime')),
            'start_size_uu': summary(dist(mods, 'PMI_Size', 'StartSize'), 1),
            'size_over_life_peak': curve_peak(dist(mods, 'PMI_SizeMultiplyLife', 'LifeMultiplier')),
            'start_velocity': summary(dist(mods, 'PMI_Velocity', 'StartVelocity'), 3),
            'color_over_life': summary(dist(mods, 'PMI_ColorOverLife', 'ColorOverLife')),
            'alpha_over_life': summary(dist(mods, 'PMI_ColorOverLife', 'AlphaOverLife')),
            'modules': [m.get('module') for m in mods],
            # an additive distortion / spark / glow sprite caught by the name rule: probably not smoke (Rendering decides)
            'not_smoke_guess': bool(NOT_SMOKE.search(mat)) or (blend_of(mat) or '').endswith('Additive') and not SMOKE.search(mat),
        })
    if not ems: continue
    fam = next((f for f, rx in FAMILIES if rx.search(name)), 'other')
    sources = {m: ('placed x%d' % placed[(name, m)]) if placed[(name, m)] else 'runtime (weapons / deaths / abilities / pickups)' for m in sorted(maps_of[name])}
    out.append({'template': name, 'family': fam, 'maps': sources, 'smoke_emitters': ems})

fam_n = collections.Counter(t['family'] for t in out)
doc = {'generated_by': 'Rebuild-Systems tools/systems/smoke_inventory.py', 'source': 'Rebuild/work/render/MP_*/map_fx_runtime.json + AssetTools authored.db (blend)',
       'smoke_rule': SMOKE.pattern, 'families': dict(fam_n), 'templates': out}
open(sys.argv[1], 'w', encoding='utf-8', newline='\n').write(json.dumps(doc, indent=1))
print('templates with smoke emitters:', len(out), dict(fam_n), '| smoke emitters:', sum(len(t['smoke_emitters']) for t in out))
