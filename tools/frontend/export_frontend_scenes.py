"""Export the frontend levels' Kismet camera / matinee data -> data/frontend/scenes.json (Frontend lane).

The original menus are drawn over live 3D levels (RE OVERNIGHT_2026-10-04 section D): UI_FrontEnd_m (+ streamed
UI_FrontEnd_capture_VIG_m) under the title / main menu, UI_CharacterCustomization_m under the party and game lobbies,
UI_CampaignLobby_m under the campaign / escalation lobby. The geometry is AssetTools' export and Rendering's drawing;
this file carries what the Frontend lane drives: each level's SeqAct_Interp matinees (length, looping, groups bound to
actors, move tracks), the actors those groups move or attach to (initial transforms, Base / hard attach, camera FOV),
and the Kismet triggers that start each matinee (fscommands, remote events, level-loaded events).

Reads, never writes: AssetTools/manifests/authored.db (read-only URI) and ExtractedAssets/maps/<level>.json.
Usage: <AssetTools>/bin/py/python.exe tools/frontend/export_frontend_scenes.py
"""
import json, os, sqlite3, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
DB = os.path.join(ROOT, 'AssetTools', 'manifests', 'authored.db').replace('\\', '/')
OUT = os.path.join(os.path.dirname(__file__), '..', '..', 'data', 'frontend', 'scenes.json')

# UI level -> the levels its scene consists of (persistent + Kismet / WorldInfo streamed) [RE section D, CONFIRMED].
SCENES = {
    'UI_FrontEnd_m': ['UI_FrontEnd_m', 'UI_FrontEnd_capture_VIG_m'],
    'UI_PartyLobby_m': ['UI_PartyLobby_m', 'UI_CharacterCustomization_m'],
    'UI_Lobby_m': ['UI_Lobby_m', 'UI_CharacterCustomization_m'],
    'UI_CampaignLobby_m': ['UI_CampaignLobby_m'],
}

con = sqlite3.connect('file:' + DB + '?mode=ro', uri=True)
cur = con.cursor()


def props(op):
    r = cur.execute('SELECT props FROM objects WHERE opath = ?', (op,)).fetchone()
    return json.loads(r[0] or '{}') if r else None


def cls(op):
    r = cur.execute('SELECT class FROM objects WHERE opath = ?', (op,)).fetchone()
    return r[0] if r else None


def track(points):
    return [{'t': p.get('InVal', 0.0), 'v': [p['OutVal'].get('X', 0.0), p['OutVal'].get('Y', 0.0), p['OutVal'].get('Z', 0.0)],
             'ai': [p.get('ArriveTangent', {}).get(k, 0.0) for k in 'XYZ'],
             'lo': [p.get('LeaveTangent', {}).get(k, 0.0) for k in 'XYZ'],
             'mode': p.get('InterpMode', 'CIM_Linear')} for p in points or []]


def float_track(points):
    # FloatTrack points in the move-track key format (value in v[0]) so the runtime evaluates them with the same curves.
    return [{'t': p.get('InVal', 0.0), 'v': [p.get('OutVal', 0.0), 0.0, 0.0], 'ai': [p.get('ArriveTangent', 0.0), 0.0, 0.0],
             'lo': [p.get('LeaveTangent', 0.0), 0.0, 0.0], 'mode': p.get('InterpMode', 'CIM_Linear')} for p in points or []]


def actor(op):
    p = props(op) or {}
    loc, rot = p.get('Location') or {}, p.get('Rotation') or {}
    a = {'name': op.rsplit('.', 1)[-1], 'class': cls(op),
         'location': [loc.get('X', 0.0), loc.get('Y', 0.0), loc.get('Z', 0.0)],
         'rotation': [rot.get('Pitch', 0), rot.get('Yaw', 0), rot.get('Roll', 0)]}
    if p.get('Base'):
        a['base'] = p['Base'].rsplit('.', 1)[-1]
        a['hardAttach'] = bool(p.get('bHardAttach'))
        rl, rr = p.get('RelativeLocation') or {}, p.get('RelativeRotation') or {}
        a['relativeLocation'] = [rl.get('X', 0.0), rl.get('Y', 0.0), rl.get('Z', 0.0)]
        a['relativeRotation'] = [rr.get('Pitch', 0), rr.get('Yaw', 0), rr.get('Roll', 0)]
    if 'FOVAngle' in p or (a['class'] or '').endswith('CameraActor'):
        a['fov'] = p.get('FOVAngle', 90.0)
    return a


