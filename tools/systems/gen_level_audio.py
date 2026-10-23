"""Generate src/game/LevelAudio.inc: the Systems level-audio manifests (one JSON document per level), from the
cooked level packages in AssetTools manifests/authored.db (read-only).

A level manifest uses the map audio.json schema (cues / reverb_presets) plus:
  "cue_limits": {cue: {MaxConcurrentPlayCount, InstanceLimiting}}   the authored per-cue-asset limits of every cue
                the level's audio uses (a map's AssetTools audio.json does not carry them)
  "kismet":     the level's Kismet audio ops and what triggers them:
      "actors":  {actor: [x, y, z] glTF metres}            Target / Source Actor variables
      "ops":     [{id, type, ...}]   play_sound / positional_pool / reverb / play_music / stop_music / timeline
      "links":   [{from, to, input}] from = GameplayStarted | FsCommand:<cmd> | MovieStopped:<movie> |
                                      Timeline:<timeline op id>:<event name>; input 0 = Play/Start, 1 = Stop
For MP_IAC_Streets only "cue_limits" is emitted: its emitters / zones / pools / reverb presets / cue bank come from
the AssetTools audio.json (the Systems manifest supplements it).

Run with AssetTools/bin/py/python.exe:  python tools/systems/gen_level_audio.py <out.inc>
"""
import io, json, os, sqlite3, sys

DB = 'F:/Transformers Rebuild/AssetTools/manifests/authored.db'
STREETS_AUDIO = 'F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps/MP_IAC_Streets/audio.json'
CONTENT = 'F:/Transformers Rebuild/ExtractedAssets/content/'
UI_LEVELS = ['UI_FrontEnd_m', 'UI_PartyLobby_m', 'UI_Lobby_m', 'UI_CampaignLobby_m']
AUDIO_OPS = {'SeqAct_PlaySound', 'SeqAct_PlayPlayerPositionalSound', 'SeqAct_Reverb', 'SeqAct_PlayMusic',
             'SeqAct_StopMusic'}
NOT_EVENTS = {'Completed', 'Aborted', 'Reversed', 'Out', 'Finished', 'Stopped'}

c = sqlite3.connect(DB)
ROWS = {}
def row(op):
    if op not in ROWS:
        r = c.execute('select class, props, package from objects where opath=?', (op,)).fetchone()
        ROWS[op] = (r[0], json.loads(r[1]), r[2]) if r else (None, {}, None)
    return ROWS[op]

def cls_default(path):
    r = c.execute('select props from objects where opath=?', (path,)).fetchone()
    return json.loads(r[0]) if r else {}

D_PLAYSOUND = cls_default('Engine.Default__SeqAct_PlaySound')
D_PP = cls_default('HM_Engine.Default__SeqAct_PlayPlayerPositionalSound')
D_MUSIC = cls_default('HM_Engine.Default__SeqAct_PlayMusic')['MusicTrack']
D_INTERP = cls_default('Engine.Default__SeqAct_Interp')
D_CUE = cls_default('Engine.Default__SoundCue')
MIXER = json.loads(c.execute("select props from objects where opath='SoundConfig.SoundMixerProperties'").fetchone()[0])
MIX_PRESETS = {p['Name']: p for p in MIXER['MixerPresets']}
MASTER_WET = {p['Name']: p for cat in MIXER['SoundCategories'] if cat['Name'] == 'MASTER_WET' for p in cat['DSPPresets']}

def gltf(loc):   # UE (X, Y, Z) units -> glTF metres (x, y, z) = (X, Z, Y) * 0.01 (AssetTools location_gltf)
    return [round(loc.get('X', 0.0) * 0.01, 4), round(loc.get('Z', 0.0) * 0.01, 4), round(loc.get('Y', 0.0) * 0.01, 4)]

def cue_package_row(cue, prefer=None):
    rows = c.execute("select package, props from objects where opath=? and class='SoundCue'", (cue,)).fetchall()
    if not rows: return None
    for pk, pr in rows:
        if pk == prefer: return json.loads(pr)
    return json.loads(rows[0][1])

def cue_limits(cue, prefer=None):
    p = cue_package_row(cue, prefer) or {}
    return {'MaxConcurrentPlayCount': int(p.get('MaxConcurrentPlayCount', D_CUE.get('MaxConcurrentPlayCount', 5))),
            'InstanceLimiting': p.get('InstanceLimiting', D_CUE.get('InstanceLimiting', 'kKillFarthest'))}

def cue_tree(cue, prefer=None):
    p = cue_package_row(cue, prefer)
    if not p or not p.get('FirstNode'): return None
    _, rp, _ = row(p['FirstNode'])
    params = {k: v for k, v in rp.items() if k not in ('ChildNodes', 'Category', 'SecondaryCategory', 'PlayMixerPreset')}
    if rp.get('Category'): params['Category'] = row(rp['Category'])[1].get('CategoryName', '')
    kids = []
    for en in [x for x in (rp.get('ChildNodes') or []) if x]:
        ecls, ep, _ = row(en)
        waves = []
        for w in [x for x in (ep.get('ChildNodes') or []) if x]:
            pk, nm = w.split('.', 1)
            wav = 'content/%s/%s.wav' % (pk, nm)
            if not os.path.exists(CONTENT + wav[8:]): print('  MISSING wave', wav)
            entry = {'node': w, 'class': 'SoundNodeWaveEx', 'wav': wav}
            # Localized wave: the extracted file came from this _LOC_ twin (authored.db keeps one per path) [RE pass4 C].
            r = c.execute("select package from objects where opath=? and class like 'SoundNodeWave%'", (w,)).fetchone()
            if r and '_LOC_' in (r[0] or ''): entry['loc'] = r[0].rsplit('_LOC_', 1)[-1]
            waves.append(entry)
        kids.append({'node': en.split('.')[-1], 'class': ecls,
                     'params': {k: v for k, v in ep.items() if k != 'ChildNodes'}, 'children': waves})
    out = {'cue': cue, 'class': 'SoundCue', 'tree': {'node': p['FirstNode'].split('.')[-1], 'class': 'SoundNodeRoot',
                                                      'params': params, 'children': kids}}
    out.update(cue_limits(cue, prefer))
    return out

