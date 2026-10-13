import json, os
R = 'F:/Transformers Rebuild/Rebuild/work/render/'
hd = json.load(open('F:/Transformers Rebuild/AssetTools/out/hd/pack/hd_index.json', encoding='utf-8'))
hdb = {r['object_path'].lower(): r for r in hd['rows']}
wl = {r['tex'].lower() for r in json.load(open('docs/handoff/SYSTEMS_M6_WORKLIST.json', encoding='utf-8'))['rows']}
BPP = {'PF_DXT1': 0.5, 'PF_DXT3': 1.0, 'PF_DXT5': 1.0, 'PF_BC5': 1.0, 'PF_G8': 1.0, 'PF_V8U8': 2.0}
MIP = 4.0 / 3.0
print('%-18s %6s %9s %9s %9s %9s' % ('map', 'texs', 'orig MB', 'HD MB', '+rough MB', 'Enh MB'))
worst = 0
for mp in sorted(d for d in os.listdir(R) if d.startswith('MP_')):
    mats = json.load(open(R + mp + '/materials_glsl.json', encoding='utf-8'))
    seen = {}
    for m in mats.values():
        for t in m.get('info', {}).get('textures', []):
            if t.get('kind', '2d') != '2d' or not t.get('size') or not t['size'][0]:
                continue
            seen[t['object'].lower()] = t
    o = h = r = 0.0
    for k, t in seen.items():
        w, hh = t['size']; b = w * hh * BPP.get(t.get('format'), 4.0) * MIP
        o += b
        hr = hdb.get(k)
        hb = hr['bytes'] if hr else b
        h += hb
        if k in wl:
            sw, sh = (hr['size'] if hr else (w, hh))
            r += sw * sh * 0.5 * MIP                                  # BC4: 0.5 byte / texel, full mip chain
    worst = max(worst, h + r - o)
    print('%-18s %6d %9.0f %9.0f %9.0f %9.0f' % (mp, len(seen), o / 2**20, h / 2**20, r / 2**20, (h + r) / 2**20))
print('largest extra VRAM vs the originals (Enhanced): %.0f MB' % (worst / 2**20))
