# Every PS in a map's ShaderCache exports whose shader map's friendly name matches a substring (scmap.py's attribution
# without the lightmap filter): scall_<map>_<tag>.json [{pkg, export, off, type, materials, guid, consts}]
import sys, os, re, json, struct, bisect, collections
sys.path.insert(0, 'F:/Transformers Rebuild/Rebuild-Rendering/tools/render')
from pk import open_pkg
import xenos_dis as X
from ue3obj import map_packages
mapname, pat, tag = sys.argv[1], sys.argv[2].lower(), sys.argv[3]   # EXPERIMENTAL: argv[4] = comma list of packages
pkgs = [os.path.join('F:/Transformers Rebuild/Game Dump/TransGame/CookedXenon', q) for q in sys.argv[4].split(',')]
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
        maps_start = max(q.off + q.uc_off + q.uc_size for q in allsh) if allsh else 0
        hit = False
        for s in allsh:
            if s.kind != 'ps': continue
            o = s.off
            g = b[o - 26:o - 10]
            ti, tn = struct.unpack_from('>ii', b, o - 34)
            tname = p.names[ti] if 0 <= ti < len(p.names) else '?%d' % ti
            mats = set()
            k = b.find(g)
            while k >= 0:
                if k != o - 26 and k > maps_start:
                    j = bisect.bisect_left(npos, k)
                    if j < len(names): mats.add(names[j][1])
                k = b.find(g, k + 1)
            if not any(pat in m.lower() for m in mats): continue
            hit = True
            out.append({'pkg': f, 'export': p.object_path(i + 1), 'off': o, 'type': tname, 'materials': sorted(mats),
                        'guid': g.hex(), 'consts': sorted(c['name'] for c in s.consts)})
        if hit: open('sc_%s_%s.bin' % (mapname, p.object_path(i + 1).split('.')[-1]), 'wb').write(b)
json.dump(out, open('scall_%s_%s.json' % (mapname, tag), 'w'), indent=0)
print(mapname, len(out), collections.Counter(x['type'] for x in out).most_common(20))
print(collections.Counter(m for x in out for m in x['materials']).most_common(10))