def reverb_preset(name):
    mp, dsp = MIX_PRESETS.get(name), MASTER_WET.get(name)
    if not mp: print('  MISSING mixer preset', name); return None
    return {'mixer_preset': mp, 'dsp_by_category': {'MASTER_WET': dsp} if dsp else {}}

SINK_OPS = {'SeqAct_PlaySound', 'SeqAct_PlayPlayerPositionalSound', 'SeqAct_PlayFlybySound', 'SeqAct_Reverb',
            'SeqAct_Mixer', 'SeqAct_PlayMusic', 'SeqAct_StopMusic', 'SeqAct_AmbientAudioZone', 'SeqAct_Delay', 'SeqAct_Gate'}
STATEFUL = {'SeqAct_AmbientAudioZone', 'SeqAct_Delay', 'SeqAct_Gate', 'SeqEvent_Touch'}   # their outputs are runtime events
# Dialog ops (any level: the Escalation maps' Round Begin / End chains, the campaign): SeqAct_Conversation (lines), SeqAct_PlayDialog
# (one dialog cue: Play / Stop, Stopped / Finished, a ducking preset - exported as a one-line conversation), SeqAct_RandomSwitch
# (UE3: a random enabled 'Link N' per input; bAutoDisableLinks disables the used one, bLooping re-enables all once none is left).
DIALOG_OPS = {'SeqAct_Conversation', 'SeqAct_PlayDialog', 'SeqAct_RandomSwitch'}
PASS_THROUGH = {'SeqAct_ActivateRemoteEvent', 'SeqAct_Toggle', 'SeqAct_Log', 'SeqAct_DialogGroup', 'SeqAct_Mixer'}
D_FLYBY = cls_default('HM_Engine.Default__SeqAct_PlayFlybySound')
D_DELAY = cls_default('Engine.Default__SeqAct_Delay')
D_GATE = cls_default('Engine.Default__SeqAct_Gate')

def mixer_preset_dsp(name):
    """A mixer preset (SoundMixerProperties MixerPresets) with every category's DSP values of that name (Volume + the
    MASTER_WET reverb / echo the runtime applies)."""
    mp = MIX_PRESETS.get(name)
    if not mp: print('  MISSING mixer preset', name); return None
    cats = {}
    for cat in MIXER['SoundCategories']:
        for d in cat['DSPPresets']:
            if d['Name'] == name: cats[cat['Name']] = d
    return {'mixer_preset': mp, 'dsp_by_category': cats}

