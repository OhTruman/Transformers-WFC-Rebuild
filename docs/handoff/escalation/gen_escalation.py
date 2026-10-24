# Escalation (SV) data for the rebuild: data/gameplay/escalation_<map>.json from RE's export (spawn list, waves, zones:
# RE-Workspace/notes/data/escalation/<map>.json, RE MP_ESCALATION_RE.md) and AssetTools' zone geometry (manifests/maps/<map>/
# escalation_geometry.json: polygons in glTF metres). Per zone the axis-aligned box of its polygons (the zones are boxes).
# Run with AssetTools' python: bin/py/python.exe -I tools/gameplay/gen_escalation.py
import json, math, os

ROOT = 'F:/Transformers Rebuild'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'data', 'gameplay')
# the players' team (TnCoopInfo.Faction; RE 2a5d0ea for the DLC maps: Tranquillity / Pulse unset = Autobots, District / Static Decepticons)
COOP = {'MP_ESC_BrokenHope': 0, 'MP_ESC_Remnant': 1, 'MP_ESC_Tranquillity': 0, 'MP_ESC_District': 1, 'MP_ESC_Pulse': 0, 'MP_ESC_Static': 1}
TYPES = ['EST_Standard', 'EST_ShortRange', 'EST_LongRange', 'EST_Air', 'EST_Boss']

def short(z):
    return z.replace('TnSurvivalZoneVolume_', '')

