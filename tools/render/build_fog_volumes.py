"""Fog volumes of a map (UE3 FogVolume*DensityInfo) for the runtime's fog-volume rendering.

Output: <out>/fog_volumes.json
    {"volumes": [{"info": path, "class": "FogVolumeConstantDensityInfo", "density": {...component properties...},
                  "approx_fog_light_color": [r, g, b, a], "location": [x, y, z] (UE), "shapes": [component keys]}]}
Shapes are the info's FogVolumeActors (a linked actor's primitive components, keyed as world.glb names them:
"actor:<ActorName>" plus each component's full path). Infos whose FogVolumeActors were nulled by the cooker
(shape meshes collected into StaticMeshCollectionActors) have no shapes: the shipped exe renders nothing for those
(Rendering work/ghidra/fogvolume_findings.md), and the runtime never draws their materials as surfaces.

    python build_fog_volumes.py MP_IAC_Rust <out_dir>
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue3obj import Repo, map_packages  # noqa: E402

DENSITY_KEYS = ('Density', 'MaxDensity', 'SphereRadius', 'HalfspacePlane', 'PlaneDistanceFactor', 'StartDistance',
                'bEnabled', 'bAffectsTranslucency', 'bOnlyAffectsTranslucency')


def main():
    mapname, out = sys.argv[1], sys.argv[2]
    pk, _ = map_packages(mapname)
    R = Repo(list(reversed(pk)), fallback=['TransGame.xxx'])
    vols = []
    for path, (pi, ix) in sorted(R.index.items()):
        p = R.pkgs[pi]
        cls = p.class_name(p.exports[ix - 1])
        if not (cls.startswith('FogVolume') and cls.endswith('Info')): continue
        o = R.obj(path) or {}
        comp = (o.get('DensityComponent') or {}).get('ref')
        c = R.obj(comp) or {} if comp else {}
        shapes = []
        for a in c.get('FogVolumeActors') or []:
            ref = (a or {}).get('ref') if isinstance(a, dict) else None
            if not ref: continue
            shapes.append('actor:' + ref.split('.')[-1])
            ao = R.obj(ref) or {}
            for k in ('StaticMeshComponent', 'CollisionComponent'):
                r = (ao.get(k) or {}).get('ref') if isinstance(ao.get(k), dict) else None
                if r and r not in shapes: shapes.append(r)
        vols.append({'info': p.object_path(ix), 'class': cls, 'component_class': (o.get('DensityComponent') or {}).get('class'),
                     'density': {k: c[k] for k in DENSITY_KEYS if k in c},
                     'approx_fog_light_color': c.get('ApproxFogLightColor'),
                     'location': o.get('Location'), 'draw_scale': o.get('DrawScale'), 'shapes': shapes})
    os.makedirs(out, exist_ok=True)
    json.dump({'map': mapname, 'volumes': vols}, open(os.path.join(out, 'fog_volumes.json'), 'w'), indent=1)
    print('fog volumes: %d (%d with linked shapes)' % (len(vols), sum(1 for v in vols if v['shapes'])))


if __name__ == '__main__':
    main()