def level(name, packages, touch_geo=None, bank=frozenset(), prefer=None, campaign=False):
    """The Kismet audio graph of a level (all `packages`: a UI level, or a map's sublevels).
    touch_geo: {SeqEvent_Touch name: trigger polygons (glTF)} from the AssetTools map manifest."""
    objs = {}
    for pkg in packages:
        for r in c.execute('select opath, class, props from objects where package=?', (pkg,)):
            objs[r[0]] = (r[1], json.loads(r[2]))
    def sid(o):
        s = o.split('Main_Sequence.', 1)[1] if 'Main_Sequence.' in o else o.split('.')[-1]
        return (o.split('.')[0] + '|' + s) if len(packages) > 1 else s     # unique across sublevels
    incoming = {}
    for o, (cl, p) in objs.items():
        for ol in p.get('OutputLinks') or []:
            for l in ol.get('Links') or []:
                if l.get('LinkedOp'): incoming.setdefault(l['LinkedOp'], []).append((o, ol.get('LinkDesc'), l.get('InputLinkIdx', 0)))
    remote_events = {}
    for o, (cl, p) in objs.items():
        if cl == 'SeqAct_ActivateRemoteEvent': remote_events.setdefault(p.get('EventName'), []).append(o)
    def var_objects(p, desc):
        out = []
        for vl in p.get('VariableLinks') or []:
            if vl.get('LinkDesc') != desc: continue
            for v in vl.get('LinkedVariables') or []:
                vc, vp = objs.get(v, (None, {}))
                if vc == 'SeqVar_Object' and vp.get('ObjValue'): out.append(vp['ObjValue'])
                elif vc == 'SeqVar_Player': out.append('<player>')
        return out
    actors, warnings = {}, []
    def actor(a):
        if a == '<player>': return '<player>'
        cl, p = objs.get(a, (None, {}))
        if cl is None: cl, p, _ = row(a)
        if p and 'Location' in p: actors[a.split('.')[-1]] = gltf(p['Location'])
        else: warnings.append('actor without location ' + a); return None
        return a.split('.')[-1]

    def is_audio_target(t):
        cl = objs.get(t, (None,))[0]
        if cl in SINK_OPS: return True
        if cl == 'Sequence': return any(o.startswith(t + '.') and objs[o][0] in SINK_OPS for o in objs)
        return False
    timelines = {}
    for o, (cl, p) in objs.items():
        if cl != 'SeqAct_Interp': continue
        data = [v for vl in p.get('VariableLinks') or [] if vl.get('LinkDesc') == 'Data' for v in vl.get('LinkedVariables') or []]
        # [campaign] InterpTrackSound keys: {time, cue, volume, pitch, actor} - the group's actor is the SeqAct_Interp variable
        # link named after the InterpGroup's GroupName (UE3 plays the key's sound on the group actor; none = at the listener)
        tl_sounds = []
        if campaign and data:
            for t, (tc, tp) in objs.items():
                if tc != 'InterpTrackSound' or not t.startswith(data[0] + '.'): continue
                grp = t.rsplit('.', 1)[0]
                gname = objs.get(grp, (None, {}))[1].get('GroupName') or ''
                act = [a for a in (actor(x) for x in var_objects(p, gname)) if a and a != '<player>'] if gname else []
                for k in tp.get('Sounds') or []:
                    if not k.get('Sound'): continue
                    tl_sounds.append({'time': round(k.get('Time', 0.0), 4), 'cue': k['Sound'], 'volume': k.get('Volume', 1.0),
                                      'pitch': k.get('Pitch', 1.0), 'actor': act[0] if act else None})
        if not tl_sounds and not any(ol.get('LinkDesc') not in NOT_EVENTS and any(is_audio_target(l.get('LinkedOp')) for l in ol.get('Links') or [])
                   for ol in p.get('OutputLinks') or []): continue
        idata = objs.get(data[0], (None, {}))[1] if data else {}
        events = []
        for t, (tc, tp) in objs.items():
            if tc == 'InterpTrackEvent' and data and t.startswith(data[0] + '.'):
                events += [{'time': round(e['Time'], 4), 'name': e['EventName']} for e in tp.get('EventTrack') or []]
        timelines[o] = {'id': sid(o), 'type': 'timeline', 'comment': p.get('ObjComment', ''),
                        'length': round(idata.get('InterpLength', 5.0), 4),
                        'looping': bool(p.get('bLooping', D_INTERP.get('bLooping', False))),
                        'play_rate': p.get('PlayRate', D_INTERP.get('PlayRate', 1.0)),
                        'events': sorted(events, key=lambda e: e['time'])}
        if tl_sounds: timelines[o]['sounds'] = sorted(tl_sounds, key=lambda e: e['time'])

    def triggers(op, inp, depth=0, seen=None):
        seen = set() if seen is None else seen
        if (op, inp) in seen or depth > 12: return []
        seen.add((op, inp))
        out = []
        for src, desc, idx in incoming.get(op, []):
            if inp is not None and idx != inp: continue
            cl, p = objs.get(src, (None, {}))
            if cl == 'SeqEvent_GameplayStarted': out.append('GameplayStarted')
            elif cl == 'SeqEvent_LevelLoaded': out.append('LevelLoaded')
            elif cl == 'GFxEvent_FsCommand': out.append('FsCommand:' + p.get('FsCommand', ''))
            elif cl == 'SeqAct_MoviePlayer' and desc == 'Stopped': out.append('MovieStopped:' + p.get('MovieName', ''))
            elif cl == 'SeqAct_Interp' and src in timelines: out.append('Timeline:%s:%s' % (sid(src), desc))
            elif cl in STATEFUL or cl in DIALOG_OPS: out.append('Out:%s:%s' % (sid(src), desc))
            elif cl == 'SeqEvent_SequenceActivated':
                seq = src.rsplit('.', 1)[0]
                for i, il in enumerate(objs.get(seq, (None, {}))[1].get('InputLinks') or []):
                    if il.get('LinkedOp') == src: out += triggers(seq, i, depth + 1, seen)
            elif cl == 'SeqEvent_RemoteEvent':       # every ActivateRemoteEvent of that name (any input)
                for a in remote_events.get(p.get('EventName'), []): out += triggers(a, None, depth + 1, seen)
            elif cl in PASS_THROUGH:                 # Out fires on any input (no latency)
                out += triggers(src, None, depth + 1, seen)
            elif cl and (cl.startswith('TnSeqEvent_') or cl.startswith('SeqEvent_')):
                out.append('Game:%s:%s' % (cl, desc))  # a gameplay-owned event: fired by name (World::levelAudioEvent)
            else:
                out.append('Unresolved:%s:%s' % (cl, desc)); warnings.append('unresolved trigger %s (%s) [%s] -> %s' % (sid(src), cl, desc, sid(op)))
        return out

    ops, links, cues, presets = [], [], set(), {}
    def preset_name(ref):
        return objs.get(ref, (None, {}))[1].get('PresetName') or row(ref or '')[1].get('PresetName')
    for o, (cl, p) in sorted(objs.items()):
        if cl not in SINK_OPS and cl != 'SeqEvent_Touch' and cl not in DIALOG_OPS: continue
        op = {'id': sid(o)}
        n_inputs = 2
        if cl == 'SeqAct_RandomSwitch':
            op.update(type='random_switch', links=int(p.get('LinkCount', 1)), looping=bool(p.get('bLooping', False)),
                      auto_disable=bool(p.get('bAutoDisableLinks', False)))
            ops.append(op)
            for t in triggers(o, 0): links.append({'from': t, 'to': op['id'], 'input': 0})
            continue
        if cl == 'SeqAct_PlayDialog':   # one dialog cue = a one-line conversation (Play / Stop in; Stopped / Finished out)
            nm = preset_name(p.get('DuckingMixerPreset'))
            op.update(type='conversation', duck=nm, lines=[{'cue': p.get('SoundCue'), 'post_delay': 0.0, 'speaker': None, 'output': None}])
            if p.get('SoundCue'): cues.add(p['SoundCue'])
            if nm: presets[nm] = mixer_preset_dsp(nm)
            ops.append(op)
            for inp in range(2):
                for t in triggers(o, inp): links.append({'from': t, 'to': op['id'], 'input': inp})
            continue
        if cl == 'SeqAct_Conversation':   # [campaign] Lines [{SoundCue, PostDelayTime}] in order; Play / Stop; Played / Finished
            nm = preset_name(p.get('DuckingMixerPreset'))
            # RE CAMPAIGN_DIALOG_TIMING_RE: outputs 0 Played / 1 Stopped / 2 Finished, then one per line (3 + i); variable links
            # in pairs per line, [2i] = the speaker. A line without a cue aborts the conversation there (-> Stopped), so keep it.
            vls, ols = p.get('VariableLinks') or [], p.get('OutputLinks') or []
            def speaker(i):
                if 2 * i >= len(vls): return None
                a = [x for x in (actor(v) for v in var_objects(p, vls[2 * i].get('LinkDesc'))) if x and x != '<player>']
                return a[0] if a else None
            op.update(type='conversation', duck=nm,
                      lines=[{'cue': l.get('SoundCue'), 'post_delay': l.get('PostDelayTime', 0.0), 'speaker': speaker(i),
                              'output': ols[3 + i].get('LinkDesc') if 3 + i < len(ols) else None}
                             for i, l in enumerate(p.get('Lines') or [])])
            for l in op['lines']:
                if l['cue']: cues.add(l['cue'])
            if nm: presets[nm] = mixer_preset_dsp(nm)
            ops.append(op)
            for inp in range(2):
                for t in triggers(o, inp): links.append({'from': t, 'to': op['id'], 'input': inp})
            continue
        if cl == 'SeqAct_PlaySound':
            q = dict(D_PLAYSOUND); q.update(p)
            op.update(type='play_sound', cue=q.get('PlaySound'), fade_in=q.get('FadeInTime', 0.0), fade_out=q.get('FadeOutTime', 0.0),
                      volume=q.get('VolumeMultiplier', 1.0), pitch=q.get('PitchMultiplier', 1.0),
                      suppress_spatialization=bool(q.get('bSuppressSpatialization', False)),
                      targets=[t for t in (actor(a) for a in var_objects(p, 'Target')) if t and t != '<player>'])
            cues.add(op['cue'])
        elif cl == 'SeqAct_PlayPlayerPositionalSound':
            q = dict(D_PP); q.update(p)
            src = [s for s in (actor(a) for a in var_objects(p, 'Source Actor')) if s]
            op.update(type='positional_pool', cue=q.get('SoundToPlay'), delay_min=q['DelayMin'], delay_max=q['DelayMax'],
                      distance_min=q['DistanceMin'], distance_max=q['DistanceMax'], looping=bool(q['Looping']),
                      source=src[0] if src else None)
            cues.add(op['cue'])
        elif cl == 'SeqAct_PlayFlybySound':
            q = dict(D_FLYBY); q.update(p)
            tg = [s for s in (actor(a) for a in var_objects(p, 'Target')) if s]
            op.update(type='flyby', cue=q.get('FlybySound'), delay_min=q['DelayMin'], delay_max=q['DelayMax'],
                      angle_max=q['AngleMax'], start_distance_min=q['StartingDistanceMin'], start_distance_max=q['StartingDistanceMax'],
                      speed_min=q['SpeedMin'], speed_max=q['SpeedMax'], head_offset_min=q['HeadOffsetMin'],
                      head_offset_max=q['HeadOffsetMax'], looping=bool(q['Looping']), target=tg[0] if tg else None)
            cues.add(op['cue'])
        elif cl == 'SeqAct_Reverb':
            nm = preset_name(p.get('ReverbMixerPreset'))
            op.update(type='reverb', preset=nm)
            if nm: presets[nm] = mixer_preset_dsp(nm)
        elif cl == 'SeqAct_Mixer':
            nm = preset_name(p.get('Preset'))
            op.update(type='mixer', preset=nm)
            if nm: presets[nm] = mixer_preset_dsp(nm)
        elif cl == 'SeqAct_PlayMusic':
            t = dict(D_MUSIC); t.update(p.get('MusicTrack') or {})
            op.update(type='play_music', cue=t['SoundCue'], fade_in=t['FadeInTime'], fade_out=t['FadeOutTime'],
                      boredom=t['BoredomTime'], priority=t['Priority'],
                      ignore_spaz_timer=bool(p.get('IgnoreSpazTimer', False)),
                      fade_out_override=p.get('CurrentTrackFadeOutTimeOverride', -1.0) if p.get('UseCurrentTrackFadeOutTimeOverride') else -1.0)
        elif cl == 'SeqAct_StopMusic':
            op.update(type='stop_music', fade_out_override=p.get('FadeTimeOverride', -1.0) if p.get('UseFadeTimeOverride') else -1.0)
        elif cl == 'SeqAct_AmbientAudioZone':
            op.update(type='zone', scenes=int(p.get('NumScenes', 1)))
            n_inputs = 1 + int(p.get('NumScenes', 1))          # Enter, Scene 0..N-1
        elif cl == 'SeqAct_Delay':
            op.update(type='delay', duration=p.get('Duration', D_DELAY.get('Duration', 1.0)))
            n_inputs = 3                                        # Start, Stop, Pause
        elif cl == 'SeqAct_Gate':
            op.update(type='gate', open=bool(p.get('bOpen', D_GATE.get('bOpen', True))))
            n_inputs = 4                                        # In, Open, Close, Toggle
        elif cl == 'SeqEvent_Touch':
            key = o.split('.')[-1]
            geo = (touch_geo or {}).get(key)
            if geo is None: warnings.append('touch without volume geometry ' + key); continue
            tr = geo.get('trigger', {})
            op.update(type='touch', polygons=geo['polygons'], max_trigger=tr.get('MaxTriggerCount', 0),
                      retrigger_delay=tr.get('ReTriggerDelay', 0.0), comment=p.get('ObjComment') or geo.get('comment', ''))
            n_inputs = 0
        ops.append(op)
        for inp in range(n_inputs):
            for t in triggers(o, inp): links.append({'from': t, 'to': op['id'], 'input': inp})
    for o, tl in sorted(timelines.items()):
        ops.append(tl)
        for t in triggers(o, 0): links.append({'from': t, 'to': tl['id'], 'input': 0})
    # Keep only what can make or change sound: sinks some trigger reaches, and stateful ops (touch / zone / delay / gate)
    # whose outputs lead to a kept op. Links into dropped ops and from unresolved sources go too.
    links = [l for l in links if not l['from'].startswith('Unresolved:')]
    SOUND = {'play_sound', 'positional_pool', 'flyby', 'reverb', 'mixer', 'play_music', 'stop_music', 'timeline', 'conversation'}
    byid = {op['id']: op for op in ops}
    keep = {op['id'] for op in ops if op['type'] in SOUND and any(l['to'] == op['id'] for l in links)}
    changed = True
    while changed:
        changed = False
        for l in links:
            if l['to'] in keep and l['from'].startswith('Out:'):
                src = l['from'].split(':', 2)[1]
                if src in byid and src not in keep: keep.add(src); changed = True
    for o2, tl in timelines.items():
        if tl['id'] in keep: pass
    ops = [op for op in ops if op['id'] in keep]
    links = [l for l in links if l['to'] in keep and (not l['from'].startswith('Out:') or l['from'].split(':', 2)[1] in keep)]
    used = {op.get('cue') for op in ops} | {l['cue'] for op in ops for l in op.get('lines', [])} | {k['cue'] for op in ops for k in op.get('sounds', [])}
    cues = {q for q in cues if q and q in used}
    if campaign:   # timeline sound keys never passed through `cues` above
        cues |= {k['cue'] for op in ops for k in op.get('sounds', [])}
    # A zone's label (diagnostics): the comment of the first touch volume that enters it.
    for op in ops:
        if op['type'] != 'zone': continue
        for l in links:
            if l['to'] == op['id'] and l['input'] == 0 and l['from'].startswith('Out:') and l['from'].endswith(':Touched'):
                t = byid.get(l['from'].split(':', 2)[1])
                if t and t.get('comment'): op['label'] = t['comment']; break
    cue_defs = {}
    for q in sorted(x for x in cues if x and x not in bank):
        d = cue_tree(q, prefer or packages[0])
        if d: cue_defs[q] = d
        else: warnings.append('cue not found ' + q)
    limits = {q: cue_limits(q, prefer or packages[0]) for q in sorted((set(cues) | set(bank)) - {None})}
    from collections import Counter
    kinds = Counter(op['type'] for op in ops)
    doc = {'map': name, 'source': 'Systems tools/systems/gen_level_audio.py from AssetTools authored.db (%s Kismet)' % ', '.join(packages),
           'cues': cue_defs, 'cue_limits': limits,
           'reverb_presets': {k: v for k, v in presets.items() if v},
           'kismet': {'actors': actors, 'ops': ops, 'links': links, 'replaces_manifest_zones': bool(touch_geo)}}
    for w in sorted(set(warnings)): print('  WARN', name, w)
    print('%s: %d ops %s, %d links, %d own cues, %d presets, %d actors' % (name, len(ops), dict(kinds), len(links),
                                                                           len(cue_defs), len(doc['reverb_presets']), len(actors)))
    return doc

