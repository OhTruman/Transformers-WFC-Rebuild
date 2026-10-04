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
    for en in rp.get('ChildNodes', []):
        ecls, ep, _ = row(en)
        waves = []
        for w in ep.get('ChildNodes', []):
            pk, nm = w.split('.', 1)
            wav = 'content/%s/%s.wav' % (pk, nm)
            if not os.path.exists(CONTENT + wav[8:]): print('  MISSING wave', wav)
            waves.append({'node': w, 'class': 'SoundNodeWaveEx', 'wav': wav})
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

def level(pkg):
    objs = {r[0]: (r[1], json.loads(r[2])) for r in c.execute('select opath, class, props from objects where package=?', (pkg,))}
    sid = lambda o: o.split('Main_Sequence.', 1)[1] if 'Main_Sequence.' in o else o.split('.')[-1]
    incoming = {}
    for o, (cl, p) in objs.items():
        for ol in p.get('OutputLinks') or []:
            for l in ol.get('Links') or []:
                if l.get('LinkedOp'): incoming.setdefault(l['LinkedOp'], []).append((o, ol.get('LinkDesc'), l.get('InputLinkIdx', 0)))
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
        if a == '<player>': return None
        cl, p = objs.get(a, (None, {}))
        if 'Location' in p: actors[a.split('.')[-1]] = gltf(p['Location'])
        else: warnings.append('actor without location ' + a)
        return a.split('.')[-1]

    # Timelines: SeqAct_Interp ops with event-track outputs that lead to audio (directly or into a sub-sequence).
    def is_audio_target(t):
        cl = objs.get(t, (None,))[0]
        if cl in AUDIO_OPS: return True
        if cl == 'Sequence': return any(o.startswith(t + '.') and objs[o][0] in AUDIO_OPS for o in objs)
        return False
    timelines = {}
    for o, (cl, p) in objs.items():
        if cl != 'SeqAct_Interp': continue
        if not any(ol.get('LinkDesc') not in NOT_EVENTS and any(is_audio_target(l.get('LinkedOp')) for l in ol.get('Links') or [])
                   for ol in p.get('OutputLinks') or []): continue
        data = [v for vl in p.get('VariableLinks') or [] if vl.get('LinkDesc') == 'Data' for v in vl.get('LinkedVariables') or []]
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

    def triggers(op, inp, depth=0):
        out = []
        for src, desc, idx in incoming.get(op, []):
            if idx != inp: continue
            cl, p = objs.get(src, (None, {}))
            if cl == 'SeqEvent_GameplayStarted': out.append('GameplayStarted')
            elif cl == 'SeqEvent_LevelLoaded': out.append('LevelLoaded')
            elif cl == 'GFxEvent_FsCommand': out.append('FsCommand:' + p.get('FsCommand', ''))
            elif cl == 'SeqAct_MoviePlayer' and desc == 'Stopped': out.append('MovieStopped:' + p.get('MovieName', ''))
            elif cl == 'SeqAct_Interp' and src in timelines: out.append('Timeline:%s:%s' % (sid(src), desc))
            elif cl == 'SeqEvent_SequenceActivated' and depth < 8:
                seq = src.rsplit('.', 1)[0]
                for i, il in enumerate(objs[seq][1].get('InputLinks') or []):
                    if il.get('LinkedOp') == src: out += triggers(seq, i, depth + 1)
            else:
                out.append('Unresolved:%s:%s' % (cl, desc)); warnings.append('unresolved trigger %s [%s] -> %s' % (sid(src), desc, sid(op)))
        return out

    ops, links, cues, presets = [], [], set(), {}
    for o, (cl, p) in sorted(objs.items()):
        if cl not in AUDIO_OPS: continue
        op = {'id': sid(o)}
        if cl == 'SeqAct_PlaySound':
            q = dict(D_PLAYSOUND); q.update(p)
            op.update(type='play_sound', cue=q.get('PlaySound'), fade_in=q.get('FadeInTime', 0.0), fade_out=q.get('FadeOutTime', 0.0),
                      volume=q.get('VolumeMultiplier', 1.0), pitch=q.get('PitchMultiplier', 1.0),
                      suppress_spatialization=bool(q.get('bSuppressSpatialization', False)),
                      targets=[t for t in (actor(a) for a in var_objects(p, 'Target')) if t])
            cues.add(op['cue'])
        elif cl == 'SeqAct_PlayPlayerPositionalSound':
            q = dict(D_PP); q.update(p)
            src = [s for s in (actor(a) for a in var_objects(p, 'Source Actor')) if s]
            op.update(type='positional_pool', cue=q.get('SoundToPlay'), delay_min=q['DelayMin'], delay_max=q['DelayMax'],
                      distance_min=q['DistanceMin'], distance_max=q['DistanceMax'], looping=bool(q['Looping']),
                      source=src[0] if src else None)
            cues.add(op['cue'])
        elif cl == 'SeqAct_Reverb':
            name = objs.get(p.get('ReverbMixerPreset'), (None, {}))[1].get('PresetName') or row(p.get('ReverbMixerPreset') or '')[1].get('PresetName')
            op.update(type='reverb', preset=name)
            presets[name] = reverb_preset(name)
        elif cl == 'SeqAct_PlayMusic':
            t = dict(D_MUSIC); t.update(p.get('MusicTrack') or {})
            op.update(type='play_music', cue=t['SoundCue'], fade_in=t['FadeInTime'], fade_out=t['FadeOutTime'],
                      boredom=t['BoredomTime'], priority=t['Priority'],
                      ignore_spaz_timer=bool(p.get('IgnoreSpazTimer', False)),
                      fade_out_override=p.get('CurrentTrackFadeOutTimeOverride', -1.0) if p.get('UseCurrentTrackFadeOutTimeOverride') else -1.0)
        elif cl == 'SeqAct_StopMusic':
            op.update(type='stop_music', fade_out_override=p.get('FadeTimeOverride', -1.0) if p.get('UseFadeTimeOverride') else -1.0)
        ops.append(op)
        for inp in (0, 1):
            for t in triggers(o, inp): links.append({'from': t, 'to': op['id'], 'input': inp})
    for o, tl in sorted(timelines.items()):
        ops.append(tl)
        for t in triggers(o, 0): links.append({'from': t, 'to': tl['id'], 'input': 0})
    cue_defs = {}
    for q in sorted(x for x in cues if x):
        d = cue_tree(q, pkg)
        if d: cue_defs[q] = d
        else: warnings.append('cue not found ' + q)
    doc = {'map': pkg, 'source': 'Systems tools/systems/gen_level_audio.py from AssetTools authored.db (%s Kismet)' % pkg,
           'cues': cue_defs, 'cue_limits': {q: {k: v for k, v in d.items() if k in ('MaxConcurrentPlayCount', 'InstanceLimiting')} for q, d in cue_defs.items()},
           'reverb_presets': {k: v for k, v in presets.items() if v},
           'kismet': {'actors': actors, 'ops': ops, 'links': links}}
    for w in warnings: print('  WARN', pkg, w)
    print('%s: %d ops (%d timelines), %d links, %d cues, %d reverb presets, %d actors' % (
        pkg, len(ops), len(timelines), len(links), len(cue_defs), len(doc['reverb_presets']), len(actors)))
    return doc

