"""The transforming world actors' baked lighting (HmInterpolatedLightMappedSkeletalMeshComponent, RE INTERPOLATED_LIGHTMAP_RE.md):
two directional texture light maps per actor (capture 0 = untransformed, 1 = transformed), each 3 coefficient LightMapTexture2D +
a scale vector per coefficient + the capture's atlas rect; the base pass lerps the decoded coefficients by the anim's normalized
position. Adds 'interpolated_lightmaps' to a lighting.json: {actor name: [capture0, capture1]}, each capture in the 'lightmaps'
record form (coeffs, scales rgb, coordScale, coordBias). Decoded with RE's native-tail reader (RE-Workspace tools/interp_lightmap.py,
read-only import).

    python build_interp_lightmaps.py <map> <src lighting.json> <out dir>
"""
import json, os, sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, r'F:/Transformers Rebuild/RE-Workspace/tools')
sys.path.insert(0, r'F:/Transformers Rebuild/AssetTools/scripts/wfc')
import interp_lightmap as il   # noqa: E402  (decode(), CLS, DUMP; its ue3pkg)
import ue3pkg                  # noqa: E402


def captures(pkg_name):
    p = ue3pkg.Package(os.path.join(il.DUMP, pkg_name + '.xxx')); d = p.data
    res = {}
    for i, e in enumerate(p.exports, 1):
        if p.class_name(e) != il.CLS: continue
        b = d[e['serial_offset']:e['serial_offset'] + e['serial_size']]
        rec = None
        for k in range(len(b) - 21, -1, -1):   # (the tail starts at the LightingGuid copy: the offset whose decode ends exactly)
            if b[k + 16] != 1: continue
            try:
                r, end = il.decode(p, b[k:])
            except Exception:
                continue
            if k + end == len(b) and (r['captures'] or end == 21): rec = r; break
        if not rec or len(rec['captures']) < 2: continue
        path = p.object_path(i)
        actor = next((s for s in path.split('.') if s.startswith('TnTransformingWorldActor_')), None)
        if not actor: continue
        res[actor] = [{'coeffs': [t['texture'].split('.')[-1] for t in c['textures']],
                       'scales': [t['scale'][:3] for t in c['textures']],
                       'coordScale': c['coordinate_scale'], 'coordBias': c['coordinate_bias']} for c in rec['captures'][:2]]
    return res


def main():
    from ue3obj import map_packages
    mapname, src, out = sys.argv[1], sys.argv[2], sys.argv[3]
    pkgs, _persistent = map_packages(mapname)
    d = json.load(open(src, encoding='utf-8'))
    il_all = {}
    for pk in pkgs:
        name = os.path.splitext(os.path.basename(pk))[0]
        if os.path.exists(os.path.join(il.DUMP, name + '.xxx')): il_all.update(captures(name))
    d['interpolated_lightmaps'] = il_all
    os.makedirs(out, exist_ok=True)
    json.dump(d, open(os.path.join(out, 'lighting.json'), 'w'), indent=0)
    lm = os.path.join(os.path.dirname(os.path.abspath(src)), 'lightmaps')
    miss = sorted({c for caps in il_all.values() for cap in caps for c in cap['coeffs'] if not os.path.exists(os.path.join(lm, c + '.png'))})
    print(mapname, len(il_all), 'interpolated light-mapped actors; atlases missing from', lm, ':', miss)


if __name__ == '__main__':
    main()