# Campaign chapters (Integration 2026-10-23): one JSON per chapter, loaded at level start - not compiled into LevelAudio.inc.
#   gen_level_audio.py --campaign <CHAPTER> <out.json> [<maps dir>]   (default: the AssetTools campaign pilot slice)
# Adds SeqAct_Conversation (dialog lines + ducking preset) and matinee InterpTrackSound keys; MP output is unchanged.
if len(sys.argv) > 1 and sys.argv[1] == '--campaign':
    chap, dst = sys.argv[2], sys.argv[3]
    cdir = sys.argv[4] if len(sys.argv) > 4 else 'F:/Transformers Rebuild/AssetTools/work/campaign_pilot/VerticalSlice/Maps/'
    am = json.load(io.open(os.path.join(cdir, chap, 'audio.json'), encoding='utf-8'))
    subs = am['sublevels']
    geo = {z['event']: {'polygons': z['trigger_polygons_gltf'], 'trigger': z.get('trigger', {}), 'comment': z.get('comment', '')}
           for z in am['zones'] if z.get('trigger_polygons_gltf')}
    audio_pkg = [x for x in subs if 'audio' in x.lower()]
    doc = level(chap, subs, geo, frozenset(am['cues']), audio_pkg[0] if audio_pkg else None, campaign=True)
    doc['campaign'] = True
    # Subtitles (CAMPAIGN_AUDIO_DIALOG section 3): every localized wave the chapter's cues play -> its subtitle lines per language,
    # from the chapter's own _LOC packages (SoundNodeWaveEx Subtitles [{Text, Time, UseSubtitles}], Duration). UseSubtitles false =
    # authored not to show (kept, flagged: the HUD skips it). int = the English text; FRA where the chapter has it.
    waves = set()
    def walk(n):
        if n.get('class') == 'SoundNodeWaveEx' and n.get('loc'): waves.add(n['node'])
        for ch in n.get('children') or []: walk(ch)
    for cd in doc['cues'].values(): walk(cd.get('tree') or {})
    subs = {}
    # AssetTools sidecars (manifests/campaign/*_loc_int_subtitles.json): the cooked _LOC_int copy of waves whose authored.db row is the
    # French one (one row per object path, the FRA copy won the dedup) - the English source for those, read first.
    import glob
    int_override = {}
    for sc in glob.glob('F:/Transformers Rebuild/AssetTools/manifests/campaign/*_loc_int_subtitles.json'):
        for wv, v in (json.load(io.open(sc, encoding='utf-8')).get('waves') or {}).items():
            lines = []
            for st in v.get('Subtitles') or []:   # raw struct: [{name, type, value}, ...]
                f = {x.get('name'): x.get('value') for x in st}
                if f.get('Text'): lines.append({'time': round(f.get('Time', 0.0) or 0.0, 3), 'text': f['Text'], 'show': bool(f.get('UseSubtitles', True))})
            if lines: int_override[wv] = {'duration': round(v.get('Duration', 0.0) or 0.0, 3), 'int': lines}
    for wv in sorted(waves):
        if wv in int_override:
            subs.setdefault(wv, {'duration': int_override[wv]['duration']})['int'] = int_override[wv]['int']
        for lang, suffix in (('int', '_LOC_int'), ('fra', '_LOC_FRA')):
            if lang == 'int' and wv in int_override: continue   # the sidecar's cooked INT copy wins
            rows = c.execute("select package, props from objects where opath=? and class='SoundNodeWaveEx' and package like ?",
                             (wv, chap + '%' + suffix)).fetchall()
            if not rows:   # a wave shared with another chapter: its _LOC copy there (e.g. D5_OME's MGTRON_LK001521 sits in D3B's)
                rows = c.execute("select package, props from objects where opath=? and class='SoundNodeWaveEx' and package like ? order by package",
                                 (wv, '%' + suffix)).fetchall()
            if not rows: continue
            p = json.loads(rows[0][1] or '{}')
            lines = [{'time': round(x.get('Time', 0.0), 3), 'text': x.get('Text', ''), 'show': bool(x.get('UseSubtitles', True))}
                     for x in p.get('Subtitles') or [] if x.get('Text')]
            if lines: subs.setdefault(wv, {'duration': round(p.get('Duration', 0.0), 3)})[lang] = lines
    doc['subtitles'] = subs
    print('%s: subtitles for %d of %d localized waves (%d with French)' % (chap, len(subs), len(waves),
                                                                         sum(1 for v in subs.values() if 'fra' in v)))
    io.open(dst, 'w', encoding='utf-8', newline='\n').write(json.dumps(doc, separators=(',', ':')))
    sys.exit(0)

