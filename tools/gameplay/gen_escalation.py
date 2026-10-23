# Escalation (SV) data for the rebuild: data/gameplay/escalation_<map>.json from RE's export (spawn list, waves, zones:
# RE-Workspace/notes/data/escalation/<map>.json, RE MP_ESCALATION_RE.md) and AssetTools' zone geometry (manifests/maps/<map>/
# escalation_geometry.json: polygons in glTF metres). Per zone the axis-aligned box of its polygons (the zones are boxes).
# Run with AssetTools' python: bin/py/python.exe -I tools/gameplay/gen_escalation.py
import json, math, os

ROOT = 'F:/Transformers Rebuild'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'data', 'gameplay')
COOP = {'MP_ESC_BrokenHope': 0, 'MP_ESC_Remnant': 1}
TYPES = ['EST_Standard', 'EST_ShortRange', 'EST_LongRange', 'EST_Air', 'EST_Boss']

def short(z):
    return z.replace('TnSurvivalZoneVolume_', '')

incs = []
# RE (escalation_zone_order.json, a5cc309): the PersistentLevel actor order of the zones (Initialize walks AllActors, adding the active
# ones in that order) and, as a test vector, every type's initial sorted order (the binary's MSVC qsort).
ORDER = json.load(open(ROOT + '/RE-Workspace/work/handoff/escalation_zone_order.json'))
for m in ['MP_ESC_BrokenHope', 'MP_ESC_Remnant']:
    re_ = json.load(open(ROOT + '/RE-Workspace/notes/data/escalation/' + m + '.json'))
    geo = json.load(open(ROOT + '/AssetTools/manifests/maps/' + m + '/escalation_geometry.json'))
    sl = re_['spawn_list'][0]
    boxes = {}; spawns = {}
    for z in geo['zones']:
        # yaw: our pawn yaw from UE Rotation.Yaw (as Match::loadSpawnData: atan2(-cos, -sin) of the UE yaw)
        spawns[z['zone']] = [[round(v, 3) for v in sp['location_gltf']] + [round(math.atan2(-math.cos(math.radians(sp['yaw_deg'])), -math.sin(math.radians(sp['yaw_deg']))), 5)]
                             for sp in z.get('spawn_points', [])]
        pts = [p for poly in z['polygons_gltf'] for p in poly]
        boxes[z['zone']] = [[min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)]]
    out = {
        '_source': 'tools/gameplay/gen_escalation.py: RE notes/data/escalation/' + m + '.json + AssetTools escalation_geometry.json',
        'map': m,
        'coopTeam': COOP[m],   # the players' team (CoopInfo faction): BrokenHope Autobots, Remnant Decepticons [RE 1]
        'preGameTime': sl.get('PreGameTime', 6.0),
        'timeBetweenWaves': sl.get('TimeBetweenWaves', 5.0),
        'fillWaveReCheckTime': sl.get('FillWaveReCheckTime', sl.get('defaults_from_cdo', {}).get('FillWaveReCheckTime', 0.5)),
        'waves': [{'budget': w['Budget'], 'budgetAlive': w['BudgetAlive'], 'deficit': w['BudgetDeficitThreshold'],
                   'buffs': [[b['BuffClass'].split('.')[-1], b['BuffLevel']] for b in w.get('Buffs', [])]} for w in sl['Waves']],
        'enemies': [{'type': e['EnemyType'].split('.')[-1], 'minWave': e['MinWave'], 'maxWave': e['MaxWave'], 'minCount': e['MinCount'],
                     'maxCount': e['MaxCount'], 'cost': e['Cost'], 'spawnType': TYPES.index(e['SpawnType']), 'loot': e['LootClassification'],
                     'power': e['PowerReward']} for e in sl['Enemies']],
        'zones': [],
    }
    key = m.replace('MP_ESC_', '')
    rank = {zid: i for i, zid in enumerate(ORDER['zone_actor_order'][key])}
    out['initialSorted'] = [ORDER['initial_sorted'][key][t] for t in ['Standard', 'ShortRange', 'LongRange', 'Air', 'Boss']]
    for z in sorted(re_['zones'], key=lambda q: rank[short(q['object'])]):   # actor order
        name = z['object']
        b = boxes.get(name)
        if not b: raise SystemExit('no geometry for ' + name)
        out['zones'].append({
            'id': short(name), 'active': bool(z.get('Active', True)), 'points': z.get('SpawnPoints', 0),
            'self': [z['SelfModifier'].get(t, 0.0) for t in TYPES],
            # mod: the ScoreChange per type; named: the types the entry lists (TnSurvivalSpawnFinder re-sorts only those, RE)
            'changes': [{'zone': short(c['zone']), 'mod': [c.get(t, 0.0) for t in TYPES], 'named': [int(t in c) for t in TYPES]}
                        for c in z.get('ZoneChanges', [])],
            'min': [round(v, 3) for v in b[0]], 'max': [round(v, 3) for v in b[1]],
            'spawnPoints': spawns.get(name, []),   # AssetTools spawn_points (x, y, z glTF m, yaw); empty: a PROV point in the box
        })
    # Generators (TnSurvivalGenerator): cost (one power level in MP), DrainRate (None = the CDO's, resolved at runtime), the
    # ActivationVolume as outward planes (n, d: inside where n.p <= d) + its box, and AssetTools' resolved New Power Level actions
    # (remote-event chains and object variables followed) flattened to (op, class, target).
    def planes(polys):
        pts = [p for poly in polys for p in poly]
        c = [sum(p[i] for p in pts) / len(pts) for i in range(3)]
        out_ = []
        for poly in polys:
            a, b, q = poly[0], poly[1], poly[2]
            u = [b[i] - a[i] for i in range(3)]; v = [q[i] - a[i] for i in range(3)]
            n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
            ln = math.sqrt(sum(x * x for x in n))
            if ln < 1e-9: continue
            n = [x / ln for x in n]
            d = sum(n[i] * a[i] for i in range(3))
            if sum(n[i] * c[i] for i in range(3)) > d: n = [-x for x in n]; d = -d
            out_.append([round(x, 5) for x in n] + [round(d, 4)])
        return out_
    OPS = {'SeqAct_ToggleHidden': {'UnHide': 'unhide', 'Hide': 'hide'}, 'TnSeqAct_TransformWorldProp': {'Transform': 'transform', 'Untransform': 'untransform'},
           'SeqAct_ChangeCollision': {'In': 'collide'}, 'SeqAct_Toggle': {'Turn On': 'on', 'Turn Off': 'off', 'Toggle': 'toggle'}}
    out['generators'] = []
    for g in geo['generators']:
        pts = [p for poly in g['activation_polygons_gltf'] for p in poly]
        acts = []
        for e in g['events']:
            for a in e['actions']:
                op = OPS.get(a['class'], {}).get(str(a.get('input')))
                if not op: continue
                for t in a.get('targets', []) or []:
                    acts.append({'op': op, 'class': t['class'], 'target': t['target']})
        out['generators'].append({
            'id': g['generator'].replace('TnSurvivalGenerator_', ''), 'nameKey': g['name_key'], 'cost': g['power_levels'][0] if g['power_levels'] else 0,
            'drainRate': g['drain_rate'], 'location': g['location_gltf'], 'volume': g['activation_volume'],
            'planes': planes(g['activation_polygons_gltf']),
            'min': [round(min(p[i] for p in pts), 3) for i in range(3)], 'max': [round(max(p[i] for p in pts), 3) for i in range(3)],
            'actions': acts,
        })
    # Map Kismet the director runs as data (RE ESCALATION_E2B_ANSWERS_RE.md 45a2a08, BrokenHope "Wall_Crashers"):
    # crash walls - any pawn's first touch of the volume: CauseDamage 200 on the TnStaticDestructibleActor (InitialHealth 1);
    # round gates - at Round Begin count `round` a player-only touch turns on: Touched -> zone on, UnTouched -> zone off (per pawn event);
    # window - a player touching `enable` arms an AI-only touch of `volume`: a horde pawn there -> 200 on the wall (health 5).
    KISMET = {
        'MP_ESC_BrokenHope': {
            'crashWalls': [['TriggerVolume_9486', 'TnStaticDestructibleActor_10489'], ['TriggerVolume_11960', 'TnStaticDestructibleActor_6870'],
                           ['TriggerVolume_10811', 'TnStaticDestructibleActor_15525'], ['TriggerVolume_7836', 'TnStaticDestructibleActor_4868']],
            'roundGates': [['TriggerVolume_11067', '15078', 4], ['TriggerVolume_14603', '7763', 4], ['TriggerVolume_11856', '7739', 3],
                           ['TriggerVolume_8879', '6666', 6]],
            'window': {'enable': 'TriggerVolume_8879', 'volume': 'TriggerVolume_13849', 'wall': 'TnStaticDestructibleActor_5491', 'damage': 200},
        },
    }
    k = KISMET.get(m, {})
    col = json.load(open(ROOT + '/ExtractedAssets/VerticalSlice/Maps/' + m + '/collision.json'))
    vols = {v['actor']: v for v in col['volumes']}
    need = set([w[0] for w in k.get('crashWalls', [])] + [g[0] for g in k.get('roundGates', [])] +
               ([k['window']['enable'], k['window']['volume']] if 'window' in k else []))
    tvs = {}
    for name in sorted(need):
        polys = vols[name]['polygons_gltf']
        pts = [p for poly in polys for p in poly]
        tvs[name] = {'planes': planes(polys), 'min': [round(min(p[i] for p in pts), 3) for i in range(3)],
                     'max': [round(max(p[i] for p in pts), 3) for i in range(3)]}
    out['kismet'] = dict(k, volumes=tvs)
    path = os.path.join(OUT, 'escalation_' + m + '.json')
    with open(path, 'w') as f:
        json.dump(out, f, indent=1)
    txt = json.dumps(out, separators=(',', ':'))
    incs.append((m, txt))
    print(path, len(out['waves']), 'waves', len(out['enemies']), 'enemies', len(out['zones']), 'zones')

# The same data compiled into the exe (src/game/EscalationData.inc): no package path to resolve. MSVC caps one string literal
# piece at 16 KB, so each map's text goes in 6000-character pieces joined at load.
NL = chr(10)
SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'game', 'EscalationData.inc')
lines = ['// Generated by tools/gameplay/gen_escalation.py (do not edit): the data/gameplay/escalation_<map>.json contents.']
for i, (m, txt) in enumerate(incs):
    lines.append('static const char* const kEsc%d[] = {' % i)
    lines += ['    R"J(' + txt[k:k + 6000] + ')J",' for k in range(0, len(txt), 6000)]
    lines.append('    nullptr};')
lines.append('static const struct { const char* map; const char* const* parts; } kEscalationData[] = {')
lines += ['    {"%s", kEsc%d},' % (m, i) for i, (m, txt) in enumerate(incs)]
lines.append('};')
with open(SRC, 'w', newline=NL) as f:
    f.write(NL.join(lines) + NL)
print(SRC)

