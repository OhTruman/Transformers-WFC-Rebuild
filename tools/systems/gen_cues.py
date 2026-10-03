"""Generate the C++ SoundCue table (src/game/SoundCues.inc) from the cooked cues.

Reads (read-only) via AssetTools objtree/typed_props. Run with AssetTools/bin/py/python.exe:
    python tools/systems/gen_cues.py <out.inc>
Cue defaults come from HM_Engine.Default__SoundNodeRoot (Volume -6, Distance 400..6400, Rolloff 1).
"""
import io, json, os, sys
sys.path.insert(0, r'F:/Transformers Rebuild/AssetTools/scripts/wfc')
import objtree, typed_props as tp

CONTENT = 'F:/Transformers Rebuild/ExtractedAssets/content/'
# (package holding cooked copies, cue paths)
CUES = [
    ('MP_IAC_Streets_BASE_m', ['BL_WPN_GUN_ION_BLASTER.SHOOT', 'BL_WPN_GUN_ION_BLASTER.SHOOT_LOW_AMMO',
                               'BL_WPN_GUN_ION_BLASTER.SHOOT_TAIL', 'BL_WPN_GUN_ION_BLASTER.ANIM_RELOAD_01',
                               'BL_WPN_GUN_ION_BLASTER.ANIM_RELOAD_02', 'BL_WPN_GUN_ION_BLASTER.IDLE_01',
                               'BL_WPN_GUN_ION_BLASTER.IDLE_02', 'BL_WPN_GUN_ION_BLASTER.IMPT_WORLD',
                               'BL_WPN_GUN_ION_BLASTER.IMPT_DMG', 'BL_WPN_FOLEY.SHOOT_DRY_FIRE_ELECTRICITY']),
    ('A1_IAC_Base_m', ['BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_BOOST_START', 'BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_BOOST_LOOP',
                       'BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_BOOST_END', 'BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_BOOST_WHEELS',
                       'BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_RAM_NITRO_START', 'BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_RAM_BOOST_START',
                       'BL_VEH_SOUNDWAVE.VEH_TRUCK_RAM_ALERT', 'BL_VEH_SOUNDWAVE.VEH_TRUCK_RAM_IMPACT',
                       'BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_DRIVE_ONLOAD', 'BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_DRIVE_OFFLOAD',
                       'BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_DRIVE_JUMP_START', 'BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_DRIVE_JUMP_LOOP',
                       'BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_HOVER_LAND_LIGHT', 'BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_HOVER_LAND_HEAVY',
                       'BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_WHEELS_LAND_LIGHT', 'BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_WHEELS_LAND_HEAVY',
                       'BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_TIRE_SQUEAL']),
    # Robot movement (SoundEvents.CHR_OPTIMUS footstep events), transformation (HmAnimNotify_Sound on the
    # Optimus transform clips), Optimus idle foley (NAV_Idle notify) and fine aim (TnWeaponIonBlaster
    # WP_StartFineAim / WP_EndFineAim). These keep their package prefix in the table.
    ('MP_IAC_Streets_BASE_m', ['BL_FS_LRG_BOT.FS_WALK_DEFAULT', 'BL_FS_LRG_BOT.FS_RUN_DEFAULT',
                               'BL_FS_LRG_BOT.FS_SCUFF_DEFAULT', 'BL_FS_LRG_BOT.FS_JUMP',
                               'BL_FS_LRG_BOT.FS_JUMP_CHARGED', 'BL_FS_LRG_BOT.FS_LAND_DEFAULT',
                               'BL_FS_LRG_BOT.FS_LAND_HARD', 'BL_FS_LRG_BOT.FS_LAND_HIGH_FALL',
                               'BL_FS_LRG_BOT.FOLEY_FS_GROAN_SERVO_01', 'BL_TRANSFORM.OPTIMUS_BOT2VEH',
                               'BL_TRANSFORM.OPTIMUS_VEH2BOT', 'BL_FOLY_IDLES.OPTIMUS_IDLE',
                               'BL_WPN_GUN_PULSE_RIFLE.FINE_AIM_START', 'BL_WPN_GUN_PULSE_RIFLE.FINE_AIM_END']),
    # Pickup sounds: the PickupSound of the Streets pickup inventory classes (AssetTools 7a69756
    # streets_pickup_factories.json: TnAmmoCratePickup / TnHealthPickup / TnOverShieldPickup).
    ('A1_IAC_Base_m', ['BL_HUD_INTERFACE.HEALTH_PU_AMMO', 'BL_HUD_INTERFACE.HEALTH_PU_ENERGON',
                       'BL_HUD_INTERFACE.OVERSHIELD_POWER_UP']),
]
ROOTDEF = {'Volume': -6.0, 'DistanceMin': 400.0, 'DistanceMax': 6400.0, 'RolloffFactor': 1.0, 'Pitch': 0.0,
           'SmartPanDistance2D': 400.0, 'SmartPanDistance3D': 800.0, 'SmartPanAttenuation3D': 0.0, 'RearAttenuation': 0.0,
           'VolumeVariationMin': 0.0, 'VolumeVariationMax': 0.0, 'PitchVariationMin': 0.0, 'PitchVariationMax': 0.0}
PARAM = {None: 'Param::None', 'SoundParameters.SOUND_DISTANCE': 'Param::Distance',
         'SoundParameters.Optimus_Prime_Speed': 'Param::Speed',
         'SoundParameters.Optimus_Prime_Tire_Squeal': 'Param::TireSqueal'}