docs = [level(p, [p]) for p in UI_LEVELS]
# Multiplayer maps: per-cue-asset limits of the map bank (AssetTools audio.json cues; the cue objects of the audio
# sublevel) and the map's announcer (TnWorldInfo.AnnouncerSoundEventSet -> HmSoundEventSet event -> dialogue cue) with
# the cues of every announcer event and of every game-type message's music, all streamed (decoded on first play).
GT_FIELDS = ('GameTypeDialog', 'GameDescriptionDialog', 'GameTypeMusic', 'GameNearlyCompleteMusic', 'AutobotsWinMusic',
             'DecepticonsWinMusic', 'TieMusic')
gametypes = {}
for o, pr in c.execute("select opath, props from objects where opath like 'TransGame.Default__TnGameTypeMessage%'"):
    d = json.loads(pr)
    gametypes[o.split('Default__')[1]] = {k: d.get(k) for k in GT_FIELDS + ('GameTypeMessage',) if d.get(k)}
progress = json.loads(c.execute("select props from objects where opath='TransGame.Default__TnGameProgressAnnouncementMessage'").fetchone()[0])['Sounds']
gameover = json.loads(c.execute("select props from objects where opath like '%Default__TnVersusGameOverMessage'").fetchone()[0])
annc = json.loads(c.execute("select props from objects where opath='TransGame.Default__TnAnnouncer'").fetchone()[0])
rules = {}
for o, pr in c.execute("select opath, props from objects where opath like 'TransGame.Default__TnGameRules_ReportGameProgress%'"):
    d = json.loads(pr)
    if d.get('Sounds'): rules[o.split('Default__')[1]] = d['Sounds']
