"""Rendering plan v3 data reads (Integration 2026-10-16), from AssetTools' authored.db (read only):
(3) vehicle headlights: every light component (Point / Spot / Directional) owned - directly or through the archetype chain - by a
    vehicle form (TnVehicleForm subclasses, VEHDEF packages, HmVehicle) or a pawn class, plus every light-typed property on those
    classes' types;
(7) dynamic emissive objects: light components owned by pickups / pickup factories / dropped pickups / interact objects /
    destructibles / movers / skeletal or static mesh actors outside the map lighting (non-Light actors), with values.
Writes a json; prints the summary. argv[1] = out json."""
import sqlite3, json, sys, collections

db = sqlite3.connect('file:F:/Transformers Rebuild/AssetTools/manifests/authored.db?mode=ro', uri=True)
LIGHT_ACTORS = {'PointLight', 'SpotLight', 'PointLightToggleable', 'PointLightMovable', 'SpotLightMovable', 'SpotLightToggleable',
                'StaticLightCollectionActor', 'BeastAreaLight', 'PointLightSpawnable', 'SpotLightSpawnable', 'DirectionalLight',
                'DirectionalLightToggleable', 'SkyLight', 'SkyLightToggleable'}
def props(path, depth=0):
    o = db.execute("select archetype, props from objects where opath=? limit 1", (path,)).fetchone()
    if not o or depth > 8:
        return {}
    own = json.loads(o[1]) if o[1] else {}
    base = props(o[0], depth + 1) if o[0] and o[0] != path else {}
    return {**base, **own}
def clean(p):
    keep = ('Radius', 'Brightness', 'LightColor', 'FalloffExponent', 'InnerConeAngle', 'OuterConeAngle', 'bEnabled', 'CastShadows',
            'CastDynamicShadows', 'bForceDynamicLight', 'LightingChannels', 'Translation', 'Rotation', 'Function')
    return {k: v for k, v in p.items() if k in keep}

# every light component and its outer's class
rows = db.execute("""select o.opath, o.class, o.outer_path, o.package, p.class from objects o left join objects p on p.opath = o.outer_path
                     where o.class in ('PointLightComponent', 'SpotLightComponent', 'DirectionalLightComponent', 'SkyLightComponent')""").fetchall()
by_owner = collections.defaultdict(list)
seen = set()
for op, cls, outer, pkg, ocls in rows:
    if op in seen or (ocls in LIGHT_ACTORS):
        continue
    seen.add(op)
    by_owner[ocls or '?'].append({'object': op, 'class': cls, 'package': pkg, 'props': clean(props(op))})

vehicle_like = {k: v for k, v in by_owner.items() if any(t in (k or '') for t in ('Vehicle', 'VEH', 'Form', 'Tank', 'Jet', 'Car', 'Truck', 'Plane', 'Hover'))}
weapon_like = {k: v for k, v in by_owner.items() if 'WeaponMesh' in (k or '')}
pawn_like = {k: v for k, v in by_owner.items() if 'Pawn' in (k or '')}
other = {k: v for k, v in by_owner.items() if k not in vehicle_like and k not in weapon_like and k not in pawn_like}

# light-typed properties declared on vehicle / pickup / interact types
decl = []
for path, p in db.execute("select path, props from types where props like '%LightComponent%'"):
    try:
        pj = json.loads(p)
    except Exception:
        continue
    for name, t in pj.items():
        if isinstance(t, dict) and 'LightComponent' in str(t.get('class', '')):
            decl.append({'type': path, 'property': name, 'class': t.get('class')})

# vehicle form classes and their packages (to say "none" precisely)
vforms = db.execute("select count(*), count(distinct package) from objects where class like '%VehicleForm%' or opath like '%VehicleForm%'").fetchone()
pickups = db.execute("select class, count(*) from objects where class like '%Pickup%' or class like '%Energon%' or class like '%Interact%' group by class order by 2 desc limit 30").fetchall()

out = {'light_components_by_owner_class': {k: {'n': len(v), 'examples': v[:8]} for k, v in sorted(by_owner.items(), key=lambda kv: -len(kv[1]))},
       'vehicle_owned': {k: v for k, v in vehicle_like.items()}, 'pawn_owned': {k: v[:4] for k, v in pawn_like.items()},
       'other_owned': {k: v for k, v in other.items()},
       'declared_light_properties': decl, 'vehicle_form_objects': {'n': vforms[0], 'packages': vforms[1]},
       'pickup_like_classes': [{'class': c, 'n': n} for c, n in pickups]}
json.dump(out, open(sys.argv[1], 'w', encoding='utf-8'), indent=1)
print('owners of non-map light components:', {k: len(v) for k, v in sorted(by_owner.items(), key=lambda kv: -len(kv[1]))})
print('declared light-component properties:')
for d in decl:
    print('  ', d['type'], d['property'], d['class'])
print('vehicle form objects', vforms)
print('pickup / interact classes', pickups[:15])
