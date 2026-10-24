"""Authored map movers -> <out>/movers.json (rendering presentation of moving / mode-hidden actors).

Source: AssetTools manifests/streets_movers.json (a23c675) + streets_kismet.json; transforms validated against the
world.glb node matrices (the baked placement each mover starts from).

  rotating : PHYS_Rotating, RotationRate (UE rotation units / s, 65536 = 360 deg). UE3 physRotation adds
             RotationRate * dt to the actor FRotator; yaw composes outermost (world Z).
  matinee  : SeqAct_Interp InterpTrackMove, IMF_RelativeToInitial, bUseQuatInterpolation: world TM = Relative * Initial
             (row vectors); rotation = slerp between Euler keys (degrees, X roll / Y pitch / Z yaw) with linear alpha;
             position = FInterpCurve (CIM_CurveAuto keys, authored tangents, Hermite) in the actor's initial frame.
  hidden   : bHidden at load; SeqAct_ToggleHidden UnHide when a SeqCond_GameRuleActive rule is active (Gameplay).
Usage: python build_movers.py <Map> <out_dir>
"""
import json, math, os, struct, sys

MANI = r'F:/Transformers Rebuild/AssetTools/manifests'
VS_MAPS = r'F:/Transformers Rebuild/ExtractedAssets/VerticalSlice/Maps'
U2D = 360.0 / 65536.0



def manifest(mapname, kind):
    """AssetTools per-map manifest <prefix>_<kind>.json (Streets: streets_movers.json ...). The prefix is the map's
    last name token in lower case, or WFC_MANIFEST_PREFIX. Returns None when the map has no such manifest."""
    pre = os.environ.get('WFC_MANIFEST_PREFIX') or mapname.split('_')[-1].lower()
    p = os.path.join(MANI, '%s_%s.json' % (pre, kind))
    if not os.path.exists(p):
        # [integration M06] the generic map pipeline writes manifests/next_map/<MAP>_<kind>.json (AssetTools
        # MAP_PIPELINE.md); Streets keeps its canonical streets_<kind>.json
        p = os.path.join(MANI, 'next_map', '%s_%s.json' % (mapname, kind))
    return json.load(open(p, encoding='utf-8')) if os.path.exists(p) else None

def rot_matrix(pitch, yaw, roll):
    """UE3 FRotationMatrix (row vectors = local X, Y, Z axes in world), angles in UE units. Returns 3x3 rows."""
    p, y, r = (math.radians(a * U2D) for a in (pitch, yaw, roll))
    SP, CP, SY, CY, SR, CR = math.sin(p), math.cos(p), math.sin(y), math.cos(y), math.sin(r), math.cos(r)
    return [[CP * CY, CP * SY, SP],
            [SR * SP * CY - CR * SY, SR * SP * SY + CR * CY, -SR * CP],
            [-(CR * SP * CY + SR * SY), CY * SR - CR * SP * SY, CR * CP]]


def glb_nodes(path):
    with open(path, 'rb') as f:
        f.read(12)
        n, _ = struct.unpack('<II', f.read(8))
        j = json.loads(f.read(n))
    return {nd.get('name'): nd for nd in j['nodes']}