# Objective / round message classes (flag, bomb, domination, CTF, round-based, KOTH zone): their dialogue / stinger /
# music fields [CONF class defaults; the switch rules are ported in MatchAudio from the decompiled ClientReceive /
# GetColoredString / DefendingTeamChanged].
OBJ_CLASSES = ('TnFlagMessage', 'TnBombMessage', 'TnDominationMessage', 'TnCTFMessage', 'TnRoundBasedGameMessage', 'TnKingOfTheHillZone')
objective = {}
for o, pr in c.execute("select opath, props from objects where opath like 'Trans%.Default__Tn%'"):
    cls = o.split('Default__')[1]
    if not any(cls == k or (cls.startswith(k) and '.' not in cls) for k in OBJ_CLASSES): continue
    d = json.loads(pr)
    f = {k: v for k, v in d.items() if v and ('Sound' in k or 'Dialog' in k or 'Music' in k)}
    if f: objective[cls] = f
match_music = sorted({v for g in gametypes.values() for k, v in g.items() if k.endswith('Music')})
match_music = sorted(set(match_music) | {v for f in objective.values() for k, v in f.items() if k.endswith('Music') and isinstance(v, str)})
# Countdown ticks (TnGameReplicationInfo.OnCountdownChange / TnGameReplicationInfoMultiplayer.OnObjectiveCountdownChange)
# and the KOTH announcer match-start hysteresis [CONF script + class defaults].
def cdo(op):
    r = c.execute("select props from objects where opath=?", (op,)).fetchone()
    return json.loads(r[0]) if r else {}