def seq_objects(level):
    return cur.execute("SELECT opath, class, props FROM objects WHERE package = ? AND opath LIKE ? ",
                       (level, level + '.TheWorld.PersistentLevel.Main_Sequence%')).fetchall()


def export_level(level):
    rows = seq_objects(level)
    by_path = {op: (c, json.loads(pr or '{}')) for op, c, pr in rows}
    # incoming links: op -> [(source op, output desc, input index)]
    incoming = {}
    for op, (c, p) in by_path.items():
        for out in p.get('OutputLinks') or []:
            for l in out.get('Links') or []:
                incoming.setdefault(l.get('LinkedOp'), []).append((op, out.get('LinkDesc'), l.get('InputLinkIdx', 0)))
    actors, matinees = {}, []

    def seq_input_fscommands(event_op, depth=0):
        # GFxEvent_FsCommand names reaching a SeqEvent_SequenceActivated through its parent Sequence's input (and, for
        # nested subsequences, through the grandparent's input feeding that parent).
        parent = event_op.rsplit('.', 1)[0]
        pc, pp = by_path.get(parent, (None, {}))
        if pc != 'Sequence' or depth > 4:
            return []
        ep = by_path.get(event_op, (None, {}))[1]
        name = ep.get('InputLabel') or ep.get('ObjComment')
        inputs = [l.get('LinkDesc') for l in pp.get('InputLinks') or []]
        if name not in inputs:
            return []
        want = inputs.index(name)
        out = []
        for src, desc, idx in incoming.get(parent, []):
            if idx != want:
                continue
            sc, sp = by_path.get(src, (None, {}))
            if sc == 'GFxEvent_FsCommand' and sp.get('FsCommand'):
                out.append(sp['FsCommand'])
            elif sc == 'SeqEvent_SequenceActivated':
                out += seq_input_fscommands(src, depth + 1)
        return out
    for op, (c, p) in by_path.items():
        if c != 'SeqAct_Interp':
            continue
        data, bind = None, {}
        for vl in p.get('VariableLinks') or []:
            lv = vl.get('LinkedVariables') or []
            if vl.get('LinkDesc') == 'Data':
                data = lv[0] if lv else None
                continue
            objs = []
            for v in lv:
                vp = (by_path.get(v) or (None, {}))[1]
                if vp.get('ObjValue'):
                    objs.append(vp['ObjValue'])
            bind[vl.get('LinkDesc')] = [o.rsplit('.', 1)[-1] for o in objs]
            for o in objs:
                actors[o] = actor(o)
        dp = props(data) if data else None
        if not dp:
            continue
        groups = []
        for g in dp.get('InterpGroups') or []:
            gc, gp = cls(g), props(g) or {}
            entry = {'name': gp.get('GroupName', 'Director' if gc == 'InterpGroupDirector' else ''), 'class': gc,
                     'actors': bind.get(gp.get('GroupName'), []), 'tracks': []}
            for t in gp.get('InterpTracks') or []:
                tc, tp = cls(t), props(t) or {}
                if tc == 'InterpTrackMove':
                    entry['tracks'].append({'class': tc, 'frame': tp.get('MoveFrame', 'IMF_World'),
                                            'pos': track((tp.get('PosTrack') or {}).get('Points')),
                                            'euler': track((tp.get('EulerTrack') or {}).get('Points'))})
                elif tc == 'InterpTrackToggle':   # emitter activation keys (ETTA_On / Off / Toggle)
                    entry['tracks'].append({'class': tc, 'toggles': [{'t': k.get('Time', 0.0), 'action': k.get('ToggleAction', 'ETTA_On')}
                                                                     for k in tp.get('ToggleTrack') or []]})
                elif tc == 'InterpTrackEvent':    # named keys -> the matinee's output links of that name
                    entry['tracks'].append({'class': tc, 'events': [{'t': k.get('Time', 0.0), 'name': k.get('EventName')}
                                                                    for k in tp.get('EventTrack') or []]})
                elif tc == 'InterpTrackFloatProp':   # e.g. DrawScale (vignette ships / boosters), FOVAngle (cameras)
                    entry['tracks'].append({'class': tc, 'property': tp.get('PropertyName'),
                                            'keys': float_track((tp.get('FloatTrack') or {}).get('Points'))})
                elif tc == 'InterpTrackFloatMaterialParam':   # material instance scalar (emblem Highlighted / Opacity)
                    entry['tracks'].append({'class': tc, 'property': tp.get('ParamName'), 'materialParam': True,
                                            'keys': float_track((tp.get('FloatTrack') or {}).get('Points'))})
                elif tc == 'InterpTrackDirector':
                    entry['tracks'].append({'class': tc, 'cuts': [{'t': x.get('Time', 0.0), 'group': x.get('TargetCamGroup'),
                                                                   'blend': x.get('TransitionTime', 0.0)}
                                                                  for x in tp.get('CutTrack') or []]})
                else:
                    entry['tracks'].append({'class': tc})
            groups.append(entry)
        starts = []
        for src, desc, idx in incoming.get(op, []):
            sc, sp = by_path.get(src, (None, {}))
            entry = {'from': src.rsplit('.', 1)[-1], 'class': sc, 'output': desc,
                     'input': ['Play', 'Reverse', 'Stop', 'Pause', 'Change Dir'][idx] if 0 <= idx < 5 else idx,
                     'fscommand': sp.get('FsCommand'), 'event': sp.get('EventName'), 'comment': sp.get('ObjComment')}
            if sc == 'SeqEvent_SequenceActivated':
                # A subsequence input event: the movie fscommands wired to that input of the parent Sequence (e.g.
                # UI_CharacterCustomization_m emblem glow / fade: glowAutobot -> GLOWIN_Autobot -> Autobot_Glow Play).
                entry['fscommands'] = sorted(set(seq_input_fscommands(src)))
            starts.append(entry)
        # Event-track outputs wired to SeqAct_ToggleHidden (inputs Hide / UnHide / Toggle) with their target actors.
        actions = []
        for out in p.get('OutputLinks') or []:
            for l in out.get('Links') or []:
                tgt = l.get('LinkedOp')
                tc_, tp_ = by_path.get(tgt, (None, {}))
                if tc_ != 'SeqAct_ToggleHidden':
                    continue
                names = []
                for vl in tp_.get('VariableLinks') or []:
                    if vl.get('LinkDesc') != 'Target':
                        continue
                    for v in vl.get('LinkedVariables') or []:
                        vp = (by_path.get(v) or (None, {}))[1]
                        if vp.get('ObjValue'):
                            names.append(vp['ObjValue'].rsplit('.', 1)[-1])
                idx = l.get('InputLinkIdx', 0)
                actions.append({'event': out.get('LinkDesc'), 'action': ['hide', 'unhide', 'toggle'][idx] if 0 <= idx < 3 else 'toggle',
                                'targets': names})
        matinees.append({'name': op.rsplit('.', 1)[-1], 'comment': p.get('ObjComment'), 'looping': bool(p.get('bLooping')),
                         'length': dp.get('InterpLength', 0.0), 'groups': groups, 'startedBy': starts, 'eventActions': actions})
    # SeqAct_ActivateRemoteEvent: the remote events this level raises and what fires them (e.g. UI_FrontEnd_m
    # enterFrontEnd -> StartFireworks, which plays the streamed battle vignette's matinees).
    remotes = []
    for op, (c, p) in by_path.items():
        if c != 'SeqAct_ActivateRemoteEvent':
            continue
        trig = []
        for src, desc, idx in incoming.get(op, []):
            sc, sp = by_path.get(src, (None, {}))
            trig.append({'class': sc, 'output': desc, 'fscommand': sp.get('FsCommand'), 'event': sp.get('EventName')})
        remotes.append({'name': op.rsplit('.', 1)[-1], 'event': p.get('EventName'), 'startedBy': trig})
    # cameras of the level (default view when no matinee drives one) and the bases of attached actors
    mjson = os.path.join(ROOT, 'ExtractedAssets', 'maps', level + '.json')
    if os.path.exists(mjson):
        for a in json.load(open(mjson, encoding='utf-8')).get('actors', []):
            if a.get('class') == 'CameraActor':
                op = level + '.TheWorld.PersistentLevel.' + a['name']
                actors[op] = actor(op)
    for op in list(actors):
        b = actors[op].get('base')
        while b:
            bop = level + '.TheWorld.PersistentLevel.' + b
            if bop in actors:
                break
            actors[bop] = actor(bop)
            b = actors[bop].get('base')
    # Customization camera switches (UI_CharacterCustomization_m): a subsequence whose ChassisID variable is a
    # SeqVar_TnCustomizationCameraId (PreviewCharNumber = preview slot) compares it with ints and finishes with a class
    # output (Scout / Scientist / Leader / Soldier); matinees start from those outputs (Play / Reverse). The
    # subsequence is reached from movie fscommands through another subsequence (Preview_Characters: fscommand ->
    # input -> FinishSequence output -> this subsequence).
    def sub_objects(seq_op):
        sp = by_path.get(seq_op, (None, {}))[1]
        res = {}
        for o in sp.get('SequenceObjects') or []:
            c2, p2 = cls(o), props(o) or {}
            res[o] = (c2, p2)
        return res
    switches = []
    for op, (c, p) in by_path.items():
        if c != 'Sequence':
            continue
        slot = None
        for vl in p.get('VariableLinks') or []:
            for v in vl.get('LinkedVariables') or []:
                if cls(v) == 'SeqVar_TnCustomizationCameraId':
                    slot = (props(v) or {}).get('PreviewCharNumber', 0)
        if slot is None:
            continue
        inner = sub_objects(op)
        outputs = {}
        for o, (c2, p2) in inner.items():
            if c2 != 'SeqCond_CompareInt':
                continue
            a_val = 0
            for vl in p2.get('VariableLinks') or []:
                if vl.get('LinkDesc') == 'A':
                    for v in vl.get('LinkedVariables') or []:
                        if v in inner and inner[v][0] == 'SeqVar_Int':
                            a_val = inner[v][1].get('IntValue', 0)
            for out in p2.get('OutputLinks') or []:
                if out.get('LinkDesc') != 'A == B':
                    continue
                for l in out.get('Links') or []:
                    t = l.get('LinkedOp')
                    if t in inner and inner[t][0] == 'SeqAct_FinishSequence':
                        outputs[str(a_val)] = inner[t][1].get('ObjComment')
        # fscommands that reach it: Main_Sequence source -> (if a Sequence) its input event -> its FinishSequence output.
        triggers = []
        for src, desc, idx in incoming.get(op, []):
            sc, sp = by_path.get(src, (None, {}))
            if sc == 'GFxEvent_FsCommand' and sp.get('FsCommand'):
                triggers.append(sp['FsCommand'])
            elif sc == 'Sequence':
                sinner = sub_objects(src)
                inputs = [l.get('LinkDesc') for l in sp.get('InputLinks') or []]
                for o, (c2, p2) in sinner.items():   # input events finishing with output 'desc'
                    if c2 != 'SeqEvent_SequenceActivated':
                        continue
                    reaches = any(sinner.get(l.get('LinkedOp'), (None, {}))[0] == 'SeqAct_FinishSequence' and
                                  sinner[l.get('LinkedOp')][1].get('ObjComment') == desc
                                  for out in p2.get('OutputLinks') or [] for l in out.get('Links') or [])
                    if not reaches:
                        continue
                    in_name = p2.get('ObjComment') or p2.get('InputLabel')
                    if in_name not in inputs:
                        continue
                    in_idx = inputs.index(in_name)
                    for src2, desc2, idx2 in incoming.get(src, []):
                        sc2, sp2 = by_path.get(src2, (None, {}))
                        if idx2 == in_idx and sc2 == 'GFxEvent_FsCommand' and sp2.get('FsCommand'):
                            triggers.append(sp2['FsCommand'])
        switches.append({'name': op.rsplit('.', 1)[-1], 'comment': p.get('ObjComment'), 'previewSlot': slot,
                         'outputs': outputs, 'triggers': sorted(set(triggers))})
    # Spawned-pawn visibility (Preview_Characters): fscommand -> subsequence input -> SeqAct_ToggleHidden (Hide / UnHide /
    # Toggle) on named pawn variables (SeqVar_Named FindVarName / SeqVar_Object VarName, e.g. PreviewGuy0 / 1).
    pawn_vis = []
    for op, (c, p) in by_path.items():
        if c != 'Sequence':
            continue
        inner = sub_objects(op)
        inputs = [l.get('LinkDesc') for l in p.get('InputLinks') or []]
        for o, (c2, p2) in inner.items():
            if c2 != 'SeqEvent_SequenceActivated':
                continue
            in_name = p2.get('ObjComment') or p2.get('InputLabel')
            if in_name not in inputs:
                continue
            cmds = [by_path[s2][1].get('FsCommand') for s2, d2, i2 in incoming.get(op, [])
                    if i2 == inputs.index(in_name) and by_path.get(s2, (None,))[0] == 'GFxEvent_FsCommand']
            for out in p2.get('OutputLinks') or []:
                for l in out.get('Links') or []:
                    t = l.get('LinkedOp')
                    if inner.get(t, (None,))[0] != 'SeqAct_ToggleHidden':
                        continue
                    idx = l.get('InputLinkIdx', 0)
                    names = []
                    for vl in inner[t][1].get('VariableLinks') or []:
                        if vl.get('LinkDesc') != 'Target':
                            continue
                        for v in vl.get('LinkedVariables') or []:
                            vp = inner.get(v, (None, {}))[1] if v in inner else (props(v) or {})
                            n = vp.get('FindVarName') or vp.get('VarName')
                            if n:
                                names.append(n)
                    for cmd in cmds:
                        if cmd and names and not any(x['fscommand'] == cmd and x['pawns'] == sorted(names) for x in pawn_vis):
                            pawn_vis.append({'fscommand': cmd, 'action': ['hide', 'unhide', 'toggle'][idx] if 0 <= idx < 3 else 'toggle',
                                             'pawns': sorted(names)})
    return {'actors': sorted(actors.values(), key=lambda a: a['name']), 'matinees': sorted(matinees, key=lambda m: m['name']),
            'remoteEvents': sorted(remotes, key=lambda r: r['name']),
            'cameraSwitches': sorted(switches, key=lambda w: w['name']),
            'pawnVisibility': sorted(pawn_vis, key=lambda x: (x['fscommand'], x['action']))}


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    levels = sorted({l for ls in SCENES.values() for l in ls})
    out = {'generated_by': 'tools/frontend/export_frontend_scenes.py',
           'sources': ['AssetTools/manifests/authored.db (read-only)', 'ExtractedAssets/maps/<level>.json',
                       'RE OVERNIGHT_2026-10-04 section D (scene levels)'],
           'provenance': 'CONFIRMED authored Kismet / matinee / actor data',
           'scenes': SCENES, 'levels': {}}
    for l in levels:
        out['levels'][l] = export_level(l)
        print(l, len(out['levels'][l]['matinees']), 'matinees,', len(out['levels'][l]['actors']), 'actors')
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    json.dump(out, open(OUT, 'w', encoding='utf-8'), indent=1)
    print('wrote', os.path.abspath(OUT))


if __name__ == '__main__':
    main()
