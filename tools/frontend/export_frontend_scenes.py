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
            starts.append({'from': src.rsplit('.', 1)[-1], 'class': sc, 'output': desc,
                           'input': ['Play', 'Reverse', 'Stop', 'Pause', 'Change Dir'][idx] if 0 <= idx < 5 else idx,
                           'fscommand': sp.get('FsCommand'), 'event': sp.get('EventName'), 'comment': sp.get('ObjComment')})
        matinees.append({'name': op.rsplit('.', 1)[-1], 'comment': p.get('ObjComment'), 'looping': bool(p.get('bLooping')),
                         'length': dp.get('InterpLength', 0.0), 'groups': groups, 'startedBy': starts})
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
    return {'actors': sorted(actors.values(), key=lambda a: a['name']), 'matinees': sorted(matinees, key=lambda m: m['name']),
            'remoteEvents': sorted(remotes, key=lambda r: r['name'])}


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