docs = [level(p) for p in UI_LEVELS]
# Multiplayer maps: per-cue-asset limits of the map bank (AssetTools audio.json cues; the cue objects of the audio
# sublevel) and the map's announcer (TnWorldInfo.AnnouncerSoundEventSet -> HmSoundEventSet event -> dialogue cue) with
# the cues of every announcer event and of every game-type message's music, all streamed (decoded on first play).
MP_MAPS = [('MP_IAC_Streets', 'MP_IAC_Streets_AUDIO_m', 'MP_IAC_Streets_BASE_m'),
           ('MP_UND_Gorge', 'MP_UND_Gorge_AUDIO_m', 'MP_UND_Gorge_BASE_m')]
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
match_music = sorted({v for g in gametypes.values() for k, v in g.items() if k.endswith('Music')})
docs.append({'map': '__match_messages__',
             'source': 'Systems gen_level_audio.py: TransGame / TransContent message class defaults (authored.db)',
             'game_type_messages': gametypes, 'progress_announcement_sounds': progress,
             'versus_game_over': {k: gameover[k] for k in ('AutobotWinSound', 'DecepticonWinSound') if k in gameover},
             'announcer': {'team0_dialog_character': annc.get('Team0DialogCharacter'), 'team1_dialog_character': annc.get('Team1DialogCharacter')},
             'progress_rules': rules})
print('match messages: %d game types, %d progress sounds, %d music cues' % (len(gametypes), len(progress), len(match_music)))

for level, audio_pkg, base_pkg in MP_MAPS:
    path = 'F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps/%s/audio.json' % level
    bank = json.load(io.open(path, encoding='utf-8'))['cues'] if os.path.exists(path) else {}
    lim = {q: cue_limits(q, audio_pkg) for q in sorted(bank)}
    doc = {'map': level, 'source': 'Systems tools/systems/gen_level_audio.py: per-cue-asset limits of the map bank and the '
           'map announcer (authored.db); emitters / zones / pools / reverb / bank are the AssetTools audio.json',
           'cue_limits': lim}
    wi = c.execute("select props from objects where package=? and class='TnWorldInfo'", (base_pkg,)).fetchone()
    setname = json.loads(wi[0]).get('AnnouncerSoundEventSet') if wi else None
    cues = {}
    if setname:
        es = json.loads(c.execute("select props from objects where opath=?", (setname,)).fetchone()[0])['SoundEventSet']
        events = {e['Event']: e['Sound'] for e in es if e.get('Sound')}
        doc['announcer'] = {'event_set': setname, 'events': events}
        for q in sorted(set(events.values()) | set(match_music)):
            d = cue_tree(q)
            if d: d['streamed'] = True; cues[q] = d
            else: print('  WARN missing cue', q)
        doc['cues'] = cues
    docs.append(doc)
    print('%s: %d cue limits (%d non-default), announcer %s (%d events), %d streamed match cues' % (
        level, len(lim), sum(1 for v in lim.values() if v != {'MaxConcurrentPlayCount': 5, 'InstanceLimiting': 'kKillFarthest'}),
        setname, len(doc.get('announcer', {}).get('events', {})), len(cues)))

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
