# Map every lightmapped base-pass PS in a map's ShaderCache exports to its material (shader-map friendly name)
# and shader type name. Output: scmap_<map>.json [{pkg, export, off, type, material, guid}] + sc_<map>_<i>.bin blobs.
import sys, os, re, json, struct, bisect
sys.path.insert(0, 'F:/Transformers Rebuild/Rebuild-Systems/work/refresh_af4d0731/tools/render')   # (Systems copy: every PS, not only lightmapped)
from pk import open_pkg
import xenos_dis as X
from ue3obj import map_packages
mapname = sys.argv[1]
pkgs, _ = map_packages(mapname)
out = []
for f in pkgs:
    p = open_pkg(f)
    for i, e in enumerate(p.exports):
        if p.class_name(e) != 'ShaderCache': continue
        b = p.data[e['serial_offset']:e['serial_offset'] + e['serial_size']]
        names = [(m.start(), m.group(1).decode()) for m in
                 re.finditer(rb'(?:Material|MaterialInstanceConstant|MaterialInstanceTimeVarying|DecalMaterial) ([A-Za-z0-9_.]+)\x00', b)]
        npos = [n[0] for n in names]
        allsh = list(X.shaders(b))
        maps_start = max(q.off + q.uc_off + q.uc_size for q in allsh) if allsh else 0   # shader maps follow the shaders
        for s in X.shaders(b):
            if s.kind != 'ps': continue
            cn = {c['name'] for c in s.consts}
            o = s.off
            ti0 = struct.unpack_from('>i', b, o - 34)[0]
            vlm = 0 <= ti0 < len(p.names) and 'VertexLightMap' in p.names[ti0] and 'BasePassPixel' in p.names[ti0]
            g = b[o - 26:o - 10]
            ti, tn = struct.unpack_from('>ii', b, o - 34)
            tname = p.names[ti] if 0 <= ti < len(p.names) else '?%d' % ti
            mats = set()
            k = b.find(g)
            while k >= 0:
                if k != o - 26 and k > maps_start:
                    j = bisect.bisect_left(npos, k)      # a shader map's friendly name follows its shader list
                    if j < len(names): mats.add(names[j][1])
                k = b.find(g, k + 1)
            out.append({'pkg': f, 'export': p.object_path(i + 1), 'off': o, 'type': tname, 'materials': sorted(mats),
                        'guid': g.hex(), 'consts': sorted(cn)})
        open('sc_%s_%s.bin' % (mapname, p.object_path(i + 1).split('.')[-1]), 'wb').write(b)   # (in this script's cwd)
json.dump(out, open('scmap_%s.json' % mapname, 'w'), indent=0)
import collections
print(mapname, len(out), 'LM PS;', collections.Counter(x['type'] for x in out).most_common(8))
print('unmapped', sum(1 for x in out if not x['materials']), 'multi', sum(1 for x in out if len(x['materials']) > 1))
