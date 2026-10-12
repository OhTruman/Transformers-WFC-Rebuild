# Find the shader map of a material in a map's ShaderCache blobs by the GUIDs in its native tail (names can be garbled).
import sys, os, re, glob, json
sys.path.insert(0, 'F:/Transformers Rebuild/Rebuild-Rendering/tools/render')
from ue3obj import Repo, map_packages
MAP, MAT = sys.argv[1], sys.argv[2]
pk, _ = map_packages(MAP)
R = Repo(list(reversed(pk)), fallback=['TransGame.xxx', 'TR_AllShader_p.xxx'])
_, t = R.native_tail(MAT)
print('tail', len(t), t[:64].hex())
scm = json.load(open('scmap_%s.json' % MAP))
for f in sorted(glob.glob('sc_%s_*.bin' % MAP)):
    b = open(f, 'rb').read()
    names = [(m.start(), m.group(1).decode()) for m in re.finditer(rb'(?:Material|MaterialInstanceConstant|MaterialInstanceTimeVarying|DecalMaterial) ([A-Za-z0-9_.]+)\x00', b)]
    for o in range(0, min(len(t) - 16, 96), 4):
        g = t[o:o + 16]
        if len(set(g)) < 6: continue
        k = b.find(g)
        while k >= 0:
            nxt = next((n for n in names if n[0] > k), None)
            print(os.path.basename(f), 'tail+%d' % o, g.hex(), '@', hex(k), 'next name:', nxt)
            k = b.find(g, k + 1)
