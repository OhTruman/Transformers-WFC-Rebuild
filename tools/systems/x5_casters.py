"""Rendering package-6 row X5 (more shadow casters): the dynamic things with meshes worth an RT shadow, from the game's own
data (read only): mesh asset, bind-pose bounds (glTF POSITION accessor min / max), whether the rebuild draws it today, and
typical / maximum counts on screen.
  projectiles   VerticalSlice/Weapons/<w>/weapon.json projectiles[k].body_mesh.gltf (World::loadProjectileVisuals)
  deployables   AssetTools manifests/mp_content/ability_assets.json SkeletalMesh / StaticMesh per ability (barrier, sentry, ammo
                crate, roller, guided missile); World draws the barrier and the sentry (WEP_SentryDeploy_SKEL)
  pickups       VerticalSlice/Maps/<map>/render_index.json pickup_factory_visuals (the renderer's map FX pickup meshes), per map
  shell / mag   WeaponFx mesh particles (GrenadeAmmo_STAT shells, IonBlaster_Mag_STAT magazines)
argv[1] = out json, argv[2] = out md."""
import json, os, glob, collections, struct

EA = 'F:/Transformers Rebuild/ExtractedAssets'
def bounds(path):
    """(min, max) of every POSITION accessor in a .gltf / .glb; None when unreadable"""
    try:
        if path.endswith('.glb'):
            b = open(path, 'rb').read(); n = struct.unpack_from('<I', b, 12)[0]; g = json.loads(b[20:20 + n])
        else:
            g = json.load(open(path, encoding='utf-8'))
    except Exception:
        return None
    lo, hi = [1e30] * 3, [-1e30] * 3
    for m in g.get('meshes', []):
        for p in m.get('primitives', []):
            a = g['accessors'][p['attributes']['POSITION']]
            if 'min' in a and 'max' in a:
                lo = [min(x, y) for x, y in zip(lo, a['min'])]; hi = [max(x, y) for x, y in zip(hi, a['max'])]
    return (lo, hi) if lo[0] < 1e29 else None
def size(path):
    b = bounds(path)
    return [round(h - l, 2) for l, h in zip(*b)] if b else None
def content(obj_path, kind):
    """content/<pkg>/<group...>/<name>.gltf for an object path pkg.group.name (umodel layout)"""
    parts = obj_path.split('.')
    cands = [os.path.join(EA, 'content', parts[0], *parts[1:]) + '.gltf', os.path.join(EA, 'content', parts[0], parts[-1]) + '.gltf']
    return next((c for c in cands if os.path.exists(c)), None)

out = {'projectiles': [], 'deployables': [], 'pickups': {}, 'effect_meshes': []}
for wj in sorted(glob.glob(EA + '/VerticalSlice/Weapons/*/weapon.json')):
    w = json.load(open(wj, encoding='utf-8'))
    for k, pj in enumerate(w.get('projectiles') or []):
        g = (pj.get('body_mesh') or {}).get('gltf')
        if not g:
            continue
        out['projectiles'].append({'weapon': os.path.basename(os.path.dirname(wj)) + ('' if k == 0 else '#%d' % k), 'mesh': g,
                                   'size_m': size(os.path.join(EA, g)), 'drawn': True})
ab = json.load(open('F:/Transformers Rebuild/AssetTools/manifests/mp_content/ability_assets.json', encoding='utf-8'))['abilities']
DRAWN = {'WEP_Shield_p.Barrier.WEP_Barrier_SKEL', 'WEP_SentryAbility_p.WEP_SentryDeploy_SKEL'}
for name, e in sorted(ab.items()):
    for t in ('SkeletalMesh', 'StaticMesh'):
        for x in (e.get('assets') or {}).get(t, []):
            o = x['object']
            if o.startswith('FX_'):
                continue   # effect cards (planes, bells, cross planes): particles, not shadow casters
            c = content(o, t)
            out['deployables'].append({'ability': name, 'class': t, 'mesh': o, 'gltf': os.path.relpath(c, EA).replace('\\', '/') if c else None,
                                       'size_m': size(c) if c else None, 'drawn': o in DRAWN})
out['deployables'].append({'ability': 'SpawnSentry (World::loadSentry)', 'class': 'SkeletalMesh', 'mesh': 'WEP_SentryAbility_p.WEP_SentryDeploy_SKEL',
                           'gltf': 'content/WEP_SentryAbility_p/WEP_SentryDeploy_SKEL.gltf',
                           'size_m': size(EA + '/content/WEP_SentryAbility_p/WEP_SentryDeploy_SKEL.gltf'), 'drawn': True})
out['deployables'].append({'ability': 'KamikazeMines (minepooper / Leader grenade)', 'class': 'StaticMesh', 'mesh': 'WEP_KamikazeMine_p (weapon mesh)',
                           'gltf': 'VerticalSlice/Weapons/KamikazeMines/weapon.glb', 'size_m': size(EA + '/VerticalSlice/Weapons/KamikazeMines/weapon.glb'),
                           'drawn': False, 'note': 'World never draws mines_ (Gameplay item)'})