incs = []
# RE (escalation_zone_order.json, a5cc309): the PersistentLevel actor order of the zones (Initialize walks AllActors, adding the active
# ones in that order) and, as a test vector, every type's initial sorted order (the binary's MSVC qsort).
ORDER = json.load(open(ROOT + '/RE-Workspace/work/handoff/escalation_zone_order.json'))
for m in COOP:
    gpath = ROOT + '/AssetTools/manifests/maps/' + m + '/escalation_geometry.json'
    if not os.path.exists(gpath):   # a DLC map whose AssetTools geometry has not landed yet
        print('skip', m, '(no escalation_geometry.json)'); continue
    re_ = json.load(open(ROOT + '/RE-Workspace/notes/data/escalation/' + m + '.json'))
    geo = json.load(open(gpath))
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
    # Subsequence calls on New Power Level (District's Zone_Manager / Zone, RE notes/data/escalation): their linked variables are
    # the actions AssetTools' flattened chains do not follow - Open Doors -> transform, Enable Survival Zones -> zone on, Disable
    # Volumes -> TriggerVolume off (NavMeshCells: the original NavigationCell gating, the escNav path only; not carried).
    SUBVARS = {'Open Doors': ('transform', 'TnTransformingWorldActor'), 'Enable Survival Zones': ('on', 'TnSurvivalZoneVolume'),
               'Disable Volumes': ('off', 'TriggerVolume'), 'Disable Generator Volumes': ('off', 'TriggerVolume')}   # (Pulse's name)
    subacts = {}
    for e in re_.get('events', []):
        if e.get('class') != 'TnSeqEvent_SurvivalGenerator': continue
        for y in e.get('downstream', [])[1:]:
            if y.get('class') != 'Sequence' or not (str(y.get('via') or '').endswith('New Power Level') or str(y.get('input')) == 'Generator Used'): continue
            for k, (op, cls) in SUBVARS.items():
                for t in (y.get('vars') or {}).get(k, []):
                    subacts.setdefault(e['originator'], []).append({'op': op, 'class': cls, 'target': t})
    for g in geo['generators']:
        pts = [p for poly in g['activation_polygons_gltf'] for p in poly]
        acts = []
        for e in g['events']:
            for a in e['actions']:
                op = OPS.get(a['class'], {}).get(str(a.get('input')))
                if not op: continue
                for t in a.get('targets', []) or []:
                    acts.append({'op': op, 'class': t['class'], 'target': t['target']})
        for x in subacts.get(g['generator'], []):
            if x not in acts: acts.append(x)
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
    cpath = ROOT + '/ExtractedAssets/VerticalSlice/Maps/' + m + '/collision.json'
    vols = {v['actor']: v for v in json.load(open(cpath))['volumes']} if os.path.exists(cpath) else {}
    need = set([w[0] for w in k.get('crashWalls', [])] + [g[0] for g in k.get('roundGates', [])] +
               ([k['window']['enable'], k['window']['volume']] if 'window' in k else []))
    tvs = {}
    for name in sorted(need):
        polys = vols[name]['polygons_gltf']
        pts = [p for poly in polys for p in poly]
        tvs[name] = {'planes': planes(polys), 'min': [round(min(p[i] for p in pts), 3) for i in range(3)],
                     'max': [round(max(p[i] for p in pts), 3) for i in range(3)]}
    # The horde bodies (E3a): per enemy entry the chassis (AssetTools out/escalation_characters/<id> as "ESC_<id>"; the MG soldiers are
    # the roster's Car8 / Car9 bodies), the robot / vehicle weapons and the ROBODEF health [RE MP_ESCALATION_RE §7.1 / §9.2, C].
    # PROV: Spider (melee / explode from its action set) and the Tank's robot cannon have no weapon in our table yet; the Brute's
    # 100,000 health is killed through its backpack weak point (E3b) - until then its health is PROV 1500.
    HORDE = {
        'Spider': ([], [], 20), 'Soldier_MachineGun_Decepticon': (['AssaultRifle'], ['AssaultRifleVehicle'], 150),
        'Soldier_MachineGun_Autobot': (['AssaultRifle'], ['AssaultRifleVehicle'], 150),
        'Soldier_Rocket_Decepticon': (['RocketLauncher'], ['RocketVehicle'], 150), 'Soldier_Rocket_Autobot': (['RocketLauncher'], ['RocketVehicle'], 150),
        'Soldier_Shotgun_Decepticon': (['EmpShotgun'], ['AssaultRifleVehicle'], 150), 'Soldier_Shotgun_Autobot': (['EmpShotgun'], ['AssaultRifleVehicle'], 150),
        'CQCSpecialist_Decepticon': (['PlasmaCannon'], [], 90), 'CQCSpecialist_Autobot': (['PlasmaCannon'], [], 90),
        'Soldier_Jet_Decepticon': (['HeavyMG'], ['PlaneMachineGun'], 250),
        'AiEscalation_Soldier_Jet_Autobot': (['HeavyMG'], ['PlaneMachineGun'], 250),   # District (DLC): the Dec jet soldier's numbers [RE 2a5d0ea]
        'Brute_Decepticon': (['HalberdAxe'], [], 1500), 'Brute_Autobot': (['WarHammer'], [], 1500),
        'Titan_Decepticon': (['GatlingGun'], [], 1250), 'Titan_Autobot': (['GatlingGun'], [], 1250),
        'Soldier_Tank_Decepticon': ([], ['TankCannon'], 6000), 'HoloBrute_Autobot': (['CeremonialStaff'], [], 1800),
    }
    index = json.load(open(ROOT + '/AssetTools/manifests/escalation/escalation_characters_index.json'))['characters']
    for e in out['enemies']:
        t = e['type'].replace('_AICD', '')
        robot, vehicle, health = HORDE.get(t, ([], [], 150))
        if t.startswith('Soldier_MachineGun_'): chassis = 'Car8' if t.endswith('Decepticon') else 'Car9'
        elif t in index: chassis = 'ESC_' + t
        else: chassis = ''
        e.update({'chassis': chassis, 'robotWeapons': robot, 'vehicleWeapons': vehicle, 'health': health})
    out['kismet'] = dict(k, volumes=tvs)
    # DLC map mechanisms the director runs as data (RE work/handoff/DLC_ESC_MECHANISMS_RE.md, 1104a86; C unless marked): positions from
    # AssetTools' DLC authored.db (UE units -> glTF metres: x = X / 100, y = Z / 100, z = Y / 100), sentry spots from RE's kismet_extra.
    MECH_RE = {
        'MP_ESC_District': {
            'lasers': [('7381', '11476', '10120'), ('11492', '15142', '2841'), ('1022', '944', '3986'), ('15098', '9997', '11907'), ('7702', '4078', '4440')],
            'sentries': {'Sentry_0': ('4356', 'light'), 'Sentry_1': ('9559', 'light'), 'Sentry_2': ('2342', 'light'), 'Sentry_3': ('9740', 'light'),
                         'Sentry_5': ('7105', 'light'), 'Sentry_6': ('7283', 'light'), 'Sentry_4': ('2249', 'repair3'), 'Sentry_7': ('13380', 'repair3'),
                         'Repair_Sentry#0': ('1070', 'repair'), 'Repair_Sentry#1': ('8605', 'repair')},
            'detpacks': [('TnInteractDetpack_12501', [('TnStaticDestructibleActor_3947', 0.0)]),
                         ('TnInteractDetpack_14838', [('TnStaticDestructibleActor_171', 0.0), ('TnStaticDestructibleActor_11555', 1.0)])],
            'outage': True,
        },
        'MP_ESC_Pulse': {'managers': True},   # Power_Generator_Action: built below from RE's chains
        'MP_ESC_Tranquillity': {
            # FloodgateGenerators in RE's order with their TriggerVolume / DynamicPhysicsVolume pair (both off on purchase)
            'floodgates': [('3589', '10055', '14764'), ('15956', '6407', '2157'), ('14278', '9646', '576')],
        },
    }
    mech = {}
    mre = MECH_RE.get(m)
    if mre:
        import sqlite3
        db = sqlite3.connect('file:' + ROOT + '/AssetTools/manifests/dlc/authored.db?mode=ro', uri=True)
        def actor(name):   # (glTF position, our yaw) of an authored actor of this map
            row = db.execute('select props from objects where opath like ? limit 1', ('%' + m + '%.' + name,)).fetchone()
            p = json.loads(row[0]) if row and row[0] else {}
            L = p.get('Location') or {}
            yaw_ue = (p.get('Rotation') or {}).get('Yaw', 0) * 360.0 / 65536.0
            pos = [round(L.get('X', 0.0) / 100.0, 3), round(L.get('Z', 0.0) / 100.0, 3), round(L.get('Y', 0.0) / 100.0, 3)]
            if not row: print('  no authored actor', name)
            return pos, round(math.atan2(-math.cos(math.radians(yaw_ue)), -math.sin(math.radians(yaw_ue))), 5)
        if 'lasers' in mre:   # all five damage volumes start off (root Toggle_14580); a purchase: on 45 s, the button usable after 3 s
            mech['hazardsOff'] = ['DynamicPhysicsVolume_' + h for (_, _, h) in mre['lasers']]
            mech['lasers'] = [{'gen': g, 'button': actor('TnInteractButtonReusable_' + b)[0], 'hazard': 'DynamicPhysicsVolume_' + h,
                               'time': 45.0, 'armDelay': 3.0, 'buttonRange': 3.0} for (g, b, h) in mre['lasers']]   # buttonRange PROV
        if 'sentries' in mre:
            xe = json.load(open(ROOT + '/RE-Workspace/notes/data/escalation/' + m + '_kismet_extra.json'))
            spots = {}
            for x in xe['mechanisms']:   # the TnPawnFactory's Spawn Point at PreGameplaySetup, per sequence (Repair_Sentry: two, in order)
                for y in x['downstream'][1:]:
                    if y['class'] == 'SeqAct_TnPawnFactory' and (y.get('vars') or {}).get('Spawn Point'):
                        seq = x['sequence']
                        n = sum(1 for k in spots if k.split('#')[0] == seq)
                        spots[seq + ('#%d' % n if seq == 'Repair_Sentry' else '')] = y['vars']['Spawn Point'][0]
            mech['sentries'] = []
            for seq, (g, kind) in mre['sentries'].items():
                if seq not in spots:
                    print('  no spawn point for', seq)
                    continue
                pos, yaw = actor(spots[seq])
                mech['sentries'].append({'seq': seq, 'gen': g, 'pos': pos, 'yaw': yaw, 'repair': kind != 'light',
                                         'time': 45.0 if kind == 'light' else 90.0, 'uses': 3 if kind == 'repair3' else 0})
        if 'detpacks' in mre:   # a player interact (TouchDistance 500 UU), no cost, single use; BaBoom: 1000 on each destructible
            mech['detpacks'] = [{'pos': actor(a)[0], 'range': 5.0, 'countdown': 3.0,   # countdown: the plant / countdown anim (H, PROV 3 s)
                                 'hits': [[w, d, 1000.0] for (w, d) in hits]} for (a, hits) in mre['detpacks']]
        if mre.get('outage'):   # Power_Outage: 25 % per Round Begin from the 4th (50 % from the 10th), 60 s, max 3, lockout 180 / 300 s
            mech['outage'] = {'fromRound': 4, 'chance': 0.25, 'lateRound': 10, 'lateChance': 0.5, 'delay': 5.0, 'duration': 60.0,
                              'lockout': 180.0, 'lateLockout': 300.0, 'max': 3}
        if 'floodgates' in mre:   # three purchases resume the 10.03 s drain (pauses 1.76 / 3.49 s); Drain Completed after the third
            mech['floodgates'] = {'gens': [{'gen': g, 'trigger': 'TriggerVolume_' + t, 'hazard': 'DynamicPhysicsVolume_' + h} for (g, t, h) in mre['floodgates']],
                                  'drainTimes': [1.76, 3.49, 10.03], 'zonesOn': ['64', '7748'], 'energon': 'DynamicPhysicsVolume_672',
                                  'blockers': ['DynamicBlockingVolume_8499', 'DynamicBlockingVolume_9335'], 'gateOpenDelay': 3.1}   # 4.35 - 1.25 s
        # Presentation actors for Rendering (WorldSurvivalMechPresent.cpp); the sim never reads them. Names from RE's scoped exports
        # (1104a86: notes/data/escalation, seq per op) and AssetTools' sequence-scoped generator rows (escalation_geometry.json); the
        # matinee tracks are gameplay.json "matinee" by action (prefab copies share one action name: bind the actors given here).
        gens_geo = {x['generator'].replace('TnSurvivalGenerator_', ''): x for x in geo['generators']}
        def gen_targets(gid, cls, op=None):
            found = []
            for e in gens_geo.get(gid, {}).get('events', []):
                for a in e['actions']:
                    if op and OPS.get(a['class'], {}).get(str(a.get('input'))) != op: continue
                    for t in a.get('targets', []) or []:
                        if t['class'] == cls and t['target'] not in found: found.append(t['target'])
            return found
        if 'lasers' in mech:
            xe = json.load(open(ROOT + '/RE-Workspace/notes/data/escalation/' + m + '_kismet_extra.json'))
            ems = {}
            for x in xe['mechanisms']:
                for y in x['downstream'][1:]:
                    if y['class'] == 'Sequence' and str(y['input']) == 'lasers on':
                        lst = ems.setdefault(y.get('seq') or x['sequence'], [])
                        for e_ in (y.get('vars') or {}).get('laser emitters', []):
                            if e_ not in lst: lst.append(e_)
            for L, sq in zip(mech['lasers'], ['Door_Lasers', 'Door_Lasers_0', 'Door_Lasers_1', 'Door_Lasers_2', 'Door_Lasers_3']):   # RE table order
                sw = gen_targets(L['gen'], 'SkeletalMeshActor', 'unhide')
                bt = gen_targets(L['gen'], 'TnInteractButtonReusable', 'unhide')
                L.update({'seq': sq, 'emitters': sorted(ems.get(sq, [])), 'buttonActor': bt[0] if bt else '', 'switchActor': sw[0] if sw else '',
                          'interps': {'button': 'SeqAct_Interp_13587', 'buttonLength': 0.06, 'switch': 'SeqAct_Interp_13276', 'switchLength': 1.84}})
        for S in mech.get('sentries', []):
            S.update({'emitters': gen_targets(S['gen'], 'Emitter'), 'body': spots.get(S['seq'], ''),
                      'template': 'WEP_SentryBlaster01_p.' + ('RepairSentry_TURRETDEF' if S['repair'] else 'LightSentry_TURRETDEF')})
        if 'outage' in mech:   # the dark grade (PPV swap) and Interp_6320 (screens / switch materials, 1.51 s forward, reversed at the end)
            mech['outage'].update({'ppvOn': 'PostProcessVolume_4388', 'ppvOff': 'PostProcessVolume_3052', 'interp': 'SeqAct_Interp_6320', 'interpLength': 1.51})
        if 'floodgates' in mech:
            unhide = []
            for e in re_.get('events', []):
                for y in e.get('downstream', [])[1:]:
                    gid = (e.get('originator') or '').replace('TnSurvivalGenerator_', '')
                    for F in mech['floodgates']['gens']:
                        if F['gen'] != gid or y['class'] != 'SeqAct_Interp' or y['op'] == 'SeqAct_Interp_5361': continue
                        v = y.get('vars') or {}
                        if 'Fall_1' in v: F['waterfallInterp'] = y['op']
                        if 'MatInstance' in v: F['materialInterp'] = y['op']
                    if y['class'] == 'SeqAct_ToggleHidden' and str(y['input']) == 'UnHide':
                        for t in (y.get('vars') or {}).get('Target', []):
                            if t.startswith('TnStaticDestructibleActor') and t not in unhide: unhide.append(t)
            mech['floodgates'].update({
                'drainInterp': 'SeqAct_Interp_5361', 'drainLength': 10.03, 'unhideAtDrain': unhide, 'fogOnAtDrain': True,
                # BaseDoors (Flood_Gate.BaseDoors): both 6.67 s matinees held at 1.25 s; OpenBase resumes; at "Opened" 4.35 s the pistons hide
                'baseDoors': [{'interp': 'SeqAct_Interp_6026', 'pistons': ['HmSkeletalMeshActor_7715', 'HmSkeletalMeshActor_13308'], 'blocker': 'DynamicBlockingVolume_9335'},
                              {'interp': 'SeqAct_Interp_10535', 'pistons': ['HmSkeletalMeshActor_16151', 'HmSkeletalMeshActor_817'], 'blocker': 'DynamicBlockingVolume_8499'}],
                'doorHold': 1.25, 'doorOpened': 4.35, 'doorLength': 6.67})
        if m == 'MP_ESC_Pulse':   # Power_Generator_Action (RE section 8): the 10 Zone_Manager instances' door matinees + destroyed actors
            # Which manager each generator fires: RE's resolved chains (the remote events / first-of-two gates already followed).
            MGR_INTERPS = {'Zone_Manager': ['SeqAct_Interp_54', 'SeqAct_Interp_4544'], 'Zone_Manager_1': ['SeqAct_Interp_5622', 'SeqAct_Interp_8917'],
                           'Zone_Manager_2': ['SeqAct_Interp_7549'], 'Zone_Manager_3': ['SeqAct_Interp_12776'], 'Zone_Manager_4': ['SeqAct_Interp_11165'],
                           'Zone_Manager_5': ['SeqAct_Interp_11287'], 'Zone_Manager_6': ['SeqAct_Interp_14950'], 'Zone_Manager_8': ['SeqAct_Interp_12613'],
                           'Zone_Manager_9': ['SeqAct_Interp_7793'], 'Zone_Manager_10': ['SeqAct_Interp_542']}
            fired = {}
            for e in re_.get('events', []):
                if e.get('class') != 'TnSeqEvent_SurvivalGenerator': continue
                for y in e.get('downstream', [])[1:]:
                    if y.get('class') == 'Sequence' and y['op'].startswith('Zone_Manager'):
                        fired.setdefault(y['op'], set()).add(e['originator'].replace('TnSurvivalGenerator_', ''))
            xe = json.load(open(ROOT + '/RE-Workspace/notes/data/escalation/' + m + '_kismet_extra.json'))
            iactors, destroyed = {}, set()
            for x in xe['mechanisms']:
                for y in x['downstream'][1:]:
                    v = y.get('vars') or {}
                    if y['class'] == 'SeqAct_Interp':
                        for k, lst in v.items():
                            if k != 'Data':
                                for a in lst:
                                    if 'InterpActor' in a or 'SkeletalMeshActor' in a: iactors.setdefault(y['op'], set()).add(a)
                    if y['class'] == 'SeqAct_Destroy':
                        destroyed.update(a for a in v.get('Target', []) if 'InterpActor' in a)
            gp = json.load(open(ROOT + '/AssetTools/out/dlc_content/VerticalSlice/Maps/' + m + '/gameplay.json'))
            ilen = {e['action']: e['length_s'] for e in gp.get('matinee', [])}
            pawn_glb = open(ROOT + '/AssetTools/out/dlc_content/VerticalSlice/Maps/' + m + '/collision_pawn.glb', 'rb').read()
            def colliding(a): return a.encode() in pawn_glb   # a node of the pawn collision (the door / window / force field blocks)
            mech['managers'] = []
            for mgr, interps in MGR_INTERPS.items():
                acts = sorted({a for i in interps for a in iactors.get(i, set())})
                mech['managers'].append({'seq': mgr, 'gens': sorted(fired.get(mgr, set())), 'interps': interps,
                                         'delay': round(max([ilen.get(i, 0.0) for i in interps] + [0.0]), 3),   # collision off when the door is open
                                         'blockers': [a for a in acts if colliding(a)], 'actors': acts})
            mech['blockersAll'] = sorted({a for mm in mech['managers'] for a in mm['blockers']})
        own = {x['gen'] for x in mech.get('lasers', [])} | {x['gen'] for x in mech.get('sentries', [])}
        for g in out['generators']:   # their flattened chains cross the prefab copies (RE 1104a86 section 0): the mechanism runs them
            if g['id'] in own: g['actions'] = []
    if mech: out['mechanisms'] = mech   # (base maps: no key, their data unchanged)
    # TnWeaponPickupFactory (AssetTools weapon_pickups): our weapon id = the inventory class without TransContent.TnWeapon.
    out['weaponPickups'] = [{'actor': w['actor'], 'weapon': w['inventory_type'].split('.')[-1].replace('TnWeapon', ''),
                             'respawn': w.get('respawn_time') or 2.0, 'pos': [round(v, 3) for v in w['location_gltf']],
                             'yaw': round(math.atan2(-math.cos(math.radians(w.get('yaw_deg', 0.0))), -math.sin(math.radians(w.get('yaw_deg', 0.0)))), 5)}
                            for w in geo.get('weapon_pickups', [])]
    # The dropped-weapon mesh (TnDroppedPickupWeapon draws the inventory's PickupMesh, as the factory's): weapon id -> mesh object.
    out['dropMeshes'] = {w['inventory_type'].split('.')[-1].replace('TnWeapon', ''): w['pickup_mesh']['mesh']
                         for w in geo.get('weapon_pickups', []) if (w.get('pickup_mesh') or {}).get('mesh')}
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

