"""Matinee float-property tracks (InterpTrackFloatProp) of a map family's levels -> <out>/matinee_floatprops.json.

    python build_scene_floatprops.py <Map> <out dir>

The frontend scene exports (AssetTools ui_scenes.json, Frontend data/frontend/scenes.json) list these tracks without
their keys. Every track is written with its level, InterpData, InterpGroup name, PropertyName (FOVAngle on the camera
groups, DrawScale on the vignette ships / emitters, ...) and the FInterpCurveFloat points as cooked:
InVal (time, s), OutVal, ArriveTangent, LeaveTangent, InterpMode (CIM_Linear / CIM_CurveAuto / CIM_Constant /
CIM_CurveUser / CIM_CurveBreak / CIM_CurveAutoClamped). Evaluate with render::evalInterpCurveFloat (Renderer.h).
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue3obj import Repo, map_packages  # noqa: E402


def props(x):
    """Tagged-property list ([{name, value}, ...]) or dict -> dict."""
    if isinstance(x, dict):
        return x
    if isinstance(x, list) and all(isinstance(e, dict) and 'name' in e for e in x):
        return {e['name']: e.get('value') for e in x}
    return {}


def points(track):
    ft = props(track.get('FloatTrack'))
    pts = ft.get('Points') or []
    out = []
    for k in pts:
        k = props(k)
        mode = k.get('InterpMode', 'CIM_Linear')
        out.append({'t': float(k.get('InVal', 0.0)), 'v': float(k.get('OutVal', 0.0)),
                    'arrive': float(k.get('ArriveTangent', 0.0)), 'leave': float(k.get('LeaveTangent', 0.0)),
                    'mode': mode if isinstance(mode, str) else str(mode)})
    return out


def main():
    mapname, out = sys.argv[1], sys.argv[2]
    pkgs = map_packages(mapname)[0]
    tracks = []
    for pk in pkgs:
        r = Repo([pk], fallback=['TransGame.xxx'])
        p = r.pkgs[0]
        level = pk.rsplit('.', 1)[0]
        # SeqAct_Interp -> InterpData (VariableLinks "Data" -> LinkedVariables): the frontend keys matinees by action
        owner = {}
        for i, e in enumerate(p.exports):
            if p.class_name(e) != 'SeqAct_Interp':
                continue
            act = p.object_path(i + 1)
            o = r.obj(act) or {}
            for vl in o.get('VariableLinks') or []:
                vl = props(vl)
                if vl.get('LinkDesc') != 'Data':
                    continue
                for ref in vl.get('LinkedVariables') or []:
                    rr = ref.get('ref') if isinstance(ref, dict) else ref
                    if rr:
                        owner[rr.split('.')[-1]] = (act.split('.')[-1], o.get('ObjComment'))
        for i, e in enumerate(p.exports):
            if p.class_name(e) != 'InterpTrackFloatProp':
                continue
            path = p.object_path(i + 1)
            o = r.obj(path) or {}
            parts = path.split('.')
            group = props(r.obj('.'.join(parts[:-1])) or {}).get('GroupName')
            interp = next((s for s in reversed(parts) if s.startswith('InterpData')), None)
            act, comment = owner.get(interp, (None, None))
            tracks.append({'level': level, 'seqact_interp': act, 'matinee_comment': comment, 'interp_data': interp,
                           'group': group, 'property': o.get('PropertyName'),
                           'track': parts[-1], 'keys': points(o)})
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'matinee_floatprops.json'), 'w', encoding='utf-8') as f:
        json.dump({'generated_by': 'tools/render/build_scene_floatprops.py', 'map': mapname, 'tracks': tracks}, f, indent=1)
    by = {}
    for t in tracks:
        by[t['property']] = by.get(t['property'], 0) + 1
    print('matinee float props: %d tracks %s' % (len(tracks), by))


if __name__ == '__main__':
    main()
