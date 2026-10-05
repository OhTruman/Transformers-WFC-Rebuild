"""Generate src/game/CharacterAudio.inc: the per-character audio profiles (one JSON document) from AssetTools
mp_content/roster_package.json + mp_weapons.json and the cooked objects in authored.db (read-only).

Per chassis (roster key): display name, faction, the robot SoundEventSet (CHR_*: FS_DEFAULT_* footsteps / jump / land,
impacts, BF_ body, warcry, death react ...), the vehicle SoundEventSet (Auto_* engine / boost / jump / land / ram ...),
the vehicle death sound, the iconic weapon loadout, and the sound-bearing animation notifies of every clip in the robot
anim sets (later sets override earlier ones for the same clip name, UE3 FindAnimSequence order [HIGH]):
  AnimNotify_Footstep (Type -> HmFootstepComponent slot -> FS_DEFAULT_* event), HmAnimNotify_SoundEvent (event),
  HmAnimNotify_Sound (direct cue). Weapons: per weapon class, its WeaponSounds events (mp_weapons.json) and PickupSound.
The cue trees of every referenced cue are emitted once ("cues"); a character's cues load when it spawns.
Run with AssetTools/bin/py/python.exe:  python tools/systems/gen_character_audio.py <out.inc>
"""
import io, json, os, sqlite3, sys
sys.path.insert(0, os.path.dirname(__file__))

MAN = 'F:/Transformers Rebuild/AssetTools/manifests/'
c = sqlite3.connect(MAN + 'authored.db')
roster = json.load(io.open(MAN + 'mp_content/roster_package.json', encoding='utf-8'))
weapons = json.load(io.open(MAN + 'mp_content/mp_weapons.json', encoding='utf-8'))['weapons']


def props(op):
    r = c.execute('select class, props from objects where opath=?', (op,)).fetchone()
    return (r[0], json.loads(r[1])) if r else (None, {})

def event_set(name):
    if not name: return {}
    _, p = props(name)
    return {e['Event'].split('.')[-1]: e['Sound'] for e in p.get('SoundEventSet') or [] if e.get('Event') and e.get('Sound')}

FOOT = {None: 'FS_DEFAULT_WALK', 'kFootstep': 'FS_DEFAULT_WALK', 'kFootstepRun': 'FS_DEFAULT_RUN', 'kScuff': 'FS_DEFAULT_SCUFF',
        'kLand': 'FS_DEFAULT_LAND', 'kHardLand': 'FS_DEFAULT_HARD_LAND', 'kFootstepJog': 'FS_DEFAULT_JOG'}
_, D_FOOT = props('Engine.Default__AnimNotify_Footstep')
_, D_HMSND = props('HM_Engine.Default__HmAnimNotify_Sound')
_, D_HMEVT = props('HM_Engine.Default__HmAnimNotify_SoundEvent')

clip_cache = {}
def anim_set_clips(anim_set):
    if anim_set in clip_cache: return clip_cache[anim_set]
    out = {}
    for o, p in c.execute("select opath, props from objects where class='AnimSequence' and opath like ?", (anim_set + '.%',)):
        if o.count('.') != 2: continue
        d = json.loads(p)
        notes = []
        for n in d.get('Notifies') or []:
            cl, np = props(n.get('Notify') or '')
            if cl == 'AnimNotify_Footstep':
                notes.append({'t': round(n['Time'], 6), 'event': FOOT.get(np.get('Type', D_FOOT.get('Type')), 'FS_DEFAULT_WALK'),
                              'min_weight': np.get('MinWeight', D_FOOT.get('MinWeight', 0.25)), 'kind': 'footstep'})
            elif cl in ('HmAnimNotify_SoundEvent', 'HmAnimNotify_ConditionalSoundEvent'):
                ev = np.get('SoundEvent')
                if ev: notes.append({'t': round(n['Time'], 6), 'event': ev.split('.')[-1], 'min_weight': np.get('MinWeight', D_HMEVT.get('MinWeight', 0.25)),
                                     'kind': 'sound_event'})
            elif cl in ('HmAnimNotify_Sound', 'AnimNotify_Sound', 'HmAnimNotify_ConditionalSound'):
                cue = np.get('SoundCue') or np.get('Sound')
                if cue: notes.append({'t': round(n['Time'], 6), 'cue': cue, 'min_weight': np.get('MinWeight', D_HMSND.get('MinWeight', 0.25)),
                                      'socket': np.get('SocketName'), 'kind': 'sound'})
        if notes:
            out[d.get('SequenceName')] = {'length': d.get('SequenceLength', 0.0), 'rate': d.get('RateScale', 1.0), 'notifies': notes}
    clip_cache[anim_set] = out
    return out

