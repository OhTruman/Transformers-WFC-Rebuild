"""The materials of a map's transforming world actors (TnTransformingWorldActor: the MP doors / bridges; AssetTools
manifests/maps/mp_transforming_actors.json, their skeletal meshes' slot materials): extra materials for build_materials.py.

    python transforming_materials.py MAP
"""
import json, os, sys

MANIFEST = r'F:/Transformers Rebuild/AssetTools/manifests/maps/mp_transforming_actors.json'


def main():
    m = sys.argv[1]
    if not os.path.exists(MANIFEST): return
    d = json.load(open(MANIFEST, encoding='utf-8'))
    meshes = {a['mesh'] for a in d['actors'] if a['map'] == m}
    for name in sorted({x['material'] for k in meshes for x in d['meshes'].get(k, {}).get('materials', [])}):
        print(name)


if __name__ == '__main__':
    main()
