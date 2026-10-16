"""The original's transient (gameplay) lights for Rendering's plan v2 item 4, from AssetTools' authored.db (read only):
- weapon muzzle lights: HmWeaponMesh.MuzzleFlashLight (a PointLightComponent archetyped on
  TransGame.Default__TnWeaponMesh.MuzzleFlashLightA), EnableMuzzleLight, MuzzleFlashDuration, MuzzleFlashes[].DisableLight
  / WeaponEventType, values resolved through the archetype chain;
- pawn cloak / death lights: TnPawn.CloakLight / DeathLight + TnRampLightManager (RampLightProperties: RampInTime,
  LightTime, RampOutTime, Radius, Brightness, LightColor, FalloffExponent, Translation), and the
  TnDeathModifierTriggerLight / TnAnimNotify_TriggerDeathLight instances that carry RampLightProperties;
- any other light component owned by a non-map object (projectiles, pickups, abilities).
argv[1] = output json. UE3 units: cm; LightColor is sRGB bytes x Brightness."""
import sqlite3, json, sys, collections

db = sqlite3.connect('file:F:/Transformers Rebuild/AssetTools/manifests/authored.db?mode=ro', uri=True)
cache = {}
def obj(path):
    if path not in cache:
        cache[path] = db.execute("select opath, class, archetype, package, props from objects where opath=? or key=? limit 1", (path, path)).fetchone()
    return cache[path]
def props(path, depth=0):
    """props merged through the archetype chain (the archetype's values under the object's own)"""
    o = obj(path)
    if not o or depth > 8:
        return {}
    own = json.loads(o[4]) if o[4] else {}
    base = props(o[2], depth + 1) if o[2] and o[2] != path else {}
    return {**base, **own}
def clean(p):
    return {k: v for k, v in p.items() if k not in ('LightGuid', 'LightmapGuid')}

out = {}
# class defaults
out['defaults'] = {k: clean(props(k)) for k in ('TransGame.Default__TnWeaponMesh', 'HM_Engine.Default__HmWeaponMesh',
                                               'TransGame.Default__TnWeaponMesh.MuzzleFlashLightA', 'TransGame.Default__TnPawn')}
for k, v in list(out['defaults']['TransGame.Default__TnPawn'].items()):
    if 'Light' not in k:
        del out['defaults']['TransGame.Default__TnPawn'][k]
for k in ('TransGame.Default__TnWeaponMesh', 'HM_Engine.Default__HmWeaponMesh'):
    out['defaults'][k] = {a: b for a, b in out['defaults'][k].items() if 'Muzzle' in a or 'Light' in a}
for pr in ('CloakLight', 'DeathLight'):
    for src, dst in db.execute("select src, dst from refs where prop=? limit 1", (pr,)):
        out['defaults'][f'TnPawn.{pr} ({dst})'] = clean(props(dst))

# weapon muzzle lights (one row per weapon mesh object path; packages repeat the same object)
weap = {}
for src, dst in db.execute("select src, dst from refs where prop='MuzzleFlashLight'"):
    if src in weap:
        continue
    sp, lp = props(src), clean(props(dst))
    weap[src] = {'EnableMuzzleLight': sp.get('EnableMuzzleLight'), 'MuzzleFlashDuration': sp.get('MuzzleFlashDuration'),
                 'flashes': [{'event': m.get('WeaponEventType'), 'ps': m.get('PSTemplate'), 'loops': m.get('PSLoops'),
                              'DisableLight': m.get('DisableLight')} for m in sp.get('MuzzleFlashes') or []],
                 'light': lp}
out['muzzle_lights'] = weap

# ramp lights carried by death modifiers / anim notifies
ramps = []
for cls in ('TnDeathModifierTriggerLight', 'TnAnimNotify_TriggerDeathLight'):
    for (op,) in db.execute("select opath from objects where class=?", (cls,)):
        p = props(op)
        ramps.append({'object': op, 'class': cls, 'props': p.get('LightProperties') or p.get('DLPs') or p})
out['ramp_lights'] = {r['object']: r for r in ramps}

# every other light component whose outer is not a map actor
other = collections.defaultdict(list)
q = """select o.opath, o.class, o.outer_path, o.package, p.class from objects o left join objects p on p.opath = o.outer_path
       where o.class in ('PointLightComponent', 'SpotLightComponent') and (p.class is null or p.class not in
       ('PointLight', 'SpotLight', 'PointLightToggleable', 'PointLightMovable', 'SpotLightMovable', 'SpotLightToggleable',
        'StaticLightCollectionActor', 'BeastAreaLight', 'PointLightSpawnable', 'SpotLightSpawnable'))"""
seen = set()
for op, cls, outer, pkg, ocls in db.execute(q):
    if op in seen:
        continue
    seen.add(op)
    other[ocls or '?'].append({'object': op, 'class': cls, 'package': pkg})
out['other_light_components'] = {k: {'n': len(v), 'examples': v[:6]} for k, v in sorted(other.items(), key=lambda kv: -len(kv[1]))}
json.dump(out, open(sys.argv[1], 'w', encoding='utf-8'), indent=1)
print(len(weap), 'weapon meshes,', len(ramps), 'ramp lights,', {k: v['n'] for k, v in out['other_light_components'].items()})
