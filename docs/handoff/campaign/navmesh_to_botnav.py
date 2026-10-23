# PROV (until AssetTools' per-chapter bot_nav.json): AssetTools' raw campaign navmesh.json (out/campaign_render/<CH>/) -> the
# BotNav schema (edges start / end / normal / neighbors, clearance_m, headroom_m, flags). No reach-spec links; clearance assumed 3 m.
# usage: python -I navmesh_to_botnav.py <chapter dir> <out bot_nav.json>
import json, sys

src, out = sys.argv[1], sys.argv[2]
j = json.load(open(src + '/navmesh.json', encoding='utf-8'))
cells = []
for c in j['cells']:
    edges = []
    for e in c['edges']:
        n = e.get('normal_ue') or [0.0, 0.0, 0.0]
        edges.append({'start': e['start_gltf'], 'end': e['end_gltf'], 'normal': [n[0], n[2], n[1]],
                      'neighbors': [{'cell': x['cell']} for x in e.get('neighbors', [])], 'border': e.get('border', False)})
    cells.append({'id': c['id'], 'actor': c['actor'], 'vertices_gltf': c['vertices_gltf'], 'centroid_gltf': c['centroid_gltf'],
                  'clearance_m': 3.0, 'headroom_m': 8.0, 'flags': {'vehicle_capable': True, 'tactical': bool(c.get('tactical')),
                  'blocked_at_start': bool(c.get('blocked_at_start'))}, 'edges': edges})
json.dump({'map': j['chapter'], 'status': 'PROV navmesh_to_botnav (no links, clearance 3 m)', 'units': j['units'], 'cells': cells,
           'links': [], 'anchors': []}, open(out, 'w', encoding='utf-8'))
print(len(cells), 'cells ->', out)
