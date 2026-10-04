"""Materials of a map's actors that are not in world.glb (render_index actors_by_level: skeletal actors such as the
frontend battle vignette's ships).

    python scene_materials.py <Map>      -> material object paths, one per line (nothing for maps without the table)

Cooked SkeletalMesh materials are native (not tagged properties), so the glTF slot names (umodel short names) are
resolved in the map's package index, preferring the mesh's own package family.
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue3obj import Repo, map_packages, VS_MAPS  # noqa: E402

KEY = 'actors_by_level (names = Matinee / Kismet targets; world_glb_node = node name in world.glb)'
CONTENT = 'F:/Transformers Rebuild/ExtractedAssets/content'


def main():
    mapname = sys.argv[1]
    ri = os.path.join(VS_MAPS, mapname, 'render_index.json')
    if not os.path.exists(ri):
        return
    table = json.load(open(ri, encoding='utf-8')).get(KEY) or {}
    gltfs = {e['mesh']: e.get('gltf') for lv in table.values() for e in lv
             if e.get('class') == 'HmSkeletalMeshActor' and e.get('mesh')}
    if not gltfs:
        return
    repo = Repo(map_packages(mapname)[0], fallback=['TransGame.xxx'])
    byname = {}
    for path in repo.index:
        if (repo.cls(path) or '') in ('Material', 'MaterialInstanceConstant'):
            byname.setdefault(path.split('.')[-1], []).append(path)
    out = []
    for mesh, g in sorted(gltfs.items()):
        if not g or not g.startswith('content/'):
            continue
        gp = os.path.join(CONTENT, g[len('content/'):])
        if not os.path.exists(gp):
            continue
        pkg = mesh.split('.')[0].lower()
        for mt in json.load(open(gp, encoding='utf-8')).get('materials') or []:
            cands = byname.get((mt.get('name') or '').lower(), [])
            pick = next((c for c in cands if c.startswith(pkg)), cands[0] if cands else None)
            if pick:
                h = repo.find(pick)
                full = repo.pkgs[h[0]].object_path(h[1])
                if full not in out:
                    out.append(full)
    print('\n'.join(out))


if __name__ == '__main__':
    main()