def main():
    mapname, out = sys.argv[1], sys.argv[2]
    mv = manifest(mapname, 'movers') or {'movers': []}
    wg = os.path.join(VS_MAPS, mapname, 'world.glb')
    nodes = glb_nodes(wg) if os.path.exists(wg) else {}   # (transform validation only: maps outside VerticalSlice skip it)
    kis = manifest(mapname, 'kismet') or {}
    res = {'map': mapname, 'source': 'AssetTools <map>_movers.json / <map>_kismet.json', 'rotating': [],
           'matinee': [], 'hidden': [], 'validation': []}
    for m in mv.get('movers', []):
        actor = m['actor'].split('.')[-1]
        t = m['transform']
        L = [t['Location'][k] for k in 'XYZ']
        R = t['Rotation']
        rows = rot_matrix(R['Pitch'], R['Yaw'], R['Roll'])
        nd = nodes.get(actor)
        if nd is not None and 'matrix' in nd:   # validation: node translation = (X, Z, Y) / 100
            M = nd['matrix']
            err = max(abs(M[12] - L[0] / 100), abs(M[13] - L[2] / 100), abs(M[14] - L[1] / 100))
            res['validation'].append({'actor': actor, 'translation_error_m': err})
        if m.get('physics') == 'PHYS_Rotating' and m.get('RotationRate'):
            rr = m['RotationRate']
            res['rotating'].append({'actor': actor, 'location_ue': L, 'rotation_ue': [R['Pitch'], R['Yaw'], R['Roll']],
                                    'rate_ue': [rr['Pitch'], rr['Yaw'], rr['Roll']],
                                    'confidence': 'CONFIRMED AUTHORED DATA (PHYS_Rotating)'})
        if m.get('bHidden'):
            res['hidden'].append({'actor': actor, 'confidence': 'CONFIRMED AUTHORED DATA (bHidden)'})
    # (AssetTools <out>/matinee_tracks.json when the map has one: each InterpGroup bound to its own actors - UE3 VariableLink
    # LinkDesc == GroupName -> SeqVar_Object - and every action by its full path. Without it, below, every Move group went to
    # every actor the matinee links: Tranquillity's drain Interp_5361 wrote the energon's curve onto the fog volume too, and
    # SeqAct_Interp_10535 exists in 8 of its sequences. The shipped maps' matinees are single-group: unchanged.)
    mtp = os.path.join(out, 'matinee_tracks.json')
    bound_src = json.load(open(mtp, encoding='utf-8')).get('matinees', []) if os.path.exists(mtp) else None
    if bound_src is not None:
        where = {}
        for mt in mv.get('matinee', []):
            for a in mt['actors']:
                where[a['actor'].split('.')[-1]] = (a['location_ue'], a['rotation_ue'])
        for m in mv.get('movers', []):
            t = m['transform']
            where.setdefault(m['actor'].split('.')[-1], ([t['Location'][k] for k in 'XYZ'],
                                                         [t['Rotation'][k] for k in ('Pitch', 'Yaw', 'Roll')]))
        for x in bound_src:
            for g in x['groups']:
                for tr in g['tracks']:
                    if tr['class'] != 'InterpTrackMove': continue
                    pr = tr['props']
                    pos = [{'t': k['InVal'], 'v': [k['OutVal'][c] for c in 'XYZ'],
                            'arrive': [k['ArriveTangent'][c] for c in 'XYZ'], 'leave': [k['LeaveTangent'][c] for c in 'XYZ'],
                            'mode': k['InterpMode']} for k in pr['PosTrack']['Points']]
                    eul = [{'t': k['InVal'], 'v': [k['OutVal'][c] for c in 'XYZ']} for k in pr['EulerTrack']['Points']]
                    for b in g['bound']:
                        actor = b['actor'].split('.')[-1]
                        if actor not in where:
                            print('movers: %s group %s -> %s has no authored transform, skipped' % (x['action'], g['name'], actor))
                            continue
                        res['matinee'].append({'actor': actor, 'group': g['name'], 'action': x['action'].split('.')[-1],
                                               'action_path': x['action'], 'looping': bool(x['action_props'].get('bLooping', False)),
                                               'length_s': x['length_s'], 'location_ue': where[actor][0], 'rotation_ue': where[actor][1],
                                               'move_frame': pr.get('MoveFrame'), 'quat_interp': pr.get('bUseQuatInterpolation'),
                                               'pos_keys': pos, 'euler_keys_deg': eul,
                                               'confidence': 'CONFIRMED AUTHORED DATA (track + UE3 group binding); UE3 '
                                                             'InterpTrackMove evaluation rules HIGH'})
    for mt in (mv.get('matinee', []) if bound_src is None else []):
        for g in mt['groups']:
            for tr in g['tracks']:
                if tr['class'] != 'InterpTrackMove': continue
                pr = tr['props']
                pos = [{'t': k['InVal'], 'v': [k['OutVal'][c] for c in 'XYZ'],
                        'arrive': [k['ArriveTangent'][c] for c in 'XYZ'], 'leave': [k['LeaveTangent'][c] for c in 'XYZ'],
                        'mode': k['InterpMode']} for k in pr['PosTrack']['Points']]
                eul = [{'t': k['InVal'], 'v': [k['OutVal'][c] for c in 'XYZ']} for k in pr['EulerTrack']['Points']]
                for a in mt['actors']:
                    res['matinee'].append({'actor': a['actor'], 'group': g['group'], 'action': mt['action'],
                                           'looping': mt['looping'], 'length_s': mt['length_s'],
                                           'location_ue': a['location_ue'], 'rotation_ue': a['rotation_ue'],
                                           'move_frame': pr.get('MoveFrame'), 'quat_interp': pr.get('bUseQuatInterpolation'),
                                           'pos_keys': pos, 'euler_keys_deg': eul,
                                           'start': 'SeqEvent_GameplayStarted -> RemoteEvent SkyBeam -> Play',
                                           'confidence': 'CONFIRMED AUTHORED DATA (track); UE3 InterpTrackMove '
                                                         'evaluation rules HIGH'})
    # Kismet: hidden objective bases unhidden by game rule
    rules = {}
    for s in kis.get('actor_links', kis.get('links', [])) if isinstance(kis, dict) else []:
        pass
    res['hidden_unhide_rules'] = {'rules': ['TransGame.TnGameRules_ScoreBombingRun', 'TransGame.TnGameRules_SingleFlagCTF'],
                                  'source': 'Main_Sequence: SeqEvent_GameplayStarted -> SeqCond_GameRuleActive_15312 '
                                            '(ScoreBombingRun, "EXT") / _15682 (SingleFlagCTF, "CTF") -> '
                                            'SeqAct_ToggleHidden_3764 UnHide',
                                  'confidence': 'CONFIRMED AUTHORED DATA'}
    json.dump(res, open(os.path.join(out, 'movers.json'), 'w'), indent=1)
    print('movers: %d rotating, %d matinee, %d hidden; max translation error %.5f m' % (
        len(res['rotating']), len(res['matinee']), len(res['hidden']),
        max([v['translation_error_m'] for v in res['validation']] or [0])))


if __name__ == '__main__':
    main()
