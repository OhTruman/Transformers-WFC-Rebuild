# Prints the weapon + melee batch's value table (markdown) with the authored source of every value, for the RE lane's check.
# Usage: python -I value_table.py <weapon_presentation_audit.json> <authored.db> <melee_presentation_audit.json>
import json, sqlite3, sys

audit, db, melee = sys.argv[1], sys.argv[2], sys.argv[3]
c = sqlite3.connect('file:%s?mode=ro' % db, uri=True)
arch = json.loads(c.execute("select props from objects where opath = 'TransGame.Default__TnWeaponMesh'").fetchone()[0])

def vec(v):
    return '(%g, %g, %g)' % (v.get('X', 0), v.get('Y', 0), v.get('Z', 0)) if isinstance(v, dict) else '-'

def member(a, o, k, kind):
    if o and k in o:
        return (vec(o[k]) if kind == 'v' else ('%g' % o[k] if kind == 'f' else '/'.join(o[k].get(x, a[k].get(x, 'ERS_Zero')).replace('ERS_', '') for x in 'XYZ'))), 'mesh'
    v = a[k]
    return (vec(v) if kind == 'v' else ('%g' % v if kind == 'f' else '/'.join(v.get(x, 'ERS_Zero').replace('ERS_', '') for x in 'XYZ'))), 'arch'

fields = [('TimeDuration', 'f'), ('RotAmplitude', 'v'), ('RotFrequency', 'v'), ('RotParams', 'p'), ('LocAmplitude', 'v'), ('LocFrequency', 'v'), ('LocParams', 'p')]
print('## Per-weapon recoil (HmSkelControlRecoil): resolved value [source]')
print()
print('Source: arch = TransGame.Default__TnWeaponMesh (authored.db); mesh = the weapon mesh\'s authored member (RE weapon_presentation_audit.json, <WEP>_WEPMESH).')
print('Overlay rule: an authored vector member replaces the archetype member whole (absent axes 0); Params override per axis.')
print()
print('| Weapon | Control | ' + ' | '.join(f for f, _ in fields) + ' |')
print('|' + '---|' * (len(fields) + 2))
for e in json.load(open(audit)):
    s, h = e.get('spine_recoil'), e.get('hand_recoil')
    if not s and not h: continue
    for name, a, o in (('Spine', arch['SpineRecoil'], s), ('RightHand', arch['RightHandRecoil'], h)):
        cells = []
        for f, kind in fields:
            v, src = member(a, o, f, kind)
            cells.append('%s [%s]' % (v, src))
        print('| %s (%s) | %s | %s |' % (e['id'], e['mesh'], name, ' | '.join(cells)))
print()
print('Weapons without authored recoil (vehicle weapons, grenades, melee) use the archetype row.')
print()
print('## Melee sets (player TnMeleeSet): values used [source: RE melee_presentation_audit.json, PRESENTATION_AUDIT_2_RE.md part 2]')
print()
print('| Set | Chassis | Attack | Weapons | Damage | Damage type | Impulse | Dash | GroundSpeedMult | Used in ours |')
print('|---|---|---|---|---|---|---|---|---|---|')
reach = {('WeaponAttack', 'Any'), ('WeaponAttack', 'Poke'), ('WeaponAttack', 'Flag'), ('WeaponAttack', 'Bomb'), ('Whirlwind', 'Any')}
for a in json.load(open(melee))['attacks']:
    if a['set'].startswith('Ai'): continue
    used = any((a['type'], w) in reach for w in a['weapons'])
    print('| %s | %s | %s | %s | %g | %s | %g | %s | %g | %s |' % (a['set'], ', '.join(a['chassis']), a['type'], ', '.join(a['weapons']), a['damage'], a['dtype'],
          a['impulse'], a['dash'], a['ground_speed_mult'], 'yes' if used else 'no (not reachable in Versus)'))