profiles, all_cues = {}, set()
for key, ch in roster['chassis'].items():
    au = ch.get('audio') or {}
    voice = event_set((au.get('voice') or {}).get('object'))
    vehicle = event_set((au.get('vehicle') or {}).get('object'))
    clips = {}
    for s in [x for x in ((ch.get('robot') or {}).get('anim_sets') or []) if x]:
        clips.update(anim_set_clips(s))                   # later sets override earlier ones
    loadout = (ch.get('weapons') or {}).get('iconic_loadout') or {}
    disp = ch.get('display') or {}
    name = ((disp.get('iconic') or disp.get('custom_body') or {}).get('INT')) if isinstance(disp, dict) else disp
    prof = {'key': key, 'name': name, 'faction': ch.get('faction'),
            'voice_set': (au.get('voice') or {}).get('object'), 'vehicle_set': (au.get('vehicle') or {}).get('object'),
            'voice': voice, 'vehicle': vehicle, 'vehicle_death_sound': au.get('vehicle_death_sound'),
            'clips': clips, 'weapons': sorted(set(loadout.values()))}
    profiles[key] = prof
    all_cues |= set(voice.values()) | set(vehicle.values())
    if prof['vehicle_death_sound']: all_cues.add(prof['vehicle_death_sound'])
    for cl in clips.values():
        for n in cl['notifies']:
            if n.get('cue'): all_cues.add(n['cue'])
# Hit effects (TnHitEffectPlayer.PlayEffect / FindEffect [CONF script]): the victim's blueprint (every MP Transformer:
# TR_HitEffectPlayer_p.SharedHitEffectPlayer; OmegaSupreme_HitEffect only on Omega) maps the hit's DamageType - exact
# match first, then the first entry it ClassIsChildOf - to HitSound / BlockSound sound EVENTS played on the victim
# (its own SoundEventSet), retriggered at most every RetriggerTime per entry; only if the damage type bCausesBlood
# (TnPawn.ShouldPlayHitEffect).
_, HEP = props('TR_HitEffectPlayer_p.SharedHitEffectPlayer')
HIT_EFFECTS = HEP.get('Effects') or []
def type_super(t):
    r = c.execute('select super from types where path=?', (t,)).fetchone()
    return r[0] if r else None
def find_effect(dt):
    for i, e in enumerate(HIT_EFFECTS):
        if e.get('DamageType') == dt: return i
    chain, t = [], dt
    while t:
        chain.append(t); t = type_super(t)
    for i, e in enumerate(HIT_EFFECTS):
        if e.get('DamageType') in chain: return i
    return -1
def causes_blood(dt):
    t = dt
    while t:
        _, d = props(t.replace('.', '.Default__', 1))
        if 'bCausesBlood' in d: return bool(d['bCausesBlood'])
        t = type_super(t)
    return False
# Weapon-mesh animation sounds (HmAnimNotify_Sound on the weapon's own AnimSet sequences) [CONF data]: the WEPMESH's
# WeaponEventAnims (WP_Fire / WP_Reload / WP_Equip / WP_PutDown -> sequence) and IdleAnimation (<Seq>Group -> <Seq>,
# HIGH: the name rule; CONF for the Ion Blaster's hand-checked table). AnimSets via AnimatedMesh -> Mesh -> AnimSets.
def weapon_anims(wepmesh):
    if not wepmesh: return {}
    _, wm = props(wepmesh)
    _, am = props(wm.get('AnimatedMesh') or '')
    _, sk = props(am.get('Mesh') or '')
    seqs = {}
    for aset in [x for x in (sk.get('AnimSets') or []) if x]:
        for o, pr in c.execute("select opath, props from objects where class='AnimSequence' and opath like ?", (aset + '.%',)):
            if o.count('.') != 2: continue
            d = json.loads(pr)
            snd = []
            for n in d.get('Notifies') or []:
                cl, np = props(n.get('Notify') or '')
                if cl in ('HmAnimNotify_Sound', 'AnimNotify_Sound', 'HmAnimNotify_ConditionalSound'):
                    q = np.get('SoundCue') or np.get('Sound')
                    if q: snd.append([round(n['Time'], 6), q])
            seqs.setdefault(d.get('SequenceName'), {'length': round(d.get('SequenceLength', 0.0), 6), 'sounds': snd})
    out = {}
    for e in wm.get('WeaponEventAnims') or []:
        sq = seqs.get(e.get('AnimName'))
        if sq: out[e['WeaponEventType']] = dict(sq, clip=e['AnimName'])
    idle = (wm.get('IdleAnimation') or '')
    if idle.endswith('Group') and seqs.get(idle[:-5]): out['Idle'] = dict(seqs[idle[:-5]], clip=idle[:-5])
    return out