for ri in sorted(glob.glob(EA + '/VerticalSlice/Maps/MP_*/render_index.json')):
    m = os.path.basename(os.path.dirname(ri))
    d = json.load(open(ri, encoding='utf-8'))
    cnt = collections.Counter()
    meshes = {}
    for e in d.get('pickup_factory_visuals') or []:
        if 'mesh particle' in (e.get('kind') or '') or not str(e.get('gltf', '')).startswith('content/'):
            continue
        cnt[e.get('mesh')] += 1
        meshes[e.get('mesh')] = e.get('gltf')
    out['pickups'][m] = {k: {'count': n, 'gltf': meshes[k], 'size_m': size(os.path.join(EA, meshes[k]))} for k, n in cnt.most_common()}
for g, what in (('content/FX_GrenadeLauncher_p/GrenadeAmmo_STAT.gltf', 'shell casings (Shell_AssaultRifle_FX: 1 per shot, 1 s)'),
                ('content/FX_IonBlaster_p/IonBlaster_Mag_STAT.gltf', 'dropped magazines (Magazine_IonBlaster_FX: 1 per reload, 3 s)')):
    out['effect_meshes'].append({'mesh': g, 'what': what, 'size_m': size(os.path.join(EA, g))})
json.dump(out, open(__import__('sys').argv[1], 'w', encoding='utf-8'), indent=1)

L = ['# X5: dynamic shadow casters (Rendering package-6 row; Systems data, read only)', '',
     'Characters (robots / vehicles) already cast. These are the remaining dynamic meshes: what the original had, whether the '
     'rebuild draws them today, their bind-pose size (glTF bounds, m) and how many are on screen. tools/systems/x5_casters.py; '
     'full data SYSTEMS_X5_CASTERS.json.', '',
     '## Projectile bodies (World::loadProjectileVisuals; drawn while in flight)', '', '| weapon | mesh | size m (x y z) |', '|---|---|---|']
for p in out['projectiles']:
    L.append('| %s | %s | %s |' % (p['weapon'], p['mesh'].split('/')[-1], p['size_m']))
L += ['', 'Only the thrown grenades have a mesh body. Rockets, the grenade launcher, the plasma cannon and the other launchers are '
      'particle-only in the original (weapon.json projectile_visual: no mesh on the projectile class default mesh component; the '
      'FlightEffect is the visible body, CONFIRMED), so they have nothing to cast. Grenades (0.4 x 0.7 m) are in flight ~1-3 s; '
      '0-2 on screen in 4v5, a handful in 31v32. Low value: a small moving shadow.', '',
      '## Deployables (abilities)', '', '| ability | mesh | size m | drawn today |', '|---|---|---|---|']
for d in out['deployables']:
    L.append('| %s | %s | %s | %s%s |' % (d['ability'], d['mesh'], d['size_m'], 'yes' if d['drawn'] else 'no', (' - ' + d['note']) if d.get('note') else ''))
L += ['', 'Caps: one barrier per Leader, one sentry per Scientist (TnAbilitySpawnSentry kills the previous), ammo crate / roller / '
      'guided missile one per use; mines up to ~7 per minepooper buff (one per 2 s for 15 s). Typical on screen: 0-2 sentries / '
      'barriers in 4v5, up to ~6-10 in 31v32. The sentry (bind-pose bounds 5.2 x 2.3 x 2.75 m incl. the folded deploy parts) '
      'is the obvious first caster. The barrier is a translucent additive force field in the original (BLEND_Additive): it '
      'should NOT cast. The ammo crate / roller / guided missile meshes are not drawn by World today (their abilities spawn '
      'effects only), so they wait for presentation first.', '',
      '## Pickups (map factories; the renderer\'s pickup meshes, rotating while available)', '', '| map | mesh | count | size m |', '|---|---|---|---|']
for m, d in out['pickups'].items():
    for k, v in d.items():
        L.append('| %s | %s | %d | %s |' % (m, k, v['count'], v['size_m']))
L += ['', 'All visible at once where the camera sees them (static placement, 10-30 per map); small (0.5-1 m), so shadows are '
      'cheap and read on the floor under each pickup.', '', '## Effect meshes', '']
for e in out['effect_meshes']:
    L.append('- %s: %s, size %s m. Too small and short-lived to matter; skip.' % (e['mesh'], e['what'], e['size_m']))
L += ['', '## Not drawn by the rebuild (no caster until drawn)', '',
      '- Dropped weapons (HmDroppedPickup: a dead pawn\'s weapon on the floor): not presented; Gameplay item.',
      '- Kamikaze mines (minepooper): simulated, never drawn; reported to Gameplay.', '',
      'Suggested order for X5: sentries > pickups > projectile bodies (rockets, grenades) > ammo crate / roller; never the barrier.']
open(__import__('sys').argv[2], 'w', encoding='utf-8').write('\n'.join(L) + '\n')
print('\n'.join(L[:40]))
