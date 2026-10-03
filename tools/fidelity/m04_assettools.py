"""Run AssetTools' validate_m04.py READ-ONLY and copy the authored inventory Experimental's M04 gate needs.

AssetTools is a read-only reference during parallel agent work, so validate_m04 runs here with its log
directory redirected into <out> and bytecode caching disabled (nothing is written under AssetTools).

    python m04_assettools.py <AssetTools root> <out dir>

Writes <out>/validate_m04.json (validator checks), <out>/authored.json (counts + every item list the gate
classifies), and prints a one-line summary. Exit 0 even when the validator fails: data-side failures are
AssetTools findings, reported by the gate, never product FAILs.
"""
import json, os, sys

sys.dont_write_bytecode = True
AT, OUT = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
os.makedirs(OUT, exist_ok=True)
WFC = os.path.join(AT, 'scripts', 'wfc')
MANI = os.path.join(AT, 'manifests')
sys.path.insert(0, WFC)


def J(p):
    with open(p, encoding='utf-8') as f:
        return json.load(f)


# ---- 1. validator (logs redirected) ----
val = {'ran': False}
try:
    import validate_m04 as V
    V.LOGS = OUT
    try:
        V.main()
    except SystemExit:
        pass
    val = J(os.path.join(OUT, 'validate_m04.json'))
    val['ran'] = True
except Exception as e:  # validator crash = tool problem, reported as such
    val = {'ran': False, 'error': '%s: %s' % (type(e).__name__, e)}
    json.dump(val, open(os.path.join(OUT, 'validate_m04.json'), 'w'), indent=1)

# ---- 2. authored item lists ----
comp = J(os.path.join(MANI, 'mp_iac_streets_complete.json'))
inv = J(os.path.join(MANI, 'streets_actor_inventory.json'))
diff = J(os.path.join(MANI, 'streets_rebuild_diff.json'))
movers = J(os.path.join(MANI, 'streets_movers.json'))
lit = J(os.path.join(MANI, 'streets_lighting_audit.json'))
pref = J(os.path.join(MANI, 'streets_prefabs.json'))
c = comp['counts']
lm_types = c['lightmaps']
authored = {
    'source': {'assettools': AT, 'manifest': 'manifests/mp_iac_streets_complete.json', 'generated': comp.get('generated')},
    'counts': {
        'sublevels': 1 + len(comp['hierarchy']['streaming_levels']),
        'placed_actors': sum(1 for a in inv['actors'] if a['class'] != 'ModelComponent'),
        'bsp_render_components': c['bsp']['model_components'],
        'components': c['placed_components'],
        'bsp_nodes': c['bsp']['nodes'],
        'bsp_surfaces': c['bsp']['surfaces'],
        'bsp_triangles': c['bsp']['triangles_in_render_elements'],
        'bsp_render_elements': c['bsp']['render_elements'],
        'placed_meshes': c['static_meshes']['placed'],
        'domination_totems': c['objectives']['TnDominationPoint'],
        'movers': c['movers'],
        'destructibles': c['destructibles'],
        'decals': c['decals'],
        'lights': sum(c['lights'].values()),
        'lightmapped_components': sum(v for k, v in lm_types.items() if k.startswith('texture')),
        'vertex_lit_components': sum(v for k, v in lm_types.items() if k.startswith('vertex')),
        'non_baked_components': sum(v for k, v in lm_types.items() if k.startswith('none')),
        'particle_effects': c['fx']['particle_components'],
        'level_emitters': c['fx']['level_emitters'],
        'pickup_fx_components': c['fx']['pickup_fx_components'],
        'ambient_emitters': c['audio']['point'] + c['audio']['line'] + c['audio']['volume'],
        'ambient_point': c['audio']['point'], 'ambient_line': c['audio']['line'], 'ambient_volume': c['audio']['volume'],
        'zones': c['audio']['zones'],
        'reverb_presets': c['audio']['reverb_presets'],
        'oneshot_pools': c['kismet'].get('MP_IAC_Streets_AUDIO_m', {}).get('SeqAct_PlayPlayerPositionalSound', 0),
        'pickups': sum(c['pickups'].values()),
        'pickups_by_class': c['pickups'],
        'player_starts': sum(c['starts'].values()),
        'objective_actors': sum(c['objectives'].values()),
        'objectives_by_class': c['objectives'],
        'prefab_containers': c['prefabs']['instances'],
        'prefab_members': c['prefabs']['members'],
        'prefab_uncovered_members': c['prefabs']['uncovered_members'],
    },
    'assettools_rebuild_comparison': comp.get('current_rebuild_comparison'),
    'non_matching': diff['non_matching'],
    'movers': [{k: m.get(k) for k in ('actor', 'class', 'physics', 'RotationRate', 'bHidden', 'mesh', 'matinee', 'classification', 'transform')}
               for m in movers['movers']],
    'matinee': [{'action': m['action'], 'looping': m['looping'], 'length_s': m['length_s'],
                 'actors': [{k: a.get(k) for k in ('actor', 'mesh', 'location_gltf', 'location_ue', 'yaw_deg')} for a in m['actors']]}
                for m in movers['matinee']],
    'texture_lightmapped': sorted(k for k, v in lit['components'].items() if v.get('type') == 2),
    'texture_lightmapped_unbound': sorted(k for k, v in lit['components'].items() if v.get('type') == 2 and not v.get('bound_in_lightmaps_json')),
    'prefabs_uncovered': pref.get('uncovered_members'),
    'presentation_vs_mode_logic': comp.get('presentation_vs_mode_logic'),
    'why_incomplete': comp.get('why_the_rebuild_reads_as_incomplete'),
}
json.dump(authored, open(os.path.join(OUT, 'authored.json'), 'w'), indent=1)
chk = val.get('checks') or {}
print('validate_m04: ran=%s checks=%d failures=%d -> %s' % (val.get('ran'), sum(v[0] for v in chk.values()),
      sum(v[1] for v in chk.values()), OUT))