wpn = {}
for cls, w in weapons.items():
    snd = w.get('sounds') or {}
    ev = {e['WeaponEventType']: e['WeaponSound'] for e in (snd.get('events') or []) if e.get('WeaponSound')}
    # HmWeaponMesh.DefaultImpactSound (CreateImpactEffects -> GetImpactSound fallback) and BulletBySound (the
    # victim's NotifyNearlyShot) of the weapon's WEPMESH [CONF data + script].
    if snd.get('impact'): ev['DefaultImpactSound'] = snd['impact']
    if snd.get('bullet_by'): ev['BulletBySound'] = snd['bullet_by']
    _, dp = props(cls.replace('TransContent.', 'TransContent.Default__').replace('TransGame.', 'TransGame.Default__'))
    dts = list((w.get('damage_types') or {}).keys())
    hit = None
    if dts:
        i = find_effect(dts[0])
        if i >= 0:
            e = HIT_EFFECTS[i]
            hit = {'damage_type': dts[0], 'index': i, 'hit_event': (e.get('HitSound') or '').split('.')[-1],
                   'block_event': (e.get('BlockSound') or '').split('.')[-1], 'retrigger': e.get('RetriggerTime', 0.0),
                   'causes_blood': causes_blood(dts[0])}
    wpn[cls] = {'events': ev, 'pickup_sound': dp.get('PickupSound'), 'damage_types': dts, 'hit_effect': hit,
                'anims': weapon_anims((w.get('mesh') or {}).get('weapon_mesh_template'))}
    for an in wpn[cls]['anims'].values():
        for t, q in an['sounds']: all_cues.add(q)
    all_cues |= set(ev.values())
    if dp.get('PickupSound'): all_cues.add(dp['PickupSound'])

# cue trees (same format as the level manifests)
import importlib
sys.argv_saved = sys.argv
gl = {}
src = io.open(os.path.join(os.path.dirname(__file__), 'gen_level_audio.py'), encoding='utf-8').read()
lib = src[:src.index('SINK_OPS =')]                       # the helper section (db, cue_tree, cue_limits ...)
exec(compile(lib, 'gen_level_audio_helpers', 'exec'), gl)
cues = {}
missing = []
for q in sorted(x for x in all_cues if x):
    d = gl['cue_tree'](q)
    if d: cues[q] = d
    else: missing.append(q)
doc = {'map': '__characters__', 'source': 'Systems tools/systems/gen_character_audio.py (AssetTools roster_package / mp_weapons / authored.db)',
       'profiles': profiles, 'weapons': wpn, 'cues': cues}
print('profiles %d, weapons %d, cues %d (missing %d: %s)' % (len(profiles), len(wpn), len(cues), len(missing), missing[:6]))
print('hit effects: %d entries; weapons with an effect %d / %d; no effect: %s' % (len(HIT_EFFECTS), sum(1 for w in wpn.values() if w['hit_effect']),
      len(wpn), sorted(k.split('.')[-1] for k, w in wpn.items() if not w['hit_effect'])))
for k in ('TransContent.TnWeaponIonBlaster', 'TransContent.TnWeaponHeavyPistol', 'TransContent.TnWeaponShortSword'):
    if k in wpn: print('  ', k.split('.')[-1], wpn[k]['events'].get('DefaultImpactSound'), wpn[k]['hit_effect'])
print('weapon anims: %d / %d weapons; Ion Blaster %s' % (sum(1 for w in wpn.values() if w['anims']), len(wpn),
      {k: (v['clip'], v['length'], v['sounds']) for k, v in wpn['TransContent.TnWeaponIonBlaster']['anims'].items()}))
for k in ('Truck', 'Car', 'Tank', 'Jet'):
    p = profiles.get(k)
    if p: print('  %-6s %-20s voice %3d vehicle %2d clips %3d weapons %s death %s' % (k, p['name'], len(p['voice']), len(p['vehicle']),
                                                                                   len(p['clips']), p['weapons'], p['vehicle_death_sound']))
s = json.dumps(doc, separators=(',', ':'))
assert ')WFCJSON"' not in s
pieces = [s[i:i + 8000] for i in range(0, len(s), 8000)]
out = ['// Generated by tools/systems/gen_character_audio.py - do not edit by hand.\n',
       'const char* const kCharacterAudioJson =\n'] + ['    R"WFCJSON(%s)WFCJSON"\n' % p for p in pieces] + [';\n']
io.open(sys.argv[1], 'w', encoding='utf-8', newline='\n').write(''.join(out))
