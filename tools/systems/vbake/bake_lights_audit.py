"""Which lights did the original lightmap bake use, and does lighting.json carry them? (Rendering's runtime RT lightmap
plan, 2026-10-13.) Read-only over the cooked map packages and <render data>/<map>/lighting.json.

Per map:
  - every light component in the level packages (any *LightComponent class): owner actor class, LightGuid, the bake
    flags (bHasLightEverBeenBuiltIntoLightMap, UseDirectLightMap, bForceDynamicLight), CastStaticShadows, channels;
  - the light GUIDs the bake recorded per receiver: StaticMeshComponent FLightMap1D / FLightMap2D LightGuids and the
    BSP ModelComponent elements' lightmap LightGuids (the lights whose contribution is IN that lightmap);
  - lighting.json's lights (StaticLightCollectionActor components only) matched by GUID.
Reports: baked GUIDs with no light in lighting.json (missing), lighting.json lights never referenced by any lightmap
(dynamic-only / not baked), light components outside the StaticLightCollectionActors.

  bake_lights_audit.py <tools/render dir of the head> <render data root> <out.json> [map ...]
"""
import collections, json, os, struct, sys

RENDER_TOOLS = sys.argv[1]
sys.path.insert(0, RENDER_TOOLS)
from ue3obj import Repo, map_packages, tags_to_dict  # noqa: E402

MP = ['MP_ESC_BrokenHope', 'MP_ESC_Remnant', 'MP_IAC_Berth', 'MP_IAC_Rust', 'MP_IAC_Seed', 'MP_IAC_Streets',
      'MP_KON_Molten', 'MP_ORB_Debris', 'MP_UND_Complex', 'MP_UND_Gorge']


def guid_be(b, o):
    return tuple(struct.unpack_from('>4I', b, o))


def guid_prop(v):
    return tuple(int(x) & 0xFFFFFFFF for x in v) if isinstance(v, (list, tuple)) and len(v) == 4 else None


def model_component_guids(t, model_idx):
    """FModelComponent elements' lightmap LightGuids (build_lighting.parse_model_component's layout)."""
    key = struct.pack('>i', model_idx)
    a = t.find(key, 0, 16)
    if a < 4: return None
    o = a + 4
    ne = struct.unpack_from('>i', t, o)[0]; o += 4
    out = []
    for _ in range(ne):
        _pad, comp, mat, nn = struct.unpack_from('>iiii', t, o); o += 16
        o += 2 * nn
        ns = struct.unpack_from('>i', t, o)[0]; o += 4 + 4 * ns
        ni = struct.unpack_from('>i', t, o)[0]; o += 4 + 16 * ni
        lt = struct.unpack_from('>i', t, o)[0]; o += 4
        if lt == 2:
            ng = struct.unpack_from('>i', t, o)[0]; o += 4
            out += [guid_be(t, o + 16 * k) for k in range(ng)]
            o += 16 * ng + 4 + 3 * 20 + 16
        elif lt != 0:
            raise ValueError('BSP lightmap type %d' % lt)
        ng2 = struct.unpack_from('>i', t, o)[0]
        o += 4 + 16 * ng2
    return out