gri, grim, koth = cdo('TransGame.Default__TnGameReplicationInfo'), cdo('TransGame.Default__TnGameReplicationInfoMultiplayer'), \
    cdo('TransGame.Default__TnKingOfTheHillZoneBase')
countdown = {'low_tick_sound': gri.get('LowCountdownTickSound') or '', 'low_tick_threshold': gri.get('LowCountdownTickThreshold', 10),
             'objective_tick_sound': grim.get('LowObjectiveCountdownTickSound') or '',
             'objective_tick_threshold': grim.get('LowObjectiveCountdownTickThreshold', 5),
             'koth_announcer_start_hysteresis': koth.get('AnnouncerMatchStartHysteresisTime', 3.0)}
stingers = sorted({v for f in objective.values() for k, v in f.items() if isinstance(v, str) and v.startswith('BL_')} |
                  {q for q in (countdown['low_tick_sound'], countdown['objective_tick_sound']) if q})
# Mode tag -> the game-type message class its rules broadcast (TnOnlineGameSettings<mode>.Rules: the rule with a
# Team / FFA GameMessageClass; TnGameRules.BroadcastGameTypeMessage picks by WorldInfo.Game.bTeamGame; DM's game class
# is TnFreeForAllGame (FFA), SV is cooperative - both message classes are the same there).
rule_msgs = {}
for o, pr in c.execute("select opath, props from objects where opath like 'TransGame.Default__TnGameRules%'"):
    d = json.loads(pr)
    if d.get('TeamGameMessageClass') or d.get('FFAGameMessageClass'):
        rule_msgs['TransGame.' + o.split('Default__')[1]] = (d.get('TeamGameMessageClass'), d.get('FFAGameMessageClass'))
mode_messages = {}
for o, pr in c.execute("select opath, props from objects where opath like 'TransGame.Default__TnOnlineGameSettings%'"):
    tag = o.split('Default__TnOnlineGameSettings')[1]
    rl = json.loads(pr).get('Rules') or []
    for r in rl:
        if r in rule_msgs:
            team, ffa = rule_msgs[r]
            cls = ffa if tag == 'DM' else team
            if cls: mode_messages[tag] = cls.split('.')[-1]
print('mode messages', mode_messages)
# Kill streaks: TransCustomization.ini [<Id> TnDataProvider_Killstreak] ObjectPath -> the TnKillstreak class's
# AnnouncementMessageType (a TnKillstreakActivated* LocalMessage) -> its Self / Friendly / EnemyAnnouncementSound
# (announcer events), each resolved down the class chain [CONF config + class defaults].
import re as _re
_cini = io.open('F:/Transformers Rebuild/ExtractedAssets/config/Coalesced_ini/TransGame/Config/Xenon/Cooked/TransCustomization.ini',
                encoding='utf-8', errors='replace').read()
def _chain_props(path):                                 # UE names are case-insensitive (ini TnKillStreak* vs TnKillstreak*)
    out, chain, t = {}, [], path
    while t:
        r = c.execute('select path, super from types where path=? collate nocase', (t,)).fetchone()
        if not r: break
        chain.append(r[0]); t = r[1]
    for t in reversed(chain):
        op = t.replace('TransGame.', 'TransGame.Default__').replace('TransContent.', 'TransContent.Default__')
        r = c.execute('select props from objects where opath=? collate nocase', (op,)).fetchone()
        if r: out.update(json.loads(r[0]))
    return out
killstreaks = {}
for m in _re.finditer(r'^\[(\w+) TnDataProvider_Killstreak\]\s*$(.*?)(?=^\[|\Z)', _cini, _re.M | _re.S):
    body = dict(_re.findall(r'^(\w+)=(.*?)\s*$', m.group(2), _re.M))
    cls = body.get('ObjectPath', '')
    msg = _chain_props(cls).get('AnnouncementMessageType') if cls else None
    snd = _chain_props(msg) if msg else {}
    killstreaks[m.group(1)] = {'class': cls.split('.')[-1], 'message': (msg or '').split('.')[-1],
                               'self': snd.get('SelfAnnouncementSound') or '', 'friendly': snd.get('FriendlyAnnouncementSound') or '',
                               'enemy': snd.get('EnemyAnnouncementSound') or '',
                               'faction': [x or '' for x in (snd.get('FactionAnnouncementSound') or [])]}
print('killstreaks %d: %s' % (len(killstreaks), {k: (v['self'].split('.')[-1], v['friendly'].split('.')[-1], v['enemy'].split('.')[-1])
                                                 for k, v in sorted(killstreaks.items())}))