# HM_Engine.Default__SoundNodeWaveEvent pans Center / BackL / BackR / LFE to -96 dB (front stereo).
def stereo_gain(e):
    lin = lambda db: 10.0 ** (db / 20.0) if db > -96.0 else 0.0
    front = (lin(e.get('PanFrontLeft', 0.0)) + lin(e.get('PanFrontRight', 0.0))) * 0.5
    back = (lin(e.get('PanBackLeft', -96.0)) + lin(e.get('PanBackRight', -96.0))) * 0.5
    return min(1.0, front + 0.70710678 * back)   # stereo fold-down: surround rears at -3 dB [MED]

def f(x):
    s = '%.6g' % x
    return s + ('f' if ('.' in s or 'e' in s) else '.0f')

def curve(c):
    return '{' + ', '.join('{%s, %s}' % (f(p['X']), f(p['Y'])) for p in (c or [])) + '}'

def short(c):
    pkg, name = c.split('.', 1)
    if pkg in ('BL_FS_LRG_BOT', 'BL_TRANSFORM', 'BL_FOLY_IDLES', 'BL_WPN_GUN_PULSE_RIFLE', 'BL_HUD_INTERFACE'): return c
    return {'BL_WPN_GUN_ION_BLASTER': '', 'BL_WPN_FOLEY': 'FOLEY.', 'BL_VEH_OPTIMUS_PRIME': '', 'BL_VEH_SOUNDWAVE': ''}[pkg] + name

out, missing = [], []
for pkgname, cues in CUES:
    p = objtree.package(pkgname)
    def rd(path):
        return tp.simplify(p._tr.read_dict(p._idx[path.lower()]))
    for c in cues:
        cue = rd(c); root = rd(cue['FirstNode'])
        R = dict(ROOTDEF); R.update({k: v for k, v in root.items() if k in ROOTDEF})
        param = PARAM[root.get('SoundParameter')]
        cat = rd(root['Category']).get('CategoryName', '-') if root.get('Category') else '-'
        preset = rd(root['PlayMixerPreset']).get('PresetName', '') if root.get('PlayMixerPreset') else ''
        rows = []
        for en in root['ChildNodes']:
            e = rd(en)
            waves = []
            for w in e.get('ChildNodes', []):
                pk, nm = w.split('.', 1)
                path = pk + '/' + nm + '.wav'
                if not os.path.exists(CONTENT + path): missing.append(path)
                waves.append('"%s"' % path)
            env = e.get('Envelope') or {}
            rows.append('        {%s, %s, %s, %s, %s, %s, %s, %d, %s, %s, {%s}, %s, %s, %s, %s, %s, %s},\n' % (
                f(e.get('Time', 0.0)), f(e.get('Volume', 0.0)), f(e.get('VolumeVariationMin', 0.0)),
                f(e.get('VolumeVariationMax', 0.0)), f(e.get('Pitch', 0.0)), f(e.get('PitchVariationMin', 0.0)),
                f(e.get('PitchVariationMax', 0.0)), int(e.get('ChanceToPlayNone', 0)),
                'true' if e.get('bLooping') else 'false', f(stereo_gain(e)), ', '.join(waves),
                curve(e.get('VolumeCurve')), curve(e.get('PitchCurve')),
                curve(env.get('VolumeCurve')), curve(env.get('PitchCurve')),
                'true' if e.get('OverridePriority') else 'false', f(e.get('Priority', 0.0))))
        occl = 'true' if root.get('EnableOcclusionVolume', True) else 'false'   # Default__SoundNodeRoot: true
        # HM_Engine SoundNodeRoot.Spatialization enum: 0 k3D (class default), 1 k2D, 2 kSmartPan, 3 kSmartPan_PreferPlayer.
        spat = {'k2D': 'Spatial::TwoD', 'kSmartPan': 'Spatial::SmartPan',
                'kSmartPan_PreferPlayer': 'Spatial::SmartPanPreferPlayer'}.get(root.get('SpatializationType'), 'Spatial::ThreeD')
        # Engine.Default__SoundCue: MaxConcurrentPlayCount 5, InstanceLimiting kKillFarthest.
        limit = {'kKillOldest': 'Limit::KillOldest', 'kKillNewest': 'Limit::KillNewest'}.get(
            cue.get('InstanceLimiting'), 'Limit::KillFarthest')
        out.append(('    // %s\n    {"%s", %d, ' + limit + ', %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, "%s", "%s", %s, ' + spat + ', %s, {\n%s    }, %s, false},\n') % (
            c, short(c), cue.get('MaxConcurrentPlayCount', 5), f(R['Volume']), f(R['VolumeVariationMin']),
            f(R['VolumeVariationMax']), f(R['Pitch']), f(R['PitchVariationMin']), f(R['PitchVariationMax']),
            f(R['DistanceMin']), f(R['DistanceMax']), f(R['RolloffFactor']),
            f(R['SmartPanDistance2D']), f(R['SmartPanDistance3D']), f(R['SmartPanAttenuation3D']), f(R['RearAttenuation']),
            cat, preset, occl, param, ''.join(rows), f(root.get('Priority', 0.0))))
io.open(sys.argv[1], 'w', encoding='utf-8').write(
    '// Generated by tools/systems/gen_cues.py from the cooked cues - do not edit by hand.\n' + ''.join(out))
print('cues:', sum(len(c) for _, c in CUES), 'missing waves:', missing)
