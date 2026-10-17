"""Lens flares (RE LENS_FLARE_RE.md; AssetTools manifests/maps/lens_flares/<map>_lens_flares.json): the map's LensFlareSources ->
<out>/lens_flares.json for the runtime: per source its world position (glTF metres), source colour, template fields (the
FixedRelativeBoundingBox, ScreenPercentageMap) and every ENABLED reflection element with its distributions in the cooked lookup
layout ([MinOut, MaxOut, InBias, InScale, samples...] for curves; a value for constants) and its material. A map without a
manifest writes nothing (no flares: identical runtime).

    python build_lens_flares.py <map> <out dir>
"""
import json, os, sys

MANIFESTS = r'F:/Transformers Rebuild/AssetTools/manifests/maps/lens_flares'
DISTS = ('Scaling', 'OcclusionScaling', 'AxisScaling', 'Rotation', 'Color', 'Alpha', 'Offset', 'DistMap_Scale', 'DistMap_Color',
         'DistMap_Alpha', 'LFMaterialIndex')


def dist(d):
    if not isinstance(d, dict): return None
    if d.get('kind', '').startswith('constant') and 'curve' not in d.get('kind', ''):
        v = d.get('value')
        return {'const': v if isinstance(v, list) else [v]}
    return {'table': d.get('raw') or []}


def main():
    mapname, out = sys.argv[1], sys.argv[2]
    src = os.path.join(MANIFESTS, mapname + '_lens_flares.json')
    if not os.path.exists(src):
        print(mapname, 'no lens flares'); return
    m = json.load(open(src, encoding='utf-8'))
    flares = []
    for lf in m['lens_flares']:
        loc = lf['location_ue']
        tp = lf.get('template_props') or {}
        box = tp.get('FixedRelativeBoundingBox') or {}
        els = []
        for e in lf.get('reflections') or []:
            if not e.get('bIsEnabled', True): continue
            el = {'name': e.get('ElementName'), 'ray_distance': e.get('RayDistance', 0.0),
                  'size': [e['Size'].get('X', 1.0), e['Size'].get('Y', 1.0), e['Size'].get('Z', 0.0)] if isinstance(e.get('Size'), dict) else [1, 1, 0],
                  'normalize_radial': bool(e.get('bNormalizeRadialDistance', True)),
                  'modulate_by_source': bool(e.get('bModulateColorBySource', False)),
                  'use_source_distance': bool(e.get('bUseSourceDistance', False)),
                  'materials': e.get('LFMaterials') or []}
            for k in DISTS: el[k] = dist(e.get(k))
            els.append(el)
        sc = (lf.get('component_props') or {}).get('SourceColor') or {'R': 1, 'G': 1, 'B': 1, 'A': 1}
        flares.append({'actor': lf['actor'].split('.')[-1], 'template': lf.get('template'),
                       'position_m': [loc['X'] * 0.01, loc['Z'] * 0.01, loc['Y'] * 0.01],   # (as build_lighting ue_to_gltf_pos)
                       'position_ue': [loc['X'], loc['Y'], loc['Z']], 'draw_scale': lf.get('draw_scale', 1.0),
                       'source_color': [sc.get('R', 1), sc.get('G', 1), sc.get('B', 1), sc.get('A', 1)],
                       'fixed_box': box, 'use_fixed_box': bool(tp.get('bUseFixedRelativeBoundingBox', False)),
                       'screen_percentage_map': dist(tp.get('ScreenPercentageMap')), 'elements': els})
    os.makedirs(out, exist_ok=True)
    json.dump({'map': mapname, 'source': src, 'flares': flares}, open(os.path.join(out, 'lens_flares.json'), 'w'), indent=1)
    print(mapname, len(flares), 'flare(s),', sum(len(f['elements']) for f in flares), 'enabled elements')


if __name__ == '__main__':
    main()