docs.append({'map': '__match_messages__',
             'source': 'Systems gen_level_audio.py: TransGame / TransContent message class defaults (authored.db)',
             'game_type_messages': gametypes, 'progress_announcement_sounds': progress,
             'versus_game_over': {k: gameover[k] for k in ('AutobotWinSound', 'DecepticonWinSound') if k in gameover},
             'announcer': {'team0_dialog_character': annc.get('Team0DialogCharacter'), 'team1_dialog_character': annc.get('Team1DialogCharacter')},
             'progress_rules': rules, 'mode_messages': mode_messages, 'objective_messages': objective, 'countdown': countdown,
             'killstreaks': killstreaks})
print('match messages: %d game types, %d progress sounds, %d music cues' % (len(gametypes), len(progress), len(match_music)))

# Every processed multiplayer map (ExtractedAssets/VerticalSlice/Maps/MP_*/audio.json): the full Kismet audio graph of
# all its sublevels (touch volumes from the AssetTools zones, zones, reverb, mixer, pools, flybys, beds, delays, gates,
# remote events, sub-sequences), the per-cue-asset limits, and the announcer set of its persistent level's TnWorldInfo.
MAPS_DIR = 'F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps/'
# The MP transforming world actors (doors / bridges): their AnimSets' HmAnimNotify_Sound cues play on every client as the transform
# anim runs [RE MP_TRANSFORMING_ACTOR_CUES_RE], so each map that places one carries those cues too.
TA_MANIFEST = 'F:/Transformers Rebuild/AssetTools/manifests/maps/mp_transforming_actors.json'
ta_sets_by_map = {}
for a in json.load(io.open(TA_MANIFEST, encoding='utf-8'))['actors']:
    if a.get('animset'): ta_sets_by_map.setdefault(a['map'], set()).add((a['animset'], a.get('anim_sequence') or ''))
def actor_notify_cues(sets):   # {(animset, the sequence the actor plays)}
    out = set()
    for s, name in sets:
        for (pr,) in c.execute("select props from objects where class='AnimSequence' and opath like ? escape '\\'",
                               (s.replace('_', '\\_') + '.%',)):
            seq = json.loads(pr or '{}')
            if seq.get('SequenceName') != name: continue
            for n in seq.get('Notifies') or []:
                r = c.execute('select class, props from objects where opath=?', (n.get('Notify') or '',)).fetchone()
                if r and r[0] == 'HmAnimNotify_Sound':
                    q = json.loads(r[1] or '{}').get('SoundCue')
                    if q: out.add(q)
    return out
announcer_sets = {}
for level_name in sorted(d for d in os.listdir(MAPS_DIR) if d.startswith('MP_') and os.path.exists(MAPS_DIR + d + '/audio.json')):
    am = json.load(io.open(MAPS_DIR + level_name + '/audio.json', encoding='utf-8'))
    subs = am['sublevels']
    base = [x for x in subs if 'base' in x.lower()] or subs[:1]
    geo = {z['event']: {'polygons': z['trigger_polygons_gltf'], 'trigger': z.get('trigger', {}), 'comment': z.get('comment', '')}
           for z in am['zones'] if z.get('trigger_polygons_gltf')}
    audio_pkg = [x for x in subs if 'audio' in x.lower()]
    doc = level(level_name, subs, geo, frozenset(am['cues']), audio_pkg[0] if audio_pkg else None)
    ta = sorted(q for q in actor_notify_cues(ta_sets_by_map.get(level_name, ())) if q not in am['cues'] and q not in doc['cues'])
    for q in ta:
        d = cue_tree(q)
        if d: doc['cues'][q] = d; doc['cue_limits'][q] = cue_limits(q)
        else: print('  WARN', level_name, 'transforming-actor cue not found', q)
    if ta: print('%s: + %d transforming-actor notify cues' % (level_name, len(ta)))
    wi = c.execute("select props from objects where package=? and class='TnWorldInfo'", (base[0],)).fetchone()
    setname = json.loads(wi[0]).get('AnnouncerSoundEventSet') if wi else None
    if setname:
        if setname not in announcer_sets:
            es = json.loads(c.execute("select props from objects where opath=?", (setname,)).fetchone()[0])['SoundEventSet']
            announcer_sets[setname] = {e['Event']: e['Sound'] for e in es if e.get('Sound')}
        doc['announcer'] = {'event_set': setname}
    docs.append(doc)
# The announcer sets and the match cues (dialogue + mode music), shared by every map; streamed (decoded on first play).
match_doc = [d for d in docs if d['map'] == '__match_messages__'][0]
match_doc['announcer_sets'] = announcer_sets
mc = {}
for q in sorted({v for ev in announcer_sets.values() for v in ev.values()} | set(match_music) | set(stingers)):
    d = cue_tree(q)
    if d: d['streamed'] = True; mc[q] = d
    else: print('  WARN missing match cue', q)
match_doc['cues'] = mc
print('shared: %d announcer sets, %d streamed match cues' % (len(announcer_sets), len(mc)))

out = ['// Generated by tools/systems/gen_level_audio.py from AssetTools authored.db - do not edit by hand.\n',
       '// Systems level-audio manifests (see LevelAudio.h); one JSON document per level.\n',
       'const LevelAudioManifest kLevelAudioManifests[] = {\n']
for d in docs:
    s = json.dumps(d, separators=(',', ':'), sort_keys=False)
    assert ')WFCJSON"' not in s
    # split into <= 8000-char raw string pieces (adjacent literals concatenate)
    pieces = [s[i:i + 8000] for i in range(0, len(s), 8000)]
    out.append('    {"%s",\n' % d['map'] + ''.join('     R"WFCJSON(%s)WFCJSON"\n' % p for p in pieces) + '    },\n')
out.append('};\n')
io.open(sys.argv[1], 'w', encoding='utf-8', newline='\n').write(''.join(out))