def audit(mapname, render_root):
    pkgs, _ = map_packages(mapname)
    repo = Repo(pkgs)
    comps = []
    baked = collections.Counter()          # GUID -> receivers whose lightmap lists it
    receivers = {'sm_1d': 0, 'sm_2d': 0, 'bsp_elements': 0}
    for pi, p in enumerate(repo.pkgs):
        pr = repo.readers[pi]
        level = next((i + 1 for i, e in enumerate(p.exports) if p.class_name(e) == 'Level'), None)
        models = [i + 1 for i, e in enumerate(p.exports) if p.class_name(e) == 'Model' and e['outer'] == level]
        for i, e in enumerate(p.exports):
            cn = p.class_name(e)
            if cn.endswith('LightComponent'):
                try:
                    d = tags_to_dict(pr.read_object(i + 1)[0])
                except Exception:
                    d = {}
                owner = p.class_name(p.exports[e['outer'] - 1]) if e['outer'] > 0 else None
                ch = {}
                for t in d.get('LightingChannels') or []:
                    if isinstance(t, dict) and 'name' in t: ch[t['name']] = bool(t['value'])
                comps.append({'name': p.obj_name(i + 1), 'path': p.object_path(i + 1), 'class': cn, 'owner': owner,
                              'guid': guid_prop(d.get('LightmapGuid')), 'light_guid': guid_prop(d.get('LightGuid')),
                              'built_into_lightmap': d.get('bHasLightEverBeenBuiltIntoLightMap'),
                              'use_direct_lightmap': d.get('UseDirectLightMap'),
                              'force_dynamic': d.get('bForceDynamicLight'),
                              'cast_static_shadows': d.get('CastStaticShadows'), 'enabled': d.get('bEnabled'),
                              'brightness': d.get('Brightness'), 'radius': d.get('Radius'),
                              'falloff': d.get('FalloffExponent'), 'channels': ch,
                              'other_props': sorted(k for k in d if k not in (
                                  'LightGuid', 'LightmapGuid', 'bHasLightEverBeenBuiltIntoLightMap', 'UseDirectLightMap',
                                  'bForceDynamicLight', 'CastStaticShadows', 'bEnabled', 'Brightness', 'Radius',
                                  'FalloffExponent', 'LightingChannels', 'LightColor'))})
            elif cn == 'StaticMeshComponent':
                try:
                    used = pr.read_object(i + 1)[1]
                except Exception:
                    continue
                nat = p.data[e['serial_offset'] + used:e['serial_offset'] + e['serial_size']]
                if len(nat) <= 40: continue
                lt = struct.unpack_from('>i', nat, 13)[0]
                if lt not in (1, 2): continue
                ng = struct.unpack_from('>i', nat, 17)[0]
                if ng < 0 or 21 + 16 * ng > len(nat): continue
                receivers['sm_1d' if lt == 1 else 'sm_2d'] += 1
                for k in range(ng): baked[guid_be(nat, 21 + 16 * k)] += 1
            elif cn == 'ModelComponent' and models and e['outer'] == level:
                _, used = pr.read_object(i + 1)
                tail = p.data[e['serial_offset'] + used - 8:e['serial_offset'] + e['serial_size']]
                for mi in models:
                    try:
                        g = model_component_guids(tail, mi)
                    except Exception:
                        g = None
                    if g is not None:
                        receivers['bsp_elements'] += 1
                        for x in g: baked[x] += 1
                        break
    lj = json.load(open(os.path.join(render_root, mapname, 'lighting.json'), encoding='utf-8'))
    # lighting.json keeps LightGuid; the lightmaps list the light's LightmapGuid (UE3 keeps both): join via the component
    by_light_guid = {c['light_guid']: c for c in comps if c['light_guid']}
    lj_guids = {}
    lj_unmatched = 0
    for l in lj['lights']:
        c = by_light_guid.get(tuple(int(x) & 0xFFFFFFFF for x in l.get('light_guid') or []))
        if c and c['guid']: lj_guids[c['guid']] = l
        else: lj_unmatched += 1
    by_guid = {c['guid']: c for c in comps if c['guid']}
    missing = [g for g in baked if g not in lj_guids]
    unbaked = [g for g in lj_guids if g not in baked]
    owners = collections.Counter(c['owner'] for c in comps)
    outside = [c for c in comps if c['owner'] != 'StaticLightCollectionActor']
    res = {
        'map': mapname, 'receivers': receivers, 'baked_guids': len(baked),
        'light_components': len(comps), 'owners': dict(owners), 'classes': dict(collections.Counter(c['class'] for c in comps)),
        'lighting_json_lights': len(lj['lights']), 'lighting_json_lights_without_component': lj_unmatched,
        'baked_missing_from_lighting_json': [{'guid': list(g), 'receivers': baked[g],
                                             'component': by_guid.get(g, {}).get('path'),
                                             'class': by_guid.get(g, {}).get('class'),
                                             'owner': by_guid.get(g, {}).get('owner')} for g in missing],
        'lighting_json_not_in_any_lightmap': [{'name': lj_guids[g]['name'], 'class': lj_guids[g]['class'],
                                               'built_into_lightmap': lj_guids[g].get('built_into_lightmap'),
                                               'enabled': lj_guids[g].get('enabled'),
                                               'cast_static_shadows': lj_guids[g].get('cast_static_shadows'),
                                               'brightness': lj_guids[g].get('brightness'),
                                               'channels': lj_guids[g].get('channels')} for g in unbaked],
        'components_outside_collections': [{k: c[k] for k in ('path', 'class', 'owner', 'built_into_lightmap',
                                                                'use_direct_lightmap', 'force_dynamic', 'enabled',
                                                                'brightness', 'radius')} | {'baked': baked.get(c['guid'], 0) if c['guid'] else 0}
                                           for c in outside],
        'flag_counts': {
            'UseDirectLightMap': dict(collections.Counter(str(c['use_direct_lightmap']) for c in comps)),
            'bForceDynamicLight': dict(collections.Counter(str(c['force_dynamic']) for c in comps)),
            'bHasLightEverBeenBuiltIntoLightMap': dict(collections.Counter(str(c['built_into_lightmap']) for c in comps)),
        },
        'other_light_props_seen': dict(collections.Counter(k for c in comps for k in c['other_props'])),
    }
    return res


def main():
    render_root, out = sys.argv[2], sys.argv[3]
    maps = sys.argv[4:] or MP
    R = []
    for m in maps:
        r = audit(m, render_root)
        R.append(r)
        print('%-18s receivers %s; baked GUIDs %d; light comps %d %s; lighting.json %d; baked-but-missing %d; in json but in no lightmap %d; comps outside collections %d'
              % (m, r['receivers'], r['baked_guids'], r['light_components'], r['owners'], r['lighting_json_lights'] - r['lighting_json_lights_without_component'],
                 len(r['baked_missing_from_lighting_json']), len(r['lighting_json_not_in_any_lightmap']),
                 len(r['components_outside_collections'])), flush=True)
    json.dump(R, open(out, 'w', encoding='utf-8'), indent=1)


if __name__ == '__main__':
    main()
